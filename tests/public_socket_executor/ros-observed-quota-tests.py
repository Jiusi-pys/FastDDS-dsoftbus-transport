"""Exact one-window policy through actual formal contract code; no driver/device."""
import importlib.util,json,shutil,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
AUTH='ONE_ROS2_DIAGNOSTIC_DEVICE_WINDOW_AND_SUCCESSFUL_LOCAL_COMMIT'
LIMITS=dict(canonical_windows=1,repair_retries=0,total_attempts=1,query_pairs_per_attempt=1,total_query_pairs=1,max_record_bytes=8192,per_side_writes=10,per_side_takes=10,stages=7)
class Quota(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);(self.root/'executor').mkdir()
  shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py',self.root/'executor/task_contract.py')
  (self.root/'PRIVATE_CONFIG.json').write_text(json.dumps(dict(targets={'A':'UNIT_A','B':'UNIT_B'},tokens={'A':'1','B':'2'})))
  spec=importlib.util.spec_from_file_location('observed_quota',self.root/'executor/task_contract.py');self.m=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.m)
  self.f=dict(scope=self.m.ROS_SCOPE,authorization=AUTH,limits=LIMITS,new_quota_id='ROS2_OBSERVED_20261002_1');self.save()
 def save(self):(self.root/'DEVICE_CONTRACT.json').write_text(json.dumps(self.f))
 def test_exact_new_policy(self):self.m.verify_ros_authorization(self.f)
 def test_current_policy_rejects_stale_six_stage_declaration(self):
  self.assertRaises(ValueError,self.m.verify_ros_authorization,dict(self.f,limits=dict(LIMITS,stages=6)))
 def test_claim_consumed_forbids_even_failed_second_attempt(self):
  ledger=self.root/'ros2/attempt-ledger';ledger.mkdir(parents=True);(ledger/'data-1.json').write_text('{}')
  with self.assertRaisesRegex(ValueError,'quota'):self.m.ros_next_attempt()
 def test_no_predecessor_or_retry_in_new_grant(self):
  self.f['previous_ros_data_attempt']={};self.save()
  with self.assertRaises(ValueError):self.m.verify_ros_authorization(self.f)
 def test_expanded_or_float_limit_is_rejected(self):
  for key,value in [('total_attempts',2),('repair_retries',1),('total_query_pairs',2),('total_attempts',1.0)]:
   with self.subTest(key=key,value=value):
    bad=dict(self.f,limits=dict(LIMITS,**{key:value}));self.assertRaises(ValueError,self.m.verify_ros_authorization,bad)
 def test_missing_new_quota_id_is_rejected(self):
  del self.f['new_quota_id'];self.assertRaises(ValueError,self.m.verify_ros_authorization,self.f)
 def test_legacy_contract_unchanged(self):self.m.verify_ros_authorization(dict(authorization=self.m.ROS_AUTHORIZATION,limits=self.m.ROS_LIMITS))
if __name__=='__main__':unittest.main()
