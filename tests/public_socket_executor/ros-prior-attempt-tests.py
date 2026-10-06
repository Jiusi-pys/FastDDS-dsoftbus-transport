"""Read-only real predecessor evidence; isolated contract units, no driver/admission."""
import hashlib,importlib.util,json,os,shutil,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
REPO=Path(__file__).resolve().parents[2]
OLD=REPO/'.artifacts/ros2-device-acceptance/exec-supp-query-20261001'
SOURCE=Path(os.environ.get('ROS_PRIOR_SOURCE',str(REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py')))
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
@unittest.skipUnless((OLD/'ros2/attempt-ledger/data-1.json').exists(),'External predecessor evidence NOT_RUN when absent')
class Prior(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(prefix='local-prior-unit-',dir=OLD.parent);self.addCleanup(self.tmp.cleanup)
  self.root=Path(self.tmp.name);(self.root/'executor').mkdir();shutil.copyfile(SOURCE,self.root/'executor/task_contract.py');shutil.copyfile(OLD/'PRIVATE_CONFIG.json',self.root/'PRIVATE_CONFIG.json')
  spec=importlib.util.spec_from_file_location('prior_unit',self.root/'executor/task_contract.py');self.m=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.m)
  claim=OLD/'ros2/attempt-ledger/data-1.json';window=Path(json.loads(claim.read_text())['window'])
  self.f=dict(scope=self.m.ROS_SCOPE,original_data_quota_id='ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE_CANONICAL_1',previous_ros_data_attempt=dict(contract_path=str(OLD/'DEVICE_CONTRACT.json'),contract_sha256=sha(OLD/'DEVICE_CONTRACT.json'),claim_sha256=sha(claim),receipts={n:sha(window/n) for n in ('window.json','window-summary.json','result.json','DATA_ATTEMPT_STARTED')}))
  self.save()
 def save(self):(self.root/'DEVICE_CONTRACT.json').write_text(json.dumps(self.f))
 def test_failed_restored_predecessor_means_attempt_two(self):self.assertEqual(self.m.ros_next_attempt(),2)
 def test_wrong_quota_and_hashes_block(self):
  for key in ('contract_sha256','claim_sha256'):
   original=self.f['previous_ros_data_attempt'][key];self.f['previous_ros_data_attempt'][key]='0'*64;self.save();self.assertRaises(ValueError,self.m.ros_next_attempt);self.f['previous_ros_data_attempt'][key]=original
  self.f['original_data_quota_id']='OTHER';self.save();self.assertRaises(ValueError,self.m.ros_next_attempt)
 def test_receipt_tamper_or_missing_blocks(self):
  self.f['previous_ros_data_attempt']['receipts']['result.json']='0'*64;self.save();self.assertRaises(ValueError,self.m.ros_next_attempt)
  del self.f['previous_ros_data_attempt']['receipts']['result.json'];self.save();self.assertRaises(ValueError,self.m.ros_next_attempt)
 def test_local_or_unrestored_predecessor_blocks(self):
  self.f['local_contract_test']=True;self.save();self.assertRaises(ValueError,self.m.ros_next_attempt)
  del self.f['local_contract_test'];self.save()
  with patch.object(self.m,'_require_normal_restored',side_effect=ValueError('TEST_RECOVERY_BLOCKED')):self.assertRaises(ValueError,self.m.ros_next_attempt)
 def test_consumed_repair_cannot_start_third(self):
  ledger=self.root/'ros2/attempt-ledger';ledger.mkdir(parents=True);(ledger/'data-2.json').write_text('{}');self.assertRaises(ValueError,self.m.ros_next_attempt)
if __name__=='__main__':unittest.main()
