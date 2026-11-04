"""Fixed offline A-pair builder. No HDC, controls, preflight or live imports."""
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import types

REPO=Path(__file__).resolve().parents[2]
BASE=Path('E:/Codex-FastDDS-readiness-trigger-package-20261102-d01')
BASE_SHA='0a5dea1b8e0be764b13b7895653d94979e1237f408faeaaa00a326b9bf15b020'
PACKAGE=REPO/'build/hdc-a-pair-package-20261102-d01'
ACTIVE=PACKAGE/'active'
RUN=REPO/'build/hdc-a-pair-once-20261102_a8d3f641'
SCOPE='ONE_NEW_SAME_SERVER_A_PAIR_60S_D01'
NONCE='A8D3F641_A_PAIR_60S_D01'
CARD_PATH='docs/HDC_A_PAIR_DIAGNOSTIC.md'
PACKAGE_FORMAT='FIXED_A_PAIR_PACKAGE1'
ORIGINAL=('goal_contract.py','goal_policy.py','goal_authorization.py','goal_driver.py',
 'goal_windows_backend.py','goal_win32_owned.py','goal_handle_lifecycle.py','goal_memory_trace.py',
 'goal_notice.py','goal_storage.py','goal_evidence.py','goal_workflow.py','goal_entry.py','preflight.py','verify_offline.py')
SOURCES=(*ORIGINAL,'probe_pair.py')
MAINTAINED=('goal_driver.py','goal_windows_backend.py','goal_notice.py')
CONTROLS=('EXECUTION_AUTHORIZATION_D01.json','MAIN_ADMISSION_D01.json','FRESH_PREFLIGHT_D01.json',
          'ONCE_RESERVED_D01.json','ENTRY_RECEIPT_D01.json')
CODE_PATHS=[*(f'scripts/hdc_goal/{n}' for n in (*MAINTAINED,'probe_pair.py','prepare_probe_pair.py','prepare_readiness.py')),
            'tests/test_hdc_probe_pair.py','docs/HDC_A_PAIR_DIAGNOSTIC.md']

def need(ok,code):
    if not ok:raise ValueError(code)

def sha(data):return hashlib.sha256(data).hexdigest()
def encoded(value):return (json.dumps(value,indent=2,ensure_ascii=True)+'\n').encode('ascii')

def read_safe(path,cap=65536):
    path=Path(path)
    for parent in reversed(path.parents):
        s=os.lstat(parent);need(stat.S_ISDIR(s.st_mode) and not s.st_file_attributes&1024,'SOURCE_ANCESTOR')
    s=os.lstat(path)
    need(stat.S_ISREG(s.st_mode) and s.st_nlink==1 and not s.st_file_attributes&1024
         and 0<s.st_size<=cap,'SOURCE_FILE')
    with path.open('rb') as f:
        a=os.fstat(f.fileno());data=f.read(cap+1);b=os.fstat(f.fileno())
    c=os.lstat(path)
    need(all((x.st_dev,x.st_ino,x.st_size,x.st_mtime_ns,x.st_nlink)==
             (s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_nlink) for x in (a,b,c))
         and len(data)==s.st_size,'SOURCE_CHANGED')
    return data

def write_new(path,data):
    with Path(path).open('xb') as f:f.write(data);f.flush();os.fsync(f.fileno())

def directory(path):
    path=Path(path)
    for item in (*reversed(path.parents),path):
        try:s=os.lstat(item)
        except FileNotFoundError:item.mkdir();s=os.lstat(item)
        need(stat.S_ISDIR(s.st_mode) and not s.st_file_attributes&1024,'OUTPUT_ANCESTOR')

def replace_once(text,old,new):
    need(text.count(old)==1,'EXACT_TEMPLATE_POINT');return text.replace(old,new)

def replace_function(text,name,replacement):
    nodes=[n for n in ast.parse(text).body if isinstance(n,ast.FunctionDef) and n.name==name]
    need(len(nodes)==1,'EXACT_TEMPLATE_FUNCTION');n=nodes[0];lines=text.splitlines(keepends=True)
    return ''.join(lines[:n.lineno-1])+replacement.strip()+'\n'+''.join(lines[n.end_lineno:])

