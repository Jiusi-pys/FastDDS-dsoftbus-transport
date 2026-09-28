"""Task-local experiment admission receipt. Not SDK capability or user permission."""
from pathlib import Path
import json,hashlib,re,argparse,importlib.util
TASK_ROOT=Path(__file__).resolve().parent.parent
def read_private_config():
 value=json.loads((TASK_ROOT/'PRIVATE_CONFIG.json').read_text(encoding='utf-8'))
 if not isinstance(value,dict):raise ValueError('Private configuration object required')
 return value
PRIVATE=read_private_config()
TARGETS=PRIVATE['targets']
TOKENS=PRIVATE['tokens']
def verify_private_binding(c):
 sha=digest(TASK_ROOT/'PRIVATE_CONFIG.json')
 need(c.get('private_config_sha256')==sha and c.get('hdc_path')==PRIVATE['hdc_path'] and c.get('permission_baseline_sha256')==PRIVATE['permission_baseline_sha256'],'Private task input changed')

CASES=['SHARED_W1','SHARED_W2','F1M']
LIBDIRS=['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64']
OBSERVED_SCOPE='N10_OBSERVED_RETRY_AND_CONDITIONAL_N100'
FINAL_N100_AUTH='ONE_QUERY100_AND_USE_EXISTING_CONDITIONAL_N100'
OBSERVED_LIMITS=dict(canonical_windows=2,impact_regressions=0,repair_retries=0,total_attempts=2,n10_attempts=1,n100_attempts=1,query_pairs_per_window=1,max_record_bytes=8192)

def claim_path(kind,cycles):
 need(kind in ('data','query') and type(cycles) is int and cycles in (10,100),'Invalid claim purpose')
 return TASK_ROOT/'observed-retry'/'attempt-ledger'/f'{kind}-N{cycles}.json'

MATERIALIZED_FILES=('executor-route-query/launch.ps1', 'executor-route-query/prepare-query.py', 'executor-route-query/query_receipt.py', 'executor-route-query/route_query.py', 'executor-route-query/run-window.ps1', 'executor/bounded-output.cjs', 'executor/capture-load.sh', 'executor/load_origin.py', 'executor/observe-native.cjs', 'executor/prepare-window.py', 'executor/private_config.py', 'executor/record-pair.cjs', 'executor/route_binding.py', 'executor/run-window.ps1', 'executor/runner-template.cjs', 'executor/shared-control.cjs', 'executor/task_contract.py')
def materialization_record(frozen):
 p=TASK_ROOT/'MATERIALIZATION.json';need(digest(p)==frozen.get('materialization_sha256'),'Materialization descriptor missing/changed')
 record=json.loads(p.read_text(encoding='utf-8'));need(record.get('scope')=='LOCAL_MATERIALIZATION_ONLY_NO_EXECUTION_AUTHORITY' and record.get('task')==str(TASK_ROOT) and record.get('repo')==str(TASK_ROOT.parents[2]) and record.get('private_config_sha256')==frozen.get('private_config_sha256'),'Materialization identity mismatch')
 rows=record.get('files',[]);names=[e['destination'].replace(chr(92),'/') for e in rows];need(len(names)==len(set(names)) and set(names)==set(MATERIALIZED_FILES),'Materialized dependency membership mismatch')
 entries=frozen.get('execution_files',[]);mapped={str((TASK_ROOT.parents[2]/e['path']).resolve()):e['sha256'] for e in entries};need(len(mapped)==len(entries),'Duplicate executor binding')
 required={str((TASK_ROOT/e['destination']).resolve()):e['sha256'] for e in rows}
 task_entries={p for p in mapped if Path(p).is_relative_to(TASK_ROOT/'executor') or Path(p).is_relative_to(TASK_ROOT/'executor-route-query')}
 need(task_entries==set(required) and all(mapped.get(p)==sha for p,sha in required.items()),'Required materialized executor omitted or changed')
 external=record.get('external_files',[]);names=[e.get('path') for e in external];need(len(names)==2 and set(names)=={'scripts/NativePermissionGuard.ps1','scripts/public_socket/analyze_shared.py'},'Required external execution membership mismatch')
 for e in external:
  path=str(TASK_ROOT.parents[2]/e['path']);need(mapped.get(path)==e.get('sha256') and isinstance(e.get('sha256'),str),'Required external execution dependency omitted or changed');required[path]=e['sha256']
 return required

