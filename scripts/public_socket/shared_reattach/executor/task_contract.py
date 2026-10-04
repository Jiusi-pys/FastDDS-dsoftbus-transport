"""Task-local experiment admission receipt. Not SDK capability or user permission."""
from pathlib import Path
import json,hashlib,re,argparse,importlib.util,sys,datetime
# Recovery CLI is local-only and never enters admission/claim/dispatch.
_RECOVERY_FLAGS=("--seal-local-actions", "--recover-local-no-state-change")
TASK_ROOT=Path(__file__).resolve().parent.parent
for _flag in _RECOVERY_FLAGS:
 if _flag in sys.argv:
  _index=sys.argv.index(_flag)+1
  if _index < len(sys.argv): TASK_ROOT=Path(sys.argv[_index]).resolve().parent.parent
def read_private_config():
 value=json.loads((TASK_ROOT/'PRIVATE_CONFIG.json').read_text(encoding='utf-8'))
 if not isinstance(value,dict):raise ValueError('Private configuration object required')
 return value
PRIVATE=read_private_config()
TARGETS=PRIVATE['targets']
TOKENS=PRIVATE['tokens']
def repo_root():return Path(PRIVATE.get('repo_root',str(TASK_ROOT.parents[2]))).resolve()
def verify_private_binding(c):
 sha=digest(TASK_ROOT/'PRIVATE_CONFIG.json')
 need(c.get('private_config_sha256')==sha and c.get('hdc_path')==PRIVATE['hdc_path'] and c.get('permission_baseline_sha256')==PRIVATE['permission_baseline_sha256'],'Private task input changed')

CASES=['SHARED_W1','SHARED_W2','F1M','INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE']
def layered():
 spec=importlib.util.spec_from_file_location('bound_layered_contract',Path(__file__).with_name('layered_contract.py'))
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
def this_contract():
 from types import SimpleNamespace
 return SimpleNamespace(**globals())
def is_dds214(c):return isinstance(c,dict) and c.get('scenario')=='DDS214_LAYER_BASELINE'
ROS_SCENARIO='ROS2_DSOFTBUS'
ROS_SCOPE='ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE'
ROS_AUTHORIZATION='ROS2_TARGET_BUILD_BOUNDED_DEVICE_ACCEPTANCE_AND_LOCAL_COMMIT'
ROS_OBSERVED_AUTHORIZATION='ONE_ROS2_DIAGNOSTIC_DEVICE_WINDOW_AND_SUCCESSFUL_LOCAL_COMMIT'
ROS_SERVICE_NAME_AUTHORIZATION='ONE_ROS2_SERVICE_NAME_DEVICE_VALIDATION_AND_SUCCESSFUL_LOCAL_COMMIT'
ROS_PRIOR_CONSUMED_LEDGER_SHA256='00ca1a2928ca8d082c7bcf9bb1ddc18fa21596e2fd34e934f82d9dafbce8f00c'
ROS_LAYERED_AUTHORIZATION='ONE_CONDITIONAL_ROS_AFTER_DDS214_DEVICE_VALIDATION'
ROS_TASK_AUTHORIZATION='TASK_SCOPED_ROS2_DEVICE_VALIDATION'
ROS_TASK_GRANT_RELATIVE='.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/AUTHORIZATION.json'
ROS_TASK_GRANT_PATH=None
ROS_TASK_GRANT_SHA256='1dbadad0591bad3f76ef98a6bab09ff80ee6b75491e54c447410c841b39bea51'
ROS_V1_SUCCESSOR_GRANT_RELATIVE='.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/V1_SUCCESSOR_AUTHORIZATION_20261004.json'
ROS_V1_SUCCESSOR_GRANT_PATH=None
ROS_V1_SUCCESSOR_GRANT_SHA256='fce6e3d9ca8c7d0d20790dbeba2e3e4b673d521ab815b685af17f519b6ad0750'
ROS_V1_RECOVERED_SOURCE_RELATIVE='.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-23/INDEPENDENT_RECOVERED_IDENTITY_SOURCE_ACCEPTANCE.json'
ROS_V1_RECOVERED_SOURCE_SHA256='7e3cd32f3496216847b6ba15c62077c65f2485174d0fbb4f0f1ac2cb22e6547b'
ROS_TASK_WINDOW_LIMITS=dict(timeout_seconds=300,per_side_writes=10,per_side_takes=10,record_count=20,stdout_budget=25165824,max_record_bytes=8192,stages=7)
ROS_ONE_WINDOW_AUTHORIZATIONS=(ROS_OBSERVED_AUTHORIZATION,ROS_SERVICE_NAME_AUTHORIZATION,ROS_LAYERED_AUTHORIZATION)
ROS_STAGES=['LISTENER_READY','RELIABLE_READY','RELIABLE_DONE','BEST_EFFORT_READY','BEST_EFFORT_DONE','TEARDOWN_READY','DDS_DELETED']
ROS_LIMITS=dict(canonical_windows=1,repair_retries=1,total_attempts=2,query_pairs_per_attempt=1,
                total_query_pairs=2,max_record_bytes=8192,per_side_writes=10,per_side_takes=10,stages=7)
ROS_LIMITS_ONCE=dict(ROS_LIMITS,repair_retries=0,total_attempts=1,total_query_pairs=1)

def verify_ros_authorization(frozen):
 if frozen.get('authorization')==ROS_TASK_AUTHORIZATION:return verify_ros_task_grant(frozen)
 if frozen.get('authorization') in ROS_ONE_WINDOW_AUTHORIZATIONS:
  limits=frozen.get('limits')
  need(limits==ROS_LIMITS_ONCE and all(type(v) is int for v in limits.values()),'ROS observed exact one-window limits')
  need(isinstance(frozen.get('new_quota_id'),str) and re.fullmatch('[A-Z0-9_]{1,80}',frozen['new_quota_id']),'ROS new quota identity missing')
  need(not any(k in frozen for k in ('previous_ros_data_attempt','retry_evidence','supplemental_query_authorization','resume_authorization')),'ROS observed grant cannot inherit repair/resume')
 else:need(frozen.get('authorization')==ROS_AUTHORIZATION and frozen.get('limits')==ROS_LIMITS,'ROS authorization/limits mismatch')

def ros_task_mode(frozen=None):
 if frozen is None:
  p=TASK_ROOT/'DEVICE_CONTRACT.json'
  frozen=json.loads(p.read_text(encoding='utf-8-sig')) if p.is_file() else {}
 return frozen.get('authorization')==ROS_TASK_AUTHORIZATION

def verify_v1_successor(frozen,grant):
 # An additional human task authorization, never a platform permission grant.
 # It exempts exactly the old accepted checkpoint; a new PASS remains terminal.
 binding=frozen.get('v1_successor_authorization')
 if binding is None:return None
 need(binding==PRIVATE.get('v1_successor_authorization'),'V1 successor private input mismatch')
 expected=Path(ROS_V1_SUCCESSOR_GRANT_PATH).resolve() if ROS_V1_SUCCESSOR_GRANT_PATH is not None else (repo_root()/ROS_V1_SUCCESSOR_GRANT_RELATIVE).resolve()
 need(binding==dict(path=str(expected),sha256=ROS_V1_SUCCESSOR_GRANT_SHA256) and digest(expected)==ROS_V1_SUCCESSOR_GRANT_SHA256,'V1 successor authorization anchor drift')
 record=json.loads(expected.read_text(encoding='utf-8-sig'))
 need(record.get('schema')==1 and record.get('authorization')=='PACKAGE_SCOPED_V1_SERVICE_NAME_REVISION_AND_TASK_CONTINUATION' and record.get('work_package')=='V1_LOCATOR_DEVICE_ACCEPTANCE','V1 successor purpose')
 need(record.get('original_authorization_sha256')==ROS_TASK_GRANT_SHA256 and record.get('native_name_contract')=='PACKAGE_SCOPED_V1' and record.get('profile')=='BYTES_NATIVE','V1 original authority/contract')
 for key in ('same_registered_identity','same_platform_permission_scheme'):need(record.get(key) is True,'V1 identity scope expanded')
 for key in ('historical_refund','fixed_attempt_balance','reaccept','production_qualification'):need(record.get(key) is False,'V1 unauthorized expansion')
 prior=record.get('predecessor',{});window=Path(prior.get('window','')).resolve()
 need(window==Path(grant['task_root']).resolve()/'iterations/iteration-20/windows/data-15' and prior.get('run')=='be8cb2d90f287223' and digest(window/'window.json')==prior.get('window_sha256'),'V1 exact predecessor drift')
 item=prior.get('independent_acceptance',{});acceptance=window/'independent-tester/INDEPENDENT_ACCEPTANCE.json'
 need(item==dict(path=str(acceptance),sha256=digest(acceptance)),'V1 independent predecessor drift')
 accepted=json.loads(acceptance.read_text(encoding='utf-8-sig'));config=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
 need(accepted.get('run')==prior['run'] and accepted.get('functional_verdict')=='PASS_IN_STATED_DUAL_BOARD_ROS_SCOPE' and accepted.get('environment_restoration_verdict')=='PASS','V1 predecessor not independently accepted/restored')
 need(config.get('run')==prior['run'] and config.get('targets')==TARGETS and config.get('token')==TOKENS and config.get('package_name')==PRIVATE.get('ros2_package_name'),'V1 registered identity changed')
 return record

def verify_recovered_identity_source(window):
 # Exactly the reviewed original query and its appended file-only restoration.
 # Original FAIL and consumed claim remain unchanged; ordinary query validation
 # never uses this exception.
 path=(repo_root()/ROS_V1_RECOVERED_SOURCE_RELATIVE).resolve()
 need(path.is_file() and digest(path)==ROS_V1_RECOVERED_SOURCE_SHA256,'Recovered identity acceptance drift')
 accepted=json.loads(path.read_text(encoding='utf-8-sig'));window=Path(window).resolve()
 need(accepted.get('verdict')=='PASS_IN_PREREQUISITE_SCOPE' and accepted.get('LEGITIMATE_PRIVATE_LOCAL_UDID_INPUTS_AVAILABLE') is True and accepted.get('ORIGINAL_QUERY_RAW_ROUTE_CROSSMATCH')=='PASS' and accepted.get('APPENDED_OWNED_ARTIFACT_RESTORATION')=='PASS' and accepted.get('ORIGINAL_QUERY_WINDOW')=='FAIL_PRESERVED','Recovered identity prerequisite not independently accepted')
 need(accepted.get('source_window')==str(window) and accepted.get('source_window_sha256')==digest(window/'window.json'),'Recovered source original window drift')
 need(accepted.get('sdk_queries_repeated')==0 and accepted.get('claim_status')=='ORIGINAL_CONSUMED_QUERY_CLAIM_NOT_REFUNDED','Recovered source cannot refund/query again')
 files=accepted.get('proof_files_sha256',{});need(bool(files),'Recovered source raw proof absent')
 for item,expected in files.items():need(digest(item)==expected,'Recovered source original/recovery raw evidence drift')
 recovery=Path(accepted.get('recovery_directory','')).resolve()
 need(recovery==window/'identity-artifact-recovery/attempt-1' and digest(recovery/'RESULT.json')==accepted.get('recovery_result_sha256'),'Recovered source exact restoration drift')
 result=json.loads((recovery/'RESULT.json').read_text(encoding='utf-8-sig'))
 need(result.get('status')=='PASS' and all(result.get(key)==0 for key in ('sdk_queries','permission_actions','business_dispatches')),'Recovery performed unauthorized SDK/permission/business actions')
 need(set(result.get('sides',{}))=={'A','B'} and all(row.get('remote_absent') is True and row.get('mode')=='600' and row.get('bytes')==64 for row in result['sides'].values()),'Recovered source export restoration incomplete')
 cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
 need(cfg.get('targets')==TARGETS and cfg.get('token')==TOKENS and cfg.get('identity_qualification') is True,'Recovered source registered identity mismatch')
 return accepted

def verify_ros_task_grant(frozen):
 need(frozen.get('scope')==ROS_SCOPE and frozen.get('authorization')==ROS_TASK_AUTHORIZATION,'Task authorization purpose mismatch')
 need(frozen.get('local_contract_test') is not True and frozen.get('simulated') is not True and frozen.get('device_authorized') is True,'Task grant is not local/simulated authority')
 expected=Path(ROS_TASK_GRANT_PATH).resolve() if ROS_TASK_GRANT_PATH is not None else (repo_root()/ROS_TASK_GRANT_RELATIVE).resolve()
 binding=frozen.get('authorization_record');need(isinstance(binding,dict),'Task original grant missing')
 path=Path(binding.get('path','')).resolve()
 need(path==expected and binding.get('sha256')==ROS_TASK_GRANT_SHA256 and digest(path)==ROS_TASK_GRANT_SHA256,'Task original source anchor drift')
 grant=json.loads(path.read_text(encoding='utf-8-sig'))
 authority=Path(grant.get('task_root','')).resolve();iteration=frozen.get('execution_iteration_id')
 need(grant.get('authorization')==ROS_TASK_AUTHORIZATION and frozen.get('task_authority_root')==str(authority) and frozen.get('task_root')==str(TASK_ROOT),'Task grant root binding mismatch')
 need(type(iteration) is int and iteration>=1,'Task execution iteration identity')
 expected_execution=authority/'iterations'/f'iteration-{iteration}'
 need(TASK_ROOT==expected_execution.resolve() or (iteration==1 and TASK_ROOT==authority),'Task execution root must be exact immutable child')
 need(grant.get('task_id')==frozen.get('task_id') and isinstance(grant.get('task_id'),str) and re.fullmatch('[A-Z0-9_]{1,80}',grant['task_id']),'Task identity mismatch')
 need(grant.get('purpose')=='ROS2_PARTICIPANT_DELETE_AND_FINAL_DUAL_BOARD_ACCEPTANCE' and grant.get('scenario')==ROS_SCENARIO and grant.get('targets')==TARGETS,'Task registered pair/purpose mismatch')
 need(grant.get('per_window_limits')==ROS_TASK_WINDOW_LIMITS and frozen.get('limits')==ROS_TASK_WINDOW_LIMITS and all(type(v) is int for v in frozen['limits'].values()),'Task per-window limits changed')
 need(grant.get('historical_refund') is False and grant.get('git_authorized') is False,'Task cannot refund or authorize Git')
 if grant.get('historical_ledger'):
  old=grant['historical_ledger'];need(digest(old['path'])==old['sha256'],'Task historical ledger changed')
 if grant.get('predecessor_restore'):
  old=grant['predecessor_restore'];w=Path(old['window']).resolve();need(digest(w/'INDEPENDENT_ACCEPTANCE.json')==old['independent_acceptance_sha256'],'Original predecessor acceptance drift')
  accepted=json.loads((w/'INDEPENDENT_ACCEPTANCE.json').read_text(encoding='utf-8-sig'))
  need(accepted.get('run')==old['run'] and accepted.get('environment_restored') is True,'Previous real environment not restored')
  need(digest(w/'window.json')==accepted.get('window_sha256'),'Original predecessor window drift')
  prior=json.loads((w/'window.json').read_text(encoding='utf-8-sig'))
  need(PRIVATE.get('ros2_package_name')==prior.get('package_name') and TARGETS==prior.get('targets') and TOKENS==prior.get('token'),'Task changed existing device/permission identity scheme')
  _require_normal_restored(w)
 verify_v1_successor(frozen,grant)
 return grant

