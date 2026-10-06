'use strict';
// One explicit ROS scene in the existing dispatcher, not a transport or supervisor.
const STAGES=['LISTENER_READY','RELIABLE_READY','RELIABLE_DONE','BEST_EFFORT_READY','BEST_EFFORT_DONE','TEARDOWN_READY','DDS_DELETED'];
const stagesFor=c=>c.route_lifecycle_native?['LISTENER_READY','PRE_READY','PRE_DONE','OFFLINE_READY','OFFLINE_DONE','REACCEPT_DONE','POST_DONE','TEARDOWN_READY','DDS_DELETED']:c.bounded_native_reaccept?require('./bounded-native-scene.cjs').STAGES:STAGES;
const isRos=c=>c.scenario==='ROS2_DSOFTBUS';
function kv(line){
 const fields={};
 for(const token of line.split(' ').slice(1)){
  const match=/^([a-z_]+)=(\S+)$/.exec(token);
  if(!match||Object.hasOwn(fields,match[1]))throw Error('ROS_RECEIPT_MALFORMED');
  fields[match[1]]=match[2];
 }
 return fields;
}
function commands(c,tokens,libdirs){
 if(c.main_member!=='ordinary_ros2_transport_consumer'||c.main_mode!=='dual'||c.main_prefix!=='ROS_MAIN')throw Error('ROS_APPLICATION_CONTRACT');
 const result={};
 for(const side of ['A','B']){
  const root=c.remote[side];
  if(root!==`/data/local/tmp/mdds_dds_${c.run}_${side}`||c.token[side]!==tokens[side]||!/^[1-9][0-9]*$/.test(tokens[side]))throw Error('ROS_COMMAND_IDENTITY');
  result[side]=`cd ${root} && env -i ${c.bounded_native_reaccept?`FDDST_BOUNDED_NATIVE_SCENE=${c.route_lifecycle_native?'ROUTE_RETURN_ONCE':'BOUNDED_NORMAL_ONCE'} `:""}PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=${root}:${libdirs.join(':')} SKIP_DEFAULT_XML=1 ROS_LOG_DIR=${root}/ros-log RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=201 ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET FASTDDS_DSOFTBUS_REQUIRED_CONFIG=${root}/ros.conf FASTDDS_DSOFTBUS_NATIVE_TOKEN=${tokens[side]} FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=${root}/control timeout ${c.timeout_seconds} ${root}/${c.main_member} ${side} ${c.run} dual; rc=$?; echo REMOTE_EXIT=$rc; exit $rc`;
 }
 return result;
}
function controller(c,release,admission){
 const STAGES=stagesFor(c),bounded=!!c.bounded_native_reaccept;
 // One actual command at a time, including a peer whose main arrives during
 // failure cleanup. Each caller still receives its own original rejection.
 let dispatchChain=Promise.resolve();
 const serialRelease=action=>{
  const current=dispatchChain.then(action);
  dispatchChain=current.catch(()=>{});return current;
 };
 const progress={A:0,B:0},pids={},listeners={},pub={},write={},take={},graph={},done={},sent={},handled={},response={},deleted={},closed={};
 let first=null,chain=Promise.resolve(),cleanupChain=Promise.resolve(),runtimeCloseChain=Promise.resolve();const released=[],issued=[];
 const runtime_close_ready={},runtime_close_controls={},runtime_close_failures=[];
 function queueRuntimeClose(){
  if(!c.paired_runtime_close)return;
  const normal=!first&&['A','B'].every(s=>runtime_close_ready[s]&&cleanup_controls[s]?.state==='COMPLETED'&&cleanup_controls[s].reason==='NORMAL_COMPLETION');
  for(const side of ['A','B'])if(runtime_close_ready[side]&&!ended[side]&&!runtime_close_controls[side]&&
      (normal||(first&&cleanup_controls[side]?.state==='COMPLETED'))){
   const request={side,pid:pids[side],stage:'RUNTIME_CLOSE',reason:normal?'NORMAL_COMPLETION':'WINDOW_FAILED',state:'QUEUED'};
   runtime_close_controls[side]=request;
   const precedingCleanup=cleanupChain;
   runtimeCloseChain=runtimeCloseChain.then(async()=>{
    await precedingCleanup;
    try{await serialRelease(()=>{
     if(first)request.reason='WINDOW_FAILED';request.state='ISSUED';
     return release(side,'RUNTIME_CLOSE',{pid:request.pid,reason:request.reason});
    });request.state='COMPLETED';}
    catch(e){request.state='FAILED';runtime_close_failures.push({side,reason:String(e.message||e)});fail('ROS_RUNTIME_CLOSE_CONTROL_FAILED_'+side);}
   });
  }
 }
 const cleanup_controls={},cleanup_control_failures=[],teardown_events={A:[],B:[]},ended={};
 const primary_failure={},cleanup_failure={},capture_failure=[],application_exit={},terminal_sequences={A:new Set(),B:new Set()};
 const residual_summaries={A:[],B:[]},residual_objects={A:[],B:[]};
 function queueCleanup(reason){
  for(const side of ['A','B'])if(pids[side]&&!ended[side]&&!cleanup_controls[side]){
   const request={side,pid:pids[side],reason,stage:'CLEANUP',state:'QUEUED'};cleanup_controls[side]=request;
   const priorBusiness=chain;
   cleanupChain=cleanupChain.then(async()=>{await priorBusiness.catch(()=>{});
    // A failure arriving during normal cleanup changes only an unissued peer
    // cleanup. Never rewrite a dispatched command or release new business.
    try{await serialRelease(()=>{
     if(first)request.reason='WINDOW_FAILED';request.state='ISSUED';
     return release(side,'CLEANUP',{pid:request.pid,reason:request.reason});
    });request.state='COMPLETED';queueRuntimeClose();}
    catch(e){request.state='FAILED';cleanup_control_failures.push({side,pid:request.pid,reason:String(e.message||e)});first ||= 'ROS_CLEANUP_CONTROL_FAILED_'+side;}
   });
  }
 }
 let route_restore_failure=null;
 const fail=why=>{first ||= why;if(c.route_lifecycle_native&&native)native.restore().catch(e=>{route_restore_failure=String(e.message||e);});queueCleanup('WINDOW_FAILED');queueRuntimeClose();};
 const nativeSend=async(s,p)=>{
  const dispatched=await serialRelease(async()=>{if(first&&p!=='ETH1_UP')return false;await release(s,p);return true;});
  if(dispatched&&p==='REACCEPT_DONE'){issued.push(s+p);released.push(s+p);}return dispatched;
 };
 const native=bounded?require('./bounded-native-scene.cjs').create(c,{fail,send:nativeSend}):null;
 const capture=why=>{if(capture_failure.length<16)capture_failure.push(why);fail(why);};
 function terminal(side,text){
  if(text.startsWith('ROS_DIAGNOSTIC_CAPTURE ')){capture('ROS_DIAGNOSTIC_CAPTURE_INCOMPLETE');return true;}
  const joined='Failed to delete participantROS_FAILURE remote match missing';
  const legacy=text==='ROS_FAILURE remote match missing'||text===joined;
  if(legacy){
   primary_failure[side] ||= {reason:'REMOTE_MATCH_MISSING',stage:'RELIABLE_MATCH',identity_origin:'RUNNER_CONTEXT_NOT_EMITTED',context_run:c.run,context_side:side,context_pid:pids[side]||'UNKNOWN',process_order:'UNKNOWN'};
   if(text===joined){cleanup_failure[side] ||= [];if(cleanup_failure[side].length<8)cleanup_failure[side].push({reason:'PARTICIPANT_DELETE_FAILED',api_return:'NOT_OBSERVED',identity_origin:'RUNNER_CONTEXT_NOT_EMITTED',process_order:'UNKNOWN'});}
   fail('ROS_APPLICATION_FAILURE_'+side);return true;
  }
  if(/^ROS_BUSINESS_EXCEPTION failed to configure logging: .+ native_cleanup=NOT_PROVEN$/.test(text)){
   primary_failure[side] ||= {reason:'RCLCPP_INIT_EXCEPTION',stage:'RCLCPP_INIT',identity_origin:'RUNNER_CONTEXT_NOT_EMITTED',context_run:c.run,context_side:side,process_order:'UNKNOWN'};fail('ROS_APPLICATION_FAILURE_'+side);return true;
  }
  if(text==='Failed to delete participant'||text==='FDDST_FACTORY_DESTRUCTOR_INCOMPLETE_NO_NORMAL_CLOSE'){
   cleanup_failure[side] ||= [];if(cleanup_failure[side].length<8)cleanup_failure[side].push({reason:text==='Failed to delete participant'?'PARTICIPANT_DELETE_FAILED':'FACTORY_NORMAL_CLOSE_INCOMPLETE',api_return:'NOT_OBSERVED',identity_origin:'RUNNER_CONTEXT_NOT_EMITTED',process_order:'UNKNOWN'});fail('ROS_CLEANUP_FAILURE_'+side);return true;
  }
  if(!text.startsWith('ROS_FAILURE ')&&!text.startsWith('ROS_CLEANUP_FAILURE '))return false;
  let r;try{r=kv(text);}catch{capture('ROS_FAILURE_RECEIPT_MALFORMED');return true;}
  if(r.schema===undefined&&Object.keys(r).sort().join(',')==='reason,run,side'&&r.reason==='APPLICATION_FAILURE'&&r.run===c.run&&r.side===side){primary_failure[side] ||= {...r,identity_origin:'LEGACY_RUNNER_PID_CONTEXT',context_pid:pids[side]||'UNKNOWN'};fail('ROS_APPLICATION_FAILURE_'+side);return true;}
  if(r.schema!=='1'||r.run!==c.run||r.side!==side||!/^([1-9][0-9]*)$/.test(r.pid||'')||
     (pids[side]&&r.pid!==pids[side])||!/^([1-9][0-9]{0,19})$/.test(r.seq||'')||
     !/^[A-Z][A-Z0-9_]{0,63}$/.test(r.stage||'')||!/^[A-Z][A-Z0-9_]{0,63}$/.test(r.reason||'')||
     !/^(0|[1-9][0-9]{0,19})$/.test(r.failure_ref||'')||
     Object.keys(r).some(k=>!['schema','run','side','pid','seq','stage','reason','failure_ref','detail','truncated'].includes(k))||
     (r.detail!==undefined&&!/^(?:[0-9a-f]{2}){0,192}$/.test(r.detail))||
     (r.truncated!==undefined&&!['0','1'].includes(r.truncated))){capture('ROS_FAILURE_RECEIPT_IDENTITY');return true;}
  if(terminal_sequences[side].has(r.seq)){capture('ROS_FAILURE_RECEIPT_DUPLICATE');return true;}
  if(terminal_sequences[side].size>=16){capture('ROS_FAILURE_RECEIPT_OVERFLOW');return true;}
  terminal_sequences[side].add(r.seq);
  if(text.startsWith('ROS_FAILURE ')){
   if(r.failure_ref!=='0'||primary_failure[side]){capture('ROS_PRIMARY_FAILURE_CONTRADICTORY');return true;}
   primary_failure[side]={...r,identity_origin:'APPLICATION_SCHEMA_1'};fail('ROS_APPLICATION_FAILURE_'+side);
  }else{
   if(r.failure_ref!=='0'&&BigInt(r.failure_ref)>=BigInt(r.seq)){capture('ROS_CLEANUP_FAILURE_REFERENCE');return true;}
   cleanup_failure[side] ||= [];if(cleanup_failure[side].length<8)cleanup_failure[side].push({...r,identity_origin:'APPLICATION_SCHEMA_1'});else capture('ROS_CLEANUP_FAILURE_OVERFLOW');
   fail('ROS_CLEANUP_FAILURE_'+side);
  }
  return true;
 }
 const key=(s,p)=>s+':'+p;
 function phaseComplete(phase){
  for(const side of ['A','B']){
   const other=side==='A'?'B':'A',k=key(side,phase),peer=pub[key(other,phase)];
   if(!done[k]||!pub[k]||!peer||pub[k].gid===peer.gid||!graph[k])return false;
   for(let ordinal=1;ordinal<=5;ordinal++){
    const w=write[k]?.[ordinal],t=take[k]?.[ordinal];
    if(!w||!t||t.gid!==peer.gid||w.payload!==`${c.run}:${phase}:${side}:${ordinal}`||t.payload!==`${c.run}:${phase}:${other}:${ordinal}`)return false;
   }
  }
  return sent[phase]&&handled[phase]&&response[phase]&&sent[phase].request_id===handled[phase].request_id&&sent[phase].request_id===response[phase].request_id&&handled[phase].sum==='7'&&response[phase].sum==='7';
 }
 function line(side,text){
  if(!['A','B'].includes(side)){capture('ROS_UNKNOWN_SOURCE_SIDE');return;}
  const deletionPrefix='Failed to delete participant';
  if(text.startsWith(deletionPrefix+'ROS_TEARDOWN_')||text.startsWith(deletionPrefix+'ROS_RESIDUAL_')){
   const suffix=text.slice(deletionPrefix.length);
   if(!/^ROS_(?:TEARDOWN_EVENT|TEARDOWN_CAPTURE|RESIDUAL_SUMMARY|RESIDUAL_OBJECT) /.test(suffix)){capture('ROS_JOINED_DIAGNOSTIC_SYNTAX');return;}
   line(side,suffix);terminal(side,deletionPrefix);return;
  }
  if(text.startsWith(deletionPrefix+'ROS_CONTEXT_CLOSED ')){
   const suffix=text.slice(deletionPrefix.length);
   const joined=/^ROS_CONTEXT_CLOSED run=([0-9a-f]{16}) side=([AB]) result=(0|[1-9][0-9]{0,9}) result_scope=APPLICATION_EXIT_STATUS shutdown_return=([01])$/.exec(suffix);
   if(!joined||joined[1]!==c.run||joined[2]!==side||!pids[side]){capture('ROS_JOINED_CONTEXT_CLOSE_IDENTITY');return;}
   terminal(side,deletionPrefix); // Library prefix has runner context, not an API rc.
   line(side,suffix);return;
  }
  if(terminal(side,text))return;
  if(text.startsWith('ROS_STARTUP_CAPTURE ')){
   let record;try{record=kv(text);}catch{capture('ROS_STARTUP_CAPTURE_MALFORMED');return;}
   if(record.completeness==='INCOMPLETE')capture('ROS_STARTUP_CAPTURE_INCOMPLETE');
   return;
  }
  if(!text.startsWith('ROS_')&&!text.startsWith('FDDST_ROS_CLOSED')&&!text.startsWith('NATIVE_'))return;
  let r;try{r=kv(text);}catch(e){fail(e.message);return;}
  if(native&&(text.startsWith('NATIVE_')||text.startsWith('ROS_REACCEPT_ANCHOR ')||(c.route_lifecycle_native&&text.startsWith('ROS_STARTUP_EVENT ')))){chain=chain.then(async()=>{await native.line(side,text.split(' ')[0],r);if(c.route_lifecycle_native&&text.startsWith('NATIVE_SENDER_SUBMISSION '))await native.finishOffline();}).catch(e=>fail('NATIVE_CONTROL_FAILED:'+e.message));return;}
  if(text.startsWith('ROS_FAILURE ')||text.startsWith('ROS_BUSINESS_EXCEPTION ')){fail('ROS_APPLICATION_FAILURE_'+side);return;}
  if(text.startsWith('ROS_MAIN ')){
   if(r.run!==c.run||r.side!==side||r.scenario!=='ROS2_DSOFTBUS'||r.mode!=='dual'||pids[side]||!/^[1-9][0-9]*$/.test(r.pid||'')){fail('ROS_MAIN_IDENTITY');return;}
   pids[side]=r.pid;if(first)queueCleanup('WINDOW_FAILED');return;
  }
  if(text.startsWith('ROS_RUNTIME_CLOSE_READY ')){
   if(!c.paired_runtime_close||r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.stage!=='RUNTIME_CLOSE'||
      r.stopping!=='1'||r.sealed!=='1'||r.active_pumps!=='0'||r.pump_joined!=='1'||runtime_close_ready[side]||
      !['ISSUED','COMPLETED'].includes(cleanup_controls[side]?.state)||
      Object.keys(r).sort().join(',')!=='active_pumps,pid,pump_joined,run,schema,sealed,side,stage,stopping'){
    capture('ROS_RUNTIME_CLOSE_READY_IDENTITY');return;
   }
   runtime_close_ready[side]=r;queueRuntimeClose();return;
  }
  if(text.startsWith('ROS_RUNTIME_CLOSE_FAILURE ')){
   if(!c.paired_runtime_close||r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.stage!=='RUNTIME_CLOSE'||
      !['CONTROL_MISSING_OR_INVALID','WINDOW_FAILED','CONTROL_CAPTURE_EXCEPTION'].includes(r.reason)||
      Object.keys(r).sort().join(',')!=='pid,reason,run,schema,side,stage'){capture('ROS_RUNTIME_CLOSE_FAILURE_IDENTITY');return;}
   runtime_close_failures.push({...r});fail('ROS_RUNTIME_CLOSE_FAILURE_'+side);return;
  }
  if(text.startsWith('ROS_TEARDOWN_EVENT ')){
   if(r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side]||
       !/^[1-9][0-9]{0,19}$/.test(r.seq||'')||!/^[1-9][0-9]{0,19}$/.test(r.call||'')||
       !/^[A-Za-z_]{1,47}$/.test(r.operation||'')||!/^[A-Z_]{1,47}$/.test(r.branch||'')||
       !/^[0-9]{1,20}$/.test(r.actor||'')||!/^[0-9]{1,20}$/.test(r.object||'')||r.entered!=='1'||!['0','1'].includes(r.returned)||
       !['DDS_RETURN_CODE','PUBLIC_RETURN_CODE','INTEGRATION_CLOSE_RESULT'].includes(r.value_type)||
       (r.returned==='1'?!/^-?[0-9]{1,20}$/.test(r.value||''):r.value!=='NOT_OBSERVED')){capture('ROS_TEARDOWN_RECEIPT_IDENTITY');return;}
   if(teardown_events[side].length>=256){capture('ROS_TEARDOWN_RECEIPT_OVERFLOW');return;}
   if(teardown_events[side].some(e=>e.seq===r.seq)){capture('ROS_TEARDOWN_RECEIPT_DUPLICATE');return;}
   teardown_events[side].push(r);
   if(r.returned==='1'&&r.value!=='0'){cleanup_failure[side] ||= [];if(cleanup_failure[side].length<8)cleanup_failure[side].push({...r,reason:'DELETE_OR_CLOSE_NON_OK',identity_origin:'FACTORY_VALUE_OBSERVATION'});fail('ROS_TEARDOWN_FAILURE_'+side);}
   return;
  }
  if(text.startsWith('ROS_RESIDUAL_SUMMARY ')||text.startsWith('ROS_RESIDUAL_OBJECT ')||text.startsWith('ROS_RESIDUAL_CAPTURE ')){
   if(r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side]){capture('ROS_RESIDUAL_IDENTITY');return;}
   if(text.startsWith('ROS_RESIDUAL_CAPTURE ')){capture('ROS_RESIDUAL_CAPTURE_INCOMPLETE');return;}
   if(!/^[1-9][0-9]{0,19}$/.test(r.snapshot||'')){capture('ROS_RESIDUAL_SNAPSHOT_ID');return;}
   if(text.startsWith('ROS_RESIDUAL_SUMMARY ')){
    if(residual_summaries[side].length>=8||residual_summaries[side].some(x=>x.snapshot===r.snapshot)||
       r.sampling!=='SEQUENTIAL_NONATOMIC'||!/^[0-9]+$/.test(r.participant||'')||
       !['pubs','subs','topics','object_stored','object_total'].every(k=>/^[0-9]+$/.test(r[k]||''))||Number(r.object_stored)>32||r.object_limit!=='32'){
     capture('ROS_RESIDUAL_SUMMARY_INVALID');return;
    }
    residual_summaries[side].push(r);
    if(r.completeness!=='CAPTURED'||r.truncated!=='0')capture('ROS_RESIDUAL_CAPTURE_INCOMPLETE');
   }else{
    const summary=residual_summaries[side].find(x=>x.snapshot===r.snapshot);
    if(!summary||residual_objects[side].length>=256||!['0','1','2'].includes(r.kind)||!/^([1-9][0-9]*)$/.test(r.ordinal||'')||Number(r.ordinal)>Number(summary.object_stored)||r.sample_order!==String(Number(r.kind)+1)||
       !['0','1','NOT_OBSERVED'].includes(r.referenced)||!['0','1'].includes(r.name_known)||!['0','1'].includes(r.type_known)||!['0','1'].includes(r.text_truncated)||
       !/^(?:-|(?:[0-9a-f]{2}){1,127})$/.test(r.name_hex||'')||!/^(?:-|(?:[0-9a-f]{2}){1,127})$/.test(r.type_hex||'')||
       residual_objects[side].some(x=>x.snapshot===r.snapshot&&x.ordinal===r.ordinal)||!/^[0-9]+$/.test(r.object||'')){
     capture('ROS_RESIDUAL_OBJECT_INVALID');return;
    }
    residual_objects[side].push(r);
   }
   return;
  }
  if(text.startsWith('ROS_TEARDOWN_CAPTURE ')){if(r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side])capture('ROS_TEARDOWN_CAPTURE_IDENTITY');else if(r.completeness!=='CAPTURED')capture('ROS_TEARDOWN_CAPTURE_INCOMPLETE');return;}
  if(text.startsWith('ROS_TEARDOWN_CAPTURE_FAILURE ')){capture('ROS_TEARDOWN_CAPTURE_FAILED');return;}
  if(text.startsWith('ROS_CLEANUP_ENTERED ')){
   if(r.schema!=='1'||r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.stage!=='CLEANUP'||
       !cleanup_controls[side]||r.reason!==cleanup_controls[side].reason){capture('ROS_CLEANUP_RECEIPT_IDENTITY');return;}
   cleanup_controls[side].application_entered=true;return;
  }
  const event=text.split(' ')[0],phase=r.phase,k=key(side,phase);
  if(event==='ROS_LISTENER_READY'){
   if(r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.fake_sdk!=='0'||!/^\d+$/.test(r.listener||'')||Number(r.listener)>2147483647||listeners[side])fail('ROS_LISTENER_IDENTITY');
   else listeners[side]=r;
   return;
  }
  if(['ROS_PUBLISHER','ROS_WRITE','ROS_TAKE','ROS_GRAPH','ROS_BUSINESS_DONE','ROS_SERVICE_SENT','ROS_SERVICE_REQUEST','ROS_SERVICE_RESPONSE'].includes(event)){
   if(r.run!==c.run||!(bounded&&event.startsWith('ROS_SERVICE_')?['PRE','POST']:['reliable','best_effort']).includes(phase)||(!event.startsWith('ROS_SERVICE_')&&r.side!==side)){fail('ROS_RECEIPT_IDENTITY');return;}
   if(event==='ROS_PUBLISHER'){if(pub[k]||r.intra_process!=='0'||!/^([0-9a-f]{32,64})$/.test(r.gid||''))fail('ROS_PUBLISHER');else pub[k]=r;}
   if(event==='ROS_WRITE'||event==='ROS_TAKE'){
    if(native){native.line(side,event,r);if(c.route_lifecycle_native&&Number(r.ordinal)===1001){if(r.segment!=='OFFLINE'){fail('OFFLINE_SAMPLE_SEGMENT');return;}const target=event==='ROS_WRITE'?write:take;target[k] ||= {};if(target[k][1001]){fail('OFFLINE_SAMPLE_DUPLICATE');return;}target[k][1001]=r;return;}if(r.segment!==(Number(r.ordinal)<=2?'PRE':'POST')){fail('NATIVE_SAMPLE_SEGMENT');return;}}
    const map=event==='ROS_WRITE'?write:take,ordinal=Number(r.ordinal);
    if(!Number.isInteger(ordinal)||ordinal<1||ordinal>5||map[k]?.[ordinal])fail('ROS_SAMPLE_DUPLICATE_OR_RANGE');
    else{map[k] ||= {};map[k][ordinal]=r;}
   }
   if(event==='ROS_GRAPH'){const g=bounded?k+':'+r.segment:k;if(graph[g]||r.peer!==`/fdds_${c.run}/transport_${side==='A'?'B':'A'}`||r.visible!=='1'||r.subscription_count!=='1')fail('ROS_GRAPH');else graph[g]=r;}
   if(event==='ROS_BUSINESS_DONE'){if(done[k]||r.writes!=='5'||r.takes!=='5')fail('ROS_PHASE_DONE');else done[k]=r;}
   const services={'ROS_SERVICE_SENT':[sent,'A'],'ROS_SERVICE_REQUEST':[handled,'B'],'ROS_SERVICE_RESPONSE':[response,'A']};
   if(services[event]){
    const [map,expected]=services[event];
    if(side!==expected||map[phase])fail('ROS_SERVICE_SIDE_OR_DUPLICATE');
    else if(!/^[0-9]+$/.test(r.request_id||'')||
       (event==='ROS_SERVICE_SENT'&&(r.a!==(phase==='POST'?'5':'3')||r.b!==(phase==='POST'?'6':'4')))||
       (event==='ROS_SERVICE_REQUEST'&&(!/^[0-9a-f]{32}$/.test(r.writer||'')||r.a!==(phase==='POST'?'5':'3')||r.b!==(phase==='POST'?'6':'4')||r.sum!==(phase==='POST'?'11':'7')))||
       (event==='ROS_SERVICE_RESPONSE'&&r.sum!==(phase==='POST'?'11':'7')))fail('ROS_SERVICE_IDENTITY');
    else map[phase]=r;
   }
   return;
  }
  if(event==='ROS_DDS_DELETED'){if(r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.dds_component_result!=='0'||r.api_return!=='NOT_OBSERVED'||r.scope!=='FACTORY_POST_DELETE'||deleted[side])fail('ROS_PUBLIC_DELETE_COMPONENT');else deleted[side]=r;return;}
  if(event==='ROS_CONTEXT_CLOSED'){
   if(r.run!==c.run||r.side!==side||closed[side]){capture('ROS_CONTEXT_CLOSE_IDENTITY');return;}
   application_exit[side]=r.result;
   if(r.result_scope!==undefined){
    if(r.result_scope!=='APPLICATION_EXIT_STATUS'||!['0','1'].includes(r.shutdown_return)){capture('ROS_SHUTDOWN_RECEIPT_INVALID');return;}
    if(r.shutdown_return==='1')closed[side]=r;
    else {cleanup_failure[side] ||= [];if(cleanup_failure[side].length<8)cleanup_failure[side].push({reason:'SHUTDOWN_RETURNED_FALSE',value:'0',result_type:'RCLCPP_SHUTDOWN_BOOL'});fail('ROS_CLEANUP_FAILURE_'+side);}
   }else if(r.result==='0')closed[side]=r;
   if(r.result!=='0')fail('ROS_APPLICATION_EXIT_NONZERO_'+side);
   return;
  }
  if(event!=='ROS_STAGE')return;
  if(r.run!==c.run||r.side!==side||r.pid!==pids[side]||r.stage!==STAGES[progress[side]]){fail('ROS_STAGE_IDENTITY_OR_ORDER');return;}
  if(first)return;
  if(bounded){
   if(r.stage==='PRE_READY'&&!['reliable','best_effort'].every(q=>pub[key(side,q)]&&graph[key(side,q)+':PRE'])){fail('NATIVE_BEFORE_GRAPH_OR_PUBLISHER');return;}
  }
  if(r.stage==='LISTENER_READY'&&!listeners[side]){fail('ROS_STAGE_BEFORE_LISTENER');return;}
  if(r.stage==='RELIABLE_READY'||r.stage==='BEST_EFFORT_READY'){
   const ready=key(side,r.stage==='RELIABLE_READY'?'reliable':'best_effort');
   if(!pub[ready]||!graph[ready]){fail('ROS_STAGE_BEFORE_GRAPH_OR_PUBLISHER');return;}
  }
  const index=progress[side]++;
   if(index===STAGES.length-1){if(!deleted[side])fail('ROS_STAGE_BEFORE_PUBLIC_DELETE');return;}
  if(r.stage==='TEARDOWN_READY'){if(progress.A>index&&progress.B>index)queueCleanup(first?'WINDOW_FAILED':'NORMAL_COMPLETION');return;}
  if(progress.A>index&&progress.B>index)chain=chain.then(async()=>{
   if(first)return;
   // The SDK connect must precede DDS/ROS discovery. Capture dynamic ROS type
   // libraries once the same main PID reaches ready, before any formal write.
   if(index===1){const load=await admission();if(!load.A?.pass||!load.B?.pass){fail('ROS_LOAD_CAPTURE_FAILED');return;}}
   if(bounded&&(r.stage==='PRE_DONE'||r.stage==='POST_DONE')){
    const segment=r.stage==='PRE_DONE'?'PRE':'POST',lo=segment==='PRE'?1:3,hi=segment==='PRE'?2:5;
    for(const s of ['A','B'])for(const q of ['reliable','best_effort'])for(let o=lo;o<=hi;o++){
     const k=key(s,q),other=s==='A'?'B':'A',w=write[k]?.[o],t=take[k]?.[o];
     if(!w||!t||w.payload!==`${c.run}:${q}:${s}:${o}`||t.payload!==`${c.run}:${q}:${other}:${o}`||t.gid!==pub[key(other,q)]?.gid||!graph[k+':'+segment]){fail('NATIVE_SEGMENT_LEDGER');return;}
    }
    if(!sent[segment]||!handled[segment]||!response[segment]||sent[segment].request_id!==handled[segment].request_id||sent[segment].request_id!==response[segment].request_id){fail('NATIVE_SERVICE_LEDGER');return;}
    if(segment==='PRE')for(const s of ['A','B'])await nativeSend(s,'NATIVE_ARM');
    else if(!native.complete()){fail('NATIVE_FINAL_IDENTITY');return;}
   }
   if(c.route_lifecycle_native&&r.stage==='OFFLINE_READY')return;
   if(c.route_lifecycle_native&&r.stage==='OFFLINE_DONE'){
    for(const s of ['A','B'])for(const q of ['reliable','best_effort'])if(!write[key(s,q)]?.[1001]){fail('OFFLINE_WRITE_MISSING');return;}
    await native.offlineDone();return;
   }
   if(bounded&&r.stage==='REACCEPT_DONE')return; // Native publication, not a generic stage, grants POST.
   if((r.stage==='RELIABLE_DONE'||r.stage==='BEST_EFFORT_DONE')&&!phaseComplete(r.stage==='RELIABLE_DONE'?'reliable':'best_effort')){fail('ROS_PHASE_LEDGER_INCOMPLETE');return;}
   for(const board of ['A','B']){
    if(first)return;
    const dispatched=await serialRelease(async()=>{if(first)return false;issued.push(board+r.stage);await release(board,r.stage);return true;});
    if(dispatched)released.push(board+r.stage);
   }
  }).catch(e=>fail('ROS_CONTROL_RELEASE_FAILED:'+e.message));
 }
 return{line,stop:capture,end(side,confirmed){ended[side]=true;if(!confirmed)capture('ROS_COLLECTOR_INCOMPLETE_'+side);else if(progress[side]!==STAGES.length)fail('ROS_COLLECTOR_INCOMPLETE_'+side);},async finish(){await chain;if(c.route_lifecycle_native){try{await native.restore();}catch(e){route_restore_failure=String(e.message||e);fail('ETH1_RESTORE_FAILED:'+e.message);}}await cleanupChain;for(const side of ['A','B']){
   if(primary_failure[side]?.identity_origin==='APPLICATION_SCHEMA_1'&&primary_failure[side].pid!==pids[side]){capture('ROS_FAILURE_WITHOUT_MAIN_IDENTITY');delete primary_failure[side];}
   for(const r of cleanup_failure[side]||[])if(r.identity_origin==='APPLICATION_SCHEMA_1'&&(r.pid!==pids[side]||(r.failure_ref!=='0'&&r.failure_ref!==primary_failure[side]?.seq)))capture('ROS_CLEANUP_FAILURE_UNBOUND');
   if(!closed[side]||!deleted[side])fail('ROS_FINAL_LIFECYCLE_INCOMPLETE_'+side);
   if(c.require_residual_snapshot&&(!residual_summaries[side].length||residual_summaries[side].some(s=>residual_objects[side].filter(o=>o.snapshot===s.snapshot).length!==Number(s.object_stored))))capture('ROS_REQUIRED_RESIDUAL_EVIDENCE_MISSING_'+side);
  }await cleanupChain;await runtimeCloseChain;
  if(c.paired_runtime_close&&!['A','B'].every(s=>runtime_close_ready[s]&&runtime_close_controls[s]?.state==='COMPLETED'&&runtime_close_controls[s].reason==='NORMAL_COMPLETION'))fail('ROS_RUNTIME_CLOSE_COORDINATION_INCOMPLETE');
  const firstSide=/_(A|B)$/.exec(first||'')?.[1];const primarySide=firstSide?(primary_failure[firstSide]?firstSide:null):['A','B'].find(s=>primary_failure[s]);return{pass:!first&&released.length===10&&Object.values(cleanup_controls).every(x=>x.state==='COMPLETED'),first_failure:primarySide?'ROS_APPLICATION_FAILURE_'+primarySide:first,reported_first_host:first,route_restore_failure,primary_failure,cleanup_failure,capture_failure,application_exit,cleanup_controls,cleanup_control_failures,runtime_close_ready,runtime_close_controls,runtime_close_failures,teardown_events,residual_summaries,residual_objects,primary_selection:'FIRST_HOST_WITH_SAME_SIDE_PRIMARY_REFERENCE_NOT_CROSS_PROCESS_CAUSALITY',cross_side_failure_order:'UNKNOWN',released,issued,pids,progress,ros:{pub,write,take,graph,done,sent,handled,response,deleted,closed}};}};
}
module.exports={STAGES,stagesFor,isRos,commands,controller};
