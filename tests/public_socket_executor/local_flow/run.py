"""Actual local top-level formal executor chain; never a device driver fallback."""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path

REPO=Path(__file__).resolve().parents[3];HERE=Path(__file__).resolve().parent
OLD=REPO/'.artifacts/installed-runtime-device/exec-20260929-r6'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(p,x):Path(p).write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--root',required=True,type=Path);ap.add_argument('--phase',choices=['init','query','data','update-driver','build-config-receipt'],required=True);ap.add_argument('--window',default='query-2');ap.add_argument('--data-window',default='data-2');ap.add_argument('--fault',default='');ap.add_argument('--scenario',choices=['INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE'],default='INSTALLED_RUNTIME');a=ap.parse_args();root=a.root.resolve()
 ros=a.scenario=='ROS2_DSOFTBUS'
 dds214=a.scenario=='DDS214_LAYER_BASELINE'
 negative=bool(__import__('re').fullmatch(r'local-negative-20260930-[a-z_]+',root.name)) or (ros and bool(__import__('re').fullmatch(r'local-negative-20261001-ros-[a-z_]+',root.name)))
 if dds214:
  if root.parent!=REPO/'.artifacts/ros2-device-acceptance' or root.name not in ('local-verified-20261002-dds214-layer-r1','local-verified-20261002-dds214-layer-r2','local-verified-20261002-dds214-layer-r3','local-verified-20261002-dds214-layer-r4','local-verified-20261002-dds214-layer-r5'):raise ValueError('Only explicitly isolated DDS214 root')
 elif ros:
  if root.parent!=REPO/'.artifacts/ros2-device-acceptance' or (root.name not in ('local-flow-20261001-ros','local-flow-20261001-ros-r2','local-flow-20261001-ros-r3','local-flow-20261001-ros-r4','local-verified-20261001-ros-final','local-verified-20261001-ros-final-r2','local-verified-20261001-ros-final-r3','local-verified-20261002-ros-load-r1','local-verified-20261002-ros-load-r2','local-verified-20261002-ros-load-r3','local-verified-20261002-ros-threefix-r1') and not negative):raise ValueError('Only the explicitly isolated ROS test roots')
 elif root.parent!=REPO/'.artifacts/installed-runtime-device' or root==OLD or (root.name not in ('local-flow-20260930-isolated','local-flow-20260930-isolated-r2','local-verified-20260930-current') and not negative):raise ValueError('Only explicitly isolated local test roots')
 if a.fault and not negative:raise ValueError('Fault injection is only a distinct local test fixture')
 def command(label,argv,env=None,timeout=60):
  logs=root/'local-command-receipts';logs.mkdir(exist_ok=True);p=logs/label
  if p.with_suffix('.json').exists():raise ValueError('Refusing command receipt overwrite '+label)
  put(p.with_suffix('.json'),dict(argv=[str(x) for x in argv],cwd=str(REPO),python=sys.executable,scope='LOCAL_ONLY',status='STARTED'))
  with p.with_suffix('.stdout').open('wb') as out,p.with_suffix('.stderr').open('wb') as err:
   result=subprocess.run([str(x) for x in argv],cwd=REPO,env=env,stdout=out,stderr=err,timeout=timeout,shell=False)
  put(p.with_suffix('.result.json'),dict(exit_code=result.returncode))
  print(label+' rc='+str(result.returncode),flush=True)
  if result.returncode:raise RuntimeError(label+' failed; receipts '+str(p))
 if a.phase=='init':
  if root.exists():raise ValueError('Fresh environment only; never replace failed root')
  if ros and root.name in ('local-verified-20261001-ros-final','local-verified-20261001-ros-final-r2','local-verified-20261001-ros-final-r3'):
   prior=root.with_name('local-flow-20261001-ros-r4' if root.name.endswith('-final') else 'local-verified-20261001-ros-final' if root.name.endswith('-r2') else 'local-verified-20261001-ros-final-r2')
   import importlib.util
   spec=importlib.util.spec_from_file_location('ros_final_prior_recovery',prior/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
   for marker in (prior/'windows').glob('*/EXECUTION_STARTED'):m.require_restored(marker.parent)
   positive=prior/'windows'/('data-3' if root.name.endswith('-final') else 'data-2')
   if read(positive/'result.json').get('pass') is not True or read(positive/'ros-analysis.json').get('pass') is not True:raise ValueError('Prior positive not independently closed')
  if ros and root.name=='local-flow-20261001-ros-r2':
   prior=root.with_name('local-flow-20261001-ros');logs=prior/'local-command-receipts'
   expected={'init.json','init.result.json','init.stdout','init.stderr','query-2-prepare.json','query-2-prepare.result.json','query-2-prepare.stdout','query-2-prepare.stderr'}
   if {p.name for p in logs.iterdir()}!=expected:raise ValueError('Prior commands not limited to initialization and preparation')
   prepare=read(logs/'query-2-prepare.json');args=prepare['argv']
   if Path(args[1]).resolve()!=prior/'executor-route-query/prepare-query.py' or read(logs/'query-2-prepare.result.json')['exit_code']!=1:raise ValueError('Prior preparation result not identified')
   if 'Required external execution membership mismatch' not in (logs/'query-2-prepare.stderr').read_text():raise ValueError('Prior source preparation cause not identified')
   mat=read(prior/'MATERIALIZATION.json')
   for item in mat['files']:
    if sha(prior/item['destination'])!=item['sha256']:raise ValueError('Prior materialized source changed')
   if list(prior.rglob('EXECUTION_STARTED')) or (prior/'sandbox/fake-hdc-invocations.jsonl').exists() or (prior/'sandbox/actions.jsonl').exists():raise ValueError('Prior dispatch evidence exists; full recovery required')
  if ros and root.name=='local-flow-20261001-ros-r3':
   prior=root.with_name('local-flow-20261001-ros-r2')
   if list(prior.rglob('DATA_ATTEMPT_STARTED')):raise ValueError('Prior data dispatch prohibits environment-change continuation')
   import importlib.util
   spec=importlib.util.spec_from_file_location('ros_prior_recovery',prior/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
   markers=list((prior/'windows').glob('*/EXECUTION_STARTED'))
   if len(markers)!=1 or read(markers[0].parent/'ROUTE_QUERY_RESULT.json')['pass'] is not True:raise ValueError('Prior query scope not closed')
   for marker in markers:m.require_restored(marker.parent)
  if ros and root.name=='local-flow-20261001-ros-r4':
   prior=root.with_name('local-flow-20261001-ros-r3')
   import importlib.util
   spec=importlib.util.spec_from_file_location('ros_materialization_recovery',prior/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
   markers=list((prior/'windows').glob('*/EXECUTION_STARTED'))
   if len(markers)!=2 or list(prior.rglob('DATA_ATTEMPT_STARTED')):raise ValueError('Prior materialization failure boundary not established')
   for marker in markers:m.require_restored(marker.parent)
   if read(prior/'windows/data-2/window-summary.json')['first_failure']!='Error: Materialization membership mismatch':raise ValueError('Unexpected prior failure')
  if root.name in ('local-flow-20260930-isolated-r2','local-verified-20260930-current'):
   prior=root.with_name('local-flow-20260930-isolated' if root.name.endswith('-r2') else 'local-flow-20260930-isolated-r2')
   import importlib.util
   spec=importlib.util.spec_from_file_location('prior_recovery_gate',prior/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
   markers=list((prior/'windows').glob('*/EXECUTION_STARTED'))
   for marker in markers:m.require_restored(marker.parent)
   attempts=list((prior/'windows').glob('*/DATA_ATTEMPT_STARTED'))
   if root.name.endswith('-r2') and attempts:raise ValueError('R1 business attempt: no root-change continuation allowed')
   if root.name=='local-verified-20260930-current' and (len(attempts)!=1 or read(attempts[0].parent/'result.json')['pass'] is not True):raise ValueError('Earlier positive scope must be closed successfully before final fixture verification')
  if dds214 and root.name.endswith(('-r2','-r3','-r4','-r5')):
   prior=root.with_name('local-verified-20261002-dds214-layer-r'+str(int(root.name[-1])-1))
   import importlib.util
   spec=importlib.util.spec_from_file_location('dds214_prior_recovery',prior/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
   for marker in (prior/'windows').glob('*/EXECUTION_STARTED'):m.require_restored(marker.parent)
   if list(prior.rglob('DATA_ATTEMPT_STARTED')) and not root.name.endswith(('-r4','-r5')):raise ValueError('Prior data dispatch requires explicitly checked restoration')
  profile=read(OLD/'PRIVATE_CONFIG.json');profile.update(task_root=str(root),hdc_path=str(root/'local-flow/fake-hdc.exe'),query_helper_path=str(root/'helpers/softbus_record_probe'),tokens={'A':'671088739','B':'671088740'},installed_service_prefix='mdds.localisolated',installed_package_name='local.synthetic.mdds')
  if ros or dds214:profile.update(ros2_service_prefix='fdds.ros.localisolated',ros2_package_name='local.synthetic.fdds.ros')
  profile_path=root.with_suffix('.profile.json')
  # Only synthetic values; no live permission or identity is read or changed.
  profile_path.write_text(json.dumps(profile,indent=2)+'\n')
  p=subprocess.run([sys.executable,str(REPO/'scripts/public_socket/shared_reattach/materialize.py'),'--repo',str(REPO),'--task',str(root),'--private-config',str(profile_path)],cwd=REPO,capture_output=True)
  if p.returncode:raise RuntimeError(p.stderr.decode(errors='replace'))
  (root/'materialize.stdout').write_bytes(p.stdout);(root/'materialize.stderr').write_bytes(p.stderr)
  if ros and root.name in ('local-verified-20261001-ros-final','local-verified-20261001-ros-final-r2','local-verified-20261001-ros-final-r3'):
   put(root/'PRIOR_ENVIRONMENT_CLOSURE.json',dict(prior=str(prior),basis='Final source verification after full normal restoration; no quota refund or cleanup escape',restored_windows=[str(p.parent) for p in (prior/'windows').glob('*/EXECUTION_STARTED')],prior_positive_sha256=sha(positive/'result.json')))
  if root.name in ('local-flow-20260930-isolated-r2','local-verified-20260930-current'):put(root/'PRIOR_ENVIRONMENT_CLOSURE.json',dict(prior=str(prior),restored_windows=[str(p.parent) for p in markers],data_dispatches=len(attempts),reason='Immutable source/fixture verification after complete cleanup, never cleanup escape or quota refund',prior_claims=[dict(path=str(p),sha256=sha(p)) for p in prior.rglob('*') if p.is_file() and ('claims' in str(p) or 'attempt-ledger' in str(p))]))
  local=root/'local-flow';local.mkdir();helpers=root/'helpers';helpers.mkdir()
  for name in ('fake-hdc.py','finite_model.py','consumer.py','ros-consumer.py','dds214-consumer.py','init_fixture.py'):shutil.copyfile(HERE/name,local/name)
  shutil.copyfile(OLD/'local-flow/fake-hdc.exe',local/'fake-hdc.exe')
  if sha(local/'fake-hdc.exe')!='db7ae6830317e94976954d34a874d801c81eb189b824a876742d598853f83bd3':raise ValueError('Unknown local-only shim')
  for name in ('native_permission_probe','softbus_record_probe'):shutil.copyfile(OLD/'helpers'/name,helpers/name)
  package=REPO/'.artifacts/ros2-device-acceptance/layer-gap-validation-20261002/target-static-package' if dds214 else REPO/'.artifacts/ros2-device-acceptance/run-20260930-local/target-package-candidate-r3' if ros else Path('C:/mdds-installed-runtime-0929/package')
  consumer=REPO/'examples/dds214_layer_baseline' if dds214 else Path('C:/fdds-ros2-target-consumer-final-1001/source') if ros else Path('C:/mdds-installed-runtime-0929/consumer-source')
  command('init',[sys.executable,local/'init_fixture.py','--repo',REPO,'--root',root,'--package-source',package,'--consumer-source',consumer,'--scenario',a.scenario,'--header',REPO/'communication_dsoftbus/interfaces/kits/transport/trans_type.h'])
  if negative:put(root/'sandbox/fault.json',dict(scope='LOCAL_NEGATIVE_TEST_ONLY',fault=a.fault))
  inputs=list(local.glob('*'))+[root/'DEVICE_CONTRACT.json',root/'PRIVATE_CONFIG.json',root/'MATERIALIZATION.json',HERE/'run.py']
  put(root/'BEFORE_FIRST_DISPATCH.json',dict(local_contract_test=True,device_authority=False,root=str(root),sandbox=str(root/'sandbox'),inputs=[dict(path=str(p),sha256=sha(p)) for p in inputs if p.is_file()],baseline=[dict(path=str(p),sha256=sha(p)) for p in (root/'sandbox').rglob('*') if p.is_file()],query_claims=[],data_claims=[],interpreter=sys.executable))
  return
 if a.phase=='update-driver':
  import importlib.util
  spec=importlib.util.spec_from_file_location('current_task',root/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  for marker in (root/'windows').glob('*/EXECUTION_STARTED'):m.require_restored(marker.parent)
  number=len(list(root.glob('DRIVER_REVISION-*.json')))+2;receipt=root/f'DRIVER_REVISION-{number}.json'
  if receipt.exists():raise ValueError('No revision overwrite')
  files=[]
  for name in ('fake-hdc.py','finite_model.py','consumer.py','ros-consumer.py'):
   old=root/'local-flow'/name;archive=root/f'local-flow-previous-{number}';archive.mkdir(exist_ok=True);shutil.copyfile(old,archive/name);shutil.copyfile(HERE/name,old);files.append(dict(path=str(old),sha256=sha(old),source=str(HERE/name)))
  files.append(dict(path=str(HERE/'run.py'),sha256=sha(HERE/'run.py'),source=str(HERE/'run.py')))
  put(receipt,dict(restoration_gate='REQUIRE_RESTORED_COMPLETED',inputs=files,device_operations=0));return
 if a.phase=='build-config-receipt':
  cache=Path('C:/mdds-installed-runtime-0929/target/CMakeCache.txt');text=cache.read_text()
  if 'FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME:BOOL=ON' not in text:raise ValueError('Actual declared build option absent')
  build=root/'build-r2';shutil.copytree(root/'build',build)
  manifest=read(build/'build-manifest.json');manifest.update(configureArguments=['-DFASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME=ON'],configuration_origin=dict(path=str(cache),sha256=sha(cache),scope='DERIVED_CACHE_OPTION_NOT_ORIGINAL_BUILD_COMMAND'))
  put(build/'build-manifest.json',manifest)
  f=read(root/'DEVICE_CONTRACT.json');f['build_binding'].update(build_manifest=str(build/'build-manifest.json'),build_manifest_sha256=sha(build/'build-manifest.json'),source_state=str(build/'source-state.json'),source_state_sha256=sha(build/'source-state.json'))
  put(root/'DEVICE_CONTRACT-r2.json',f);return
 binding=read(root/'BEFORE_FIRST_DISPATCH.json');overrides={}
 revisions=sorted(root.glob('DRIVER_REVISION-*.json'))
 if revisions:overrides={p['path']:p['sha256'] for p in read(revisions[-1])['inputs']}
 for p in binding['inputs']:
  if sha(p['path'])!=overrides.get(p['path'],p['sha256']):raise ValueError('Pre-dispatch input changed '+p['path'])
 ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
 env=os.environ.copy();env.update(FAKE_HDC_SCRIPT=str(root/'local-flow/fake-hdc.py'),FAKE_HDC_PYTHON=sys.executable,FAKE_HDC_RAW_LOG=str(root/'sandbox/raw.log'),FAKE_HDC_SANDBOX=str(root/'sandbox'))
 env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules;C:/Program Files/WindowsPowerShell/Modules'
 if a.phase=='query':
  if not __import__('re').fullmatch('query-[2-9]',a.window):raise ValueError('Fresh local query name')
  w=root/'windows'/a.window
  command(a.window+'-prepare',[sys.executable,root/'executor-route-query/prepare-query.py',w,'--preflight-config',root/'preflight.local.json','--helper-provenance',root/'helper-provenance.json','--contract',root/'DEVICE_CONTRACT.json'])
  c=read(w/'window.json');env.update(FAKE_HDC_TEST_RUN=c['run'],FAKE_HDC_WINDOW_ROOT=str(w))
  command(a.window+'-launch',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',root/'executor-route-query/launch.ps1','-WindowDirectory',w],env,120)
 elif a.phase=='data':
  if not __import__('re').fullmatch('data-[2-9]',a.data_window):raise ValueError('Fresh data window')
  w=root/'windows'/a.data_window;query=root/'windows'/a.window
  build=root/('build-r2' if (root/'build-r2').exists() else 'build');contract=root/('DEVICE_CONTRACT-r2.json' if (root/'DEVICE_CONTRACT-r2.json').exists() else 'DEVICE_CONTRACT.json')
  command(a.data_window+'-prepare',[sys.executable,root/'executor/prepare-window.py',a.scenario,'--build',build,'--destination',w,'--preflight',root/'preflight.local.json','--route',query/'PRIVATE_ROUTE_BINDINGS.json','--contract',contract])
  c=read(w/'window.json');env.update(FAKE_HDC_TEST_RUN=c['run'],FAKE_HDC_WINDOW_ROOT=str(w))
  command(a.data_window+'-run',[ps,'-NoLogo','-NoProfile','-NonInteractive','-File',root/'executor/run-window.ps1','-WindowDirectory',w],env,240)
if __name__=='__main__':main()
