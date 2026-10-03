"""Finite original-RMW debugger support for the already-dispatched ROS window."""
import argparse,ast,hashlib,json,os,re,subprocess,threading,time,sys,importlib.util
from pathlib import Path

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def verify_instruction_image(script,image):
    # Read only the literal reviewed table; never execute a script during
    # pre-dispatch validation and never broaden support by a version string.
    tree=ast.parse(Path(script).read_text(encoding='utf-8-sig'))
    values=[node.value for node in tree.body if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='LAYOUTS' for t in node.targets)]
    if len(values)!=1 or sha(image) not in ast.literal_eval(values[0]):
        raise ValueError('DEBUGGER_RMW_INSTRUCTION_IMAGE_UNSUPPORTED')
def write(p,v):
    with Path(p).open('x',encoding='utf-8') as f:json.dump(v,f,indent=2)

class Command:
    def __init__(self,root,label,argv,seconds,env=None):
        self.root=root;self.label=label;self.argv=argv;self.seconds=seconds;self.overflow=False;self.ready=threading.Event();self.buffers={'stdout':bytearray(),'stderr':bytearray()}
        write(root/(label+'.intent.json'),{'argv':argv,'cwd':str(root),'deadline_seconds':seconds,'purpose':'RMW_DEBUGGER_SUPPORT_NOT_BUSINESS','dispatch':'INTENT_NOT_REMOTE_EXECUTION_PROOF'})
        self.child=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env,shell=False,cwd=root)
        try:write(root/(label+'.started.json'),{'argv':argv,'cwd':str(root),'local_pid':self.child.pid,'deadline_seconds':seconds,'purpose':'RMW_DEBUGGER_SUPPORT_NOT_BUSINESS'})
        except Exception:
            # Only this owned host child. Disconnecting it is not proof that a
            # target debugger/attached inferior recovered. The intent remains.
            self.child.kill()
            try:self.child.wait(timeout=2)
            except subprocess.TimeoutExpired:pass
            for pipe in (self.child.stdout,self.child.stderr):pipe.close()
            raise RuntimeError('DEBUGGER_ACTION_RECORD_FAILED_REMOTE_UNKNOWN')
        self.threads=[]
        for stream in ('stdout','stderr'):
            t=threading.Thread(target=self.read,args=(stream,),daemon=True);t.start();self.threads.append(t)
        self.finished=threading.Event();threading.Thread(target=self.wait,daemon=True).start()
    def read(self,stream):
        source=getattr(self.child,stream)
        while True:
            block=source.read1(4096)
            if not block:break
            remaining=65536-len(self.buffers[stream]);self.buffers[stream].extend(block[:max(0,remaining)])
            if len(block)>remaining:self.overflow=True
            text=self.buffers[stream].decode('utf-8',errors='replace')
            if 'Listening to port ' in text:self.ready.set()
        source.close()
    def wait(self):
        timed=False;termination=None
        try:self.child.wait(timeout=self.seconds)
        except subprocess.TimeoutExpired:
            timed=True;termination={'target_local_pid':self.child.pid,'reason':'DEBUGGER_HOST_DEADLINE','remote_termination':'NOT_PROVEN'}
            write(self.root/(self.label+'.termination.json'),termination);self.child.kill()
            try:self.child.wait(timeout=2)
            except subprocess.TimeoutExpired:pass
        for thread in self.threads:thread.join(timeout=2)
        complete=all(not t.is_alive() for t in self.threads)
        for name,data in self.buffers.items():(self.root/(self.label+'.'+name)).write_bytes(bytes(data))
        self.result={'argv':self.argv,'local_pid':self.child.pid,'exit_code':self.child.poll(),'timed_out':timed,'capture_complete':complete,'overflow':self.overflow,'termination':termination,
            'stdout_sha256':sha(self.root/(self.label+'.stdout')),'stderr_sha256':sha(self.root/(self.label+'.stderr'))}
        write(self.root/(self.label+'.result.json'),self.result);self.finished.set()

def command_text(command):
    command.finished.wait(command.seconds+8)
    if not command.finished.is_set():raise RuntimeError('DEBUGGER_COMMAND_CAPTURE_UNCONFIRMED')
    if command.result['exit_code']!=0 or command.result['overflow'] or not command.result['capture_complete'] or command.result.get('timed_out') or command.result.get('termination'):
        raise RuntimeError('DEBUGGER_COMMAND_CAPTURE_FAILED')
    return command.buffers['stdout'].decode('utf-8',errors='replace')

def remote_result(command):
    text=command_text(command);values=re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\r?$',text,re.M)
    if len(values)!=1 or values[0]!='0':
        raise RuntimeError('DEBUGGER_SUPPORT_REMOTE_COMMAND_FAILED')
    return text

def server_exit(command):
    text=command_text(command);values=re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\r?$',text,re.M)
    if len(values)!=1:raise RuntimeError('DEBUGGER_SERVER_EXIT_UNCONFIRMED')
    return int(values[0])

def forward_rows(text):
    # Version-bound documented HDC task list. Unknown output is not empty.
    lines=[line.strip() for line in text.splitlines() if line.strip()]
    if len(lines)==1 and lines[0].lower()=='[empty]':return []
    rows=[]
    for line in lines:
        match=re.fullmatch(r'(tcp:[0-9]+) (tcp:[0-9]+) \[(Forward|Reverse)\]',line)
        if not match:raise RuntimeError('DEBUGGER_FORWARD_LIST_UNRECOGNIZED')
        rows.append(match.groups())
    if not rows:raise RuntimeError('DEBUGGER_FORWARD_LIST_MISSING')
    return rows

def forward_entries(text):
    """HDC ls is global, including its connect key; never discard ownership."""
    lines=[line.strip() for line in text.splitlines() if line.strip()]
    if len(lines)==1 and lines[0].lower()=='[empty]':return []
    rows=[]
    for line in lines:
        match=re.fullmatch(r'([^\s]+)\s+(tcp:[0-9]+)\s+(tcp:[0-9]+)\s+\[(Forward|Reverse)\]',line)
        if not match:raise RuntimeError('DEBUGGER_FORWARD_LIST_UNRECOGNIZED')
        rows.append(match.groups())
    if not rows:raise RuntimeError('DEBUGGER_FORWARD_LIST_MISSING')
    if len(set(rows))!=len(rows):raise RuntimeError('DEBUGGER_FORWARD_LIST_DUPLICATE')
    return rows

def owned_forward_rows(text,target):
    return [(a,b,role) for owner,a,b,role in forward_entries(text) if owner==target]

def forward_remove_argv(base,node):return base+['fport','rm',node,node]

def process_fields(text,pid):
    def field(name):
        values=re.findall(r'^'+name+r'=(.*)\r?$',text,re.M)
        if len(values)!=1:raise RuntimeError('DEBUGGER_CURRENT_PID_FIELD:'+name)
        return values[0].rstrip('\r')
    stat=field('MDDS_DEBUG_STAT');parts=stat.rsplit(')',1)
    if len(parts)!=2 or not stat.startswith(str(pid)+' (') or len(parts[1].split())<20:raise RuntimeError('DEBUGGER_CURRENT_STAT_INVALID')
    blocks=re.findall(r'^MDDS_DEBUG_CMDLINE_HEX_BEGIN\r?\n(.*?)^MDDS_DEBUG_CMDLINE_HEX_END\r?$',text,re.M|re.S)
    if len(blocks)!=1 or text.count('MDDS_DEBUG_CMDLINE_HEX_BEGIN')!=1 or text.count('MDDS_DEBUG_CMDLINE_HEX_END')!=1 or len(blocks[0])>32768 or not re.fullmatch(r'[0-9a-fA-F\s]+',blocks[0]):raise RuntimeError('DEBUGGER_CMDLINE_HEX_INVALID')
    try:data=bytes.fromhex(blocks[0])
    except ValueError:raise RuntimeError('DEBUGGER_CMDLINE_HEX_INVALID')
    if not data.endswith(b'\0'):raise RuntimeError('DEBUGGER_CMDLINE_TRUNCATED')
    try:argv=[p.decode('utf-8',errors='strict') for p in data[:-1].split(b'\0')]
    except UnicodeError:raise RuntimeError('DEBUGGER_CMDLINE_ENCODING')
    return dict(exe=field('MDDS_DEBUG_EXE'),starttime=parts[1].split()[19],argv=argv)

def process_identity(text,pid,expected_exe,starttime):
    observed=process_fields(text,pid)
    if observed['exe']!=expected_exe or observed['argv'][0]!=expected_exe or observed['starttime']!=str(starttime):raise RuntimeError('DEBUGGER_PID_REUSED_OR_WRONG_PROGRAM')

def process_command(pid):
    return f"(if [ ! -d /proc/{pid} ]; then echo MDDS_DEBUG_PROCESS_ABSENT=1; exit 0; fi; test -r /proc/{pid}/stat || exit 4; printf 'MDDS_DEBUG_EXE='; readlink /proc/{pid}/exe || exit 5; printf 'MDDS_DEBUG_STAT='; cat /proc/{pid}/stat || exit 6; printf 'MDDS_DEBUG_CMDLINE_HEX_BEGIN\\n'; od -v -An -tx1 /proc/{pid}/cmdline || exit 7; printf 'MDDS_DEBUG_CMDLINE_HEX_END\\n'); mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"

def start_command(remote,port,pid):
    return f'env -i PATH=/system/bin:/system/xbin {remote}/lldb-server gdbserver 127.0.0.1:{port} --attach {pid} & mdds_server=$!; echo MDDS_DEBUG_SERVER_PID=$mdds_server; wait $mdds_server; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'