def ros_task_authority_root():
 frozen=json.loads((TASK_ROOT/'DEVICE_CONTRACT.json').read_text(encoding='utf-8-sig'))
 return Path(verify_ros_task_grant(frozen)['task_root']).resolve()

def ros_task_claim_rows():
 rows=[]
 for kind in ('query','data'):
  for p in (ros_task_authority_root()/'ros2/task-ledger').glob(kind+'-*.json'):
   m=re.fullmatch(kind+r'-([1-9][0-9]*)\.json',p.name);need(m is not None,'Task ledger filename')
   row=json.loads(p.read_text(encoding='utf-8-sig'));need(row.get('scope')==ROS_SCOPE and row.get('attempt')==int(m[1]),'Task ledger row identity')
   rows.append((int(m[1]),kind,p,row))
 return sorted(rows,key=lambda r:(r[0],r[1]))

def ros_task_data_outcome(window,config):
 # The recovery summary precedes postrun analysis. It is not a functional PASS.
 window=Path(window);evidence={}
 def record(name):
  p=window/name;need(p.is_file(),'Task terminal evidence incomplete: '+name)
  evidence[name]=digest(p);value=json.loads(p.read_text(encoding='utf-8-sig'))
  need(isinstance(value,dict),'Task terminal evidence object: '+name);return value
 def failed(reason):return dict(status='FAIL',first_failure=reason,evidence=evidence)
 summary=record('window-summary.json')
 runner=record('result.json');need(runner.get('run')==config['run'] and type(runner.get('pass')) is bool,'Task runner terminal identity/result')
 if runner.get('timedOut') is True:
  recovery=window/'remaining-resource-recovery/attempt-1/APPENDED_WINDOW_RESOURCE_RECOVERY.json'
  if recovery.exists():
   validated_window_resource_recovery(window);evidence[str(recovery.relative_to(window))]=digest(recovery)
   return failed(summary.get('first_failure') or 'ORIGINAL_HOST_COLLECTION_TIMEOUT_RESOURCE_RECOVERED')
 need(runner.get('timedOut') is False and runner.get('collectionCleanup')=='HDC_STREAMS_CLOSED' and runner.get('activeCollectors')==[],'Task child/collector completion unconfirmed')
 for side in ('A','B'):
  child=runner.get(side);need(isinstance(child,dict) and child.get('closed') is True and child.get('exited') is True,'Task child exit/close missing: '+side)
 if summary.get('first_failure'):return failed(summary['first_failure'])
 if runner['pass'] is False:return failed('ROS_RUNNER_FAILED')
 loads=record('load-collection-summary.json')
 need('first_failure' in loads and isinstance(loads.get('failures'),list),'Task load terminal result missing')
 if loads['first_failure'] or loads['failures']:return failed(loads['first_failure'] or 'ROS_LOAD_ORIGIN_FAILED')
 names=['ros-analysis','ros-native-analysis']
 if 'v1_deployment' in config:names.append('v1-locator-analysis')
 for name in names:
  failure_name='V1_ANALYSIS_FAILURE.json' if name=='v1-locator-analysis' else 'ANALYSIS_FAILURE.json'
  failure=window/failure_name
  if failure.is_file():
   value=record(failure_name);need(value.get('run')==config['run'] and bool(value.get('first_failure')),'Task analysis failure identity')
   return failed(value['first_failure'])
  value=record(name+'.json');need(value.get('run')==config['run'] and type(value.get('pass')) is bool,'Task analysis terminal identity/result')
  rc=window/(name+'.rc');need(rc.is_file(),'Task analysis exit receipt missing');evidence[rc.name]=digest(rc)
  code=rc.read_text(encoding='utf-8-sig').strip();need(re.fullmatch(r'-?[0-9]+',code) is not None,'Task analysis exit receipt invalid')
  if value['pass'] is False or int(code)!=0:return failed({'ros-analysis':'ROS_ANALYSIS_FAILED','ros-native-analysis':'ROS_NATIVE_SAMPLE_CORRELATION_FAILED','v1-locator-analysis':'ACTUAL_V1_LOCATOR_ANALYSIS_FAILED'}[name])
 return dict(status='PASS',first_failure=None,evidence=evidence)

def pregrant_module():
 p=TASK_ROOT/'recovery/pregrant_recovery.py';record=json.loads((TASK_ROOT/'MATERIALIZATION.json').read_text(encoding='utf-8-sig'))
 rows=[e for e in record['files'] if e['destination']=='recovery/pregrant_recovery.py']
 need(len(rows)==1 and digest(p)==rows[0]['sha256'],'Bound recovery verifier missing or changed')
 spec=importlib.util.spec_from_file_location('bound_pregrant_recovery',p);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def appended_pregrant_recovery(window):
 window=Path(window).resolve();paths=list((window/'recovery').glob('attempt-*'))
 need(paths and all(p.is_dir() and re.fullmatch('attempt-[1-9][0-9]*',p.name) for p in paths),'Appended recovery absent or malformed')
 latest=max(paths,key=lambda p:int(p.name.split('-')[1]));result=latest/'RECOVERY_RESULT.json'
 receipt=pregrant_module().verify_result(result,require_acceptance=True)
 need(Path(receipt['window']).resolve()==window,'Foreign appended recovery')
 return dict(receipt=receipt,result_path=str(result),result_sha256=digest(result),acceptance_sha256=digest(latest/'INDEPENDENT_ACCEPTANCE.json'))

def task_preparation_failures(authority,exclude_window=None):
 result=[];roots=[authority]+[p for p in (authority/'iterations').glob('iteration-*') if re.fullmatch('iteration-[1-9][0-9]*',p.name)]
 for root in roots:
  for marker in (root/'windows').glob('*/EXECUTION_STARTED'):
   window=marker.parent
   if exclude_window is not None and window.resolve()==Path(exclude_window).resolve():continue
   cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
   if cfg.get('scenario')!=ROS_SCENARIO or (window/'DATA_ATTEMPT_STARTED').exists():continue
   need(cfg.get('ros_authorization_mode')==ROS_TASK_AUTHORIZATION,'Foreign task preparation authority')
   recovery=appended_pregrant_recovery(window)
   result.append((cfg['ros_attempt'],'preparation',window/'window-summary.json',dict(window=str(window),run=cfg['run']),recovery))
 return result

def ros_task_next_attempt(exclude_window=None):
 frozen=json.loads((TASK_ROOT/'DEVICE_CONTRACT.json').read_text(encoding='utf-8-sig'));grant=verify_ros_task_grant(frozen)
 successor=verify_v1_successor(frozen,grant)
 rows=ros_task_claim_rows();preparations=task_preparation_failures(Path(grant['task_root']).resolve(),exclude_window)
 if not rows:return 1
 latest=max(r[0] for r in rows)
 for attempt,kind,path,row in rows:
  need(row.get('task_id')==grant['task_id'],'Foreign task ledger')
  window=Path(row.get('window','')).resolve();execution=window.parent.parent;authority=Path(grant['task_root']).resolve()
  need(window.parent.name=='windows' and (execution==authority or execution.parent==authority/'iterations' and re.fullmatch(r'iteration-[1-9][0-9]*',execution.name)),'Task previous window location')
  need((window/'EXECUTION_STARTED').is_file() and (window/'window-summary.json').is_file(),'Task pending/crashed execution has incomplete receipts')
  config=json.loads((window/'window.json').read_text(encoding='utf-8-sig'));original=config.get('task_contract',{})
  contract_path=Path(original.get('freeze_path','')).resolve()
  need(contract_path.parent==execution and original.get('task_root')==str(execution) and digest(contract_path)==original.get('freeze_sha256'),'Task original contract identity drift')
  original_frozen=json.loads(contract_path.read_text(encoding='utf-8-sig'))
  need(original_frozen.get('authorization')==ROS_TASK_AUTHORIZATION and original_frozen.get('task_id')==grant['task_id'] and original_frozen.get('task_authority_root')==str(authority) and original_frozen.get('authorization_record',{}).get('sha256')==ROS_TASK_GRANT_SHA256,'Task original grant/source mismatch')
  need(config.get('run')==row.get('run') and config.get('ros_attempt')==attempt and config.get('targets')==TARGETS and config.get('ros_task_id')==grant['task_id'] and Path(config.get('window_root','')).resolve()==window,'Task original window/run/targets mismatch')
  if kind=='query':need(row==ros_query_claim_row(config,False),'Task query claim fields drift')
  else:need(row.get('kind')=='DATA_DISPATCH_INTENT' and row.get('side')=='A' and row.get('simulated') is False and row.get('authorization')==ROS_TASK_AUTHORIZATION and row.get('authorization_record_sha256')==ROS_TASK_GRANT_SHA256,'Task real data intent identity')
  if kind=='data':need((window/'DATA_ATTEMPT_STARTED').is_file() and json.loads((window/'DATA_ATTEMPT_STARTED').read_text(encoding='utf-8-sig'))==row,'Task data intent/marker mismatch')
  _require_normal_restored(window) # The local NO_STATE_CHANGE branch is never authority here.
  summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
  if kind=='data':
   outcome=ros_task_data_outcome(window,config)
   predecessor=successor and successor['predecessor']
   need(outcome['status']!='PASS' or (predecessor and row.get('run')==predecessor['run'] and window==Path(predecessor['window']).resolve()),'Successful task data window ends further dispatch')
 current=[r for r in rows if r[0]==latest]
 query=next((r for r in current if r[1]=='query'),None);data=next((r for r in current if r[1]=='data'),None)
 prep=next((r for r in preparations if r[0]==latest),None)
 if successor and data and data[3].get('run')==successor['predecessor']['run']:
  return latest+1
 if query and not data and not prep:
  window=Path(query[3]['window']);summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
  if successor and (repo_root()/ROS_V1_RECOVERED_SOURCE_RELATIVE).is_file():
   recovered=json.loads((repo_root()/ROS_V1_RECOVERED_SOURCE_RELATIVE).read_text(encoding='utf-8-sig'))
   if recovered.get('source_window')==str(window.resolve()):verify_recovered_identity_source(window);return latest
  if not summary.get('first_failure'):
   result=window/'ROUTE_QUERY_RESULT.json';need(result.is_file() and json.loads(result.read_text(encoding='utf-8-sig')).get('pass') is True,'Task query derived result incomplete')
   return latest # Own consumed query is available for data, never another query dispatch.
 continuation=Path(grant['task_root'])/'ros2/task-ledger'/f'continuation-{latest}.json'
 need(continuation.is_file(),'Task failure needs new verified continuation evidence')
 proof=json.loads(continuation.read_text(encoding='utf-8-sig'));previous=data or prep or query
 current_contract_sha=digest(TASK_ROOT/'DEVICE_CONTRACT.json')
 if proof.get('binding',{}).get('after_contract_sha256')!=current_contract_sha:
  successor=continuation.with_name(f'continuation-{latest}-for-{current_contract_sha}.json')
  need(successor.is_file(),'Current source revision needs exact append-only continuation successor')
  selected=json.loads(successor.read_text(encoding='utf-8-sig'))
  need(selected.get('base_continuation')==dict(path=str(continuation),sha256=digest(continuation)),'Continuation successor base identity')
  proof=selected
 predecessor_key='previous_preparation_summary_sha256' if previous and previous[1]=='preparation' else 'previous_claim_sha256'
 need(previous is not None and proof.get('task_id')==grant['task_id'] and proof.get('previous_attempt')==latest and proof.get('previous_window')==previous[3]['window'] and proof.get(predecessor_key)==digest(previous[2]),'Task continuation predecessor mismatch')
 need(proof.get('decision')=='RESUME_WITH_NEW_EVIDENCE_OR_VALIDATED_FIX' and proof.get('owner_payload_receiver_safe') is True,'Task safety/continuation not confirmed')
 need(proof.get('verified_change') is True or proof.get('new_direct_evidence') is True,'Repeated failure without new evidence or verified repair')
 need(proof.get('source_or_configuration_delta') or proof.get('new_evidence_description'),'Task requires a concrete verifiable change, not renamed run')
 previous_window=Path(previous[3]['window']);previous_cfg=json.loads((previous_window/'window.json').read_text(encoding='utf-8-sig'))
 previous_freeze=Path(previous_cfg['task_contract']['freeze_path']);old_frozen=json.loads(previous_freeze.read_text(encoding='utf-8-sig'))
 outcome=ros_task_data_outcome(previous_window,previous_cfg) if previous[1]=='data' else dict(first_failure=json.loads((previous_window/'window-summary.json').read_text(encoding='utf-8-sig')).get('first_failure'),evidence={})
 binding=dict(previous_window_sha256=digest(previous_window/'window.json'),previous_summary_sha256=digest(previous_window/'window-summary.json'),previous_terminal_evidence_sha256=outcome['evidence'],before_contract_sha256=digest(previous_freeze),after_contract_sha256=digest(TASK_ROOT/'DEVICE_CONTRACT.json'))
 binding[predecessor_key]=digest(previous[2])
 if previous[1]=='preparation':binding.update(previous_recovery_result_sha256=previous[4]['result_sha256'],previous_recovery_acceptance_sha256=previous[4]['acceptance_sha256'])
 need(proof.get('binding')==binding,'Task continuation failure/source binding mismatch')
 old_sources={e['path']:e['sha256'] for e in old_frozen.get('source',[])+old_frozen.get('execution_files',[])}
 new_sources={e['path']:e['sha256'] for e in frozen.get('source',[])+frozen.get('execution_files',[])}
 deltas=proof.get('delta_files',[])
 for delta in deltas:
  name=delta.get('path');need(name in new_sources and delta.get('before_sha256')==old_sources.get(name) and delta.get('after_sha256')==new_sources[name] and delta.get('before_sha256')!=delta.get('after_sha256'),'Task claimed source delta not present in actual selected inputs')
  need(digest(repo_root()/name)==new_sources[name],'Task changed file does not match current tested source')
 old_private=json.loads((previous_freeze.parent/'PRIVATE_CONFIG.json').read_text(encoding='utf-8-sig'))
 config_deltas=proof.get('configuration_delta',[])
 for delta in config_deltas:
  key=delta.get('field');need(key in ('ros2_service_prefix',) and delta.get('before')==old_private.get(key) and delta.get('after')==PRIVATE.get(key) and delta['before']!=delta['after'],'Task configuration delta must be a real permitted change')
 evidence=proof.get('direct_evidence_files',[])
 for item in evidence:
  p=Path(item.get('path','')).resolve();need(p.is_relative_to(previous_window) and digest(p)==item.get('sha256'),'Task direct evidence must belong to the actual failed predecessor')
 need((proof.get('verified_change') is True and bool(deltas or config_deltas)) or (proof.get('new_direct_evidence') is True and bool(evidence)),'Task continuation has no real source/config/evidence delta')
 for key in ('analysis','validation','review'):
  item=proof.get(key);need(isinstance(item,dict),'Task continuation evidence missing')
  p=Path(item.get('path','')).resolve();need(p.is_relative_to(Path(grant['task_root'])) and digest(p)==item.get('sha256'),'Task continuation evidence drift')
  value=json.loads(p.read_text(encoding='utf-8-sig'))
  need(value.get('task_id')==grant['task_id'] and value.get('continuation_binding')==binding,'Unrelated analysis/validation/review cannot permit continuation')
  if key=='analysis':need(value.get('first_failure')==outcome['first_failure'] and value.get('root_cause_or_new_evidence'),'Task analysis must identify this actual first failure/evidence')
  if key=='validation':need(value.get('result')=='PASS' or value.get('overall')=='PASS','Affected validation not PASS')
  if key=='review':need(value.get('verdict') in ('PASS','NO_ACTIONABLE_FINDINGS_IN_FINAL_OFFLINE_DIFF') and value.get('blocking') is not True,'Continuation independent review not closed')
 return latest+1