def verify_shared_guard(c,frozen):
 required=materialization_record(frozen)
 need(c.get('shared_script_sha256')=={'NativePermissionGuard.ps1':required[str(TASK_ROOT.parents[2]/'scripts/NativePermissionGuard.ps1')]},'Window must bind the exact reviewed external guard')
 need(c.get('analyzer_sha256')==required[str(TASK_ROOT.parents[2]/'scripts/public_socket/analyze_shared.py')],'Window formal analyzer not bound')

def verify_materialized_execution(frozen):
 for p,sha in materialization_record(frozen).items():need(digest(p)==sha,'Materialized execution bytes changed')
 spec=importlib.util.spec_from_file_location('bound_private_config',TASK_ROOT/'executor/private_config.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.validate_profile(PRIVATE,TASK_ROOT,TASK_ROOT.parents[2])

def require_accepted_n10(frozen,proof):
 need(frozen.get('final_n100_authorization')==FINAL_N100_AUTH and not frozen.get('resume_authorization'),'Invalid final N100 authorization')
 path=Path(frozen['n10_acceptance_path']).resolve();need(path.is_relative_to(TASK_ROOT) and isinstance(proof,dict) and proof==dict(path=str(path),sha256=digest(path)) and digest(path)==frozen['n10_review_sha256'],'Accepted N10 review missing/changed')
 review=json.loads(path.read_text());need(review.get('review_kind')=='INDEPENDENT_N10_EVIDENCE_REVIEW' and review.get('overall')=='PASS' and review.get('accepted') is True and review.get('pass') is True,'N10 review not accepting')
 claim=json.loads(claim_path('data',10).read_text());window=Path(claim['window']).resolve();need(claim.get('scope')==OBSERVED_SCOPE and type(claim.get('cycles')) is int and claim['cycles']==10 and claim.get('kind')=='DATA_DISPATCH_INTENT' and window.parent==TASK_ROOT/'windows' and review.get('run')==claim['run'] and review.get('window')==str(window),'Accepted N10 claim identity mismatch')
 cfg=json.loads((window/'window.json').read_text());need(cfg.get('work_package')==OBSERVED_SCOPE and cfg.get('cycles')==10 and cfg.get('scenario')=='SHARED_W1' and cfg.get('run')==review['run'] and digest(window/'window.json')==review.get('window_sha256'),'Accepted N10 window mismatch')
 original=frozen['n10_original_contract'];op=Path(original['path']).resolve();need(op.is_relative_to(TASK_ROOT) and digest(op)==original['sha256']==review.get('contract_sha256') and cfg['task_contract']['freeze_path']==str(op) and cfg['task_contract']['freeze_sha256']==original['sha256'],'Accepted N10 contract mismatch')
 old=json.loads(op.read_text());need(old['execution_files']==review['execution_files'],'N10 original executor mismatch')
 need(review['main_sha256']==cfg['main_sha256']==old['build_binding']['main_sha256']==frozen['build_binding']['main_sha256'] and review['source_state_sha256']==cfg['source_state_sha256']==old['build_binding']['source_state_sha256']==frozen['build_binding']['source_state_sha256'],'N10 runtime/package mismatch')
 oldmap={e['path']:e['sha256'] for e in old['execution_files']};newmap={e['path']:e['sha256'] for e in frozen['execution_files']};need(len(oldmap)==len(old['execution_files']) and len(newmap)==len(frozen['execution_files']) and oldmap.keys()==newmap.keys(),'Executor membership changed')
 delta=[dict(path=p,original_sha256=v,current_sha256=newmap[p],purpose='admission-only') for p,v in oldmap.items() if v!=newmap[p]]
 need(delta==frozen['executor_delta'] and len(delta)==1 and (TASK_ROOT.parents[2]/delta[0]['path']).resolve()==TASK_ROOT/'executor/task_contract.py','Unreviewed executor delta')
 for p,sha in newmap.items():need(digest(TASK_ROOT.parents[2]/p)==sha,'Current executor drift')
 archive=frozen['n10_executor_archive'];ap=Path(archive['path']).resolve();need(ap.is_relative_to(TASK_ROOT) and digest(ap)==archive['sha256'],'N10 archive mapping changed')
 rows=json.loads(ap.read_text());mapping={str(Path(e['original_path']).resolve()):e for e in rows};oldabs={str((TASK_ROOT.parents[2]/p).resolve()):sha for p,sha in oldmap.items()};need(len(mapping)==len(rows) and mapping.keys()==oldabs.keys(),'N10 archive membership mismatch')
 for p,sha in oldabs.items():need(mapping[p]['sha256']==sha and digest(mapping[p]['archive_path'])==sha,'N10 original executor archive drift')
 hashes=review.get('evidence_sha256',{});required=['window.json','result.json','cycle-analysis.json','window-summary.json','load-collection-summary.json','A.raw.stdout.log','B.raw.stdout.log','A/actual-load-origin.json','B/actual-load-origin.json','A/final-guard.json','B/final-guard.json'];need(all(str(window/n) in hashes for n in required),'N10 review evidence incomplete')
 for p,sha in hashes.items():
  actual=mapping[str(Path(p).resolve())]['archive_path'] if str(Path(p).resolve()) in mapping else p
  need(digest(actual)==sha,'N10 reviewed evidence changed')
 checks=('exits','all_cycles','participant_counts','sample_ledger','native_continuity','control_termination','live_load','postrun','guards','collector_drained','no_watchdog_substitution');need(all(review.get('checks',{}).get(k) is True for k in checks),'N10 review checks incomplete')
 result=json.loads((window/'result.json').read_text());analysis=json.loads((window/'cycle-analysis.json').read_text());summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'));loads=json.loads((window/'load-collection-summary.json').read_text(encoding='utf-8-sig'))
 need(result.get('run')==review['run'] and result.get('scenario')=='SHARED_W1' and result.get('pass') is True and result.get('controller',{}).get('pass') is True and result.get('timedOut') is False and result.get('activeCollectors')==[] and result.get('collectionCleanup')=='HDC_STREAMS_CLOSED','Accepted N10 execution incomplete')
 need(all(result.get(s,{}).get('remoteExit')==0 and result[s].get('hdcExit')==0 and result[s].get('signal') is None for s in 'AB'),'N10 endpoint exit failed')
 need(analysis.get('pass_') is True and analysis.get('run')==review['run'] and analysis.get('cycles')==10 and analysis.get('writes')==analysis.get('reader_samples')==300 and analysis.get('send_policy')=='selected_retirement_v1','N10 formal ledger incomplete')
 need('first_failure' in summary and summary['first_failure'] is None and 'first_failure' in loads and loads['first_failure'] is None and loads.get('failures')==[],'N10 postrun failure')
 need(all(json.loads((window/side/'actual-load-origin.json').read_text()).get('complete') is True for side in 'AB'),'N10 live loading incomplete');require_restored(window)

def require_new_n10(frozen,proof):
 if 'final_n100_authorization' in frozen:return require_accepted_n10(frozen,proof)
 path=Path(frozen['n10_acceptance_path']).resolve()
 need(path==TASK_ROOT/'observed-retry'/'N10_ACCEPTANCE.json','Foreign acceptance location')
 need(isinstance(proof,dict) and Path(proof.get('path','')).resolve()==path and digest(path)==proof.get('sha256'),'Missing/stale N10 independent acceptance')
 a=json.loads(path.read_text());need(a.get('decision')=='PASS' and a.get('scope')==OBSERVED_SCOPE and a.get('independent_review_closed') is True,'N10 review not accepting')
 claim=json.loads(claim_path('data',10).read_text());window=Path(claim['window']).resolve();need(window.parent==TASK_ROOT/'windows' and a.get('window')==str(window) and a.get('run')==claim['run'],'Acceptance wrong N10')
 cfg=json.loads((window/'window.json').read_text());need(cfg.get('work_package')==OBSERVED_SCOPE and cfg.get('cycles')==10 and cfg.get('run')==a['run'],'Acceptance is not this grant N10')
 need(a.get('main_sha256')==cfg['main_sha256']==frozen['build_binding']['main_sha256'] and a.get('source_state_sha256')==cfg['source_state_sha256']==frozen['build_binding']['source_state_sha256'],'Acceptance package mismatch')
 need(a.get('execution_files')==frozen['execution_files'],'Acceptance executor mismatch')
 report=a.get('review_report',{});rp=Path(report.get('path','')).resolve();need(rp.is_relative_to(TASK_ROOT) and digest(rp)==report.get('sha256'),'Independent review receipt missing/changed')
 review=json.loads(rp.read_text());need(review.get('verdict')=='PASS' and review.get('run')==a['run'] and review.get('window')==str(window) and review.get('review_kind')=='INDEPENDENT_N10_EVIDENCE_REVIEW','Independent N10 review not accepting')
 checks=('exits','all_cycles','participant_counts','sample_ledger','native_continuity','control_termination','live_load','postrun','guards','collector_drained','no_watchdog_substitution')
 need(all(review.get('checks',{}).get(k) is True for k in checks),'Independent N10 review incomplete')
 required=['window.json','result.json','cycle-analysis.json','window-summary.json','load-collection-summary.json','A.raw.stdout.log','B.raw.stdout.log','A/actual-load-origin.json','B/actual-load-origin.json','A/final-guard.json','B/final-guard.json']
 hashes=a.get('evidence_sha256',{});need(all(hashes.get(n)==digest(window/n) for n in required),'N10 accepted evidence changed/missing')
 need(review.get('evidence_sha256')==hashes,'Independent N10 review evidence mismatch')
 need(all(review.get(k)==a.get(k) for k in ('main_sha256','source_state_sha256','execution_files')),'Independent N10 review identity mismatch')
 result=json.loads((window/'result.json').read_text());analysis=json.loads((window/'cycle-analysis.json').read_text());summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'));loads=json.loads((window/'load-collection-summary.json').read_text(encoding='utf-8-sig'))
 need(result.get('run')==a['run'] and result.get('scenario')=='SHARED_W1' and result.get('pass') is True and result.get('controller',{}).get('pass') is True and result.get('timedOut') is False and result.get('activeCollectors')==[] and result.get('collectionCleanup')=='HDC_STREAMS_CLOSED','N10 execution incomplete')
 need(all(result.get(s,{}).get('remoteExit')==0 and result[s].get('hdcExit')==0 and result[s].get('signal') is None for s in ('A','B')),'N10 endpoint exit failed')
 need(analysis.get('pass_') is True and analysis.get('run')==a['run'] and analysis.get('cycles')==10 and analysis.get('writes')==analysis.get('reader_samples')==300 and analysis.get('send_policy')=='selected_retirement_v1','N10 formal ledger incomplete')
 need('first_failure' in summary and summary['first_failure'] is None and 'first_failure' in loads and loads['first_failure'] is None and loads.get('failures')==[],'N10 control/load failure')
 need(all(json.loads((window/s/'actual-load-origin.json').read_text()).get('complete') is True for s in ('A','B')),'N10 live loading incomplete')
 require_restored(window)

def verify_observed_budget(c,frozen,query=False):
 cycles=c.get('query_for_cycles') if query else c.get('cycles')
 need(c.get('work_package')==OBSERVED_SCOPE and type(cycles) is int and cycles in (10,100),'Wrong supplemental scope/purpose')
 need('retry_evidence' not in c,'Supplemental grant has no repair retries')
 if 'final_n100_authorization' in frozen:
  need(frozen['final_n100_authorization']==FINAL_N100_AUTH and not frozen.get('resume_authorization') and cycles==100,'Final authorization permits only query100/N100')
 if frozen.get('resume_authorization'):
  need(frozen['resume_authorization']=='REPAIR_REPLAY_AND_RESUME_UNUSED_DATA','Invalid resume authorization')
  need(not query,'No new query permitted by resume authorization')
  need(cycles==10,'No legal query100 evidence; no new query authorization')
 if cycles==100:require_new_n10(frozen,c.get('n10_acceptance'))
 kind='query' if query else 'data';need(not claim_path(kind,cycles).exists(),'Supplemental quota already claimed')
 current=Path(c['window_root']).resolve()
 for p in (TASK_ROOT/'windows').glob('*/window.json'):
  if p.parent==current:continue
  old=json.loads(p.read_text(encoding='utf-8-sig'))
  if old.get('work_package')!=OBSERVED_SCOPE:continue
  if not query and old.get('cycles')==cycles and (p.parent/'DATA_ATTEMPT_STARTED').exists():need(False,'Supplemental data attempt already dispatched')
 if not query:
  qclaim=json.loads(claim_path('query',cycles).read_text());qwindow=Path(qclaim['window']).resolve();proof=c.get('route_binding',{})
  need(qclaim.get('scope')==OBSERVED_SCOPE and type(qclaim.get('cycles')) is int and qclaim['cycles']==cycles and qwindow.parent==TASK_ROOT/'windows' and proof.get('query_run')==qclaim['run'],'Data window not bound to its one current query')
  if frozen.get('resume_authorization'):
   bridge=frozen['query_resume'];private=Path(proof.get('private_receipt','')).resolve()
   need(bridge['query_run']==qclaim['run'] and bridge['source_query_window']==str(qwindow) and bridge['query_for_cycles']==cycles and bridge['claim_sha256']==digest(claim_path('query',cycles)),'Resume original query/claim mismatch')
   need(str(private)==bridge['private_receipt'] and digest(private)==bridge['private_sha256'] and proof.get('source_query_window')==str(qwindow) and proof.get('query_for_cycles')==cycles,'Resume replay/purpose mismatch')
   spec=importlib.util.spec_from_file_location('resume_route_binding',TASK_ROOT/'executor/route_binding.py');route=importlib.util.module_from_spec(spec);spec.loader.exec_module(route);route.verify_frozen(proof,c['peer'])
   qconfig=json.loads((qwindow/'window.json').read_text());need(c['platform']==qconfig['platform'] and c['targets']==qconfig['targets'],'Resume platform/target invalidator')
  else:need(Path(proof.get('private_receipt','')).resolve().parent==qwindow,'Data route belongs to another query')
  qcfg=json.loads((qwindow/'window.json').read_text());need(qcfg.get('work_package')==OBSERVED_SCOPE and qcfg.get('query_for_cycles')==cycles and qcfg.get('run')==qclaim['run'],'Query purpose/scope mismatch')
  require_restored(qwindow)

def claim_query(c):
 execution_admission(c['task_contract']);verify_query(c)
 need(c.get('work_package')==OBSERVED_SCOPE,'Claim only for supplemental grant')
 p=claim_path('query',c['query_for_cycles']);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('x',encoding='utf-8') as stream:json.dump(dict(scope=OBSERVED_SCOPE,cycles=c['query_for_cycles'],run=c['run'],window=str(Path(c['window_root']).resolve()),kind='QUERY_DISPATCH_INTENT',remote_execution='UNOBSERVED_AT_CLAIM'),stream)
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def need(value,message):
 if not value:raise ValueError(message)
def assert_fresh_run(run):need(isinstance(run,str) and re.fullmatch('[0-9a-f]{16}',run),'Invalid run')
def validate_shape(c):
 verify_private_binding(c)
 cycles=c.get('cycles',1);need(type(cycles) is int and 1<=cycles<=100,'Invalid cycle count')
 if cycles>1:need((c['scenario'],cycles) in [('SHARED_W1',10),('SHARED_W2',100)],'Unauthorized cycle/order pair')
 assert_fresh_run(c['run']);need(c['scenario'] in CASES and c['delete_first']==('A' if c['scenario']=='SHARED_W2' else 'B'),'Wrong scenario/deletion order')
 need(c['expected_writes']==({'A':3,'B':0} if c['scenario']=='F1M' else {'A':15*cycles,'B':15*cycles}),'Wrong directional sample contract')
 need(c['targets']==TARGETS and c['token']==TOKENS,'Unregistered board/token')
 need(c['remote']=={s:f"/data/local/tmp/mdds_dds_{c['run']}_{s}" for s in ['A','B']},'Wrong exclusive path')
 need(c['system_library_dirs']==LIBDIRS,'Undeclared loader paths')
 need(c['record_count']==(3 if c['scenario']=='F1M' else 30*cycles) and c['payload_bytes']==(1048576 if c['scenario']=='F1M' else 32) and c['max_record_bytes']==8192 and c['timeout_seconds']==(180+330*cycles if cycles>1 else 180) and c['stdout_budget']==25165824,'Expanded frozen budget')
 need(set(c['peer'])=={'A','B'} and all(re.fullmatch('[0-9a-f]{64}',v) for v in c['peer'].values()),'No current peer binding')
def load(path=None):
 p=(Path(path) if path else TASK_ROOT/'DEVICE_CONTRACT.json').resolve();need(p.parent==TASK_ROOT and re.fullmatch(r'DEVICE_CONTRACT(?:-r(?:[2-9]|[1-9][0-9]+))?\.json',p.name),'Foreign contract')
 x=json.loads(p.read_text());need(x.get('private_config_sha256')==digest(TASK_ROOT/'PRIVATE_CONFIG.json'),'Private configuration not bound by contract');need(x['task_root']==str(TASK_ROOT) and x['scope'] in ('SHARED_LIFECYCLE_EGRESS_AND_CAPTURE_CLOSURE','NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION',OBSERVED_SCOPE),'Wrong task')
 verify_materialized_execution(x)
 if x['scope']==OBSERVED_SCOPE:
  need(x.get('authorization')=='ONE_NEW_N10_AND_CONDITIONAL_N100' and x['limits']==OBSERVED_LIMITS,'Supplemental authorization/limits')
  need(x['plan_sha256']==digest(TASK_ROOT/'OBSERVED_RETRY_CONTRACT.md'),'Supplemental plan drift')
  if 'final_n100_authorization' in x:need(x['final_n100_authorization']==FINAL_N100_AUTH and not x.get('resume_authorization'),'Invalid final N100 authorization')
  return {'task_root':str(TASK_ROOT),'freeze_path':str(p),'freeze_sha256':digest(p),'plan_sha256':x['plan_sha256']}
 cycling=x['scope']=='NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION'
 need(x['plan_sha256']==digest(TASK_ROOT/('CYCLE_CONTRACT.md' if cycling else 'CASE_MATRIX_R6.md')),'Plan drift')
 need(x['limits']==({'canonical_windows':2,'impact_regressions':0,'repair_retries':1,'total_attempts':3,'max_record_bytes':8192} if cycling else {'canonical_windows':3,'impact_regressions':1,'repair_retries':3,'total_attempts':6,'max_record_bytes':8192}),'Wrong limits')
 return {'task_root':str(TASK_ROOT),'freeze_path':str(p),'freeze_sha256':digest(p),'plan_sha256':x['plan_sha256']}
def execution_admission(proof):
 need(proof==load(proof['freeze_path']),'Contract drift');x=json.loads(Path(proof['freeze_path']).read_text());need(x['execution_admitted'] is True,'Independent gates incomplete')
 need({e['role'] for e in x['admission_evidence']} >= {'host','asan','review','target','executor_tests'},'Missing evidence roles')
 for row in x['admission_evidence']:
  p=Path(row['path']).resolve();need(p.is_relative_to(TASK_ROOT) and digest(p)==row['sha256'],'Admission evidence changed')
 source=Path(x['build_binding']['source_state']);need(digest(source)==x['build_binding']['source_state_sha256'] and bool(x['source']) and x['source']==json.loads(source.read_text()),'Source receipt closure mismatch')
 docs=x.get('documentation_delta',{})
 need(not docs or ((x.get('resume_authorization')=='REPAIR_REPLAY_AND_RESUME_UNUSED_DATA' or x.get('final_n100_authorization')==FINAL_N100_AUTH) and set(docs)<= {'README.md','PLAN.md'}),'Invalid documentation delta')
 for e in x['source']:
  expected=e['sha256']
  if e['path'] in docs:
   need(docs[e['path']]['original_sha256']==expected,'Original documentation hash mismatch');expected=docs[e['path']]['current_sha256']
  need(digest(TASK_ROOT.parents[2]/e['path'])==expected,'Source drift')
 if x['scope']==OBSERVED_SCOPE:
  need(bool(x.get('execution_files')),'Missing current executor selection')
  for e in x['execution_files']:need(digest(TASK_ROOT.parents[2]/e['path'])==e['sha256'],'Executor selection drift')
  binding=x['build_binding'];need(digest(binding['build_manifest'])==binding['build_manifest_sha256'],'Build manifest drift')
  for e in binding['package_artifacts']:need(digest(e['source'])==e['sha256'],'Package member drift')
  need(next(e['sha256'] for e in binding['package_artifacts'] if e['name']=='public_socket_dds_experiment')==binding['main_sha256'],'Main identity drift')
  need(bool(x.get('compiled_inputs')),'Missing compile input closure')
  for e in x['compiled_inputs']:need(digest(e['path'])==e['sha256'],'Compile input drift')
def require_restored(window):
 summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'));need(summary['cleanup_failures']==[],'Prior cleanup incomplete')
 if (window/'result.json').exists():
  result=json.loads((window/'result.json').read_text());need(not result.get('timedOut') and result.get('collectionCleanup')!='UNCONFIRMED','Prior runtime/collector termination uncertain')
 for side in ['A','B']:
  g=json.loads((window/side/'final-guard.json').read_text(encoding='utf-8-sig'));need(all(g.get(k) is True for k in ['Passed','byte_match','metadata_match','process_absent']) and g.get('service_permission')=='DENIED','Prior attempt not restored')
  for phase in ['before','after']:need(Path(g['metadata_'+phase]['evidence']).resolve()==(window/side/('metadata-'+phase)).resolve(),'Foreign prior metadata receipt')
  need(digest(window/side/'baseline.json')==digest(window/side/'after.json'),'Prior raw permission bytes differ')
  for role in ['ownership-guard.json','final-snapshot-guard.json']:need(json.loads((window/side/role).read_text(encoding='utf-8-sig'))['Passed'] is True,'Prior ownership/JSON incomplete')
def verify_window(c):
 validate_shape(c);need(c['task_contract']==load(c['task_contract']['freeze_path']),'Contract changed')
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text());verify_shared_guard(c,frozen)
 if frozen['scope']=='NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION':need(c.get('work_package')==frozen['scope'] and (c['scenario'],c.get('cycles')) in [('SHARED_W1',10),('SHARED_W2',100)],'Wrong cycle work-package window')
 for key in ['build_manifest','build_manifest_sha256','source_state','source_state_sha256','main_sha256']:need(c[key]==frozen['build_binding'][key],'Package/source drift: '+key)
 need(c['package_artifacts']==frozen['build_binding']['package_artifacts'],'Member drift');need(c['permission_helper']==frozen['permission_helper'],'Helper drift');need(c['header_sha256']==frozen['header_sha256'],'Header drift')
 pre=Path(c['preflight_config']).resolve();need(pre.is_relative_to(TASK_ROOT) and digest(pre)==c['preflight_config_sha256'],'Preflight drift');expected=json.loads(pre.read_text(encoding='utf-8-sig'))
 need(c['platform']==expected['platform'] and c['required_platform_paths']==expected['required_platform_paths'] and bool(c['required_platform_paths']),'Platform/mapping allowlist drift')
 w=Path(c['window_root']).resolve();need(w.parent==TASK_ROOT/'windows','Foreign window')
 if frozen['scope']==OBSERVED_SCOPE:
  for p in (TASK_ROOT/'windows').glob('*/EXECUTION_STARTED'):
   if p.parent!=w:require_restored(p.parent)
  verify_observed_budget(c,frozen);return
 prior=[]
 for p in (TASK_ROOT/'windows').glob('*/window.json'):
  if p.parent==w:continue
  if (p.parent/'EXECUTION_STARTED').exists():
   # A window not yet restored, including auxiliary operations, blocks new work.
   require_restored(p.parent)
  if (p.parent/'DATA_ATTEMPT_STARTED').exists():
   old=json.loads(p.read_text())
   if c.get('cycles',1)==1 or old.get('work_package')=='NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION':prior.append((p.parent,old))
 if c.get('cycles')==100:
  successful=[p for p,x in prior if x.get('cycles')==10 and (p/'cycle-analysis.json').is_file() and json.loads((p/'cycle-analysis.json').read_text()).get('pass_') is True and json.loads((p/'result.json').read_text()).get('pass') is True and json.loads((p/'window-summary.json').read_text(encoding='utf-8-sig')).get('first_failure') is None and json.loads((p/'load-collection-summary.json').read_text(encoding='utf-8-sig')).get('first_failure') is None and json.loads((p/'load-collection-summary.json').read_text(encoding='utf-8-sig')).get('failures')==[]]
  need(bool(successful),'N100 requires successful restored N10')
 need(len(prior)<frozen['limits']['total_attempts'],'Data-attempt limit reached')
 repeated=[(p,x) for p,x in prior if x['scenario']==c['scenario']]
 if repeated:
  row=c.get('retry_evidence');need(isinstance(row,dict),'Repeated case requires concrete repair')
  path=Path(row['path']).resolve();need(path.is_relative_to(TASK_ROOT) and digest(path)==row['sha256'],'Repair receipt drift')
  repair=json.loads(path.read_text());need(repair.get('root_cause') and repair.get('fix') and repair.get('affected_validation') and repair.get('review'),'Unsubstantiated repair')
  need(sum('retry_evidence' in x for _,x in prior)<frozen['limits']['repair_retries'],'Repair retry limit reached')