def cleanup_command(remote,port,pid,server_pid,server_starttime=None):
    expected=[remote+'/lldb-server','gdbserver','127.0.0.1:'+str(port),'--attach',str(pid)]
    encoded=b'\0'.join(x.encode() for x in expected)+b'\0';birth=str(server_starttime) if server_starttime is not None else 'UNOBSERVED'
    # All final identity comparisons and signal are in this same shell. No tr,
    # command pipeline, arbitrary process-name kill, or stale Python-only check.
    return f"(if [ -d /proc/{server_pid} ]; then IFS= read -r s < /proc/{server_pid}/stat || exit 2; f=${{s##*) }}; [ \"$f\" != \"$s\" ] || exit 2; set -f; set -- $f; [ \"$#\" -ge 20 ] || exit 2; shift 19; [ \"$1\" = '{birth}' ] || exit 2; [ \"$(readlink /proc/{server_pid}/exe)\" = '{remote}/lldb-server' ] || exit 2; h=$(od -v -An -tx1 /proc/{server_pid}/cmdline) || exit 2; v=; for b in $h; do v=$v$b; done; [ \"$v\" = '{encoded.hex()}' ] || exit 2; kill -TERM {server_pid} || exit 3; fi); mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"

def status_command(pid):
    return f'if [ -d /proc/{pid} ]; then cat /proc/{pid}/status; else echo MDDS_DEBUG_MAIN_ABSENT=1; fi; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'

def resume_command(pid,exe,birth,argv):
    """One owned OS group-stop resume; never terminate or release SDK owners."""
    if type(pid) is not int or pid<1 or not re.fullmatch(r'/[A-Za-z0-9_./+\-]+',exe) or not re.fullmatch(r'[0-9]+',str(birth)) or not argv or argv[0]!=exe:raise ValueError('DEBUGGER_RESUME_IDENTITY')
    encoded=(b'\0'.join(x.encode('utf-8') for x in argv)+b'\0').hex()
    return f"(test -d /proc/{pid} || exit 4; IFS= read -r s < /proc/{pid}/stat || exit 5; f=${{s##*) }}; [ \"$f\" != \"$s\" ] || exit 5; set -f; set -- $f; [ \"$#\" -ge 20 ] || exit 5; shift 19; [ \"$1\" = '{birth}' ] || exit 6; [ \"$(readlink /proc/{pid}/exe)\" = '{exe}' ] || exit 6; h=$(od -v -An -tx1 /proc/{pid}/cmdline) || exit 6; v=; for b in $h; do v=$v$b; done; [ \"$v\" = '{encoded}' ] || exit 6; tracer=; state=; while read -r k value rest; do case \"$k\" in TracerPid:) tracer=$value;; State:) state=$value;; esac; done < /proc/{pid}/status; [ \"$tracer\" = 0 ] && [ \"$state\" = T ] || exit 7; kill -CONT {pid} || exit 8); mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"

def absent_command(pid):
    return f'test ! -d /proc/{pid}; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'

def current_process(base,root,label,pid,cfg,side,actual,seconds=8):
    checked=Command(root,label,base+['shell',process_command(pid)],seconds);text=remote_result(checked)
    process_identity(text,pid,cfg['remote'][side]+'/'+cfg['main_member'],actual['starttime'])

def startup_seconds(deadline,maximum=8):
    remaining=deadline-time.monotonic()
    if remaining<=0:raise RuntimeError('DEBUGGER_SHARED_STARTUP_DEADLINE')
    return min(maximum,remaining)

def observed_owned_tracer(base,root,pid,cfg,side,actual,deadline):
    """Positive actual attach proof when HDC withholds the server's live text."""
    current_process(base,root,'pid-before-tracer-proof',pid,cfg,side,actual,startup_seconds(deadline))
    command=Command(root,'tracer-proof-status',base+['shell',status_command(pid)],startup_seconds(deadline));text=remote_result(command)
    ids=re.findall(r'^TracerPid:\s+([1-9][0-9]*)\s*$',text,re.M)
    if len(ids)!=1 or not re.search(r'^State:\s+t\b',text,re.M):raise RuntimeError('DEBUGGER_ORIGINAL_TRACING_STOP_NOT_OBSERVED')
    server_pid=int(ids[0]);command=Command(root,'tracer-proof-identity',base+['shell',process_command(server_pid)],startup_seconds(deadline))
    identity=process_fields(remote_result(command),server_pid)
    expected=[cfg['remote'][side]+'/lldb-server','gdbserver','127.0.0.1:'+str(cfg['rmw_debugger']['ports'][side]),'--attach',str(pid)]
    if identity['exe']!=expected[0] or identity['argv']!=expected:raise RuntimeError('DEBUGGER_TRACER_NOT_OWNED')
    write(root/'OWNED_TRACER.json',dict(run=cfg['run'],side=side,pid=pid,server_pid=server_pid,server_starttime=identity['starttime'],
        identity_source='ACTUAL_MAIN_TRACER_PID_AND_EXE_ARGV_BIRTH_NOT_SERVER_STDOUT'))
    return server_pid

def confirm_visible_server_pid(root,server):
    proof_file=root/'OWNED_TRACER.json'
    if not proof_file.is_file():return None
    proof=json.loads(proof_file.read_text());ids=re.findall(r'^MDDS_DEBUG_SERVER_PID=([1-9][0-9]*)\r?$',server.buffers['stdout'].decode('utf-8',errors='replace'),re.M)
    if ids and ids!=[str(proof['server_pid'])]:raise RuntimeError('SERVER_IDENTITY_PROOFS_CONTRADICT')
    return proof

def prerelease_origin(window,side,pid,run):
    """Consume the observer's completed pre-release proof, never postrun output."""
    live=window/side/'load-pre-release-analysis.json'
    summary=json.loads((window/side/'load-pre-release-summary.json').read_text(encoding='utf-8-sig'))
    actual=json.loads(live.read_text(encoding='utf-8-sig'))
    names={'load-status.txt','load-exe.txt','load-cwd.txt','load-cmdline.txt','load-environ.txt','load-stat-before.txt','load-stat-after.txt','load-uptime.txt','load-maps.txt'}
    rows=summary.get('transfer',[])
    if (summary.get('run')!=run or summary.get('side')!=side or summary.get('host_collector_test') is not False
        or summary.get('pass') is not True or summary.get('formal_complete') is not True
        or summary.get('formal_exit')!=0 or summary.get('formal_child_closed') is not True
        or summary.get('collection_cleanup')!='CHILD_STREAMS_CLOSED'
        or summary.get('formal_report_sha256')!=sha(live)
        or len(rows)!=len(names) or {r.get('name') for r in rows}!=names):
        raise ValueError('DEBUGGER_PRERELEASE_PROOF_INVALID')
    if (actual.get('complete') is not True or actual.get('scope')!='LIVE_PROC_SNAPSHOT_NOT_FULL_SDK_MODULE_CLOSURE' or actual.get('run')!=run or actual.get('board')!=side
        or str(actual.get('pid'))!=str(pid) or not re.fullmatch('[0-9]+',str(actual.get('starttime','')))
        or actual.get('errors')!=[]):raise ValueError('DEBUGGER_LIVE_PID_NOT_VERIFIED')
    for row in rows:
        if (row.get('pass') is not True or row.get('present') is not True or row.get('closed') is not True
            or row.get('file_error') or row.get('child_error') or row.get('receipt_error')
            or sha(window/side/'load'/row['name'])!=row.get('sha256')):
            raise ValueError('DEBUGGER_PRERELEASE_RAW_CHANGED')
    for name,expected in actual.get('evidence_sha256',{}).items():
        if Path(name).name!=name or sha(window/side/'load'/name)!=expected:raise ValueError('DEBUGGER_PRERELEASE_ANALYSIS_SOURCE_CHANGED')
    if not actual.get('evidence_sha256'):raise ValueError('DEBUGGER_PRERELEASE_ANALYSIS_SOURCE_MISSING')
    return actual

def validate_inputs(window,side,pid,cfg):
    if side not in ('A','B') or type(pid) is not int or pid<1 or not re.fullmatch('[0-9a-f]{16}',cfg['run']):raise ValueError('DEBUGGER_IDENTITY')
    if cfg.get('simulated') or cfg.get('local_contract_test') or not (window/'DATA_ATTEMPT_STARTED').is_file():raise ValueError('DEBUGGER_REAL_DISPATCH_REQUIRED')
    import task_contract as contract
    proof=cfg['task_contract']
    if contract.load(proof['freeze_path'])!=proof:raise ValueError('DEBUGGER_CONTRACT_DRIFT')
    contract.execution_admission(proof)
    frozen=json.loads(Path(proof['freeze_path']).read_text(encoding='utf-8-sig'))
    contract.verify_ros_task_grant(frozen);contract.verify_private_binding(cfg);contract.ros_shape(cfg)
    if Path(cfg['window_root']).resolve()!=window or window.parent!=contract.TASK_ROOT/'windows':raise ValueError('DEBUGGER_WINDOW_IDENTITY')
    marker=json.loads((window/'DATA_ATTEMPT_STARTED').read_text(encoding='utf-8-sig'))
    claim=json.loads(contract.ros_claim_path('data',cfg['ros_attempt']).read_text(encoding='utf-8-sig'))
    if marker!=claim or marker.get('run')!=cfg['run'] or Path(marker.get('window','')).resolve()!=window or marker.get('task_id')!=frozen['task_id'] or marker.get('kind')!='DATA_DISPATCH_INTENT' or marker.get('simulated') is not False:raise ValueError('DEBUGGER_DATA_CLAIM_IDENTITY')
    spec=cfg['rmw_debugger']
    for key in ('client','server','script'):
        if not Path(spec[key]['path']).is_absolute() or sha(spec[key]['path'])!=spec[key]['sha256']:raise ValueError('DEBUGGER_INPUT_CHANGED:'+key)
    verify_instruction_image(spec['script']['path'],Path(cfg['package'])/'librmw_fastrtps_shared_cpp.so')
    remote=cfg['remote'][side];port=spec['ports'][side]
    if remote!=f"/data/local/tmp/mdds_dds_{cfg['run']}_{side}" or type(port) is not int or not 1024<=port<=65535:raise ValueError('DEBUGGER_PATH_OR_PORT')
    # The observer binds main/PID before invoking this support entry. Recheck
    # its saved value, never accept an unrelated process merely by number.
    return prerelease_origin(window,side,pid,cfg['run'])