def verify_ros_observed_grant(frozen):
 if frozen.get('authorization') not in ROS_ONE_WINDOW_AUTHORIZATIONS:return
 if frozen.get('authorization')==ROS_LAYERED_AUTHORIZATION:
  layered().grant(this_contract(),frozen,'ROS2');return
 binding=frozen.get('authorization_record');need(isinstance(binding,dict),'ROS observed original grant required')
 path=Path(binding['path']).resolve();need(digest(path)==binding['sha256'],'ROS observed grant drift')
 grant=json.loads(path.read_text(encoding='utf-8'))
 need(grant.get('authorization')==frozen['authorization'] and grant.get('task_root')==str(TASK_ROOT) and grant.get('quota_id')==frozen['new_quota_id'],'ROS observed grant/task mismatch')
 limits=grant.get('new_limits')
 need(limits=={'query_pair':1,'data_attempt':1,'repair_retry':0} and all(type(v) is int for v in limits.values()),'ROS observed original grant expansion')
 old=grant['historical_ledger'];need(digest(old['path'])==old['sha256'],'Historical consumed ledger drift')
 if frozen['authorization']==ROS_SERVICE_NAME_AUTHORIZATION:
  expected=(TASK_ROOT.parent/'observed-device-20261002/FINAL_ACTUAL_CONSUMPTION_LEDGER.json').resolve()
  need(grant.get('work_package')=='ROS2_SERVICE_NAME_DEVICE_VALIDATION' and Path(old['path']).resolve()==expected and old['sha256']==ROS_PRIOR_CONSUMED_LEDGER_SHA256,'ROS latest consumed ledger identity')
  prior=json.loads(expected.read_text(encoding='utf-8'))
  zero={'query_pair':0,'data_attempt':0,'repair_retry':0}
  used={'query_pair':4,'data_attempt':3}
  need(prior.get('authorization')==ROS_OBSERVED_AUTHORIZATION and prior.get('quota_id')!=frozen['new_quota_id'] and
       prior.get('new_used')=={'query_pair':1,'data_attempt':1,'repair_retry':0} and prior.get('new_remaining')==zero and
       all(type(v) is int for v in prior['new_remaining'].values()) and prior.get('cumulative_used')==used and prior.get('refund') is False and
       grant.get('historical_used')==used and type(grant.get('historical_remaining')) is int and grant['historical_remaining']==0 and
       grant.get('refund') is False,'ROS predecessor consumed quota mismatch')
def is_ros(c):return isinstance(c,dict) and c.get('scenario')==ROS_SCENARIO
def ros_shape(c):
 need(c.get('work_package')==ROS_SCOPE and c.get('main_member')=='ordinary_ros2_transport_consumer','ROS scope/main mismatch')
 need(c.get('main_prefix')=='ROS_MAIN' and c.get('main_mode')=='dual','ROS main output/mode mismatch')
 need(not any(k in c for k in ('cycles','delete_first','shared','shared_receipts')),'ROS is not a shared/cycle alias')
 assert_fresh_run(c['run'])
 need(c['targets']==TARGETS and c['token']==TOKENS,'ROS board/token mismatch')
 need(c['remote']=={s:f"/data/local/tmp/mdds_dds_{c['run']}_{s}" for s in ['A','B']},'ROS remote path mismatch')
 need(c.get('expected_writes')=={'A':10,'B':10} and c.get('record_count')==20,'ROS directional/aggregate sample contract')
 need(c.get('stages')==ROS_STAGES and c.get('timeout_seconds')==300 and c.get('stdout_budget')==25165824,'ROS frozen stage/budget mismatch')
 need(c['system_library_dirs']==LIBDIRS and c.get('max_record_bytes')==8192,'ROS loader/log budget mismatch')
 need(type(c.get('ros_attempt')) is int and (c['ros_attempt']>=1 if ros_task_mode() else c['ros_attempt'] in (1,2)),'ROS attempt identity')
 names=[x['name'] for x in c['package_artifacts']]
 need(len(names)==len(set(names)) and len(names)<=128 and c['main_member'] in names,'ROS package identity/size')
 need(all(Path(x['source']).name==x['name'] and digest(x['source'])==x['sha256'] for x in c['package_artifacts']),'ROS package drift')
 need(set(c['peer'])=={'A','B'} and all(re.fullmatch('[0-9a-f]{64}',v) for v in c['peer'].values()),'ROS current guarded route missing')
 if c.get('rmw_debugger') is not None:
  frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'));spec=c['rmw_debugger']
  need(ros_task_mode(frozen) and spec==frozen.get('rmw_debugger') and c.get('require_residual_snapshot') is True,'Debugger observation must belong to reviewed task contract')
  need(set(spec)=={'client','server','script','ports','hit_limit','seconds'} and set(spec['ports'])=={'A','B'},'Debugger input shape')
  need(type(spec['hit_limit']) is int and 1<=spec['hit_limit']<=256 and type(spec['seconds']) is int and 1<=spec['seconds']<=260,'Debugger bounded observation')
  need(all(type(v) is int and 1024<=v<=65535 for v in spec['ports'].values()) and len(set(spec['ports'].values()))==2,'Debugger port identity')
  for key in ('client','server','script'):
   row=spec[key];need(set(row)=={'path','sha256'} and Path(row['path']).is_absolute() and digest(row['path'])==row['sha256'],'Debugger frozen tool changed')
  need(Path(spec['server']['path']).name=='lldb-server' and Path(spec['client']['path']).name=='lldb.exe' and Path(spec['script']['path']).resolve()==TASK_ROOT/'executor/rmw_lldb_trace.py','Debugger exact tool/script binding')
def ros_claim_path(kind,attempt):
 need(kind in ('query','data') and type(attempt) is int and (attempt>=1 if ros_task_mode() else attempt in (1,2)),'ROS claim purpose')
 if ros_task_mode():return ros_task_authority_root()/'ros2/task-ledger'/f'{kind}-{attempt}.json'
 return TASK_ROOT/'ros2/attempt-ledger'/f'{kind}-{attempt}.json'
def ros_previous_attempt():
 p=TASK_ROOT/'DEVICE_CONTRACT.json'
 if not p.exists():return None
 frozen=json.loads(p.read_text());binding=frozen.get('previous_ros_data_attempt')
 if ros_task_mode(frozen):return None
 if binding is None:return None
 need(frozen.get('scope')==ROS_SCOPE and frozen.get('local_contract_test') is not True,'ROS predecessor requires real scope')
 contract=Path(binding['contract_path']).resolve();oldroot=contract.parent
 need(oldroot!=TASK_ROOT and oldroot.parent==TASK_ROOT.parent and contract.name=='DEVICE_CONTRACT.json','ROS predecessor location')
 need(digest(contract)==binding['contract_sha256'],'ROS predecessor contract drift')
 old=json.loads(contract.read_text())
 need(old.get('scope')==ROS_SCOPE and old.get('device_authorized') is True and old.get('local_contract_test') is not True,'ROS predecessor is not real authority')
 need(bool(frozen.get('original_data_quota_id')) and old.get('original_data_quota_id')==frozen['original_data_quota_id'],'ROS predecessor quota mismatch')
 claim=oldroot/'ros2/attempt-ledger/data-1.json'
 need(digest(claim)==binding['claim_sha256'],'ROS predecessor claim drift')
 previous=json.loads(claim.read_text());window=Path(previous['window']).resolve()
 need(previous.get('scope')==ROS_SCOPE and previous.get('attempt')==1 and previous.get('simulated') is False and window.parent==oldroot/'windows','ROS predecessor dispatch identity')
 need(json.loads((window/'DATA_ATTEMPT_STARTED').read_text())==previous,'ROS predecessor dispatch marker')
 need(bool(binding.get('receipts')),'ROS predecessor receipts absent')
 for name,expected in binding['receipts'].items():
  relative=Path(name);need(not relative.is_absolute() and '..' not in relative.parts,'ROS predecessor receipt path')
  need(digest(window/relative)==expected,'ROS predecessor receipt drift')
 need({'window.json','window-summary.json','result.json','DATA_ATTEMPT_STARTED'}<=set(binding['receipts']),'ROS predecessor required receipts absent')
 _require_normal_restored(window)
 need(bool(json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig')).get('first_failure')),'Successful predecessor forbids repair')
 need(not (oldroot/'ros2/attempt-ledger/data-2.json').exists(),'ROS predecessor repair already used')
 return claim

def ros_next_attempt(exclude_window=None):
 if ros_task_mode():return ros_task_next_attempt(exclude_window)
 claims=list((TASK_ROOT/'ros2/attempt-ledger').glob('data-*.json'))
 freeze=TASK_ROOT/'DEVICE_CONTRACT.json'
 if freeze.is_file():
  frozen=json.loads(freeze.read_text())
  if frozen.get('authorization') in ROS_ONE_WINDOW_AUTHORIZATIONS:
   verify_ros_authorization(frozen)
   if frozen['authorization'] in (ROS_SERVICE_NAME_AUTHORIZATION,ROS_LAYERED_AUTHORIZATION):verify_ros_observed_grant(frozen)
   need(not claims,'ROS observed data quota exhausted');return 1
 predecessor=ros_previous_attempt()
 if predecessor is not None:
  need(not claims,'ROS repair data quota consumed')
  return 2
 need(len(claims)<2,'ROS data quota exhausted')
 if not claims:return 1
 need(len(claims)==1 and claims[0]==ros_claim_path('data',1),'ROS prior claim sequence')
 previous=json.loads(claims[0].read_text());window=Path(previous['window']).resolve()
 need(previous.get('scope')==ROS_SCOPE and previous.get('attempt')==1 and window.parent==TASK_ROOT/'windows','ROS prior claim identity')
 need(json.loads((window/'DATA_ATTEMPT_STARTED').read_text())==previous,'ROS dispatch intent/marker mismatch')
 require_restored(window)
 summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
 need(bool(summary.get('first_failure')),'A successful ROS window forbids another attempt')
 return 2
def verify_ros_budget(c,query=False):
 attempt=c.get('ros_attempt');need(type(attempt) is int and attempt==ros_next_attempt(None if query else c['window_root']),'ROS attempt sequence/quota')
 if attempt==2 and not ros_task_mode():
  row=c.get('retry_evidence');need(isinstance(row,dict),'ROS reviewed repair required')
  path=Path(row['path']).resolve();need(path.is_relative_to(TASK_ROOT) and digest(path)==row['sha256'],'ROS repair evidence drift')
  repair=json.loads(path.read_text());need(all(repair.get(k) for k in ('root_cause','fix','affected_validation','review')),'ROS repair is not validated')
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text())
 if ros_task_mode(frozen):
  verify_ros_task_grant(frozen);need(c.get('ros_task_id')==frozen.get('task_id') and c.get('ros_authorization_mode')==ROS_TASK_AUTHORIZATION and c.get('task_authority_root')==frozen['task_authority_root'] and frozen['execution_iteration_id']>=attempt,'Task window authorization identity')
 if frozen.get('authorization') in ROS_ONE_WINDOW_AUTHORIZATIONS:need('retry_evidence' not in c and attempt==1,'ROS observed retry forbidden')
 if query:need(not ros_claim_path('query',attempt).exists(),'ROS query quota consumed')
 else:
  route=c.get('route_binding');need(isinstance(route,dict),'ROS guarded route missing')
  receipt=json.loads(ros_claim_path('query',attempt).read_text());need(receipt.get('scope')==ROS_SCOPE and receipt.get('run')==route.get('query_run'),'ROS query purpose binding')
def verify_ros_query_claim(c):
 receipt=json.loads(ros_claim_path('query',c['ros_attempt']).read_text())
 need(receipt==ros_query_claim_row(c,False),'ROS completed query/claim mismatch')

def ros_query_claim_row(c,local):
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text())
 row=dict(scope=ROS_SCOPE,run=c['run'],window=str(Path(c['window_root']).resolve()),
          purpose=ROS_SCENARIO,targets=c['targets'],contract_sha256=c['task_contract']['freeze_sha256'],
          materialization_sha256=frozen['materialization_sha256'],source_state_sha256=frozen['build_binding']['source_state_sha256'])
 if local:row.update(local_contract_test=True,kind='LOCAL_QUERY_DISPATCH_INTENT',device_authority=False)
 else:row.update(attempt=c['ros_attempt'],kind='ROS_QUERY_DISPATCH_INTENT',authorization=frozen['authorization'],supplemental_authorization=frozen.get('supplemental_query_authorization'))
 if frozen.get('authorization') in ROS_ONE_WINDOW_AUTHORIZATIONS:row['quota_id']=frozen['new_quota_id']
 if ros_task_mode(frozen):row.update(task_id=frozen['task_id'],authorization_record_sha256=frozen['authorization_record']['sha256'])
 return row

