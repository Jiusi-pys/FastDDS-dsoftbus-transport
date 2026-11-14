"""Independent dual-board UA/DDS acceptance entry point for D5."""
import argparse
import asyncio
import json
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback
import psutil
import run_ua_complete_acceptance as d4
import run_ua_acceptance as d1
import run_request_reply_acceptance as d3
from build_ua_device import ROOT,snapshot,validate,sha
from build_dds_ua_gateway import verify_client
from ua_device_support import PAIR,Deployment,Session,Forwarder,ReverseForwarder,demand


def execution_snapshot():
    return {**snapshot(),**{str(p.relative_to(ROOT)):sha(p) for p in (Path(__file__).resolve(),ROOT/'scripts/standards/ua_device_support.py')}}


class Job:
    def __init__(self,case,record):self.case,self.record=case,record;self.returncode=None
    def poll(self):
        log=d4.text(self.record['log'])
        match=re.search(r'D4_PEER_CLOSED exit=(\d+) owners=(\d+)',log)
        if match:self.returncode=int(match[1]) or (1 if int(match[2]) else 0)
        return self.returncode
    def wait(self,timeout=12):
        end=time.monotonic()+timeout
        while self.poll() is None:
            demand(time.monotonic()<end,'native job shutdown deadline');self.case.session.sync();time.sleep(.02)
        return self.returncode


class ControlledProcess:
    def __init__(self,process,session):self.process,self.session=process,session
    def __getattr__(self,name):return getattr(self.process,name)
    def wait(self,timeout=None):
        # Existing restart oracles touch stop and call wait directly; flush
        # control files before blocking on the actual native process receipt.
        self.session.sync();return self.process.wait(timeout)


def collect_local(process,record):
    """Collect an owned UA OS child, including bounded failure recovery."""
    try:
        try:process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            if process.poll() is None:
                try:process.terminate();record['forced_cleanup']=True
                except OSError:
                    if process.poll() is None:process.kill();record['forced_cleanup']=True
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait(timeout=5)
        demand(process.returncode is not None,'local UA process exit not collected')
    finally:
        # None means uncollected; never invent an OS exit code for an error.
        record['exit_code']=process.returncode


