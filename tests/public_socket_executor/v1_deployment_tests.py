"""Synthetic controlled-input V1 configuration checks; no device driver."""
import copy,hashlib,importlib.util,json,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('v1_private',REPO/'scripts/public_socket/shared_reattach/executor/private_config.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Deployment(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(dir=REPO/'build');self.addCleanup(self.temp.cleanup);self.repo=Path(self.temp.name);folder=self.repo/'.artifacts/unit';folder.mkdir(parents=True)
  self.x=dict(ros2_package_name='unit.project',ros2_service_prefix='approved')
  v=dict(namespace='unit-v1',domain=201,profile='BYTES_NATIVE',slots=dict(A=0,B=0),udids=dict(A='synthetic-udid-A',B='synthetic-udid-B'),udid_receipts={},initiator_names=dict(A='unit.project.approved.old.0.outgoing',B='unit.project.approved.old.1.outgoing'))
  for side in 'AB':
   p=folder/(side+'-identity.bin');p.write_bytes(v['udids'][side].encode());v['udid_receipts'][side]=dict(path=str(p),sha256=self.sha(p))
  p=folder/'approved-names.json';p.write_text(json.dumps(dict(names=v['initiator_names'],decision='TEMPORARILY_RETAIN_EXACT_PREVIOUSLY_APPROVED_NAMES',package_name=self.x['ros2_package_name'],service_stem=self.x['ros2_service_prefix'])));v['initiator_receipt']=dict(path=str(p),sha256=self.sha(p));self.x['v1_deployment']=v
 def sha(self,p):return hashlib.sha256(p.read_bytes()).hexdigest()
 def test_exact_raw_inputs_and_approved_names(self):self.assertEqual(m.validate_v1_deployment(self.x,self.repo),self.x['v1_deployment'])
 def test_no_package_prefix_in_logical_namespace(self):
  v=m.validate_v1_deployment(self.x,self.repo);self.assertEqual(v['namespace'],'unit-v1');self.assertEqual(v['udids']['A'],'synthetic-udid-A')
 def test_missing_wrong_profile_slot_peer_rejected(self):
  for field,value in [('profile','RAW_PLAIN'),('slots',dict(A=0,B=1)),('slots',dict(A=True,B=0)),('domain',202),('udids',dict(A='synthetic-udid-A'))]:
   x=copy.deepcopy(self.x);x['v1_deployment'][field]=value;self.assertRaises(ValueError,m.validate_v1_deployment,x,self.repo)
  x=copy.deepcopy(self.x);x['v1_deployment'].pop('initiator_receipt');self.assertRaises(ValueError,m.validate_v1_deployment,x,self.repo)
 def test_normalized_or_synthetic_replacement_cannot_claim_raw_sdk_input(self):
  x=copy.deepcopy(self.x);x['v1_deployment']['udids']['A']='synthetic-udid-a';self.assertRaises(ValueError,m.validate_v1_deployment,x,self.repo)
 def test_invented_initiator_or_wrong_package_rejected(self):
  x=copy.deepcopy(self.x);x['v1_deployment']['initiator_names']['A']='unit.project.approved.NEW.0.outgoing';self.assertRaises(ValueError,m.validate_v1_deployment,x,self.repo)
  x=copy.deepcopy(self.x);x['ros2_package_name']='other.package';self.assertRaises(ValueError,m.validate_v1_deployment,x,self.repo)
 def test_private_source_hash_mutation(self):
  Path(self.x['v1_deployment']['udid_receipts']['B']['path']).write_bytes(b'OTHER_UNIT_INPUT');self.assertRaises(ValueError,m.validate_v1_deployment,self.x,self.repo)
if __name__=='__main__':unittest.main()
