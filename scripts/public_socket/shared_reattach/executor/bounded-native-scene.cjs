'use strict';
// Scene metadata and phase selection within the existing ROS controller.
// No HDC driver, SDK calls, identity allocation or permission logic.
const STAGES=['LISTENER_READY','PRE_READY','PRE_DONE','REACCEPT_DONE','POST_DONE','TEARDOWN_READY','DDS_DELETED'];
const ACTIONS=['NATIVE_ARM','NATIVE_END','NATIVE_REPLACE'];
function create(c,{send,fail}){
 const events={A:[],B:[]},anchors={A:{},B:{}},endpoints={A:{},B:{}},app={A:{},B:{}};
 let ending=false,replacing=false,published=false;
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
  if(!ending&&both('ARMED')){ending=true;await send(origin,'NATIVE_END');}
  if(!replacing&&latest('A','OLD_OWNER_SETTLED')&&latest('B','PEER_READY')){replacing=true;await send('A','NATIVE_REPLACE');}
  if(!published&&both('OWNER_PUBLISHED')){
   for(const s of ['A','B'])validatePublication(s);
   const local=latest(origin,'LOCAL_CLOSE_RETURNED');
   if(!local||local.sdk_shutdown_calls!=='1'||local.sdk_shutdown_returns!=='1')throw Error('NATIVE_LOCAL_CLOSE_NOT_ACTUALLY_CALLED');
   published=true;for(const s of ['A','B'])await send(s,'REACCEPT_DONE');
  }
 }
 function line(side,event,r){
  try {
   requireIdentity(r,side);
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
 return{line,advance,complete,events,anchors,endpoints,app,get published(){return published;}};
}
module.exports={STAGES,ACTIONS,create};
