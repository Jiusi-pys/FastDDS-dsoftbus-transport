'use strict';
// Inert controller fixtures; these are not SDK execution evidence.
const assert=require('node:assert/strict');
const {create}=require('../../scripts/public_socket/shared_reattach/executor/bounded-native-scene.cjs');
const run='1234567890abcdef';
function fixture(order='outgoing-first') {
 const sent=[],errors=[];const c={run,bounded_native_reaccept:{order}};
 const scene=create(c,{send:async(s,p)=>sent.push([s,p]),fail:e=>errors.push(e)});
 let seq={A:0,B:0};
 async function event(s,stage,extra={}) {
  return scene.line(s,'NATIVE_REACCEPT_EVENT',{run,side:s,schema:'1',seq:String(++seq[s]),wall_ms:String(seq[s]),stage,source:'SDK_PUBLIC_SOCKET',...extra});
 }
 const old={old_id:'2',old_owner:'10',listener:'1',listener_entry:'100',client_entry:'101'};
 function anchors(s,point) {
  scene.line(s,'NATIVE_ENTITY_ANCHOR',{run,side:s,point,runtime:'1000',participant:'wire',guid:'1'.repeat(32)});
  scene.line(s,'NATIVE_ENDPOINT_ANCHOR',{run,side:s,point,role:'writer',identity_source:'SDK_SEND_SEDP',guid:'2'.repeat(32),topic:'rt/inert'});
  for(const phase of ['reliable','best_effort'])scene.line(s,'ROS_REACCEPT_ANCHOR',{run,side:s,point,phase,pid:'123',node:'1',publisher:'2',subscriber:'3',context:'4',service:'5',client:'6',gid:'2'.repeat(32)});
 }
 async function ready() {
  for(const s of ['A','B']){anchors(s,'BEFORE');await event(s,'ARMED',old);}
  await event(order==='outgoing-first'?'A':'B','LOCAL_CLOSE_RETURNED',{sdk_shutdown_calls:'1',sdk_shutdown_returns:'1'});
  for(const s of ['A','B'])await event(s,'OLD_OWNER_SETTLED',{callbacks:'0',receivers:'0',shutdown_returned:'1',on_shutdown_seen:'0'});
  await event('B','PEER_READY');
 }
 async function publish(extra={}) {
  for(const s of ['A','B']){anchors(s,'AFTER');await event(s,'OWNER_PUBLISHED',{...old,new_id:'4',new_owner:'11',sdk_epoch:'NOT_PROVEN',sdk_drain:'NOT_PROVEN',...extra});}
 }
 return {scene,sent,errors,event,old,ready,publish};
}
(async()=>{
 for(const order of ['outgoing-first','accepted-first']) {
  const f=fixture(order);await f.ready();await f.publish();assert.deepEqual(f.errors,[]);assert.equal(f.scene.complete(),true);
  assert.deepEqual(f.sent,[[order==='outgoing-first'?'A':'B','NATIVE_END'],['A','NATIVE_REPLACE'],['A','REACCEPT_DONE'],['B','REACCEPT_DONE']]);
 }
 for(const extra of [{new_id:'2'},{new_owner:'10'},{source:'HOST_OPAQUE_PUBLIC_API_SHAPE'},{listener:'9'},{sdk_drain:'PROVEN'}]) {
  const f=fixture();await f.ready();try{await f.publish(extra);}catch(e){f.errors.push(e.message);}
  assert.ok(f.errors.length);assert.equal(f.scene.published,false);assert.ok(!f.sent.some(x=>x[1]==='REACCEPT_DONE'));
 }
 {const f=fixture();await f.event('A','OWNER_PUBLISHED',f.old);assert.deepEqual(f.errors,['NATIVE_PUBLISHED_BEFORE_PERMIT']);}
 {const f=fixture();f.scene.line('A','ROS_WRITE',{run,side:'A',segment:'POST'});assert.deepEqual(f.errors,['POST_WRITTEN_BEFORE_NATIVE_PUBLICATION']);}
 {const f=fixture();await f.event('A','ARMED',f.old);await f.event('A','ARMED',f.old);assert.deepEqual(f.errors,['NATIVE_STAGE_OR_BOUND']);}
 {
  const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
  let unblock;const held=new Promise(resolve=>{unblock=resolve;});const calls=[];
  const ctl=controller({scenario:'ROS2_DSOFTBUS',run,bounded_native_reaccept:{order:'outgoing-first'}},async(s,p)=>{calls.push([s,p]);if(p==='LISTENER_READY')await held;},async()=>({A:{pass:true},B:{pass:true}}));
  for(const s of ['A','B']) {
   const pid=s==='A'?101:102;
   ctl.line(s,`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${s} pid=${pid} mode=dual`);
   ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${pid} listener=1 fake_sdk=0`);
   ctl.line(s,`ROS_STAGE run=${run} side=${s} pid=${pid} stage=LISTENER_READY`);
  }
  await new Promise(r=>setImmediate(r));assert.ok(calls.some(x=>x[1]==='LISTENER_READY'));
  for(const s of ['A','B'])ctl.line(s,`NATIVE_REACCEPT_EVENT run=${run} side=${s} schema=1 seq=1 wall_ms=1 stage=ARMED source=SDK_PUBLIC_SOCKET`);
  ctl.line('A',`ROS_FAILURE run=${run} side=A reason=APPLICATION_FAILURE`);
  unblock();await ctl.finish();assert.ok(!calls.some(x=>['NATIVE_END','NATIVE_REPLACE','REACCEPT_DONE'].includes(x[1])));assert.ok(calls.some(x=>x[1]==='CLEANUP'));
 }
 console.log('PASS 11 bounded native scene model cases; NO_DEVICE_EVIDENCE');
})().catch(e=>{console.error(e);process.exitCode=1;});
