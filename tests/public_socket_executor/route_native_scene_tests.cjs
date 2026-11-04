'use strict';
// Inert receipt/control tests, never SDK/device evidence.
const assert=require('node:assert/strict');
const {create}=require('../../scripts/public_socket/shared_reattach/executor/bounded-native-scene.cjs');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='1234567890abcdef';
const sent=[],errors=[];
const scene=create({run,bounded_native_reaccept:{order:'outgoing-first'},route_lifecycle_native:{trigger:{max_offline_seconds:90}}},{send:async(s,p)=>{sent.push([s,p]);return true;},fail:e=>errors.push(e)});
async function native(side,stage){await scene.line(side,'NATIVE_REACCEPT_EVENT',{run,side,schema:'1',seq:'1',wall_ms:'1',stage,source:'SDK_PUBLIC_SOCKET'});}
(async()=>{
 await native('A','ARMED');await native('B','ARMED');
 assert.deepEqual(sent,[['A','ETH1_DOWN']]);assert.ok(!sent.some(x=>x[1]==='NATIVE_END'));
 for(const side of ['A','B']){
  await scene.line(side,'ROS_STARTUP_EVENT',{run,side,operation:'NodeOffline_processed',value:'0',value_type:'LOCAL_STATUS',source:'REAL_TARGET'});
  await scene.line(side,'NATIVE_ROUTE_EVENT',{run,side,stage:'OFFLINE',source:'SDK_PUBLIC_NODE',revision:'2',available:'0'});
 }
 assert.ok(['A','B'].every(s=>sent.some(x=>x[0]===s&&x[1]==='OFFLINE_READY')));
 await scene.restore();assert.ok(sent.some(x=>x[0]==='A'&&x[1]==='ETH1_UP'));
 assert.deepEqual(errors,[]);
 for(const side of ['A','B'])for(const [qos,digit] of [['reliable','1'],['best_effort','2']]){
  const writer=digit.repeat(32);
  await scene.line(side,'ROS_REACCEPT_ANCHOR',{run,side,point:'BEFORE',phase:qos,gid:writer});
  for(const sn of ['1','2'])await scene.line(side,'NATIVE_SENDER_SUBMISSION',{run,side,segment:'PRE',sender:'5',writer,sequence_hi:'0',sequence_lo:sn,status:'0',queue_admitted:'1'});
  // A rejected PRE retry cannot authorize restoration/OFFLINE completion.
  await scene.line(side,'NATIVE_SENDER_SUBMISSION',{run,side,segment:'OFFLINE',sender:'5',writer,sequence_hi:'0',sequence_lo:'2',status:'7',queue_admitted:'0',route_known:'1',route_sample:'ADMISSION_GATE',available:'0',route_revision:'2',native_work:'NOT_ENTERED_BY_SUBMISSION',submission_calls:'1'});
 }
 assert.equal(await scene.finishOffline(),false);
 assert.ok(!sent.some(x=>x[1]==='OFFLINE_DONE'));
 for(const side of ['A','B'])for(const digit of ['1','2'])await scene.line(side,'NATIVE_SENDER_SUBMISSION',{run,side,segment:'OFFLINE',sender:'5',writer:digit.repeat(32),sequence_hi:'0',sequence_lo:'3',status:'7',queue_admitted:'0',route_known:'1',route_sample:'ADMISSION_GATE',available:'0',route_revision:'2',native_work:'NOT_ENTERED_BY_SUBMISSION',submission_calls:'2'});
 assert.equal(sent.filter(x=>x[1]==='OFFLINE_DONE').length,2);
 let complete,up=0;
 const pending=create({run,bounded_native_reaccept:{order:'outgoing-first'},route_lifecycle_native:{trigger:{max_offline_seconds:90}}},{send:async(s,p)=>{if(p==='ETH1_UP'){up++;await new Promise(r=>complete=r);}return true;},fail:()=>{}});
 for(const side of ['A','B'])await pending.line(side,'NATIVE_REACCEPT_EVENT',{run,side,schema:'1',seq:'1',wall_ms:'1',stage:'ARMED'});
 const first=pending.restore(),second=pending.restore();assert.equal(first,second);
 await new Promise(r=>setImmediate(r));assert.equal(up,1);complete();await second;
 const suppressed=create({run,bounded_native_reaccept:{order:'outgoing-first'},route_lifecycle_native:{trigger:{max_offline_seconds:90}}},{send:async(s,p)=>p!=='ETH1_UP',fail:()=>{}});
 for(const side of ['A','B'])await suppressed.line(side,'NATIVE_REACCEPT_EVENT',{run,side,schema:'1',seq:'1',wall_ms:'1',stage:'ARMED'});
 await assert.rejects(suppressed.restore(),/ETH1_UP_NOT_DISPATCHED/);
 // Exercise the real controller failure latch and its serialized dispatch queue.
 for(const queued of [false,true]){
  const controls=[];let downEntered,finishDown;
  const down=new Promise(r=>downEntered=r);
  const ctl=controller({scenario:'ROS2_DSOFTBUS',run,bounded_native_reaccept:{order:'outgoing-first'},route_lifecycle_native:{trigger:{max_offline_seconds:90}}},async(s,p)=>{
   controls.push([s,p]);if(p==='ETH1_DOWN'){downEntered();if(queued)await new Promise(r=>finishDown=r);}
  },async()=>({A:{pass:true},B:{pass:true}}));
  for(const side of ['A','B'])ctl.line(side,`NATIVE_REACCEPT_EVENT run=${run} side=${side} schema=1 seq=1 wall_ms=1 stage=ARMED`);
  await down;ctl.stop('INJECTED_CAPTURE_FAILURE');if(queued)finishDown();
  const result=await ctl.finish();assert.equal(result.pass,false);
  assert.equal(controls.filter(x=>x[1]==='ETH1_UP').length,1);
  assert.equal(controls.filter(x=>x[1]==='ETH1_DOWN').length,1);
  assert.ok(!controls.some(x=>x[1]==='NATIVE_REPLACE'));
 }
 {
  const controls=[];
  const ctl=controller({scenario:'ROS2_DSOFTBUS',run,bounded_native_reaccept:{order:'outgoing-first'},route_lifecycle_native:{trigger:{max_offline_seconds:90}}},async(s,p)=>{controls.push([s,p]);if(p==='ETH1_UP')throw Error('REMOTE_UP_FAILED');},async()=>({A:{pass:true},B:{pass:true}}));
  for(const side of ['A','B']){
   ctl.line(side,`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${side} pid=${side==='A'?101:102} mode=dual`);
   ctl.line(side,`NATIVE_REACCEPT_EVENT run=${run} side=${side} schema=1 seq=1 wall_ms=1 stage=ARMED`);
  }
  await new Promise(r=>setImmediate(r));ctl.stop('INJECTED_CAPTURE_FAILURE');
  const result=await ctl.finish();assert.equal(result.pass,false);
  assert.equal(controls.filter(x=>x[1]==='ETH1_UP').length,1);
  assert.equal(controls.filter(x=>x[1]==='CLEANUP').length,2);
  assert.equal(result.route_restore_failure,'REMOTE_UP_FAILED');
 }
 console.log('PASS route scene: no END injection; exact A eth1 trigger and compensating restore');
})().catch(e=>{console.error(e);process.exitCode=1;});
