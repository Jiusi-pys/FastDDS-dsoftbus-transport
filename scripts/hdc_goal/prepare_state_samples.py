"""Offline fixed two-time list-state package; no actual controls or HDC calls."""
import ast
import importlib.util
from pathlib import Path
import sys

_path=Path(__file__).with_name('prepare_probe_pair.py')
_spec=importlib.util.spec_from_file_location('_state_private_pair_builder',_path)
builder=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(builder)
builder.PACKAGE=builder.REPO/'build/hdc-state-samples-package-20261103-d01'
builder.ACTIVE=builder.PACKAGE/'active'
builder.RUN=builder.REPO/'build/hdc-state-samples-once-20261103_c74e205b'
builder.SCOPE='ONE_NEW_FIXED_A_STATE_SAMPLES_STRICT_AUTH_60S_D01'
builder.NONCE='C74E205B_STATE_SAMPLES_60S_D01'
builder.CARD_PATH='docs/HDC_STATE_SAMPLES_EXECUTION.md'
builder.PACKAGE_FORMAT='FIXED_STATE_SAMPLES_PACKAGE1'
_pair_sources=builder.SOURCES
builder.SOURCES=(*builder.ORIGINAL,'state_samples.py')
builder.CODE_PATHS=[*builder.CODE_PATHS,'scripts/hdc_goal/state_samples.py',
 'scripts/hdc_goal/prepare_state_samples.py','tests/test_hdc_state_samples.py',builder.CARD_PATH]
_pair_source_buffers=builder.source_buffers