def readonly_failure_facts(window,side):
    """Offline closure of debugger reads only; never a business/SDK guard."""
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
    if side not in ('A','B') or cfg.get('simulated') or cfg.get('local_contract_test'):raise ValueError('DEBUGGER_RECOVERY_REAL_IDENTITY')
    run=cfg['run'];root=window/side/'debugger';task=Path(cfg['task_contract']['task_root']).resolve()
    if window.parent!=task/'windows' or Path(cfg['window_root']).resolve()!=window:raise ValueError('DEBUGGER_RECOVERY_FOREIGN_WINDOW')
    contract_path=Path(cfg['task_contract']['freeze_path']);frozen=json.loads(contract_path.read_text(encoding='utf-8-sig'))
    if sha(contract_path)!=cfg['task_contract']['freeze_sha256']:raise ValueError('DEBUGGER_RECOVERY_CONTRACT_CHANGED')
    original=task/'executor/rmw_debugger.py'
    repo=next(p for p in window.parents if (p/'.git').exists())
    expected={str((repo/e['path']).resolve()):e['sha256'] for e in frozen['execution_files']}
    if expected.get(str(original))!=sha(original):raise ValueError('DEBUGGER_RECOVERY_ORIGINAL_SOURCE_CHANGED')
    marker=json.loads((window/'DATA_ATTEMPT_STARTED').read_text(encoding='utf-8-sig'))
    if marker.get('run')!=run or Path(marker.get('window','')).resolve()!=window or marker.get('kind')!='DATA_DISPATCH_INTENT' or marker.get('simulated') is not False:raise ValueError('DEBUGGER_RECOVERY_DISPATCH_IDENTITY')
    record=json.loads((window/'result.json').read_text(encoding='utf-8-sig'))
    row=record['debugger']['sides'][side];pid=row['pid']
    if type(pid) is not int or pid<1 or not row.get('error') or row.get('code')==0:raise ValueError('DEBUGGER_RECOVERY_FAILURE_IDENTITY')
    if row.get('run')!=run or row.get('side')!=side or row.get('closed') is not True or row.get('overflow') is not False or type(row.get('code')) is not int or row.get('signal') is not None or row.get('argv')!=[str(original),'--window',str(window),'--side',side,'--pid',str(pid)]:raise ValueError('DEBUGGER_RECOVERY_MANAGER_NOT_CLOSED')
    if record.get('timedOut') or record.get('collectionCleanup')!='HDC_STREAMS_CLOSED':raise ValueError('DEBUGGER_RECOVERY_COLLECTOR_NOT_CLOSED')
    spec=importlib.util.spec_from_file_location('readonly_original_debugger',original);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    origin=module.prerelease_origin(window,side,pid,run)
    labels={p.name[:-len('.intent.json')] for p in root.glob('*.intent.json')}
    if not labels or not labels<={'pid-before-attach','forward-before'}:raise ValueError('DEBUGGER_RECOVERY_MUTATION_OR_UNKNOWN_INTENT')
    for suffix in ('started.json','result.json'):
        if {p.name[:-len('.'+suffix)] for p in root.glob('*.'+suffix)}!=labels:raise ValueError('DEBUGGER_RECOVERY_ACTION_INCOMPLETE')
    receipts={};base=[cfg['hdc_path'],'-t',cfg['targets'][side]]
    for label in sorted(labels):
        argv=base+(['shell',module.process_command(pid)] if label=='pid-before-attach' else ['fport','ls'])
        intent=json.loads((root/(label+'.intent.json')).read_text());started=json.loads((root/(label+'.started.json')).read_text());result=json.loads((root/(label+'.result.json')).read_text())
        for entry in (intent,started,result):
            if entry.get('argv')!=argv:raise ValueError('DEBUGGER_RECOVERY_WRONG_COMMAND')
        if any(e.get('cwd')!=str(root) or e.get('purpose')!='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS' for e in (intent,started)):raise ValueError('DEBUGGER_RECOVERY_ACTION_IDENTITY')
        if type(started.get('local_pid')) is not int or started['local_pid']<1 or result.get('local_pid')!=started['local_pid'] or type(result.get('exit_code')) is not int or result.get('timed_out') is not False or result.get('capture_complete') is not True or result.get('overflow') is not False or result.get('termination') is not None:raise ValueError('DEBUGGER_RECOVERY_CHILD_UNCONFIRMED')
        for stream in ('stdout','stderr'):
            if sha(root/(label+'.'+stream))!=result.get(stream+'_sha256'):raise ValueError('DEBUGGER_RECOVERY_STREAM_CHANGED')
        for suffix in ('intent.json','started.json','result.json','stdout','stderr'):
            p=root/(label+'.'+suffix);receipts[str(p.relative_to(window))]=sha(p)
    for name in ('window.json','DATA_ATTEMPT_STARTED','result.json',side+'/debugger-manager.stdout',side+'/debugger-manager.stderr',side+'/load-pre-release-analysis.json',side+'/load-pre-release-summary.json'):
        receipts[name]=sha(window/name)
    # Direct files not inventoried are unknown actions, never inferred absent.
    if {p.name for p in root.iterdir() if p.is_file()}-{'RESULT.json'}!={label+'.'+suffix for label in labels for suffix in ('intent.json','started.json','result.json','stdout','stderr')}:raise ValueError('DEBUGGER_RECOVERY_UNACCOUNTED_FILE')
    return dict(schema='DEBUGGER_READ_ONLY_FAILURE_CLOSURE/v1',run=run,window=str(window),side=side,pid=pid,
        original_manager_sha256=sha(original),contract_sha256=sha(contract_path),recovery_tool_path=str(Path(__file__).resolve()),recovery_tool_sha256=sha(__file__),
        labels=sorted(labels),receipts=receipts,decision='SUPPORT_READS_CLOSED_NOT_BUSINESS_PASS',
        observed_failure=row.get('error'),observation_result='NOT_OBSERVED',mutation_intents=[],normal_guards_required=True)

def append_readonly_failure_closure(window,side):
    facts=readonly_failure_facts(window,side);root=Path(window)/side/'debugger/recovery/attempt-1';root.mkdir(parents=True,exist_ok=False)
    write(root/'READ_ONLY_FAILURE_CLOSURE.json',facts)
    return facts

def verify_readonly_failure_closure(window,side):
    path=Path(window)/side/'debugger/recovery/attempt-1/READ_ONLY_FAILURE_CLOSURE.json'
    facts=readonly_failure_facts(window,side)
    if json.loads(path.read_text())!=facts:raise ValueError('DEBUGGER_RECOVERY_DERIVED_RECEIPT_CHANGED')
    acceptance=json.loads((path.parent/'INDEPENDENT_ACCEPTANCE.json').read_text())
    if acceptance.get('decision')!='ACCEPTED_SUPPORT_READ_ONLY_CLOSURE' or acceptance.get('run')!=facts['run'] or acceptance.get('side')!=side or acceptance.get('closure_sha256')!=sha(path) or acceptance.get('recovery_tool_sha256')!=sha(__file__):raise ValueError('DEBUGGER_RECOVERY_INDEPENDENT_ACCEPTANCE_MISSING')
    evidence=Path(acceptance['review_path'])
    if not evidence.is_file() or sha(evidence)!=acceptance.get('review_sha256'):raise ValueError('DEBUGGER_RECOVERY_REVIEW_CHANGED')
    review=json.loads(evidence.read_text(encoding='utf-8-sig'))
    if (review.get('scope')!='INDEPENDENT_DEBUGGER_READ_ONLY_RECOVERY_ACCEPTANCE' or review.get('verdict')!='PASS'
        or review.get('run')!=facts['run'] or review.get('window')!=facts['window']
        or review.get('recovery_tool_sha256')!=sha(__file__) or review.get('normal_guards_pass') is not True
        or review.get('sides',{}).get(side)!=dict(closure_sha256=sha(path),decision='ACCEPTED_SUPPORT_READ_ONLY_CLOSURE')):
        raise ValueError('DEBUGGER_RECOVERY_REVIEW_NOT_ACCEPTED_FOR_THIS_WINDOW')
    return facts

