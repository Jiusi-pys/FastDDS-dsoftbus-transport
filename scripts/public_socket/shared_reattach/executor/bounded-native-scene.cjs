'use strict';
// Scene metadata and phase selection within the existing ROS controller.
// No HDC driver, SDK calls, identity allocation or permission logic.
const STAGES=['LISTENER_READY','PRE_READY','PRE_DONE','REACCEPT_DONE','POST_DONE','TEARDOWN_READY','DDS_DELETED'];
const ACTIONS=['NATIVE_ARM','NATIVE_END','NATIVE_REPLACE','NATIVE_OFFLINE_CAPTURE'];
function create(c,{send,fail}){
 const events={A:[],B:[]},anchors={A:{},B:{}},endpoints={A:{},B:{}},app={A:{},B:{}};
 let ending=false,replacing=false,published=false;
 const route=!!c.route_lifecycle_native,routeStates={A:{},B:{}},nodeApis={A:[],B:[]},submissions={A:[],B:[]},oldBoth={};
 let restorePromise=null,restored=false,offline_checked=false,restore_timer,offlineReleased=false;
 // All failure, timeout and finish callers await the same physical restoration.
 // A suppressed action is not a successful remote dispatch.
 const restore=()=>{
  if(!route||!ending)return Promise.resolve();
  if(restorePromise)return restorePromise;
  restorePromise=Promise.resolve().then(async()=>{
   try{if(await send('A','ETH1_UP')!==true)throw Error('ETH1_UP_NOT_DISPATCHED');restored=true;}
   finally{clearTimeout(restore_timer);}
  });
  return restorePromise;
 };
 const latest=(side,stage)=>events[side].find(x=>x.stage===stage);
 const both=stage=>['A','B'].every(s=>latest(s,stage));
 const origin=c.bounded_native_reaccept.order==='outgoing-first'?'A':'B';
 function requireIdentity(r,s){if(r.run!==c.run||r.side!==s)throw Error('NATIVE_RECEIPT_IDENTITY');}
 function validatePublication(s){
  const before=latest(s,'ARMED'),value=latest(s,'OWNER_PUBLISHED'),old=latest(s,'OLD_OWNER_SETTLED');
  if(!before||!value||!old||value.old_id!==before.old_id||value.old_owner!==before.old_owner||value.listener!==before.listener||value.listener_entry!==before.listener_entry||value.client_entry!==before.client_entry||value.new_id===value.old_id||value.new_owner===value.old_owner||Number(value.new_id)<1||!/^([1-9][0-9]*)$/.test(value.new_owner)||old.callbacks!=='0'||old.receivers!=='0'||(old.shutdown_returned!=='1'&&old.on_shutdown_seen!=='1'))throw Error('NATIVE_OWNER_PUBLICATION');
  if(value.source!=='SDK_PUBLIC_SOCKET'||value.sdk_epoch!=='NOT_PROVEN'||value.sdk_drain!=='NOT_PROVEN')throw Error('NATIVE_SOURCE_OR_OVERCLAIM');
  const a=anchors[s].BEFORE,b=anchors[s].AFTER;if(!a||!b||a.runtime!==b.runtime||a.participant!==b.participant||a.guid!==b.guid)throw Error('NATIVE_ENTITY_CHANGED');
  const e=endpoints[s];const canonical=m=>JSON.stringify(Object.entries(m).sort(([a],[b])=>a.localeCompare(b)));if(!e.BEFORE||!e.AFTER||!Object.keys(e.BEFORE).length||canonical(e.BEFORE)!==canonical(e.AFTER))throw Error('NATIVE_ENDPOINT_GUID_CHANGED');
 }
 async function advance(){
  if(!ending&&both('ARMED')){ending=true;
   if(route){restore_timer=setTimeout(()=>{restore().then(()=>fail('PLATFORM_OFFLINE_NOT_EXERCISED_OR_CHECK_TIMEOUT'),e=>fail('ETH1_RESTORE_FAILED:'+e.message));},c.route_lifecycle_native.trigger.max_offline_seconds*1000);try{if(await send('A','ETH1_DOWN')!==true)throw Error('ETH1_DOWN_NOT_DISPATCHED');}catch(e){await restore();throw e;}}
   else await send(origin,'NATIVE_END');
  }
  if(route&&['A','B'].every(s=>routeStates[s].OFFLINE)&&!offline_checked){offline_checked=true;for(const s of ['A','B'])await send(s,'OFFLINE_READY');}
  const routeReady=!route||(restored&&['A','B'].every(s=>routeStates[s].RESOLVED&&latest(s,'PEER_READY')&&oldBoth[s]));
  if(!replacing&&latest('A','OLD_OWNER_SETTLED')&&latest('B','PEER_READY')&&routeReady){replacing=true;for(const side of route?['A','B']:['A'])await send(side,'NATIVE_REPLACE');}
  if(!published&&both('OWNER_PUBLISHED')){
   for(const s of ['A','B'])validatePublication(s);
   const local=latest(origin,'LOCAL_CLOSE_RETURNED');
   if(!route&&(!local||local.sdk_shutdown_calls!=='1'||local.sdk_shutdown_returns!=='1'))throw Error('NATIVE_LOCAL_CLOSE_NOT_ACTUALLY_CALLED');
   published=true;for(const s of ['A','B'])await send(s,'REACCEPT_DONE');
  }
 }
 function line(side,event,r){
  try {
   requireIdentity(r,side);
   if(route&&event==='NATIVE_ROUTE_OWNERS'){if(r.source!=='SDK_PUBLIC_NODE'||r.stage!=='OLD_BOTH_SETTLED'||r.callbacks!=='0'||r.receivers!=='0')throw Error('OLD_ROUTE_OWNERS_NOT_SETTLED');oldBoth[side]=r;return advance();}
   if(route&&event==='ROS_STARTUP_EVENT'){if(r.source!=='REAL_TARGET')throw Error('NODE_API_SOURCE');if(nodeApis[side].length>=256)throw Error('NODE_API_OBSERVATION_BOUND');nodeApis[side].push(r);return advance();}
   if(route&&event==='NATIVE_ROUTE_EVENT'){
    if(r.source!=='SDK_PUBLIC_NODE'||!['OFFLINE','RESOLVED'].includes(r.stage)||routeStates[side][r.stage])throw Error('ROUTE_EVENT_SOURCE_OR_DUPLICATE');
    const required=r.stage==='OFFLINE'?'NodeOffline_processed':'NodeOnline_processed';
    if(!nodeApis[side].some(x=>x.operation===required&&x.value==='0'&&x.value_type==='LOCAL_STATUS'))throw Error('ROUTE_WITHOUT_PRODUCT_NODE_INPUT');
    if(r.stage==='RESOLVED'){
     const online=nodeApis[side].filter(x=>x.operation===required&&x.value==='0').at(-1);
     let after=BigInt(online.observation_seq||0);
     for(const name of ['GetAllNodes_enter','GetAllNodes_return','GetNodeUdid_enter','GetNodeUdid_return','NodeIdentity_match']){
      const query=nodeApis[side].find(x=>x.operation===name&&BigInt(x.observation_seq||0)>after&&(!name.endsWith('_return')||x.value==='0')&&(name!=='NodeIdentity_match'||x.value==='1'));
      if(!query)throw Error('ROUTE_WITHOUT_FRESH_PUBLIC_QUERY');after=BigInt(query.observation_seq);
     }
    }
    routeStates[side][r.stage]=r;return advance();
   }
  if(route&&event==='NATIVE_SENDER_SUBMISSION'){if(submissions[side].length>=512)throw Error('OFFLINE_SUBMISSION_BOUND');submissions[side].push(r);return finishOffline();}
   if(event==='NATIVE_REACCEPT_REFUSAL')throw Error('NATIVE_ADMISSION_REFUSED_'+side);
   if(event==='NATIVE_REACCEPT_EVENT'){
 const stages=['ARMED','ADMISSION_STOP_REQUESTED','LOCAL_CLOSE_RETURNED','OLD_OWNER_SETTLED','PEER_READY','OWNER_PUBLISHED'];
    if(!stages.includes(r.stage)||events[side].length>=8||latest(side,r.stage)||r.schema!=='1'||!/^([1-9][0-9]*)$/.test(r.seq))throw Error('NATIVE_STAGE_OR_BOUND');
    if(r.stage==='OWNER_PUBLISHED'&&!replacing)throw Error('NATIVE_PUBLISHED_BEFORE_PERMIT');
    const last=events[side].at(-1);if(last&&(BigInt(r.seq)<=BigInt(last.seq)||BigInt(r.wall_ms)<BigInt(last.wall_ms)))throw Error('NATIVE_SEQUENCE_OR_CLOCK');
    events[side].push(r);return advance();
   }
   if(event==='NATIVE_ENTITY_ANCHOR'){
    if(!['BEFORE','AFTER'].includes(r.point)||anchors[side][r.point]||!/^[0-9a-f]{32}$/.test(r.guid))throw Error('NATIVE_ENTITY_ANCHOR');anchors[side][r.point]=r;return;
   }
   if(event==='NATIVE_ENDPOINT_ANCHOR'){
    if(!['BEFORE','AFTER'].includes(r.point)||!['reader','writer'].includes(r.role)||r.identity_source!=='SDK_SEND_SEDP'||!/^[0-9a-f]{32}$/.test(r.guid))throw Error('NATIVE_ENDPOINT_ANCHOR');
    const map=endpoints[side][r.point] ||= {};if(Object.keys(map).length>=64||map[r.guid])throw Error('NATIVE_ENDPOINT_BOUND_OR_DUPLICATE');map[r.guid]={role:r.role,topic:r.topic};return;
   }
   if(event==='ROS_REACCEPT_ANCHOR'){
    const key=r.point+':'+r.phase;if(!['BEFORE','AFTER'].includes(r.point)||!['reliable','best_effort'].includes(r.phase)||app[side][key])throw Error('NATIVE_APP_ANCHOR');
    app[side][key]=r;return;
   }
   if(event==='ROS_WRITE'&&r.segment==='POST'&&!published)throw Error('POST_WRITTEN_BEFORE_NATIVE_PUBLICATION');
  }catch(e){fail(e.message);}
 }
 function complete(){
  try{if(!published)throw Error('NATIVE_REACCEPT_NOT_EXERCISED');for(const s of ['A','B'])for(const qos of ['reliable','best_effort']){
   const a=app[s]['BEFORE:'+qos],b=app[s]['AFTER:'+qos];if(!a||!b)throw Error('NATIVE_APP_IDENTITY_MISSING');
   for(const k of ['pid','node','publisher','subscriber','context','service','client','gid'])if(a[k]!==b[k])throw Error('NATIVE_APP_ENTITY_CHANGED');
  }return true;}catch(e){fail(e.message);return false;}
 }
 async function offlineDone(){
  if(!route||!offline_checked)throw Error('OFFLINE_PHASE_WITHOUT_INVALIDATION');
  for(const side of ['A','B'])await send(side,'NATIVE_OFFLINE_CAPTURE');
 }
 async function finishOffline(){
  if(!route)return;
  for(const side of ['A','B']){
   for(const qos of ['reliable','best_effort']){
    const writer=(app[side]['BEFORE:'+qos]?.gid||'').slice(0,32);if(!/^[0-9a-f]{32}$/.test(writer))return false;
    const pre=submissions[side].filter(x=>x.segment==='PRE'&&x.writer===writer&&x.status==='0'&&x.queue_admitted==='1');
    const numbers=pre.map(x=>(BigInt(x.sequence_hi)<<32n)+BigInt(x.sequence_lo));
    // Frozen PRE has exactly two application writes per original writer.
    if(!numbers.includes(1n)||!numbers.includes(2n)||numbers.some(x=>x>2n))return false;
    if(!submissions[side].some(x=>x.segment==='OFFLINE'&&x.writer===writer&&pre.some(p=>p.sender===x.sender&&/^[1-9][0-9]*$/.test(x.sender||''))&&x.sequence_hi==='0'&&x.sequence_lo==='3'&&x.status==='7'&&x.queue_admitted==='0'&&x.route_known==='1'&&x.route_sample==='ADMISSION_GATE'&&x.available==='0'&&x.route_revision===routeStates[side].OFFLINE?.revision&&x.native_work==='NOT_ENTERED_BY_SUBMISSION'&&Number(x.submission_calls)>0))return false;
   }
  }
  await restore();
  if(!offlineReleased){offlineReleased=true;for(const side of ['A','B'])await send(side,'OFFLINE_DONE');}
 }
 return{line,advance,complete,offlineDone,finishOffline,restore,events,anchors,endpoints,app,routeStates,nodeApis,submissions,get published(){return published;}};
}
module.exports={STAGES,ACTIONS,create};
