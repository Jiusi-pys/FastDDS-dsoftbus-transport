"""Actual formal pregrant failure and recovery CLI using the existing Fake driver.

Explicit local inputs only; no PATH substitution and no real HDC fallback.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys

REPO=Path(__file__).resolve().parents[2]
FIXTURE=REPO/'tests/public_socket_executor/local_flow'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def put(p,x):Path(p).parent.mkdir(parents=True,exist_ok=True);Path(p).write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--root',type=Path,required=True);ap.add_argument('--package-source',type=Path,required=True);ap.add_argument('--shim-source',type=Path,required=True);ap.add_argument('--matrix',action='store_true');ap.add_argument('--auto-reviewed',action='store_true');a=ap.parse_args()
 root=a.root.resolve();need=lambda value: None if value else (_ for _ in ()).throw(ValueError('Fixture assertion failed'))
 need(root.parent==REPO/'.artifacts/ros2-recovery-tests' and not root.exists())
 profile=read(FIXTURE/'private-fixture-input.json');profile.update(task_root=str(root),repo_root=str(REPO),hdc_path=str(root/'local-flow/fake-hdc.exe'),query_helper_path=str(root/'helpers/softbus_record_probe'),ros2_service_prefix='fdds.ros.recovery',ros2_package_name='unit.fdds.recovery')
 inputs=root.parent/(root.name+'-input.json');put(inputs,profile)
 env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules;C:/Program Files/WindowsPowerShell/Modules'
 receipts=root.parent/(root.name+'-receipts');receipts.mkdir()
 def call(label,argv,expected=0,timeout=180):
  p=receipts/label;need(not p.with_suffix('.command.json').exists());put(p.with_suffix('.command.json'),dict(argv=[str(x) for x in argv],cwd=str(REPO),scope='LOCAL_FAKE_RECOVERY_TEST_ONLY'))
  result=subprocess.run([str(x) for x in argv],cwd=REPO,env=env,capture_output=True,timeout=timeout)
  p.with_suffix('.stdout').write_bytes(result.stdout);p.with_suffix('.stderr').write_bytes(result.stderr);put(p.with_suffix('.result.json'),dict(exit_code=result.returncode))
  if result.returncode!=expected:raise RuntimeError(label+' expected '+str(expected)+' actual '+str(result.returncode)+': '+result.stderr.decode(errors='replace')[-700:])
  return result
 call('materialize',[sys.executable,REPO/'scripts/public_socket/shared_reattach/materialize.py','--repo',REPO,'--task',root,'--private-config',inputs])
 (root/'local-flow').mkdir();(root/'helpers').mkdir()
 shutil.copyfile(a.shim_source,root/'local-flow/fake-hdc.exe')
 for name in ('fake-hdc.py','finite_model.py','consumer.py','ros-consumer.py','dds214-consumer.py'):
  if (FIXTURE/name).exists():shutil.copyfile(FIXTURE/name,root/'local-flow'/name)
 helper_root=REPO/'.artifacts/installed-runtime-device/exec-20260929-r6/helpers'
 for name in ('native_permission_probe','softbus_record_probe'):shutil.copyfile(helper_root/name,root/'helpers'/name)
 call('init',[sys.executable,FIXTURE/'init_fixture.py','--repo',REPO,'--root',root,'--package-source',a.package_source,'--consumer-source',REPO/'examples/ros2_transport','--header',REPO/'communication_dsoftbus/interfaces/kits/transport/socket.h','--scenario','ROS2_DSOFTBUS'])
 if a.auto_reviewed:
  need(not a.matrix)
  parent=root/'TEST_AUTO_POLICY_REVIEW.json';put(parent,dict(verdict='PASS',allow_automatic_pregrant_recovery=True,scope='LOCAL_FAKE_SOURCE_POLICY_ONLY'))
  f=read(root/'DEVICE_CONTRACT.json');binding=dict(path=str(parent),sha256=sha(parent));f['admission_evidence']=[dict(role='review',**binding)]
  f['pregrant_recovery_policy']=dict(scope='OWNED_PREGRANT_RECOVERY_ONLY',review=binding,helper=dict(path=str(root/'helpers/native_permission_probe'),sha256=sha(root/'helpers/native_permission_probe')))
  put(root/'DEVICE_CONTRACT.json',f)
 env.update(FAKE_HDC_SCRIPT=str(root/'local-flow/fake-hdc.py'),FAKE_HDC_PYTHON=sys.executable,FAKE_HDC_SANDBOX=str(root/'sandbox'),FAKE_HDC_RAW_LOG=str(root/'sandbox/raw.log'))
 ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
 q=root/'windows/query-unit'
 call('query-prepare',[sys.executable,root/'executor-route-query/prepare-query.py',q,'--preflight-config',root/'preflight.local.json','--helper-provenance',root/'helper-provenance.json','--contract',root/'DEVICE_CONTRACT.json'])
 env.update(FAKE_HDC_TEST_RUN=read(q/'window.json')['run'],FAKE_HDC_WINDOW_ROOT=str(q))
 # Obtain actual Fake-driver baseline bytes/metadata through the existing formal
 # readonly collector. init_fixture's declared expectations are not capture receipts.
 ro=root/'TEST_READONLY_SELECTION.json';pre=read(root/'preflight.local.json')
 put(ro,dict(preflight_output_parent=str(root),targets={'A':'SIM-A','B':'SIM-B'},tokens=read(root/'PRIVATE_CONFIG.json')['tokens'],
  device_driver=dict(path=str(root/'local-flow/fake-hdc.exe'),sha256=sha(root/'local-flow/fake-hdc.exe')),
  platform_reference=str(root/'preflight.local.json'),platform_reference_sha256=sha(root/'preflight.local.json'),permission_helper=pre['permission_helper'],source_files=[dict(path=str(root/'recovery/pregrant_recovery.py'),sha256=sha(root/'recovery/pregrant_recovery.py'))],
  preflight_tool_sha256=sha(root/'recovery/readonly_preflight.py'),local_contract_test=True))
 approval=root/'TEST_READONLY_REVIEW.json';put(approval,dict(verdict='PASS',selection_sha256=sha(ro),allow_readonly_preflight=True,scope='LOCAL_FIXTURE_NO_DEVICE_AUTHORITY'))
 call('current-preflight',[sys.executable,root/'recovery/readonly_preflight.py','--selection',ro,'--review',approval,'--output',root/'preflight-observed'])
 call('query-execute',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',root/'executor-route-query/launch.ps1','-WindowDirectory',q])
 w=root/'windows/data-unit'
 call('data-prepare',[sys.executable,root/'executor/prepare-window.py','ROS2_DSOFTBUS','--build',root/'build','--destination',w,'--preflight',root/'preflight-observed/EXECUTOR_PREFLIGHT.json','--route',q/'PRIVATE_ROUTE_BINDINGS.json','--contract',root/'DEVICE_CONTRACT.json'])
 env.update(FAKE_HDC_TEST_RUN=read(w/'window.json')['run'],FAKE_HDC_WINDOW_ROOT=str(w))
 put(root/'sandbox/fault.json',dict(fault='recovery_offline_once' if a.auto_reviewed else 'recovery_offline'))
 call('data-pregrant-failure',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',root/'executor/run-window.ps1','-WindowDirectory',w],expected=1)
 summary=read(w/'window-summary.json');need(summary['failure_stage']=='PREFLIGHT_OR_DEPLOY' and summary['attempted_grants']==[] and not (w/'DATA_ATTEMPT_STARTED').exists())
 original={str(p):sha(p) for p in w.rglob('*') if p.is_file()}
 if a.auto_reviewed:
  result=w/'recovery/attempt-1/RECOVERY_RESULT.json';r=read(result)
  need(r['status']=='PASS' and r['scope']=='LOCAL_RECOVERY_MODEL' and r['first_failure']==summary['first_failure'])
  need(not (w/'B/baseline.json').exists() and not (w/'DATA_ATTEMPT_STARTED').exists())
  put(receipts/'RESULT.json',dict(result='PASS',scope='AUTOMATIC_REVIEWED_RECOVERY_ONLY_AFTER_ACTUAL_PREGRANT_FAILURE',recovery_result=str(result),business_still_failed=True,independent_acceptance_still_required=True,device_operations=0))
  print('AUTOMATIC_LOCAL_RECOVERY_FLOW_PASS');return
 put(root/'sandbox/fault.json',dict(fault=''))
 selection=root/'RECOVERY_SELECTION.json'
 call('recover-prepare',[sys.executable,root/'recovery/pregrant_recovery.py','--prepare-window',w,'--tools-root',root,'--output',selection,'--helper',root/'helpers/native_permission_probe'])
 s=read(selection);need(s['facts']['sides']['B']['baseline']['kind']=='PREFLIGHT_BASELINE')
 review=root/'RECOVERY_TEST_REVIEW.json';put(review,dict(verdict='PASS',scope='LOCAL_FAKE_TEST_APPROVAL_NOT_DEVICE_AUTHORITY',allow_pregrant_recovery=True,recovery_selection_sha256=sha(selection),allow_readonly_preflight=True,selection_sha256=s['readonly_selection']['sha256']))
 cases=[]
 if a.matrix:
  import copy,importlib.util,tempfile
  spec=importlib.util.spec_from_file_location('recovery_test_current',root/'recovery/pregrant_recovery.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  lock=w/'recovery/ACTIVE.json';put(lock,dict(owner=module.process_identity(os.getpid()),nonce='a'*32,scope='LOCAL_LIVE_OWNER_REJECTION_FIXTURE'))
  before=(root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()
  try:call('concurrent-owner',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
  finally:lock.unlink()
  need((root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()==before);cases.append('concurrent-owner')
  with tempfile.TemporaryDirectory() as td:
   td=Path(td);p=td/'attempt-1';put(p/'STARTED.json',dict(owner=module.process_identity(os.getpid()),owner_nonce='b'*32))
   try:module.prior_recovery_closed(td);raise AssertionError('live owner accepted')
   except ValueError as e:need(str(e)=='PRIOR_RECOVERY_STILL_RUNNING')
   code="import sys,json,os;sys.path.insert(0,sys.argv[1]);import pregrant_recovery as m;print(json.dumps(m.process_identity(os.getpid())))"
   dead=json.loads(subprocess.check_output([sys.executable,'-c',code,str(root/'recovery')],text=True))
   put(p/'STARTED.json',dict(owner=dead,owner_nonce='b'*32));put(td/('LOCK_RELEASED-'+'b'*32+'.json'),dict(owner=dead,nonce='b'*32,execution_finished=True));put(p/'A/test/command.json',dict(scope='LOCAL_LIVE_CHILD_REJECTION_FIXTURE'))
   put(p/'A/test/spawn.json',dict(identity=module.process_identity(os.getpid())))
   try:module.prior_recovery_closed(td);raise AssertionError('live child accepted')
   except ValueError as e:need(str(e)=='PRIOR_RECOVERY_CHILD_STILL_RUNNING')
  cases.append('live-child')
  for label,key,value in [('wrong-run','run','0'*16),('wrong-target','targets',{'A':'FOREIGN','B':'SIM-B'}),('wrong-window','window',str(root/'windows/foreign'))]:
   bad=copy.deepcopy(s);bad['facts'][key]=value;sp=root/(label+'.json');put(sp,bad);rp=root/(label+'-review.json');rv=read(review);rv['recovery_selection_sha256']=sha(sp);put(rp,rv)
   before=(root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()
   call(label,[sys.executable,root/'recovery/pregrant_recovery.py','--recover',sp,'--review',rp],expected=1)
   need((root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()==before);cases.append(label)
  call('business-arguments',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review,'--scenario','ROS2_DSOFTBUS'],expected=2);cases.append('business-arguments')
  baseline=w/'A/baseline.json';backup=baseline.read_bytes()
  try:
   baseline.unlink();call('missing-original-baseline',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
  finally:baseline.write_bytes(backup)
  cases.append('missing-original-baseline')
  put(root/'sandbox/fault.json',dict(fault='recovery_offline'))
  try:call('recovery-unreachable',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
  finally:put(root/'sandbox/fault.json',dict(fault=''))
  cases.append('recovery-unreachable')
  interrupted=w/'recovery/attempt-1/RECOVERY_RESULT.json';saved=interrupted.with_name('RESULT_PRESERVED_FOR_CRASH_STATE_TEST.json');interrupted.rename(saved)
  put(receipts/'CRASH_STATE_INJECTION.json',dict(scope='LOCAL_CRASH_STATE_INJECTION_AFTER_ACTUAL_COMMANDS_NOT_OS_SIGNAL',saved_result=str(saved),sha256=sha(saved),original_action_logs_unchanged=True))
  for label,p,mutate in [('permission-difference',root/'sandbox/A/nativetoken.json',lambda v:[dict(x,permissions=['ohos.permission.DISTRIBUTED_DATASYNC']) if x['processName']=='fastdds_dsoftbus_probe' else x for x in v]),('metadata-difference',root/'sandbox/B/metadata.json',lambda v:dict(v,mode='600'))]:
   data=p.read_bytes()
   try:put(p,mutate(read(p)));call(label,[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
   finally:p.write_bytes(data)
   cases.append(label)
  foreign=root/'sandbox/processes/foreign.json';put(foreign,dict(pid=os.getpid()))
  try:call('unknown-live-process',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
  finally:foreign.unlink()
  cases.append('unknown-live-process')
  foreign=root/'sandbox/remote'/read(w/'window.json')['run']/'A/foreign.txt';foreign.write_text('FOREIGN_TEST_OBJECT')
  try:call('unknown-file',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],expected=1)
  finally:foreign.unlink()
  cases.append('unknown-file')
 call('recover',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review],timeout=300)
 result=max((w/'recovery').glob('attempt-*/RECOVERY_RESULT.json'),key=lambda p:int(p.parent.name.split('-')[1]));r=read(result);need(r['status']=='PASS' and r['scope']=='LOCAL_RECOVERY_MODEL')
 before=(root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()
 call('repeat-readonly-verify',[sys.executable,root/'recovery/pregrant_recovery.py','--recover',selection,'--review',review])
 need((root/'sandbox/fake-hdc-invocations.jsonl').read_bytes()==before)
 need(all(sha(p)==h for p,h in original.items()))
 need(not (w/'B/baseline.json').exists() and not (w/'B/metadata-before').exists())
 call('acceptance-required',[sys.executable,root/'recovery/pregrant_recovery.py','--verify-result',result],expected=1)
 tampered=read(result)
 for side in 'AB':
  for name in ('command.json','result.json','spawn.json','stdout.log','stderr.log'):tampered['evidence_sha256'].pop(str(result.parent/side/'snapshot-guard'/name),None)
 altered=result.with_name('RESULT_MISSING_GUARD_EXECUTION.json');put(altered,tampered)
 call('guard-execution-evidence-required',[sys.executable,root/'recovery/pregrant_recovery.py','--verify-result',altered],expected=1)
 put(result.parent/'INDEPENDENT_ACCEPTANCE.json',dict(verdict='PASS',scope='LOCAL_TEST_ACCEPTANCE_NOT_DEVICE_AUTHORITY',environment_restored=True,run=r['run'],window=r['window'],recovery_result_sha256=sha(result)))
 call('accepted-raw-replay',[sys.executable,root/'recovery/pregrant_recovery.py','--verify-result',result])
 gate=receipts/'check-gate.py';gate.write_text("import importlib.util,sys\nfrom pathlib import Path\np=Path(sys.argv[1]);s=importlib.util.spec_from_file_location('gate',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);m.require_restored(Path(sys.argv[2]));print('APPEND_GATE_PASS')\n")
 call('actual-continuation-gate',[sys.executable,gate,root/'executor/task_contract.py',w])
 put(receipts/'RESULT.json',dict(result='PASS',query_run=read(q/'window.json')['run'],failed_run=read(w/'window.json')['run'],recovery_result=str(result),negative_cases=cases,original_unchanged=True,repeated_driver_calls=0,device_operations=0,native_evidence='NOT_RUN_LOCAL_MODEL',scope='FORMAL_PREGRANT_AND_RECOVERY_FLOW'))
 print('FORMAL_LOCAL_RECOVERY_FLOW_PASS')

if __name__=='__main__':main()