def admit_ros_query_dispatch(c,side):
 # Existing receipt validation has no new claim or quota-refund capability.
 verify_query_context(c)
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text())
 if frozen['scope']==layered().SCOPE:return layered().dispatch(this_contract(),c,side)
 need(frozen['scope']==ROS_SCOPE and c.get('query_for_scenario')==ROS_SCENARIO,'ROS dispatch purpose')
 local=frozen.get('local_contract_test') is True
 claim=TASK_ROOT/'local-test-claims'/('query-'+c['run']+'.json') if local else ros_claim_path('query',c['ros_attempt'])
 need(json.loads(claim.read_text())==ros_query_claim_row(c,local),'ROS dispatch original claim mismatch')
 predecessor=ros_previous_attempt() if not local else None
 if predecessor is not None:
  need(json.loads((predecessor.parent/'repair-continuation.json').read_text())==ros_query_claim_row(c,False),'ROS repair reservation mismatch')
 need(side in ('A','B'),'ROS dispatch side')
 path=Path(c['window_root']).resolve()/('QUERY_DISPATCH_INTENT_'+side+'.json')
 row=dict(run=c['run'],side=side,target=c['targets'][side],purpose=ROS_SCENARIO,claim_path=str(claim),claim_sha256=digest(claim),
          contract_sha256=c['task_contract']['freeze_sha256'],driver_path=c['hdc_path'],remote_execution='UNOBSERVED_AT_INTENT')
 with path.open('x',encoding='utf-8') as stream:json.dump(row,stream)
# First-class scenario for the installed public-Socket runtime consumer. It is not
# a SHARED_*/F1M alias: no cycle, shared-rebuild, P/Q or deletion-order semantics
# exist for it, and it is validated by its own contract instead of the old schema.
INSTALLED_SCENARIO='INSTALLED_RUNTIME'
INSTALLED_SCOPE='INSTALLED_RUNTIME_DUAL_BOARD_CONSUMER'
INSTALLED_MAIN='mdds_installed_consumer'
INSTALLED_STAGES=['LISTENER_READY','MATCH','DATA_DONE','DDS_DELETED']
INSTALLED_MEMBERS=frozenset(['mdds_installed_consumer','libmdds_runtime_token_bridge.so','libfastdds.so.3.6','libfastcdr.so.2','libfoonathan_memory-0.7.4.so','libc++_shared.so'])
INSTALLED_MAIN_PREFIX='INST_MAIN'
INSTALLED_MAIN_MODE='installed_runtime'
INSTALLED_LIMITS=dict(canonical_windows=1,repair_retries=1,total_attempts=2,max_record_bytes=8192,payload_bytes=32,per_side_writes=5,per_side_takes=5,stages=4)
def is_installed(c):
 return isinstance(c,dict) and c.get('scenario')==INSTALLED_SCENARIO
def installed_shape(c):
 need(c.get('cycles',1)==1,'INSTALLED_RUNTIME has no cycle contract')
 need('delete_first' not in c,'INSTALLED_RUNTIME has no deletion-order contract')
 need('shared' not in c and 'shared_receipts' not in c,'INSTALLED_RUNTIME has no shared-rebuild contract')
 assert_fresh_run(c['run'])
 need(c['targets']==TARGETS and c['token']==TOKENS,'Unregistered board/token')
 need(c['remote']=={s:f"/data/local/tmp/mdds_dds_{c['run']}_{s}" for s in ['A','B']},'Wrong exclusive path')
 need(c['system_library_dirs']==LIBDIRS,'Undeclared loader paths')
 need(c['expected_writes']=={'A':INSTALLED_LIMITS['per_side_writes'],'B':INSTALLED_LIMITS['per_side_writes']},'Wrong directional write contract')
 need(c['record_count']==2*INSTALLED_LIMITS['per_side_writes'],'Wrong formal record count')
 need(c['payload_bytes']==INSTALLED_LIMITS['payload_bytes'] and c['max_record_bytes']==INSTALLED_LIMITS['max_record_bytes'],'Expanded frozen budget')
 need(c['stages']==INSTALLED_STAGES,'Wrong installed stage contract')
 need(c['main_member']==INSTALLED_MAIN,'Wrong installed main member')
 members=[e['name'] for e in c['package_artifacts']]
 need(len(members)==len(set(members)) and set(members)==INSTALLED_MEMBERS,'Installed package membership mismatch')
 need(next((e['sha256'] for e in c['package_artifacts'] if e['name']==INSTALLED_MAIN),None)==c['main_sha256'],'Installed main identity drift')
 need(isinstance(c.get('service_prefix'),str) and re.fullmatch(r'[a-z0-9.]{1,40}',c['service_prefix']),'Missing shared service prefix')
 need(isinstance(c.get('package_name'),str) and re.fullmatch(r'[A-Za-z0-9_.]{1,64}',c['package_name']),'Missing authorized package name')
 need(isinstance(c.get('timeout_seconds'),int) and 60<=c['timeout_seconds']<=600,'Wrong installed window deadline')
 need(Path(c['window_root']).resolve().parent==TASK_ROOT/'windows' and Path(c['package']).resolve().is_absolute(),'Foreign installed window/package path')
 # Per-member identity, not a file count: every declared member must be the actual
 # bytes of the frozen artifact it names.
 for _member in c['package_artifacts']:
  need(Path(_member['source']).name==_member['name'] and digest(_member['source'])==_member['sha256'],'Installed package member hash mismatch')
 need(c.get("main_prefix")==INSTALLED_MAIN_PREFIX and c.get("main_mode")==INSTALLED_MAIN_MODE,"Installed process identity must be the scenario exact pair")
 need(set(c['peer'])=={'A','B'} and all(re.fullmatch('[0-9a-f]{64}',v) for v in c['peer'].values()),'No current peer binding')
LIBDIRS=['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64']
OBSERVED_SCOPE='N10_OBSERVED_RETRY_AND_CONDITIONAL_N100'
FINAL_N100_AUTH='ONE_QUERY100_AND_USE_EXISTING_CONDITIONAL_N100'
OBSERVED_LIMITS=dict(canonical_windows=2,impact_regressions=0,repair_retries=0,total_attempts=2,n10_attempts=1,n100_attempts=1,query_pairs_per_window=1,max_record_bytes=8192)

def claim_path(kind,cycles):
 need(kind in ('data','query') and type(cycles) is int and cycles in (10,100),'Invalid claim purpose')
 return TASK_ROOT/'observed-retry'/'attempt-ledger'/f'{kind}-N{cycles}.json'

LEGACY_MATERIALIZED_FILES=('executor-route-query/launch.ps1', 'executor-route-query/prepare-query.py', 'executor-route-query/query_receipt.py', 'executor-route-query/route_query.py', 'executor-route-query/run-window.ps1', 'executor/bounded-output.cjs', 'executor/capture-load.sh', 'executor/load_origin.py', 'executor/observe-native.cjs', 'executor/prepare-window.py', 'executor/private_config.py', 'executor/record-pair.cjs', 'executor/route_binding.py', 'executor/run-window.ps1', 'executor/runner-template.cjs', 'executor/shared-control.cjs', 'executor/task_contract.py', 'executor/ros-control.cjs')
PRIOR_MATERIALIZED_FILES=LEGACY_MATERIALIZED_FILES+('executor/dds214-control.cjs','executor/layered_contract.py')
RECOVERY_MATERIALIZED_FILES=PRIOR_MATERIALIZED_FILES+('recovery/pregrant_recovery.py','recovery/recovery-guard.ps1','recovery/readonly_preflight.py')
MATERIALIZED_FILES=RECOVERY_MATERIALIZED_FILES+('executor/rmw_debugger.py','executor/rmw-debugger.cjs','executor/rmw_lldb_trace.py')
def verify_local_driver(x):
 # Shared local-mode binding. These are exactly the fields the PowerShell bootstrap
 # reads, so both layers validate and bind one frozen contract instead of keeping
 # two policies. A local contract test is never device evidence and never holds the
 # device admission record.
 need(isinstance(x.get('local_test_basis'),str) and x['local_test_basis'],'Local contract test needs a stated basis')
 need(x.get('device_evidence') is not True and x.get('device_authorized') is not True,'A local contract test may not claim device authority')
 need(x.get('execution_admitted') is not True,'A local contract test may not claim the device admission record')
 d=x.get('local_driver')
 need(isinstance(d,dict) and d.get('kind')=='LOCAL_FAKE_SANDBOX','Local contract test needs a LOCAL_FAKE_SANDBOX driver')
 tr=d.get('test_root');sb=d.get('sandbox_root')
 need(isinstance(tr,str) and isinstance(sb,str),'Local driver needs explicit absolute roots')
 tr=Path(tr);sb=Path(sb)
 need(tr.is_absolute() and sb.is_absolute() and tr.resolve()==TASK_ROOT,'Local test root must be this task root')
 need(sb.resolve().is_relative_to(TASK_ROOT) and sb.resolve()!=TASK_ROOT,'Local sandbox root must live inside the local test root')
 return d

def verify_installed_device_mode(x,c=None):
 need(x.get('scope') in (INSTALLED_SCOPE,ROS_SCOPE,layered().SCOPE) and x.get('device_authorized') is True,'Runtime device authorization missing')
 for key in ('local_contract_test','local_driver','local_test_basis','device_evidence'):
  need(key not in x,'Device contract contains local-test field: '+key)
 d=x.get('device_driver');need(isinstance(d,dict),'Explicit real device-tool binding required')
 p=Path(d.get('path','')).resolve()
 need(p==Path(PRIVATE['hdc_path']).resolve() and p.name.lower()=='hdc.exe' and not p.is_relative_to(TASK_ROOT),'Real device tool must be external to task, never a local driver')
 need(d.get('sha256')==digest(p),'Device tool changed')
 if c is not None:need('simulated' not in c,'Device window contains simulation marker')

def installed_claim_path(kind,attempt):
 need(kind in ('query','data') and type(attempt) is int and attempt in (1,2),'Installed claim purpose invalid')
 return TASK_ROOT/'installed-runtime/attempt-ledger'/f'{kind}-{attempt}.json'

def installed_next_attempt():
 claims=list((TASK_ROOT/'installed-runtime/attempt-ledger').glob('data-*.json'))
 need(len(claims)<2,'Installed data quota exhausted')
 if not claims:return 1
 need(len(claims)==1 and claims[0]==installed_claim_path('data',1),'Installed claim sequence invalid')
 r=json.loads(claims[0].read_text());w=Path(r['window']).resolve()
 need(r.get('scope')==INSTALLED_SCOPE and r.get('attempt')==1 and w.parent==TASK_ROOT/'windows' and r.get('kind')=='DATA_DISPATCH_INTENT','Installed prior claim invalid')
 need(json.loads((w/'DATA_ATTEMPT_STARTED').read_text())==r,'Installed dispatch marker/claim mismatch')
 require_restored(w)
 summary=json.loads((w/'window-summary.json').read_text(encoding='utf-8-sig'));result=json.loads((w/'result.json').read_text())
 load=json.loads((w/'load-collection-summary.json').read_text(encoding='utf-8-sig'))
 need(summary.get('first_failure') or result.get('pass') is not True or load.get('first_failure'),'Successful installed attempt forbids an extra attempt')
 return 2

def verify_installed_budget(c,query=False):
 attempt=c.get('installed_attempt');need(type(attempt) is int and attempt==installed_next_attempt(),'Wrong installed attempt')
 if attempt==2:
  row=c.get('retry_evidence');need(isinstance(row,dict),'Installed repair evidence required')
  p=Path(row['path']).resolve();need(p.is_relative_to(TASK_ROOT) and digest(p)==row['sha256'],'Installed repair evidence changed')
  repair=json.loads(p.read_text());need(all(repair.get(k) for k in ('root_cause','fix','affected_validation','review')),'Installed repair not reviewed/validated')
 if query:need(not installed_claim_path('query',attempt).exists(),'Installed query quota consumed')
 else:
  route=c.get('route_binding');need(isinstance(route,dict),'Installed data requires guarded route query')
  q=json.loads(installed_claim_path('query',attempt).read_text())
  need(q.get('run')==route.get('query_run') and q.get('kind')=='INSTALLED_QUERY_DISPATCH_INTENT','Installed route/query claim mismatch')

def verify_installed_query_claim(c):
 q=json.loads(installed_claim_path('query',c['installed_attempt']).read_text())
 need(q==dict(scope=INSTALLED_SCOPE,attempt=c['installed_attempt'],kind='INSTALLED_QUERY_DISPATCH_INTENT',run=c['run'],window=str(Path(c['window_root']).resolve())),'Completed installed query claim mismatch')