def verify_query(c):
 verify_private_binding(c)
 need(c['task_contract']==load(c['task_contract']['freeze_path']),'Query contract drift');assert_fresh_run(c['run'])
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text());verify_shared_guard(c,frozen);need(not frozen.get('resume_authorization'),'No new query permitted by resume authorization');pre=Path(c['preflight_config']).resolve()
 need(pre.is_relative_to(TASK_ROOT) and digest(pre)==c['preflight_config_sha256'],'Query preflight drift');expected=json.loads(pre.read_text(encoding='utf-8-sig'))
 need(c['platform']==expected['platform'] and c['permission_helper']==frozen['permission_helper'],'Query platform/helper drift')
 need(c['scenario']=='readonly-directory-query' and c['targets']==TARGETS and c['token']==TOKENS,'Query identity')
 need(c['remote']=={s:f"/data/local/tmp/mdds_dds_{c['run']}_{s}" for s in ['A','B']} and c['system_library_dirs']==LIBDIRS,'Query paths')
 need(Path(c['window_root']).resolve().parent==TASK_ROOT/'windows','Foreign query')
 if frozen['scope']==OBSERVED_SCOPE:verify_observed_budget(c,frozen,query=True)
 for p in (TASK_ROOT/'windows').glob('*/EXECUTION_STARTED'):
  if p.parent!=Path(c['window_root']).resolve():require_restored(p.parent)
if __name__=='__main__':
 p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--check-query');g.add_argument('--check-window');g.add_argument('--claim-query');a=p.parse_args();c=json.loads(Path(a.check_query or a.check_window or a.claim_query).read_text(encoding='utf-8-sig'));execution_admission(c['task_contract']);(claim_query if a.claim_query else verify_query if a.check_query else verify_window)(c);print('PASS frozen DDS execution receipt')
