"""Scoped continuation authority tests; inert local fixtures, no device driver."""
import copy, json, unittest
from ros_task_scoped_tests import TaskScoped, AUTH

class V1Successor(unittest.TestCase):
 sha=TaskScoped.sha
 save=TaskScoped.save
 def setUp(self):
  TaskScoped.setUp(self)
  self.window=self.root/'iterations/iteration-20/windows/data-15'
  self.window.mkdir(parents=True)
  (self.window/'window.json').write_text(json.dumps(dict(run='be8cb2d90f287223',targets=self.private['targets'],token=self.private['tokens'],package_name=self.private['ros2_package_name'])))
  accepted=self.window/'independent-tester/INDEPENDENT_ACCEPTANCE.json';accepted.parent.mkdir()
  accepted.write_text(json.dumps(dict(run='be8cb2d90f287223',functional_verdict='PASS_IN_STATED_DUAL_BOARD_ROS_SCOPE',environment_restoration_verdict='PASS')))
  self.record=self.root/'V1_SUCCESSOR_AUTHORIZATION_20261004.json'
  self.proof=dict(schema=1,authorization='PACKAGE_SCOPED_V1_SERVICE_NAME_REVISION_AND_TASK_CONTINUATION',work_package='V1_LOCATOR_DEVICE_ACCEPTANCE',original_authorization_sha256=self.sha(self.anchor),native_name_contract='PACKAGE_SCOPED_V1',profile='BYTES_NATIVE',same_registered_identity=True,same_platform_permission_scheme=True,historical_refund=False,fixed_attempt_balance=False,reaccept=False,production_qualification=False,predecessor=dict(window=str(self.window),run='be8cb2d90f287223',window_sha256=self.sha(self.window/'window.json'),independent_acceptance=dict(path=str(accepted),sha256=self.sha(accepted))))
  self.record.write_text(json.dumps(self.proof))
  self.m.ROS_V1_SUCCESSOR_GRANT_PATH=self.record
  self.m.ROS_V1_SUCCESSOR_GRANT_SHA256=self.sha(self.record)
  self.f['v1_successor_authorization']=dict(path=str(self.record),sha256=self.sha(self.record))
  self.m.PRIVATE['v1_successor_authorization']=self.f['v1_successor_authorization']
 def test_valid_specific_successor(self):
  self.assertEqual(self.m.verify_v1_successor(self.f,self.grant)['predecessor']['run'],'be8cb2d90f287223')
 def test_unbound_or_changed_record(self):
  bad=copy.deepcopy(self.f);bad['v1_successor_authorization']['sha256']='0'*64
  self.assertRaises(ValueError,self.m.verify_v1_successor,bad,self.grant)
  self.record.write_text('{}');self.assertRaises(ValueError,self.m.verify_v1_successor,self.f,self.grant)
 def test_previous_input_mutation(self):
  (self.window/'window.json').write_text('{}')
  self.assertRaises(ValueError,self.m.verify_v1_successor,self.f,self.grant)
 def test_old_contract_no_implicit_successor(self):
  self.assertIsNone(self.m.verify_v1_successor({},self.grant))
 def identity_fixture(self):
  source=self.root/'probes/softbus_record_probe.cpp';source.parent.mkdir();source.write_text('INERT_UNIT_SOURCE')
  image=self.root/'unit-helper';image.write_bytes(b'INERT_UNIT_ELF_FIXTURE')
  self.m.PRIVATE.update(repo_root=str(self.root),query_helper_path=str(image))
  self.f.update(identity_qualification=True,identity_query_helper=dict(path=str(image),sha256=self.sha(image),size=image.stat().st_size,source=dict(path='probes/softbus_record_probe.cpp',sha256=self.sha(source))))
  receipt=self.root/'compile.json';receipt.write_text(json.dumps(dict(schema='IDENTITY_HELPER_TARGET_COMPILE',scope='COMPILE_LINK_ONLY_NO_DEVICE',compile_exit=0,artifact=dict(path=str(image),bytes=image.stat().st_size,sha256=self.sha(image)),inputs=[dict(path=str(source),sha256=self.sha(source))])))
  self.f['identity_query_helper']['binding_receipt']=dict(path=str(receipt),sha256=self.sha(receipt))
  dep=self.root/'unit.d';dep.write_text('INERT_UNIT_DEPFILE')
  libraries=[]
  for index in range(3):
   library=self.root/('unit-library-'+str(index));library.write_bytes(b'INERT_UNIT_LIBRARY');libraries.append(dict(path=str(library),sha256=self.sha(library)))
  closure=self.root/'closure.json';closure.write_text(json.dumps(dict(schema='IDENTITY_HELPER_TARGET_COMPILE',scope='COMPILE_LINK_ONLY_NO_DEVICE',compile_link_exit=0,device_operations=0,artifact_correspondence=dict(same_bytes=True,original_binding_receipt_sha256=self.sha(receipt),original_artifact_sha256=self.sha(image),fresh_artifact_sha256=self.sha(image)),compiler=dict(path=str(libraries[0]['path']),sha256=libraries[0]['sha256']),direct_link_libraries=libraries,translation_units=[dict(depfile=str(dep),depfile_sha256=self.sha(dep),dependency_count=1,dependencies=[dict(path=str(source),sha256=self.sha(source))]) for _ in range(4)],closure=dict(translation_unit_count=4,all_dependency_entries=4,system_headers_included=True))))
  self.f['identity_query_helper']['dependency_closure']=dict(path=str(closure),sha256=self.sha(closure))
  remote=dict(A='/unit/A',B='/unit/B')
  return dict(identity_qualification=True,remote=remote,private_identity_output={s:remote[s]+'/local-udid.bin' for s in 'AB'},query_helper=dict(path=str(image),sha256=self.sha(image)),command_contract='--list-nodes --check-identities --identity-output <exact-window>/local-udid.bin --expected-token <board-token> --preflight-confirmed')
 def test_identity_helper_mode_and_content_binding(self):
  c=self.identity_fixture();self.m.verify_identity_query_binding(c,self.f)
  for field,value in [('identity_qualification',False),('identity_qualification','true'),('command_contract','--list-nodes')]:
   self.assertRaises(ValueError,self.m.verify_identity_query_binding,dict(c,**{field:value}),self.f)
  Path=__import__('pathlib').Path
  Path(c['query_helper']['path']).write_bytes(b'CHANGED')
  self.assertRaises(ValueError,self.m.verify_identity_query_binding,c,self.f)
 def test_identity_source_mutation_and_missing_successor(self):
  c=self.identity_fixture();bad=dict(self.f);bad.pop('v1_successor_authorization')
  self.assertRaises(ValueError,self.m.verify_identity_query_binding,c,bad)
  (self.root/'probes/softbus_record_probe.cpp').write_text('CHANGED')
  self.assertRaises(ValueError,self.m.verify_identity_query_binding,c,self.f)
 def test_transitive_sdk_header_mutation_rejected(self):
  c=self.identity_fixture();sdk=self.root/'unit-sdk-header.h';sdk.write_text('INERT_UNIT_SDK_HEADER')
  proof=self.f['identity_query_helper']['dependency_closure'];p=__import__('pathlib').Path(proof['path']);closure=json.loads(p.read_text())
  closure['translation_units'][0]['dependencies'].append(dict(path=str(sdk),sha256=self.sha(sdk)));closure['translation_units'][0]['dependency_count']=2;closure['closure']['all_dependency_entries']=5
  p.write_text(json.dumps(closure));proof['sha256']=self.sha(p)
  self.m.verify_identity_query_binding(c,self.f)
  sdk.write_text('CHANGED_SDK_CONTRACT');self.assertRaisesRegex(ValueError,'compiler dependency drift',self.m.verify_identity_query_binding,c,self.f)
 def ledger_fixture(self,with_new_pass=False,old_run='be8cb2d90f287223'):
  rows=[]
  for attempt,window,run in [(1,self.window,old_run)]+([(2,self.root/'iterations/iteration-22/windows/data-16','0123456789abcdef')] if with_new_pass else []):
   window.mkdir(parents=True,exist_ok=True);execution=window.parent.parent
   original=copy.deepcopy(self.f);original.update(task_root=str(execution));original.pop('v1_successor_authorization',None)
   freeze=execution/'DEVICE_CONTRACT.json';freeze.write_text(json.dumps(original))
   cfg=dict(run=run,ros_attempt=attempt,targets=self.private['targets'],token=self.private['tokens'],package_name=self.private['ros2_package_name'],ros_task_id=self.grant['task_id'],window_root=str(window),task_contract=dict(task_root=str(execution),freeze_path=str(freeze),freeze_sha256=self.sha(freeze)))
   (window/'window.json').write_text(json.dumps(cfg));(window/'EXECUTION_STARTED').write_text('INERT_UNIT_EXECUTION')
   (window/'window-summary.json').write_text('{}')
   row=dict(scope=self.m.ROS_SCOPE,attempt=attempt,run=run,window=str(window),task_id=self.grant['task_id'],kind='DATA_DISPATCH_INTENT',side='A',simulated=False,authorization=AUTH,authorization_record_sha256=self.m.ROS_TASK_GRANT_SHA256)
   claim=window/'DATA_ATTEMPT_STARTED';claim.write_text(json.dumps(row));rows.append((attempt,'data',claim,row))
  self.proof['predecessor']['window_sha256']=self.sha(self.window/'window.json')
  self.record.write_text(json.dumps(self.proof));self.m.ROS_V1_SUCCESSOR_GRANT_SHA256=self.sha(self.record)
  self.f['v1_successor_authorization']['sha256']=self.sha(self.record);self.save()
  self.m.ros_task_claim_rows=lambda:rows
  self.m.task_preparation_failures=lambda *args:[]
  # These test-only outcomes isolate admission transitions. Real restoration
  # and acceptance checks are exercised separately by canonical executor tests.
  self.m._require_normal_restored=lambda *args:None
  self.m.ros_task_data_outcome=lambda *args:dict(status='PASS',evidence={},first_failure=None)
 def test_exact_old_pass_permits_next_attempt(self):
  self.ledger_fixture();self.assertEqual(self.m.ros_task_next_attempt(),2)
 def test_new_v1_pass_remains_terminal(self):
  self.ledger_fixture(with_new_pass=True)
  self.assertRaisesRegex(ValueError,'Successful task data window ends',self.m.ros_task_next_attempt)
 def test_other_pass_cannot_use_exception(self):
  self.ledger_fixture()
  rows=self.m.ros_task_claim_rows();row=rows[0][3];row['run']='aaaaaaaaaaaaaaaa'
  cfg=json.loads((self.window/'window.json').read_text());cfg['run']=row['run'];(self.window/'window.json').write_text(json.dumps(cfg))
  (self.window/'DATA_ATTEMPT_STARTED').write_text(json.dumps(row))
  # Exact predecessor verification rejects the altered input before dispatch.
  self.assertRaises(ValueError,self.m.ros_task_next_attempt)
 def test_missing_successor_keeps_old_pass_terminal(self):
  self.ledger_fixture();self.f.pop('v1_successor_authorization');self.save()
  self.assertRaisesRegex(ValueError,'Successful task data window ends',self.m.ros_task_next_attempt)
 def test_changed_successor_cannot_reopen_old_pass(self):
  self.ledger_fixture();self.record.write_text('{}')
  self.assertRaises(ValueError,self.m.ros_task_next_attempt)

if __name__=='__main__':unittest.main()
