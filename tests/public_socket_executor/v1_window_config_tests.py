"""Actual admission of generated V1 config; synthetic identities, no devices."""
import copy,hashlib,importlib.util,json,shutil,unittest
from pathlib import Path
from ros_task_scoped_tests import TaskScoped,REPO
class WindowConfig(unittest.TestCase):
 save=TaskScoped.save
 def setUp(self):
  TaskScoped.setUp(self)
  shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/private_config.py',self.root/'executor/private_config.py')
  self.m.PRIVATE.update(repo_root=str(self.root),ros2_package_name='unit.project',ros2_service_prefix='approved')
  d=self.root/'.artifacts/inputs';d.mkdir(parents=True)
  v=dict(namespace='unit-v1',domain=201,profile='BYTES_NATIVE',slots=dict(A=0,B=0),udids=dict(A='synthetic-A',B='synthetic-B'),udid_receipts={},initiator_names=dict(A='unit.project.approved.old.0.outgoing',B='unit.project.approved.old.1.outgoing'))
  for side in 'AB':
   p=d/(side+'.bin');p.write_bytes(v['udids'][side].encode());v['udid_receipts'][side]=dict(path=str(p),sha256=self.sha(p))
  p=d/'names.json';p.write_text(json.dumps(dict(names=v['initiator_names'],decision='TEMPORARILY_RETAIN_EXACT_PREVIOUSLY_APPROVED_NAMES',package_name='unit.project',service_stem='approved')));v['initiator_receipt']=dict(path=str(p),sha256=self.sha(p))
  self.m.PRIVATE['v1_deployment']=v
  analyzer=self.root/'scripts/public_socket/analyze_v1_locators.py';analyzer.parent.mkdir(parents=True);shutil.copyfile(REPO/'scripts/public_socket/analyze_v1_locators.py',analyzer)
  self.c=dict(v1_deployment=copy.deepcopy(v),package_name='unit.project',service_prefix='approved',window_root=str(self.root/'windows/data'),v1_locator_analyzer=dict(path=str(analyzer),sha256=self.sha(analyzer)))
  self.f=dict(v1_successor_authorization={'unit':True},v1_locator_analyzer=self.c['v1_locator_analyzer'])
 def sha(self,p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
 def test_exact_private_binding_before_generation(self):self.m.verify_v1_window(self.c,self.f)
 def test_missing_changed_or_foreign_endpoint_rejected(self):
  for key,value in [('v1_deployment',None),('package_name','other.package'),('service_prefix','other')]:
   self.assertRaises(ValueError,self.m.verify_v1_window,dict(self.c,**{key:value}),self.f)
  c=copy.deepcopy(self.c);c['v1_deployment']['udids']['B']='synthetic-OTHER';self.assertRaises(ValueError,self.m.verify_v1_window,c,self.f)
 def config(self,side):
  v=self.c['v1_deployment'];other='B' if side=='A' else 'A'
  return dict(locator_version='V1',native_name_contract='PACKAGE_SCOPED_V1',namespace=v['namespace'],domain='201',profile='BYTES_NATIVE',local_udid=v['udids'][side],peer_udid=v['udids'][other],service_slot='0',peer_slot='0',local_initiator_name=v['initiator_names'][side],peer_initiator_name=v['initiator_names'][other],authorized_package='unit.project',service_prefix='unit.project.approved')
 def put(self,side,values):
  p=Path(self.c['window_root'])/side/'aux/ros.conf';p.parent.mkdir(parents=True,exist_ok=True);p.write_text(''.join(k+'='+v+'\n' for k,v in values.items()));return p
 def test_materialized_configs_and_mutations(self):
  for s in 'AB':self.put(s,self.config(s))
  self.m.verify_v1_window(self.c,self.f)
  for key,value in [('locator_version','V0'),('namespace','changed'),('native_name_contract','UNSPECIFIED'),('peer_udid','synthetic-A'),('service_prefix','unit.project.unit.project.approved'),('profile','RAW_PLAIN'),('peer_slot','1'),('peer_initiator_name','unit.project.approved.invented')]:
   values=self.config('A');values[key]=value;self.put('A',values);self.assertRaises(ValueError,self.m.verify_v1_window,self.c,self.f)
  self.put('A',self.config('A'));p=self.put('B',self.config('B'));p.write_text(p.read_text()+'peer_slot=0\n');self.assertRaises(ValueError,self.m.verify_v1_window,self.c,self.f)
 def test_analyzer_must_match_frozen_canonical_source(self):
  bad=copy.deepcopy(self.c);bad['v1_locator_analyzer']['sha256']='0'*64;self.assertRaises(ValueError,self.m.verify_v1_window,bad,self.f)
 def test_corresponding_accepted_board_exports_required(self):
  recovery=self.root/'.artifacts/recovery';recovery.mkdir();result=recovery/'RESULT.json';result.write_text(json.dumps(dict(sides={s:dict(local_path=v['path'],sha256=v['sha256']) for s,v in self.c['v1_deployment']['udid_receipts'].items()})))
  accepted=dict(source_window=str(recovery),recovery_directory=str(recovery));p=self.root/self.m.ROS_V1_RECOVERED_SOURCE_RELATIVE;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(accepted))
  # Fixture-only replacement of the independently pinned acceptance verifier.
  self.m.verify_recovered_identity_source=lambda window:accepted
  self.m.verify_v1_identity_correspondence(self.c)
  bad=copy.deepcopy(self.c);bad['v1_deployment']['udid_receipts']['A']=bad['v1_deployment']['udid_receipts']['B'];self.assertRaises(ValueError,self.m.verify_v1_identity_correspondence,bad)
  unrelated=self.root/'.artifacts/unrelated.bin';unrelated.write_bytes(Path(self.c['v1_deployment']['udid_receipts']['A']['path']).read_bytes());bad=copy.deepcopy(self.c);bad['v1_deployment']['udid_receipts']['A']['path']=str(unrelated);self.assertRaises(ValueError,self.m.verify_v1_identity_correspondence,bad)
 def test_v1_terminal_requires_actual_locator_analysis(self):
  w=self.root/'windows/terminal';w.mkdir(parents=True);c=dict(run='1234567890abcdef',v1_deployment=self.c['v1_deployment'])
  def put(name,value):(w/name).write_text(json.dumps(value))
  put('window-summary.json',dict(first_failure=None));put('result.json',dict(run=c['run'],**{'pass':True},timedOut=False,collectionCleanup='HDC_STREAMS_CLOSED',activeCollectors=[],A=dict(closed=True,exited=True),B=dict(closed=True,exited=True)));put('load-collection-summary.json',dict(first_failure=None,failures=[]))
  for name in ('ros-analysis','ros-native-analysis'):put(name+'.json',dict(run=c['run'],**{'pass':True}));(w/(name+'.rc')).write_text('0')
  self.assertRaises(ValueError,self.m.ros_task_data_outcome,w,c)
  put('v1-locator-analysis.json',dict(run=c['run'],**{'pass':True}));self.assertRaises(ValueError,self.m.ros_task_data_outcome,w,c)
  (w/'v1-locator-analysis.rc').write_text('0');self.assertEqual(self.m.ros_task_data_outcome(w,c)['status'],'PASS')
  (w/'v1-locator-analysis.rc').write_text('1');self.assertEqual(self.m.ros_task_data_outcome(w,c)['status'],'FAIL')
  (w/'v1-locator-analysis.rc').write_text('0');put('v1-locator-analysis.json',dict(run=c['run'],**{'pass':False}));self.assertEqual(self.m.ros_task_data_outcome(w,c)['status'],'FAIL')
  (w/'v1-locator-analysis.json').unlink();put('V1_ANALYSIS_FAILURE.json',dict(run=c['run'],first_failure='ACTUAL_V1_LOCATOR_ANALYSIS_FAILED'));self.assertEqual(self.m.ros_task_data_outcome(w,c)['first_failure'],'ACTUAL_V1_LOCATOR_ANALYSIS_FAILED')
if __name__=='__main__':unittest.main(defaultTest='WindowConfig')
