"""D5 native deployment and byte-only UA forwarding. DDS stays on DSoftBus."""
import io
import json
from pathlib import Path
import re
import select
import shlex
import socket
import subprocess
import tarfile
import threading
import time
import uuid
from web_device_support import Board as WebBoard, Deployment as WebDeployment, Forwarder, Process
from build_ua_device import ROOT, TARGETS, sha, validate

PAIR=('100.97.6.8','100.97.143.74')


class LogStreams:
    def __init__(self,*streams):self.streams=streams
    def close(self):
        for stream in self.streams:stream.close()
    @property
    def closed(self):return all(s.closed for s in self.streams)


def demand(ok,message):
    if not ok:raise RuntimeError(message)


class Board(WebBoard):
    def mkdir(self,path):
        demand(path.startswith('/data/local/tmp/d5_') and '..' not in path,'D5 remote root')
        self.run('mkdir -p '+shlex.quote(path)+'; chmod 700 '+shlex.quote(path))


class ReverseForwarder:
    """A board-loopback listener carrying only UA bytes to the host fixture."""
    def __init__(self,board,port):
        self.board,self.port=board,port;self.stop=threading.Event();self.threads=[];self.lock=threading.Lock();self.channels=[]
        def incoming(channel,origin,address):
            def copy():
                connection=None
                try:
                    connection=socket.create_connection(('127.0.0.1',port),timeout=6)
                    with self.lock:self.channels.append((channel,connection))
                    while not self.stop.is_set():
                        ready,_,_=select.select([channel,connection],[],[],.2)
                        if channel in ready:
                            data=channel.recv(65536)
                            if not data:break
                            connection.sendall(data)
                        if connection in ready:
                            data=connection.recv(65536)
                            if not data:break
                            channel.sendall(data)
                except (OSError,EOFError):pass
                finally:
                    channel.close()
                    if connection:connection.close()
            thread=threading.Thread(target=copy,daemon=True);self.threads.append(thread);thread.start()
        actual=board.client.get_transport().request_port_forward('127.0.0.1',port,handler=incoming)
        demand(actual==port,'reverse UA listener port mismatch')
    def close(self):
        self.stop.set();self.board.client.get_transport().cancel_port_forward('127.0.0.1',self.port)
        with self.lock:
            for channel,connection in self.channels:channel.close();connection.close()
        for thread in self.threads:thread.join(timeout=2)
        demand(not any(t.is_alive() for t in self.threads),'reverse UA forwarder not drained')


