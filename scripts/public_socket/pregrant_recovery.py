"""Bounded recovery verification for an owned ROS pregrant setup failure.

Never calls a business launcher, grants/revokes permission, deletes files, or
queries the directory. Historical execution files are inputs, never outputs.
"""
from pathlib import Path
import argparse, datetime, hashlib, importlib.util, json, os, re, subprocess, sys, time, uuid

TOKEN='/data/service/el0/access_token/nativetoken.json'
AUTH='TASK_SCOPED_ROS2_DEVICE_VALIDATION'
GRANT_SHA='1dbadad0591bad3f76ef98a6bab09ff80ee6b75491e54c447410c841b39bea51'
PROCESS_PATTERN=r'fastdds_socket|public_socket|mdds_installed|ordinary_ros2|fastdds_dsoftb|native_permiss|softbus_recor'
WRAPPER='; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'

def need(value,reason):
    if not value: raise ValueError(reason)

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def text(path):
    data=Path(path).read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig')
def put(path,value):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('x',encoding='utf-8') as stream: json.dump(value,stream,indent=2);stream.write('\n')
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()

def process_identity(pid):
    """Exact local process instance, never a name-based process operation."""
    if os.name!='nt':
        p=Path('/proc')/str(pid)/'stat'
        if not p.exists():return None
        return dict(pid=pid,birth=p.read_text().rsplit(')',1)[1].split()[19])
    import ctypes
    from ctypes import wintypes
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=wintypes.HANDLE
    kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)]
    kernel.GetProcessTimes.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
    kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    handle=kernel.OpenProcess(0x1000,False,pid)
    if not handle:
        need(ctypes.get_last_error()==87,'LOCAL_PROCESS_IDENTITY_UNOBSERVABLE');return None
    try:
        code=wintypes.DWORD();need(kernel.GetExitCodeProcess(handle,ctypes.byref(code)),'LOCAL_PROCESS_EXIT_UNOBSERVABLE')
        if code.value!=259:return None
        created=wintypes.FILETIME();exited=wintypes.FILETIME();system=wintypes.FILETIME();user=wintypes.FILETIME()
        need(kernel.GetProcessTimes(handle,ctypes.byref(created),ctypes.byref(exited),ctypes.byref(system),ctypes.byref(user)),'LOCAL_PROCESS_BIRTH_UNOBSERVABLE')
        return dict(pid=pid,birth=str((created.dwHighDateTime<<32)|created.dwLowDateTime))
    finally:kernel.CloseHandle(handle)

def process_still_live(identity):
    need(isinstance(identity,dict) and type(identity.get('pid')) is int and identity['pid']>0 and isinstance(identity.get('birth'),str) and re.fullmatch('[0-9]+',identity['birth']),'MISSING_LOCAL_PROCESS_IDENTITY')
    return process_identity(identity['pid'])==identity

def collect_child(child,timeout):
    try:
        out,err=child.communicate(timeout=timeout);return out,err,False,True
    except subprocess.TimeoutExpired:
        child.kill() # Only this owned local child; not proof of remote termination.
        try:
            out,err=child.communicate(timeout=2);return out,err,True,True
        except subprocess.TimeoutExpired as error:
            return error.stdout or b'',error.stderr or b'',True,False

def prior_recovery_closed(base):
    for attempt in base.glob('attempt-*'):
        start=read(attempt/'STARTED.json')
        nonce=start.get('owner_nonce');released=base/('LOCK_RELEASED-'+str(nonce)+'.json')
        need(isinstance(nonce,str) and re.fullmatch('[0-9a-f]{32}',nonce),'PRIOR_RECOVERY_OWNER_NONCE')
        if released.is_file():
            value=read(released);need(value.get('owner')==start.get('owner') and value.get('nonce')==nonce and value.get('execution_finished') is True,'PRIOR_RECOVERY_RELEASE_MISMATCH')
        else:need(not process_still_live(start.get('owner')),'PRIOR_RECOVERY_STILL_RUNNING')
        for command in attempt.glob('**/command.json'):
            spawn=command.with_name('spawn.json');need(spawn.exists(),'PRIOR_COMMAND_SPAWN_UNOBSERVED')
            result=command.with_name('result.json')
            if result.exists():need(read(result).get('capture_complete') is True and type(read(result).get('exit_code')) is int,'PRIOR_RECOVERY_PIPE_DRAIN_UNCONFIRMED')
            else:
                identity=read(spawn).get('identity')
                if identity is not None:need(not process_still_live(identity),'PRIOR_RECOVERY_CHILD_STILL_RUNNING')
                need(False,'PRIOR_RECOVERY_COMMAND_RESULT_MISSING')
        capture=base/('current-'+attempt.name.split('-')[1])
        if capture.exists():
            for command in capture.glob('*.command.json'):
                spawn=command.with_name(command.name.replace('.command.json','.spawn.json'));need(spawn.exists(),'PRIOR_CAPTURE_CHILD_UNOBSERVED')
                result=command.with_name(command.name.replace('.command.json','.result.json'))
                if result.exists():need(read(result).get('capture_complete') is True and type(read(result).get('exit_code')) is int,'PRIOR_CAPTURE_PIPE_DRAIN_UNCONFIRMED')
                else:
                    identity=read(spawn).get('identity')
                    if identity is not None:need(not process_still_live(identity),'PRIOR_CAPTURE_CHILD_STILL_RUNNING')
                    need(False,'PRIOR_CAPTURE_RESULT_MISSING')

def remote_text(stdout,local_exit):
    need(type(local_exit) is int and local_exit==0,'LOCAL_DRIVER_FAILED')
    stdout=stdout.replace('\r\n','\n')
    codes=re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\s*$',stdout,re.M)
    need(len(codes)==1,'REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE')
    need(codes==['0'],'REMOTE_COMMAND_FAILED')
    return re.sub(r'^MDDS_REMOTE_EXIT=0\s*$','',stdout,flags=re.M).strip()

def metadata(value):
    stat=re.search(r'^META=([^|\r\n]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)\s*$',value,re.M)
    label=re.search(r'u:object_r:[a-zA-Z0-9_]+:s0',value)
    enforcement=re.search(r'^(Enforcing|Permissive|Disabled)\s*$',value,re.M)
    need(stat and label and enforcement,'METADATA_INCOMPLETE')
    result=dict(zip(('kind','mode','uid','gid','size','inode','mtime','ctime'),stat.groups()))
    result.update(label=label[0],enforcement=enforcement[1]);return result

def stable_metadata(value):return {k:value[k] for k in ('kind','mode','uid','gid','label','enforcement')}
def metadata_command():return f"test -f {TOKEN} && test ! -L {TOKEN} && stat -c 'META=%F|%a|%u|%g|%s|%i|%Y|%Z' {TOKEN} && ls -lZ {TOKEN} && getenforce"
def file_command(path):return f"test -f {path} && test ! -L {path} && sha256sum {path} && stat -c 'FILE=%F|%a|%u|%g|%s' {path}"
def file_observation(output,path,expected_sha,expected_mode=None):
    need(re.search(r'^'+expected_sha+r'\s+'+re.escape(path)+r'$',output,re.M),'OWNED_FILE_HASH_MISMATCH')
    stat=re.search(r'^FILE=regular file\|([0-9]+)\|([0-9]+)\|([0-9]+)\|([0-9]+)$',output,re.M)
    need(stat and stat[2]=='0' and (expected_mode is None or stat[1]==expected_mode),'OWNED_FILE_MODE_OR_OWNER_MISMATCH')
    return dict(mode=stat[1],uid=stat[2],gid=stat[3],size=int(stat[4]))