def verify_bound_readonly_failure_closure(window,side):
    """Use the immutable materialized reader that originally closed this failure."""
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
    path=window/side/'debugger/recovery/attempt-1/READ_ONLY_FAILURE_CLOSURE.json';row=json.loads(path.read_text())
    reader=Path(row['recovery_tool_path']).resolve();task=reader.parent.parent
    authority=Path(cfg['task_contract']['task_root']).resolve().parent.parent
    if task.parent!=authority/'iterations' or not re.fullmatch(r'iteration-[1-9][0-9]*',task.name) or reader!=task/'executor/rmw_debugger.py' or sha(reader)!=row['recovery_tool_sha256']:raise ValueError('DEBUGGER_RECOVERY_READER_ORIGIN')
    manifest=json.loads((task/'MATERIALIZATION.json').read_text(encoding='utf-8-sig'))
    matches=[r for r in manifest['files'] if r['destination']=='executor/rmw_debugger.py' and r['source']=='scripts/public_socket/shared_reattach/executor/rmw_debugger.py']
    if len(matches)!=1 or matches[0]['sha256']!=sha(reader):raise ValueError('DEBUGGER_RECOVERY_READER_NOT_MATERIALIZED')
    spec=importlib.util.spec_from_file_location('bound_readonly_debugger_recovery',reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module.verify_readonly_failure_closure(window,side)

def failed_forward_facts(window,side):
    """Bind a fully captured failed forward attempt; no server/attach permitted."""
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
    if side not in ('A','B') or cfg.get('simulated') or cfg.get('local_contract_test'):raise ValueError('FORWARD_RECOVERY_REAL_WINDOW_REQUIRED')
    task=Path(cfg['task_contract']['task_root']).resolve();root=window/side/'debugger'
    if window.parent!=task/'windows' or Path(cfg['window_root']).resolve()!=window:raise ValueError('FORWARD_RECOVERY_FOREIGN_WINDOW')
    frozen_path=Path(cfg['task_contract']['freeze_path']);frozen=json.loads(frozen_path.read_text(encoding='utf-8-sig'))
    original=task/'executor/rmw_debugger.py';repo=next(p for p in window.parents if (p/'.git').exists())
    rows={str((repo/r['path']).resolve()):r['sha256'] for r in frozen['execution_files']}
    if sha(frozen_path)!=cfg['task_contract']['freeze_sha256'] or rows.get(str(original))!=sha(original):raise ValueError('FORWARD_RECOVERY_ORIGINAL_SOURCE_CHANGED')
    marker=json.loads((window/'DATA_ATTEMPT_STARTED').read_text(encoding='utf-8-sig'))
    if marker.get('run')!=cfg['run'] or Path(marker.get('window','')).resolve()!=window or marker.get('kind')!='DATA_DISPATCH_INTENT' or marker.get('simulated') is not False:raise ValueError('FORWARD_RECOVERY_CLAIM_IDENTITY')
    result=json.loads((window/'result.json').read_text(encoding='utf-8-sig'));manager=result['debugger']['sides'][side]
    pid=manager['pid']
    if type(pid) is not int or pid<1 or manager.get('run')!=cfg['run'] or manager.get('side')!=side or manager.get('closed') is not True or manager.get('overflow') is not False or manager.get('code')!=1 or manager.get('signal') is not None or manager.get('argv')!=[str(original),'--window',str(window),'--side',side,'--pid',str(pid)]:raise ValueError('FORWARD_RECOVERY_MANAGER_NOT_CLOSED')
    if result.get('timedOut') or result.get('collectionCleanup')!='HDC_STREAMS_CLOSED':raise ValueError('FORWARD_RECOVERY_COLLECTOR_NOT_CLOSED')
    terminal=json.loads((root/'RESULT.json').read_text())
    if terminal.get('run')!=cfg['run'] or terminal.get('side')!=side or terminal.get('pid')!=pid or terminal.get('manager_sha256')!=sha(original) or terminal.get('support_closed') is not False or terminal.get('forward_owned') is not True or terminal.get('server_started') is not False or terminal.get('server') is not None or terminal.get('client') is not None:raise ValueError('FORWARD_RECOVERY_ORIGINAL_STATE_NOT_SUPPORTED')
    spec=importlib.util.spec_from_file_location('failed_forward_original',original);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    contract_spec=importlib.util.spec_from_file_location('failed_forward_original_contract',task/'executor/task_contract.py');original_contract=importlib.util.module_from_spec(contract_spec);contract_spec.loader.exec_module(original_contract)
    if rows.get(str(task/'executor/task_contract.py'))!=sha(task/'executor/task_contract.py'):raise ValueError('FORWARD_RECOVERY_ORIGINAL_CONTRACT_TOOL_CHANGED')
    original_contract.verify_ros_task_grant(frozen);original_contract.verify_private_binding(cfg);original_contract.ros_shape(cfg);original_contract.verify_installed_device_mode(frozen,cfg)
    origin=module.prerelease_origin(window,side,pid,cfg['run'])
    base=[cfg['hdc_path'],'-t',cfg['targets'][side]];node='tcp:'+str(cfg['rmw_debugger']['ports'][side])
    commands={'pid-before-attach':base+['shell',module.process_command(pid)],'forward-before':base+['fport','ls'],
        'forward':base+['fport',node,node],'forward-after':base+['fport','ls'],'forward-remove':base+['fport','rm',node+' '+node]}
    receipts=terminal['receipts'];expected={label+'.'+suffix for label in commands for suffix in ('intent.json','started.json','result.json','stdout','stderr')}
    if set(receipts)!=expected or {p.name for p in root.iterdir() if p.is_file()}!=expected|{'RESULT.json'}:raise ValueError('FORWARD_RECOVERY_UNKNOWN_OR_INCOMPLETE_ACTION')
    outputs={}
    for label,argv in commands.items():
        intent=json.loads((root/(label+'.intent.json')).read_text());started=json.loads((root/(label+'.started.json')).read_text());row=json.loads((root/(label+'.result.json')).read_text())
        if any(r.get('argv')!=argv for r in (intent,started,row)) or any(r.get('cwd')!=str(root) or r.get('purpose')!='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS' for r in (intent,started)):raise ValueError('FORWARD_RECOVERY_COMMAND_IDENTITY')
        if type(started.get('local_pid')) is not int or started['local_pid']<1 or row.get('local_pid')!=started['local_pid'] or row.get('exit_code')!=0 or row.get('timed_out') is not False or row.get('capture_complete') is not True or row.get('overflow') is not False or row.get('termination') is not None:raise ValueError('FORWARD_RECOVERY_COMMAND_NOT_CLOSED')
        for suffix in ('intent.json','started.json','result.json','stdout','stderr'):
            name=label+'.'+suffix
            if sha(root/name)!=receipts[name]:raise ValueError('FORWARD_RECOVERY_ORIGINAL_RECEIPT_CHANGED')
        for stream in ('stdout','stderr'):
            if sha(root/(label+'.'+stream))!=row.get(stream+'_sha256'):raise ValueError('FORWARD_RECOVERY_RAW_CHANGED')
        outputs[label]=(root/(label+'.stdout')).read_text()
    module.process_identity(outputs['pid-before-attach'],pid,cfg['remote'][side]+'/'+cfg['main_member'],origin['starttime'])
    if re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\r?$',outputs['pid-before-attach'],re.M)!=['0']:raise ValueError('FORWARD_RECOVERY_PID_READ_INCOMPLETE')
    if any(node in row[1:3] for row in forward_entries(outputs['forward-before'])) or outputs['forward'].strip()!='Forwardport result:OK':raise ValueError('FORWARD_RECOVERY_CREATION_NOT_OWNED')
    target=cfg['targets'][side]
    if forward_entries(outputs['forward-after']).count((target,node,node,'Forward'))!=1 or '[Fail]Remove forward ruler failed' not in outputs['forward-remove']:raise ValueError('FORWARD_RECOVERY_FAILURE_SHAPE_CHANGED')
    refs={str((root/name).relative_to(window)):checksum for name,checksum in receipts.items()}
    for name in ('window.json','DATA_ATTEMPT_STARTED','result.json',side+'/debugger/RESULT.json',side+'/debugger-manager.stdout',side+'/debugger-manager.stderr',side+'/load-pre-release-analysis.json',side+'/load-pre-release-summary.json'):refs[name]=sha(window/name)
    return dict(schema='DEBUGGER_FORWARD_RECOVERY/v1',run=cfg['run'],window=str(window),side=side,target=target,node=node,
        original_manager_sha256=sha(original),original_receipts=refs,recovery_tool_path=str(Path(__file__).resolve()),recovery_tool_sha256=sha(__file__),
        observation_result='NOT_OBSERVED',normal_guards_required=True,server_or_attach_started=False)

def recover_forward_only(window,side):
    window=Path(window).resolve()
    facts=failed_forward_facts(window,side);cfg=json.loads((Path(window)/'window.json').read_text())
    import task_contract as contract
    contract._require_window_guards_restored(Path(window).resolve())
    root=Path(window)/side/'debugger/recovery/attempt-1';root.mkdir(parents=True,exist_ok=False)
    write(root/'ORIGINAL_FORWARD_FAILURE.json',facts)
    base=[cfg['hdc_path'],'-t',cfg['targets'][side]];node=facts['node'];owned=(facts['target'],node,node,'Forward')
    current=Command(root,'forward-current',base+['fport','ls'],8);before=forward_entries(command_text(current))
    if before.count(owned)!=1 or any(node in row[1:3] and row!=owned for row in before):raise RuntimeError('FORWARD_RECOVERY_CURRENT_OWNERSHIP_UNCONFIRMED')
    removed=Command(root,'forward-remove',forward_remove_argv(base,node),8);text=command_text(removed).strip()
    if text!='Remove forward ruler success, ruler:'+node+' '+node:raise RuntimeError('FORWARD_RECOVERY_REMOVE_NOT_CONFIRMED')
    final=Command(root,'forward-final',base+['fport','ls'],8);after=forward_entries(command_text(final))
    if sorted(after)!=sorted(row for row in before if row!=owned):raise RuntimeError('FORWARD_RECOVERY_FOREIGN_CHANGE_OR_OWN_TASK_REMAINS')
    facts.update(decision='OWNED_FORWARD_REMOVED_NOT_BUSINESS_PASS',foreign_rows_unchanged=True,
        recovery_receipts={p.name:sha(p) for p in root.iterdir() if p.is_file()})
    write(root/'DEBUGGER_FORWARD_RECOVERY.json',facts);return facts

def verify_forward_recovery(window,side):
    root=Path(window)/side/'debugger/recovery/attempt-1';p=root/'DEBUGGER_FORWARD_RECOVERY.json';row=json.loads(p.read_text())
    facts=failed_forward_facts(window,side)
    if any(row.get(k)!=v for k,v in facts.items()) or row.get('decision')!='OWNED_FORWARD_REMOVED_NOT_BUSINESS_PASS' or row.get('foreign_rows_unchanged') is not True:raise ValueError('FORWARD_RECOVERY_FACTS_CHANGED')
    base=[json.loads((Path(window)/'window.json').read_text())['hdc_path'],'-t',facts['target']];node=facts['node']
    commands={'forward-current':base+['fport','ls'],'forward-remove':forward_remove_argv(base,node),'forward-final':base+['fport','ls']}
    expected={'ORIGINAL_FORWARD_FAILURE.json'}|{label+'.'+suffix for label in commands for suffix in ('intent.json','started.json','result.json','stdout','stderr')}
    refs=row.get('recovery_receipts',{})
    if set(refs)!=expected:raise ValueError('FORWARD_RECOVERY_JOURNAL_INCOMPLETE')
    for suffix in ('intent.json','started.json','result.json'):
        if {p.name[:-len('.'+suffix)] for p in root.glob('*.'+suffix)}!=set(commands):raise ValueError('FORWARD_RECOVERY_UNACCOUNTED_DISPATCH')
    outputs={}
    for name,checksum in refs.items():
        if sha(root/name)!=checksum:raise ValueError('FORWARD_RECOVERY_JOURNAL_CHANGED')
    for label,argv in commands.items():
        intent=json.loads((root/(label+'.intent.json')).read_text());started=json.loads((root/(label+'.started.json')).read_text());result=json.loads((root/(label+'.result.json')).read_text())
        if any(r.get('argv')!=argv for r in (intent,started,result)) or any(r.get('cwd')!=str(root) or r.get('purpose')!='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS' for r in (intent,started)):raise ValueError('FORWARD_RECOVERY_COMMAND_IDENTITY')
        if type(started.get('local_pid')) is not int or started['local_pid']<1 or result.get('local_pid')!=started['local_pid'] or result.get('exit_code')!=0 or result.get('timed_out') is not False or result.get('capture_complete') is not True or result.get('overflow') is not False or result.get('termination') is not None:raise ValueError('FORWARD_RECOVERY_COMMAND_NOT_CLOSED')
        for stream in ('stdout','stderr'):
            if sha(root/(label+'.'+stream))!=result.get(stream+'_sha256'):raise ValueError('FORWARD_RECOVERY_STREAM_CHANGED')
        outputs[label]=(root/(label+'.stdout')).read_text()
    owned=(facts['target'],node,node,'Forward');before=forward_entries(outputs['forward-current']);after=forward_entries(outputs['forward-final'])
    if before.count(owned)!=1 or any(node in r[1:3] and r!=owned for r in before) or sorted(after)!=sorted(r for r in before if r!=owned) or outputs['forward-remove'].strip()!='Remove forward ruler success, ruler:'+node+' '+node:raise ValueError('FORWARD_RECOVERY_STATE_NOT_CONFIRMED')
    acceptance=json.loads((root/'INDEPENDENT_ACCEPTANCE.json').read_text());review_path=Path(acceptance['review_path']);review=json.loads(review_path.read_text(encoding='utf-8-sig'))
    if (acceptance.get('decision')!='ACCEPTED_OWNED_FORWARD_RECOVERY' or acceptance.get('run')!=facts['run'] or acceptance.get('side')!=side or acceptance.get('recovery_sha256')!=sha(p)
        or acceptance.get('review_sha256')!=sha(review_path) or review.get('scope')!='INDEPENDENT_DEBUGGER_FORWARD_RECOVERY_ACCEPTANCE' or review.get('verdict')!='PASS'
        or review.get('run')!=facts['run'] or review.get('window')!=facts['window'] or review.get('normal_guards_pass') is not True or review.get('recovery_tool_sha256')!=sha(__file__)
        or review.get('sides',{}).get(side)!=dict(recovery_sha256=sha(p),decision='ACCEPTED_OWNED_FORWARD_RECOVERY')):raise ValueError('FORWARD_RECOVERY_INDEPENDENT_ACCEPTANCE_REQUIRED')
    return row

def verify_bound_forward_recovery(window,side):
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text())
    row=json.loads((window/side/'debugger/recovery/attempt-1/DEBUGGER_FORWARD_RECOVERY.json').read_text())
    reader=Path(row['recovery_tool_path']).resolve();task=reader.parent.parent;authority=Path(cfg['task_contract']['task_root']).resolve().parent.parent
    if task.parent!=authority/'iterations' or not re.fullmatch(r'iteration-[1-9][0-9]*',task.name) or reader!=task/'executor/rmw_debugger.py' or sha(reader)!=row['recovery_tool_sha256']:raise ValueError('FORWARD_RECOVERY_READER_ORIGIN')
    manifest=json.loads((task/'MATERIALIZATION.json').read_text());matches=[r for r in manifest['files'] if r['destination']=='executor/rmw_debugger.py' and r['source']=='scripts/public_socket/shared_reattach/executor/rmw_debugger.py']
    if len(matches)!=1 or matches[0]['sha256']!=sha(reader):raise ValueError('FORWARD_RECOVERY_READER_NOT_MATERIALIZED')
    spec=importlib.util.spec_from_file_location('bound_forward_recovery',reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    argument=window
    saved=json.loads((window/side/'debugger/recovery/attempt-1/forward-current.intent.json').read_text())['cwd']
    if not Path(saved).is_absolute():
        # This exact deployed reader recorded its original relative argument.
        # Replay that syntax only in the declared repo context. All original
        # argv/raw hashes/decisions remain unchanged; no path is rewritten.
        legacy='89fad0e9f7b21c13da531e140766d033e5e5896e9033204c11de29809c4da8bd'
        repo=next(p for p in window.parents if (p/'.git').exists())
        expected=str((window/side/'debugger/recovery/attempt-1').relative_to(repo))
        if row['recovery_tool_sha256']!=legacy or Path(cfg.get('repo','')).resolve()!=repo or saved!=expected or Path.cwd().resolve()!=repo:raise ValueError('FORWARD_RECOVERY_LEGACY_CWD_CONTEXT_NOT_BOUND')
        for label in ('forward-current','forward-remove','forward-final'):
            for suffix in ('intent.json','started.json'):
                record=json.loads((window/side/'debugger/recovery/attempt-1'/(label+'.'+suffix)).read_text())
                if record.get('cwd')!=expected:raise ValueError('FORWARD_RECOVERY_MIXED_LEGACY_CWD')
        argument=window.relative_to(repo)
    return module.verify_forward_recovery(argument,side)

def resource_recovery_facts(window):
    """Recheck this real task's current resources, never rewrite failed capture."""
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text());task=Path(cfg['task_contract']['task_root']).resolve()
    if cfg.get('simulated') or cfg.get('local_contract_test') or cfg.get('scenario')!='ROS2_DSOFTBUS' or window.parent!=task/'windows':raise ValueError('RESOURCE_RECOVERY_REAL_TASK_ONLY')
    frozen_path=Path(cfg['task_contract']['freeze_path']);frozen=json.loads(frozen_path.read_text());original_contract=task/'executor/task_contract.py'
    repo=Path(cfg['repo']).resolve();selected={str((repo/e['path']).resolve()):e['sha256'] for e in frozen['execution_files']}
    if sha(frozen_path)!=cfg['task_contract']['freeze_sha256'] or selected.get(str(original_contract))!=sha(original_contract):raise ValueError('RESOURCE_RECOVERY_ORIGINAL_SOURCE_CHANGED')
    spec=importlib.util.spec_from_file_location('resource_original_contract',original_contract);c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
    c.verify_ros_task_grant(frozen);c.verify_private_binding(cfg);c.ros_shape(cfg);c.verify_installed_device_mode(frozen,cfg)
    marker=json.loads((window/'DATA_ATTEMPT_STARTED').read_text());claim=Path(frozen['task_authority_root'])/'ros2/task-ledger'/('data-'+str(cfg['ros_attempt'])+'.json')
    if json.loads(claim.read_text())!=marker or marker.get('run')!=cfg['run'] or Path(marker.get('window','')).resolve()!=window:raise ValueError('RESOURCE_RECOVERY_ORIGINAL_CLAIM_CHANGED')
    root=window/'remaining-resource-recovery/attempt-1';summary=json.loads((root/'RESOURCE_RESTORATION_RESULT.json').read_text())
    if summary.get('run')!=cfg['run'] or summary.get('window')!=str(window) or summary.get('scope')!='APPENDED_CURRENT_RESOURCE_RESTORATION_NOT_NORMAL_ROS_CLOSE' or set(summary.get('sides',{}))!={'A','B'} or any(summary.get(k)!=0 for k in ('permission_mutations','business_dispatches','directory_queries')):raise ValueError('RESOURCE_RECOVERY_SCOPE_OR_SIDES')
    if summary.get('original_summary_sha256')!=sha(window/'window-summary.json') or summary.get('original_result_sha256')!=sha(window/'result.json'):raise ValueError('RESOURCE_RECOVERY_OLD_FAILURE_CHANGED')
    helper_record=Path(frozen['task_authority_root'])/'iterations/iteration-6/windows/data-5/recovery/attempt-1/RECOVERY_RESULT.json'
    helpers=json.loads(helper_record.read_text())['sides']
    primitive=task/'recovery/pregrant_recovery.py'
    if selected.get(str(primitive))!=sha(primitive):raise ValueError('RESOURCE_RECOVERY_PRIMITIVE_SOURCE_CHANGED')
    spec=importlib.util.spec_from_file_location('resource_metadata_protocol',primitive);p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
    evidence={};count=0
    def bound(path):
        path=Path(path);evidence[str(path)]=sha(path);return path
    for name in ('window.json','window-summary.json','result.json','EXECUTION_STARTED','DATA_ATTEMPT_STARTED'):bound(window/name)
    bound(frozen_path);bound(original_contract);bound(primitive);bound(claim);bound(helper_record);bound(root/'RESOURCE_RESTORATION_RESULT.json')
    raw_accept_path=window/'INDEPENDENT_RESOURCE_RESTORATION_ACCEPTANCE.json';raw_accept=json.loads(bound(raw_accept_path).read_text())
    inventory_path=Path(raw_accept.get('evidence_inventory_path','')).resolve()
    if (raw_accept.get('scope')!='INDEPENDENT_REAL_DEBUGGER_RESOURCE_RESTORATION_ACCEPTANCE' or raw_accept.get('verdict')!='PASS'
        or raw_accept.get('environment_restored') is not True or raw_accept.get('run')!=cfg['run'] or raw_accept.get('window')!=str(window)
        or raw_accept.get('recovery_result_sha256')!=sha(root/'RESOURCE_RESTORATION_RESULT.json') or not inventory_path.is_relative_to(window)
        or sha(bound(inventory_path))!=raw_accept.get('evidence_inventory_sha256')):raise ValueError('RESOURCE_RECOVERY_RAW_INDEPENDENT_REVIEW_MISSING')
    inventory=json.loads(inventory_path.read_text())
    if inventory.get('scope')!='CURRENT_RECOVERY_REVIEW_INPUT_HASHES_NOT_ORIGINAL_BUILD_PROVENANCE' or not inventory.get('files'):raise ValueError('RESOURCE_RECOVERY_RAW_INVENTORY_MISSING')
    for entry in inventory['files']:
        path=(repo/entry['path']).resolve()
        if not path.is_relative_to(repo) or sha(bound(path))!=entry.get('sha256'):raise ValueError('RESOURCE_RECOVERY_RAW_INVENTORY_CHANGED')
    reviewed={str((repo/e['path']).resolve()) for e in inventory['files']}
    for pattern in ('*.intent.json','*.started.json','*.result.json'):
        for path in window.rglob(pattern):
            if str(path.resolve()) not in reviewed:raise ValueError('RESOURCE_RECOVERY_UNREVIEWED_DISPATCH')
    host=json.loads(bound(root/'HOST_CHILD_CURRENT.json').read_text())
    for side in ('A','B'):
        folder=root/side;before=window/side/'baseline.json';after=folder/'current-token.json';row=summary['sides'][side]
        started=json.loads(bound(window/side/'debugger/server.started.json').read_text())
        if host[side].get('original_pid')!=started['local_pid'] or host[side].get('original_started_sha256')!=sha(window/side/'debugger/server.started.json') or host[side].get('current_original_pid_absent') is not True or host[side].get('current_identity') is not None:raise ValueError('RESOURCE_RECOVERY_HOST_CHILD_NOT_CLOSED')
        bound(window/side/'debugger/RESULT.json')
        if sha(bound(before))!=sha(bound(after)) or row.get('baseline_sha256')!=sha(before) or row.get('current_sha256')!=sha(after):raise ValueError('RESOURCE_RECOVERY_PERMISSION_BYTES')
        guard=json.loads(bound(folder/'snapshot-guard.json').read_text())
        if guard.get('run')!=cfg['run'] or guard.get('side')!=side or guard.get('Passed') is not True or guard.get('byte_match') is not True or guard.get('ownership',{}).get('Passed') is not True or guard.get('snapshot',{}).get('Passed') is not True or guard['snapshot'].get('Phase')!='Revoked':raise ValueError('RESOURCE_RECOVERY_JSON_OWNERSHIP')
        baseline_meta=p.metadata(p.text(bound(window/side/'metadata-before/stdout.log')))
        meta=p.metadata((folder/'metadata-current.stdout').read_text().replace('\r',''));last_meta=p.metadata((folder/'metadata-after-denied.stdout').read_text().replace('\r',''))
        if p.stable_metadata(baseline_meta)!=p.stable_metadata(meta) or p.stable_metadata(meta)!=p.stable_metadata(last_meta) or row.get('metadata')!=meta or row.get('metadata_after_denied')!=last_meta:raise ValueError('RESOURCE_RECOVERY_METADATA_DIFFERENCE')
        helper=helpers[side]['recovery_helper_artifact']
        helper_text=(folder/'retained-helper-current.stdout').read_text().replace('\r','')
        observed=p.file_observation(helper_text,helper['path'],helper['sha256'],'700')
        if observed['uid']!='0' or observed['gid']!='0' or observed['size']!=helper['size'] or row.get('retained_readonly_helper')!=helper:raise ValueError('RESOURCE_RECOVERY_HELPER_IDENTITY')
        node='tcp:'+str(cfg['rmw_debugger']['ports'][side]);base=[cfg['hdc_path'],'-t',cfg['targets'][side]];owned=(cfg['targets'][side],node,node,'Forward')
        fbefore=forward_entries((folder/'forward-current.stdout').read_text());fafter=forward_entries((folder/'forward-final.stdout').read_text())
        if fbefore.count(owned)!=1 or any(node in r[1:3] and r!=owned for r in fbefore) or sorted(fafter)!=sorted(r for r in fbefore if r!=owned) or (folder/'forward-remove.stdout').read_text().strip()!='Remove forward ruler success, ruler:'+node+' '+node:raise ValueError('RESOURCE_RECOVERY_FORWARD_DIFF')
        loader=str(Path(helper['path']).parent)+':'+':'.join(cfg['system_library_dirs']) # Exact as-recorded Windows producer syntax.
        shells={'exact-main-absent':'test ! -d /proc/'+str(json.loads((window/side/'load-pre-release-analysis.json').read_text())['pid']),
            'exact-server-absent':'test ! -d /proc/'+str(json.loads((window/'debugger-recovery-readonly/RESULT.json').read_text())[side]['argv'][-1])}
        # Server identity is captured by its actual tracer read, not inferred
        # from the main PID in --attach.
        tracer_started=json.loads((window/'debugger-recovery-readonly'/side/'tracer-identity.started.json').read_text())
        server_pid=int(re.search(r'/proc/([0-9]+)/stat',tracer_started['argv'][-1])[1]);shells['exact-server-absent']='test ! -d /proc/'+str(server_pid)
        shells.update({'metadata-current':p.metadata_command(),'token-hash':'sha256sum '+p.TOKEN,'retained-helper-current':p.file_command(helper['path']),
            'verify-denied-retained':'env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+loader+' '+helper['path']+' --verify-denied --expected-token '+cfg['token'][side]+' --preflight-confirmed',
            'token-hash-after-denied':'sha256sum '+p.TOKEN,'metadata-after-denied':p.metadata_command(),'processes-current':'ps -A'})
        if side=='A':shells['verify-denied']='env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+cfg['remote'][side]+':'+':'.join(cfg['system_library_dirs'])+' '+cfg['remote'][side]+'/native_permission_probe --verify-denied --expected-token '+cfg['token'][side]+' --preflight-confirmed'
        for i,item in enumerate(cfg['platform'][side]['files']+[cfg['platform'][side]['service_server']]):shells['platform-hash-'+str(i)]='sha256sum '+item['path']
        for i,item in enumerate(cfg['package_artifacts']):shells['package-hash-'+str(i)]='sha256sum '+cfg['remote'][side]+'/'+item['name']
        labels=set(shells)|{'forward-current','forward-remove','forward-final','token-recv','guard-current'}
        for suffix in ('intent.json','started.json','result.json'):
            if {q.name[:-len('.'+suffix)] for q in folder.glob('*.'+suffix)}!=labels:raise ValueError('RESOURCE_RECOVERY_MISSING_OR_UNKNOWN_ACTION')
        for label in labels:
            intent=json.loads(bound(folder/(label+'.intent.json')).read_text());start=json.loads(bound(folder/(label+'.started.json')).read_text());result=json.loads(bound(folder/(label+'.result.json')).read_text());argv=intent['argv']
            if label in shells:expected=base+['shell',shells[label]+'; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc']
            elif label=='token-recv':expected=base+['file','recv',p.TOKEN,str(after)]
            elif label=='forward-remove':expected=forward_remove_argv(base,node)
            elif label in ('forward-current','forward-final'):expected=base+['fport','ls']
            else:
                if Path(argv[0]).resolve()!=(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe').resolve():raise ValueError('RESOURCE_RECOVERY_FOREIGN_POWERSHELL')
                expected=[argv[0],'-NoLogo','-NoProfile','-NonInteractive','-File',str(Path(argv[5])),'-GuardSource',str(repo/'scripts/NativePermissionGuard.ps1'),'-Baseline',str(before),'-Current',str(after),'-Run',cfg['run'],'-Side',side,'-Token',cfg['token'][side],'-Output',str(folder/'snapshot-guard.json')]
                tool=Path(argv[5]).resolve();mat=json.loads((tool.parent.parent/'MATERIALIZATION.json').read_text());match=[q for q in mat['files'] if q['destination']=='recovery/recovery-guard.ps1'];
                authority=Path(frozen['task_authority_root']).resolve();tool_task=tool.parent.parent
                if (tool_task.parent!=authority/'iterations' or not re.fullmatch(r'iteration-[1-9][0-9]*',tool_task.name) or tool!=tool_task/'recovery/recovery-guard.ps1'
                    or len(match)!=1 or match[0].get('source')!='scripts/public_socket/recovery-guard.ps1' or sha(bound(tool))!=match[0]['sha256']):raise ValueError('RESOURCE_RECOVERY_GUARD_TOOL_CHANGED')
            if argv!=expected or start.get('argv')!=expected or result.get('argv')!=expected or intent.get('cwd')!=str(folder) or start.get('cwd')!=str(folder) or intent.get('purpose')!='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS' or start.get('purpose')!=intent['purpose'] or type(start.get('local_pid')) is not int or start['local_pid']!=result.get('local_pid'):raise ValueError('RESOURCE_RECOVERY_COMMAND_IDENTITY:'+side+':'+label)
            if result.get('exit_code')!=0 or result.get('capture_complete') is not True or result.get('timed_out') is not False or result.get('overflow') is not False or result.get('termination') is not None:raise ValueError('RESOURCE_RECOVERY_ACTION_NOT_CLOSED')
            for stream in ('stdout','stderr'):
                path=bound(folder/(label+'.'+stream))
                if sha(path)!=result.get(stream+'_sha256'):raise ValueError('RESOURCE_RECOVERY_STREAM_CHANGED')
            text=(folder/(label+'.stdout')).read_text().replace('\r','')
            if label in shells and re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)$',text,re.M)!=(['64'] if label=='verify-denied' else ['0']):raise ValueError('RESOURCE_RECOVERY_REMOTE_EXIT')
            if label=='verify-denied-retained' and ('verification_permission=-1 writes=NOT_PERFORMED token_switch=NOT_PERFORMED communication=NOT_RUN' not in text or 'result=PASS' not in text):raise ValueError('RESOURCE_RECOVERY_DENIED_NOT_PROVEN')
            if label=='processes-current' and re.search(p.PROCESS_PATTERN,text):raise ValueError('RESOURCE_RECOVERY_PROCESS_PRESENT')
            if label in ('token-hash','token-hash-after-denied'):
                hashes=re.findall(r'^([0-9a-f]{64})\s+'+re.escape(p.TOKEN)+r'$',text,re.M)
                if hashes!=[sha(before)]:raise ValueError('RESOURCE_RECOVERY_TOKEN_HASH_DIFFERENCE')
            if label in ('exact-main-absent','exact-server-absent') and text.strip()!='MDDS_REMOTE_EXIT=0':raise ValueError('RESOURCE_RECOVERY_EXACT_ABSENCE')
            if label.startswith('package-hash-'):
                item=cfg['package_artifacts'][int(label.rsplit('-',1)[1])];path=cfg['remote'][side]+'/'+item['name']
                if not re.search(r'^'+item['sha256']+r'\s+'+re.escape(path)+r'$',text,re.M):raise ValueError('RESOURCE_RECOVERY_PACKAGE_DIFFERENCE')
            if label.startswith('platform-hash-'):
                item=(cfg['platform'][side]['files']+[cfg['platform'][side]['service_server']])[int(label.rsplit('-',1)[1])]
                if not re.search(r'^'+item['sha256']+r'\s+'+re.escape(item['path'])+r'$',text,re.M):raise ValueError('RESOURCE_RECOVERY_PLATFORM_DIFFERENCE')
            count+=1
    bound(root/'HOST_CHILD_CURRENT.json')
    return dict(scope='REAL_DEBUGGER_AND_WINDOW_RESOURCE_RECOVERY',run=cfg['run'],window=str(window),
        original_failure_preserved=True,normal_ros_close='FAIL_NOT_REPAIRED',original_server_capture='UNKNOWN_NOT_BACKFILLED',
        actions_checked=count,evidence_sha256=evidence,current_environment_restored=True)

