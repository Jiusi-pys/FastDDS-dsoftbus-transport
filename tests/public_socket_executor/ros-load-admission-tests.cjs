'use strict';
// A collector's zero exit is not proof that its live /proc files were retrieved
// and accepted by the formal load analyzer before business release.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const {spawn}=require('node:child_process');
const {createLoadObserver}=require('../../scripts/public_socket/observe-native.cjs');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');

async function main(){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'ros-load-admission-'));
 const run='1234567890abcdef';
 const config={scenario:'ROS2_DSOFTBUS',run,main_member:'ordinary_ros2_transport_consumer',
   hdc_path:path.join(directory,'LOCAL_TEST_DRIVER_NEVER_EXECUTED'),targets:{A:'LOCAL_A',B:'LOCAL_B'},
   remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`}};
 const calls=[];
 const localSpawn=(_exe,args)=>{
  calls.push(args);
  if(args[2]!=='shell')throw Error('No real file capture exists in this negative fixture');
  return spawn(process.execPath,['-e',"process.stdout.write('LOAD_OBSERVER_REMOTE_EXIT=0\\n')"],{windowsHide:true});
 };
 const observer=createLoadObserver({config,directory,spawn:localSpawn});
 const released=[];
 const control=controller(config,async(side,stage)=>{released.push(side+stage);},observer.admission);
 for(const side of ['A','B']){
  const pid=side==='A'?101:102;
  const rows=[
   `ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${side} pid=${pid} mode=dual`,
   `ROS_LISTENER_READY run=${run} side=${side} pid=${pid} listener=7 fake_sdk=0`,
   `ROS_STAGE run=${run} side=${side} pid=${pid} stage=LISTENER_READY`,
   `ROS_PUBLISHER run=${run} side=${side} gid=${(side==='A'?'a':'b').repeat(32)} intra_process=0 phase=reliable topic=/fdds_${run}/reliable/from_${side}`,
   `ROS_GRAPH run=${run} side=${side} peer=/fdds_${run}/transport_${side==='A'?'B':'A'} visible=1 subscription_count=1 phase=reliable`,
   `ROS_STAGE run=${run} side=${side} pid=${pid} stage=RELIABLE_READY`];
  for(const row of rows){observer.output(side,'stdout',Buffer.from(row+'\n'));control.line(side,row);}
 }
 const observed=await observer.admission();
 const report=await control.finish();
 assert.equal(observed.A.pass,false,'A may not pass with no live files');
 assert.equal(observed.B.pass,false,'B may not pass with no live files');
 assert.equal(report.reported_first_host,'ROS_LOAD_CAPTURE_FAILED');
 assert.deepEqual(released.filter(x=>x.endsWith('RELIABLE_READY')),[]);
 assert.equal(calls.filter(x=>x[2]==='shell').length,2);
 console.log('PASS collector exit zero without files cannot release ROS RELIABLE');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