def original(window):
    window=Path(window).resolve();cfg=read(window/'window.json');proof=cfg['task_contract']
    root=Path(proof['task_root']).resolve();freeze=Path(proof['freeze_path']).resolve()
    need(window.parent==root/'windows' and Path(cfg['window_root']).resolve()==window,'WRONG_WINDOW')
    need(re.fullmatch('[0-9a-f]{16}',cfg.get('run','')) and cfg.get('scenario')=='ROS2_DSOFTBUS','WRONG_RUN_OR_SCENARIO')
    need(freeze.parent==root and sha(freeze)==proof['freeze_sha256'],'ORIGINAL_CONTRACT_DRIFT')
    frozen=read(freeze);private=read(root/'PRIVATE_CONFIG.json')
    need(sha(root/'PRIVATE_CONFIG.json')==cfg['private_config_sha256']==frozen['private_config_sha256'],'PRIVATE_IDENTITY_DRIFT')
    need(cfg['targets']==private['targets'] and cfg['token']==private['tokens'] and cfg['hdc_path']==private['hdc_path'],'WRONG_DEVICE_IDENTITY')
    need(cfg['remote']=={s:f"/data/local/tmp/mdds_dds_{cfg['run']}_{s}" for s in 'AB'},'WRONG_DEPLOYMENT_DIRECTORY')
    need(sha(root/'MATERIALIZATION.json')==frozen['materialization_sha256'],'ORIGINAL_MATERIALIZATION_DRIFT')
    mat=read(root/'MATERIALIZATION.json');repo=Path(cfg['repo']).resolve()
    need(Path(private['repo_root']).resolve()==repo and cfg['package_artifacts']==frozen['build_binding']['package_artifacts'],'ORIGINAL_PACKAGE_BINDING')
    need(cfg.get('main_member')=='ordinary_ros2_transport_consumer' and cfg['system_library_dirs']==['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'],'ORIGINAL_MAIN_OR_LOADER_SCOPE')
    for row in mat['files']:need(sha(root/row['destination'])==row['sha256'],'ORIGINAL_EXECUTOR_DRIFT')
    for side in 'AB':
        expected_aux={'capture-load.sh','native_permission_probe','required-load-paths.txt','ros.conf'}
        if cfg.get('rmw_debugger') is not None:
            need(cfg['rmw_debugger']==frozen.get('rmw_debugger'),'ORIGINAL_DEBUGGER_BINDING')
            expected_aux.add('lldb-server')
            need(cfg['auxiliary_sha256'][side].get('lldb-server')==cfg['rmw_debugger']['server']['sha256'] and
                 sha(window/side/'aux/lldb-server')==cfg['rmw_debugger']['server']['sha256'],'ORIGINAL_DEBUGGER_AUXILIARY_HASH')
        need(set(cfg['auxiliary_sha256'][side])==expected_aux,'ORIGINAL_AUXILIARY_MEMBERSHIP')
    need(all(re.fullmatch('[A-Za-z0-9_.+-]+',e['name']) for e in cfg['package_artifacts']),'UNSAFE_PACKAGE_NAME')
    local=frozen.get('local_contract_test') is True
    if local:
        need(cfg.get('simulated') is True and cfg['targets']=={'A':'SIM-A','B':'SIM-B'} and Path(cfg['hdc_path']).name=='fake-hdc.exe','LOCAL_IDENTITY_BOUNDARY')
        need(frozen.get('device_authorized') is not True and frozen.get('execution_admitted') is not True,'LOCAL_CANNOT_GRANT_DEVICE_AUTHORITY')
        authority=root
    else:
        need(frozen.get('authorization')==AUTH and frozen.get('device_authorized') is True and 'simulated' not in cfg,'NOT_TASK_DEVICE_AUTHORITY')
        authority=Path(frozen['task_authority_root']).resolve();grant=authority/'AUTHORIZATION.json'
        need(sha(grant)==GRANT_SHA and frozen['authorization_record']==dict(path=str(grant),sha256=GRANT_SHA),'TASK_GRANT_DRIFT')
        permission=read(grant)
        need(Path(permission['task_root']).resolve()==authority and permission['task_id']==frozen['task_id'] and permission['targets']==cfg['targets'] and root==authority/'iterations'/f"iteration-{frozen['execution_iteration_id']}",'TASK_SCOPE_MISMATCH')
        predecessor=read(Path(permission['predecessor_restore']['window'])/'window.json')
        need(cfg['hdc_path']==predecessor['hdc_path'] and Path(cfg['hdc_path']).name.lower()=='hdc.exe' and not Path(cfg['hdc_path']).resolve().is_relative_to(authority),'DEVICE_DRIVER_SCOPE')
        need(sha(cfg['hdc_path'])==frozen['device_driver']['sha256'],'DEVICE_DRIVER_DRIFT')
    summary=read(window/'window-summary.json')
    need(summary.get('failure_stage')=='PREFLIGHT_OR_DEPLOY' and bool(summary.get('first_failure')) and summary.get('attempted_grants')==[], 'NOT_PREGRANT_FAILURE')
    need(summary.get('process_outcome')=='NO_BUSINESS_PROCESS_STARTED' and summary.get('cleanup_failures')==[],'BUSINESS_OR_UNKNOWN_CLEANUP')
    need((window/'EXECUTION_STARTED').is_file() and bool((window/'EXECUTION_STARTED').read_bytes()),'MISSING_EXECUTION_START')
    for name in ('DATA_ATTEMPT_STARTED','runner.stdout.log','runner.stderr.log','result.json','data-dispatch-A.json','data-dispatch-B.json'):
        need(not (window/name).exists(),'BUSINESS_DISPATCH_PRESENT')
    for claim in (authority/'ros2/task-ledger').glob('data-*.json'):
        row=read(claim);need(row.get('run')!=cfg['run'] and row.get('window')!=str(window),'BUSINESS_CLAIM_PRESENT')
    return cfg,frozen,private,authority,local