def materialization_record(frozen):
 p=TASK_ROOT/'MATERIALIZATION.json';need(digest(p)==frozen.get('materialization_sha256'),'Materialization descriptor missing/changed')
 record=json.loads(p.read_text(encoding='utf-8'));need(record.get('scope')=='LOCAL_MATERIALIZATION_ONLY_NO_EXECUTION_AUTHORITY' and record.get('task')==str(TASK_ROOT) and record.get('repo')==str(repo_root()) and record.get('private_config_sha256')==frozen.get('private_config_sha256'),'Materialization identity mismatch')
 rows=record.get('files',[]);names=[e['destination'].replace(chr(92),'/') for e in rows]
 current_members=set(names) in (set(MATERIALIZED_FILES),set(RECOVERY_MATERIALIZED_FILES),set(PRIOR_MATERIALIZED_FILES))
 strict=frozen.get('scope')==layered().SCOPE or frozen.get('authorization')==ROS_LAYERED_AUTHORIZATION
 need(len(names)==len(set(names)) and (current_members or (not strict and set(names)==set(LEGACY_MATERIALIZED_FILES))),'Materialized dependency membership mismatch')
 if frozen.get('authorization')==ROS_TASK_AUTHORIZATION:need(set(names) in ((set(MATERIALIZED_FILES),) if frozen.get('rmw_debugger') else (set(MATERIALIZED_FILES),set(RECOVERY_MATERIALIZED_FILES))),'Task recovery-capable execution requires current complete materialization')
 entries=frozen.get('execution_files',[]);mapped={str((repo_root()/e['path']).resolve()):e['sha256'] for e in entries};need(len(mapped)==len(entries),'Duplicate executor binding')
 required={str((TASK_ROOT/e['destination']).resolve()):e['sha256'] for e in rows}
 task_entries={p for p in mapped if any(Path(p).is_relative_to(TASK_ROOT/folder) for folder in ('executor','executor-route-query','recovery'))}
 need(task_entries==set(required) and all(mapped.get(p)==sha for p,sha in required.items()),'Required materialized executor omitted or changed')
 external=record.get('external_files',[]);names=[e.get('path') for e in external]
 legacy={'scripts/NativePermissionGuard.ps1','scripts/public_socket/analyze_shared.py'}
 current=legacy|{'scripts/public_socket/analyze_ros2.py'}
 native=current|{'scripts/public_socket/analyze_ros2_native.py'}
 direct=native|{'scripts/public_socket/analyze_dds214.py'}
 need(len(names)==len(set(names)) and set(names) in (legacy,current,native,direct) and
      (not current_members or set(names)==direct) and (frozen.get('scope')!=ROS_SCOPE or set(names) in (native,direct)) and (frozen.get('scope')!=layered().SCOPE or set(names)==direct),'Required external execution membership mismatch')
 for e in external:
  path=str(repo_root()/e['path']);need(mapped.get(path)==e.get('sha256') and isinstance(e.get('sha256'),str),'Required external execution dependency omitted or changed');required[path]=e['sha256']
 return required

def verify_shared_guard(c,frozen):
 required=materialization_record(frozen)
 need(c.get('shared_script_sha256')=={'NativePermissionGuard.ps1':required[str(repo_root()/'scripts/NativePermissionGuard.ps1')]},'Window must bind the exact reviewed external guard')
 analyzer='analyze_dds214.py' if is_dds214(c) else 'analyze_ros2.py' if is_ros(c) else 'analyze_shared.py'
 need(c.get('analyzer_sha256')==required[str(repo_root()/'scripts/public_socket'/analyzer)],'Window formal analyzer not bound')
 if is_ros(c) and frozen.get('local_contract_test') is not True:
  need(c.get('native_analyzer_sha256')==required[str(repo_root()/'scripts/public_socket/analyze_ros2_native.py')],'ROS native analyzer not bound')

def verify_materialized_execution(frozen):
 for p,sha in materialization_record(frozen).items():need(digest(p)==sha,'Materialized execution bytes changed')
 spec=importlib.util.spec_from_file_location('bound_private_config',TASK_ROOT/'executor/private_config.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.validate_profile(PRIVATE,TASK_ROOT,repo_root())

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
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text())
 if frozen.get('scope')==layered().SCOPE:return layered().claim(this_contract(),c)
 if frozen.get('scope')==ROS_SCOPE:
  if frozen.get('local_contract_test') is True:
   verify_local_driver(frozen);p=TASK_ROOT/'local-test-claims'/('query-'+c['run']+'.json')
   row=ros_query_claim_row(c,True)
  else:
   verify_installed_device_mode(frozen,c);p=ros_claim_path('query',c['ros_attempt'])
   row=ros_query_claim_row(c,False)
   predecessor=ros_previous_attempt()
   if predecessor is not None:
    with (predecessor.parent/'repair-continuation.json').open('x',encoding='utf-8') as stream:json.dump(row,stream)
  p.parent.mkdir(parents=True,exist_ok=True)
  with p.open('x',encoding='utf-8') as stream:json.dump(row,stream)
  return
 if frozen.get('scope')==INSTALLED_SCOPE and frozen.get('local_contract_test') is True:
  verify_local_driver(frozen)
  p=TASK_ROOT/'local-test-claims'/('query-'+c['run']+'.json');p.parent.mkdir(parents=True,exist_ok=True)
  with p.open('x',encoding='utf-8') as stream:json.dump(dict(scope=INSTALLED_SCOPE,local_contract_test=True,run=c['run'],window=str(Path(c['window_root']).resolve()),kind='LOCAL_QUERY_DISPATCH_INTENT',device_authority=False),stream)
  return
 if frozen.get('scope')==INSTALLED_SCOPE:
  verify_installed_device_mode(frozen,c)
  p=installed_claim_path('query',c['installed_attempt']);p.parent.mkdir(parents=True,exist_ok=True)
  with p.open('x',encoding='utf-8') as stream:json.dump(dict(scope=INSTALLED_SCOPE,attempt=c['installed_attempt'],kind='INSTALLED_QUERY_DISPATCH_INTENT',run=c['run'],window=str(Path(c['window_root']).resolve())),stream)
  return
 need(c.get('work_package')==OBSERVED_SCOPE,'Claim only for supplemental grant')
 p=claim_path('query',c['query_for_cycles']);p.parent.mkdir(parents=True,exist_ok=True)
 with p.open('x',encoding='utf-8') as stream:json.dump(dict(scope=OBSERVED_SCOPE,cycles=c['query_for_cycles'],run=c['run'],window=str(Path(c['window_root']).resolve()),kind='QUERY_DISPATCH_INTENT',remote_execution='UNOBSERVED_AT_CLAIM'),stream)
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def need(value,message):
 if not value:raise ValueError(message)
def assert_fresh_run(run):need(isinstance(run,str) and re.fullmatch('[0-9a-f]{16}',run),'Invalid run')
def validate_shape(c):
 if is_dds214(c):return layered().shape(this_contract(),c)
 if is_ros(c):return ros_shape(c)
 verify_private_binding(c)
 if is_installed(c):return installed_shape(c)
 need(c.get('scenario') in CASES,'Unknown scenario')
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
 x=json.loads(p.read_text());need(x.get('private_config_sha256')==digest(TASK_ROOT/'PRIVATE_CONFIG.json'),'Private configuration not bound by contract');need(x['task_root']==str(TASK_ROOT) and x['scope'] in ('SHARED_LIFECYCLE_EGRESS_AND_CAPTURE_CLOSURE','NEW_PATH_EXACT_ONCE_AND_BOUNDED_REBUILD_REPETITION',OBSERVED_SCOPE,INSTALLED_SCOPE,ROS_SCOPE,layered().SCOPE),'Wrong task')
 verify_materialized_execution(x)
 if x['scope']==layered().SCOPE:
  need(x.get('limits')==layered().LIMITS and x['plan_sha256']==digest(TASK_ROOT/'DDS214_CONTRACT.md'),'DDS214 frozen limits/plan')
  if x.get('local_contract_test') is True:verify_local_driver(x)
  else:
   need(x.get('execution_admitted') is True,'DDS214 final gates incomplete');layered().grant(this_contract(),x,'DDS214');verify_installed_device_mode(x)
  return {'task_root':str(TASK_ROOT),'freeze_path':str(p),'freeze_sha256':digest(p),'plan_sha256':x['plan_sha256']}
 if x['scope']==ROS_SCOPE:
  verify_ros_authorization(x);verify_ros_observed_grant(x)
  need(x['plan_sha256']==digest(TASK_ROOT/'ROS2_CONTRACT.md'),'ROS frozen plan drift')
  if x.get('local_contract_test') is True:verify_local_driver(x)
  else:
   need(x.get('execution_admitted') is True,'ROS final independent gates incomplete')
   verify_installed_device_mode(x)
  return {'task_root':str(TASK_ROOT),'freeze_path':str(p),'freeze_sha256':digest(p),'plan_sha256':x['plan_sha256']}
 if x['scope']==INSTALLED_SCOPE:
  need(x.get('limits')==INSTALLED_LIMITS,'Wrong installed limits')
  need(x['plan_sha256']==digest(TASK_ROOT/'INSTALLED_RUNTIME_CONTRACT.md'),'Installed plan drift')
  if x.get('local_contract_test') is True:
   # A local executor contract test is a separate, explicitly marked mode. It is
   # never device evidence, and it cannot claim the device admission record.
   need(x.get('device_evidence') is not True and x.get('device_authorized') is not True,'A local contract test may not claim device authority')
   verify_local_driver(x)
  else:
   need(x['execution_admitted'] is True,'Installed contract not admitted')
   verify_installed_device_mode(x)
  return {'task_root':str(TASK_ROOT),'freeze_path':str(p),'freeze_sha256':digest(p),'plan_sha256':x['plan_sha256']}
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
 need(proof==load(proof['freeze_path']),'Contract drift');x=json.loads(Path(proof['freeze_path']).read_text())
 if x.get('scope')==layered().SCOPE:
  if x.get('local_contract_test') is True:verify_local_driver(x);return
  layered().grant(this_contract(),x,'DDS214');verify_installed_device_mode(x)
 if x.get('scope')==ROS_SCOPE and x.get('local_contract_test') is True:
  verify_local_driver(x);return
 if x.get('scope')==INSTALLED_SCOPE:
  need(x.get('limits')==INSTALLED_LIMITS,'Wrong installed limits')
  if x.get('local_contract_test') is True:
   need(x.get('device_evidence') is not True,'A local contract test may not claim device evidence')
   verify_local_driver(x)
   need(x.get('plan_sha256')==digest(TASK_ROOT/'INSTALLED_RUNTIME_CONTRACT.md'),'Installed plan drift')
   return
  need(x['execution_admitted'] is True,'Independent gates incomplete')
  need(x.get('device_authorized') is True,'Installed device window not authorized by its contract')
 need(x['execution_admitted'] is True,'Independent gates incomplete')
 if x.get('scope')==ROS_SCOPE:
  verify_ros_authorization(x);verify_ros_observed_grant(x);need(x.get('device_authorized') is True,'ROS actual device grant missing')
  verify_installed_device_mode(x)
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
  need(digest(repo_root()/e['path'])==expected,'Source drift')
 if x['scope']==OBSERVED_SCOPE:
  need(bool(x.get('execution_files')),'Missing current executor selection')
  for e in x['execution_files']:need(digest(repo_root()/e['path'])==e['sha256'],'Executor selection drift')
  binding=x['build_binding'];need(digest(binding['build_manifest'])==binding['build_manifest_sha256'],'Build manifest drift')
  for e in binding['package_artifacts']:need(digest(e['source'])==e['sha256'],'Package member drift')
  need(next(e['sha256'] for e in binding['package_artifacts'] if e['name']=='public_socket_dds_experiment')==binding['main_sha256'],'Main identity drift')
  need(bool(x.get('compiled_inputs')),'Missing compile input closure')
  for e in x['compiled_inputs']:need(digest(e['path'])==e['sha256'],'Compile input drift')
def validated_window_resource_recovery(window):
 p=Path(__file__).parent/'rmw_debugger.py'
 spec=importlib.util.spec_from_file_location('current_bound_resource_recovery',p);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 return module.verify_bound_resource_recovery(window)

def _require_normal_restored(window):
 if (window/'remaining-resource-recovery/attempt-1/APPENDED_WINDOW_RESOURCE_RECOVERY.json').exists():
  validated_window_resource_recovery(window);return
 if (window/'window.json').is_file():
  cfg=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
  if cfg.get('rmw_debugger'):require_debugger_restored(window,cfg)
 _require_window_guards_restored(window)

def _require_window_guards_restored(window):
 # The normal device restoration checks remain independent of auxiliary
 # debugger closure. Neither can substitute for the other.
 summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
 if summary['cleanup_failures']:
  need((window/'window.json').is_file() and json.loads((window/'window.json').read_text(encoding='utf-8-sig')).get('identity_qualification') is True,'Prior cleanup incomplete')
  verify_recovered_identity_source(window)
 if (window/'window.json').is_file() and json.loads((window/'window.json').read_text(encoding='utf-8-sig')).get('identity_qualification') is True:
  cleanup=json.loads((window/'IDENTITY_ARTIFACT_CLEANUP.json').read_text(encoding='utf-8-sig'))
  if cleanup.get('pass') is not True:verify_recovered_identity_source(window)
  else:need(set(cleanup.get('sides',{}))=={'A','B'} and all(row.get('remote_absent') is True for row in cleanup['sides'].values()),'Prior private identity artifact restoration incomplete')
 if (window/'result.json').exists():
  result=json.loads((window/'result.json').read_text());need(not result.get('timedOut') and result.get('collectionCleanup')!='UNCONFIRMED','Prior runtime/collector termination uncertain')
 for side in ['A','B']:
  g=json.loads((window/side/'final-guard.json').read_text(encoding='utf-8-sig'));need(all(g.get(k) is True for k in ['Passed','byte_match','metadata_match','process_absent']) and g.get('service_permission')=='DENIED','Prior attempt not restored')
  for phase in ['before','after']:need(Path(g['metadata_'+phase]['evidence']).resolve()==(window/side/('metadata-'+phase)).resolve(),'Foreign prior metadata receipt')
  need(digest(window/side/'baseline.json')==digest(window/side/'after.json'),'Prior raw permission bytes differ')
  for role in ['ownership-guard.json','final-snapshot-guard.json']:need(json.loads((window/side/role).read_text(encoding='utf-8-sig'))['Passed'] is True,'Prior ownership/JSON incomplete')

