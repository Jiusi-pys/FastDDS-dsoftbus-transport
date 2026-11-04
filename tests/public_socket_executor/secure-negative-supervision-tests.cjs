'use strict';
// Inert local subprocess and controller tests. No HDC, SDK or device evidence.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {spawn}=require('node:child_process');
const {createLoadObserver}=require(process.env.FDDST_TEST_OBSERVE_SOURCE||'../../scripts/public_socket/observe-native.cjs');
const {controller}=require(process.env.FDDST_TEST_CONTROL_SOURCE||'../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='1234567890abcdef',tick=()=>new Promise(r=>setImmediate(r));
const pid=s=>s==='A'?101:102;
const main=s=>`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${s} pid=${pid(s)} mode=dual`;
const ready=s=>`ROS_RUNTIME_CLOSE_READY schema=1 run=${run} side=${s} pid=${pid(s)} stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1`;
function config(scene){return {scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true,main_member:'ordinary_ros2_transport_consumer',
  hdc_path:'INERT_NEVER_EXECUTED',targets:{A:'INERT_A',B:'INERT_B'},
  remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`},
  ...(scene?{secure_candidate:{spec:{case:scene}}}:{})};}
async function listenerCapture(scene,expected){
 const base=path.resolve(__dirname,'../../build/secure-candidate-target-20261009/negative-supervision-local');fs.mkdirSync(base,{recursive:true});
 const directory=fs.mkdtempSync(path.join(base,'capture-'));let calls=0;
 const observer=createLoadObserver({config:config(scene),directory,spawn:()=>{calls++;return spawn(process.execPath,['-e',"console.log('LOAD_OBSERVER_REMOTE_EXIT=0')"],{windowsHide:true});}});
 for(const s of ['A','B'])observer.output(s,'stdout',Buffer.from(main(s)+`\nROS_STAGE run=${run} side=${s} pid=${pid(s)} stage=LISTENER_READY\n`));
 const result=await observer.admission();
 assert.equal(calls>0,expected,'negative captures must start before reliable match');
 if(expected)for(const s of ['A','B']){assert.notEqual(result[s].error,'NO_NATIVE_PID_RECORD');assert.equal(result[s].pid_source,'ROS_MAIN_AND_LISTENER_READY_BOUND');}
 assert.equal(result.A.pass,false,'inert collector with no real files never qualifies');
}
async function listenerGate(scene,pass){
 let resolve,admissions=0;const pending=new Promise(r=>resolve=r),released=[];
 const ctl=controller(config(scene),async(s,stage)=>released.push(s+':'+stage),()=>{admissions++;return pending;});
 for(const s of ['A','B']){ctl.line(s,main(s));ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${pid(s)} listener=7 fake_sdk=0`);ctl.line(s,`ROS_STAGE run=${run} side=${s} pid=${pid(s)} stage=LISTENER_READY`);}
 await tick();assert.equal(admissions,1);assert.deepEqual(released,[],'neither SDK connect release before pair admission');
 resolve({A:{pass:true},B:{pass}});await tick();await tick();
 assert.equal(released.filter(x=>x.endsWith(':LISTENER_READY')).length,pass?2:0);
 const result=await ctl.finish();if(!pass)assert.equal(result.reported_first_host,'ROS_LOAD_CAPTURE_FAILED');
}
async function queuedClose(mutate){
 let resolve;const waiting=new Promise(r=>resolve=r),commands=[];
 const ctl=controller(config(),async(s,stage,meta)=>{commands.push({s,stage,...meta});if(s==='A'&&stage==='CLEANUP')await waiting;},async()=>({A:{pass:true},B:{pass:true}}));
 for(const s of ['A','B'])ctl.line(s,main(s));ctl.stop('ORIGINAL_FAILURE');await tick();
 ctl.line('B',mutate?mutate(ready('B')):ready('B'));await tick();
 assert.equal(commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,0,'queued readiness cannot bypass cleanup completion');
 resolve();await tick();await tick();const result=await ctl.finish();
 assert.equal(commands.filter(x=>x.s==='B'&&x.stage==='RUNTIME_CLOSE').length,mutate?0:1);
 assert.equal(result.reported_first_host,'ORIGINAL_FAILURE');
 if(!mutate){assert.equal(result.runtime_close_ready.B.pid,'102');assert(!result.capture_failure.includes('ROS_RUNTIME_CLOSE_READY_IDENTITY'));}
}
(async()=>{
 for(const scene of ['S2','S3']){await listenerCapture(scene,true);await listenerGate(scene,true);await listenerGate(scene,false);}
 for(const scene of [null,'S1','S1_FRAGMENT'])await listenerCapture(scene,false);
 await queuedClose();
 for(const mutate of [x=>x.replace('pid=102','pid=999'),x=>x.replace('active_pumps=0','active_pumps=1'),x=>x+' pid=102'])await queuedClose(mutate);
 console.log('PASS secure negative listener admission and queued close; NO_DEVICE_EVIDENCE');
})().catch(e=>{console.error(e);process.exitCode=1;});
