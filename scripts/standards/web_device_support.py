"""Native process/deployment adapter. HTTP, XML, WS and DDS oracles stay in tests.

SSH forwarding carries only the Windows web-client connection. Inter-board DDS
uses the native Socket Transport with builtin UDP/SHM disabled by the programs.
"""
import hashlib
import errno
import ipaddress
import io
import json
import os
from pathlib import Path
import re
import select
import shlex
import socket
import socketserver
import subprocess
import threading
import tarfile
import time
import uuid
import paramiko

ROOT=Path(__file__).resolve().parents[2]

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

class Board:
    def __init__(self,host,connect_host=None):
        if host not in ('100.97.6.8','100.97.143.74'):raise ValueError('board outside declared pair')
        self.host=host;self.connect_host=connect_host or host;self.client=paramiko.SSHClient()
        address=ipaddress.ip_address(self.connect_host)
        if not address.is_private or address.is_loopback or address.is_multicast:raise ValueError('SSH address outside private device network')
        if self.connect_host!=host:
            known=paramiko.HostKeys(str(Path.home()/'.ssh/known_hosts')).lookup(host)
            if not known:raise ValueError('logical board has no trusted SSH host key')
            for kind,key in known.items():self.client.get_host_keys().add(self.connect_host,kind,key)
        else:self.client.load_system_host_keys()
        self.client.connect(self.connect_host,username='root',timeout=12,banner_timeout=12,auth_timeout=12)
        self.client.get_transport().set_keepalive(15)
        self.lock=threading.Lock()
    def run(self,command,timeout=20):
        _,out,err=self.client.exec_command(command,timeout=timeout)
        text=out.read().decode('utf-8',errors='replace');error=err.read().decode('utf-8',errors='replace')
        code=out.channel.recv_exit_status()
        if code:raise RuntimeError(f'{self.host} command exit {code}: {error} {text}')
        return text
    def mkdir(self,path):
        if not path.startswith('/data/local/tmp/c4_') or '..' in path:raise ValueError('unsafe remote root')
        self.run('mkdir -p '+shlex.quote(path)+'; chmod 700 '+shlex.quote(path))
    def put(self,path,data,mode=0o600):
        self.stream('umask 077; cat > '+shlex.quote(path)+'; chmod '+oct(mode)[2:]+' '+shlex.quote(path),data)
    def stream(self,command,data):
        with self.lock:
            channel=self.client.get_transport().open_session(timeout=12);channel.settimeout(40)
            channel.exec_command(command);end=time.monotonic()+120
            for at in range(0,len(data),65536):
                remaining=end-time.monotonic()
                if remaining<=0:channel.close();raise TimeoutError('native upload deadline')
                channel.settimeout(min(40,remaining));channel.sendall(data[at:at+65536])
            channel.shutdown_write()
            code=channel.recv_exit_status();channel.close()
            if code:raise RuntimeError('native stream upload failed')
    def archive(self,path,files):
        buffer=io.BytesIO();seen={}
        with tarfile.open(fileobj=buffer,mode='w:gz',compresslevel=6) as tar:
            for name,local in files.items():
                if name.startswith('/') or '..' in name.split('/'):raise ValueError('unsafe archive member')
                data=Path(local).read_bytes();sha=hashlib.sha256(data).hexdigest();info=tarfile.TarInfo(name);info.mode=0o700 if name.startswith('bin/') else 0o600
                if sha in seen:info.type=tarfile.LNKTYPE;info.linkname=seen[sha];tar.addfile(info)
                else:info.size=len(data);tar.addfile(info,io.BytesIO(data));seen[sha]=name
        data=buffer.getvalue();print('C4_UPLOAD',self.host,len(files),len(data),'compressed bytes',flush=True)
        self.stream('tar -xzf - -C '+shlex.quote(path),data)
    def touch(self,path):self.put(path,b'')
    def close(self):self.client.close()

