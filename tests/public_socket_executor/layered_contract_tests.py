"""Fixed-slot grant/claim tests only, never device authority or dispatch."""
import importlib.util,json,hashlib,tempfile,unittest
from pathlib import Path
from types import SimpleNamespace
FILE=Path(__file__).resolve().parents[2]/'scripts/public_socket/shared_reattach/executor/layered_contract.py'
s=importlib.util.spec_from_file_location('tested_layered',FILE);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x));return p
def need(v,msg):
 if not v:raise ValueError(msg)
class Contracts(unittest.TestCase):
 def setUp(self):
  self.t=tempfile.TemporaryDirectory();self.root=Path(self.t.name);self.dds=self.root/'dds';self.ros=self.root/'ros';self.dds.mkdir();self.ros.mkdir();old=put(self.root/'historical.json',{'balance':0,'immutable':True})
  self.g=dict(authorization=m.AUTH,human_approved=True,quota_id='SYNTHETIC_UNIT_NOT_DEVICE',limits={k:dict(query_pair=1,data_attempt=1,repair_retry=0) for k in ('DDS214','ROS2')},task_roots={'DDS214':str(self.dds),'ROS2':str(self.ros)},targets={'A':'UNIT_A','B':'UNIT_B'},refund=False,historical_ledger=dict(path=str(old),sha256=sha(old)))
  self.p=put(self.root/'grant.json',self.g);self.f=dict(authorization=m.AUTH,layered_authorization=dict(path=str(self.p),sha256=sha(self.p)),layered_quota_id=self.g['quota_id'],device_authorized=True)
  self.c=SimpleNamespace(TASK_ROOT=self.dds,TARGETS=self.g['targets'],PRIVATE={'repo_root':str(self.root)},need=need,digest=sha)
  # Synthetic isolated authority anchor only in this in-memory test module.
  m.APPROVED_GRANT_RELATIVE='grant.json';m.APPROVED_GRANT_SHA256=sha(self.p)
 def tearDown(self):self.t.cleanup()
 def change(self):put(self.p,self.g);self.f['layered_authorization']['sha256']=sha(self.p)
 def test_exact_fixed_grant(self):self.assertEqual(m.grant(self.c,self.f,'DDS214'),self.g)
 def test_utf8_human_approval_is_read_without_locale_dependency(self):
  self.g['approval_answer']='批准上述有限分层设备验证';self.p.write_text(json.dumps(self.g,ensure_ascii=False),encoding='utf-8')
  m.APPROVED_GRANT_SHA256=sha(self.p);self.f['layered_authorization']['sha256']=sha(self.p)
  self.assertEqual(m.grant(self.c,self.f,'DDS214')['approval_answer'],self.g['approval_answer'])
 def test_no_expansion_or_bool_quantity(self):
  for value in (2,True,1.0):
   self.g['limits']['DDS214']['data_attempt']=value;self.change()
   with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
 def test_wrong_target_root_or_refund(self):
  self.c.TARGETS={'A':'WRONG','B':'UNIT_B'}
  with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
  self.c.TARGETS=self.g['targets'];self.c.TASK_ROOT=self.root
  with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
  self.c.TASK_ROOT=self.dds;self.g['refund']=True;self.change()
  with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
 def test_local_contract_cannot_claim_device_grant(self):
  self.f['local_contract_test']=True
  with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
 def test_ros_requires_actual_acceptance(self):
  self.c.TASK_ROOT=self.ros;self.f['authorization']=m.ROS_AUTH;self.f['dds214_acceptance']={'path':str(self.dds/'DDS214_ACCEPTANCE.json'),'sha256':'0'*64}
  with self.assertRaises((ValueError,OSError)):m.grant(self.c,self.f,'ROS2')
 def test_history_drift_is_not_refunded(self):
  Path(self.g['historical_ledger']['path']).write_text('changed')
  with self.assertRaises(ValueError):m.grant(self.c,self.f,'DDS214')
 def test_self_consistent_new_root_cannot_add_slot(self):
  clone=self.root/'second-task';clone.mkdir();self.g['task_roots']['DDS214']=str(clone);self.change();self.c.TASK_ROOT=clone
  with self.assertRaisesRegex(ValueError,'anchor'):m.grant(self.c,self.f,'DDS214')
 def test_same_claim_not_recreated(self):
  self.c.verify_query_context=lambda x:None
  frozen=put(self.dds/'freeze.json',{'materialization_sha256':'a'*64,'build_binding':{'source_state_sha256':'b'*64},'local_contract_test':True})
  x=dict(run='1234567890abcdef',window_root=str(self.dds/'windows/q'),targets=self.c.TARGETS,task_contract=dict(freeze_path=str(frozen),freeze_sha256=sha(frozen)),hdc_path='UNIT_ONLY_NOT_EXECUTABLE')
  Path(x['window_root']).mkdir(parents=True);m.claim(self.c,x);m.completed_query(self.c,x)
  with self.assertRaises(FileExistsError):m.claim(self.c,x)
  wrong=dict(x,run='2222222222222222')
  with self.assertRaises(ValueError):m.dispatch(self.c,wrong,'A')
  self.assertFalse((Path(x['window_root'])/'QUERY_DISPATCH_INTENT_A.json').exists())
  m.dispatch(self.c,x,'A')
  with self.assertRaises(FileExistsError):m.dispatch(self.c,x,'A')
  m.completed_query(self.c,x) # Completed claim remains readable, never reacquired.
  original=json.loads(m.path(self.c,'query').read_text());tampered=dict(original,targets={'A':'WRONG','B':'UNIT_B'});put(m.path(self.c,'query'),tampered)
  with self.assertRaises(ValueError):m.completed_query(self.c,x)
if __name__=='__main__':unittest.main()