def require_debugger_restored(window,cfg):
 # No target actions. Validate each support command's saved bytes independently
 # before the normal permission/process guards can permit a later window.
 if (window/'remaining-resource-recovery/attempt-1/APPENDED_WINDOW_RESOURCE_RECOVERY.json').exists():
  validated_window_resource_recovery(window);return
 manager=Path(cfg['task_contract']['task_root'])/'executor/rmw_debugger.py'
 frozen=json.loads(Path(cfg['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'))
 expected={str(Path(repo_root()/e['path']).resolve()):e['sha256'] for e in frozen['execution_files']}
 need(expected.get(str(manager.resolve()))==digest(manager),'Prior debugger source binding changed')
 for side in ('A','B'):
  root=window/side/'debugger'
  if not root.exists():continue # No manager action; normal business guards still required.
  if not (root/'RESULT.json').is_file():
   # Missing historical terminal receipts are never reconstructed in place.
   # A separately reviewed, fully inventoried read-only support closure may
   # discharge only this auxiliary obligation; normal device guards follow.
   recovery=Path(__file__).parent/'rmw_debugger.py'
   recovery_spec=importlib.util.spec_from_file_location('debugger_recovery_protocol',recovery)
   recovery_module=importlib.util.module_from_spec(recovery_spec);recovery_spec.loader.exec_module(recovery_module)
   recovery_module.verify_bound_readonly_failure_closure(window,side)
   continue
  row=json.loads((root/'RESULT.json').read_text())
  if row.get('support_closed') is False:
   need((root/'recovery/attempt-1/DEBUGGER_FORWARD_RECOVERY.json').is_file(),'Prior debugger forward recovery missing')
   recovery=Path(__file__).parent/'rmw_debugger.py'
   recovery_spec=importlib.util.spec_from_file_location('debugger_forward_recovery_protocol',recovery)
   recovery_module=importlib.util.module_from_spec(recovery_spec);recovery_spec.loader.exec_module(recovery_module)
   recovery_module.verify_bound_forward_recovery(window,side)
   continue
  need(row.get('run')==cfg['run'] and row.get('side')==side and type(row.get('pid')) is int and row['pid']>0 and row.get('manager_sha256')==digest(manager) and row.get('support_closed') is True,'Prior debugger recovery not confirmed')
  receipts=row.get('receipts');need(isinstance(receipts,dict) and receipts,'Prior debugger action evidence missing')
  for name,checksum in receipts.items():
   path=(root/name).resolve();need(path.is_relative_to(root.resolve()) and path.is_file() and digest(path)==checksum,'Prior debugger receipt drift')
  need(all(p.name in receipts for p in root.glob('*.started.json')),'Prior debugger dispatch omitted')
  labels={p.name[:-len('.started.json')] for p in root.glob('*.started.json')}
  intents={p.name[:-len('.intent.json')] for p in root.glob('*.intent.json')}
  allowed={'pid-before-attach','pid-before-tracer-proof','tracer-proof-status','tracer-proof-identity','forward-before','forward','forward-after','server','client','detach-client','inferior-status','pid-before-os-resume','owned-os-resume','inferior-after-os-resume','pid-after-detach','server-identity','server-cleanup','server-absent','forward-remove-before','forward-remove','forward-removed-check'}
  need(labels<=allowed,'Prior debugger unknown dispatch')
  need(intents==labels and all(label+'.intent.json' in receipts and (root/(label+'.result.json')).is_file() for label in intents),'Prior debugger intent incomplete')
  spec=importlib.util.spec_from_file_location('prior_debugger_protocol',manager);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  live=window/side/'load-pre-release-analysis.json';need(row.get('load_origin_phase')=='PRE_RELEASE' and digest(live)==row.get('load_origin_sha256') and digest(window/side/'load-pre-release-summary.json')==row.get('load_summary_sha256'),'Prior debugger original PID source changed')
  origin=module.prerelease_origin(window,side,row['pid'],cfg['run']);pid=row['pid'];remote=cfg['remote'][side];port=cfg['rmw_debugger']['ports'][side];node='tcp:'+str(port)
  need(origin.get('complete') is True and origin.get('run')==cfg['run'] and origin.get('board')==side and str(origin.get('pid'))==str(pid),'Prior debugger PID source identity')
  base=[cfg['hdc_path'],'-t',cfg['targets'][side]]
  server_started=(root/'server.started.json').is_file();forward_started=(root/'forward.started.json').is_file()
  need(row.get('server_started') is server_started and row.get('forward_owned') is forward_started,'Prior debugger action flags contradict dispatch')
  if row.get('status')=='INPUT_REJECTED':
   need(not server_started and not forward_started and row.get('failure') and not (root/'UNAVAILABLE.json').exists(),'Rejected debugger incorrectly admitted')
   module.readonly_failure_facts(window,side)
   continue
  def output(label,argv,remote_exit=False,allow_nonzero=False):
   for suffix in ('started.json','result.json','stdout','stderr'):need(label+'.'+suffix in receipts,'Prior debugger command evidence missing')
   intent=json.loads((root/(label+'.intent.json')).read_text());started=json.loads((root/(label+'.started.json')).read_text());r=json.loads((root/(label+'.result.json')).read_text());text=(root/(label+'.stdout')).read_text()
   need(intent.get('argv')==argv and intent.get('cwd')==str(root) and intent.get('purpose')=='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS','Prior debugger dispatch intent object mismatch')
   need(started.get('argv')==argv and r.get('argv')==argv and started.get('cwd')==str(root) and type(started.get('local_pid')) is int and started['local_pid']>0 and r.get('local_pid')==started['local_pid'] and started.get('purpose')=='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS','Prior debugger executed object mismatch')
   need((type(r.get('exit_code')) is int if allow_nonzero else r.get('exit_code')==0) and r.get('capture_complete') is True and r.get('overflow') is False and r.get('timed_out') is False and r.get('stdout_sha256')==digest(root/(label+'.stdout')) and r.get('stderr_sha256')==digest(root/(label+'.stderr')),'Prior debugger command incomplete')
   if remote_exit:need(re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\r?$',text,re.M)==['0'],'Prior debugger remote exit unconfirmed')
   return text
  before=output('pid-before-attach',base+['shell',module.process_command(pid)],True)
  module.process_identity(before,pid,remote+'/'+cfg['main_member'],origin['starttime'])
  selected_rows=lambda text:module.owned_forward_rows(text,cfg['targets'][side]) if hasattr(module,'owned_forward_rows') else module.forward_rows(text)
  previous_text=output('forward-before',base+['fport','ls']);previous=selected_rows(previous_text)
  if server_started:
   server=output('server',base+['shell',module.start_command(remote,port,pid)]);need(len(re.findall(r'^MDDS_REMOTE_EXIT=([0-9]+)\r?$',server,re.M))==1,'Prior debugger server exit unconfirmed')
   ids=re.findall(r'^MDDS_DEBUG_SERVER_PID=([1-9][0-9]*)\r?$',server,re.M)
   proof_file=root/'OWNED_TRACER.json'
   if proof_file.is_file():
    need('OWNED_TRACER.json' in receipts,'Prior debugger tracer proof omitted');proof=json.loads(proof_file.read_text())
    need(proof.get('run')==cfg['run'] and proof.get('side')==side and proof.get('pid')==pid and type(proof.get('server_pid')) is int,'Prior debugger tracer proof identity')
    main=output('pid-before-tracer-proof',base+['shell',module.process_command(pid)],True);module.process_identity(main,pid,remote+'/'+cfg['main_member'],origin['starttime'])
    tracer_status=output('tracer-proof-status',base+['shell',module.status_command(pid)],True)
    need(re.findall(r'^TracerPid:\s+([1-9][0-9]*)\s*$',tracer_status,re.M)==[str(proof['server_pid'])] and re.search(r'^State:\s+t\b',tracer_status,re.M),'Prior debugger actual tracing stop missing')
    tracer_text=output('tracer-proof-identity',base+['shell',module.process_command(proof['server_pid'])],True);identity=module.process_fields(tracer_text,proof['server_pid'])
    need(identity['exe']==remote+'/lldb-server' and identity['argv']==[remote+'/lldb-server','gdbserver','127.0.0.1:'+str(port),'--attach',str(pid)] and identity['starttime']==proof['server_starttime'] and (not ids or ids==[str(proof['server_pid'])]),'Prior debugger actual tracer not owned')
    ids=[str(proof['server_pid'])]
   need(len(ids)==1,'Prior debugger server PID unconfirmed');server_pid=int(ids[0])
   if 'server-identity' in labels:
    identity=output('server-identity',base+['shell',module.process_command(server_pid)],True)
    birth=row.get('server_starttime')
    if re.search(r'^MDDS_DEBUG_PROCESS_ABSENT=1\r?$',identity,re.M):
     need(birth==(proof['server_starttime'] if proof_file.is_file() else None),'Absent debugger birth/source contradiction')
    else:
     observed=module.process_fields(identity,server_pid)
     need(observed['exe']==remote+'/lldb-server' and observed['argv']==[remote+'/lldb-server','gdbserver','127.0.0.1:'+str(port),'--attach',str(pid)] and observed['starttime']==birth,'Prior debugger server identity mismatch')
     if proof_file.is_file():need(birth==proof['server_starttime'],'Prior debugger captured server birth changed')
    output('server-cleanup',base+['shell',module.cleanup_command(remote,port,pid,server_pid,birth)],True)
   else:
    # Historical managers retain their original cleanup command identity.
    need('server_starttime' not in row,'Current debugger missing guarded cleanup identity')
    output('server-cleanup',base+['shell',module.cleanup_command(remote,port,pid,server_pid)],True)
   output('server-absent',base+['shell',module.absent_command(server_pid)],True)
   status=output('inferior-status',base+['shell',module.status_command(pid)],True)
   if 'owned-os-resume' in labels:
    need(re.search(r'^TracerPid:\s+0\s*$',status,re.M) and re.search(r'^State:\s+T\b',status,re.M),'Prior debugger unnecessary OS resume')
    identity_text=output('pid-before-os-resume',base+['shell',module.process_command(pid)],True);module.process_identity(identity_text,pid,remote+'/'+cfg['main_member'],origin['starttime']);values=module.process_fields(identity_text,pid)
    output('owned-os-resume',base+['shell',module.resume_command(pid,values['exe'],values['starttime'],values['argv'])],True)
    status=output('inferior-after-os-resume',base+['shell',module.status_command(pid)],True)
   need(re.search(r'^MDDS_DEBUG_MAIN_ABSENT=1\r?$',status,re.M) or (re.search(r'^TracerPid:\s+0\s*$',status,re.M) and re.search(r'^State:\s+[RSIDZ]\b',status,re.M)),'Prior debugger inferior not resumed')
   if not re.search(r'^MDDS_DEBUG_MAIN_ABSENT=1\r?$',status,re.M):
    after=output('pid-after-detach',base+['shell',module.process_command(pid)],True);module.process_identity(after,pid,remote+'/'+cfg['main_member'],origin['starttime'])
  if forward_started:
   need(all(node not in r[:2] for r in previous),'Prior debugger forward was preexisting')
   if hasattr(module,'forward_entries'):need(all(node not in r[1:3] for r in module.forward_entries(previous_text)),'Prior debugger global forward selector conflict')
   need(output('forward',base+['fport',node,node]).strip()=='Forwardport result:OK','Prior debugger forward not owned')
   after_text=output('forward-after',base+['fport','ls'])
   need(selected_rows(after_text).count((node,node,'Forward'))==1,'Prior debugger forward not confirmed')
   if hasattr(module,'forward_entries'):need(all(r==(cfg['targets'][side],node,node,'Forward') for r in module.forward_entries(after_text) if node in r[1:3]),'Prior debugger foreign forward selector')
   if hasattr(module,'forward_remove_argv'):
    before_remove=output('forward-remove-before',base+['fport','ls'])
    rows=module.forward_entries(before_remove);owned=(cfg['targets'][side],node,node,'Forward')
    need(rows.count(owned)==1 and all(r==owned for r in rows if node in r[1:3]),'Prior debugger deletion selector ownership unconfirmed')
   argv=module.forward_remove_argv(base,node) if hasattr(module,'forward_remove_argv') else base+['fport','rm',node+' '+node]
   output('forward-remove',argv);text=output('forward-removed-check',base+['fport','ls'])
   need(all(node not in r[:2] for r in selected_rows(text)),'Prior debugger forward still present')
   if hasattr(module,'forward_entries'):need(all(node not in r[1:3] for r in module.forward_entries(text)),'Prior debugger global selector remains')
  for label,filename in [('client','commands.lldb'),('detach-client','detach.lldb')]:
   if label in labels:
    output(label,[cfg['rmw_debugger']['client']['path'],'--batch','--source',str(root/filename)],allow_nonzero=True)
def _local_recovery_facts(window,journal_bytes=None):
 # Evidence inspection only: never spawn a driver, acquire a claim, or restore data.
 window=Path(window).resolve();c=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
 need(window.parent==TASK_ROOT/'windows' and Path(c['window_root']).resolve()==window,'LOCAL_RECOVERY_WRONG_WINDOW')
 need(re.fullmatch('[0-9a-f]{16}',c['run']) is not None,'LOCAL_RECOVERY_WRONG_RUN')
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'))
 need(frozen.get('local_contract_test') is True,'LOCAL_RECOVERY_LOCAL_ONLY')
 for k in ('device_evidence','device_authorized','execution_admitted'):
  need(frozen.get(k) is None or frozen.get(k) is False,'LOCAL_RECOVERY_DEVICE_AUTHORITY')
 verify_private_binding(c)
 need(c['task_contract']==load(c['task_contract']['freeze_path']),'LOCAL_RECOVERY_CONTRACT_DRIFT')
 driver=verify_local_driver(frozen)
 need(c.get('scenario')=='readonly-directory-query' and c['targets']==TARGETS=={'A':'SIM-A','B':'SIM-B'} and c.get('token')==TOKENS,'LOCAL_RECOVERY_QUERY_ONLY')
 image=Path(c['hdc_path']).resolve();need(image.is_relative_to(TASK_ROOT) and image.name=='fake-hdc.exe','LOCAL_RECOVERY_DRIVER_BOUNDARY')
 marker=window/'EXECUTION_STARTED';need(marker.is_file(),'LOCAL_RECOVERY_NO_EXECUTION_MARKER')
 summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
 need(summary.get('attempted_grants')==[] and summary.get('cleanup_failures')==[],'LOCAL_RECOVERY_MUTATION_OR_CLEANUP')
 need(summary.get('process_outcome')=='NO_BUSINESS_PROCESS_STARTED' and isinstance(summary.get('first_failure'),str),'LOCAL_RECOVERY_PROCESS_OR_FAILURE_UNKNOWN')
 need(not (window/'DATA_ATTEMPT_STARTED').exists() and not (window/'result.json').exists(),'LOCAL_RECOVERY_DATA_DISPATCH')
 close=json.loads((window/'launcher.receipt.json').read_text(encoding='utf-8-sig'))
 need(close.get('synchronous') is True and type(close.get('child_exit_code')) is int and close.get('launcher_exit_code')==close.get('child_exit_code') and close.get('failure') is None,'LOCAL_RECOVERY_LAUNCHER_NOT_CLOSED')
 for key in ('launcher.child.rc','launcher.rc'):
  need(int((window/key).read_text(encoding='utf-8-sig'))==close['child_exit_code'],'LOCAL_RECOVERY_EXIT_MISMATCH')
 launch=json.loads((window/'launcher.originalcommand.json').read_text(encoding='utf-8-sig'))
 need(launch['arguments']==['-NoLogo','-NoProfile','-NonInteractive','-File',str(TASK_ROOT/'executor-route-query/run-window.ps1'),'-WindowDirectory',str(window)],'LOCAL_RECOVERY_FOREIGN_LAUNCHER')
 journal=Path(driver['sandbox_root'])/'fake-hdc-invocations.jsonl'
 raw=journal.read_bytes()
 if journal_bytes is None:journal_bytes=len(raw)
 need(type(journal_bytes) is int and 0<journal_bytes<=len(raw),'LOCAL_RECOVERY_JOURNAL_PREFIX_MISSING')
 prefix=raw[:journal_bytes];need(prefix.endswith(b'\n'),'LOCAL_RECOVERY_JOURNAL_PREFIX_TRUNCATED')
 rows=[json.loads(line) for line in prefix.decode('utf-8').splitlines() if line]
 for line in raw[journal_bytes:].decode('utf-8').splitlines():
  later=json.loads(line)
  need(later.get('test_run')!=c['run'] and not (isinstance(later.get('window_root'),str) and Path(later['window_root']).resolve()==window),'LOCAL_RECOVERY_LATE_DISPATCH')
  need(isinstance(later.get('test_run'),str) and re.fullmatch('[0-9a-f]{16}',later['test_run']) and isinstance(later.get('window_root'),str) and Path(later['window_root']).is_absolute(),'LOCAL_RECOVERY_UNATTRIBUTED_SUFFIX')
 need(all(not (isinstance(v.get('window_root'),str) and Path(v['window_root']).is_absolute() and Path(v['window_root']).resolve()==window) or v.get('test_run')==c['run'] for v in rows),'LOCAL_RECOVERY_RUN_WINDOW_CONFLICT')
 selected=[v for v in rows if v.get('test_run')==c['run']]
 commands=sorted(window.glob('[AB]/*/command.json'))
 need(bool(commands) and len(commands)==len(selected),'LOCAL_RECOVERY_DISPATCH_COUNT')
 need(len(list(window.rglob('command.json')))==len(commands),'LOCAL_RECOVERY_UNACCOUNTED_COMMAND')
 ledger=[];used=set()
 for command in commands:
  argv=json.loads(command.read_text(encoding='utf-8-sig'));side=command.parent.parent.name
  need(len(argv)==5 and Path(argv[0]).resolve()==image and argv[1:4]==['-t',c['targets'][side],'shell'],'LOCAL_RECOVERY_NON_READ_ONLY_DISPATCH')
  suffix='; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'
  need(argv[4].endswith(suffix) and argv[4].count('MDDS_REMOTE_EXIT=')==1,'LOCAL_RECOVERY_UNKNOWN_WRAPPER')
  core=argv[4][:-len(suffix)]
  readonly=(core in ('ps -A','uname -a') or re.fullmatch(r'param get [A-Za-z0-9_.]+',core) or re.fullmatch(r'sha256sum /system/[A-Za-z0-9_./+\-]+',core))
  need(bool(readonly) and '..' not in core.split('/'),'LOCAL_RECOVERY_UNKNOWN_OR_MUTATING_COMMAND')
  matches=[i for i,v in enumerate(selected) if i not in used and v.get('argv')==argv[1:]]
  need(len(matches)==1,'LOCAL_RECOVERY_DRIVER_CORRESPONDENCE');i=matches[0];used.add(i);v=selected[i]
  need(v.get('schema')==2 and isinstance(v.get('window_root'),str) and Path(v['window_root']).is_absolute() and Path(v['window_root']).resolve()==window,'LOCAL_RECOVERY_DRIVER_IDENTITY_UNRECORDED')
  need(v.get('target')==c['targets'][side] and v.get('subcommand')=='shell' and v.get('subcommand_args')==[argv[4]],'LOCAL_RECOVERY_SIDE_MISMATCH')
  need(v.get('sandbox_root')==str(Path(driver['sandbox_root']).resolve()) and v.get('driver_image_sha256')==digest(image),'LOCAL_RECOVERY_DRIVER_ENV_UNPROVEN')
  need(type(v.get('remote_rc')) is int and 0<=v['remote_rc']<=255,'LOCAL_RECOVERY_REMOTE_RC_UNKNOWN')
  need(v.get('driver_status')=='DRIVER_ACCEPTED_INVOCATION' and type(v.get('driver_exit')) is int and v.get('driver_exit')==0 and v.get('completed') is True,'LOCAL_RECOVERY_DRIVER_REJECTED_OR_OPEN')
  need(v.get('action_class')=='READ_ONLY' and v.get('mutation_intents')==[] and v.get('model_mutations')==[] and v.get('spawned_children')==[],'LOCAL_RECOVERY_MUTATION_OR_PROCESS')
  source=Path(v['driver_source']).resolve()
  need(source==image.with_suffix('.py') and source.is_relative_to(TASK_ROOT/'local-flow'),'LOCAL_RECOVERY_DRIVER_SOURCE_ORIGIN')
  snapshot=Path(v.get('driver_source_snapshot','')).resolve()
  expected_snapshot=(Path(driver['sandbox_root'])/'driver-sources'/(str(v.get('driver_sha256'))+'.py')).resolve()
  need(snapshot==expected_snapshot and snapshot.is_relative_to(Path(driver['sandbox_root']).resolve()) and digest(snapshot)==v.get('driver_sha256'),'LOCAL_RECOVERY_DRIVER_VERSION_UNPROVEN')
  need(type(v.get('pid')) is int and v['pid']>0,'LOCAL_RECOVERY_PID_UNKNOWN')
  if sys.platform=='win32':
   import ctypes
   api=ctypes.WinDLL('kernel32',use_last_error=True);api.OpenProcess.restype=ctypes.c_void_p
   api.GetExitCodeProcess.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong)];api.CloseHandle.argtypes=[ctypes.c_void_p]
   handle=api.OpenProcess(0x1000,False,v['pid'])
   if handle:
    code=ctypes.c_ulong();ok=api.GetExitCodeProcess(handle,ctypes.byref(code));api.CloseHandle(handle)
    need(ok and code.value!=259,'LOCAL_RECOVERY_PROCESS_NOT_ENDED')
   else: need(ctypes.get_last_error()==87,'LOCAL_RECOVERY_PROCESS_STATUS_UNKNOWN')
  else:
   import os
   try:os.kill(v['pid'],0)
   except ProcessLookupError:pass
   else:need(False,'LOCAL_RECOVERY_PROCESS_NOT_ENDED')
  files=[command,command.parent/'stdout.log',command.parent/'stderr.log',command.parent/'exit-code.txt']
  need(int(files[-1].read_text(encoding='utf-8-sig'))==0,'LOCAL_RECOVERY_LOCAL_EXIT_FAILED')
  ledger.append(dict(side=side,command=core,classification='READ_ONLY',driver_entry=i,driver_sha256=v['driver_sha256'],receipts={str(f.relative_to(window)):digest(f) for f in files}))
 return dict(schema='LOCAL_ACTION_JOURNAL/v1',run=c['run'],window=str(window),local_contract_test=True,
   completeness='FORMAL_SYNCHRONOUS_LAUNCHER_AND_BIJECTIVE_DRIVER_RECEIPTS',complete=True,
   first_failure=summary['first_failure'],execution_started_sha256=digest(marker),window_sha256=digest(window/'window.json'),
   contract_sha256=digest(c['task_contract']['freeze_path']),driver_invocation_journal_sha256=hashlib.sha256(prefix).hexdigest(),driver_invocation_journal_bytes=journal_bytes,
   launcher_sha256={name:digest(window/name) for name in ('launcher.receipt.json','launcher.originalcommand.json','launcher.child.rc','launcher.rc','window-summary.json')},
   executed_command_count=len(ledger),actions=ledger,mutation_intents=[],attempted_grants=[],business_process_started=False,data_attempt_started=False)

