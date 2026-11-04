'use strict';
// Real observer parsing; inert collector subprocess, no HDC or device files.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {createLoadObserver}=require('../../scripts/public_socket/observe-native.cjs');
async function check(native,stage,expected){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'native-load-stage-')),run='1234567890abcdef';let calls=0,ready=0;
 const config={scenario:'ROS2_DSOFTBUS',run,remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`},targets:{A:'INERT_A',B:'INERT_B'},hdc_path:'INERT_NOT_EXECUTED',...(native?{bounded_native_reaccept:{contract:'BOUNDED_NORMAL_ONCE'}}:{})};
 const observer=createLoadObserver({config,directory,onRosReady:()=>{ready++;},spawn:()=>{calls++;return spawn(process.execPath,['-e',"process.stdout.write('LOAD_OBSERVER_REMOTE_EXIT=0\\n')"],{windowsHide:true});}});
 observer.output('A','stdout',Buffer.from(`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=A pid=101 mode=dual\nROS_STAGE run=${run} side=A pid=101 stage=${stage}\n`));
 const result=await observer.admission();assert.equal(calls>0,expected===1);assert.equal(ready,expected);assert.equal(result.A.pass,false,'Missing formal files remain FAIL');
 if(expected)assert.notEqual(result.A.error,'NO_NATIVE_PID_RECORD');else assert.equal(result.A.error,'NO_NATIVE_PID_RECORD');
}
(async()=>{await check(true,'PRE_READY',1);await check(true,'RELIABLE_READY',0);await check(false,'RELIABLE_READY',1);await check(false,'PRE_READY',0);console.log('PASS 4 observer stage cases; NO_DEVICE_EVIDENCE');})().catch(e=>{console.error(e);process.exitCode=1;});
