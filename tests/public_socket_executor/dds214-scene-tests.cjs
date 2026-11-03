'use strict';
const assert=require('node:assert/strict');
const runner=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const c={scenario:'DDS214_LAYER_BASELINE',run:'1234567890abcdef',main_member:'fastdds_dsoftbus_dds214_baseline',main_prefix:'DDS214_MAIN',main_mode:'normal',timeout_seconds:240,system_library_dirs:['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'],token:{A:'123',B:'124'},targets:{A:'SIM-A',B:'SIM-B'},remote:{A:'/data/local/tmp/mdds_dds_1234567890abcdef_A',B:'/data/local/tmp/mdds_dds_1234567890abcdef_B'}};
async function test(){
 const commands=runner.buildCommands(c,c.token);
 assert(commands.A.includes('/fastdds_dsoftbus_dds214_baseline A '+c.run+' normal'));
 assert(commands.A.includes('FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX='));
 assert(!commands.A.includes('RMW_IMPLEMENTATION='));
 assert.throws(()=>runner.buildCommands({...c,main_member:'ordinary_ros2_transport_consumer'},c.token));
 assert.throws(()=>runner.buildCommands({...c,main_mode:'delete-refusal'},c.token));
 const release=[];let admitted=0;
 const ctrl=runner.createController(c,async(s,t)=>release.push(s+':'+t),async()=>{admitted++;return {A:{pass:false},B:{pass:true}};});
 for(const [s,pid] of [['A',101],['B',102]])ctrl.line(s,`DDS214_MAIN scenario=DDS214_LAYER_BASELINE run=${c.run} side=${s} pid=${pid} mode=normal`);
 ctrl.line('A',`ROS_LISTENER_READY run=${c.run} side=A pid=101 listener=1 fake_sdk=0`);
 await new Promise(r=>setImmediate(r));assert.deepEqual(release,[]);
 ctrl.line('B',`ROS_LISTENER_READY run=${c.run} side=B pid=102 listener=1 fake_sdk=0`);
 await new Promise(r=>setImmediate(r));assert.deepEqual(release,['A:LISTENER_READY','B:LISTENER_READY']);assert.equal(admitted,0);
 for(const [s,pid] of [['A',101],['B',102]]){
  const prefix=s==='A'?'1':'2';const peer=s==='A'?'B':'A';
  ctrl.line(s,`DDS214_ID run=${c.run} side=${s} participant=${prefix.repeat(32)} writer=${prefix.repeat(31)}3 reader=${prefix.repeat(31)}4 writer_topic=dds214_${c.run}_from_${s} reader_topic=dds214_${c.run}_from_${peer}`);
  ctrl.line(s,`DDS214_MATCH run=${c.run} side=${s} writers=1 readers=1`);
  ctrl.line(s,`DDS214_STAGE run=${c.run} side=${s} pid=${pid} stage=MATCH`);
 }
 const result=await ctrl.finish();assert.equal(admitted,1);assert.equal(result.pass,false);assert.equal(result.first_failure,'DDS214_LOAD_CAPTURE_FAILED');assert.deepEqual(release,['A:LISTENER_READY','B:LISTENER_READY']);
 console.log('DDS214_SCENE_TEST_PASS command/listener-before-match/load-failure');
}
test().catch(e=>{console.error(e);process.exitCode=1;});
