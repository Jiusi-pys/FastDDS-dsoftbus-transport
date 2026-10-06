"""Bounded forward recovery using actual command collector and typed UNIT seam."""
import importlib.util,json,subprocess,sys,tempfile,threading,unittest
import types
from pathlib import Path
from unittest.mock import patch
REPO=Path(__file__).resolve().parents[2]
SOURCE=REPO/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
s=importlib.util.spec_from_file_location('forward_recovery_under_test',SOURCE);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)

class Forward(unittest.TestCase):
 def facts(self,w):
  return dict(run='a'*16,window=str(w),side='A',target='UNIT_A',node='tcp:19001',recovery_tool_sha256=m.sha(SOURCE),recovery_tool_path=str(SOURCE),normal_guards_required=True)
 def scenario(self,case):
  with tempfile.TemporaryDirectory() as f:
   w=Path(f);(w/'A/debugger').mkdir(parents=True);cfg=dict(hdc_path='UNIT_NO_HDC',targets={'A':'UNIT_A'})
   (w/'window.json').write_text(json.dumps(cfg));calls=[]
   owned='UNIT_A tcp:19001 tcp:19001 [Forward]\n';foreign='UNIT_B tcp:19002 tcp:19002 [Forward]\n'
   before=owned+foreign
   if case=='wrongtarget':before=owned.replace('UNIT_A','FOREIGN')+foreign
   if case=='ambiguous':before+=owned.replace('UNIT_A','FOREIGN')
   if case=='reverse':before+=owned.replace('[Forward]','[Reverse]')
   if case=='unknown':before='UNKNOWN\n'
   if case=='missing':before=''
   original=m.Command
   class ActualChild(original):
    def __init__(self,root,label,argv,seconds,env=None):
     calls.append((label,argv))
     text=before if label=='forward-current' else (foreign if label=='forward-final' else 'Remove forward ruler success, ruler:tcp:19001 tcp:19001\n')
     if label=='forward-remove' and case=='removefail':text='[Fail]Remove failed\n'
     if label=='forward-final' and case=='foreignchanged':text=foreign.replace('19002','19003')
     # Command executes a real local Python child. The seam records the UNIT
     # dispatch shape separately; neither its argv nor result is device proof.
     super().__init__(root,label,[sys.executable,'-B','-c','import sys;sys.stdout.write('+repr(text)+')'],seconds,env)
   def guards(window):return None
   contract=type('Contract',(),{'_require_window_guards_restored':staticmethod(guards)})
   with patch.object(m,'failed_forward_facts',return_value=self.facts(w)),patch.object(m,'Command',ActualChild),patch.dict(sys.modules,{'task_contract':contract}):
    try:m.recover_forward_only(w,'A');error=None
    except Exception as e:error=e
   p=w/'A/debugger/recovery/attempt-1/DEBUGGER_FORWARD_RECOVERY.json'
   return error,calls,json.loads(p.read_text()) if p.exists() else None
 def test_success_removes_only_exact_owned_selector_and_preserves_foreign(self):
  error,calls,row=self.scenario('success');self.assertIsNone(error);self.assertTrue(row['foreign_rows_unchanged'])
  self.assertEqual([c[0] for c in calls],['forward-current','forward-remove','forward-final'])
  self.assertEqual(calls[1][1],['UNIT_NO_HDC','-t','UNIT_A','fport','rm','tcp:19001','tcp:19001'])
 def test_wrong_unknown_or_ambiguous_identity_never_removes(self):
  for case in ('wrongtarget','ambiguous','reverse','unknown','missing'):
   with self.subTest(case=case):
    error,calls,row=self.scenario(case);self.assertIsNotNone(error);self.assertEqual([c[0] for c in calls],['forward-current']);self.assertIsNone(row)
 def test_failed_remove_or_foreign_change_not_recovered(self):
  for case in ('removefail','foreignchanged'):
   error,calls,row=self.scenario(case);self.assertIsNotNone(error);self.assertIsNone(row)
 def test_historical_data7_original_shape_is_bound_and_not_closed(self):
  w=REPO/'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-8/windows/data-7'
  if not w.exists():self.skipTest('optional historical device receipt not distributed')
  for side in ('A','B'):
   facts=m.failed_forward_facts(w,side);self.assertFalse(facts['server_or_attach_started']);self.assertEqual(facts['observation_result'],'NOT_OBSERVED')
   self.assertFalse(json.loads((w/side/'debugger/RESULT.json').read_text())['support_closed'])
 def test_unavailable_normal_guards_reject_before_any_command(self):
  with tempfile.TemporaryDirectory() as f:
   w=Path(f);(w/'window.json').write_text(json.dumps(dict(hdc_path='UNIT',targets={'A':'UNIT_A'})))
   contract=type('Contract',(),{'_require_window_guards_restored':staticmethod(lambda _:(_ for _ in ()).throw(ValueError('GUARD_NOT_RESTORED')))})
   with patch.object(m,'failed_forward_facts',return_value=self.facts(w)),patch.object(m,'Command') as command,patch.dict(sys.modules,{'task_contract':contract}):
    self.assertRaises(ValueError,m.recover_forward_only,w,'A');command.assert_not_called()

 def verified_fixture(self,w):
  root=w/'A/debugger/recovery/attempt-1';root.mkdir(parents=True);facts=self.facts(w)
  (w/'window.json').write_text(json.dumps({'hdc_path':'UNIT_NO_HDC'}));(root/'ORIGINAL_FORWARD_FAILURE.json').write_text(json.dumps(facts))
  before='UNIT_A tcp:19001 tcp:19001 [Forward]\nUNIT_B tcp:19002 tcp:19002 [Forward]\n'
  entries={'forward-current':(['fport','ls'],before),'forward-remove':(['fport','rm','tcp:19001','tcp:19001'],'Remove forward ruler success, ruler:tcp:19001 tcp:19001\n'),'forward-final':(['fport','ls'],'UNIT_B tcp:19002 tcp:19002 [Forward]\n')}
  for label,(tail,text) in entries.items():
   argv=['UNIT_NO_HDC','-t','UNIT_A']+tail
   for suffix,data in [('intent',dict(argv=argv,cwd=str(root),purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS')),('started',dict(argv=argv,cwd=str(root),purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS',local_pid=987))]:
    (root/(label+'.'+suffix+'.json')).write_text(json.dumps(data))
   (root/(label+'.stdout')).write_text(text);(root/(label+'.stderr')).write_text('')
   row=dict(argv=argv,local_pid=987,exit_code=0,timed_out=False,capture_complete=True,overflow=False,termination=None,stdout_sha256=m.sha(root/(label+'.stdout')),stderr_sha256=m.sha(root/(label+'.stderr')))
   (root/(label+'.result.json')).write_text(json.dumps(row))
  receipt=dict(facts,decision='OWNED_FORWARD_REMOVED_NOT_BUSINESS_PASS',foreign_rows_unchanged=True,recovery_receipts={p.name:m.sha(p) for p in root.iterdir()})
  (root/'DEBUGGER_FORWARD_RECOVERY.json').write_text(json.dumps(receipt));review=w/'review.json'
  review.write_text(json.dumps(dict(scope='INDEPENDENT_DEBUGGER_FORWARD_RECOVERY_ACCEPTANCE',verdict='PASS',run=facts['run'],window=str(w),normal_guards_pass=True,recovery_tool_sha256=m.sha(SOURCE),sides={'A':dict(recovery_sha256=m.sha(root/'DEBUGGER_FORWARD_RECOVERY.json'),decision='ACCEPTED_OWNED_FORWARD_RECOVERY')})))
  (root/'INDEPENDENT_ACCEPTANCE.json').write_text(json.dumps(dict(decision='ACCEPTED_OWNED_FORWARD_RECOVERY',run=facts['run'],side='A',recovery_sha256=m.sha(root/'DEBUGGER_FORWARD_RECOVERY.json'),review_path=str(review),review_sha256=m.sha(review))))
  return root,facts,review

 def test_actual_verifier_replays_complete_receipts_and_rejects_unrecorded_action(self):
  with tempfile.TemporaryDirectory() as f:
   w=Path(f);root,facts,review=self.verified_fixture(w)
   with patch.object(m,'failed_forward_facts',return_value=facts):
    self.assertEqual(m.verify_forward_recovery(w,'A')['decision'],'OWNED_FORWARD_REMOVED_NOT_BUSINESS_PASS')
    (root/'unknown.intent.json').write_text('{}');self.assertRaises(ValueError,m.verify_forward_recovery,w,'A')
 def test_failed_or_foreign_review_cannot_be_rehashed_into_acceptance(self):
  for case in ('FAIL','foreignrun','wrongside'):
   with tempfile.TemporaryDirectory() as f:
    w=Path(f);root,facts,review=self.verified_fixture(w);data=json.loads(review.read_text())
    if case=='FAIL':data['verdict']='FAIL'
    elif case=='foreignrun':data['run']='b'*16
    else:data['sides']={'B':data['sides']['A']}
    review.write_text(json.dumps(data));p=root/'INDEPENDENT_ACCEPTANCE.json';row=json.loads(p.read_text());row['review_sha256']=m.sha(review);p.write_text(json.dumps(row))
    with patch.object(m,'failed_forward_facts',return_value=facts):self.assertRaises(ValueError,m.verify_forward_recovery,w,'A')

 def test_exact_historical_reader_keeps_as_recorded_relative_cwd(self):
  w=REPO/'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-8/windows/data-7'
  if not (w/'A/debugger/recovery/attempt-1/DEBUGGER_FORWARD_RECOVERY.json').is_file():self.skipTest('optional saved recovery not distributed')
  spec=types.SimpleNamespace(loader=types.SimpleNamespace(exec_module=lambda module:None))
  reader=types.SimpleNamespace(verify_forward_recovery=lambda window,side:(window,side))
  with patch.object(m.importlib.util,'spec_from_file_location',return_value=spec),patch.object(m.importlib.util,'module_from_spec',return_value=reader):
   argument,side=m.verify_bound_forward_recovery(w,'A')
   self.assertEqual(argument,w.relative_to(REPO));self.assertEqual(side,'A')
   with patch.object(m.Path,'cwd',return_value=REPO/'.artifacts'):
    self.assertRaises(ValueError,m.verify_bound_forward_recovery,w,'A')
 def test_future_recovery_resolves_window_before_any_action(self):
  with tempfile.TemporaryDirectory(dir=REPO/'build/ros2-target/tmp') as f:
   w=Path(f);(w/'window.json').write_text(json.dumps(dict(hdc_path='UNIT',targets={'A':'UNIT_A'})))
   seen=[]
   def facts(window,side):seen.append(window);return self.facts(window)
   contract=type('Contract',(),{'_require_window_guards_restored':staticmethod(lambda _:(_ for _ in ()).throw(ValueError('UNIT_NO_ACTION')))})
   with patch.object(m,'failed_forward_facts',side_effect=facts),patch.object(m,'Command') as command,patch.dict(sys.modules,{'task_contract':contract}):
    self.assertRaises(ValueError,m.recover_forward_only,w.relative_to(REPO),'A')
    self.assertEqual(seen,[w.resolve()]);command.assert_not_called()

if __name__=='__main__':unittest.main()