def seal_resource_recovery(window):
    window=Path(window).resolve();facts=resource_recovery_facts(window);root=window/'remaining-resource-recovery/attempt-1'
    value=dict(facts,verification_reader_path=str(Path(__file__).resolve()),verification_reader_sha256=sha(__file__))
    write(root/'APPENDED_WINDOW_RESOURCE_RECOVERY.json',value);return value

def verify_resource_recovery(window):
    window=Path(window).resolve();root=window/'remaining-resource-recovery/attempt-1';path=root/'APPENDED_WINDOW_RESOURCE_RECOVERY.json';row=json.loads(path.read_text())
    facts=resource_recovery_facts(window)
    if any(row.get(k)!=v for k,v in facts.items()) or row.get('verification_reader_sha256')!=sha(__file__):raise ValueError('RESOURCE_RECOVERY_SEALED_FACTS_CHANGED')
    accepted=json.loads((root/'INDEPENDENT_ACCEPTANCE.json').read_text());review_path=Path(accepted['review_path']);review=json.loads(review_path.read_text())
    if accepted.get('verdict')!='PASS' or accepted.get('run')!=facts['run'] or accepted.get('recovery_sha256')!=sha(path) or accepted.get('review_sha256')!=sha(review_path) or review.get('scope')!='INDEPENDENT_REAL_DEBUGGER_RESOURCE_RECOVERY_ACCEPTANCE' or review.get('verdict')!='PASS' or review.get('run')!=facts['run'] or review.get('window')!=facts['window'] or review.get('recovery_sha256')!=sha(path) or review.get('verification_reader_sha256')!=sha(__file__):raise ValueError('RESOURCE_RECOVERY_INDEPENDENT_ACCEPTANCE_REQUIRED')
    return row

