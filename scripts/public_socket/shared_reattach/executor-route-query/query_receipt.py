"""Read-only completed-query evidence validation. No driver, claim or dispatch API."""
from pathlib import Path
import hashlib,json,importlib.util

def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def need(ok,label):
 if not ok:raise ValueError(label)
def verify_completed_query(window,archive_manifest=None):
 window=Path(window).resolve();task=Path(__file__).resolve().parent.parent;files={}
 def raw(p):
  p=Path(p).resolve();data=p.read_bytes();files[str(p)]=hashlib.sha256(data).hexdigest();return data
 def obj(p):
  x=json.loads(raw(p).decode('utf-8-sig'));need(isinstance(x,dict),'Receipt must be object');return x
 cfg=obj(window/'window.json');need(window.parent==task/'windows' and Path(cfg['window_root']).resolve()==window,'Source query window mismatch')
 spec=importlib.util.spec_from_file_location('completed_contract',task/'executor/task_contract.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
 proof=cfg['task_contract'];freeze=Path(proof['freeze_path']).resolve();need(freeze.parent==task and proof['task_root']==str(task),'Foreign source contract')
 frozen=obj(freeze);c.materialization_record(frozen);c.verify_shared_guard(cfg,frozen);raw(task/'MATERIALIZATION.json');c.verify_private_binding(cfg);raw(task/'PRIVATE_CONFIG.json');need(frozen.get('private_config_sha256')==digest(task/'PRIVATE_CONFIG.json'),'Original private configuration mismatch');need(files[str(freeze)]==proof['freeze_sha256'] and frozen['task_root']==str(task),'Original contract hash/identity mismatch')
 local=frozen.get('scope')==c.INSTALLED_SCOPE and frozen.get('local_contract_test') is True
 if local:
  c.verify_local_driver(frozen)
  need(cfg['targets']=={'A':'SIM-A','B':'SIM-B'} and Path(cfg['hdc_path']).name=='fake-hdc.exe','Local query must use synthetic driver/targets')
  claim=obj(task/'local-test-claims'/('query-'+cfg['run']+'.json'))
  need(claim==dict(scope=c.INSTALLED_SCOPE,local_contract_test=True,run=cfg['run'],window=str(window),kind='LOCAL_QUERY_DISPATCH_INTENT',device_authority=False),'Local query claim mismatch')
 else:need(frozen['execution_admitted'] is True,'Source contract not admitted')
 need(frozen['plan_sha256']==proof['plan_sha256'],'Source plan mismatch')
 c.assert_fresh_run(cfg['run']);need(cfg['scenario']=='readonly-directory-query' and cfg['targets']==c.TARGETS and cfg['token']==c.TOKENS,'Original query target/scenario mismatch')
 need(cfg['system_library_dirs']==c.LIBDIRS and cfg['remote']=={s:f"/data/local/tmp/mdds_dds_{cfg['run']}_{s}" for s in 'AB'},'Query command identity mismatch')
 need(cfg['permission_helper']==frozen['permission_helper'],'Original permission helper mismatch')
 pre=obj(cfg['preflight_config']);need(files[str(Path(cfg['preflight_config']).resolve())]==cfg['preflight_config_sha256'] and pre['platform']==cfg['platform'] and pre['eligible_for_executor_gate'] is True,'Original preflight mismatch')
 raw(cfg['helper_provenance']);need(files[str(Path(cfg['helper_provenance']).resolve())]==cfg['helper_provenance_sha256'],'Helper provenance mismatch')
 archive={}
 if archive_manifest:
  rows=json.loads(raw(archive_manifest).decode('utf-8-sig'));need(isinstance(rows,list),'Archive mapping type')
  for row in rows:
   original=str(Path(row['original_path']).resolve());need(original not in archive,'Duplicate archive mapping');archive[original]=row
 archived_inputs={str((task.parents[2]/e['path']).resolve()) for e in frozen.get('execution_files',[])}
 def original_hash(path,expected):
  path=Path(path).resolve();row=archive.get(str(path))
  if archive_manifest and str(path) in archived_inputs:
   need(row is not None and row['sha256']==expected,'Missing/mismatched original executor archive');actual=raw(row['archive_path'])
  else:actual=raw(path)
  need(hashlib.sha256(actual).hexdigest()==expected,'Original execution input drift')
 for row in frozen.get('execution_files',[]):original_hash(task.parents[2]/row['path'],row['sha256'])
 if archive_manifest:need(len(archive)==len(frozen['execution_files']),'Archive membership mismatch')
 original_hash(task/'executor/task_contract.py',cfg['contract_validator_sha256'])
 for name,sha in cfg['executor_sha256'].items():original_hash(task/'executor-route-query'/name,sha)
 for name,sha in cfg['shared_script_sha256'].items():original_hash(Path(cfg['repo'])/'scripts'/name,sha)
 binding=frozen['build_binding'];raw(binding['source_state']);need(files[str(Path(binding['source_state']).resolve())]==binding['source_state_sha256'],'Original source manifest changed')
 if not local:need(json.loads(Path(binding['source_state']).read_text())==frozen['source'],'Original source list mismatch')
 else:
  state=json.loads(Path(binding['source_state']).read_text())
  need(bool(state.get('_local_fixture_note')) and bool(state.get('consumer_source')),'Declared derived local source record required')
 for side in 'AB':
  for art in cfg['package_artifacts']:need(digest(window/'helpers'/art['name'])==art['sha256'],'Query helper package changed');raw(window/'helpers'/art['name'])
 purpose=cfg.get('query_for_cycles')
 if frozen['scope']==c.OBSERVED_SCOPE:
  need(cfg.get('work_package')==frozen['scope'] and type(purpose) is int and purpose in (10,100),'Completed query purpose invalid')
  claim=obj(c.claim_path('query',purpose));need(claim.get('scope')==frozen['scope'] and type(claim.get('cycles')) is int and claim['cycles']==purpose and claim.get('run')==cfg['run'] and claim.get('window')==str(window) and claim.get('kind')=='QUERY_DISPATCH_INTENT','Completed query claim mismatch')
 if frozen['scope']==c.INSTALLED_SCOPE and not local:
  c.verify_installed_device_mode(frozen,cfg);c.verify_installed_query_claim(cfg);raw(c.installed_claim_path('query',cfg['installed_attempt']))
 need(bool(raw(window/'EXECUTION_STARTED').strip()),'Execution receipt missing')
 summary=obj(window/'window-summary.json');need(summary.get('first_failure') is None and summary.get('cleanup_failures')==[] and summary.get('attempted_grants')==['A','B'],'Query recovery incomplete')
 for s in 'AB':
  q=window/s/'directory-query';need(raw(q/'exit-code.txt').decode('utf-8-sig').strip()=='0','Local query exit failed')
  need(bool(raw(q/'stdout.log').strip()),'Query stdout incomplete');raw(q/'stderr.log')
  argv=json.loads(raw(q/'command.json').decode('utf-8-sig'));root=cfg['remote'][s];loader=':'.join([root]+cfg['system_library_dirs'])
  command=f"env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH={loader} timeout 10 {root}/softbus_record_probe --list-nodes --expected-token {cfg['token'][s]} --preflight-confirmed; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"
  need(isinstance(argv,list) and len(argv)==5 and Path(argv[0]).resolve()==Path(cfg['hdc_path']).resolve() and argv[1:]==['-t',cfg['targets'][s],'shell',command],'Query executable/argv target or run mismatch')
  for name in ('granted-guard.json','ownership-guard.json','final-snapshot-guard.json'):
   need(obj(window/s/name).get('Passed') is True,'Required guard failed')
  g=obj(window/s/'final-guard.json');need(all(g.get(k) is True for k in ('Passed','byte_match','metadata_match','process_absent')) and g.get('Phase')=='Revoked' and g.get('service_permission')=='DENIED','Final restoration incomplete')
  for phase in ('before','after'):need(Path(g['metadata_'+phase]['evidence']).resolve()==window/s/('metadata-'+phase),'Foreign metadata evidence')
  need(raw(window/s/'baseline.json')==raw(window/s/'after.json'),'Restoration bytes mismatch')
 return dict(schema=2,source_query_window=str(window),query_run=cfg['run'],query_for_cycles=purpose,query_pair_consumed=(frozen['scope']==c.OBSERVED_SCOPE or (frozen['scope']==c.INSTALLED_SCOPE and not local)),accounting_scope='ORIGINAL_QUERY_ONLY',original_query_dds='NOT_RUN',replay_side_effects='NO_DEVICE_OR_CLAIM_MUTATION',source_contract=proof,source_executor=frozen.get('execution_files',[]),source_state_sha256=binding['source_state_sha256'],archive_manifest=str(Path(archive_manifest).resolve()) if archive_manifest else None,original_files_sha256=files,replay_validator_sha256={str(Path(__file__).resolve()):digest(__file__),str(task/'executor-route-query/route_query.py'):digest(task/'executor-route-query/route_query.py'),str(task/'executor/task_contract.py'):digest(task/'executor/task_contract.py')})
