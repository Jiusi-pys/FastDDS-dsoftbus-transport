"""Inert pre-runtime failure classification; no driver, grant or SDK call."""
import json,unittest
from pathlib import Path
from ros_task_scoped_tests import TaskScoped,REPO

class ReturnedPreparation(unittest.TestCase):
 sha=TaskScoped.sha
 save=TaskScoped.save
 def setUp(self):
  TaskScoped.setUp(self)
  self.w=self.root/'windows/unit';self.w.mkdir(parents=True)
  self.cfg=dict(scenario=self.m.ROS_SCENARIO,ros_attempt=17,ros_authorization_mode=self.m.ROS_TASK_AUTHORIZATION,run='a'*16,bounded_native_reaccept=dict(contract='BOUNDED_NORMAL_ONCE'),task_contract=dict(task_root=str(self.root),freeze_path=str(self.root/'DEVICE_CONTRACT.json')),executor_sha256={})
  self.f.update(bounded_native_reaccept=self.cfg['bounded_native_reaccept'],bounded_native_authorization=dict(sha256=self.m.ROS_NATIVE_GRANT_SHA256));self.save()
  self.cfg['task_contract']['freeze_sha256']=self.sha(self.root/'DEVICE_CONTRACT.json')
  source=REPO/'scripts/public_socket/shared_reattach/executor/runner-template.cjs'
  (self.root/'executor/runner-template.cjs').write_bytes(source.read_bytes());(self.w/'runner.cjs').write_bytes(source.read_bytes())
  self.cfg['executor_sha256']['runner-template.cjs']=self.sha(source)
  self.put('window.json',self.cfg);(self.w/'EXECUTION_STARTED').write_text('INERT_NO_DEVICE')
  self.put('window-summary.json',dict(first_failure='Runner failed rc=1',cleanup_failures=[],attempted_grants=['A','B']))
  (self.w/'runner-exit-code.txt').write_text('1')
  (self.w/'runner.stderr.log').write_text('Error: ROS_TASK_ACTUAL_WINDOW_LIMITS\n at verifyTaskGrant\n at readBoundPrivate\n at main\n')
  self.receipt='INDEPENDENT_GRANTED_PRE_DISPATCH_RESTORATION.json'
  for side in 'AB':
   (self.w/side).mkdir()
   for n in ['baseline.json','after.json','final-guard.json','ownership-guard.json','final-snapshot-guard.json']:(self.w/side/n).write_text('{}')
  paths=[self.w/n for n in ['window-summary.json','runner.stderr.log','runner-exit-code.txt','runner.cjs']]+[self.w/s/n for s in 'AB' for n in ['baseline.json','after.json','final-guard.json','ownership-guard.json','final-snapshot-guard.json']]
  for side in 'AB':
   for folder in ['metadata-before','metadata-after','processes-after']:
    (self.w/side/folder).mkdir()
    for name in ['command.json','stdout.log','stderr.log','exit-code.txt','remote-result.json']:
     p=self.w/side/folder/name;p.write_text('INERT_RAW_FIXTURE');paths.append(p)
  self.accept=dict(schema='NORMAL_RESTORATION_AFTER_GRANTED_PRE_DISPATCH_FAILURE',verdict='PASS',run=self.cfg['run'],window_sha256=self.sha(self.w/'window.json'),freeze_sha256=self.cfg['task_contract']['freeze_sha256'],runner_sha256=self.sha(self.w/'runner.cjs'),first_main_launch_not_reached=True,evidence_sha256={str(p):self.sha(p) for p in paths})
  self.put(self.receipt,self.accept)
  self.accept['independent_checks']={s:dict(permission_bytes_restored=True,security_metadata_restored=True,DENIED=True,process_absent=True) for s in 'AB'};self.put(self.receipt,self.accept)
  self.guards=[];self.m._require_window_guards_restored=lambda w:self.guards.append(w)
  self.m.appended_pregrant_recovery=lambda w:(_ for _ in ()).throw(ValueError('NOT_PREGRANT_FAILURE'))
 def put(self,n,v):(self.w/n).write_text(json.dumps(v))
 def check(self):return self.m.task_preparation_failures(self.root)
 def test_returned_after_grant_uses_original_normal_guards(self):
  rows=self.check();self.assertEqual(len(rows),1);self.assertEqual(self.guards,[self.w]);self.assertEqual(rows[0][4]['scope'],'NORMAL_RESTORATION_AFTER_GRANTED_PRE_DISPATCH_FAILURE')
 def test_runtime_or_dispatch_evidence_refused(self):
  for name in ['result.json','commands.json','A.raw.stdout.log']:
   with self.subTest(name=name):
    (self.w/name).write_text('{}');self.assertRaises(ValueError,self.check);(self.w/name).unlink()
 def test_changed_stderr_or_acceptance_refused(self):
  (self.w/'runner.stderr.log').write_text('OTHER_ERROR');self.assertRaises(ValueError,self.check)
 def test_other_native_failure_not_generic_recovery(self):
  self.put(self.receipt,dict(self.accept,first_main_launch_not_reached=False));self.assertRaises(ValueError,self.check)
 def test_ordinary_path_keeps_pregrant_contract(self):
  self.cfg.pop('bounded_native_reaccept');self.put('window.json',self.cfg);self.assertRaisesRegex(ValueError,'NOT_PREGRANT',self.check)
 def test_missing_raw_restoration_receipt_refused(self):
  self.accept['evidence_sha256'].pop(str(self.w/'A/metadata-before/stdout.log'));self.put(self.receipt,self.accept);self.assertRaisesRegex(ValueError,'evidence incomplete',self.check)

if __name__=='__main__':unittest.main()
