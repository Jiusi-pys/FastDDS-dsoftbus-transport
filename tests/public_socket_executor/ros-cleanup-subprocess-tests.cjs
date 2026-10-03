'use strict';
// Real local child/pipe control test. Text model only, not ROS/SDK evidence.
const assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='bcde1234567890ab';
async function test(failed,options={}){
 const children={},logs={},commands=[],pending=[],started={},failure_order=[];let ctl;
 const helper=`const rl=require('node:readline').createInterface({input:process.stdin});const side=process.argv[1],run=process.argv[2];console.log('ROS_MAIN scenario=ROS2_DSOFTBUS run='+run+' side='+side+' pid='+process.pid+' mode=dual');console.log('CHILD_WAITING_FOR_FINAL_OR_CLEANUP side='+side);rl.on('line',line=>{if(line==='FAIL'){console.log('Failed to delete participant');return;}if(line==='EXIT'){process.exit(4);}const expected='ROS_CLEANUP schema=1 run='+run+' side='+side+' pid='+process.pid+' stage=CLEANUP reason=WINDOW_FAILED';if(line!==expected){console.log('CHILD_BAD_CLEANUP side='+side);process.exit(3);}else{console.log('CHILD_CLEANED side='+side+' business_released=0');process.exit(4);}});`;
 ctl=controller({scenario:'ROS2_DSOFTBUS',run},async(side,stage,meta)=>{
   commands.push({side,stage,meta});if(stage!=='CLEANUP')throw Error('BUSINESS_RELEASE_AFTER_FAILURE');
   if(options.channelMissing)throw Error('LOCAL_CONTROL_CHANNEL_MISSING');
   if(options.channelTimeout)throw Error('LOCAL_CONTROL_DEADLINE');
   const row=`ROS_CLEANUP schema=1 run=${options.wrongRun?'0'.repeat(16):run} side=${options.wrongSide?(side==='A'?'B':'A'):side} pid=${options.wrongPid?Number(meta.pid)+1:meta.pid} stage=${options.wrongPhase?'BEST_EFFORT_DONE':'CLEANUP'} reason=${meta.reason}`;
   children[side].stdin.write(row+'\n');
 },async()=>({A:{pass:true},B:{pass:true}}));
 for(const side of ['A','B']){
  logs[side]='';const child=spawn(process.execPath,['-e',helper,side,run],{stdio:['pipe','pipe','pipe']});children[side]=child;
  pending.push(new Promise(resolve=>child.once('close',(code,signal)=>resolve({side,code,signal}))));
  let tail='';child.stdout.on('data',b=>{logs[side]+=b;tail+=b;for(;;){const i=tail.indexOf('\n');if(i<0)break;const row=tail.slice(0,i).replace(/\r$/,'');tail=tail.slice(i+1);if(row.startsWith('ROS_MAIN '))started[side]=true;if(row==='Failed to delete participant')failure_order.push(side);ctl.line(side,row);}});
  child.stderr.on('data',b=>{logs[side]+=b;});
 }
 const wait=async(predicate,budget)=>{const end=Date.now()+budget;while(!predicate()&&Date.now()<end)await new Promise(r=>setTimeout(r,10));return predicate();};
 try{
  assert(await wait(()=>started.A&&started.B,3000),'two actual child mains');
  for(const side of failed)children[side].stdin.write('FAIL\n');
  await wait(()=>logs.A.includes('CHILD_CLEANED')&&logs.B.includes('CHILD_CLEANED'),1500);
  if(options.duplicate)ctl.stop('REPEATED_FAILURE_REQUEST');
  const result=await ctl.finish();
  if(options.channelMissing||options.channelTimeout){assert.equal(result.pass,false);assert(result.cleanup_control_failures?.length,'cleanup failure independently retained');}
  else if(options.wrongRun||options.wrongSide||options.wrongPid||options.wrongPhase){assert(!logs.A.includes('CHILD_CLEANED')&&!logs.B.includes('CHILD_CLEANED'));assert(logs.A.includes('CHILD_BAD_CLEANUP')&&logs.B.includes('CHILD_BAD_CLEANUP'));assert.equal(result.pass,false);}
  else{assert(logs.A.includes('CHILD_CLEANED')&&logs.B.includes('CHILD_CLEANED'),'failure must allow both real children legal cleanup');assert.equal(commands.length,2,'one cleanup per child');assert.equal(result.first_failure,'ROS_CLEANUP_FAILURE_'+failure_order[0]);assert.equal(result.pass,false);}
  assert(commands.every(x=>x.stage==='CLEANUP'));return {failed,options,commands,result,logs};
 }finally{
  // Only owned test children. Failure cleanup of the test is never SDK proof.
  for(const child of Object.values(children))if(child.exitCode===null)child.stdin.write('EXIT\n');
  await Promise.all(pending);
 }
}
(async()=>{
 const cases=[];for(const pair of [['A'],['B'],['A','B']])cases.push(await test(pair));
 cases.push(await test(['A'],{channelMissing:true}));cases.push(await test(['B'],{channelTimeout:true}));
 for(const option of ['wrongRun','wrongSide','wrongPid','wrongPhase','duplicate'])cases.push(await test(['A'],{[option]:true}));
 console.log(JSON.stringify({scope:'REAL_LOCAL_CHILD_PROTOCOL_NOT_ROS_SDK',cases},null,2));
})().catch(e=>{console.error(e.stack);process.exitCode=1;});
