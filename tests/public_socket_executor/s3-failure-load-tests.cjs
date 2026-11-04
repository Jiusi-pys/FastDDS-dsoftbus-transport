'use strict';
// Controller/observer contracts only, inert subprocesses; no HDC or SDK.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const {createLoadObserver}=require('../../scripts/public_socket/observe-native.cjs');
const run='1234567890abcdef',tick=()=>new Promise(r=>setImmediate(r));
const pid=s=>s==='A'?101:102,main=s=>`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${s} pid=${pid(s)} mode=dual`;
const failure=s=>`ROS_FAILURE schema=1 run=${run} side=${s} pid=${pid(s)} seq=1 stage=NODE_CREATE reason=ROS_OPERATION_EXCEPTION failure_ref=0`;
const ready=s=>`ROS_FAILURE_LOAD_READY schema=1 run=${run} side=${s} pid=${pid(s)} failure_ref=1 stage=FAILED_CONSTRUCTION`;
const config={scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true,main_member:'ordinary_ros2_transport_consumer',secure_candidate:{spec:{case:'S3'}},
 hdc_path:'INERT_NEVER_EXECUTED',targets:{A:'INERT_A',B:'INERT_B'},remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`}};
async function controlCase(pass,missing,mutate){
 let resolve,admissions=0;const wait=new Promise(r=>resolve=r),commands=[];
 const ctl=controller(config,async(s,stage,metadata)=>commands.push({s,stage,...metadata}),()=>{admissions++;return wait;});
 for(const s of ['A','B']){ctl.line(s,main(s));ctl.line(s,failure(s));}
 ctl.line('A',mutate?mutate(ready('A')):ready('A'));await tick();
 assert.equal(admissions,0,'first waiter must not cache a missing peer');
 if(missing)ctl.end('B',true);else ctl.line('B',ready('B'));await tick();
 if(mutate){ctl.end('A',true);resolve({A:{pass:false},B:{pass:false}});await ctl.finish();return;}
 assert.equal(admissions,1);
 assert.equal(commands.filter(x=>x.stage==='FAILED_CONSTRUCTION_LOAD').length,0);
 resolve({A:{pass},B:{pass:pass&&!missing}});await tick();await tick();const result=await ctl.finish();
 const acknowledgements=commands.filter(x=>x.stage==='FAILED_CONSTRUCTION_LOAD');
 assert.equal(acknowledgements.length,missing?1:2,'capture failure must release every valid waiting process');
 assert(acknowledgements.every(x=>x.reason===(pass&&!missing?'CAPTURE_COMPLETE':'CAPTURE_FAILED')));
 assert.equal(result.primary_failure.A.reason,'ROS_OPERATION_EXCEPTION');assert.equal(result.pass,false);
 assert(!commands.some(x=>x.stage==='LISTENER_READY'||x.stage==='RELIABLE_READY'));
}
async function observerCase(){
 const base=path.resolve(__dirname,'../../build/secure-candidate-target-20261009/s3-load-local');fs.mkdirSync(base,{recursive:true});
 const directory=fs.mkdtempSync(path.join(base,'capture-'));let calls=0;
 const observer=createLoadObserver({config,directory,spawn:()=>{calls++;return spawn(process.execPath,['-e',"console.log('LOAD_OBSERVER_REMOTE_EXIT=0')"],{windowsHide:true});}});
 for(const s of ['A','B'])observer.output(s,'stdout',Buffer.from(main(s)+'\n'+failure(s)+'\n'+ready(s)+'\n'));
 const result=await observer.admission();assert(calls>0);assert.equal(result.A.target_pid,101);assert.equal(result.B.target_pid,102);
 assert.equal(result.A.pid_source,'ROS_MAIN_AND_FAILED_CONSTRUCTION_BOUND');assert.equal(result.A.pass,false,'no formal files cannot qualify');
}
(async()=>{await controlCase(true,false);await controlCase(false,false);await controlCase(false,true);for(const f of [x=>x.replace('pid=101','pid=999'),x=>x.replace('failure_ref=1','failure_ref=2'),x=>x+' pid=101'])await controlCase(false,false,f);await observerCase();console.log('PASS S3 failed-construction capture coordination; NO_DEVICE_EVIDENCE');})().catch(e=>{console.error(e);process.exitCode=1;});