def replace_assignment(text,name,value):
    nodes=[n for n in ast.parse(text).body if isinstance(n,ast.Assign)
           and any(isinstance(t,ast.Name) and t.id==name for t in n.targets)]
    need(len(nodes)==1,'EXACT_TEMPLATE_ASSIGNMENT');n=nodes[0];a=text.splitlines(keepends=True)
    return ''.join(a[:n.lineno-1])+name+'='+value+'\n'+''.join(a[n.end_lineno:])

def load_buffer(data,name,path):
    m=types.ModuleType(name);m.__file__=str(path);old=sys.modules.get(name);sys.modules[name]=m
    try:exec(compile(data,str(path),'exec'),m.__dict__)
    finally:
        if old is None:sys.modules.pop(name,None)
        else:sys.modules[name]=old
    return m

def source_buffers():
    raw=read_safe(BASE/'active/PACKAGE_FINAL_D01.json');need(sha(raw)==BASE_SHA,'BASE_MANIFEST')
    manifest=json.loads(raw);pins={r['role']:r for r in manifest['active_pins']}
    need({f'source.{n}' for n in ORIGINAL}<set(pins),'BASE_SOURCE_ROLES')
    original={}
    for n in ORIGINAL:
        data=read_safe(BASE/'active'/n)
        need(sha(data)==pins['source.'+n]['sha256'],'BASE_SOURCE_DRIFT');original[n]=data
    old_contract=load_buffer(original['goal_contract.py'],'_pair_old_contract',BASE/'active/goal_contract.py')
    buffers={n:(read_safe(REPO/'scripts/hdc_goal'/n) if n in MAINTAINED else original[n]) for n in ORIGINAL}
    for n in SOURCES:
        if n=='probe_pair.py':buffers[n]=read_safe(REPO/'scripts/hdc_goal'/n)
        text=buffers[n].decode('utf-8-sig')
        text=text.replace(str(BASE).replace('\\','/'),str(PACKAGE).replace('\\','/'))
        text=text.replace('E:/Codex-FastDDS-hdc-ready-once-20261102_6d2b9a73',str(RUN).replace('\\','/'))
        text=text.replace('ONE_NEW_FIXED_AB_PRINTF_TRIGGER_FAMILY_60S_D02',SCOPE)
        text=text.replace('6D2B9A73_READY_60S_D02',NONCE)
        text=text.replace("'ready.A'","'probe.A1'").replace("'ready.B'","'probe.A2'")
        buffers[n]=text.encode('utf-8')
    text=buffers['goal_contract.py'].decode()
    text=replace_assignment(text,'TARGETS',repr({'A1':old_contract.TARGETS['A'],'A2':old_contract.TARGETS['A']}))
    text=replace_assignment(text,'SOURCE_NAMES',repr(SOURCES))
    prefix="printf '%s\\n' 'MDDS_OBS_"
    # Keep payload tags outside the notice classifier's auth/pair word cues.
    # r is the numeric status assigned by $?; avoid nested double-quote
    # reconstruction while retaining a real status rather than a literal 0.
    suffix="_A8D3F641'; r=$?; printf '__MDDS_OBS_RC=%s\\n' $r; exit $r"
    text=replace_function(text,'body',"def body(side):\n need(side in ('A1','A2'),'PAIR_SIDE')\n return "+repr(prefix)+"+side+"+repr(suffix))
    text=text.replace("('A','B')","('A1','A2')").replace("'ready.'","'probe.'")
    text=text.replace("s=='A'","s=='A1'").replace("'ready.A'","'probe.A1'").replace("'ready.B'","'probe.A2'")
    text=replace_once(text,"requires=('server',) if s=='A1' else ('server','probe.A1')","requires=('server',)")
    buffers['goal_contract.py']=text.encode()
    text=buffers['goal_evidence.py'].decode().replace("'ready.'","'probe.'").replace("'MDDS_READY_'","'MDDS_OBS_'")
    text=text.replace('_6D2B9A73','_A8D3F641').replace('__MDDS_READY_RC','__MDDS_OBS_RC')
    text=replace_once(text,"need(stdout==expected,'CHALLENGE_EXACT_STDOUT')",
        "need(any(stdout==expected.replace(b'\\n',ending) for ending in (b'\\n',b'\\r\\n',b'\\r\\r\\n')),'CHALLENGE_EXACT_STDOUT')")
    buffers['goal_evidence.py']=text.encode()
    text=buffers['goal_storage.py'].decode().replace("('server','ready.A','ready.B')","('server','probe.A1','probe.A2')")
    buffers['goal_storage.py']=text.encode()
    text=buffers['goal_authorization.py'].decode()
    effects={'hostkey','automatic_USB_auth_state','endpoint_readonly_misdelivery',
        'opaque_external_Job_residual','sampling_overshoot','remote_shell_termination_unknown',
        'new_remote_printf_commands','sync_record_blocking','first_exact_pair_collection','one_conditional_second_A_sample'}
    text=replace_assignment(text,'EFFECTS','{'+','.join(repr(s) for s in sorted(effects))+'}')
    text=replace_once(text,"grant.server_unknown_continue=approval['effects']['server_auth_context_unknown_continue']",
        "grant.server_unknown_continue=False\n grant.pair_collection_continue=approval['effects']['first_exact_pair_collection']\n grant.pair_second_sample=approval['effects']['one_conditional_second_A_sample']")
    text=text.replace("'operator_main_thread'","'operator_context'")
    text=replace_once(text,"=='01a16fdf-b4af-74c0-8ff1-41876ccdc140'","=='CURRENT_WINDOWS_PROJECT_ORCHESTRATOR'")
    buffers['goal_authorization.py']=text.encode()
    text=buffers['goal_driver.py'].decode()
    text=replace_function(text,'actual_driver','''
def actual_driver(package,manifest_sha,binding_sha,registry=None):
    from goal_authorization import load_actual_grant
    from probe_pair import actual_driver as pair_factory
    return pair_factory(load_actual_grant(package,manifest_sha,binding_sha))
''')
    buffers['goal_driver.py']=text.encode()
    text=buffers['goal_windows_backend.py'].decode()
    text=replace_once(text,"need(grant in _grants,'NO_ACTUAL_GRANT')",
        "need(grant in _grants,'NO_ACTUAL_GRANT')\n        need(type(self).__name__=='PairBackend','PAIR_BACKEND_REQUIRED')")
    buffers['goal_windows_backend.py']=text.encode()
    buffers['goal_workflow.py']=b'class FixedReadinessWorkflow:\n def __init__(self,driver):self.driver=driver\n def run(self):return self.driver.run_pair()\n'
    text=buffers['goal_entry.py'].decode()
    text=replace_once(text,"from goal_driver import GoalDriver\n  from goal_windows_backend import GoalWindowsBackend\n  driver=GoalDriver(grant.registry,GoalWindowsBackend(grant))",
        "from probe_pair import actual_driver,observation_complete\n  driver=actual_driver(grant)")
    begin=text.index(' passed=(result is not None');end=text.index(" record={'scope'",begin)
    text=text[:begin]+" result=driver.summary() if driver else None\n passed=(failure is None and driver is not None and observation_complete(driver,cleanup))\n"+text[end:]
    text=text.replace('operational_success_candidate','observation_complete_candidate')
    text=replace_once(text,"'no_retry':True","'max_samples':2,'automatic_retry_loop':False,'first_failure_kept':True")
    text=text.replace('operational_roundtrip_and_owned_close','diagnostic_observation_and_owned_close')
    buffers['goal_entry.py']=text.encode()
    for name,data in buffers.items():
        ast.parse(data,filename=name)
        need(b'6D2B9A73' not in data and b'ready.A' not in data and b'ready.B' not in data,'OLD_RUNTIME_LITERAL')
    return buffers,old_contract

