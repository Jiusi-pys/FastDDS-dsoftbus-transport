"""Portable synthetic task flow. Explicit local helpers only; no SDK/HDC dependency."""
from pathlib import Path
import hashlib,importlib.util,json,shutil,subprocess,sys,tempfile,unittest,os
HERE=Path(__file__).resolve().parent;REPO=HERE.parents[1]
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(p,x):p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x),encoding='utf-8');return p
def mod(p,name):
 s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
class Flow(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.repo=Path(self.tmp.name)/'repo';self.repo.mkdir();self.task=self.repo/'.artifacts/group/run'
  shutil.copytree(REPO/'scripts/public_socket',self.repo/'scripts/public_socket',ignore=shutil.ignore_patterns('__pycache__'))
  shutil.copyfile(REPO/'scripts/NativePermissionGuard.ps1',self.repo/'scripts/NativePermissionGuard.ps1')
  probe=self.repo/'fake-query.bin';probe.write_bytes(b'Q'*257536);helper=self.repo/'fake-permission.bin';helper.write_bytes(b'LOCAL_HELPER_NOT_EXECUTABLE')
  profile=put(self.repo/'input-profile.json',dict(targets={'A':'LOCAL_A','B':'LOCAL_B'},tokens={'A':'123','B':'456'},hdc_path=sys.executable,query_helper_path=str(probe),permission_baseline_sha256={'A':'a'*64,'B':'b'*64}))
  self.materializer=mod(self.repo/'scripts/public_socket/shared_reattach/materialize.py','materializer');self.materializer.materialize(self.repo,self.task,profile)
  self.c=mod(self.task/'executor/task_contract.py','flow_contract');self.pre=mod(self.task/'executor-route-query/prepare-query.py','flow_prepare')
  # Only replace the external compiled helper attestations in this local test module.
  # The helper is never executed; production hashes remain unchanged on disk.
  self.pre.QUERY_SHA256=sha(probe);self.pre.PERMISSION_SHA256=sha(helper)
  plan=self.task/'OBSERVED_RETRY_CONTRACT.md';plan.write_text('LOCAL SYNTHETIC CONTRACT; NO DEVICE AUTHORITY')
  self.p=put(self.task/'pre.json',dict(eligible_for_executor_gate=True,permission_helper={'path':str(helper),'sha256':sha(helper)},platform={'A':{'fixture':True},'B':{'fixture':True}},required_platform_paths=['/system/lib64/local-test-only.so']))
  self.prov=put(self.task/'provenance.json',{'fixture':'LOCAL_ONLY'});stub=put(self.repo/'fixture.json',{'local':True});source=put(self.task/'build/source-state.json',[dict(path='fixture.json',sha256=sha(stub))])
  self.build=self.task/'build';package=self.build/'package';package.mkdir();artifacts=[]
  for name in ('public_socket_dds_experiment','libmdds_runtime_token_bridge.so','libfastdds.so.3.6','libfastcdr.so.2','libfoonathan_memory-0.7.4.so','libc++_shared.so'):
   p=package/name;p.write_text('LOCAL_SYNTHETIC_NOT_EXECUTABLE '+name);artifacts.append(dict(name=name,path=str(p),sha256=sha(p)))
  manifest=put(self.build/'build-manifest.json',dict(bridgeExit=0,configureExit=0,buildExit=0,configureArguments=['-DBUILD_PUBLIC_SOCKET_DDS_EXPERIMENT=ON'],sourceStateSha256=sha(source),artifacts=artifacts));ev=put(self.task/'evidence.json',{'fixture':True})
  materialized=read(self.task/'MATERIALIZATION.json')
  files=[self.task/e['destination'] for e in materialized['files']]+[self.repo/e['path'] for e in materialized['external_files']]
  self.f=dict(materialization_sha256=sha(self.task/'MATERIALIZATION.json'),task_root=str(self.task),scope=self.c.OBSERVED_SCOPE,authorization='ONE_NEW_N10_AND_CONDITIONAL_N100',limits=self.c.OBSERVED_LIMITS,plan_sha256=sha(plan),private_config_sha256=sha(self.task/'PRIVATE_CONFIG.json'),execution_admitted=True,permission_helper=read(self.p)['permission_helper'],header_sha256={'socket':'a'*64,'trans_type':'b'*64},source=read(source),compiled_inputs=[dict(path=str(stub),sha256=sha(stub))],execution_files=[dict(path=str(p),sha256=sha(p)) for p in files],admission_evidence=[dict(role=k,path=str(ev),sha256=sha(ev)) for k in ('host','asan','review','target','executor_tests')],build_binding=dict(source_state=str(source),source_state_sha256=sha(source),build_manifest=str(manifest),build_manifest_sha256=sha(manifest),main_sha256=artifacts[0]['sha256'],package_artifacts=[dict(name=e['name'],source=e['path'],sha256=e['sha256']) for e in artifacts]))
  self.contract=put(self.task/'DEVICE_CONTRACT-r12.json',self.f);self.w=self.task/'windows/query'
 def tearDown(self):self.tmp.cleanup()
 def prepare(self,cycles=10):
  previous=Path.cwd()
  try:
   os.chdir(self.repo);self.pre.prepare(self.w,self.p,self.prov,self.contract,cycles)
  finally:os.chdir(previous)
  self.cfg=read(self.w/'window.json');return self.cfg
 def claim(self):self.c.claim_query(self.cfg)
 def fake_execute_restore(self):
  (self.w/'EXECUTION_STARTED').write_text('LOCAL_SYNTHETIC_NO_DEVICE')
  for s in 'AB':
   driver=HERE/'fake_query_driver.py';child=subprocess.run([sys.executable,str(driver),s],capture_output=True,text=True,check=True);data=json.loads(child.stdout);self.assertEqual(data['scope'],'LOCAL_SYNTHETIC_NO_DEVICE')
   d=self.w/s;put(d/'fake-driver.json',dict(executable=sys.executable,argv=[str(driver),s],exit_code=child.returncode))
   q=d/'directory-query';q.mkdir();(q/'stdout.log').write_text(data['stdout']);(q/'stderr.log').write_text('');(q/'exit-code.txt').write_text(str(data['local_exit']))
   root=self.cfg['remote'][s];loader=':'.join([root]+self.cfg['system_library_dirs']);command=f"env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH={loader} timeout 10 {root}/softbus_record_probe --list-nodes --expected-token {self.cfg['token'][s]} --preflight-confirmed; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"
   put(q/'command.json',[self.cfg['hdc_path'],'-t',self.cfg['targets'][s],'shell',command])
   for n in ('baseline.json','after.json'):put(d/n,{'local_fixture':True})
   put(d/'final-guard.json',dict(Passed=True,Phase='Revoked',byte_match=True,metadata_match=True,process_absent=True,service_permission='DENIED',metadata_before={'evidence':str(d/'metadata-before')},metadata_after={'evidence':str(d/'metadata-after')}))
   for n in ('ownership-guard.json','final-snapshot-guard.json','granted-guard.json'):put(d/n,{'Passed':data['restored']})
  put(self.w/'window-summary.json',dict(first_failure=None,cleanup_failures=[],attempted_grants=['A','B']))
 def postrun(self,out=None,archive=None):
  args=[sys.executable,str(self.task/'executor-route-query/route_query.py'),str(self.w)]
  if out:args+=['--output',str(out)]
  if archive:args+=['--archive-manifest',str(archive)]
  return subprocess.run(args,capture_output=True,text=True)
 def test_complete_actual_path(self):
  self.prepare();self.claim();self.fake_execute_restore();result=self.postrun();self.assertEqual(result.returncode,0,result.stderr)
  self.assertTrue(read(self.w/'PRIVATE_ROUTE_BINDINGS.json')['pass']);self.assertRaises(ValueError,self.c.claim_query,self.cfg)
  again=self.postrun(self.task/'replay-second');self.assertEqual(again.returncode,0,again.stderr)
  self.assertEqual(len(list((self.task/'observed-retry/attempt-ledger').glob('query-*.json'))),1);self.assertEqual(list((self.task/'observed-retry/attempt-ledger').glob('data-*')),[])
 def test_bad_cycle_before_creation(self):
  for value in (10.0,100.0,True,False,'10',None,0,11,101,{},[]):
   with self.subTest(value=value):
    with self.assertRaises((ValueError,TypeError)):self.prepare(value)
    self.assertFalse(self.w.exists());self.assertFalse((self.task/'observed-retry/attempt-ledger').exists())
  self.assertEqual(self.c.claim_path('query',100).name,'query-N100.json')
  proc=subprocess.run([sys.executable,str(self.task/'executor-route-query/prepare-query.py'),str(self.w),'--preflight-config',str(self.p),'--helper-provenance',str(self.prov),'--contract',str(self.contract),'--for-cycles','10.0'],capture_output=True)
  self.assertNotEqual(proc.returncode,0);self.assertFalse(self.w.exists())
 def completed(self):self.prepare();self.claim();self.fake_execute_restore()
 def assert_blocked(self):
  p=self.postrun(self.task/'blocked-result');self.assertNotEqual(p.returncode,0,p.stdout);self.assertFalse((self.task/'blocked-result/PRIVATE_ROUTE_BINDINGS.json').exists())
  self.assertRaises((ValueError,FileNotFoundError),self.c.claim_query,self.cfg)
 def test_claim_identity(self):
  self.completed();p=self.c.claim_path('query',10);good=p.read_bytes()
  for key,value in [('run','f'*16),('window',str(self.task/'windows/foreign')),('cycles',100),('cycles',10.0),('scope','foreign'),('kind','NO_DISPATCH')]:
   with self.subTest(key=key,value=value):x=json.loads(good);x[key]=value;put(p,x);self.assert_blocked();p.write_bytes(good)
  p.unlink();self.assertNotEqual(self.postrun(self.task/'no-claim').returncode,0);p.write_bytes(good)
 def test_missing_actual_receipts(self):
  self.completed()
  for name in ['A/directory-query/stdout.log','B/directory-query/exit-code.txt','A/directory-query/command.json','EXECUTION_STARTED','A/final-guard.json','B/ownership-guard.json','B/granted-guard.json']:
   with self.subTest(name=name):p=self.w/name;b=p.read_bytes();p.unlink();self.assert_blocked();p.write_bytes(b)
 def test_invalid_outputs_and_exits(self):
  self.completed();p=self.w/'A/directory-query/stdout.log';b=p.read_bytes()
  for text in ['', 'null','unrelated',b.decode()[:100],b.decode().replace('MDDS_REMOTE_EXIT=0','MDDS_REMOTE_EXIT=5'),b.decode()+'route_kind=local network_id='+('c'*64)+'\n',b.decode().replace('b'*64,'c'*64)]:
   with self.subTest(text=text[:30]):p.write_text(text);self.assert_blocked();p.write_bytes(b)
  rc=self.w/'B/directory-query/exit-code.txt';rc.write_text('1');self.assert_blocked()
 def test_wrong_target_and_recovery(self):
  self.completed();p=self.w/'A/directory-query/command.json';good=p.read_bytes();x=read(p);x[2]=self.cfg['targets']['B'];put(p,x);self.assert_blocked();p.write_bytes(good)
  for name,key,value in [('A/final-guard.json','Passed',False),('window-summary.json','cleanup_failures',['LOCAL_FAKE_RESTORE_FAILED']),('A/ownership-guard.json','Passed',False)]:
   p=self.w/name;good=p.read_bytes();x=read(p);x[key]=value;put(p,x);self.assert_blocked();p.write_bytes(good)
 def test_route_loading_and_output_immutability(self):
  self.completed();out=self.task/'derived-one';p=self.postrun(out);self.assertEqual(p.returncode,0,p.stderr)
  loader=mod(self.task/'executor/route_binding.py','flow_route');private=out/'PRIVATE_ROUTE_BINDINGS.json';proof=loader.load_route_binding(private);loader.verify_frozen(proof,proof['peer']);self.assertEqual(proof['query_run'],self.cfg['run'])
  self.assertEqual(proof['query_for_cycles'],10);self.assertNotEqual(proof['query_run'],'f'*16)
  saved=private.read_bytes();self.assertNotEqual(self.postrun(out).returncode,0);self.assertEqual(saved,private.read_bytes())
  raw=self.w/'A/directory-query/stdout.log';raw.write_text(raw.read_text()+'changed\n');self.assertRaises(ValueError,loader.verify_frozen,proof,proof['peer'])
 def test_concurrent_claim(self):
  self.prepare();script=self.task/'claim_local.py';script.write_text("import sys,json;sys.path.insert(0,"+repr(str(self.task/'executor'))+");import task_contract as c;c.claim_query(json.load(open(sys.argv[1])))")
  children=[subprocess.Popen([sys.executable,str(script),str(self.w/'window.json')],stdout=subprocess.PIPE,stderr=subprocess.PIPE) for _ in range(2)]
  for child in children:child.communicate(timeout=10)
  self.assertEqual(sorted(p.returncode==0 for p in children),[False,True]);self.fake_execute_restore();self.assertEqual(self.postrun().returncode,0)
 def archive(self):
  rows=[]
  for i,e in enumerate(self.f['execution_files']):
   p=self.task/'archive'/str(i);p.parent.mkdir(exist_ok=True);shutil.copyfile(e['path'],p);rows.append(dict(original_path=e['path'],archive_path=str(p),sha256=e['sha256']))
  return put(self.task/'archive.json',rows)
 def test_archive27_and_identity_domains(self):
  self.completed();archive=self.archive();self.assertEqual(self.postrun(self.task/'archive-ok',archive).returncode,0)
  for row in read(archive):
   p=Path(row['archive_path']);b=p.read_bytes();p.write_bytes(b+b'changed');result=self.postrun(self.task/'archive-bad',archive);self.assertNotEqual(result.returncode,0);p.write_bytes(b)
  original=self.task/'executor/task_contract.py';b=original.read_bytes();original.write_bytes(b+b'\n# future revision\n')
  # Past execution uses the archived source, not current edited code identity.
  result=self.postrun(self.task/'archive-after-current-change',archive);self.assertEqual(result.returncode,0,result.stderr)
  self.assertRaises(ValueError,self.c.execution_admission,self.cfg['task_contract']);original.write_bytes(b)
  rows=read(archive);put(archive,rows[:-1]);self.assertNotEqual(self.postrun(self.task/'archive-missing',archive).returncode,0)
 def test_source_contract_and_peer_drift(self):
  self.completed();original=self.contract.read_bytes();self.contract.write_bytes(original+b' ');self.assert_blocked();self.contract.write_bytes(original)
  source=Path(self.f['build_binding']['source_state']);original=source.read_bytes();source.write_bytes(original+b' ');self.assert_blocked();source.write_bytes(original)
  out=self.task/'peer-output';self.assertEqual(self.postrun(out).returncode,0);loader=mod(self.task/'executor/route_binding.py','peer_loader');p=out/'PRIVATE_ROUTE_BINDINGS.json';x=read(p);x['peer']['A']='f'*64;put(p,x);self.assertRaises(ValueError,loader.load_route_binding,p)

 def test_private_profile_fails_closed(self):
  profile=read(self.repo/'input-profile.json');profile['tokens']={'A':'123'};p=put(self.repo/'invalid-profile.json',profile);dest=self.repo/'.artifacts/group/invalid'
  self.assertRaises(ValueError,self.materializer.materialize,self.repo,dest,p);self.assertFalse(dest.exists())
 def test_wrong_root_and_profile_drift(self):
  self.assertRaises(ValueError,self.materializer.materialize,self.repo,self.repo/'wrong-depth',self.repo/'input-profile.json')
  self.prepare();p=self.task/'PRIVATE_CONFIG.json';p.write_text(p.read_text()+' ');self.assertRaises(ValueError,self.c.claim_query,self.cfg)
 def accepted_n10_fixture(self):
  w=self.task/'windows/accepted-n10';run='3'*16;binding={k:self.f['build_binding'][k] for k in ('main_sha256','source_state_sha256')}
  put(w/'window.json',dict(run=run,work_package=self.c.OBSERVED_SCOPE,cycles=10,**binding));put(self.c.claim_path('data',10),dict(scope=self.c.OBSERVED_SCOPE,run=run,window=str(w)))
  put(w/'result.json',dict(run=run,scenario='SHARED_W1',**{'pass':True},controller={'pass':True},timedOut=False,activeCollectors=[],collectionCleanup='HDC_STREAMS_CLOSED',A={'remoteExit':0,'hdcExit':0,'signal':None},B={'remoteExit':0,'hdcExit':0,'signal':None}))
  put(w/'cycle-analysis.json',dict(pass_=True,run=run,cycles=10,writes=300,reader_samples=300,send_policy='selected_retirement_v1'));put(w/'window-summary.json',dict(first_failure=None,cleanup_failures=[]));put(w/'load-collection-summary.json',dict(first_failure=None,failures=[]))
  for side in 'AB':
   d=w/side;put(d/'actual-load-origin.json',dict(complete=True));(w/(side+'.raw.stdout.log')).write_text('LOCAL SYNTHETIC PREREQUISITE, NOT DEVICE EVIDENCE')
   for name in ('baseline.json','after.json'):put(d/name,{'fixture':True})
   put(d/'final-guard.json',dict(Passed=True,byte_match=True,metadata_match=True,process_absent=True,service_permission='DENIED',metadata_before={'evidence':str(d/'metadata-before')},metadata_after={'evidence':str(d/'metadata-after')}))
   for name in ('ownership-guard.json','final-snapshot-guard.json'):put(d/name,dict(Passed=True))
  names=['window.json','result.json','cycle-analysis.json','window-summary.json','load-collection-summary.json','A.raw.stdout.log','B.raw.stdout.log','A/actual-load-origin.json','B/actual-load-origin.json','A/final-guard.json','B/final-guard.json'];hashes={n:sha(w/n) for n in names};checks={k:True for k in ('exits','all_cycles','participant_counts','sample_ledger','native_continuity','control_termination','live_load','postrun','guards','collector_drained','no_watchdog_substitution')}
  review=put(self.task/'review.json',dict(verdict='PASS',run=run,window=str(w),review_kind='INDEPENDENT_N10_EVIDENCE_REVIEW',checks=checks,**binding,execution_files=self.f['execution_files'],evidence_sha256=hashes))
  acceptance=put(self.task/'observed-retry/N10_ACCEPTANCE.json',dict(decision='PASS',scope=self.c.OBSERVED_SCOPE,independent_review_closed=True,window=str(w),run=run,**binding,execution_files=self.f['execution_files'],review_report={'path':str(review),'sha256':sha(review)},evidence_sha256=hashes))
  self.f['n10_acceptance_path']=str(acceptance);put(self.contract,self.f)
 def test_full100_and_bound_node_identity(self):
  self.accepted_n10_fixture();self.prepare(100);self.claim();self.fake_execute_restore();result=self.postrun();self.assertEqual(result.returncode,0,result.stderr);self.assertRaises(ValueError,self.c.claim_query,self.cfg)
  previous=sys.modules.get('task_contract');sys.modules['task_contract']=self.c
  try:prepare_data=mod(self.task/'executor/prepare-window.py','portable_data')
  finally:
   if previous is None:sys.modules.pop('task_contract',None)
   else:sys.modules['task_contract']=previous
  out=self.task/'windows/data100';prepare_data.prepare('SHARED_W2',self.build,out,self.p,self.w/'PRIVATE_ROUTE_BINDINGS.json',None,self.contract,100);cfg=read(out/'window.json');self.assertEqual(cfg['record_count'],3000);self.assertEqual(cfg['expected_writes'],{'A':1500,'B':1500});self.assertNotEqual(cfg['run'],self.cfg['run']);self.assertFalse(self.c.claim_path('data',100).exists())
  code="const fs=require('node:fs'),assert=require('node:assert/strict'),r=require(process.argv[1]),c=JSON.parse(fs.readFileSync(process.argv[2]));const profile=r.readBoundPrivate(c);assert.equal(profile.tokens.A,'123');assert.match(r.buildCommands(c,profile.tokens).A,/--cycles 100/);c.private_config_sha256='0'.repeat(64);assert.throws(()=>r.readBoundPrivate(c));c.private_config_sha256=JSON.parse(fs.readFileSync(process.argv[2])).private_config_sha256;fs.appendFileSync(require('node:path').join(c.window_root,'record-pair.cjs'),'\\n// drift');assert.throws(()=>r.readBoundPrivate(c));"
  node=shutil.which('node');self.assertIsNotNone(node);p=subprocess.run([node,'-e',code,str(self.task/'executor/runner-template.cjs'),str(out/'window.json')],capture_output=True,text=True);self.assertEqual(p.returncode,0,p.stderr)
  put(self.c.claim_path('data',100),dict(scope=self.c.OBSERVED_SCOPE,run=cfg['run'],window=str(out)));self.assertRaises(ValueError,self.c.verify_window,cfg);again=self.postrun(self.task/'replay-after-data');self.assertEqual(again.returncode,0,again.stderr)
 def test_materialized_closure_cannot_omit_dependency(self):
  path=self.task/'executor/private_config.py';self.f['execution_files']=[e for e in self.f['execution_files'] if Path(e['path'])!=path];put(self.contract,self.f)
  path.write_text(path.read_text()+'\n# source drift behind omitted entry\n')
  fresh=mod(self.task/'executor/task_contract.py','fresh_contract_with_drift')
  self.assertRaises(ValueError,lambda:fresh.execution_admission(fresh.load(self.contract)))
 def test_all_materialized_dependencies_and_descriptor_are_required(self):
  good=list(self.f['execution_files'])
  for name in self.c.MATERIALIZED_FILES:
   path=self.task/name;self.f['execution_files']=[e for e in good if Path(e['path'])!=path];put(self.contract,self.f)
   with self.subTest(name=name):self.assertRaisesRegex(ValueError,'omitted',self.c.load,self.contract)
  self.f['execution_files']=good;put(self.contract,self.f)
  p=self.task/'MATERIALIZATION.json';original=p.read_bytes();p.write_bytes(original+b' ');self.assertRaisesRegex(ValueError,'descriptor',self.c.load,self.contract);p.write_bytes(original)
  rows=read(p);rows['files']=rows['files'][:-1];put(p,rows);self.f['materialization_sha256']=sha(p);put(self.contract,self.f);self.assertRaisesRegex(ValueError,'membership',self.c.load,self.contract)
 def test_unbound_helper_not_executed_before_gate(self):
  p=self.task/'executor/private_config.py';p.write_text(p.read_text()+"\nraise RuntimeError('UNVERIFIED_HELPER_EXECUTED')\n")
  fresh=mod(self.task/'executor/task_contract.py','bootstrap_without_helper')
  self.assertRaisesRegex(ValueError,'bytes changed',fresh.load,self.contract)
 def data_window_fixture(self):
  self.prepare();self.claim();self.fake_execute_restore();result=self.postrun();self.assertEqual(result.returncode,0,result.stderr)
  old=sys.modules.get('task_contract');sys.modules['task_contract']=self.c
  try:module=mod(self.task/'executor/prepare-window.py','bootstrap_data_prepare')
  finally:
   if old is None:sys.modules.pop('task_contract',None)
   else:sys.modules['task_contract']=old
  out=self.task/'windows/data';module.prepare('SHARED_W1',self.build,out,self.p,self.w/'PRIVATE_ROUTE_BINDINGS.json',None,self.contract,10);return out
 def test_node_no_helper_execution_before_binding(self):
  out=self.data_window_fixture();marker=self.repo/'UNBOUND_NODE_HELPER';p=out/'shared-control.cjs';p.write_text(p.read_text()+"\nrequire('node:fs').writeFileSync("+json.dumps(str(marker))+",'bad');throw Error('UNBOUND_NODE_HELPER');\n")
  proc=subprocess.run([shutil.which('node'),str(out/'runner.cjs')],cwd=self.repo,capture_output=True,text=True,timeout=10)
  self.assertNotEqual(proc.returncode,0);self.assertFalse(marker.exists(),'unverified helper executed before Node binding gate')
 @unittest.skipUnless(sys.platform=='win32','Windows PowerShell bootstrap boundary')
 def test_powershell_no_guard_execution_before_binding(self):
  for data in (False,True):
   case=Flow();case.setUp()
   try:
    if data:window=case.data_window_fixture()
    else:case.prepare();window=case.w
    marker=case.repo/'UNBOUND_PS_GUARD';guard=case.repo/'scripts/NativePermissionGuard.ps1';guard.write_text(guard.read_text()+"\n[IO.File]::WriteAllText('"+str(marker)+"','bad')\nthrow 'UNBOUND_PS_GUARD'\n")
    env=dict(os.environ)
    for key in list(env):
     if key.lower()=='psmodulepath':env.pop(key)
    env['PSModulePath']=str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/Modules')
    script=case.task/('executor' if data else 'executor-route-query')/'run-window.ps1'
    proc=subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(script),'-WindowDirectory',str(window)],cwd=case.repo,env=env,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
    self.assertNotEqual(proc.returncode,0);self.assertFalse(marker.exists(),'unverified project guard executed before binding')
   finally:case.tearDown()
 @unittest.skipUnless(sys.platform=='win32','Windows PowerShell validator bootstrap')
 def test_powershell_validator_is_bound_before_execution(self):
  window=self.data_window_fixture();marker=self.repo/'UNBOUND_PYTHON_VALIDATOR';p=self.task/'executor/task_contract.py'
  p.write_text("from pathlib import Path;Path("+repr(str(marker))+").write_text('bad')\n"+p.read_text(encoding='utf-8'),encoding='utf-8')
  env=dict(os.environ)
  for key in list(env):
   if key.lower()=='psmodulepath':env.pop(key)
  env['PSModulePath']=str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/Modules')
  proc=subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(self.task/'executor/run-window.ps1'),'-WindowDirectory',str(window)],cwd=self.repo,env=env,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
  self.assertNotEqual(proc.returncode,0);self.assertFalse(marker.exists(),'Python validator executed before binding to reviewed contract')
 @unittest.skipUnless(sys.platform=='win32','Windows external guard omission')
 def test_external_guard_cannot_be_omitted(self):
  self.prepare();guard=self.repo/'scripts/NativePermissionGuard.ps1';self.f['execution_files']=[e for e in self.f['execution_files'] if Path(e['path'])!=guard];put(self.contract,self.f)
  self.cfg['task_contract']['freeze_sha256']=sha(self.contract);self.cfg['shared_script_sha256']={};put(self.w/'window.json',self.cfg)
  marker=self.repo/'OMITTED_EXTERNAL_GUARD';guard.write_text("[IO.File]::WriteAllText('"+str(marker)+"','bad')\nthrow 'OMITTED_EXTERNAL_GUARD'\n")
  env=dict(os.environ)
  for key in list(env):
   if key.lower()=='psmodulepath':env.pop(key)
  env['PSModulePath']=str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/Modules')
  proc=subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(self.task/'executor-route-query/run-window.ps1'),'-WindowDirectory',str(self.w)],cwd=self.repo,env=env,capture_output=True,timeout=15,creationflags=subprocess.CREATE_NO_WINDOW)
  self.assertNotEqual(proc.returncode,0);self.assertFalse(marker.exists(),'omitted external guard was executed')
 def test_formal_analyzer_is_required_external_code(self):
  p=self.repo/'scripts/public_socket/analyze_shared.py';p.write_text(p.read_text()+'\n# unbound analyzer drift\n');self.f['execution_files']=[e for e in self.f['execution_files'] if Path(e['path'])!=p];put(self.contract,self.f)
  self.assertRaises(ValueError,lambda:self.c.execution_admission(self.c.load(self.contract)))
if __name__=='__main__':unittest.main()