class Forwarder:
    def __init__(self,board,port):
        class Handler(socketserver.BaseRequestHandler):
            def handle(inner):
                channel=None
                try:
                    channel=board.client.get_transport().open_channel('direct-tcpip',('127.0.0.1',inner.server.server_address[1]),inner.client_address,timeout=6)
                    client_open=True
                    while True:
                        ready,_,_=select.select(([inner.request] if client_open else [])+[channel],[],[],6)
                        if not ready:continue
                        if inner.request in ready:
                            data=inner.request.recv(65536)
                            if not data:channel.shutdown_write();client_open=False
                            else:channel.sendall(data)
                        if channel in ready:
                            data=channel.recv(65536)
                            if not data:break
                            inner.request.sendall(data)
                except (OSError,EOFError,paramiko.SSHException):pass
                finally:
                    if channel:channel.close()
        class Server(socketserver.ThreadingTCPServer):
            daemon_threads=True;allow_reuse_address=False
        self.server=Server(('127.0.0.1',port),Handler);self.port=self.server.server_address[1]
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
    def close(self):self.server.shutdown();self.server.server_close();self.thread.join(timeout=2)

class Process:
    def __init__(self,board,command,stdout,stderr):
        self.board=board;self.channel=board.client.get_transport().open_session(timeout=12)
        self.channel.exec_command(command);self.returncode=None;self.pid=None
        self.stdout_target=stdout;self.stderr_target=stderr;self.text='';self.error='';self.threads=[]
        for source,target,attribute in [(self.channel.makefile('rb'),stdout,'text'),(self.channel.makefile_stderr('rb'),stderr,'error')]:
            def copy(source=source,target=target,attribute=attribute):
                while True:
                    data=source.read(1)
                    if not data:break
                    value=data.decode('utf-8',errors='replace');setattr(self,attribute,getattr(self,attribute)+value)
                    if target:target.write(value);target.flush()
            thread=threading.Thread(target=copy,daemon=True);thread.start();self.threads.append(thread)
    def poll(self):
        if self.returncode is None and self.channel.exit_status_ready():
            self.returncode=self.channel.recv_exit_status()
            for t in self.threads:t.join(timeout=3)
        return self.returncode
    def wait(self,timeout=None):
        end=time.monotonic()+(timeout if timeout is not None else 60)
        while self.poll() is None:
            if time.monotonic()>=end:raise subprocess.TimeoutExpired('remote native process',timeout)
            time.sleep(.02)
        return self.returncode
    def terminate(self):
        self.channel.shutdown_write()
        match=re.search(r'C4_PID=(\d+)',self.text)
        if match:self.board.run('kill -TERM '+match[1])
        else:self.channel.close()

