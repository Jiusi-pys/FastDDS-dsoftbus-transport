"""Inert authority fixtures: no device queries, grants or execution."""
import copy,json,unittest
from ros_task_scoped_tests import TaskScoped
from v1_successor_tests import V1Successor

class NativeAuthority(unittest.TestCase):
 sha=TaskScoped.sha
 save=TaskScoped.save
 def setUp(self):
  V1Successor.setUp(self)
  self.m.repo_root=lambda:self.root
  self.window=self.root/'iterations/iteration-26/windows/data-16'
  self.window.mkdir(parents=True)
  receipt=self.window.parent.parent/'INDEPENDENT_V1_RAW_ACCEPTANCE.json'
  receipt.write_text(json.dumps(dict(run='e72e18d67cbc5b6d',verdict='PASS_IN_STATED_V1_DUAL_BOARD_SCOPE',V1_LOCATOR_DEVICE_VERIFIED=True,ROS2_V1_DUAL_BOARD_VERIFIED=True,ENVIRONMENT_RESTORATION='PASS')))
  self.record=self.root/self.m.ROS_NATIVE_GRANT_RELATIVE;self.record.parent.mkdir(parents=True)
  self.proof=dict(schema=1,authorization=self.m.ROS_NATIVE_AUTHORIZATION,work_package='A5C1_BOUNDED_NATIVE_REACCEPT_VALIDATION',base_commit='b4bd67d00f436beb107d9f324932b74c81641359',original_authorization_sha256=self.m.ROS_TASK_GRANT_SHA256,platform_permission_change=False,git_authorized=False,predecessor=dict(run='e72e18d67cbc5b6d',window=str(self.window),acceptance_sha256=self.sha(receipt)))
  self.record.write_text(json.dumps(self.proof));self.m.ROS_NATIVE_GRANT_SHA256=self.sha(self.record)
  binding=dict(path=str(self.record),sha256=self.sha(self.record))
  self.f.update(bounded_native_authorization=binding,native_name_contract='PACKAGE_SCOPED_V1',bounded_native_reaccept=dict(contract='BOUNDED_NORMAL_ONCE',order='outgoing-first',pre_ordinals=[1,2],post_ordinals=[3,4,5],pre_total=8,post_total=12,total=20,source='SDK_PUBLIC_SOCKET'))
  self.m.PRIVATE['bounded_native_authorization']=binding
  self.restored=[]
  self.m._require_normal_restored=lambda window:self.restored.append(window)
 def test_exact_supplement_and_restore_invocation(self):
  result=self.m.verify_v1_successor(self.f,self.grant)
  self.assertEqual(result['predecessor'],self.proof['predecessor'])
  self.assertEqual([p['run'] for p in result['authorized_predecessors']],['be8cb2d90f287223','e72e18d67cbc5b6d'])
  self.assertEqual(self.restored,[self.window])
 def test_scope_and_identity_expansion_rejected(self):
  for key,value in [('platform_permission_change',True),('git_authorized',True),('base_commit','0'*40),('authorization','OTHER')]:
   bad=dict(self.proof,**{key:value});self.record.write_text(json.dumps(bad))
   self.m.ROS_NATIVE_GRANT_SHA256=self.sha(self.record);self.f['bounded_native_authorization']['sha256']=self.sha(self.record)
   self.assertRaises(ValueError,self.m.verify_v1_successor,self.f,self.grant)
 def test_scene_expansion_rejected(self):
  for key,value in [('order','arbitrary'),('pre_ordinals',[1,2,3]),('total',40),('source','FAKE')]:
   bad=copy.deepcopy(self.f);bad['bounded_native_reaccept'][key]=value
   self.assertRaises(ValueError,self.m.verify_v1_successor,bad,self.grant)
 def test_name_and_window_contract_rejected(self):
  for key,value in [('native_name_contract','BARE_MD1'),('limits',dict(self.f['limits'],timeout_seconds=600))]:
   self.assertRaises(ValueError,self.m.verify_v1_successor,dict(self.f,**{key:value}),self.grant)
 def test_changed_predecessor_not_accepted(self):
  (self.window.parent.parent/'INDEPENDENT_V1_RAW_ACCEPTANCE.json').write_text('{}')
  self.assertRaises(ValueError,self.m.verify_v1_successor,self.f,self.grant)
 def test_omitted_native_window_rejected_before_dispatch(self):
  self.save()
  c=dict(work_package=self.m.ROS_SCOPE,main_member='ordinary_ros2_transport_consumer',task_contract=dict(freeze_path=str(self.root/'DEVICE_CONTRACT.json')))
  self.assertRaisesRegex(ValueError,'Native scene omitted',self.m.ros_shape,c)

if __name__=='__main__':unittest.main()