def transfer_classification(step,source,local=False):
    # Saved HDC evidence, not an inference from the current missing file.
    need(int(text(step/'exit-code.txt').strip())==0,'ORIGINAL_TRANSFER_DRIVER_FAILED')
    output=text(step/'stdout.log').replace('\r\n','\n').strip()
    rejected='[Fail]Error opening file: no such file or directory, path:'+str(source)
    lines=output.splitlines()
    detail=r'\[F\]\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\] open path:'+re.escape(str(source))+r', localPath:'+re.escape(str(source))+r', error:no such file or directory, dir:0, master:1'
    if lines and lines[0]==rejected and (len(lines)==1 or len(lines)==2 and re.fullmatch(detail,lines[1])):return 'HOST_FILE_OPEN_FAILED'
    if re.fullmatch(r'FileTransfer finish, Size:[0-9]+, File count = 1, time:[0-9]+ms rate:[0-9.]+kB/s',output):return 'TRANSFER_REPORTED_SUCCESS'
    # Existing explicit local driver has its own finite transfer result.
    if local and output=='':return 'LOCAL_FAKE_TRANSFER_REPORTED_SUCCESS'
    need(False,'ORIGINAL_TRANSFER_RESULT_UNKNOWN')

def reconcile_owned(expected,listing,observe,absent):
    attempted={e['name'] for e in expected}
    required={e['name'] for e in expected if e.get('transfer_class')!='HOST_FILE_OPEN_FAILED'}
    need(len(listing)==len(set(listing)) and required<=set(listing)<=attempted,'UNEXPLAINED_DEPLOYMENT_MEMBER')
    result=[]
    for i,item in enumerate(expected):
        if item['name'] in listing:
            result.append(dict(item,**observe(i,item),disposition='RETAINED_OWNED_ARTIFACT'))
        else:
            need(item.get('transfer_class')=='HOST_FILE_OPEN_FAILED','MISSING_CONFIRMED_DEPLOYMENT')
            absent(i,item)
            result.append(dict(item,disposition='ATTEMPTED_ABSENT_CONFIRMED',actual_file_sha256=None,
                               actual_mode=None,transfer='HOST_SOURCE_OPEN_FAILED_NOT_DEPLOYMENT_SUCCESS'))
    return result

def inspect_window(window):
    window=Path(window).resolve();cfg,frozen,private,authority,local=original(window)
    inputs={str(p):sha(p) for p in (window/'window.json',window/'window-summary.json',window/'EXECUTION_STARTED',Path(cfg['task_contract']['freeze_path']))}
    pre=Path(cfg['preflight_config']);need(sha(pre)==cfg['preflight_config_sha256'],'PREFLIGHT_DRIFT');inputs[str(pre)]=sha(pre)
    before=read(pre);need(before['permission_baseline_sha256']==cfg['permission_baseline_sha256'],'PREFLIGHT_BASELINE_MISMATCH')
    need(cfg['platform']==before['platform'],'ORIGINAL_PLATFORM_DRIFT')
    sides={}
    for side in 'AB':
        remote=cfg['remote'][side];folder=window/side;owned={};created=False;commands=[]
        package={e['name']:e['sha256'] for e in cfg['package_artifacts']};aux=cfg['auxiliary_sha256'][side]
        allowed={**package,**aux};known_modes={}
        platform={x['path'] for x in cfg['platform'][side]['files']}|{cfg['platform'][side]['service_server']['path']}
        hashes={f'sha256sum {p}' for p in platform}|{f'sha256sum {remote}/{name}' for name in allowed}
        params={('uname -a' if x['name']=='kernel' else 'param get '+x['name']) for x in cfg['platform'][side]['parameters']}
        meta=f"test -f {TOKEN} && test ! -L {TOKEN} && stat -c 'META=%F|%a|%u|%g|%s|%i|%Y|%Z' {TOKEN} && ls -lZ {TOKEN} && getenforce"
        for command_file in sorted(folder.glob('*/command.json')):
            step=command_file.parent;argv=read(command_file)
            need(isinstance(argv,list) and argv[:3]==[cfg['hdc_path'],'-t',cfg['targets'][side]],'COMMAND_TARGET_OR_DRIVER_MISMATCH')
            kind=None
            if len(argv)==7 and argv[3:5]==['file','send']:
                name=Path(argv[5]).name;need(name in allowed and argv[6]==remote+'/'+name,'UNOWNED_SEND')
                expected=(window/side/'aux'/name) if name in aux else Path(cfg['package'])/name
                need(Path(argv[5]).resolve()==expected.resolve() and sha(expected)==allowed[name],'DEPLOYMENT_SOURCE_DRIFT')
                owned[name]=dict(sha256=allowed[name],transfer_class=transfer_classification(step,expected,local));kind='OWNED_FILE_SEND'
            elif len(argv)==7 and argv[3:5]==['file','recv']:
                need(argv[5]==TOKEN and Path(argv[6]).resolve()==folder/'baseline.json','UNKNOWN_RECEIVE')
                kind='READ_ONLY_BASELINE_CAPTURE'
            elif len(argv)==5 and argv[3]=='shell' and argv[4].endswith(WRAPPER):
                core=argv[4][:-len(WRAPPER)]
                if core in hashes|params|{'ps -A',meta}:kind='READ_ONLY'
                elif core in ("env -i PATH=/system/bin:/system/xbin /system/bin/sh -c 'for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v \\\"$mdds_tool\\\" || exit 3; done'", "env -i PATH=/system/bin:/system/xbin /system/bin/sh -c 'for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v \"$mdds_tool\" || exit 3; done'"):
                    kind='READ_ONLY'
                elif core=='test ! -e '+remote+' && mkdir '+remote:
                    remote_text(text(step/'stdout.log'),int(text(step/'exit-code.txt').strip()));created=True;kind='OWNED_DIRECTORY_CREATE'
                elif core in ('chmod 700 '+remote+'/native_permission_probe','chmod 700 '+remote+'/'+cfg['main_member']+' && test -x '+remote+'/'+cfg['main_member']) or (cfg.get('rmw_debugger') is not None and core=='chmod 700 '+remote+'/lldb-server && test -x '+remote+'/lldb-server'):
                    known_modes[core.split()[2].rsplit('/',1)[-1]]='700';kind='OWNED_FILE_MODE'
            need(kind is not None,'UNKNOWN_OR_FORBIDDEN_ORIGINAL_COMMAND')
            for p in step.iterdir():
                if p.is_file():inputs[str(p)]=sha(p)
            commands.append(dict(step=step.name,kind=kind,command_sha256=sha(command_file)))
        need(not owned or created,'DEPLOYMENT_EXCLUSIVE_OWNERSHIP_UNPROVEN')
        baseline=folder/'baseline.json'
        if baseline.exists():
            origin='DATA_WINDOW_BEFORE';metadata_file=folder/'metadata-before/stdout.log'
        else:
            need(not created and not owned,'MISSING_BASELINE_AFTER_MUTATION')
            baseline=pre.parent/(side+'-nativetoken.json');metadata_file=pre.parent/(side+'-metadata.stdout');origin='PREFLIGHT_BASELINE'
        need(baseline.is_file() and metadata_file.is_file() and sha(baseline)==cfg['permission_baseline_sha256'][side],'MISSING_OR_WRONG_ORIGINAL_BASELINE')
        baseline_metadata=metadata(text(metadata_file));inputs[str(baseline)]=sha(baseline);inputs[str(metadata_file)]=sha(metadata_file)
        sides[side]=dict(baseline=dict(kind=origin,path=str(baseline),sha256=sha(baseline),metadata_path=str(metadata_file),metadata_sha256=sha(metadata_file),metadata=baseline_metadata,
            original_collection_utc='UNKNOWN_NOT_RECORDED',ordering='BOUND_BY_CURRENT_TASK_PREFLIGHT_BEFORE_DATA_PREPARATION' if origin=='PREFLIGHT_BASELINE' else 'ACTUAL_DATA_WINDOW_PREGRANT_CAPTURE'),
            created=created,retained_owned_artifacts=[dict(name=n,**v,expected_mode=known_modes.get(n)) for n,v in sorted(owned.items())],original_commands=commands)
    return dict(run=cfg['run'],window=str(window),task_authority_root=str(authority),local_contract_test=local,targets=cfg['targets'],sides=sides,original_inputs_sha256=inputs,
        first_failure=read(window/'window-summary.json')['first_failure'],permission_actions='NOT_PERFORMED',business_process='NOT_STARTED')