def seal_local_actions(window):
 window=Path(window).resolve();facts=_local_recovery_facts(window);p=window/'LOCAL_ACTION_JOURNAL.json'
 need(not p.exists(),'LOCAL_RECOVERY_REFUSE_JOURNAL_OVERWRITE')
 with p.open('x',encoding='utf-8') as out:json.dump(facts,out,indent=2)
 return facts

def recover_local_no_state_change(window):
 window=Path(window).resolve();journal=window/'LOCAL_ACTION_JOURNAL.json'
 sealed=json.loads(journal.read_text(encoding='utf-8'));facts=_local_recovery_facts(window,sealed.get('driver_invocation_journal_bytes'))
 need(json.dumps(json.loads(journal.read_text(encoding='utf-8')),sort_keys=True)==json.dumps(facts,sort_keys=True),'LOCAL_RECOVERY_ACTION_JOURNAL_INCOMPLETE_OR_TAMPERED')
 receipt=dict(facts,action_journal_sha256=digest(journal),decision='NO_STATE_CHANGE_CONFIRMED',
   recovery_source_sha256=digest(__file__),recorded_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
   grant='NOT_PERFORMED',deployment='NOT_PERFORMED',business_process='NOT_STARTED')
 p=window/'NO_STATE_CHANGE_RECOVERY.json';need(not p.exists(),'LOCAL_RECOVERY_REFUSE_RECEIPT_OVERWRITE')
 with p.open('x',encoding='utf-8') as out:json.dump(receipt,out,indent=2)
 return receipt

def validate_local_no_state_change(window):
 window=Path(window).resolve();journal=window/'LOCAL_ACTION_JOURNAL.json'
 sealed=json.loads(journal.read_text(encoding='utf-8'));facts=_local_recovery_facts(window,sealed.get('driver_invocation_journal_bytes'))
 need(json.dumps(json.loads(journal.read_text(encoding='utf-8')),sort_keys=True)==json.dumps(facts,sort_keys=True),'LOCAL_RECOVERY_ACTION_JOURNAL_INCOMPLETE_OR_TAMPERED')
 receipt=json.loads((window/'NO_STATE_CHANGE_RECOVERY.json').read_text(encoding='utf-8'))
 need(json.dumps({k:receipt.get(k) for k in facts},sort_keys=True)==json.dumps(facts,sort_keys=True),'LOCAL_RECOVERY_RECEIPT_DRIFT')
 need(receipt.get('grant')=='NOT_PERFORMED' and receipt.get('deployment')=='NOT_PERFORMED' and receipt.get('business_process')=='NOT_STARTED','LOCAL_RECOVERY_FALSE_RESTORATION_CLAIM')
 need(receipt.get('decision')=='NO_STATE_CHANGE_CONFIRMED' and receipt.get('action_journal_sha256')==digest(journal) and receipt.get('recovery_source_sha256')==digest(__file__),'LOCAL_RECOVERY_INVALID_SEAL')
 return 'RESTORED_FOR_LOCAL_TEST_CONTINUATION'