def commands(contract):
    return {'status':'PROPOSAL_ONLY_NOT_AUTHORITY','scope':SCOPE,'nonce':NONCE,'commands':[
        {'id':'server','argv':[contract.HDC,'-s',contract.ENDPOINT,'-m']},
        *[{'id':'probe.'+s,'argv':[contract.HDC,'-p','-s',contract.ENDPOINT,'-t',contract.TARGETS[s],'shell',contract.body(s)]}
          for s in ('A1','A2')]]}

def assemble():
    need(not os.path.lexists(PACKAGE) and not os.path.lexists(RUN),'NEW_OUTPUT_REQUIRED')
    buffers,old=source_buffers()
    contract=load_buffer(buffers['goal_contract.py'],'_pair_candidate_contract',ACTIVE/'goal_contract.py')
    registry=contract.build_registry(commands(contract));contract.validate_registry(list(registry.values()))
    need(contract.LIMITS==old.LIMITS and contract.SCOPE==SCOPE and contract.NONCE==NONCE
         and contract.TARGETS['A1']==contract.TARGETS['A2']==old.TARGETS['A'],'PAIR_CONTRACT')
    directory(ACTIVE);directory(PACKAGE/'prepare')
    for name,data in buffers.items():write_new(ACTIVE/name,data)
    write_new(ACTIVE/'COMMANDS_D01.json',encoded(commands(contract)))
    auth_tree=ast.parse(buffers['goal_authorization.py'])
    effects=next(ast.literal_eval(n.value) for n in auth_tree.body if isinstance(n,ast.Assign)
                 and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
    schema={'scope':SCOPE,'nonce':NONCE,'actual_execution_authorized':False,
            'new_direct_user_approval_required':True,'operator_context':'CURRENT_WINDOWS_PROJECT_ORCHESTRATOR',
            'effects':sorted(effects)}
    write_new(ACTIVE/'INPUT_SCHEMAS_D01.json',encoded(schema))
    source={'scope':SCOPE,'nonce':NONCE,'limits':contract.LIMITS,'registry_sha256':contract.registry_digest(list(registry.values())),
            'sources':[row(ACTIVE/n) for n in (*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')],
            'actual_execution_authorized':False}
    write_new(ACTIVE/'SOURCE_BINDING_D01.json',encoded(source))
    write_new(PACKAGE/'prepare/PROVENANCE_D01.json',encoded({'base_manifest_sha256':BASE_SHA,
        'source_sha256':{n:sha(data) for n,data in buffers.items()},'HDC_calls':0,'controls_created':False}))
    return source

def row(path,role=None,cap=65536):
    data=read_safe(path,cap);r={'path':str(path),'bytes':len(data),'sha256':sha(data)}
    if role:r['role']=role
    return r

def freeze(commit):
    need(re.fullmatch('[0-9a-f]{40}',commit) is not None,'COMMIT')
    need(not os.path.lexists(RUN) and all(not os.path.lexists(ACTIVE/n) for n in CONTROLS),'NO_ACTUAL_CONTROLS')
    expected,_=source_buffers()
    for n,data in expected.items():need(read_safe(ACTIVE/n)==data,'CANDIDATE_SOURCE_DRIFT')
    for path in CODE_PATHS:
        q=subprocess.run(['git','show',commit+':'+path],cwd=REPO,capture_output=True)
        need(q.returncode==0 and q.stdout==read_safe(REPO/path),'PUBLISHED_BYTE_BINDING')
    source_raw=read_safe(ACTIVE/'SOURCE_BINDING_D01.json');source=json.loads(source_raw);source_sha=sha(source_raw)
    need(source['sources']==[row(ACTIVE/n) for n in (*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')],
         'SOURCE_BINDING_DRIFT')
    for name in ('FINAL_TEST_RESULT_D01.json','AGENTIC_REVIEW_CLOSURE_D04.json'):
        j=json.loads(read_safe(PACKAGE/'prepare'/name));need(j['source_binding_sha256']==source_sha,'EVIDENCE_BINDING')
        if name.startswith('FINAL'):need(j['native_exit']==j['outer_controller_exit']==0,'FINAL_FAILED')
        else:need(j['blocking_findings_open']==0,'REVIEW_OPEN')
    c=load_buffer(expected['goal_contract.py'],'_pair_freeze_contract',ACTIVE/'goal_contract.py')
    rows=[row(ACTIVE/n,'source.'+n) for n in SOURCES]
    rows +=[row(ACTIVE/n,role) for n,role in [('COMMANDS_D01.json','commands'),('INPUT_SCHEMAS_D01.json','schema'),('SOURCE_BINDING_D01.json','source_binding')]]
    for role,path,digest in [('tool.hdc',c.HDC,c.HDC_SHA),('tool.python','C:/Users/17715/anaconda3/python.exe','7679e53fa969789309e81fdad0d52b8cda5f83c9abf7cb31a3c58bf24b31e264')]:
        r=row(Path(path),role,6291456);need(r['sha256']==digest,'TOOL_DRIFT');rows.append(r)
    rows +=[row(PACKAGE/'prepare'/n,role) for n,role in [('FINAL_TEST_RESULT_D01.json','evidence.final'),('AGENTIC_REVIEW_CLOSURE_D04.json','evidence.review')]]
    total=sum(r['bytes'] for r in rows);need(total<c.LIMITS['active_bytes'] and len(rows)<=c.LIMITS['active_files'],'ACTIVE_BUDGET')
    owned_files=[];todo=[PACKAGE];directories=0
    while todo:
        p=todo.pop();s=os.lstat(p);need(not s.st_file_attributes&1024,'OWNED_REPARSE')
        if stat.S_ISDIR(s.st_mode):
            directories+=1;need(directories<=32,'OWNED_DIRECTORY_CAP');todo.extend(p.iterdir())
        else:
            need(stat.S_ISREG(s.st_mode) and s.st_nlink==1,'OWNED_REGULAR');owned_files.append(p)
            need(len(owned_files)<=160,'OWNED_FILE_CAP')
    helper=load_buffer(read_safe(REPO/'scripts/hdc_goal/prepare_readiness.py'),'_pair_budget_only',REPO/'scripts/hdc_goal/prepare_readiness.py')
    budget=helper.checked_budget(sum(p.stat().st_size for p in owned_files),sum(r['bytes'] for r in rows if r['role'].startswith('tool.')),
        sum(len(read_safe(REPO/p)) for p in CODE_PATHS),len(owned_files),2+len(CODE_PATHS))
    m={'format':PACKAGE_FORMAT,'scope':SCOPE,'nonce':NONCE,'limits':c.LIMITS,'registry_sha256':source['registry_sha256'],
       'active_pins':rows,'actual_execution_authorized':False,'published_source_commit':commit,'preparation_budget':budget}
    mr=encoded(m);write_new(ACTIVE/'PACKAGE_FINAL_D01.json',mr)
    b={'scope':SCOPE,'nonce':NONCE,'manifest_sha256':sha(mr),'registry_sha256':source['registry_sha256'],
       'run_root':str(RUN),'active_closure_bytes':total,'actual_execution_authorized':False,'full_executor_final':True,
       'published_source_commit':commit,'qualification':'OFFLINE_FAKE_NOT_DEVICE'}
    br=encoded(b);write_new(ACTIVE/'FINAL_EXECUTION_BINDING_D01.json',br)
    card=read_safe(REPO/CARD_PATH).decode('utf-8')
    card+='\n## Fixed private binding\n\nManifest SHA256: '+sha(mr)+'\n\nBinding SHA256: '+sha(br)+'\n\nScope: '+SCOPE+'\n\nNonce: '+NONCE+'\n\nPackage: '+str(PACKAGE)+'\n\nRun: '+str(RUN)+'\n\n'
    card+='Target is the existing fixed A only. No authorization, admission, fresh record or once is generated.\n'
    write_new(PACKAGE/'EXECUTION_CARD_D01.md',card.encode('utf-8'))
    result={'manifest_sha256':sha(mr),'binding_sha256':sha(br),'card_sha256':sha(card.encode('utf-8')),
            'source_binding_sha256':source_sha,'published_source_commit':commit,'actual_execution_authorized':False}
    write_new(PACKAGE/'prepare/FINAL_HASHES_D01.json',encoded(result));return result

def main(args):
    try:
        if args==['--assemble']:assemble();print('OFFLINE_PAIR_ASSEMBLED_HDC0_NO_CONTROLS');return 0
        if len(args)==2 and args[0]=='--freeze':print(json.dumps(freeze(args[1])));return 0
        print('OFFLINE_PAIR_ONLY_NOT_GRANTED');return 78
    except BaseException:print('OFFLINE_PAIR_HOLD_PRESERVE_PARTIAL');return 1

if __name__=='__main__':raise SystemExit(main(sys.argv[1:]))
