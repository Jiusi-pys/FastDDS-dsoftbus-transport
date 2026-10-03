// Pure review seam: actual controller + actual control release; injected EventEmitter spawn, zero child/HDC.
'use strict';
const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs'),{EventEmitter}=require('node:events');
const repo=path.resolve(__dirname,'../..'),tmp=path.join(repo,'build/ros2-target/tmp');
fs.mkdirSync(tmp,{recursive:true});
const out=fs.mkdtempSync(path.join(tmp,'runtime-close-late-main-')),run='1234567890abcdef';
const {controller,STAGES}=require(path.join(repo,'scripts/public_socket/shared_reattach/executor/ros-control.cjs'));
const {createControlRelease}=require(path.join(repo,'scripts/public_socket/shared_reattach/executor/runner-template.cjs'));
(async()=>{
 const calls=[],spawns=[];let runtimeChild;
 const c={scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true,hdc_path:process.execPath,stages:STAGES,targets:{A:'REVIEW-ONLY-A',B:'REVIEW-ONLY-B'},remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`}};
 const finish=child=>{child.stdout.emit('data',Buffer.from('CONTROL_REMOTE_EXIT=0\n'));child.emit('exit',0,null);child.emit('close',0,null);};
 const release=createControlRelease(c,out,{spawn:(exe,argv)=>{
   assert.equal(exe,process.execPath);const child=new EventEmitter();child.pid=600+spawns.length;child.stdout=new EventEmitter();child.stderr=new EventEmitter();child.kill=()=>{throw Error('MUST_NOT_KILL');};
   spawns.push({exe,argv});if(argv[3].includes('control.RUNTIME_CLOSE'))runtimeChild=child;else setImmediate(()=>finish(child));return child;
 }});
 const ctl=controller(c,async(side,stage,meta)=>{calls.push({side,stage,...meta});return release(side,stage,meta);},async()=>{throw Error('NO_BUSINESS');});
 const tick=()=>new Promise(r=>setImmediate(r));
 ctl.line('A',`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=A pid=101 mode=dual`);
 ctl.stop('FIRST_STARTUP_FAILURE');await tick();await tick();
 ctl.line('A',`ROS_RUNTIME_CLOSE_READY schema=1 run=${run} side=A pid=101 stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1`);await tick();
 assert(runtimeChild,'A release must be in flight');
 ctl.line('B',`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=B pid=102 mode=dual`);await tick();
 finish(runtimeChild);await tick();
 const result=await ctl.finish();
 const observed={scope:'PURE_CONTROLLER_AND_CONTROL_RELEASE_INJECTED_SPAWN',real_children:0,real_hdc:0,calls,spawn_count:spawns.length,actual_control_receipts:release.receipts,cleanup_controls:result.cleanup_controls,cleanup_control_failures:result.cleanup_control_failures,runtime_close_controls:result.runtime_close_controls,first_failure:result.reported_first_host,pass:result.pass};
 fs.writeFileSync(path.join(out,'ACTUAL_RESULT.json'),JSON.stringify(observed,null,2)+'\n',{flag:'wx'});
 assert.equal(result.cleanup_controls.B.state,'COMPLETED');assert.equal(result.cleanup_control_failures.length,0);assert.equal(spawns.length,3);assert.equal(result.reported_first_host,'FIRST_STARTUP_FAILURE');
 console.log(JSON.stringify({pass:true,actual_spawn_seam_calls:spawns.length,real_children:0,real_hdc:0,out},null,2));
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