class Deployment(WebDeployment):
    def prepare(self,bundles,hdc,ssh_map):
        ssh_map=ssh_map or {};demand(not set(ssh_map)-set(PAIR),'unknown SSH map board')
        self.root='/data/local/tmp/d5_'+uuid.uuid4().hex[:16]
        self.nodes={};self.remote={};self.manifest=[];self.bundles=bundles;self.port_next=20000;self.port_receipts=[];self.port_ranges={}
        receipts=[]
        for host in PAIR:
            board=Board(host,ssh_map.get(host));self.boards[host]=board
            command=[str(hdc),'-t',host,'shell','uname -m']
            result=subprocess.run(command,capture_output=True,text=True,timeout=25)
            receipts.append(dict(argv=command,exit_code=result.returncode,stdout=result.stdout,stderr=result.stderr))
            demand(result.returncode==0 and result.stdout.strip()=='aarch64','HDC native board control')
            token=next(t for t in json.loads(board.run('cat /data/service/el0/access_token/nativetoken.json')) if t['processName']=='com.kaihong.mdds')
            demand({'ohos.permission.DISTRIBUTED_DATASYNC','ohos.permission.DISTRIBUTED_SOFTBUS_CENTER'}<=set(token['permissions']),'native token permissions')
            rule=next(r for r in json.loads(board.run('cat /system/etc/communication/softbus/softbus_trans_permission.json')) if r.get('SESSION_NAME')=='com.kaihong.mdds.*')
            demand(any(a.get('TYPE')=='native_app' and a.get('PKG_NAME')=='com.kaihong.mdds' for a in rule['APP_INFO']),'Socket naming policy')
            udid=board.run('param get const.product.devUdid').strip();demand(re.fullmatch('[0-9A-Fa-f]{64}',udid),'live UDID')
            ranges=tuple(map(int,board.run('cat /proc/sys/net/ipv4/ip_local_port_range').split()))
            demand(len(ranges)==2 and 0<ranges[0]<=ranges[1]<65536,'native ephemeral range');self.port_ranges[host]=ranges
            self.nodes[host]=dict(udid=udid,token=str(token['tokenId']),permissions=token['permissions'],rule=rule,side=PAIR.index(host),ssh_target=board.connect_host,ssh_host_key_sha256=__import__('hashlib').sha256(board.client.get_transport().get_remote_server_key().asbytes()).hexdigest())
            board.mkdir(self.root)
            for abi,bundle in bundles.items():
                validate(bundle);remote=self.root+'/'+abi;self.remote[host,abi]=remote;board.mkdir(remote)
                files={'bin/'+n:Path(bundle['build'])/n for n in TARGETS}
                for directory in (Path(bundle['prefix'])/'lib',Path(bundle['stock'])/'lib'):
                    for path in directory.glob('*.so*'):
                        if path.is_file():
                            name='lib/'+path.name
                            demand(name not in files or sha(files[name])==sha(path),'native DSO name collision')
                            files[name]=path
                board.archive(remote,files)
                actual={line.split(None,1)[1].strip():line.split()[0] for line in board.run('sha256sum '+' '.join(shlex.quote(remote+'/'+n) for n in files)).splitlines()}
                demand(len(actual)==len(files),'deployed path census')
                for name,path in files.items():
                    digest=sha(path);observed=actual[remote+'/'+name];demand(digest==observed,'deployed bytes mismatch')
                    self.manifest.append(dict(board=host,version=abi,local=str(path),remote=remote+'/'+name,sha256=digest,observed=observed))
            remote=self.remote[host,next(iter(bundles))]
            directory=board.run('LD_LIBRARY_PATH='+shlex.quote(remote+'/lib')+' '+shlex.quote(remote+'/bin/d5_query')+' '+self.nodes[host]['token'])
            match=re.search(r'C4_LOCAL network_id=([0-9a-f]{64})',directory)
            demand(match and 'C4_DIRECTORY PASS' in directory,'native directory query')
            self.nodes[host].update(network=match[1],directory=directory)
        for host,node in self.nodes.items():
            peer=next(p for p in PAIR if p!=host)
            demand('C4_REMOTE network_id='+self.nodes[peer]['network'] in node['directory'],'mutual DSoftBus online directory')
        for name,value in [('nodes',self.nodes),('hdc-receipts',receipts),('deployment-manifest',self.manifest),('ssh-map',ssh_map)]:
            (self.out/(name+'.json')).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

    def port(self,host):
        table=self.boards[host].run('cat /proc/net/tcp /proc/net/tcp6')
        occupied=set()
        for line in table.splitlines():
            fields=line.split()
            if not fields or fields[0]=='sl':continue
            demand(len(fields)>=4 and re.fullmatch('[0-9A-Fa-f]{8,32}:[0-9A-Fa-f]{4}',fields[1]),'native TCP inventory')
            occupied.add(int(fields[1].rsplit(':',1)[1],16))
        inventory='port-state-'+str(len(self.port_receipts))+'.txt';(self.out/inventory).write_text(table,encoding='utf-8')
        for port in range(self.port_next,30000):
            self.port_next=port+1
            if port in occupied or any(low<=port<=high for low,high in self.port_ranges.values()):continue
            with socket.socket() as probe:
                try:probe.bind(('127.0.0.1',port))
                except OSError:continue
            self.port_receipts.append(dict(board=host,selected_port=port,native_tcp_inventory=inventory,ephemeral_ranges=self.port_ranges))
            (self.out/'port-allocation.json').write_text(json.dumps(self.port_receipts,indent=2)+'\n',encoding='utf-8')
            return port
        raise RuntimeError('D5 common loopback port pool exhausted')

    def configuration(self,host,run,root):
        local=self.nodes[host];other=self.nodes[next(p for p in PAIR if p!=host)]
        fields=dict(token=local['token'],run=run,local_udid=local['udid'],peer_udid=other['udid'],peer_network=other['network'],side=local['side'],domain=219,gate=root+'/go',stop=root+'/stop')
        board=self.boards[host];board.mkdir(root)
        path=root+'/transport.conf';board.put(path,(''.join(str(k)+'='+str(v)+'\n' for k,v in fields.items())).encode())
        return path

    def launch(self,host,abi,target,args,path):
        board=self.boards[host];remote=self.remote[host,abi]
        argv=[remote+'/bin/'+target,*map(str,args)]
        command='echo C4_PID=$$; exec env LD_LIBRARY_PATH='+shlex.quote(remote+'/lib')+' '+' '.join(shlex.quote(a) for a in argv)
        log=Path(path);error=log.with_name('stderr.log')
        stream=log.open('w',encoding='utf-8');stderr=error.open('w',encoding='utf-8');process=Process(board,command,stream,stderr)
        record=dict(board=host,abi=abi,target=target,argv=argv,command=command,log=str(log),stderr=str(error),forced_cleanup=False)
        return process,LogStreams(stream,stderr),record

    def audit(self,process,record):
        match=re.search(r'C4_PID=(\d+)',process.text);demand(match,'native PID receipt')
        pid=match[1];board=self.boards[record['board']]
        maps=board.run('cat /proc/'+pid+'/maps');files={line.split()[-1] for line in maps.splitlines() if '/' in line}
        expected=self.remote[record['board'],record['abi']]+'/lib/'
        relevant=[p for p in files if any(t in p.split('/')[-1] for t in ('libfastdds','libfastrtps','libfastcdr','libfoonathan'))]
        demand(relevant and all(p.startswith(expected) for p in relevant),'native ABI/Transport maps path')
        dds='libfastrtps.so' if record['abi']=='2.14.5' else 'libfastdds.so'
        opposite='libfastdds.so' if record['abi']=='2.14.5' else 'libfastrtps.so'
        demand(any(p.split('/')[-1].startswith(dds) for p in relevant) and not any(p.split('/')[-1].startswith(opposite) for p in relevant),'native DDS ABI')
        demand(any('libfastdds_dsoftbus_stock_' in p for p in relevant),'native Socket Transport absent')
        return dict(pid=int(pid),board=record['board'],abi=record['abi'],maps=maps,modules=relevant)


