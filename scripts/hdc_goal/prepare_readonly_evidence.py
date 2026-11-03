"""Offline D04 read-only packet. No controls, HDC, raw or SDK access."""
import ast
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('_readonly_state_builder',HERE/'prepare_state_samples.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
b=base.builder
state_buffers=b.source_buffers
REPO=b.REPO
PACKAGE=REPO/'build/d04-readonly-evidence-package-20261103-d01'
ACTIVE=PACKAGE/'active'
RUN=Path('E:/Codex-FastDDS-d04-readonly-once-20261103_e31b704c')
SCOPE='ONE_NEW_D04_FIXED_AB_READONLY_EVIDENCE_60S_D01'
NONCE='E31B704C_D04_READONLY_60S_D01'
DOC='docs/D04_READONLY_EVIDENCE_EXECUTION.md'
SOURCES=(*b.ORIGINAL,'readonly_evidence.py','goal_read_context.py')
CODE_PATHS=['scripts/hdc_goal/readonly_evidence.py','scripts/hdc_goal/prepare_readonly_evidence.py',
 'tests/test_hdc_readonly_evidence.py',DOC]
D04=REPO/'build/directdds-terminal-package-20261103-d01/active'
D04_MANIFEST='876fbe1eb03c0475c315d5dcb7556c0fb7532c5926c6c04e443fdbbc75c58472'


def load_pinned_d04(name):
    raw=b.read_safe(D04/'GOAL_PACKAGE_FINAL_D01.json');b.need(b.sha(raw)==D04_MANIFEST,'D04_MANIFEST')
    rows={r['role']:r for r in json.loads(raw)['active_pins']}
    data=b.read_safe(D04/name);b.need(b.sha(data)==rows['source.'+name]['sha256'],'D04_SOURCE_PIN')
    return data


def contexts():
    """Only historical accepted metadata; never open raw or an executable."""
    p=REPO/'build/native-data-analysis-20261103-d01/REMOTE_PROCESS_CANDIDATES_D01.json'
    j=json.loads(b.read_safe(p));result={}
    # This is the already accepted Main-generated run, not a new observation.
    # Pair run is taken from the fixed producer control; it is not emitted.
    fields={}
    for line in b.read_safe(Path('E:/Codex-FastDDS-directdds-once-20261103_d5a29f38/control/data/pair')).decode('ascii').splitlines():
        key,value=line.split('=',1);fields[key]=value
    for side,r in j['targets'].items():
        b.need(r['side']==side and r['node_pid_parent_match_boot'] is True
          and r['no_signal_authority'] is True,'ACCEPTED_CONTEXT_REQUIRED')
        result[side]={k:int(r[src]) for k,src in [('pid','pid'),('parent','parent'),('birth','birth_proc_ticks'),
          ('uid','UID'),('euid','EUID'),('gid','GID'),('egid','EGID')]}
        result[side]['run']=int(fields['run'],16)
    b.need(set(result)=={'A','B'},'FIXED_TWO_CONTEXTS');return result


def source_buffers():
    buffers,old=state_buffers()
    oldroot=str(base.builder.PACKAGE).replace('\\','/');oldrun=str(base.builder.RUN).replace('\\','/')
    for name in buffers:
        text=buffers[name].decode('utf-8-sig').replace(oldroot,str(PACKAGE).replace('\\','/')).replace(oldrun,str(RUN).replace('\\','/'))
        text=text.replace(base.builder.SCOPE,SCOPE).replace(base.builder.NONCE,NONCE)
        text=text.replace('state.S1','read.B').replace('state.S2','read.A')
        buffers[name]=text.encode('utf-8')
    buffers.pop('state_samples.py')
    context=contexts()
    buffers['goal_read_context.py']=('"""Fixed private accepted context; no current liveness or signal grant."""\nEXPECTED='+repr(context)+'\n').encode('ascii')
    buffers['readonly_evidence.py']=b.read_safe(HERE/'readonly_evidence.py')
    text=buffers['goal_contract.py'].decode()
    d04=b.load_buffer(load_pinned_d04('goal_contract.py'),'_readonly_d04_contract',D04/'goal_contract.py')
    text=b.replace_assignment(text,'TARGETS',repr(d04.TARGETS))
    text=b.replace_assignment(text,'SOURCE_NAMES',repr(SOURCES))
    text=text.replace('client_ms=5000','client_ms=15000').replace('stream_capture=32768','stream_capture=65536')
    text=b.replace_function(text,'body',"""
def body(side):
 from readonly_evidence import query
 from goal_read_context import EXPECTED
 return query(side,EXPECTED[side])
""")
    text=b.replace_function(text,'build_registry',"""
def build_registry(commands):
 expected=[('server',(HDC,'-s',ENDPOINT,'-m'))]
 expected += [('read.'+side,(HDC,'-p','-s',ENDPOINT,'-t',TARGETS[side],'shell',body(side))) for side in ('B','A')]
 need(type(commands.get('commands')) is list and [(r.get('id'),tuple(r.get('argv',[]))) for r in commands['commands']]==expected,'EXACT_READONLY_COMMANDS')
 return {n:GoalStep(n,'listener' if n=='server' else 'readonly','' if n=='server' else n[-1],argv[1:],
  'server' if n=='server' else 'readonly',requires=() if n=='server' else ('server',) if n=='read.B' else ('server','read.B')) for n,argv in expected}
""")
    text=b.replace_function(text,'validate_registry',"""
def validate_registry(rows):
 commands={'commands':[{'id':'server','argv':[HDC,'-s',ENDPOINT,'-m']},
  *[{'id':'read.'+s,'argv':[HDC,'-p','-s',ENDPOINT,'-t',TARGETS[s],'shell',body(s)]} for s in ('B','A')]]}
 expected=build_registry(commands);need(rows==list(expected.values()),'REGISTRY_MUTATION');return expected
""")
    buffers['goal_contract.py']=text.encode()
    text=buffers['goal_authorization.py'].decode()
    effects={'hostkey','automatic_USB_auth_state','endpoint_readonly_misdelivery','opaque_external_Job_residual',
      'sampling_overshoot','sync_record_blocking','remote_shell_termination_unknown','controller_no_raw_stream_persistence',
      'fixed_D04_records_and_B_proc_bounded_read','server_auth_word_unknown_observation','sampled_path_open_race',
      'exe_link_not_read_image_unverified'}
    text=b.replace_assignment(text,'EFFECTS','{'+','.join(repr(s) for s in sorted(effects))+'}')
    text=b.replace_once(text,"grant.strict_state_samples=approval['effects']['strict_auth_context_unknown_stop']",
      "grant.fixed_readonly_evidence=approval['effects']['fixed_D04_records_and_B_proc_bounded_read']\n grant.scoped_server_keyword_observation=approval['effects']['server_auth_word_unknown_observation']")
    buffers['goal_authorization.py']=text.encode()
    # Keep the maintained shared host implementation and the already qualified
    # D04 original-handle primitives, including the terminal-only 50ms evidence.
    text=b.read_safe(HERE/'goal_windows_backend.py').decode('utf-8-sig')
    text=b.replace_once(text,"need(grant in _grants,'NO_ACTUAL_GRANT')", "need(grant in _grants,'NO_ACTUAL_GRANT')\n        need(type(self).__name__=='ReadonlyBackend','READONLY_BACKEND_REQUIRED')")
    buffers['goal_windows_backend.py']=text.encode()
    text=load_pinned_d04('goal_win32_owned.py').decode('utf-8-sig')
    text=b.replace_once(text,"self.captures.append(open(self.dest/(name+'.raw'),'xb'))",
      "from readonly_evidence import MemoryCapture\n   self.captures.append(MemoryCapture())")
    text=b.replace_once(text,"if self.case!='server':self.output[i].extend(data)",
      "# Borrowed content is consumed by Projection, never retained here.")
    text=b.replace_once(text,"self.closed=True;self.authority=False;self.receipt['signal_authority']=False",
      "self.closed=True;self.authority=False;self.receipt['signal_authority']=False\n  for buffer in self.output:buffer.clear()")
    text=b.replace_once(text,"'argv':[str(exe),*args]",
      "'argv':([str(exe),*args] if case=='server' else ['FIXED_READONLY_COMMAND_PIN'])")
    text=b.replace_once(text,"'argv':[str(self.exe),*self.args]",
      "'argv':([str(self.exe),*self.args] if self.case=='server' else ['FIXED_READONLY_COMMAND_PIN'])")
    buffers['goal_win32_owned.py']=text.encode()
    buffers['goal_handle_lifecycle.py']=b.read_safe(HERE/'goal_handle_lifecycle.py')
    for name in ('goal_driver.py','goal_entry.py'):
        buffers[name]=buffers[name].replace(b'from state_samples import actual_driver',b'from readonly_evidence import actual_driver')
    buffers['goal_workflow.py']=b'class FixedReadinessWorkflow:\n def __init__(self,driver):self.driver=driver\n def run(self):return self.driver.run_readonly()\n'
    text=buffers['goal_entry.py'].decode().replace("'max_state_samples':2,'catch_up':False,'raw_list_persisted':False",
      "'max_fixed_queries':2,'automatic_retry':False,'raw_stream_persisted':False")
    buffers['goal_entry.py']=text.encode()
    for name,data in buffers.items():ast.parse(data,filename=name)
    return buffers,old


def modules(buffers):
    """Temporary pure imports only. Never load backend/native/authorization."""
    names=('readonly_evidence','goal_read_context','goal_contract');saved={n:sys.modules.get(n) for n in names}
    try:
        for n in names:
            m=b.load_buffer(buffers[n+'.py'],n,ACTIVE/(n+'.py'));sys.modules[n]=m
        return sys.modules['goal_contract']
    finally:
        for n,v in saved.items():
            if v is None:sys.modules.pop(n,None)
            else:sys.modules[n]=v


def commands(buffers):
    saved={n:sys.modules.get(n) for n in ('readonly_evidence','goal_read_context')}
    try:
        for name in saved:sys.modules[name]=b.load_buffer(buffers[name+'.py'],name,ACTIVE/(name+'.py'))
        c=b.load_buffer(buffers['goal_contract.py'],'_readonly_contract',ACTIVE/'goal_contract.py')
        rows=[{'id':'server','argv':[c.HDC,'-s',c.ENDPOINT,'-m']}]
        rows +=[{'id':'read.'+s,'argv':[c.HDC,'-p','-s',c.ENDPOINT,'-t',c.TARGETS[s],'shell',c.body(s)]} for s in ('B','A')]
        registry=c.build_registry({'commands':rows});c.validate_registry(list(registry.values()))
        return c,rows,registry
    finally:
        for n,v in saved.items():
            if v is None:sys.modules.pop(n,None)
            else:sys.modules[n]=v


def assemble():
    b.need(not PACKAGE.exists() and not RUN.exists(),'NEW_OUTPUT_REQUIRED')
    buffers,_=source_buffers();c,rows,registry=commands(buffers)
    b.directory(ACTIVE);b.directory(PACKAGE/'prepare')
    for name,data in buffers.items():b.write_new(ACTIVE/name,data)
    b.write_new(ACTIVE/'COMMANDS_D01.json',b.encoded({'status':'PROPOSAL_ONLY_NOT_AUTHORITY','scope':SCOPE,'nonce':NONCE,'commands':rows}))
    tree=ast.parse(buffers['goal_authorization.py']);effects=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
    b.write_new(ACTIVE/'INPUT_SCHEMAS_D01.json',b.encoded({'scope':SCOPE,'nonce':NONCE,'actual_execution_authorized':False,'new_direct_user_approval_required':True,'effects':sorted(effects)}))
    source={'scope':SCOPE,'nonce':NONCE,'limits':c.LIMITS,'registry_sha256':c.registry_digest(list(registry.values())),
      'sources':[b.row(ACTIVE/n) for n in (*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')],'actual_execution_authorized':False}
    b.write_new(ACTIVE/'SOURCE_BINDING_D01.json',b.encoded(source))
    b.write_new(PACKAGE/'prepare/PROVENANCE_D01.json',b.encoded({'base_manifest_sha256':b.BASE_SHA,'D04_manifest_sha256':D04_MANIFEST,
      'HDC_calls':0,'real_raw_access':False,'controls_created':False,'exe_link_read':False,'source_bytes_cap':25600}))
    return source


def freeze(commit):
    b.need(__import__('re').fullmatch('[0-9a-f]{40}',commit) is not None,'COMMIT')
    b.need(not RUN.exists() and all(not (ACTIVE/n).exists() for n in b.CONTROLS),'NO_ACTUAL_CONTROLS')
    buffers,_=source_buffers()
    for n,data in buffers.items():b.need(b.read_safe(ACTIVE/n)==data,'SOURCE_DRIFT')
    for name in CODE_PATHS:
        p=subprocess.run(['git','show',commit+':'+name],cwd=REPO,capture_output=True)
        b.need(p.returncode==0 and p.stdout==b.read_safe(REPO/name),'PUBLISHED_BINDING')
    raw=b.read_safe(ACTIVE/'SOURCE_BINDING_D01.json');source=json.loads(raw);source_sha=b.sha(raw)
    b.need(source['sources']==[b.row(ACTIVE/n) for n in (*SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')],'SOURCE_ROWS')
    for name in ('FINAL_TEST_RESULT_D01.json','AGENTIC_REVIEW_CLOSURE_D04.json'):
        j=json.loads(b.read_safe(PACKAGE/'prepare'/name));b.need(j['source_binding_sha256']==source_sha,'EVIDENCE_BINDING')
        if name.startswith('FINAL'):b.need(j['native_exit']==j['outer_controller_exit']==0 and j['Commit gate']=='PASS','FINAL_FAILED')
        else:b.need(j['blocking_findings_open']==0,'REVIEW_OPEN')
    c,_,registry=commands(buffers)
    rows=[b.row(ACTIVE/n,'source.'+n) for n in SOURCES]
    rows +=[b.row(ACTIVE/n,role) for n,role in [('COMMANDS_D01.json','commands'),('INPUT_SCHEMAS_D01.json','schema'),('SOURCE_BINDING_D01.json','source_binding')]]
    for role,path,digest in [('tool.hdc',c.HDC,c.HDC_SHA),('tool.python','C:/Users/17715/anaconda3/python.exe','7679e53fa969789309e81fdad0d52b8cda5f83c9abf7cb31a3c58bf24b31e264')]:
        row=b.row(Path(path),role,6291456);b.need(row['sha256']==digest,'TOOL_DRIFT');rows.append(row)
    rows +=[b.row(PACKAGE/'prepare'/n,role) for n,role in [('FINAL_TEST_RESULT_D01.json','evidence.final'),('AGENTIC_REVIEW_CLOSURE_D04.json','evidence.review')]]
    total=sum(r['bytes'] for r in rows);b.need(total<c.LIMITS['active_bytes'],'ACTIVE_BUDGET')
    manifest={'format':'D04_FIXED_READONLY_PACKAGE1','scope':SCOPE,'nonce':NONCE,'limits':c.LIMITS,
      'registry_sha256':c.registry_digest(list(registry.values())),'active_pins':rows,'actual_execution_authorized':False,'published_source_commit':commit}
    mr=b.encoded(manifest);b.write_new(ACTIVE/'PACKAGE_FINAL_D01.json',mr)
    binding={'scope':SCOPE,'nonce':NONCE,'manifest_sha256':b.sha(mr),'registry_sha256':manifest['registry_sha256'],
      'run_root':str(RUN),'active_closure_bytes':total,'actual_execution_authorized':False,'full_executor_final':True,
      'published_source_commit':commit,'qualification':'OFFLINE_SYNTHETIC_NOT_DEVICE'}
    br=b.encoded(binding);b.write_new(ACTIVE/'FINAL_EXECUTION_BINDING_D01.json',br)
    card=b.read_safe(REPO/DOC).decode('utf-8')+'\nFixed scope: '+SCOPE+'\n\nNonce: '+NONCE+'\n\nManifest SHA256: '+b.sha(mr)+'\n\nBinding SHA256: '+b.sha(br)+'\n'
    b.write_new(PACKAGE/'EXECUTION_CARD_D01.md',card.encode('utf-8'))
    result={'scope':SCOPE,'nonce':NONCE,'card_sha256':b.sha(card.encode('utf-8')),'manifest_sha256':b.sha(mr),
      'binding_sha256':b.sha(br),'source_binding_sha256':source_sha,'published_source_commit':commit,'actual_execution_authorized':False}
    b.write_new(PACKAGE/'prepare/FINAL_HASHES_D01.json',b.encoded(result));return result


if __name__=='__main__':
    try:
        if sys.argv[1:]==['--assemble']:assemble();print('OFFLINE_READONLY_ASSEMBLED_HDC0');raise SystemExit(0)
        if len(sys.argv)==3 and sys.argv[1]=='--freeze':print(json.dumps(freeze(sys.argv[2])));raise SystemExit(0)
        print('OFFLINE_ONLY_NOT_GRANTED');raise SystemExit(78)
    except SystemExit:raise
    except BaseException:print('OFFLINE_READONLY_HOLD_PRESERVE_PARTIAL');raise SystemExit(1)