def require_restored(window):
 try:return _require_normal_restored(window)
 except (OSError,ValueError,KeyError,TypeError) as original:
  # A separately accepted real pregrant recovery is not a local NO_STATE receipt.
  try:
   c=json.loads((Path(window)/'window.json').read_text(encoding='utf-8-sig'))
   f=json.loads(Path(c['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'))
  except (OSError,ValueError,KeyError,TypeError):raise original
  if (Path(window)/'recovery').is_dir() and c.get('scenario')==ROS_SCENARIO:
   return appended_pregrant_recovery(window)
  if f.get('local_contract_test') is not True or not (Path(window)/'NO_STATE_CHANGE_RECOVERY.json').exists():raise original
  return validate_local_no_state_change(window)

def verify_v1_window(c,frozen):
 if not frozen.get('v1_successor_authorization'):return
 spec=importlib.util.spec_from_file_location('window_v1_private',TASK_ROOT/'executor/private_config.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 v=module.validate_v1_deployment(PRIVATE,repo_root())
 need(v is not None and c.get('v1_deployment')==v,'Window V1 deployment differs from controlled input')
 need(c.get('package_name')==PRIVATE['ros2_package_name'] and c.get('service_prefix')==PRIVATE['ros2_service_prefix'],'Window native package/prefix changed')
 analyzer=c.get('v1_locator_analyzer',{});canonical=repo_root()/'scripts/public_socket/analyze_v1_locators.py'
 need(analyzer==frozen.get('v1_locator_analyzer') and Path(analyzer.get('path','')).resolve()==canonical.resolve() and digest(canonical)==analyzer.get('sha256'),'V1 independent analyzer not frozen')
 root=Path(c['window_root'])
 for side in 'AB':
  path=root/side/'aux/ros.conf'
  if not root.exists():continue # First preparation pass; actual launch always has the files.
  need(path.is_file(),'Generated V1 ROS config missing')
  rows=[line.split('=',1) for line in path.read_text(encoding='utf-8').splitlines()]
  need(all(len(row)==2 for row in rows) and len({row[0] for row in rows})==len(rows),'Generated V1 ROS config duplicate/invalid field')
  values=dict(rows);other='B' if side=='A' else 'A'
  expected=dict(locator_version='V1',native_name_contract='PACKAGE_SCOPED_V1',namespace=v['namespace'],domain=str(v['domain']),profile=v['profile'],local_udid=v['udids'][side],peer_udid=v['udids'][other],service_slot=str(v['slots'][side]),peer_slot=str(v['slots'][other]),local_initiator_name=v['initiator_names'][side],peer_initiator_name=v['initiator_names'][other],authorized_package=c['package_name'],service_prefix=c['package_name']+'.'+c['service_prefix'])
  need(all(values.get(key)==value for key,value in expected.items()),'Generated V1 ROS endpoint/name contract mismatch')
  if 'auxiliary_sha256' in c:need(c['auxiliary_sha256'][side].get('ros.conf')==digest(path),'Generated V1 ROS config bytes drift')

def verify_v1_identity_correspondence(c):
 acceptance=json.loads((repo_root()/ROS_V1_RECOVERED_SOURCE_RELATIVE).read_text(encoding='utf-8-sig'))
 accepted=verify_recovered_identity_source(acceptance['source_window'])
 result=json.loads((Path(accepted['recovery_directory'])/'RESULT.json').read_text(encoding='utf-8-sig'))
 for side in 'AB':
  row=result['sides'][side];expected=dict(path=str(Path(row['local_path']).resolve()),sha256=row['sha256'])
  actual=c['v1_deployment']['udid_receipts'][side]
  need(actual==expected and digest(expected['path'])==expected['sha256'],'V1 identity must come from corresponding accepted board export')

def verify_window(c):
 validate_shape(c);need(c['task_contract']==load(c['task_contract']['freeze_path']),'Contract changed')
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text());verify_shared_guard(c,frozen)
 need(not (frozen.get('v1_successor_authorization') and frozen.get('identity_qualification') is True),'Identity prerequisite contract cannot dispatch ROS data')
 if is_dds214(c):
  need(frozen['scope']==layered().SCOPE,'DDS214 work package mismatch')
  layered().budget(this_contract(),c)
  for key in ['build_manifest','build_manifest_sha256','source_state','source_state_sha256','main_sha256']:need(c[key]==frozen['build_binding'][key],'DDS214 build drift:'+key)
  need(c['package_artifacts']==frozen['build_binding']['package_artifacts'] and c['permission_helper']==frozen['permission_helper'],'DDS214 input drift')
  pre=Path(c['preflight_config']).resolve();need(pre.is_relative_to(TASK_ROOT) and digest(pre)==c['preflight_config_sha256'],'DDS214 preflight drift');expected=json.loads(pre.read_text(encoding='utf-8-sig'))
  need(c['platform']==expected['platform'] and c['required_platform_paths']==expected['required_platform_paths'],'DDS214 platform allowlist drift')
  need(Path(c['window_root']).resolve().parent==TASK_ROOT/'windows','DDS214 window root')
  for marker in (TASK_ROOT/'windows').glob('*/EXECUTION_STARTED'):
   if marker.parent!=Path(c['window_root']).resolve():require_restored(marker.parent)
  return
 if is_ros(c):
  need(frozen['scope']==ROS_SCOPE,'ROS work package mismatch')
  verify_v1_window(c,frozen)
  if frozen.get('v1_successor_authorization'):verify_v1_identity_correspondence(c)
  need(type(frozen.get('paired_runtime_close',False)) is bool and type(c.get('paired_runtime_close',False)) is bool and
       c.get('paired_runtime_close',False)==frozen.get('paired_runtime_close',False),'ROS paired shutdown contract drift')
  if frozen.get('local_contract_test') is True:
   verify_local_driver(frozen);need(c.get('simulated') is True,'ROS local simulation marker missing')
  else:
   need('simulated' not in c,'ROS real window contains simulation')
   verify_installed_device_mode(frozen,c);verify_ros_budget(c)
  for key in ['build_manifest','build_manifest_sha256','source_state','source_state_sha256','main_sha256']:
   need(c[key]==frozen['build_binding'][key],'ROS build/source drift:'+key)
  need(c['package_artifacts']==frozen['build_binding']['package_artifacts'],'ROS package member drift')
  need(c['permission_helper']==frozen['permission_helper'],'ROS permission helper drift')
  for marker in (TASK_ROOT/'windows').glob('*/EXECUTION_STARTED'):
   if marker.parent!=Path(c['window_root']).resolve():require_restored(marker.parent)
  return
 if is_installed(c):
  need(c.get('work_package')==INSTALLED_SCOPE and frozen['scope']==INSTALLED_SCOPE,'Wrong installed work package')
  need(c.get('simulated') is True if frozen.get('local_contract_test') is True else c.get('simulated') is not True,'Installed window must match its contract mode')
  if frozen.get('local_contract_test') is not True:
   verify_installed_device_mode(frozen,c);verify_installed_budget(c)
  for key in ['build_manifest','build_manifest_sha256','source_state','source_state_sha256','main_sha256']:need(c[key]==frozen['build_binding'][key],'Package/source drift: '+key)
  need(c['package_artifacts']==frozen['build_binding']['package_artifacts'],'Member drift');need(c['permission_helper']==frozen['permission_helper'],'Helper drift');need(c['header_sha256']==frozen['header_sha256'],'Header drift')
  pre=Path(c['preflight_config']).resolve();need(pre.is_relative_to(TASK_ROOT) and digest(pre)==c['preflight_config_sha256'],'Preflight drift');expected=json.loads(pre.read_text(encoding='utf-8-sig'))
  need(c['platform']==expected['platform'] and c['required_platform_paths']==expected['required_platform_paths'] and bool(c['required_platform_paths']),'Platform/mapping allowlist drift')
  w=Path(c['window_root']).resolve();need(w.parent==TASK_ROOT/'windows','Foreign window')
  for p in (TASK_ROOT/'windows').glob('*/window.json'):
   if p.parent==w:continue
   if (p.parent/'DATA_ATTEMPT_STARTED').exists() and (p.parent/'EXECUTION_STARTED').exists():require_restored(p.parent)
  prior=[p for p in (TASK_ROOT/'windows').glob('*/DATA_ATTEMPT_STARTED') if p.parent!=w and json.loads((p.parent/'window.json').read_text(encoding='utf-8-sig')).get('scenario')==INSTALLED_SCENARIO]
  need(len(prior)<frozen['limits']['total_attempts'],'Installed data-attempt limit reached')
  if prior:
   row=c.get('retry_evidence');need(isinstance(row,dict),'Repeated installed case requires concrete repair')
   path=Path(row['path']).resolve();need(path.is_relative_to(TASK_ROOT) and digest(path)==row['sha256'],'Repair receipt drift')
   repair=json.loads(path.read_text());need(repair.get('root_cause') and repair.get('fix') and repair.get('affected_validation') and repair.get('review'),'Unsubstantiated repair')
   need(sum('retry_evidence' in json.loads((p.parent/'window.json').read_text(encoding='utf-8-sig')) for p in prior)<frozen['limits']['repair_retries'],'Installed repair limit reached')
  return
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
def verify_identity_query_binding(c,frozen):
 identity=frozen.get('identity_qualification',False)
 need(type(identity) is bool and type(c.get('identity_qualification',False)) is bool and c.get('identity_qualification',False)==identity,'Identity query mode mismatch')
 if identity:
  need(verify_v1_successor(frozen,verify_ros_task_grant(frozen)) is not None,'Identity query missing V1 successor')
  bound=frozen.get('identity_query_helper',{});image=Path(bound.get('path','')).resolve()
  need(image==Path(PRIVATE['query_helper_path']).resolve() and c.get('query_helper')==dict(path=str(image),sha256=bound.get('sha256')) and image.stat().st_size==bound.get('size') and digest(image)==bound.get('sha256'),'Identity helper binding drift')
  source=bound.get('source',{});need(source==dict(path='probes/softbus_record_probe.cpp',sha256=digest(repo_root()/'probes/softbus_record_probe.cpp')),'Identity helper current source drift')
  proof=bound.get('binding_receipt',{});receipt_path=Path(proof.get('path','')).resolve()
  need(digest(receipt_path)==proof.get('sha256'),'Identity helper compile receipt drift')
  receipt=json.loads(receipt_path.read_text(encoding='utf-8-sig'))
  need(receipt.get('schema')=='IDENTITY_HELPER_TARGET_COMPILE' and receipt.get('scope')=='COMPILE_LINK_ONLY_NO_DEVICE' and receipt.get('compile_exit')==0 and receipt.get('artifact')==dict(path=str(image),bytes=bound['size'],sha256=bound['sha256']),'Identity helper compile correspondence')
  rows=receipt.get('inputs',[]);need(bool(rows) and any(Path(row['path']).resolve()==repo_root()/'probes/softbus_record_probe.cpp' for row in rows),'Identity helper input closure missing')
  for row in rows:need(digest(row['path'])==row['sha256'],'Identity helper input drift')
  closure_binding=bound.get('dependency_closure',{});closure_path=Path(closure_binding.get('path','')).resolve()
  need(digest(closure_path)==closure_binding.get('sha256'),'Identity helper dependency closure receipt drift')
  closure=json.loads(closure_path.read_text(encoding='utf-8-sig'))
  need(closure.get('schema')=='IDENTITY_HELPER_TARGET_COMPILE' and closure.get('scope')=='COMPILE_LINK_ONLY_NO_DEVICE' and closure.get('compile_link_exit')==0 and closure.get('device_operations')==0,'Identity helper dependency closure scope')
  correspondence=closure.get('artifact_correspondence',{})
  need(correspondence.get('same_bytes') is True and correspondence.get('original_binding_receipt_sha256')==proof['sha256'] and correspondence.get('original_artifact_sha256')==bound['sha256'] and correspondence.get('fresh_artifact_sha256')==bound['sha256'],'Identity helper dependency artifact mismatch')
  compiler=closure.get('compiler',{});need(digest(compiler['path'])==compiler.get('sha256'),'Identity compiler drift')
  libraries=closure.get('direct_link_libraries',[]);need(len(libraries)==3,'Identity direct library closure')
  for row in libraries:need(digest(row['path'])==row['sha256'],'Identity direct link library drift')
  units=closure.get('translation_units',[]);need(len(units)==4 and closure.get('closure',{}).get('translation_unit_count')==4 and closure.get('closure',{}).get('system_headers_included') is True,'Identity compiler dependency coverage incomplete')
  total=0
  for unit in units:
   need(digest(unit['depfile'])==unit.get('depfile_sha256'),'Identity dependency file drift')
   dependencies=unit.get('dependencies',[]);need(len(dependencies)==unit.get('dependency_count') and bool(dependencies),'Identity dependency list incomplete');total+=len(dependencies)
   for row in dependencies:need(digest(row['path'])==row['sha256'],'Identity compiler dependency drift')
  need(total==closure['closure'].get('all_dependency_entries'),'Identity dependency count mismatch')
  need(c.get('private_identity_output')=={side:c['remote'][side]+'/local-udid.bin' for side in 'AB'},'Identity private output mismatch')
  need(c.get('command_contract')=='--list-nodes --check-identities --identity-output <exact-window>/local-udid.bin --expected-token <board-token> --preflight-confirmed','Identity query command mismatch')

def verify_query_context(c):
 verify_private_binding(c)
 need(c['task_contract']==load(c['task_contract']['freeze_path']),'Query contract drift');assert_fresh_run(c['run'])
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text());verify_shared_guard(c,frozen);need(not frozen.get('resume_authorization'),'No new query permitted by resume authorization');pre=Path(c['preflight_config']).resolve()
 verify_identity_query_binding(c,frozen)
 need(pre.is_relative_to(TASK_ROOT) and digest(pre)==c['preflight_config_sha256'],'Query preflight drift');expected=json.loads(pre.read_text(encoding='utf-8-sig'))
 need(c['platform']==expected['platform'] and c['permission_helper']==frozen['permission_helper'],'Query platform/helper drift')
 need(c['scenario']=='readonly-directory-query' and c['targets']==TARGETS and c['token']==TOKENS,'Query identity')
 need(c['remote']=={s:f"/data/local/tmp/mdds_dds_{c['run']}_{s}" for s in ['A','B']} and c['system_library_dirs']==LIBDIRS,'Query paths')
 need(Path(c['window_root']).resolve().parent==TASK_ROOT/'windows','Foreign query')
 if frozen['scope']==ROS_SCOPE:
  need(c.get('work_package')==ROS_SCOPE and c.get('query_for_scenario')==ROS_SCENARIO,'ROS query purpose mismatch')
 if frozen['scope']==layered().SCOPE:need(c.get('work_package')==layered().SCOPE and c.get('query_for_scenario')==layered().SCENARIO,'DDS214 query purpose mismatch')
 for p in (TASK_ROOT/'windows').glob('*/EXECUTION_STARTED'):
  if p.parent!=Path(c['window_root']).resolve():require_restored(p.parent)

def verify_query(c):
 verify_query_context(c)
 frozen=json.loads(Path(c['task_contract']['freeze_path']).read_text())
 if frozen['scope']==layered().SCOPE:layered().budget(this_contract(),c,query=True)
 if frozen['scope']==OBSERVED_SCOPE:verify_observed_budget(c,frozen,query=True)
 if frozen['scope']==INSTALLED_SCOPE and frozen.get('local_contract_test') is not True:
  verify_installed_device_mode(frozen,c);verify_installed_budget(c,query=True)
 if frozen['scope']==ROS_SCOPE and frozen.get('local_contract_test') is not True:
  verify_installed_device_mode(frozen,c);verify_ros_budget(c,query=True)
if __name__=='__main__':
 p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True)
 g.add_argument('--check-query');g.add_argument('--check-window');g.add_argument('--claim-query')
 g.add_argument('--admit-ros-query-dispatch');p.add_argument('--side',choices=['A','B'])
 g.add_argument('--seal-local-actions');g.add_argument('--recover-local-no-state-change');a=p.parse_args()
 if a.seal_local_actions or a.recover_local_no_state_change:
  result=(seal_local_actions if a.seal_local_actions else recover_local_no_state_change)(a.seal_local_actions or a.recover_local_no_state_change)
  print(json.dumps(result,indent=2))
 else:
  c=json.loads(Path(a.check_query or a.check_window or a.claim_query or a.admit_ros_query_dispatch).read_text(encoding='utf-8-sig'));execution_admission(c['task_contract'])
  if a.admit_ros_query_dispatch:admit_ros_query_dispatch(c,a.side)
  else:(claim_query if a.claim_query else verify_query if a.check_query else verify_window)(c)
  print('PASS frozen DDS execution receipt')