def native_exit(process,stream,record,timeout=12):
    try:
        try:process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if process.poll() is None:
                try:process.terminate();record['forced_cleanup']=True
                except Exception as error:record['terminate_error']=str(error)
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    try:
                        if hasattr(process,'kill'):process.kill()
                        else:
                            match=re.search(r'C4_PID=(\d+)',process.text);demand(match,'owned native PID unavailable for kill')
                            process.board.run('kill -KILL '+match[1])
                        record['forced_cleanup']=True
                    except Exception as error:record['kill_error']=str(error)
                    try:process.wait(timeout=5)
                    except subprocess.TimeoutExpired:record['collection_error']='native exit remains uncollected'
    finally:
        status=process.returncode
        record['channel_exit_status']=status
        # SSH uses -1 when no remote exit status was received; it is not an OS code.
        record['exit_code']=None if status==-1 else status
        if status is not None:stream.close()
    record['shutdown_pass']='D5_SHUTDOWN PASS' in process.text
    demand(record['exit_code']==0 and not record['forced_cleanup'] and record['shutdown_pass'],'native exit/ownership gate: '+record['target'])
    counters=re.search(r'C4_SOCKET sdk_sends=(\d+) received_records=(\d+) close=1',process.text)
    demand(counters and int(counters[1])>0 and int(counters[2])>0 and 'C4_PUMP healthy=1' in process.text,'native Socket send/receive/close evidence')
    record.update(sdk_sends=int(counters[1]),received_records=int(counters[2]))
    return record


