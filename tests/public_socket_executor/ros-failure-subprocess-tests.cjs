'use strict';
const assert=require('node:assert/strict');
const {spawn}=require('node:child_process');
const {runRecordPair}=require('../../scripts/public_socket/record-pair.cjs');
const {createLines}=require('../../scripts/public_socket/bounded-output.cjs');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='1234567890abcdef';
async function trial(early){
 const released=[],ctl=controller({scenario:'ROS2_DSOFTBUS',run},async(...r)=>{if(r[1]!=='CLEANUP')released.push(r);},async()=>{throw Error('No business admission in failure test');});
 const streams={},raw={A:'',B:''};for(const side of ['A','B'])for(const stream of ['stdout','stderr'])streams[side+stream]=createLines(line=>ctl.line(side,line),reason=>ctl.stop(reason));
 let starts=0;
 const result=await runRecordPair({executable:process.execPath,targets:{A:'LOCAL_A',B:'LOCAL_B'},commands:{A:'LOCAL_FAILURE_TEXT',B:'LOCAL_FAILURE_TEXT'},timeoutMs:3000,drainGraceMs:500,
  mainLineRegex:new RegExp(`^ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=A pid=[1-9][0-9]* mode=dual$`),
  spawn:(program,args,options)=>{
   assert.equal(program,process.execPath);assert.equal(args[0],'-t');assert.equal(args[2],'shell');assert.equal(args[3],'LOCAL_FAILURE_TEXT');assert(['LOCAL_A','LOCAL_B'].includes(args[1]));++starts;
   const side=args[1].slice(-1);
   // An actual local OS child writes protocol text. This is not an SDK driver.
   const source=`const side=${JSON.stringify(side)},run=${JSON.stringify(run)},pid=process.pid;
    process.stdout.write('ROS_MAIN scenario=ROS2_DSOFTBUS run='+run+' side='+side+' pid='+pid+' mode=dual\\r\\n');
    setTimeout(()=>{const message='ROS_FAILURE schema=1 run='+run+' side='+side+' pid='+pid+' seq=1 stage=MATCH reason=REMOTE_MATCH_MISSING failure_ref=0\\n';process.stderr.write(message.slice(0,17));setTimeout(()=>process.stderr.write(message.slice(17)),10);},20);
    setTimeout(()=>{${early?"process.exitCode=4;":"process.stderr.write('ROS_CLEANUP_FAILURE schema=1 run='+run+' side='+side+' pid='+pid+' seq=2 stage=PUBLIC_DELETE reason=PARTICIPANT_DELETE_FAILED failure_ref=1');process.stdout.write('REMOTE_EXIT=4\\n');"}},80);`;
   return spawn(process.execPath,['-e',source],{...options,shell:false});
  },onOutput:(side,stream,data)=>{raw[side]+=data.toString();streams[side+stream](data);},onStreamEnd:(side,confirmed)=>{streams[side+'stdout'].end(confirmed);streams[side+'stderr'].end(confirmed);ctl.end(side,confirmed);}});
 const phase=await ctl.finish();assert.equal(starts,2);assert.equal(result.timedOut,false);assert.equal(result.pass,false);assert.equal(released.length,0);
 for(const side of ['A','B']){assert.equal(phase.primary_failure[side].reason,'REMOTE_MATCH_MISSING');assert.equal(result[side].termination,null);assert.equal(result[side].closed,true);if(!early){assert.equal(phase.cleanup_failure[side][0].seq,'2');assert.equal(result[side].remoteExit,4);}else assert.equal(result[side].remoteExit,null);}
 return {kind:early?'early_exit_missing_remote_receipt':'late_cleanup_no_final_newline',pass:true,starts,primary_preserved:true,collector_terminated_on_primary:false,device_operations:0};
}
(async()=>console.log(JSON.stringify([await trial(false),await trial(true)],null,2)))().catch(e=>{console.error(e);process.exitCode=1;});