def prepare(window,tools_root,output,helper):
    window=Path(window).resolve();tools_root=Path(tools_root).resolve();output=Path(output).resolve();helper=Path(helper).resolve()
    facts=inspect_window(window);cfg,frozen,private,authority,local=original(window)
    repo=Path(cfg['repo']).resolve();mat=tools_root/'MATERIALIZATION.json';materialization=read(mat)
    names=('pregrant_recovery.py','recovery-guard.ps1','readonly_preflight.py')
    tools={str(tools_root/'recovery'/n):sha(tools_root/'recovery'/n) for n in names}
    for name in names:
        row=next(e for e in materialization['files'] if e['destination']=='recovery/'+name)
        need(row['sha256']==sha(tools_root/'recovery'/name),'RECOVERY_MATERIALIZATION_DRIFT')
    guard=repo/'scripts/NativePermissionGuard.ps1';tools[str(guard)]=sha(guard)
    ro=output.with_name(output.stem+'-readonly.json')
    readonly=dict(preflight_output_parent=str(window/'recovery'),targets=cfg['targets'],tokens=cfg['token'],device_driver=dict(path=cfg['hdc_path'],sha256=sha(cfg['hdc_path'])),
        platform_reference=cfg['preflight_config'],platform_reference_sha256=cfg['preflight_config_sha256'],permission_helper=cfg['permission_helper'],
        source_files=[dict(path=p,sha256=h) for p,h in tools.items()],preflight_tool_sha256=tools[str(tools_root/'recovery/readonly_preflight.py')],local_contract_test=local)
    put(ro,readonly)
    selection=dict(schema=1,scope='OWNED_PREGRANT_RECOVERY_ONLY',facts=facts,tools_root=str(tools_root),materialization_sha256=sha(mat),tools_sha256=tools,
        readonly_selection=dict(path=str(ro),sha256=sha(ro)),permission_helper=dict(path=str(helper),sha256=sha(helper)),
        permission_mode='VERIFY_DENIED_NO_WRITE_SWITCH_RELOAD_SOCKET',scratch_template='/data/local/tmp/mdds_recovery_{run}_{side}_{attempt}',
        permission_mutations_allowed=False,business_allowed=False,directory_query_allowed=False,deadline_seconds=300,max_output_bytes=25165824,per_command_seconds=20)
    put(output,selection);return selection

def validate_original_facts(selection):
    current=inspect_window(selection['facts']['window'])
    if current==selection['facts']:return
    # The original four-auxiliary verifier never emitted transfer_class. Only
    # that exact saved source version may consume this read-only projection.
    original_tool=str(Path(selection['tools_root'])/'recovery/pregrant_recovery.py')
    legacy_sha='6e9fcdd0f89527ee69c05fed6604c6ba38c41125d21c172df4a6f3ddd7110bc8'
    need(selection['tools_sha256'].get(original_tool)==legacy_sha and sha(original_tool)==legacy_sha,'ORIGINAL_FAILURE_EVIDENCE_DRIFT')
    cfg=read(Path(current['window'])/'window.json')
    need(cfg.get('rmw_debugger') is None and all(set(cfg['auxiliary_sha256'][s])=={'capture-load.sh','native_permission_probe','required-load-paths.txt','ros.conf'} for s in 'AB'),'LEGACY_RECOVERY_SCOPE')
    projected=json.loads(json.dumps(current))
    for side in 'AB':
        for item in projected['sides'][side]['retained_owned_artifacts']:
            need(item.get('transfer_class') in ('TRANSFER_REPORTED_SUCCESS','LOCAL_FAKE_TRANSFER_REPORTED_SUCCESS'),'LEGACY_RECOVERY_PARTIAL_TRANSFER_NOT_SUPPORTED')
            item.pop('transfer_class')
    need(projected==selection['facts'],'ORIGINAL_FAILURE_EVIDENCE_DRIFT')

def validate_selection(path,review):
    path=Path(path).resolve();selection=read(path);approval=read(review)
    need(approval.get('verdict')=='PASS' and approval.get('allow_pregrant_recovery') is True and approval.get('recovery_selection_sha256')==sha(path),'RECOVERY_REVIEW_NOT_BOUND')
    need(approval.get('allow_readonly_preflight') is True and approval.get('selection_sha256')==selection['readonly_selection']['sha256'],'READONLY_REVIEW_NOT_BOUND')
    if approval.get('approval_origin')=='INHERITED_REVIEWED_RECOVERY_POLICY':
        cfg,frozen,_,_,_=original(selection['facts']['window']);policy=frozen.get('pregrant_recovery_policy',{})
        parent=Path(approval['parent_review']['path'])
        need(sha(parent)==approval['parent_review']['sha256'] and policy.get('review')==approval['parent_review'],'AUTOMATIC_RECOVERY_PARENT_REVIEW_DRIFT')
        need(any(e.get('role')=='review' and e.get('path')==str(parent) and e.get('sha256')==sha(parent) for e in frozen.get('admission_evidence',[])),'AUTOMATIC_RECOVERY_REVIEW_NOT_IN_ORIGINAL_ADMISSION')
        value=read(parent);need(value.get('verdict')=='PASS' and value.get('allow_automatic_pregrant_recovery') is True,'AUTOMATIC_RECOVERY_NOT_REVIEWED')
        need(policy.get('helper')==selection['permission_helper'] and Path(selection['tools_root']).resolve()==Path(cfg['task_contract']['task_root']).resolve(),'AUTOMATIC_RECOVERY_BINDING')
    need(selection['scope']=='OWNED_PREGRANT_RECOVERY_ONLY' and selection['permission_mutations_allowed'] is False and selection['business_allowed'] is False and selection['directory_query_allowed'] is False,'RECOVERY_SCOPE')
    validate_original_facts(selection)
    need(selection['deadline_seconds']==300 and selection['max_output_bytes']==25165824 and selection['per_command_seconds']==20,'RECOVERY_BOUND_CHANGED')
    need(selection['scratch_template']=='/data/local/tmp/mdds_recovery_{run}_{side}_{attempt}' and selection['permission_mode']=='VERIFY_DENIED_NO_WRITE_SWITCH_RELOAD_SOCKET','RECOVERY_HELPER_SCOPE')
    for p,h in selection['tools_sha256'].items():need(sha(p)==h,'RECOVERY_TOOL_DRIFT')
    need(sha(selection['readonly_selection']['path'])==selection['readonly_selection']['sha256'],'READONLY_SELECTION_DRIFT')
    need(sha(selection['permission_helper']['path'])==selection['permission_helper']['sha256'],'RECOVERY_HELPER_DRIFT')
    need(sha(Path(selection['tools_root'])/'MATERIALIZATION.json')==selection['materialization_sha256'],'RECOVERY_MATERIALIZATION_DRIFT')
    cfg,_,_,_,local=original(selection['facts']['window']);ro=read(selection['readonly_selection']['path'])
    need(ro['targets']==cfg['targets'] and ro['tokens']==cfg['token'] and ro['local_contract_test'] is local and ro['device_driver']==dict(path=cfg['hdc_path'],sha256=sha(cfg['hdc_path'])),'READONLY_DEVICE_SCOPE')
    need(ro['platform_reference']==cfg['preflight_config'] and ro['platform_reference_sha256']==cfg['preflight_config_sha256'] and ro['preflight_output_parent']==str(Path(selection['facts']['window'])/'recovery'),'READONLY_BASELINE_SCOPE')
    return selection