class Session:
    def __init__(self,path,deployment,gateway_board,gateway_abi,peer_abi):
        self.path=Path(path);self.path.mkdir();self.deployment=deployment
        self.gateway_board=gateway_board;self.peer_board=next(p for p in PAIR if p!=gateway_board)
        self.gateway_abi,self.peer_abi=gateway_abi,peer_abi;self.id=uuid.uuid4().hex[:16]
        self.root=deployment.root+'/case_'+self.id;self.native=[];self.controls=[];self.forwarders=[];self.modules=[];self.gated=False
        self.transport={host:deployment.configuration(host,self.id,self.root) for host in PAIR}
    def control(self,local,host,remote=None,mirror=False):
        local=Path(local);local.mkdir(parents=True,exist_ok=True)
        remote=remote or self.root+'/'+local.relative_to(self.path).as_posix()
        self.deployment.boards[host].mkdir(remote)
        self.controls.append(dict(local=local,host=host,remote=remote,sent={},mirror=mirror))
        return remote
    def start(self,name,target,args,peer=False):
        host=self.peer_board if peer else self.gateway_board;abi=self.peer_abi if peer else self.gateway_abi
        log=self.path/name/'process.log';log.parent.mkdir(exist_ok=True)
        row=self.deployment.launch(host,abi,target,[self.transport[host],*args],log)
        row[2].update(name=name,directory=str(log.parent));self.native.append(row);return row
    def sync(self):
        for control in self.controls:
            board=self.deployment.boards[control['host']]
            for file in control['local'].iterdir():
                if not file.is_file() or not re.fullmatch(r'(?:request-\d+\.txt|command-\d+|go|continue|stop)',file.name):continue
                data=file.read_bytes();demand(len(data)<=9000,'control file bound')
                if file.name in control['sent']:
                    demand(control['sent'][file.name]==data,'published control mutated');continue
                remote=control['remote']+'/'+file.name;board.put(remote+'.pending',data)
                board.run('mv '+shlex.quote(remote+'.pending')+' '+shlex.quote(remote));control['sent'][file.name]=data
        mirrors=[c for c in self.controls if c['mirror']]
        if mirrors:
            host=mirrors[0]['host'];demand(all(c['host']==host for c in mirrors),'mirror board')
            board=self.deployment.boards[host]
            command='tar -cf - -C '+shlex.quote(self.root)+' '+' '.join(shlex.quote(c['remote'][len(self.root)+1:]) for c in mirrors)
            _,out,err=board.client.exec_command(command,timeout=12)
            data=out.read(4*1024*1024+1);error=err.read();code=out.channel.recv_exit_status()
            demand(not code and len(data)<=4*1024*1024,'control archive bounds/exit: '+error.decode('utf-8',errors='replace'))
            with tarfile.open(fileobj=io.BytesIO(data),mode='r:') as archive:
                for item in archive:
                    if not item.isfile():continue
                    for control in mirrors:
                        prefix=control['remote'][len(self.root)+1:]+'/'
                        if not item.name.startswith(prefix):continue
                        name=item.name[len(prefix):]
                        if not re.fullmatch(r'(?:response-\d+\.txt|process\.log)',name):continue
                        content=archive.extractfile(item).read()
                        file=control['local']/name
                        if name=='process.log':file.write_bytes(content)
                        elif not file.exists():
                            temp=file.with_suffix('.pending');temp.write_bytes(content);temp.replace(file)
    def wait(self,predicate,label,timeout=12):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            self.sync()
            if predicate():return
            demand(all(p.poll() is None for p,_,r in self.native if not r.get('collected')),'native early exit: '+label)
            time.sleep(.02)
        raise RuntimeError('D5 timeout: '+label)
    def gate(self):
        self.wait(lambda:all('C4_LISTENING' in p.text for p,_,r in self.native),'both native Socket listeners')
        for host in PAIR:self.deployment.boards[host].touch(self.root+'/go')
        self.gated=True
    def audit(self):
        for p,_,record in self.native:
            if record.get('collected'):continue
            if p.poll() is not None:
                demand(record.get('stopped_expected') and p.returncode==0,'unexpected native exit during module audit')
                continue
            self.modules.append(self.deployment.audit(p,record))
    def stop(self,row):
        p,stream,record=row
        errors=[]
        try:(Path(record['directory'])/'stop').touch();self.sync()
        except Exception as error:record['control_error']=str(error);errors.append(error)
        try:native_exit(p,stream,record)
        except Exception as error:record['collection_error']=str(error);errors.append(error)
        finally:record['collected']=p.returncode is not None and isinstance(record.get('exit_code'),int)
        if errors:raise RuntimeError('D5 stop failed: '+'; '.join(str(e) for e in errors)) from errors[0]
        return record
    def cleanup(self):
        errors=[]
        for control in self.controls:
            try:(control['local']/'stop').touch()
            except Exception as error:errors.append(str(error))
        try:self.sync()
        except Exception as e:errors.append(str(e))
        for row in self.native:
            if not row[2].get('collected'):
                try:self.stop(row)
                except Exception as e:errors.append(str(e))
        for forward in self.forwarders:
            try:forward.close()
            except Exception as e:errors.append(str(e))
        self.forwarders=[]
        if errors:raise RuntimeError('D5 cleanup: '+'; '.join(errors))