class Deployment:
    def __init__(self,out,bundles,hdc,ssh_map=None):
        self.out=Path(out);self.out.mkdir(parents=True,exist_ok=False)
        self.boards={}
        try:self.prepare(bundles,hdc,ssh_map)
        except Exception:self.close();raise
    def prepare(self,bundles,hdc,ssh_map):
        ssh_map=ssh_map or {}
        if set(ssh_map)-{'100.97.6.8','100.97.143.74'}:raise ValueError('unknown SSH map board')
        for name in ('100.97.6.8','100.97.143.74'):self.boards[name]=Board(name,ssh_map.get(name))
        self.port_next=20000;self.port_receipts=[];self.port_ranges={}
        for name,board in self.boards.items():
            values=board.run('cat /proc/sys/net/ipv4/ip_local_port_range').split()
            if len(values)!=2:raise RuntimeError('native ephemeral port range invalid')
            low,high=map(int,values)
            if not 0<low<=high<65536:raise RuntimeError('native ephemeral port range invalid')
            self.port_ranges[name]=(low,high)
        (self.out/'ssh-map.json').write_text(json.dumps(ssh_map,indent=2),encoding='utf-8')
        self.nodes={};self.remote={};self.manifest=[];self.bundles=bundles
        run=uuid.uuid4().hex[:16];self.root='/data/local/tmp/c4_'+run
        receipts=[]
        for name,board in self.boards.items():
            # Required HDC control surface: this installation's explicit SSH shim.
            command=[str(hdc),'-t',name,'shell','uname -m']
            result=subprocess.run(command,capture_output=True,text=True,timeout=25)
            receipts.append({'command':command,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
            if result.returncode or 'aarch64' not in result.stdout:raise RuntimeError('HDC board control failed')
            tokens=json.loads(board.run('cat /data/service/el0/access_token/nativetoken.json'))
            token=next(p for p in tokens if p['processName']=='com.kaihong.mdds')
            required={'ohos.permission.DISTRIBUTED_DATASYNC','ohos.permission.DISTRIBUTED_SOFTBUS_CENTER'}
            if not required.issubset(token['permissions']):raise RuntimeError('native permissions missing')
            policy=json.loads(board.run('cat /system/etc/communication/softbus/softbus_trans_permission.json'))
            rule=next(p for p in policy if p.get('SESSION_NAME')=='com.kaihong.mdds.*')
            if not any(p.get('PKG_NAME')=='com.kaihong.mdds' and p.get('TYPE')=='native_app' for p in rule['APP_INFO']):raise RuntimeError('Socket naming authorization missing')
            udid=board.run('param get const.product.devUdid').strip()
            if not re.fullmatch('[0-9A-Fa-f]{64}',udid):raise RuntimeError('UDID invalid')
            self.nodes[name]={'udid':udid,'token':str(token['tokenId']),'permissions':token['permissions'],'rule':rule,'side':0 if name=='100.97.6.8' else 1,'ssh_target':board.connect_host,'ssh_host_key_sha256':hashlib.sha256(board.client.get_transport().get_remote_server_key().asbytes()).hexdigest()}
            board.mkdir(self.root)
            for version,bundle in bundles.items():
                remote=self.root+'/'+version;board.mkdir(remote+'/bin');board.mkdir(remote+'/lib');board.mkdir(remote+'/schemas');board.mkdir(remote+'/ws-schema')
                self.remote[name,version]=remote
                files={}
                build=Path(bundle['build'])
                for n in ['webdds_gateway_c4','c4_peer','c4_query']:files['bin/'+n]=build/n
                for p in (build/'schemas').glob('*.xsd'):files['schemas/'+p.name]=p
                files['ws-schema/webdds_websockets1.xsd']=build/'websocket-schema/webdds_websockets1.xsd'
                for directory in [Path(bundle['prefix'])/'lib',Path(bundle['stock'])/'lib',Path(bundle['xml'])/'lib']:
                    for p in directory.glob('*.so*'):
                        if p.is_file():
                            key='lib/'+p.name
                            if key in files and digest(files[key])!=digest(p):raise RuntimeError('ABI runtime filename collision '+key)
                            files[key]=p
                board.archive(remote,files)
                observed_files={line.split(None,1)[1].strip():line.split()[0] for line in board.run('sha256sum '+' '.join(shlex.quote(remote+'/'+name) for name in files)).splitlines()}
                if len(observed_files)!=len(files):raise RuntimeError('incomplete native hash listing')
                for relative,path in files.items():
                    observed=observed_files[remote+'/'+relative]
                    expected=digest(path)
                    self.manifest.append({'board':name,'version':version,'local':str(path),'remote':remote+'/'+relative,'sha256':expected,'observed':observed})
                    if observed!=expected:raise RuntimeError('deployment hash mismatch')
            version=next(iter(bundles));remote=self.remote[name,version]
            query=board.run('LD_LIBRARY_PATH='+shlex.quote(remote+'/lib')+' '+shlex.quote(remote+'/bin/c4_query')+' '+self.nodes[name]['token'])
            found=re.search(r'C4_LOCAL network_id=([0-9a-f]{64})',query)
            if not found or 'C4_DIRECTORY PASS' not in query:raise RuntimeError('live DSoftBus directory failed')
            self.nodes[name].update(network=found[1],directory=query)
        for name,node in self.nodes.items():
            other=next(x for x in self.nodes if x!=name)
            if 'C4_REMOTE network_id='+self.nodes[other]['network'] not in node['directory']:raise RuntimeError('pair not mutually online')
        (self.out/'hdc-receipts.json').write_text(json.dumps(receipts,indent=2),encoding='utf-8')
        (self.out/'nodes.json').write_text(json.dumps(self.nodes,indent=2),encoding='utf-8')
        (self.out/'deployment-manifest.json').write_text(json.dumps(self.manifest,indent=2),encoding='utf-8')
    def posthash(self):
        for board in self.boards:
            items=[x for x in self.manifest if x['board']==board]
            found={line.split(None,1)[1].strip():line.split()[0] for line in self.boards[board].run('sha256sum '+' '.join(shlex.quote(x['remote']) for x in items)).splitlines()}
            if len(found)!=len(items):raise RuntimeError('incomplete native posthash listing')
            for item in items:
                item['post_sha256']=found[item['remote']]
                if item['post_sha256']!=item['sha256']:raise RuntimeError('runtime changed during native validation')
        (self.out/'deployment-posthash.json').write_text(json.dumps(self.manifest,indent=2),encoding='utf-8')
    def forwarder(self,name):
        # Windows bind(0) says nothing about the board's TCP namespace. Keep
        # both ends on one port (including absolute Location responses), but
        # inspect every native TCP state and avoid both boards' ephemeral
        # ranges. The finite pool is consumed once per invocation, never reused.
        board=self.boards[name];table=board.run('cat /proc/net/tcp /proc/net/tcp6')
        occupied=set()
        for line in table.splitlines():
            fields=line.split()
            if not fields or fields[0]=='sl':continue
            if len(fields)<4 or not re.fullmatch('[0-9A-Fa-f]{8,32}:[0-9A-Fa-f]{4}',fields[1]):raise RuntimeError('native TCP inventory invalid')
            occupied.add(int(fields[1].rsplit(':',1)[1],16))
        inventory='port-state-'+str(len(self.port_receipts))+'-'+name+'.txt'
        (self.out/inventory).write_text(table,encoding='utf-8')
        entry={'board':name,'native_tcp_inventory':inventory,'ephemeral_ranges':self.port_ranges,'local_bind_rejections':[]}
        for port in range(self.port_next,30000):
            self.port_next=port+1
            if port in occupied or any(low<=port<=high for low,high in self.port_ranges.values()):continue
            try:forward=Forwarder(board,port)
            except OSError as error:
                if error.errno not in (errno.EADDRINUSE,errno.EACCES,10048,10013):raise
                entry['local_bind_rejections'].append({'port':port,'errno':error.errno});continue
            entry['selected_port']=port;self.port_receipts.append(entry)
            (self.out/'port-allocation.json').write_text(json.dumps(self.port_receipts,indent=2),encoding='utf-8')
            return forward
        entry['error']='common loopback port pool exhausted';self.port_receipts.append(entry)
        (self.out/'port-allocation.json').write_text(json.dumps(self.port_receipts,indent=2),encoding='utf-8')
        raise RuntimeError(entry['error'])
    def close(self):
        for board in self.boards.values():board.close()

class NativeSession:
    def __init__(self,owner,args,env,stdout,stderr,standalone=False):
        self.owner=owner;self.out=Path(args[args.index('--policy')+1]).parent
        self.gateway_board=owner.gateway_board;self.peer_board=next(n for n in owner.deployment.nodes if n!=self.gateway_board)
        self.gw=owner.deployment.boards[self.gateway_board];self.peer=owner.deployment.boards[self.peer_board]
        self.id=uuid.uuid4().hex[:16];self.root=owner.deployment.root+'/case_'+self.id
        self.stop_local=Path(args[args.index('--stop-file')+1]);self.stopped=False;self.finished=False;self.standalone=standalone;self.forwarders=[];self.peer_process=None;self.gateway_process=None
        self.jobs=[]
        owner.sessions.append(self)
        for b in [self.gw,self.peer]:b.mkdir(self.root);b.mkdir(self.root+'/jobs')
        for b,name in [(self.gw,self.gateway_board),(self.peer,self.peer_board)]:
            local=owner.deployment.nodes[name];other=owner.deployment.nodes[self.peer_board if name==self.gateway_board else self.gateway_board]
            config={'token':local['token'],'run':self.id,'local_udid':local['udid'],'peer_udid':other['udid'],'peer_network':other['network'],'side':local['side'],'domain':env['C1_DOMAIN'],'gate':self.root+'/go','stop':self.root+'/stop'}
            b.put(self.root+'/transport.conf',(''.join(str(k)+'='+str(v)+'\n' for k,v in config.items())).encode())
        version=owner.gateway_version;gwroot=owner.deployment.remote[self.gateway_board,version]
        converted=[gwroot+'/bin/webdds_gateway_c4'];requested=args[1:];i=0
        while i<len(requested):
            key,value=requested[i:i+2];i+=2
            if key in ('--port','--tls-port'):
                forward=owner.deployment.forwarder(self.gateway_board);self.forwarders.append(forward);value=str(forward.port)
            elif key=='--stop-file':value=self.root+'/stop'
            elif key=='--schema-dir':
                remote=self.root+'/altered-schemas';self.gw.mkdir(remote)
                for p in Path(value).glob('*.xsd'):self.gw.put(remote+'/'+p.name,p.read_bytes())
                value=remote
            elif key in ('--policy','--tls-cert','--tls-key','--ws-schema'):
                local=Path(value);remote=self.root+'/'+key[2:]+'-'+local.name;self.gw.put(remote,local.read_bytes());value=remote
            converted.extend([key,value])
        if '--schema-dir' not in converted:converted+=['--schema-dir',gwroot+'/schemas']
        if '--ws-schema' not in converted:converted+=['--ws-schema',gwroot+'/ws-schema/webdds_websockets1.xsd']
        converted+=['--transport-config',self.root+'/transport.conf','--profile',owner.profile]
        if not standalone:
            peerroot=owner.deployment.remote[self.peer_board,owner.peer_version]
            self.peer_log=(self.out/'native-peer-daemon.log').open('w',encoding='utf-8')
            self.peer_error=(self.out/'native-peer-daemon.stderr.log').open('w',encoding='utf-8')
            peercommand='echo C4_PID=$$; exec env LD_LIBRARY_PATH='+shlex.quote(peerroot+'/lib')+' '+shlex.quote(peerroot+'/bin/c4_peer')+' '+shlex.quote(self.root+'/transport.conf')+' '+shlex.quote(self.root+'/jobs')
            self.peer_process=Process(self.peer,peercommand,self.peer_log,self.peer_error)
        environment=' '.join(shlex.quote(k+'='+v) for k,v in env.items() if k.startswith('C1_') and k.endswith('_KEY'))
        command='echo C4_PID=$$; exec env LD_LIBRARY_PATH='+shlex.quote(gwroot+'/lib')+' '+environment+' '+' '.join(shlex.quote(v) for v in converted)
        self.gateway_process=Process(self.gw,command,stdout,stderr)
        (self.out/('native-launch-'+self.id+'.json')).write_text(json.dumps({'gateway_board':self.gateway_board,'peer_board':self.peer_board,'gateway_version':version,'peer_version':owner.peer_version,'requested':args,'native_argv':converted,'remote_root':self.root,'api_keys':'environment only, values omitted','forwarding':'Windows loopback port equals native loopback port; no protocol rewriting'},indent=2),encoding='utf-8')
        if not standalone:
            end=time.monotonic()+25
            while time.monotonic()<end:
                if 'C4_LISTENING' in self.gateway_process.text and 'C4_LISTENING' in self.peer_process.text:break
                if self.gateway_process.poll() is not None or self.peer_process.poll() is not None:raise RuntimeError('native bootstrap exited: '+self.gateway_process.error+self.peer_process.error)
                time.sleep(.02)
            else:raise RuntimeError('native listener readiness deadline')
            self.gw.touch(self.root+'/go');self.peer.touch(self.root+'/go')
            end=time.monotonic()+15
            while time.monotonic()<end and 'C4_PEER_READY' not in self.peer_process.text:
                if self.peer_process.poll() is not None:raise RuntimeError('native peer bootstrap failed')
                time.sleep(.02)
            while time.monotonic()<end and 'C2_READY' not in self.gateway_process.text:
                if self.gateway_process.poll() is not None:raise RuntimeError('native gateway startup failed')
                time.sleep(.02)
            for process,board,role,version in [(self.gateway_process,self.gw,'gateway',owner.gateway_version),(self.peer_process,self.peer,'peer',owner.peer_version)]:
                match=re.search(r'C4_PID=(\d+)',process.text)
                if not match:raise RuntimeError('native PID receipt missing')
                process.pid=int(match[1]);maps=board.run('cat /proc/'+match[1]+'/maps')
                (self.out/('native-'+role+'-maps.txt')).write_text(maps,encoding='utf-8')
                required=owner.deployment.remote[board.host,version]+'/lib/'
                paths={line.split()[-1] for line in maps.splitlines() if '/' in line}
                for path in paths:
                    if re.search(r'/lib(?:fastdds|fastrtps|fastcdr|crypto\.so\.3|ssl\.so\.3|xml2\.so)',path) and not path.startswith(required):raise RuntimeError('loaded ABI library outside verified closure: '+path)
    def stop(self):
        if self.stopped:return
        self.stopped=True;self.gw.touch(self.root+'/stop')
    def finish(self,timeout):
        if self.finished:return self.gateway_process.returncode
        if self.stop_local.exists():self.stop()
        code=self.gateway_process.wait(timeout)
        if not self.stopped:self.stop()
        # Keep the remote Socket anchor alive while the gateway deletes DDS
        # entities and sends their final discovery records. Only the trusted
        # peer daemon is stopped after the gateway has actually exited.
        self.peer.touch(self.root+'/stop')
        if self.peer_process:self.peer_process.channel.shutdown_write()
        peer_code=self.peer_process.wait(10) if self.peer_process else None
        for f in self.forwarders:f.close()
        if self.peer_process:
            self.peer_log.close();self.peer_error.close()
            for job in self.jobs:
                for suffix in ['log','exit']:
                    text=self.peer.run('cat '+shlex.quote(job['remote']+'.'+suffix))
                    (self.out/('native-job-'+job['id']+'.'+suffix)).write_text(text,encoding='utf-8')
        self.finished=True
        receipt={'standalone_negative_control':self.standalone,'gateway_exit':code,'peer_exit':peer_code,'gateway_board':self.gateway_board,'peer_board':self.peer_board,'remote_root':self.root,'jobs':self.jobs,'gateway_drain':'C4_SHUTDOWN PASS' in self.gateway_process.text,'peer_drain':self.peer_process is None or 'C4_PEER_SHUTDOWN PASS' in self.peer_process.text}
        (self.out/('native-exit-'+self.id+'.json')).write_text(json.dumps(receipt,indent=2),encoding='utf-8')
        if code==0 and self.peer_process and (peer_code!=0 or not receipt['peer_drain']):raise AssertionError('native peer did not drain')
        if code==0 and not self.standalone:
            if not receipt['gateway_drain'] or 'C4_TRANSPORT_READY builtin=0 udp=0 shm=0' not in self.gateway_process.text:raise AssertionError('native transport/drain receipt missing')
            for process in [self.gateway_process,self.peer_process]:
                counters=re.search(r'C4_SOCKET sdk_sends=(\d+) received_records=(\d+) close=1',process.text)
                if not counters or (self.jobs and (int(counters[1])==0 or int(counters[2])==0)):raise AssertionError('native Socket counter/close receipt invalid')
        return code

class GatewayProcess:
    def __init__(self,session):self.session=session;self.returncode=None
    def poll(self):return self.session.gateway_process.poll()
    def wait(self,timeout=None):self.returncode=self.session.finish(timeout or 15);return self.returncode
    def terminate(self):self.session.stop();self.session.gateway_process.terminate()

class Facade:
    def __init__(self,deployment,gateway_board,gateway_version,peer_version):
        self.deployment=deployment;self.gateway_board=gateway_board;self.gateway_version=gateway_version;self.peer_version=peer_version;self.sessions=[]
        self.profile='websocket'
        self.gateway=str(Path(deployment.bundles[gateway_version]['build'])/'webdds_gateway_c4')
        self.peer=str(Path(deployment.bundles[peer_version]['build'])/'c4_peer')
    def __getattr__(self,name):return getattr(subprocess,name)
    def Popen(self,args,stdout=None,stderr=None,env=None,**kwargs):
        if str(args[0])==self.gateway:
            return GatewayProcess(NativeSession(self,[str(x) for x in args],env,stdout,stderr))
        if str(args[0])==self.peer:
            if not self.sessions:raise RuntimeError('peer job without native gateway')
            session=next(s for s in reversed(self.sessions) if not s.finished and not s.stopped and s.peer_process);job=uuid.uuid4().hex
            if not re.fullmatch('[A-Za-z0-9_]{1,64}',str(args[3])):raise ValueError('invalid topic')
            session.peer_process.channel.sendall((' '.join([job]+[str(x) for x in args[1:]])+'\n').encode())
            base=session.root+'/jobs/'+job
            session.jobs.append({'id':job,'argv':[str(x) for x in args[1:]],'remote':base})
            command='echo C4_PID=$$; while [ ! -f '+shlex.quote(base+'.log')+' ]; do sleep 0.02; done; tail -n +1 -f '+shlex.quote(base+'.log')+' & tailpid=$!; trap \'kill $tailpid 2>/dev/null\' EXIT; trap \'exit 143\' TERM INT; while [ ! -f '+shlex.quote(base+'.exit')+' ]; do sleep 0.02; done; sleep 1.1; exit $(cat '+shlex.quote(base+'.exit')+')'
            return Process(session.peer,command,stdout,stderr)
        return subprocess.Popen(args,stdout=stdout,stderr=stderr,env=env,**kwargs)
    def run(self,args,env=None,**kwargs):
        if str(args[0])!=self.gateway:return subprocess.run(args,env=env,**kwargs)
        out=io.StringIO();err=io.StringIO();session=NativeSession(self,[str(x) for x in args],env,out,err,standalone=True)
        code=session.finish(kwargs.get('timeout',15));text=out.getvalue();error=err.getvalue()
        if kwargs.get('stderr')==subprocess.STDOUT:text+=error;error=None
        return subprocess.CompletedProcess(args,code,text,error)