def previous_helpers(base,number,side,cfg):
    known=[]
    for n in range(1,number):
        attempt=base/f'attempt-{n}';step=attempt/side/'helper-mkdir'
        if not (step/'command.json').exists():continue
        started=read(attempt/'STARTED.json');selection=read(started['selection_path'])
        need(sha(started['selection_path'])==started['selection_sha256'],'PREVIOUS_RECOVERY_SELECTION_DRIFT')
        directory=f"/data/local/tmp/mdds_recovery_{cfg['run']}_{side}_{n}"
        need(read(step/'command.json')['argv']==[cfg['hdc_path'],'-t',cfg['targets'][side],'shell',f'test ! -e {directory} && mkdir {directory}'+WRAPPER],'PREVIOUS_HELPER_PATH')
        done=read(step/'result.json');need(done['exit_code']==0 and done.get('capture_complete') is True,'PREVIOUS_HELPER_OWNERSHIP_UNCONFIRMED')
        remote_text(text(step/'stdout.log'),done['exit_code'])
        sent=(attempt/side/'helper-send/command.json').exists()
        known.append(dict(attempt=n,directory=directory,helper=selection['permission_helper'] if sent else None))
    return known

def recover(selection_path,review_path):
    s=validate_selection(selection_path,review_path);base=Path(s['facts']['window'])/'recovery';base.mkdir(exist_ok=True)
    lock=base/'ACTIVE.json';owner=process_identity(os.getpid());nonce=uuid.uuid4().hex
    if lock.exists():
        previous=read(lock);need(not process_still_live(previous.get('owner')),'RECOVERY_ALREADY_RUNNING')
        need(isinstance(previous.get('nonce'),str) and re.fullmatch('[0-9a-f]{32}',previous['nonce']),'RECOVERY_LOCK_NONCE_INVALID')
        prior_recovery_closed(base);lock.rename(base/('ABANDONED_LOCK-'+previous['nonce']+'.json'))
    prior_recovery_closed(base)
    put(lock,dict(owner=owner,nonce=nonce,recorded_utc=stamp()))
    try:return _recover_attempt(selection_path,review_path)
    finally:
        need(read(lock).get('nonce')==nonce,'RECOVERY_LOCK_OWNERSHIP_CHANGED')
        put(base/('LOCK_RELEASED-'+nonce+'.json'),dict(owner=owner,nonce=nonce,execution_finished=True,recorded_utc=stamp()))
        lock.unlink()

