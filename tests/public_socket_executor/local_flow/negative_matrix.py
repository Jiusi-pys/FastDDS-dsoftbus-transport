"""Full formal-entry local negatives. Separate test cases, never cleanup escape.

Every case invokes materialize/init -> actual query/cleanup/postrun -> installed
prepare/run-window. Each test's failed window/claims are retained. No device API.
"""
import argparse,concurrent.futures,json,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;REPO=HERE.parents[2]
FAULTS=['wrong_main','wrong_run','wrong_side','wrong_pid','b_not_ready','missing_query_output','missing_take','wrong_writer','missing_runtime_close','empty_hash','missing_recv','wrong_load','early_collector','metadata_not_restored']
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def one(fault,verify_existing=False):
 root=REPO/'.artifacts/installed-runtime-device'/('local-negative-20260930-'+fault);evidence=root.with_suffix('.test.json');cmds=[]
 def invoke(phase):
  argv=[sys.executable,str(HERE/'run.py'),'--root',str(root),'--phase',phase]
  if phase=='init':argv+=['--fault',fault]
  p=subprocess.run(argv,capture_output=True,cwd=REPO,timeout=150)
  receipt={'argv':argv,'exit':p.returncode,'stdout':p.stdout.decode(errors='replace'),'stderr':p.stderr.decode(errors='replace')};cmds.append(receipt);return p.returncode
 if not verify_existing:assert invoke('init')==0
 query_rc=invoke('query') if not verify_existing else int(read(root/'local-command-receipts/query-2-launch.result.json')['exit_code'])
 if fault=='missing_query_output':
  assert query_rc!=0;w=root/'windows/query-2';assert not (w/'PRIVATE_ROUTE_BINDINGS.json').exists();assert read(w/'window-summary.json')['attempted_grants']==['A','B'];assert read(w/'A/final-guard.json')['Passed'] is True
 else:
  assert query_rc==0,cmds[-1]
  if not verify_existing:assert invoke('data')!=0,'Negative incorrectly passed'
  else:assert read(root/'local-command-receipts/data-2-run.result.json')['exit_code']!=0
  w=root/'windows/data-2'
  summary=read(w/'window-summary.json')
  if fault=='metadata_not_restored':
   assert summary['cleanup_failures'];assert not (w/'A/final-guard.json').exists() or read(w/'A/final-guard.json')['Passed'] is False
   assert read(root/'sandbox/A/metadata.json')['mode']=='600'
   # Same environment is blocked after actual mutation fails restoration.
   p=subprocess.run([sys.executable,str(root/'executor-route-query/prepare-query.py'),str(root/'windows/blocked-next-query'),'--preflight-config',str(root/'preflight.local.json'),'--helper-provenance',str(root/'helper-provenance.json'),'--contract',str(root/'DEVICE_CONTRACT.json')],capture_output=True,timeout=30,cwd=REPO)
   assert p.returncode!=0 and not (root/'windows/blocked-next-query').exists();cmds.append(dict(scope='SAME_ENVIRONMENT_UNRESTORED_SUCCESSOR_DENIAL',exit=p.returncode,stderr=p.stderr.decode(errors='replace')))
  if fault in ('wrong_main','wrong_run','wrong_side','wrong_pid'):
   result=read(w/'result.json');assert result['B'] is None,'Invalid A main started B'
  if fault=='b_not_ready':assert not read(w/'result.json')['controller']['released'],'B not ready released connect'
  if fault=='missing_take':assert 'INCOMPLETE_SAMPLE' in read(w/'result.json')['controller']['first_failure']
  if fault=='wrong_writer':assert 'SAMPLE_SOURCE' in read(w/'result.json')['controller']['first_failure']
  if fault=='missing_runtime_close':assert 'LIFECYCLE' in read(w/'result.json')['controller']['first_failure']
  if fault=='early_collector':assert read(w/'result.json')['controller']['first_failure']
  if fault in ('empty_hash','missing_recv','wrong_load'):assert summary['first_failure'] or read(w/'load-collection-summary.json')['first_failure']
  if fault not in ('empty_hash','metadata_not_restored'):
   assert all(read(w/s/'final-guard.json')['Passed'] for s in 'AB'),'Cleanup did not restore tokens'
 if evidence.exists():raise ValueError('Never overwrite completed test verdict')
 evidence.write_text(json.dumps(dict(fault=fault,status='PASS_EXPECTED_FAILURE',root=str(root),commands=cmds,verification_of_existing_execution=verify_existing),indent=2));return fault
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--fault',choices=FAULTS);p.add_argument('--verify-existing',action='store_true');a=p.parse_args()
 faults=[a.fault] if a.fault else FAULTS
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
  for name in pool.map(lambda f:one(f,a.verify_existing),faults):print('PASS '+name,flush=True)