def verify_bound_resource_recovery(window):
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text());path=window/'remaining-resource-recovery/attempt-1/APPENDED_WINDOW_RESOURCE_RECOVERY.json';row=json.loads(path.read_text())
    reader=Path(row['verification_reader_path']).resolve();task=reader.parent.parent;authority=Path(cfg['task_contract']['task_root']).resolve().parent.parent
    if task.parent!=authority/'iterations' or not re.fullmatch(r'iteration-[1-9][0-9]*',task.name) or reader!=task/'executor/rmw_debugger.py' or sha(reader)!=row['verification_reader_sha256']:raise ValueError('RESOURCE_RECOVERY_READER_ORIGIN')
    mat=json.loads((task/'MATERIALIZATION.json').read_text());entries=[r for r in mat['files'] if r['destination']=='executor/rmw_debugger.py' and r['source']=='scripts/public_socket/shared_reattach/executor/rmw_debugger.py']
    if len(entries)!=1 or entries[0]['sha256']!=sha(reader):raise ValueError('RESOURCE_RECOVERY_READER_NOT_MATERIALIZED')
    spec=importlib.util.spec_from_file_location('bound_resource_recovery_reader',reader);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module.verify_resource_recovery(window)

def main(window,side,pid):
    admission_deadline=time.monotonic()+30
    window=Path(window).resolve();cfg=json.loads((window/'window.json').read_text());actual=validate_inputs(window,side,pid,cfg)
    spec=cfg['rmw_debugger'];root=window/side/'debugger';root.mkdir(exist_ok=False)
    remote=cfg['remote'][side];port=spec['ports'][side];node=f'tcp:{port}'
    base=[cfg['hdc_path'],'-t',cfg['targets'][side]]
    owned_forward=False;forward_attempted=False;server=None;client=None;status='INPUT_REJECTED';failure=None;cleanup_ok=True;server_starttime=None
    try:
        current_process(base,root,'pid-before-attach',pid,cfg,side,actual,startup_seconds(admission_deadline))
        before=Command(root,'forward-before',base+['fport','ls'],startup_seconds(admission_deadline))
        if any(node in row[1:3] for row in forward_entries(command_text(before))):raise RuntimeError('DEBUGGER_FORWARD_ALREADY_OWNED')
        status='FORWARD_ADMISSION_FAILED'
        forward_budget=startup_seconds(admission_deadline)
        forward_attempted=True;forward=Command(root,'forward',base+['fport',node,node],forward_budget)
        if command_text(forward).strip()!='Forwardport result:OK':raise RuntimeError('DEBUGGER_FORWARD_NOT_CREATED')
        owned_forward=True
        check=Command(root,'forward-after',base+['fport','ls'],startup_seconds(admission_deadline))
        rows=forward_entries(command_text(check));owned=(cfg['targets'][side],node,node,'Forward')
        if rows.count(owned)!=1 or any(node in r[1:3] and r!=owned for r in rows):raise RuntimeError('DEBUGGER_FORWARD_NOT_CONFIRMED')
        startup_seconds(admission_deadline)
        status='UNAVAILABLE'
        server=Command(root,'server',base+['shell',start_command(remote,port,pid)],cfg['timeout_seconds']+15)
        end=min(time.monotonic()+12,admission_deadline)
        while not server.ready.is_set() and not server.finished.is_set() and time.monotonic()<end:time.sleep(.05)
        if not server.ready.is_set():
            observed_owned_tracer(base,root,pid,cfg,side,actual,admission_deadline)
            # This proves only the owned OS attach. The real GDB connection,
            # exact PID and instruction breakpoints still gate READY/business.
            server.ready.set()
        proof=confirm_visible_server_pid(root,server)
        if proof is not None:server_starttime=proof['server_starttime']
        startup_seconds(admission_deadline)
        trace=root/'trace';client_config=root/'trace-config.json'
        write(client_config,{'run':cfg['run'],'side':side,'pid':pid,'output':str(trace),'host_port':port,
              'package':cfg['package'],'remote':remote,'main_local':str(Path(cfg['package'])/cfg['main_member']),
              'rmw_local':str(Path(cfg['package'])/'librmw_fastrtps_shared_cpp.so'),'hit_limit':spec['hit_limit'],'seconds':spec['seconds']})
        script=root/'commands.lldb';script.write_text('command script import '+json.dumps(spec['script']['path'])+'\nscript rmw_lldb_trace.run_config('+repr(str(client_config))+')\nquit\n',encoding='utf-8')
        env=os.environ.copy();env['PYTHONPATH']=str(Path(spec['client']['path']).parent.parent/'script')
        client=Command(root,'client',[spec['client']['path'],'--batch','--source',str(script)],cfg['timeout_seconds']-30,env)
        end=min(time.monotonic()+15,admission_deadline)
        while not (trace/'READY.json').is_file() and not client.finished.is_set() and time.monotonic()<end:time.sleep(.05)
        if not (trace/'READY.json').is_file():raise RuntimeError('ORIGINAL_RMW_BREAKPOINTS_NOT_CONFIRMED')
        ready=json.loads((trace/'READY.json').read_text())
        if ready['run']!=cfg['run'] or ready['side']!=side or ready['pid']!=pid:raise RuntimeError('DEBUGGER_READY_IDENTITY')
        if ready.get('execution_state')!='RUNNING_EVENT_CONSUMED':raise RuntimeError('DEBUGGER_RUNNING_NOT_CONFIRMED')
        startup_seconds(admission_deadline);confirm_visible_server_pid(root,server)
        status='READY';write(root/'READY.json',ready)
        client.finished.wait(cfg['timeout_seconds']-20)
        if not client.finished.is_set() or client.result.get('exit_code')!=0 or client.result.get('timed_out') or not client.result.get('capture_complete') or client.result.get('overflow') or (trace/'FAILED.json').exists() or not (trace/'END.json').is_file():
            raise RuntimeError('ORIGINAL_RMW_OBSERVATION_INCOMPLETE')
    except Exception as error:
        failure=str(error)
        # Explicit unavailable outcome is not a null cast or native PASS. The
        # same ROS window may proceed to its independent residual observation.
    finally:
        # Confirm a stopped/attached inferior can resume before terminating the
        # debugger support process. This never detaches a native worker thread.
        if server is not None and server.ready.is_set() and (client is None or client.finished.is_set()) and not server.finished.is_set():
            detach_config=root/'detach-config.json';write(detach_config,{'run':cfg['run'],'side':side,'pid':pid,'host_port':port,
                'main_local':str(Path(cfg['package'])/cfg['main_member']),'detach_result':str(root/'DETACHED.json')})
            detach_script=root/'detach.lldb';detach_script.write_text('command script import '+json.dumps(spec['script']['path'])+'\nscript rmw_lldb_trace.detach_config('+repr(str(detach_config))+')\nquit\n')
            env=os.environ.copy();env['PYTHONPATH']=str(Path(spec['client']['path']).parent.parent/'script')
            detached=Command(root,'detach-client',[spec['client']['path'],'--batch','--source',str(detach_script)],10,env);detached.finished.wait(15)
        safe=server is None
        if server is not None:
            observed=Command(root,'inferior-status',base+['shell',status_command(pid)],8)
            try:
                status_text=remote_result(observed)
                if re.search(r'^TracerPid:\s+0\s*$',status_text,re.M) and re.search(r'^State:\s+T\b',status_text,re.M):
                    identity=Command(root,'pid-before-os-resume',base+['shell',process_command(pid)],8);identity_text=remote_result(identity)
                    process_identity(identity_text,pid,remote+'/'+cfg['main_member'],actual['starttime']);values=process_fields(identity_text,pid)
                    resumed=Command(root,'owned-os-resume',base+['shell',resume_command(pid,values['exe'],values['starttime'],values['argv'])],8);remote_result(resumed)
                    checked=Command(root,'inferior-after-os-resume',base+['shell',status_command(pid)],8);status_text=remote_result(checked)
                safe=bool(re.search(r'^MDDS_DEBUG_MAIN_ABSENT=1\r?$',status_text,re.M) or (re.search(r'^TracerPid:\s+0\s*$',status_text,re.M) and re.search(r'^State:\s+[RSIDZ]\b',status_text,re.M)))
                if safe and not re.search(r'^MDDS_DEBUG_MAIN_ABSENT=1\r?$',status_text,re.M):current_process(base,root,'pid-after-detach',pid,cfg,side,actual)
            except Exception:safe=False
        if not safe:
            write(root/'RESULT.json',{'run':cfg['run'],'side':side,'pid':pid,'status':'DEBUGGER_RECOVERY_BLOCKED','failure':failure,'support_closed':False,'inferior_resumed_or_absent':False})
            return 1
        if server is not None:
            ids=re.findall(r'^MDDS_DEBUG_SERVER_PID=([1-9][0-9]*)\r?$',server.buffers['stdout'].decode('utf-8',errors='replace'),re.M)
            proof_file=root/'OWNED_TRACER.json'
            if proof_file.is_file():
                proof=json.loads(proof_file.read_text())
                if proof.get('run')!=cfg['run'] or proof.get('side')!=side or proof.get('pid')!=pid or (ids and ids!=[str(proof['server_pid'])]):
                    ids=[];cleanup_ok=False;failure=(failure or '')+';SERVER_IDENTITY_PROOFS_CONTRADICT'
                else:ids=[str(proof['server_pid'])]
            if len(ids)!=1:cleanup_ok=False;failure=(failure or '')+';SERVER_PID_NOT_CONFIRMED'
            else:
                server_pid=int(ids[0])
                try:
                    identity=Command(root,'server-identity',base+['shell',process_command(server_pid)],8);identity_text=remote_result(identity)
                    if not re.search(r'^MDDS_DEBUG_PROCESS_ABSENT=1\r?$',identity_text,re.M):
                        observed_server=process_fields(identity_text,server_pid)
                        expected_argv=[remote+'/lldb-server','gdbserver','127.0.0.1:'+str(port),'--attach',str(pid)]
                        if observed_server['exe']!=remote+'/lldb-server' or observed_server['argv']!=expected_argv:raise RuntimeError('DEBUGGER_SERVER_IDENTITY_MISMATCH')
                        if server_starttime is not None and observed_server['starttime']!=server_starttime:raise RuntimeError('DEBUGGER_SERVER_BIRTH_CHANGED')
                        server_starttime=observed_server['starttime']
                    ended=Command(root,'server-cleanup',base+['shell',cleanup_command(remote,port,pid,server_pid,server_starttime)],8)
                    remote_result(ended);server.finished.wait(8);server_exit(server)
                    absent=Command(root,'server-absent',base+['shell',absent_command(server_pid)],8)
                    remote_result(absent)
                except Exception as error:cleanup_ok=False;failure=(failure or '')+';CLEANUP:'+str(error)
        if owned_forward:
            try:
                before_remove=Command(root,'forward-remove-before',base+['fport','ls'],8)
                rows=forward_entries(command_text(before_remove));owned=(cfg['targets'][side],node,node,'Forward')
                if rows.count(owned)!=1 or any(node in r[1:3] and r!=owned for r in rows):raise RuntimeError('DEBUGGER_FORWARD_REMOVE_OWNERSHIP_UNCONFIRMED')
                removed=Command(root,'forward-remove',forward_remove_argv(base,node),8)
                removal=command_text(removed)
                if '[Fail]' in removal or 'E001005' in removal:raise RuntimeError('DEBUGGER_FORWARD_REMOVE_FAILED')
                after=Command(root,'forward-removed-check',base+['fport','ls'],8)
                if any(node in row[1:3] for row in forward_entries(command_text(after))):raise RuntimeError('DEBUGGER_FORWARD_STILL_PRESENT')
            except Exception as error:cleanup_ok=False;failure=(failure or '')+';FORWARD:'+str(error)
        elif forward_attempted:
            # Ambiguous creation must not delete someone else's task. It blocks.
            cleanup_ok=False
        unfinished=any(not (root/(p.name[:-len('.intent.json')]+'.started.json')).is_file() or not (root/(p.name[:-len('.intent.json')]+'.result.json')).is_file() for p in root.glob('*.intent.json'))
        for path in root.glob('*.result.json'):
            result=json.loads(path.read_text())
            if type(result.get('exit_code')) is not int or result.get('timed_out') is not False or result.get('capture_complete') is not True or result.get('overflow') is not False or result.get('termination'):
                unfinished=True
        complete=bool(safe) and cleanup_ok and not unfinished and (server is None or server.finished.is_set()) and (client is None or client.finished.is_set() and client.result.get('capture_complete') is True and not client.result.get('overflow') and not client.result.get('timed_out'))
        observation_failed=status=='READY' and failure is not None
        write(root/'RESULT.json',{'run':cfg['run'],'side':side,'pid':pid,'status':'OBSERVATION_INCOMPLETE' if observation_failed else status,'failure':failure,'support_closed':complete,
              'server':getattr(server,'result',None),'client':getattr(client,'result',None),'scope':'ORIGINAL_RMW_DEBUGGER_NOT_FUNCTIONAL_RESULT',
              'manager_sha256':sha(__file__),'forward_owned':owned_forward,'server_started':server is not None,'server_starttime':server_starttime,
              'load_origin_sha256':sha(window/side/'load-pre-release-analysis.json'),
              'load_origin_phase':'PRE_RELEASE',
              'load_summary_sha256':sha(window/side/'load-pre-release-summary.json'),
              'receipts':{str(p.relative_to(root)):sha(p) for p in root.iterdir() if p.is_file()}})
        if status=='UNAVAILABLE' and complete:write(root/'UNAVAILABLE.json',{'run':cfg['run'],'side':side,'pid':pid,'reason':failure,'support_closed':True,'inferior_resumed_or_absent':True})
        if not complete or observation_failed or status not in ('READY','UNAVAILABLE'):return 1
    return 0

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--window',required=True);p.add_argument('--side',choices=['A','B'],required=True);p.add_argument('--pid',type=int);group=p.add_mutually_exclusive_group();group.add_argument('--close-readonly-failure',action='store_true');group.add_argument('--recover-forward-only',action='store_true');a=p.parse_args()
    if a.recover_forward_only:
        if a.pid is not None:p.error('Recovery accepts no business PID argument')
        recover_forward_only(a.window,a.side)
    elif a.close_readonly_failure:
        if a.pid is not None:p.error('Recovery accepts no business PID argument')
        append_readonly_failure_closure(a.window,a.side)
    else:
        if a.pid is None:p.error('--pid required for support dispatch')
        raise SystemExit(main(a.window,a.side,a.pid))