def _recover_attempt(selection_path,review_path):
    selection_path=Path(selection_path).resolve();review_path=Path(review_path).resolve();s=validate_selection(selection_path,review_path)
    window=Path(s['facts']['window']);cfg,_,_,_,local=original(window);base=window/'recovery';base.mkdir(exist_ok=True)
    previous=sorted((p for p in base.glob('attempt-*') if re.fullmatch('attempt-[1-9][0-9]*',p.name)),key=lambda p:int(p.name.split('-')[1]))
    if previous and (previous[-1]/'RECOVERY_RESULT.json').exists() and read(previous[-1]/'RECOVERY_RESULT.json').get('status')=='PASS':
        return verify_result(previous[-1]/'RECOVERY_RESULT.json',require_acceptance=False)
    number=int(previous[-1].name.split('-')[1])+1 if previous else 1
    attempt=base/f'attempt-{number}';attempt.mkdir();deadline=time.monotonic()+s['deadline_seconds'];used=0;files={};commands=[]
    put(attempt/'STARTED.json',dict(run=cfg['run'],selection_path=str(selection_path),selection_sha256=sha(selection_path),review_path=str(review_path),review_sha256=sha(review_path),
        recorded_utc=stamp(),owner=process_identity(os.getpid()),owner_nonce=read(base/'ACTIVE.json')['nonce'],resumes=str(previous[-1]) if previous else None,permission_mutations=0,business_dispatches=0))
    def command(tag,argv,kind='READ_ONLY'):
        nonlocal used
        need(time.monotonic()<deadline,'RECOVERY_DEADLINE')
        p=attempt/tag;p.mkdir(parents=True);put(p/'command.json',dict(argv=[str(x) for x in argv],run=cfg['run'],kind=kind,recorded_utc=stamp(),state='DISPATCH_INTENT'))
        env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules;C:/Program Files/WindowsPowerShell/Modules'
        child=subprocess.Popen([str(x) for x in argv],cwd=cfg['repo'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        put(p/'spawn.json',dict(pid=child.pid,identity=process_identity(child.pid)))
        stdout,stderr,timed_out,complete=collect_child(child,min(s['per_command_seconds'],max(0.1,deadline-time.monotonic())))
        (p/'stdout.log').write_bytes(stdout);(p/'stderr.log').write_bytes(stderr);put(p/'result.json',dict(exit_code=child.poll(),timeout=timed_out,capture_complete=complete,drain_budget_seconds=2))
        need(not timed_out and complete,'RECOVERY_COMMAND_TIMEOUT_OR_INCOMPLETE_CAPTURE')
        used+=len(stdout)+len(stderr);need(used<=s['max_output_bytes'],'RECOVERY_OUTPUT_BUDGET')
        commands.append(dict(tag=tag,kind=kind,exit_code=child.returncode));need(child.returncode==0,'RECOVERY_LOCAL_COMMAND_FAILED')
        return stdout.decode('utf-8-sig')
    def shell(side,tag,core,kind='READ_ONLY'):
        return remote_text(command(side+'/'+tag,[cfg['hdc_path'],'-t',cfg['targets'][side],'shell',core+WRAPPER],kind),0)
    result=dict(schema=1,scope='LOCAL_RECOVERY_MODEL' if local else 'REAL_PREGRANT_RECOVERY_VERIFICATION',status='INCOMPLETE',run=cfg['run'],window=str(window),
        selection_path=str(selection_path),selection_sha256=sha(selection_path),review_path=str(review_path),review_sha256=sha(review_path),first_failure=s['facts']['first_failure'],
        original_inputs_sha256=s['facts']['original_inputs_sha256'],permission_actions='NOT_PERFORMED',business_dispatches=0,directory_queries=0,sides={})
    try:
        capture=base/f'current-{number}'
        command('current-preflight',[sys.executable,Path(s['tools_root'])/'recovery/readonly_preflight.py','--selection',s['readonly_selection']['path'],'--review',review_path,'--output',capture])
        need(read(capture/'EXECUTOR_PREFLIGHT.json')['eligible_for_executor_gate'] is True,'CURRENT_PREFLIGHT_FAILED')
        for side in 'AB':
            expected=s['facts']['sides'][side];before=expected['baseline'];after=capture/(side+'-nativetoken.json')
            need(sha(after)==before['sha256'],'PERMISSION_BYTE_DIFFERENCE')
            now_meta=metadata(text(capture/(side+'-metadata.stdout')));need(stable_metadata(now_meta)==stable_metadata(before['metadata']),'METADATA_DIFFERENCE')
            guard_output=attempt/(side+'-snapshot-guard.json');ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
            command(side+'/snapshot-guard',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',Path(s['tools_root'])/'recovery/recovery-guard.ps1',
                '-GuardSource',Path(cfg['repo'])/'scripts/NativePermissionGuard.ps1','-Baseline',before['path'],'-Current',after,'-Run',cfg['run'],'-Side',side,'-Token',cfg['token'][side],'-Output',guard_output],'LOCAL_GUARD')
            need(read(guard_output)['Passed'] is True,'SNAPSHOT_GUARD_FAILED')
            remote=cfg['remote'][side];retained=[]
            if expected['created']:
                shell(side,'owned-directory',f'test -d {remote} && test ! -L {remote}')
                listing=shell(side,'owned-members','ls -1A '+remote).splitlines()
                def observe(i,e):
                    p=remote+'/'+e['name'];return file_observation(shell(side,'owned-file-'+str(i),file_command(p)),p,e['sha256'],e['expected_mode'])
                def absent(i,e):
                    p=remote+'/'+e['name'];shell(side,'owned-absent-'+str(i),f'test ! -e {p} && test ! -L {p}')
                retained=reconcile_owned(expected['retained_owned_artifacts'],listing,observe,absent)
            else:shell(side,'no-unowned-directory',f'test ! -e {remote} && test ! -L {remote}')
            prior_helpers=[]
            for old in previous_helpers(base,number,side,cfg):
                prefix='prior-helper-'+str(old['attempt']);names=shell(side,prefix+'-members','ls -1A '+old['directory']).splitlines()
                need(names==(['native_permission_probe'] if old['helper'] else []),'PRIOR_HELPER_UNEXPLAINED_FILES')
                entry=dict(old,disposition='RETAINED_OWNED_RECOVERY_SETUP')
                if old['helper']:
                    p=old['directory']+'/native_permission_probe';entry['observation']=file_observation(shell(side,prefix+'-hash',file_command(p)),p,old['helper']['sha256'])
                prior_helpers.append(entry)
            scratch=s['scratch_template'].format(run=cfg['run'],side=side,attempt=number);need(scratch==f"/data/local/tmp/mdds_recovery_{cfg['run']}_{side}_{number}",'SCRATCH_PATH')
            shell(side,'helper-mkdir',f'test ! -e {scratch} && mkdir {scratch}','RECOVERY_HELPER_DIRECTORY_CREATE')
            target=scratch+'/native_permission_probe'
            command(side+'/helper-send',[cfg['hdc_path'],'-t',cfg['targets'][side],'file','send',s['permission_helper']['path'],target],'RECOVERY_HELPER_FILE_SEND')
            shell(side,'helper-mode','chmod 700 '+target,'RECOVERY_HELPER_MODE')
            out=shell(side,'helper-hash',file_command(target));helper_observation=file_observation(out,target,s['permission_helper']['sha256'],'700')
            loader=scratch+':'+':'.join(cfg['system_library_dirs'])
            denied=shell(side,'verify-denied',f'env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH={loader} {target} --verify-denied --expected-token {cfg["token"][side]} --preflight-confirmed')
            need('verification_permission=-1 writes=NOT_PERFORMED token_switch=NOT_PERFORMED communication=NOT_RUN' in denied and 'result=PASS' in denied,'DENIED_NOT_PROVEN')
            final=shell(side,'permission-byte-after-helper','sha256sum '+TOKEN);need(final.split()[0]==before['sha256'],'PERMISSION_CHANGED_DURING_RECOVERY')
            after_meta=metadata(shell(side,'metadata-after-helper',metadata_command()));need(stable_metadata(after_meta)==stable_metadata(before['metadata']),'METADATA_CHANGED_DURING_RECOVERY')
            process=shell(side,'processes-after','ps -A');need(not re.search(PROCESS_PATTERN,process),'RECOVERY_PROCESS_PRESENT')
            result['sides'][side]=dict(status='PASS',baseline=before,after=dict(path=str(after),sha256=sha(after),metadata=now_meta),
                guard_path=str(guard_output),guard_sha256=sha(guard_output),retained_owned_artifacts=retained,
                recovery_helper_artifact=dict(path=target,sha256=s['permission_helper']['sha256'],**helper_observation,disposition='RETAINED_RECOVERY_HELPER'),metadata_after_helper=after_meta,
                service_permission='DENIED',permission_helper_actions='READ_ONLY_VERIFY_DENIED',process_absent=True,previous_recovery_artifacts=prior_helpers)
        result['status']='PASS'
    except (OSError,ValueError,KeyError,subprocess.SubprocessError) as error:result['failure']=str(error);result['status']='BLOCKED'
    result.update(recorded_utc=stamp(),commands=commands,output_bytes=used)
    for folder in (attempt,base/f'current-{number}'):
        if folder.exists():
            for p in folder.rglob('*'):
                if p.is_file():files[str(p)]=sha(p)
    result['evidence_sha256']=files;put(attempt/'RECOVERY_RESULT.json',result);return result

def auto_recover(window):
    window=Path(window).resolve();cfg,frozen,_,_,_=original(window);policy=frozen.get('pregrant_recovery_policy',{})
    need(policy.get('scope')=='OWNED_PREGRANT_RECOVERY_ONLY','RECOVERY_PENDING_NO_BOUND_POLICY')
    parent=policy.get('review',{});need(parent.get('path') and sha(parent['path'])==parent.get('sha256'),'RECOVERY_PENDING_REVIEW_MISSING')
    need(any(e.get('role')=='review' and e.get('path')==parent['path'] and e.get('sha256')==parent['sha256'] for e in frozen.get('admission_evidence',[])),'RECOVERY_PENDING_REVIEW_NOT_ADMITTED')
    approved=read(parent['path']);need(approved.get('verdict')=='PASS' and approved.get('allow_automatic_pregrant_recovery') is True,'RECOVERY_PENDING_POLICY_NOT_REVIEWED')
    helper=policy.get('helper',{});need(helper.get('path') and sha(helper['path'])==helper.get('sha256'),'RECOVERY_PENDING_HELPER_BINDING')
    selection=window/'recovery/AUTO_SELECTION.json';review=window/'recovery/AUTO_POLICY_BINDING.json'
    value=prepare(window,cfg['task_contract']['task_root'],selection,helper['path'])
    put(review,dict(verdict='PASS',approval_origin='INHERITED_REVIEWED_RECOVERY_POLICY',parent_review=parent,
        allow_pregrant_recovery=True,recovery_selection_sha256=sha(selection),allow_readonly_preflight=True,selection_sha256=value['readonly_selection']['sha256']))
    return recover(selection,review)

def verify_result(result_path,require_acceptance=True):
    result_path=Path(result_path).resolve();r=read(result_path);selection=validate_selection(r['selection_path'],r['review_path'])
    need(r['status']=='PASS' and r['run']==selection['facts']['run'] and r['window']==selection['facts']['window'],'RECOVERY_RESULT_NOT_PASS_OR_IDENTITY')
    need(result_path.parent.parent==Path(r['window'])/'recovery' and re.fullmatch('attempt-[1-9][0-9]*',result_path.parent.name),'RECOVERY_RESULT_LOCATION')
    need(r['selection_sha256']==sha(r['selection_path']) and r['review_sha256']==sha(r['review_path']) and r['original_inputs_sha256']==selection['facts']['original_inputs_sha256'],'RECOVERY_PROVENANCE_DRIFT')
    for p,h in r['evidence_sha256'].items():need(sha(p)==h,'RECOVERY_RAW_EVIDENCE_DRIFT')
    need(set(r['sides'])=={'A','B'} and all(v.get('status')=='PASS' and v.get('service_permission')=='DENIED' and v.get('process_absent') is True for v in r['sides'].values()),'RECOVERY_SIDES_INCOMPLETE')
    need(r['permission_actions']=='NOT_PERFORMED' and r['business_dispatches']==0 and r['directory_queries']==0,'RECOVERY_SCOPE_DRIFT')
    cfg,_,_,_,local=original(r['window']);need(r['scope']==('LOCAL_RECOVERY_MODEL' if local else 'REAL_PREGRANT_RECOVERY_VERIFICATION'),'RECOVERY_AUTHORITY_CLASS')
    attempt=result_path.parent;number=int(attempt.name.split('-')[1]);capture=attempt.parent/f'current-{number}'
    evidence=r['evidence_sha256']
    def bound(path):
        path=Path(path);need(evidence.get(str(path))==sha(path),'REQUIRED_RECOVERY_EVIDENCE_NOT_BOUND');return path
    def checked(tag,argv,kind='READ_ONLY'):
        p=attempt/tag;cmd=read(bound(p/'command.json'));result=read(bound(p/'result.json'))
        need(cmd['argv']==[str(x) for x in argv] and cmd['run']==cfg['run'] and cmd['kind']==kind,'RECOVERY_COMMAND_IDENTITY')
        need(result['exit_code']==0 and result['timeout'] is False and result.get('capture_complete') is True,'RECOVERY_COMMAND_INCOMPLETE')
        read(bound(p/'spawn.json')) # Historical reaped child: do not reopen a potentially reused PID.
        bound(p/'stderr.log');return text(bound(p/'stdout.log'))
    started=read(bound(attempt/'STARTED.json'));nonce=started.get('owner_nonce')
    need(isinstance(nonce,str) and re.fullmatch('[0-9a-f]{32}',nonce),'RECOVERY_OWNER_NONCE')
    released=read(attempt.parent/('LOCK_RELEASED-'+nonce+'.json'))
    need(released.get('owner')==started.get('owner') and released.get('nonce')==nonce and released.get('execution_finished') is True,'RECOVERY_OWNER_NOT_FINISHED')
    def shell(side,tag,core,kind='READ_ONLY'):
        return remote_text(checked(side+'/'+tag,[cfg['hdc_path'],'-t',cfg['targets'][side],'shell',core+WRAPPER],kind),0)
    # Replay the existing readonly collector's actual raw command receipts;
    # its top-level PASS is not evidence by itself.
    collected=read(bound(capture/'EXECUTOR_PREFLIGHT.json'))
    checked('current-preflight',[sys.executable,Path(selection['tools_root'])/'recovery/readonly_preflight.py','--selection',selection['readonly_selection']['path'],'--review',r['review_path'],'--output',capture])
    need(collected['local_contract_test'] is local and collected['platform']==cfg['platform'],'CURRENT_PLATFORM_CAPTURE_SCOPE')
    def raw_capture(tag,args):
        command=read(bound(capture/(tag+'.command.json')));done=read(bound(capture/(tag+'.result.json')))
        need(command['argv']==args and done['argv']==args and done['exit_code']==0 and done['timed_out'] is False and done.get('capture_complete') is True,'CURRENT_CAPTURE_COMMAND_IDENTITY_OR_EXIT')
        read(bound(capture/(tag+'.spawn.json'))) # Exit + complete capture belong to the original process instance.
        bound(capture/(tag+'.stderr'));return text(bound(capture/(tag+'.stdout')))
    inventory=raw_capture('targets',[cfg['hdc_path'],'list','targets']).split()
    need(all(t in inventory for t in cfg['targets'].values()),'REGISTERED_TARGET_MISSING')
    for side in 'AB':
        expected=selection['facts']['sides'][side];before=expected['baseline'];side_result=r['sides'][side]
        def current(tag,core):return remote_text(raw_capture(side+'-'+tag,[cfg['hdc_path'],'-t',cfg['targets'][side],'shell',core+WRAPPER]),0)
        for i,item in enumerate(cfg['platform'][side]['files']+[cfg['platform'][side]['service_server']]):
            output=current('platform-'+str(i),'sha256sum '+item['path'])
            need(re.fullmatch(item['sha256']+r'\s+'+re.escape(item['path']),output),'PLATFORM_CURRENT_HASH')
        for i,item in enumerate(cfg['platform'][side]['parameters']):
            need(current('parameter-'+str(i),'uname -a' if item['name']=='kernel' else 'param get '+item['name'])==item['value'],'PLATFORM_CURRENT_PARAMETER')
        need(not re.search(PROCESS_PATTERN,current('processes','ps -A')),'CURRENT_PROCESS_PRESENT')
        current_meta=metadata(current('metadata',metadata_command()));need(stable_metadata(current_meta)==stable_metadata(before['metadata']),'CURRENT_METADATA_DIFFERENCE')
        after=bound(capture/(side+'-nativetoken.json'))
        raw_capture(side+'-token-recv',[cfg['hdc_path'],'-t',cfg['targets'][side],'file','recv',TOKEN,str(after)])
        need(sha(after)==before['sha256'] and current('token-hash','sha256sum '+TOKEN).split()[0]==before['sha256'],'CURRENT_PERMISSION_BYTE_DIFFERENCE')
        guard=read(bound(attempt/(side+'-snapshot-guard.json')))
        ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
        checked(side+'/snapshot-guard',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',Path(selection['tools_root'])/'recovery/recovery-guard.ps1',
            '-GuardSource',Path(cfg['repo'])/'scripts/NativePermissionGuard.ps1','-Baseline',before['path'],'-Current',after,'-Run',cfg['run'],'-Side',side,'-Token',cfg['token'][side],'-Output',attempt/(side+'-snapshot-guard.json')],'LOCAL_GUARD')
        need(guard['run']==cfg['run'] and guard['side']==side and guard['Passed'] is True and guard['byte_match'] is True and guard['ownership']['Passed'] is True and guard['snapshot']['Passed'] is True and guard['snapshot']['Phase']=='Revoked' and str(guard['snapshot']['TokenId'])==cfg['token'][side],'CURRENT_JSON_OWNERSHIP_GUARD')
        need(side_result['baseline']==before and side_result['after']==dict(path=str(after),sha256=sha(after),metadata=current_meta),'RECOVERY_BASELINE_PROVENANCE')
        remote=cfg['remote'][side];retained=[]
        if expected['created']:
            shell(side,'owned-directory',f'test -d {remote} && test ! -L {remote}')
            listing=shell(side,'owned-members','ls -1A '+remote).splitlines()
            def observe(i,item):
                p=remote+'/'+item['name'];return file_observation(shell(side,'owned-file-'+str(i),file_command(p)),p,item['sha256'],item['expected_mode'])
            def absent(i,item):
                p=remote+'/'+item['name'];shell(side,'owned-absent-'+str(i),f'test ! -e {p} && test ! -L {p}')
            retained=reconcile_owned(expected['retained_owned_artifacts'],listing,observe,absent)
        else:shell(side,'no-unowned-directory',f'test ! -e {remote} && test ! -L {remote}')
        need(side_result['retained_owned_artifacts']==retained,'OWNED_ARTIFACT_RESULT_DRIFT')
        prior_helpers=[]
        for old in previous_helpers(attempt.parent,number,side,cfg):
            prefix='prior-helper-'+str(old['attempt']);names=shell(side,prefix+'-members','ls -1A '+old['directory']).splitlines()
            need(names==(['native_permission_probe'] if old['helper'] else []),'PRIOR_HELPER_UNEXPLAINED_FILES')
            entry=dict(old,disposition='RETAINED_OWNED_RECOVERY_SETUP')
            if old['helper']:
                p=old['directory']+'/native_permission_probe';entry['observation']=file_observation(shell(side,prefix+'-hash',file_command(p)),p,old['helper']['sha256'])
            prior_helpers.append(entry)
        need(side_result['previous_recovery_artifacts']==prior_helpers,'PRIOR_RECOVERY_ARTIFACT_EVIDENCE')
        scratch=f"/data/local/tmp/mdds_recovery_{cfg['run']}_{side}_{number}";target=scratch+'/native_permission_probe'
        shell(side,'helper-mkdir',f'test ! -e {scratch} && mkdir {scratch}','RECOVERY_HELPER_DIRECTORY_CREATE')
        checked(side+'/helper-send',[cfg['hdc_path'],'-t',cfg['targets'][side],'file','send',selection['permission_helper']['path'],target],'RECOVERY_HELPER_FILE_SEND')
        shell(side,'helper-mode','chmod 700 '+target,'RECOVERY_HELPER_MODE')
        helper_observation=file_observation(shell(side,'helper-hash',file_command(target)),target,selection['permission_helper']['sha256'],'700')
        need(side_result['recovery_helper_artifact']==dict(path=target,sha256=selection['permission_helper']['sha256'],**helper_observation,disposition='RETAINED_RECOVERY_HELPER'),'RECOVERY_HELPER_RESULT_DRIFT')
        loader=scratch+':'+':'.join(cfg['system_library_dirs'])
        denied=shell(side,'verify-denied',f'env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH={loader} {target} --verify-denied --expected-token {cfg["token"][side]} --preflight-confirmed')
        if not local:need(not re.search('HOST_FAKE|LOCAL_MODEL|SIMULATED',denied),'MODEL_CANNOT_BE_DEVICE_PERMISSION_EVIDENCE')
        need(len(re.findall(r'^native_permission verification_permission=-1 writes=NOT_PERFORMED token_switch=NOT_PERFORMED communication=NOT_RUN$',denied,re.M))==1 and 'result=PASS' in denied,'CURRENT_MANAGER_DENIED_NOT_OBSERVED')
        need(shell(side,'permission-byte-after-helper','sha256sum '+TOKEN).split()[0]==before['sha256'],'POST_HELPER_PERMISSION_DRIFT')
        final_meta=metadata(shell(side,'metadata-after-helper',metadata_command()));need(stable_metadata(final_meta)==stable_metadata(before['metadata']) and final_meta==side_result['metadata_after_helper'],'POST_HELPER_METADATA_DRIFT')
        need(not re.search(PROCESS_PATTERN,shell(side,'processes-after','ps -A')),'POST_HELPER_PROCESS_PRESENT')
    if require_acceptance:
        acceptance=read(result_path.parent/'INDEPENDENT_ACCEPTANCE.json')
        need(acceptance.get('verdict')=='PASS' and acceptance.get('environment_restored') is True and acceptance.get('run')==r['run'] and acceptance.get('window')==r['window'] and acceptance.get('recovery_result_sha256')==sha(result_path),'RECOVERY_INDEPENDENT_ACCEPTANCE_REQUIRED')
    return r