class CompleteCase(d4.Case):
    def __init__(self,path,deployment,board,gateway_abi,peer_abi,rep,mode):
        bundle=dict(abi=gateway_abi,client_python=verify_client(deployment.bundles[gateway_abi]['client_manifest'])['python'],runtime_dirs=[],service='d4_service',history='d4_history',peer='d4_peer')
        self.session=Session(path,deployment,board,gateway_abi,peer_abi)
        # Session created the case directory; retain the exact host case behavior.
        self.path,self.bundle,self.rep=path,bundle,rep;self.children=[];self.modules=[];self.calls=[];self.indices={}
        self.domain=219;self.name='D5_'+self.session.id;self.pending_peers=[];self.peer_group=None;self.mode=mode
    def start(self,name,argv,bundle=None,ua=False):
        folder=self.path/name;folder.mkdir();args=list(map(str,argv(folder)))
        if ua:
            if name.startswith('fixture'):
                port=int(args[args.index('--port')+1]);self.session.forwarders.append(ReverseForwarder(self.session.deployment.boards[self.session.gateway_board],port))
            stream=(folder/'process.log').open('wb');process=subprocess.Popen(args,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT)
            record=dict(name=name,argv=args,abi='UA-only',ua_only=True,runtime_dirs=[],directory=str(folder),log=str(folder/'process.log'),pid=process.pid,forced_cleanup=False)
        elif name.startswith('peer'):
            index=len(self.pending_peers);remote=self.session.control(folder,self.session.peer_board,self.session.root+'/peers/'+str(index),mirror=True)
            record=dict(name=name,argv=args,abi=self.session.peer_abi,directory=str(folder),log=str(folder/'process.log'),native_job=index,forced_cleanup=False,remote=remote)
            self.pending_peers.append(record);process=Job(self,record);stream=None
        else:
            remote=self.session.control(folder,self.session.gateway_board)
            config=Path(args[1]);content=config.read_text(encoding='utf-8').replace(str(folder/'stop'),remote+'/stop')
            self.session.deployment.boards[self.session.gateway_board].put(remote+'/profile.cfg',content.encode())
            if name.startswith('history'):
                port=int(next(line.split('=',1)[1] for line in content.splitlines() if line.startswith('port=')))
                if not any(isinstance(f,Forwarder) and f.port==port for f in self.session.forwarders):self.session.forwarders.append(Forwarder(self.session.deployment.boards[self.session.gateway_board],port))
            process,stream,record=self.session.start(name,args[0],[remote+'/profile.cfg'])
            process=ControlledProcess(process,self.session)
        self.children.append((process,stream,record));self.indices[name]=0;return record
    def ensure_peer(self):
        if self.pending_peers and self.peer_group is None:
            mode='rpc' if self.mode in ('services','capacity') else 'publish'
            self.peer_group=self.session.start('peer-group','d4_peer',[mode,219,self.name,self.rep,self.session.root+'/peers',len(self.pending_peers)],peer=True)
            self.session.gate()
    async def wait(self,predicate,label,timeout=12):
        self.ensure_peer();deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            self.session.sync()
            if predicate():return
            if not all(p.poll() is None or (r.get('stopped_expected') and p.returncode==0) for p,_,r in self.children):
                # poll may observe a successful child exit after the completion
                # predicate just observed it running. Recheck that same oracle;
                # do not accept a completed child without its required result.
                if predicate():return
                raise RuntimeError('early D5 child exit: '+label)
            await asyncio.sleep(.01)
        raise RuntimeError('D5 timeout: '+label)
    def audit(self):
        self.session.audit();self.modules=list(self.session.modules)
        for p,_,record in self.children:
            if record.get('ua_only') and p.poll() is None:
                for module in psutil.Process(p.pid).memory_maps():
                    demand(not Path(module.path).name.lower().startswith(('fastdds','fastrtps','fastcdr','foonathan')),'independent UA loaded DDS')
    def finish(self):
        error=None
        order=lambda r:0 if 'native_job' in r else 1 if r['name'].startswith(('service','history')) else 2 if r['name'].startswith('fixture') else 3
        for process,stream,record in sorted(self.children,key=lambda row:order(row[2])):
            try:
                if process.poll() is None:
                    (Path(record['directory'])/'stop').touch();self.session.sync()
                if isinstance(process,Job):record['exit_code']=process.wait(12)
                elif not record.get('ua_only') and not record.get('collected'):
                    self.session.stop((process,stream,record));stream=None
            except Exception as failure:
                record.setdefault('exit_code',process.returncode)
                record['cleanup_error']=str(failure);error=error or failure
            finally:
                # Control-file/SSH failures must not bypass local OS ownership.
                if record.get('ua_only'):
                    try:collect_local(process,record)
                    except Exception as failure:record['cleanup_error']=str(failure);error=error or failure
                if stream and not stream.closed:
                    try:stream.close()
                    except Exception as failure:record['cleanup_error']=str(failure);error=error or failure
        try:
            if self.peer_group:self.session.stop(self.peer_group)
        except Exception as failure:error=error or failure
        try:self.session.cleanup()
        except Exception as failure:error=error or failure
        (self.path/'native-processes.json').write_text(json.dumps([r for _,_,r in self.session.native],indent=2)+'\n',encoding='utf-8')
        rows=[r for _,_,r in self.children]
        if error:
            (self.path/'cleanup-error.txt').write_text(str(error),encoding='utf-8')
            failure=dict(status='FAIL',cleanup_error=str(error),processes=rows,modules=self.modules)
            (self.path/'result.json').write_text(json.dumps(failure,indent=2)+'\n',encoding='utf-8')
            raise RuntimeError('D5 controller cleanup failed: '+str(error)) from error
        return rows


def config_native(session,name,fields,peer=False):
    folder=session.path/name;host=session.peer_board if peer else session.gateway_board
    remote=session.control(folder,host);fields=dict(fields)
    for key in ('stop','stop_file'):
        if key in fields:fields[key]=remote+'/stop'
    session.deployment.boards[host].put(remote+'/profile.cfg',(''.join(str(k)+'='+str(v)+'\n' for k,v in fields.items())).encode())
    return folder,remote


