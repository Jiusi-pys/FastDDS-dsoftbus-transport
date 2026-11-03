"""Actual contract/claim code in isolated fixtures; no driver or device authority."""
import copy,json,hashlib,importlib.util,tempfile,shutil,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
AUTH='TASK_SCOPED_ROS2_DEVICE_VALIDATION'
class TaskScoped(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)/'task';(self.root/'executor').mkdir(parents=True)
  shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py',self.root/'executor/task_contract.py')
  self.private=dict(targets={'A':'UNIT_A','B':'UNIT_B'},tokens={'A':'1','B':'2'},ros2_package_name='unit.declared',ros2_service_prefix='unit.prefix')
  (self.root/'PRIVATE_CONFIG.json').write_text(json.dumps(self.private))
  spec=importlib.util.spec_from_file_location('task_scoped_fixture',self.root/'executor/task_contract.py');self.m=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.m)
  self.grant=dict(authorization=AUTH,task_id='ROS2_UNIT_TASK',task_root=str(self.root),purpose='ROS2_PARTICIPANT_DELETE_AND_FINAL_DUAL_BOARD_ACCEPTANCE',targets=self.private['targets'],scenario='ROS2_DSOFTBUS',historical_refund=False,git_authorized=False,
   per_window_limits=dict(timeout_seconds=300,per_side_writes=10,per_side_takes=10,record_count=20,stdout_budget=25165824,max_record_bytes=8192,stages=7))
  self.anchor=self.root/'AUTHORIZATION.json';self.anchor.write_text(json.dumps(self.grant))
  # Test-owned trust root only in this loaded module. No production source flag.
  self.m.ROS_TASK_GRANT_PATH=self.anchor;self.m.ROS_TASK_GRANT_SHA256=self.sha(self.anchor)
  self.f=dict(scope=self.m.ROS_SCOPE,authorization=AUTH,task_root=str(self.root),task_authority_root=str(self.root),execution_iteration_id=1,task_id=self.grant['task_id'],authorization_record={'path':str(self.anchor),'sha256':self.sha(self.anchor)},limits=self.grant['per_window_limits'],device_authorized=True,local_contract_test=False,device_evidence=True)
  self.save()
 def sha(self,p):return hashlib.sha256(p.read_bytes()).hexdigest()
 def save(self):(self.root/'DEVICE_CONTRACT.json').write_text(json.dumps(self.f))
 def test_new_mode_passes_exact_authority_and_window_bounds(self):
  self.m.verify_ros_authorization(self.f);self.m.verify_ros_task_grant(self.f)
 def test_wrong_anchor_root_target_local_or_expanded_window_rejected(self):
  for key,value in [('task_root',str(self.root.parent)),('task_id','FOREIGN'),('local_contract_test',True),('limits',dict(self.f['limits'],record_count=10))]:
   with self.subTest(key=key):
    bad=dict(self.f,**{key:value});self.assertRaises(ValueError,self.m.verify_ros_task_grant,bad)
  bad=copy.deepcopy(self.f);bad['authorization_record']['sha256']='0'*64;self.assertRaises(ValueError,self.m.verify_ros_task_grant,bad)
 def test_positive_ids_above_old_two_only_in_task_mode(self):
  self.assertEqual(self.m.ros_claim_path('data',3),self.root/'ros2/task-ledger/data-3.json')
  for number in [0,-1,True,3.0,'3']:self.assertRaises(ValueError,self.m.ros_claim_path,'data',number)
  self.f['authorization']=self.m.ROS_OBSERVED_AUTHORIZATION;self.save();self.assertRaises(ValueError,self.m.ros_claim_path,'data',3)
 def test_pending_query_without_completed_receipts_blocks_next(self):
  p=self.m.ros_claim_path('query',1);p.parent.mkdir(parents=True);p.write_text(json.dumps({'scope':self.m.ROS_SCOPE,'attempt':1,'run':'1234567890abcdef','window':str(self.root/'windows/q1'),'task_id':self.grant['task_id']}))
  self.assertRaises(ValueError,self.m.ros_next_attempt)
 def test_ledger_does_not_refund_or_reinterpret_old_quota(self):
  old=self.root/'ros2/attempt-ledger';old.mkdir(parents=True);(old/'data-1.json').write_text('OLD_IMMUTABLE')
  self.assertEqual(self.m.ros_next_attempt(),1);self.assertEqual((old/'data-1.json').read_text(),'OLD_IMMUTABLE')

 def test_debugger_recovery_requires_hashed_remote_closure(self):
  # Unit receipts only. Neither task authority nor real device evidence.
  manager=self.root/'executor/rmw_debugger.py';shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py',manager)
  self.f['execution_files']=[dict(path=str(manager),sha256=self.sha(manager))];self.save()
  window=self.root/'windows/debugger-unit';window.mkdir(parents=True)
  cfg=dict(run='1234567890abcdef',task_contract=dict(task_root=str(self.root),freeze_path=str(self.root/'DEVICE_CONTRACT.json')),rmw_debugger=dict(ports=dict(A=19001,B=19002)),
           hdc_path='INERT_UNIT_HDC_NOT_EXECUTED',targets=self.private['targets'],remote=dict(A='/unit/a',B='/unit/b'),main_member='unit-main')
  root=window/'A/debugger';root.mkdir(parents=True)
  # Hash-bound protocol UNIT receipts, never real live capture or authority.
  load=window/'A/load';load.mkdir()
  names=['load-status.txt','load-exe.txt','load-cwd.txt','load-cmdline.txt','load-environ.txt','load-stat-before.txt','load-stat-after.txt','load-uptime.txt','load-maps.txt']
  for name in names:(load/name).write_text('INERT_UNIT_PRERELEASE_'+name)
  (load/'load-collector.json').write_text('{}')
  origin=window/'A/load-pre-release-analysis.json';origin.write_text(json.dumps(dict(complete=True,run=cfg['run'],board='A',pid='123',starttime='987',
   scope='LIVE_PROC_SNAPSHOT_NOT_FULL_SDK_MODULE_CLOSURE',errors=[],evidence_sha256={p.name:self.sha(p) for p in load.iterdir()})))
  summary=window/'A/load-pre-release-summary.json';summary.write_text(json.dumps(dict(run=cfg['run'],side='A',host_collector_test=False,
   **{'pass':True},formal_complete=True,formal_exit=0,formal_child_closed=True,collection_cleanup='CHILD_STREAMS_CLOSED',formal_report_sha256=self.sha(origin),
   transfer=[dict(name=name,**{'pass':True},present=True,closed=True,sha256=self.sha(load/name)) for name in names])))
  spec=importlib.util.spec_from_file_location('unit_debugger_protocol',manager);protocol=importlib.util.module_from_spec(spec);spec.loader.exec_module(protocol)
  base=[cfg['hdc_path'],'-t',cfg['targets']['A']]
  def command(label,stdout,args):
   argv=base+args
   (root/(label+'.intent.json')).write_text(json.dumps(dict(argv=argv,cwd=str(root),purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS')))
   (root/(label+'.started.json')).write_text(json.dumps(dict(argv=argv,cwd=str(root),local_pid=1234,purpose='RMW_DEBUGGER_SUPPORT_NOT_BUSINESS')))
   (root/(label+'.stdout')).write_text(stdout);(root/(label+'.stderr')).write_text('')
   (root/(label+'.result.json')).write_text(json.dumps(dict(argv=argv,local_pid=1234,exit_code=0,capture_complete=True,overflow=False,timed_out=False,stdout_sha256=self.sha(root/(label+'.stdout')),stderr_sha256=self.sha(root/(label+'.stderr')))))
  command('pid-before-attach','MDDS_DEBUG_EXE=/unit/a/unit-main\nMDDS_DEBUG_CMDLINE_HEX_BEGIN\n'+b'/unit/a/unit-main\0'.hex(' ')+'\nMDDS_DEBUG_CMDLINE_HEX_END\nMDDS_DEBUG_STAT=123 (unit-main) S '+' '.join(['0']*18)+' 987\nMDDS_REMOTE_EXIT=0\n',['shell',protocol.process_command(123)])
  command('forward-before','[Empty]\n',['fport','ls']);command('forward','Forwardport result:OK\n',['fport','tcp:19001','tcp:19001'])
  command('forward-after',cfg['targets']['A']+' tcp:19001 tcp:19001 [Forward]\n',['fport','ls'])
  command('forward-remove-before',cfg['targets']['A']+' tcp:19001 tcp:19001 [Forward]\n',['fport','ls'])
  command('server-cleanup','MDDS_REMOTE_EXIT=0\n',['shell',protocol.cleanup_command('/unit/a',19001,123,456)])
  command('server-identity','MDDS_DEBUG_PROCESS_ABSENT=1\nMDDS_REMOTE_EXIT=0\n',['shell',protocol.process_command(456)])
  command('server-absent','MDDS_REMOTE_EXIT=0\n',['shell',protocol.absent_command(456)])
  command('server','MDDS_DEBUG_SERVER_PID=456\nMDDS_REMOTE_EXIT=0\n',['shell',protocol.start_command('/unit/a',19001,123)])
  command('inferior-status','MDDS_DEBUG_MAIN_ABSENT=1\nMDDS_REMOTE_EXIT=0\n',['shell',protocol.status_command(123)])
  command('forward-remove','Remove forward ruler success\n',protocol.forward_remove_argv(base,'tcp:19001')[3:]);command('forward-removed-check','[Empty]\n',['fport','ls'])
  row=dict(run=cfg['run'],side='A',pid=123,manager_sha256=self.sha(manager),support_closed=True,server_started=True,server_starttime=None,forward_owned=True,
           load_origin_sha256=self.sha(origin),load_origin_phase='PRE_RELEASE',load_summary_sha256=self.sha(summary),receipts={p.name:self.sha(p) for p in root.iterdir()})
  receipt=root/'RESULT.json';receipt.write_text(json.dumps(row))
  self.m.require_debugger_restored(window,cfg)
  for key,value in [('support_closed',False),('run','f'*16),('manager_sha256','0'*64),('server_started',False),('forward_owned',False),('load_origin_phase','POSTRUN'),('load_summary_sha256','0'*64)]:
   with self.subTest(key=key):receipt.write_text(json.dumps(dict(row,**{key:value})));self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg)
  receipt.write_text(json.dumps(row))
  original_raw=(load/'load-maps.txt').read_text();(load/'load-maps.txt').write_text('TAMPERED_UNIT_BYTES')
  self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg)
  (load/'load-maps.txt').write_text(original_raw)
  unknown=root/'unknown.started.json';unknown.write_text('{}')
  bad=copy.deepcopy(row);bad['receipts'][unknown.name]=self.sha(unknown);receipt.write_text(json.dumps(bad));self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg);unknown.unlink()
  intent=root/'client.intent.json';intent.write_text('{}')
  bad=copy.deepcopy(row);bad['receipts'][intent.name]=self.sha(intent);receipt.write_text(json.dumps(bad));self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg);intent.unlink()
  receipt.write_text(json.dumps(row));original=(root/'server-cleanup.stdout').read_text();(root/'server-cleanup.stdout').write_text('MDDS_REMOTE_EXIT=1\n')
  self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg)
  (root/'server-cleanup.stdout').write_text(original);(root/'server-absent.stdout').unlink()
  self.assertRaises(ValueError,self.m.require_debugger_restored,window,cfg)

 def test_debugger_ros_shape_uses_actual_frozen_contract(self):
  spec=dict(ports=dict(A=19001,B=19002),hit_limit=256,seconds=260)
  for key,name in [('client','lldb.exe'),('server','lldb-server'),('script','rmw_lldb_trace.py')]:
   path=self.root/'executor'/name;path.write_text('INERT_UNIT_INPUT')
   spec[key]=dict(path=str(path),sha256=self.sha(path))
  self.f['rmw_debugger']=spec;self.save()
  main=self.root/'ordinary_ros2_transport_consumer';main.write_text('INERT_UNIT_INPUT')
  cfg=dict(scenario='ROS2_DSOFTBUS',work_package=self.m.ROS_SCOPE,main_member=main.name,main_prefix='ROS_MAIN',main_mode='dual',run='1234567890abcdef',
           targets=self.private['targets'],token=self.private['tokens'],remote={side:'/data/local/tmp/mdds_dds_1234567890abcdef_'+side for side in 'AB'},
           expected_writes=dict(A=10,B=10),record_count=20,stages=self.m.ROS_STAGES,timeout_seconds=300,stdout_budget=25165824,
           system_library_dirs=self.m.LIBDIRS,max_record_bytes=8192,ros_attempt=1,package_artifacts=[dict(name=main.name,source=str(main),sha256=self.sha(main))],
           peer=dict(A='a'*64,B='b'*64),rmw_debugger=spec,require_residual_snapshot=True,
           task_contract=dict(freeze_path=str(self.root/'DEVICE_CONTRACT.json')))
  self.m.ros_shape(cfg)
  for key,value in [('require_residual_snapshot',False),('rmw_debugger',dict(spec,seconds=261))]:
   with self.subTest(key=key):self.assertRaises(ValueError,self.m.ros_shape,dict(cfg,**{key:value}))

 def continuation_fixture(self):
  """Fresh UNIT receipts only; never mutate executed integration fixtures."""
  authority=self.root;previous=authority/'iterations/iteration-1';current=authority/'iterations/iteration-2'
  def put(p,value):
   p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(value),encoding='utf-8')
  for execution in (previous,current):
   (execution/'executor').mkdir(parents=True)
   shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py',execution/'executor/task_contract.py')
   put(execution/'PRIVATE_CONFIG.json',dict(self.private,task_root=str(execution),repo_root=str(authority)))
  selected=authority/'unit_selected.py';selected.write_text('before\n');before=self.sha(selected)
  original=dict(self.f,task_root=str(previous),execution_iteration_id=1,source=[dict(path=selected.name,sha256=before)])
  put(previous/'DEVICE_CONTRACT.json',original)
  selected.write_text('after\n');after=self.sha(selected)
  frozen=dict(self.f,task_root=str(current),execution_iteration_id=2,source=[dict(path=selected.name,sha256=after)])
  put(current/'DEVICE_CONTRACT.json',frozen)
  spec=importlib.util.spec_from_file_location('fresh_continuation_contract',current/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  m.ROS_TASK_GRANT_PATH=self.anchor;m.ROS_TASK_GRANT_SHA256=self.sha(self.anchor)
  window=previous/'windows/data-unit';window.mkdir(parents=True)
  cfg=dict(run='1234567890abcdef',ros_attempt=1,targets=self.private['targets'],ros_task_id=self.grant['task_id'],window_root=str(window),
   task_contract=dict(task_root=str(previous),freeze_path=str(previous/'DEVICE_CONTRACT.json'),freeze_sha256=self.sha(previous/'DEVICE_CONTRACT.json')))
  put(window/'window.json',cfg);(window/'EXECUTION_STARTED').write_text('UNIT_EXECUTION_NO_DEVICE')
  put(window/'window-summary.json',dict(first_failure='UNIT_OBSERVED_FAILURE',cleanup_failures=[]))
  terminal=dict(run=cfg['run'],timedOut=False,collectionCleanup='HDC_STREAMS_CLOSED',activeCollectors=[],A=dict(closed=True,exited=True),B=dict(closed=True,exited=True))
  terminal['pass']=False;put(window/'result.json',terminal)
  for side in ('A','B'):
   folder=window/side;folder.mkdir();(folder/'baseline.json').write_text('[]');(folder/'after.json').write_text('[]')
   for phase in ('before','after'):(folder/('metadata-'+phase)).mkdir()
   put(folder/'final-guard.json',dict(Passed=True,byte_match=True,metadata_match=True,process_absent=True,service_permission='DENIED',
    metadata_before=dict(evidence=str(folder/'metadata-before')),metadata_after=dict(evidence=str(folder/'metadata-after'))))
   for role in ('ownership-guard.json','final-snapshot-guard.json'):put(folder/role,dict(Passed=True))
  row=dict(scope=m.ROS_SCOPE,attempt=1,run=cfg['run'],window=str(window),task_id=self.grant['task_id'],kind='DATA_DISPATCH_INTENT',side='A',simulated=False,
   authorization=AUTH,authorization_record_sha256=self.sha(self.anchor))
  claim=authority/'ros2/task-ledger/data-1.json';put(claim,row);put(window/'DATA_ATTEMPT_STARTED',row)
  outcome=m.ros_task_data_outcome(window,cfg)
  binding=dict(previous_claim_sha256=self.sha(claim),previous_window_sha256=self.sha(window/'window.json'),previous_summary_sha256=self.sha(window/'window-summary.json'),
   previous_terminal_evidence_sha256=outcome['evidence'],before_contract_sha256=self.sha(previous/'DEVICE_CONTRACT.json'),after_contract_sha256=self.sha(current/'DEVICE_CONTRACT.json'))
  proof=dict(task_id=self.grant['task_id'],previous_attempt=1,previous_window=str(window),previous_claim_sha256=self.sha(claim),
   decision='RESUME_WITH_NEW_EVIDENCE_OR_VALIDATED_FIX',owner_payload_receiver_safe=True,verified_change=True,new_direct_evidence=False,
   source_or_configuration_delta='UNIT selected source actual before/after bytes',binding=binding,
   delta_files=[dict(path=selected.name,before_sha256=before,after_sha256=after)])
  for role in ('analysis','validation','review'):
   record=authority/'unit-evidence'/(role+'.json');value=dict(task_id=self.grant['task_id'],continuation_binding=binding)
   if role=='analysis':value.update(first_failure=outcome['first_failure'],root_cause_or_new_evidence='UNIT verified source change')
   elif role=='validation':value.update(result='PASS')
   else:value.update(verdict='PASS',blocking=False)
   put(record,value);proof[role]=dict(path=str(record),sha256=self.sha(record))
  continuation=authority/'ros2/task-ledger/continuation-1.json';put(continuation,proof)
  return m,window,cfg,proof,continuation,put

 def test_actual_continuation_with_bound_source_and_review(self):
  m,w,c,p,receipt,put=self.continuation_fixture();self.assertEqual(m.ros_next_attempt(),2)
  self.assertEqual(m.ros_claim_path('query',2),self.root/'ros2/task-ledger/query-2.json')

 def test_exact_append_only_successor_for_unexecuted_source_revision(self):
  m,w,c,p,base,put=self.continuation_fixture();base_bytes=base.read_bytes()
  frozen=m.TASK_ROOT/'DEVICE_CONTRACT.json';value=json.loads(frozen.read_text());value['source_revision']='UNIT_REVISION_2';put(frozen,value)
  self.assertRaisesRegex(ValueError,'append-only continuation successor',m.ros_next_attempt)
  successor=base.with_name('continuation-1-for-'+self.sha(frozen)+'.json')
  current=copy.deepcopy(p);current['binding']['after_contract_sha256']=self.sha(frozen);current['base_continuation']=dict(path=str(base),sha256=self.sha(base))
  for role in ('analysis','validation','review'):
   path=self.root/'unit-evidence'/('successor-'+role+'.json');record=json.loads(Path(p[role]['path']).read_text());record['continuation_binding']=current['binding'];put(path,record);current[role]=dict(path=str(path),sha256=self.sha(path))
  put(successor,current);self.assertEqual(m.ros_next_attempt(),2);self.assertEqual(base.read_bytes(),base_bytes)
  for change in ('base','contract','unsafe'):
   bad=copy.deepcopy(current)
   if change=='base':bad['base_continuation']['sha256']='0'*64
   elif change=='contract':bad['binding']['after_contract_sha256']='0'*64
   else:bad['owner_payload_receiver_safe']=False
   put(successor,bad);self.assertRaises(ValueError,m.ros_next_attempt)
  put(successor,current);self.assertEqual(m.ros_next_attempt(),2)

 def test_materialization_generation_does_not_create_attempts(self):
  from unittest.mock import patch
  execution=self.root/'iterations/iteration-5';(execution/'executor').mkdir(parents=True)
  shutil.copyfile(REPO/'scripts/public_socket/shared_reattach/executor/task_contract.py',execution/'executor/task_contract.py')
  (execution/'PRIVATE_CONFIG.json').write_text(json.dumps(dict(self.private,repo_root=str(self.root))))
  frozen=dict(self.f,task_root=str(execution),execution_iteration_id=5)
  (execution/'DEVICE_CONTRACT.json').write_text(json.dumps(frozen))
  spec=importlib.util.spec_from_file_location('unit_generation5',execution/'executor/task_contract.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
  m.ROS_TASK_GRANT_PATH=self.anchor;m.ROS_TASK_GRANT_SHA256=self.sha(self.anchor)
  cfg=dict(ros_attempt=4,window_root=str(execution/'windows/unit'),task_contract=dict(freeze_path=str(execution/'DEVICE_CONTRACT.json')),
           ros_task_id=self.grant['task_id'],ros_authorization_mode=AUTH,task_authority_root=str(self.root))
  with patch.object(m,'ros_next_attempt',return_value=4):
   m.verify_ros_budget(cfg,query=True)
   self.assertRaises(ValueError,m.verify_ros_budget,dict(cfg,ros_attempt=5),query=True)
   self.assertRaises(ValueError,m.verify_ros_budget,dict(cfg,ros_task_id='FOREIGN'),query=True)

 def test_continuation_rejects_no_delta_wrong_hash_or_unrelated_review(self):
  m,w,c,p,receipt,put=self.continuation_fixture()
  for change in ('no_delta','wrong_source','wrong_binding','unrelated_review','unsafe'):
   with self.subTest(change=change):
    bad=copy.deepcopy(p)
    if change=='no_delta':bad['delta_files']=[]
    elif change=='wrong_source':bad['delta_files'][0]['after_sha256']='0'*64
    elif change=='wrong_binding':bad['binding']['before_contract_sha256']='0'*64
    elif change=='unsafe':bad['owner_payload_receiver_safe']=False
    else:
     record=self.root/'unit-evidence/unrelated-review.json';put(record,dict(verdict='PASS',blocking=False))
     bad['review']=dict(path=str(record),sha256=self.sha(record))
    put(receipt,bad);self.assertRaises(ValueError,m.ros_next_attempt)
  put(receipt,p);self.assertEqual(m.ros_next_attempt(),2)

 def test_pending_child_root_blocks_valid_continuation(self):
  m,w,c,p,receipt,put=self.continuation_fixture()
  pending=m.TASK_ROOT/'windows/pending-query'
  put(self.root/'ros2/task-ledger/query-2.json',dict(scope=m.ROS_SCOPE,attempt=2,task_id=self.grant['task_id'],window=str(pending),run='fedcba0987654321'))
  self.assertRaises(ValueError,m.ros_next_attempt)

 def test_terminal_missing_and_mutation_not_restored_still_block(self):
  m,w,c,p,receipt,put=self.continuation_fixture();original=(w/'result.json').read_bytes();(w/'result.json').unlink()
  self.assertRaises(ValueError,m.ros_next_attempt);(w/'result.json').write_bytes(original)
  (w/'A/after.json').write_text('[{"unit_mutation":true}]');self.assertRaises(ValueError,m.ros_next_attempt)

 def test_complete_success_stops_and_late_analysis_failure_is_not_success(self):
  m,w,c,p,receipt,put=self.continuation_fixture()
  put(w/'window-summary.json',dict(first_failure=None,cleanup_failures=[]));terminal=json.loads((w/'result.json').read_text());terminal['pass']=True;put(w/'result.json',terminal)
  put(w/'load-collection-summary.json',dict(first_failure=None,failures=[]))
  for name in ('ros-analysis','ros-native-analysis'):
   put(w/(name+'.json'),dict(run=c['run'],**{'pass':True},errors=[]));(w/(name+'.rc')).write_text('0')
  self.assertEqual(m.ros_task_data_outcome(w,c)['status'],'PASS');self.assertRaisesRegex(ValueError,'Successful task data window',m.ros_next_attempt)
  put(w/'ros-analysis.json',dict(run=c['run'],**{'pass':False},errors=['UNIT_MISSING_TAKE']));(w/'ros-analysis.rc').write_text('1')
  self.assertEqual(m.ros_task_data_outcome(w,c)['first_failure'],'ROS_ANALYSIS_FAILED')
  self.assertRaisesRegex(ValueError,'binding mismatch',m.ros_next_attempt) # Old success/failure proof cannot authorize this new outcome.

if __name__=='__main__':unittest.main()