if __name__=='__main__':
    parser=argparse.ArgumentParser();modes=parser.add_mutually_exclusive_group(required=True)
    modes.add_argument('--prepare-window',type=Path);modes.add_argument('--recover',type=Path);modes.add_argument('--verify-result',type=Path)
    modes.add_argument('--auto-window',type=Path)
    parser.add_argument('--tools-root',type=Path);parser.add_argument('--output',type=Path);parser.add_argument('--helper',type=Path);parser.add_argument('--review',type=Path)
    args=parser.parse_args()
    if args.auto_window:
        need(not any((args.review,args.tools_root,args.output,args.helper)),'AUTOMATIC_RECOVERY_ONLY_ARGUMENTS')
        outcome=auto_recover(args.auto_window);print(outcome['status']);sys.exit(0 if outcome['status']=='PASS' else 1)
    elif args.prepare_window:
        need(args.tools_root and args.output and args.helper and args.review is None,'RECOVERY_PREPARE_ARGUMENTS')
        prepare(args.prepare_window,args.tools_root,args.output,args.helper);print('RECOVERY_SELECTION_PREPARED_NOT_EXECUTED')
    elif args.recover:
        need(args.review and not any((args.tools_root,args.output,args.helper)),'RECOVERY_ONLY_ARGUMENTS')
        outcome=recover(args.recover,args.review);print(outcome['status']);sys.exit(0 if outcome['status']=='PASS' else 1)
    else:
        need(not any((args.review,args.tools_root,args.output,args.helper)),'VERIFY_ONLY_ARGUMENTS')
        verify_result(args.verify_result);print('APPENDED_RECOVERY_INDEPENDENTLY_ACCEPTED')
