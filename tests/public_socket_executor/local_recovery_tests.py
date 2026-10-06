"""Local recovery protocol tests; real PS launch + explicit Fake driver, never HDC.

These isolated fixtures deliberately fail at their first read-only preflight.
They are not successor query/data attempts and cannot unblock the historical r6 root.
"""
from pathlib import Path
import hashlib,importlib.util,json,os,shutil,subprocess,sys,unittest
from flow_tests import Flow,read,put,sha,mod,REPO

class Recovery(unittest.TestCase):
 def setUp(self):
  self.flow=Flow();self.flow.setUp();self.task=self.flow.task;self.w=self.flow.w
  self.fixture=Path(os.environ['MDDS_RECOVERY_DRIVER_FIXTURE']).resolve()
  self.local=self.task/'local-flow';self.local.mkdir()
  for name in ('fake-hdc.exe','fake-hdc.py'):shutil.copyfile(self.fixture/name,self.local/name)
  self.driver=self.local/'fake-hdc.exe'
  if sha(self.driver)!='db7ae6830317e94976954d34a874d801c81eb189b824a876742d598853f83bd3':raise ValueError('Unknown local-only PE')
  profile=read(self.task/'PRIVATE_CONFIG.json');profile.update(hdc_path=str(self.driver),targets={'A':'SIM-A','B':'SIM-B'})
  put(self.task/'PRIVATE_CONFIG.json',profile)
  material=read(self.task/'MATERIALIZATION.json');material['private_config_sha256']=sha(self.task/'PRIVATE_CONFIG.json');put(self.task/'MATERIALIZATION.json',material)
  self.flow.c=mod(self.task/'executor/task_contract.py','recovery_contract');self.c=self.flow.c
  f=self.flow.f;f.update(scope=self.c.INSTALLED_SCOPE,limits=self.c.INSTALLED_LIMITS,execution_admitted=False,local_contract_test=True,device_evidence=False,device_authorized=False,local_test_basis='ISOLATED_RECOVERY_PROTOCOL_TEST_NOT_BUSINESS_FLOW',local_driver={'kind':'LOCAL_FAKE_SANDBOX','test_root':str(self.task),'sandbox_root':str(self.task/'sandbox')},private_config_sha256=sha(self.task/'PRIVATE_CONFIG.json'),materialization_sha256=sha(self.task/'MATERIALIZATION.json'))
  plan=self.task/'INSTALLED_RUNTIME_CONTRACT.md';plan.write_text('ISOLATED LOCAL RECOVERY PROTOCOL ONLY');f['plan_sha256']=sha(plan);put(self.flow.contract,f)
  pre=read(self.flow.p);pre['platform']={side:{'files':[{'path':'/system/lib64/platformsdk/libsoftbus_client.z.so','sha256':'0'*64}],'parameters':[]} for side in 'AB'};put(self.flow.p,pre)
  self.cfg=self.flow.prepare();(self.task/'sandbox/platform').mkdir(parents=True)
  env=os.environ.copy();env.update(FAKE_HDC_SCRIPT=str(self.local/'fake-hdc.py'),FAKE_HDC_PYTHON=sys.executable,FAKE_HDC_RAW_LOG=str(self.task/'sandbox/raw.log'),FAKE_HDC_SANDBOX=str(self.task/'sandbox'),FAKE_HDC_TEST_RUN=self.cfg['run'],FAKE_HDC_WINDOW_ROOT=str(self.w))
  env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules;C:/Program Files/WindowsPowerShell/Modules'
  a=[os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/powershell.exe','-NoLogo','-NoProfile','-NonInteractive','-File',str(self.task/'executor-route-query/launch.ps1'),'-WindowDirectory',str(self.w)]
  self.launch_args=a;self.launch_env=env
  p=subprocess.run(a,capture_output=True,env=env,cwd=self.flow.repo,timeout=30)
  self.assertEqual(p.returncode,1,p.stdout.decode(errors='replace')+p.stderr.decode(errors='replace'))
  self.assertTrue((self.w/'EXECUTION_STARTED').is_file(),p.stderr.decode(errors='replace'))
  self.assertEqual(read(self.w/'window-summary.json')['attempted_grants'],[])
 def tearDown(self):self.flow.tearDown()
 def test_actual_readonly_failure_seal_recover_validate(self):
  original={str(p.relative_to(self.w)):sha(p) for p in self.w.rglob('*') if p.is_file()}
  with self.assertRaises(OSError):self.c.require_restored(self.w)
  for flag in ('--seal-local-actions','--recover-local-no-state-change'):
   result=subprocess.run([sys.executable,str(self.task/'executor/task_contract.py'),flag,str(self.w)],capture_output=True,text=True,timeout=30)
   self.assertEqual(result.returncode,0,result.stderr)
  receipt=read(self.w/'NO_STATE_CHANGE_RECOVERY.json')
  self.assertEqual(receipt['decision'],'NO_STATE_CHANGE_CONFIRMED')
  self.assertEqual(self.c.require_restored(self.w),'RESTORED_FOR_LOCAL_TEST_CONTINUATION')
  self.assertEqual(receipt['executed_command_count'],1)
  # A later driver-only edit cannot relabel or invalidate the captured old version.
  driver_source=self.local/'fake-hdc.py'
  driver_source.write_text(driver_source.read_text(encoding='utf-8')+'\n# later local fixture revision\n',encoding='utf-8')
  self.assertEqual(self.c.require_restored(self.w),'RESTORED_FOR_LOCAL_TEST_CONTINUATION')
  for name,h in original.items():self.assertEqual(sha(self.w/name),h)
  self.assertTrue(read(self.w/'window-summary.json')['first_failure'])
  for s in 'AB':self.assertFalse((self.w/s/'final-guard.json').exists())
  # Admission of a new fixture window uses the actual verify_query predecessor gate.
  next_window=self.task/'windows/next-local-fixture'
  previous=Path.cwd()
  try:
   os.chdir(self.flow.repo)
   self.flow.pre.prepare(next_window,self.flow.p,self.flow.prov,self.flow.contract,10)
  finally:os.chdir(previous)
  next_cfg=read(next_window/'window.json');self.c.verify_query(next_cfg)
  env=dict(self.launch_env,FAKE_HDC_TEST_RUN=next_cfg['run'],FAKE_HDC_WINDOW_ROOT=str(next_window))
  result=subprocess.run(self.launch_args[:-1]+[str(next_window)],capture_output=True,env=env,cwd=self.flow.repo,timeout=30)
  self.assertEqual(result.returncode,1)
  self.assertTrue((next_window/'EXECUTION_STARTED').is_file())
  # Append-only later windows do not invalidate this window's sealed prefix.
  self.assertEqual(self.c.require_restored(self.w),'RESTORED_FOR_LOCAL_TEST_CONTINUATION')
 def test_reject_negative_evidence(self):
  original_cfg=(self.w/'window.json').read_bytes()
  put(self.w/'window.json',dict(self.cfg,private_config_sha256='0'*64))
  with self.assertRaisesRegex(ValueError,'Private task input changed'):self.c.seal_local_actions(self.w)
  (self.w/'window.json').write_bytes(original_cfg)
  self.c.seal_local_actions(self.w);self.c.recover_local_no_state_change(self.w)
  summary=self.w/'window-summary.json';journal=self.task/'sandbox/fake-hdc-invocations.jsonl';actions=self.w/'LOCAL_ACTION_JOURNAL.json';cmd=next(self.w.glob('[AB]/*/command.json'))
  def rejected(path,change):
   old=path.read_bytes();change(path)
   try:
    with self.assertRaises((ValueError,OSError,KeyError)):self.c.require_restored(self.w)
   finally:path.write_bytes(old)
  rejected(summary,lambda p:put(p,dict(read(p),attempted_grants=['A'])))
  rejected(summary,lambda p:put(p,dict(read(p),process_outcome='BUSINESS_STARTED')))
  for text in ['file send', 'chmod 700 /tmp/x', 'env business', 'unknown-command']:
   rejected(cmd,lambda p,t=text:put(p,[str(self.driver),'-t','SIM-A','shell',t+'; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc']))
  flag=self.w/'DATA_ATTEMPT_STARTED';flag.write_text('test-negative')
  with self.assertRaises(ValueError):self.c.require_restored(self.w)
  flag.unlink()
  rejected(journal,lambda p:p.write_text(''))
  rejected(journal,lambda p:p.write_text(p.read_text()+p.read_text()))
  for key,value in [('test_run','f'*16),('window_root','foreign'),('target','SIM-B'),('driver_exit',False),('remote_rc',True),('sandbox_root','foreign'),('driver_source_snapshot','foreign'),('driver_status','DRIVER_REJECTED_INVOCATION'),('model_mutations',['grant']),('spawned_children',[123]),('pid',os.getpid())]:
   def mutate(p,k=key,v=value):
    row=json.loads(p.read_text());row[k]=v;p.write_text(json.dumps(row)+'\n')
   rejected(journal,mutate)
  rejected(actions,lambda p:put(p,dict(read(p),executed_command_count=99)))
  rejected(actions,lambda p:put(p,dict(read(p),complete=1)))
  rejected(self.w/'NO_STATE_CHANGE_RECOVERY.json',lambda p:put(p,dict(read(p),grant='PASS')))
  actions_bytes=actions.read_bytes();actions.unlink()
  with self.assertRaises(OSError):self.c.require_restored(self.w)
  actions.write_bytes(actions_bytes)
  # Even a generated local receipt cannot waive the real/device normal restore path.
  frozen=Path(self.cfg['task_contract']['freeze_path'])
  for value in [False,'true',1,None]:
   rejected(frozen,lambda p,v=value:put(p,dict(read(p),local_contract_test=v)))
 def test_actual_local_file_send_invalidates_sealed_window(self):
  self.c.seal_local_actions(self.w);self.c.recover_local_no_state_change(self.w)
  src=self.task/'sandbox/payload.bin';src.write_bytes(b'LOCAL_MUTATION_NOT_DEVICE_DEPLOYMENT')
  driver_args=['-t','SIM-A','file','send',str(src),self.cfg['remote']['A']+'/mutation.bin']
  step=self.w/'A/deploy-negative';step.mkdir()
  # Actual local process at the same explicit boundary; preserve its command/streams/exit.
  put(step/'command.json',[str(self.driver),*driver_args])
  result=subprocess.run([str(self.driver),*driver_args],env=self.launch_env,cwd=self.flow.repo,capture_output=True,timeout=30)
  (step/'stdout.log').write_bytes(result.stdout);(step/'stderr.log').write_bytes(result.stderr);(step/'exit-code.txt').write_text(str(result.returncode))
  self.assertEqual(result.returncode,0,result.stderr)
  self.assertEqual((self.task/'sandbox/A/mutation.bin').read_bytes(),src.read_bytes())
  with self.assertRaisesRegex(ValueError,'LOCAL_RECOVERY_LATE_DISPATCH'):self.c.require_restored(self.w)

if __name__=='__main__':unittest.main()
