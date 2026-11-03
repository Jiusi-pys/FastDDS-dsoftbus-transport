'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='1234567890abcdef';
function fixture(){
 const released=[],cleanup=[];
 const ctl=controller({scenario:'ROS2_DSOFTBUS',run},async(s,p,m)=>(p==='CLEANUP'?cleanup:released).push([s,p,m]),async()=>({A:{pass:true},B:{pass:true}}));
 for(const s of ['A','B'])ctl.line(s,`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${s} pid=${s==='A'?101:102} mode=dual`);
 return {ctl,released,cleanup};
}
async function stage(f,p){
 for(const s of ['A','B'])f.ctl.line(s,`ROS_STAGE run=${run} side=${s} pid=${s==='A'?101:102} stage=${p}`);
 await new Promise(r=>setImmediate(r));
}
(async()=>{
 const cases=[];
 async function test(name,fn){try{await fn();cases.push({name,pass:true});}catch(e){cases.push({name,pass:false,error:e.message});}}
 await test('duplicate identity cannot release',async()=>{
  const f=fixture();f.ctl.line('A',`ROS_STAGE run=wrong run=${run} side=A pid=101 stage=LISTENER_READY`);
  f.ctl.line('B',`ROS_STAGE run=${run} side=B pid=102 stage=LISTENER_READY`);
  const result=await f.ctl.finish();assert.equal(f.released.length,0);assert.equal(result.first_failure,'ROS_RECEIPT_MALFORMED');
 });
 await test('listener requires actual backend receipt',async()=>{
  const f=fixture();await stage(f,'LISTENER_READY');assert.equal(f.released.length,0);
 });
 await test('failure latch blocks subsequent release',async()=>{
  const f=fixture();f.ctl.line('A',`ROS_FAILURE run=${run} side=A reason=APPLICATION_FAILURE`);
  await stage(f,'LISTENER_READY');assert.equal(f.released.length,0);
  assert.equal((await f.ctl.finish()).first_failure,'ROS_APPLICATION_FAILURE_A');
 });
 await test('ready requires graph and publisher',async()=>{
  const f=fixture();for(const s of ['A','B'])f.ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${s==='A'?101:102} listener=7 fake_sdk=0`);
  await stage(f,'LISTENER_READY');assert.equal(f.released.length,2);
  await stage(f,'RELIABLE_READY');assert.equal(f.released.length,2);
 });
 await test('listener handle must exist and be valid',async()=>{
  for(const value of ['', 'listener=-1 ', 'listener=NaN ', 'listener=2147483648 ']){
   const f=fixture();for(const s of ['A','B'])f.ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${s==='A'?101:102} ${value}fake_sdk=0`);
   await stage(f,'LISTENER_READY');assert.equal(f.released.length,0);
  }
 });
 await test('missing service invocation identity is rejected',async()=>{
  const f=fixture();f.ctl.line('A',`ROS_SERVICE_SENT run=${run} phase=reliable a=3 b=4`);
  assert.equal((await f.ctl.finish()).first_failure,'ROS_SERVICE_IDENTITY');
 });
 await test('handled service operands must match submitted request',async()=>{
  const f=fixture();f.ctl.line('B',`ROS_SERVICE_REQUEST run=${run} phase=reliable a=4 b=3 sum=7 request_id=1 writer=${'1'.repeat(32)}`);
  assert.equal((await f.ctl.finish()).first_failure,'ROS_SERVICE_IDENTITY');
 });
 await test('actual ROS text passes the seven declared bounded stages',async()=>{
  const f=fixture();
  for(const s of ['A','B'])f.ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${s==='A'?101:102} listener=7 fake_sdk=0`);
  await stage(f,'LISTENER_READY');
  const text={};
  for(const s of ['A','B'])text[s]=fs.readFileSync(path.join(__dirname,'../ros2_host/fixtures',s+'.stdout'),'utf8').replaceAll('42ba5e959d97d571',run).split(/\r?\n/);
  for(const phase of ['reliable','best_effort']){
   for(const s of ['A','B'])for(const row of text[s])if(row.includes('phase='+phase)&&/^ROS_(PUBLISHER|GRAPH) /.test(row))f.ctl.line(s,row);
   await stage(f,phase==='reliable'?'RELIABLE_READY':'BEST_EFFORT_READY');
   for(const s of ['A','B'])for(const row of text[s])if(row.includes('phase='+phase)&&!/^ROS_(PUBLISHER|GRAPH) /.test(row))f.ctl.line(s,row);
   await stage(f,phase==='reliable'?'RELIABLE_DONE':'BEST_EFFORT_DONE');
  }
  await stage(f,'TEARDOWN_READY');
  for(const s of ['A','B']){
   f.ctl.line(s,`ROS_DDS_DELETED run=${run} side=${s} pid=${s==='A'?101:102} dds_component_result=0 api_return=NOT_OBSERVED scope=FACTORY_POST_DELETE`);
   f.ctl.line(s,`ROS_CONTEXT_CLOSED run=${run} side=${s} result=0`);
  }
  await stage(f,'DDS_DELETED');for(const s of ['A','B'])f.ctl.end(s,true);
  const result=await f.ctl.finish();assert.equal(result.pass,true,JSON.stringify(result));assert.equal(f.released.length,10);
 });
 console.log(JSON.stringify(cases,null,2));process.exitCode=cases.some(c=>!c.pass)?1:0;
})().catch(e=>{console.error(e);process.exitCode=1;});