async def d2_case(path,deployment,board,ga,pa,rep,mode):
    session=Session(path,deployment,board,ga,pa);report=dict(status='FAIL',mode=mode);client=None
    try:
        port=deployment.port(board);topic='D5_'+session.id
        peer_dir=session.control(path/'peer',session.peer_board)
        peer=session.start('peer','d2_peer',[219,topic,rep,peer_dir],peer=True)
        # Preserve the independent host oracle's peer.log contract byte for byte.
        (path/'peer/peer.log').hardlink_to(Path(peer[2]['log']))
        folder,remote=config_native(session,'gateway',dict(direction='dds_to_ua',domain=219,representation=rep,topic=topic,port=port,max_instances=2 if mode=='capacity' else 16,run_ms=90000,stop_file='stop'))
        gateway=session.start('gateway','d2_gateway',[remote+'/profile.cfg']);session.gate()
        session.wait(lambda:'D2_READY' in gateway[0].text and 'D2_PEER_READY' in peer[0].text,'D2 DDS and UA startup');session.audit()
        forward=Forwarder(deployment.boards[board],port);session.forwarders.append(forward)
        python=verify_client(deployment.bundles[ga]['client_manifest'])['python'];receipt=path/'ua-result.json'
        argv=[python,'-B',str(ROOT/'scripts/standards/d2_ua_client.py'),'--url','opc.tcp://127.0.0.1:'+str(port),'--domain','219','--topic',topic,'--control',str(path/'peer'),'--receipt',str(receipt),'--mode',mode]
        with (path/'ua-client.log').open('wb') as log:
            client=subprocess.Popen(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            session.wait(lambda:client.poll() is not None,'independent D2 semantic oracle',40)
        demand(client.returncode==0 and json.loads(receipt.read_text(encoding='utf-8'))['status']=='PASS','D2 semantic client')
        report['client']=dict(argv=argv,exit_code=client.returncode)
        session.stop(gateway);session.stop(peer);report['status']='PASS'
    except Exception as error:report['error']=str(error);raise
    finally:
        try:session.cleanup()
        except Exception as error:report.update(status='FAIL',cleanup_error=str(error));raise
        finally:
            if client and client.poll() is None:client.kill();client.wait();report['status']='FAIL';report['forced_client_cleanup']=True
            report.update(processes=[r for _,_,r in session.native],modules=session.modules)
            (path/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


def d3_case(path,deployment,board,ga,pa,rep,role):
    session=Session(path,deployment,board,ga,pa);report=dict(status='FAIL',role=role)
    try:
        service='D5_'+session.id
        appdir=session.control(path/'actor',board);peerdir=session.control(path/'peer',session.peer_board)
        actor=session.start('actor','d3_actor',[role,219,service,rep,'reverse',appdir,1,16,4])
        peer=session.start('peer','d3_peer',['server' if role=='client' else 'client',219,service,rep,'reverse',peerdir,1,4],peer=True)
        session.gate();session.wait(lambda:all('D3_READY ' in row[0].text for row in (actor,peer)),'D3 all four endpoint directions');session.audit()
        for folder in (path/'actor',path/'peer'):(folder/'go').touch()
        session.wait(lambda:all('D3_DONE ' in row[0].text for row in (actor,peer)),'D3 independent exchange')
        if role=='client':
            request=d3.messages(actor[0].text,'D3_SUBMITTED');observed=d3.messages(peer[0].text,'D3_PEER_REQUEST');reply=d3.messages(actor[0].text,'D3_RESULT');received=d3.messages(peer[0].text,'D3_PEER_REPLY')
        else:
            request=d3.messages(peer[0].text,'D3_PEER_SUBMITTED');observed=d3.messages(actor[0].text,'D3_RECEIVED');reply=d3.messages(actor[0].text,'D3_REPLIED');received=d3.messages(peer[0].text,'D3_PEER_RESULT')
        demand(len(request)==len(reply)==4 and request==observed and reply==received and set(request)==set(reply),'D3 full identity/body correlation')
        session.stop(actor if role=='client' else peer);session.stop(peer if role=='client' else actor)
        report.update(status='PASS',requests=request,replies=reply)
    finally:
        try:session.cleanup()
        except Exception as error:report.update(status='FAIL',cleanup_error=str(error));raise
        finally:
            report.update(processes=[r for _,_,r in session.native],modules=session.modules)
            (path/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


def d1_case(path,deployment,board,ga,pa,rep):
    session=Session(path,deployment,board,ga,pa);report=dict(status='FAIL');fixtures=[]
    try:
        port=deployment.port(board);topic='D5_'+session.id
        peer_remote=session.control(path/'peer',session.peer_board)
        peer=session.start('peer','d1_peer',[219,topic,rep,peer_remote+'/stop',90000],peer=True)
        folder=path/'gateway';remote=session.control(folder,board)
        config=folder/'profile.cfg';d1.config(config,port,219,topic,rep,remote+'/stop')
        deployment.boards[board].put(remote+'/profile.cfg',config.read_bytes())
        gateway=session.start('gateway','d1_gateway',[remote+'/profile.cfg']);session.gate()
        session.wait(lambda:'D1_CONNECT_FAILED' in gateway[0].text,'D1 unavailable upstream observed');session.audit()
        expected={(42,7),(43,8)}
        def seen():
            found=set()
            for key,value,label,payload in re.findall(r'D1_SAMPLE key=(\d+) value=(\d+) label=([0-9a-f]*) payload_size=(\d+)',peer[0].text):
                key,value=int(key),int(value)
                demand(key in (42,43) and label==('sensorA' if key==42 else 'sensorB').encode().hex() and payload=='0','D1 key/label/payload')
                demand((key,value) in expected,'D1 unexpected value');found.add((key,value))
            return found
        for phase in (1,2):
            name='fixture-'+str(phase);native=session.control(path/name,board)
            row=deployment.launch(board,ga,'d1_fixture',[port,native],path/name/'process.log');fixtures.append(row)
            session.wait(lambda:'D1_UA_SERVER_READY version=1.4.21' in row[0].text,'independent native UA fixture')
            session.wait(lambda:f'D1_SUBSCRIBED generation={phase} ' in gateway[0].text,'D1 new UA subscription')
            session.wait(lambda:{(42,7),(43,8)}<=seen(),'D1 initial monitored values')
            values=[(1001,0),(1002,4294967295),(1001,42),(1002,123456789)] if phase==1 else [(1001,77),(1002,88)]
            for serial,(node,value) in enumerate(values,1):
                key=42 if node==1001 else 43;expected.add((key,value))
                (path/name/('command-'+str(serial))).write_text(f'{serial} {node} {value}\n',encoding='ascii')
                session.wait(lambda:(key,value) in seen(),'D1 exact native DDS value')
            (path/name/'stop').touch();session.sync();code=row[0].wait(timeout=12);row[1].close();row[2]['exit_code']=code
            demand(code==0 and 'D1_UA_SERVER_CLOSED exit=0' in row[0].text,'independent UA fixture close')
        session.stop(gateway);session.stop(peer)
        demand('D1_RESULT status=PASS' in gateway[0].text and 'ua_owners=0 dds_owners=0 sample_owners=0' in gateway[0].text,'D1 ownership')
        report.update(status='PASS',expected_samples=sorted(expected),observed_samples=sorted(seen()))
    finally:
        try:session.cleanup()
        except Exception as error:report.update(status='FAIL',cleanup_error=str(error));raise
        finally:
            for row in fixtures:
                if not row[1].closed:
                    try:row[2]['exit_code']=row[0].wait(timeout=8)
                    except subprocess.TimeoutExpired:row[2]['forced_cleanup']=True;row[0].terminate();row[2]['exit_code']=row[0].wait(timeout=5)
                    row[1].close()
            report.update(processes=[r for _,_,r in session.native],independent_ua=[r for _,_,r in fixtures],modules=session.modules)
            (path/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


async def main(args):
    out=args.out.resolve();demand(not out.exists() and out.is_relative_to((ROOT/'build').resolve()),'new D5 evidence output required')
    bundles={r['bundle']['abi']:r['bundle'] for r in [json.loads(p.read_text(encoding='utf-8')) for p in args.build_summary]}
    demand(set(bundles)=={'3.6.2','2.14.5'},'both exact native ABI builds required')
    before={abi:validate(bundle) for abi,bundle in bundles.items()};sources=execution_snapshot();deployment=None
    report=dict(status='FAIL',scope='targeted development' if args.targeted else 'bounded D5 dual-board acceptance',argv=sys.argv,hdc=str(args.hdc.resolve()),hdc_sha256=sha(args.hdc),build_summaries=[str(p.resolve()) for p in args.build_summary],lanes=[])
    try:
        mapping=json.loads(args.ssh_map.read_text(encoding='utf-8-sig')) if args.ssh_map else None
        deployment=Deployment(out,bundles,args.hdc,mapping)
        versions=[args.version] if args.version else list(bundles)
        matrix=[(ga,pa,board,rep) for ga in versions for pa in versions for board in ([args.gateway_board] if args.gateway_board else PAIR) for rep in ([args.representation] if args.representation is not None else ([0,2] if ga==pa else [0])) if ga==pa or args.representation!=2]
        for ga,pa,board,rep in matrix:
            name=f'{ga}-{pa}-{board}-rep{rep}';lane=out/name;lane.mkdir();entry=dict(name=name,gateway_abi=ga,peer_abi=pa,gateway_board=board,peer_board=next(p for p in PAIR if p!=board),representation=rep,cases=[]);report['lanes'].append(entry)
            phases=args.targeted or ['d1','d2','d2-capacity','d3-client','d3-server','services','history','capacity','expiry','restart']
            for phase in phases:
                path=lane/phase
                if phase=='d1':result=d1_case(path,deployment,board,ga,pa,rep)
                elif phase.startswith('d2'):result=await d2_case(path,deployment,board,ga,pa,rep,'capacity' if phase=='d2-capacity' else 'normal')
                elif phase.startswith('d3'):result=d3_case(path,deployment,board,ga,pa,rep,phase.split('-')[1])
                else:
                    case=CompleteCase(path,deployment,board,ga,pa,rep,phase)
                    original_port=d4.port;d4.port=lambda:deployment.port(board)
                    try:result=await d4.run_case(case,{abi:case.bundle for abi in bundles},phase)
                    finally:d4.port=original_port
                demand(result['status']=='PASS','native phase failed');entry['cases'].append(dict(phase=phase,status='PASS',result=str(path/'result.json')))
                (out/'progress.json').write_text(json.dumps(report['lanes'],indent=2)+'\n',encoding='utf-8')
                print('D5_CASE PASS',name,phase,flush=True)
        deployment.posthash();demand(execution_snapshot()==sources and {a:validate(b) for a,b in bundles.items()}==before and sha(args.hdc)==report['hdc_sha256'],'D5 source/dependencies changed during acceptance')
        report.update(status='PASS',source_unchanged=True,source_snapshot=sources)
    except Exception as error:report.update(error=str(error),traceback=traceback.format_exc());raise
    finally:
        if deployment:deployment.close()
        if out.exists():(out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('D5_ACCEPTANCE PASS lanes='+str(len(report['lanes'])),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-summary',type=Path,action='append',required=True)
    parser.add_argument('--out',type=Path,required=True);parser.add_argument('--hdc',type=Path,required=True);parser.add_argument('--ssh-map',type=Path)
    parser.add_argument('--version',choices=['2.14.5','3.6.2']);parser.add_argument('--gateway-board',choices=PAIR);parser.add_argument('--representation',type=int,choices=[0,2])
    parser.add_argument('--targeted',nargs='+',choices=['d1','d2','d2-capacity','d3-client','d3-server','services','history','capacity','expiry','restart'])
    asyncio.run(main(parser.parse_args()))