def source_buffers():
    builder.SOURCES=_pair_sources
    try:buffers,old=_pair_source_buffers()
    finally:builder.SOURCES=(*builder.ORIGINAL,'state_samples.py')
    buffers.pop('probe_pair.py')
    for name in buffers:
        buffers[name]=buffers[name].replace(b'probe.A1',b'state.S1').replace(b'probe.A2',b'state.S2')
    text=buffers['goal_contract.py'].decode('utf-8')
    text=builder.replace_assignment(text,'SOURCE_NAMES',repr(builder.SOURCES))
    text=text.replace('client_ms=15000','client_ms=5000')
    text=builder.replace_function(text,'build_registry',"""
def build_registry(commands):
 expected=[('server',(HDC,'-s',ENDPOINT,'-m'))]
 expected += [(n,(HDC,'-p','-s',ENDPOINT,'list','targets','-v')) for n in ('state.S1','state.S2')]
 rows=commands.get('commands',[])
 need(type(rows) is list and [(r.get('id'),tuple(r.get('argv',[]))) for r in rows]==expected,'EXACT_STATE_COMMANDS')
 return {n:GoalStep(n,'listener' if n=='server' else 'state',
  '' if n=='server' else 'A1' if n=='state.S1' else 'A2',argv[1:],
  'server' if n=='server' else 'state',requires=() if n=='server' else ('server',)) for n,argv in expected}
""")
    text=builder.replace_function(text,'validate_registry',"""
def validate_registry(rows):
 expected=build_registry({'commands':[{'id':'server','argv':[HDC,'-s',ENDPOINT,'-m']},
  *[{'id':n,'argv':[HDC,'-p','-s',ENDPOINT,'list','targets','-v']} for n in ('state.S1','state.S2')]]})
 need(rows==list(expected.values()),'REGISTRY_MUTATION');return dict(expected)
""")
    # There is no remote command body in a state-only scope.
    text=builder.replace_function(text,'body',"def body(side):\n raise ContractError('NO_SHELL_BODY_IN_STATE_SCOPE')")
    buffers['goal_contract.py']=text.encode('utf-8')
    old.LIMITS=dict(old.LIMITS,client_ms=5000) # explicit narrower query budget only
    text=buffers['goal_authorization.py'].decode('utf-8');tree=ast.parse(text)
    effects=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign)
                and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
    effects-= {'first_exact_pair_collection','one_conditional_second_A_sample',
               'new_remote_printf_commands','remote_shell_termination_unknown'}
    effects|= {'two_preplanned_list_state_samples','strict_auth_context_unknown_stop','controller_no_raw_stream_persistence'}
    text=builder.replace_assignment(text,'EFFECTS','{'+','.join(repr(s) for s in sorted(effects))+'}')
    text=builder.replace_once(text,"grant.server_unknown_continue=False\n grant.pair_collection_continue=approval['effects']['first_exact_pair_collection']\n grant.pair_second_sample=approval['effects']['one_conditional_second_A_sample']",
        "grant.server_unknown_continue=False\n grant.strict_state_samples=approval['effects']['strict_auth_context_unknown_stop']")
    buffers['goal_authorization.py']=text.encode('utf-8')
    for name in ('goal_driver.py','goal_entry.py'):
        buffers[name]=buffers[name].replace(b'from probe_pair import actual_driver',b'from state_samples import actual_driver')
    text=buffers['goal_windows_backend.py'].decode('utf-8')
    buffers['goal_windows_backend.py']=builder.replace_once(text,"type(self).__name__=='PairBackend'","type(self).__name__=='StateBackend'").encode('utf-8')
    text=buffers['goal_win32_owned.py'].decode('utf-8')
    text=builder.replace_once(text,"self.captures.append(open(self.dest/(name+'.raw'),'xb'))",
        "from state_samples import MemoryCapture\n   self.captures.append(MemoryCapture())")
    text=builder.replace_once(text,"if self.case!='server':self.output[i].extend(data)",
        "# State projection consumes the borrowed bytes; never retain the list.")
    text=builder.replace_once(text,"self.receipt['create_call_before_ns']=safe_clock()",
        "from state_samples import check_creation_slot\n  check_creation_slot(self.goal_permit)\n  self.receipt['create_call_before_ns']=safe_clock()")
    text=builder.replace_once(text,"self.closed=True;self.authority=False;self.receipt['signal_authority']=False",
        "self.closed=True;self.authority=False;self.receipt['signal_authority']=False\n  for buffer in self.output:buffer.clear()")
    buffers['goal_win32_owned.py']=text.encode('utf-8')
    buffers['goal_storage.py']=buffers['goal_storage.py'].replace(b",'stdout.raw','stderr.raw'",b'')
    buffers['goal_evidence.py']=b"from goal_contract import need\ndef evaluate_step(*args):raise RuntimeError('STATE_BACKEND_REQUIRED')\ndef rejection_record(*args):return {'reason':'STATE_EVIDENCE_REJECTED'}\n"
    buffers['goal_workflow.py']=b'class FixedReadinessWorkflow:\n def __init__(self,driver):self.driver=driver\n def run(self):return self.driver.run_samples()\n'
    text=buffers['goal_entry.py'].decode('utf-8')
    text=text.replace("'max_samples':2,'automatic_retry_loop':False,'first_failure_kept':True",
        "'max_state_samples':2,'catch_up':False,'raw_list_persisted':False")
    buffers['goal_entry.py']=text.encode('utf-8')
    buffers['state_samples.py']=builder.read_safe(builder.REPO/'scripts/hdc_goal/state_samples.py')
    for name,data in buffers.items():ast.parse(data,filename=name)
    return buffers,old


def commands(contract):
    return {'status':'PROPOSAL_ONLY_NOT_AUTHORITY','scope':builder.SCOPE,'nonce':builder.NONCE,'commands':[
        {'id':'server','argv':[contract.HDC,'-s',contract.ENDPOINT,'-m']},
        *[{'id':n,'argv':[contract.HDC,'-p','-s',contract.ENDPOINT,'list','targets','-v']} for n in ('state.S1','state.S2')]]}


builder.source_buffers=source_buffers
builder.commands=commands
if __name__=='__main__':raise SystemExit(builder.main(sys.argv[1:]))
