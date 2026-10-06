'use strict';
// Actual two-child pipe/control execution; lifecycle text model, not ROS/SDK.
const assert=require('node:assert/strict'),{spawn}=require('node:child_process');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='aacceeff00112233';
async function test(failedSide){
 const children={},outputs={},commands=[],exits=[],started={};let ctl;
 const helper=`const side=process.argv[1],run=process.argv[2],rl=require('node:readline').createInterface({input:process.stdin});let cleanup=false;console.log('ROS_MAIN scenario=ROS2_DSOFTBUS run='+run+' side='+side+' pid='+process.pid+' mode=dual');rl.on('line',line=>{if(line==='FAIL'){console.log('Failed to delete participant');return;}if(line==='ABORT'){process.exit(9);}if(line==='ROS_CLEANUP schema=1 run='+run+' side='+side+' pid='+process.pid+' stage=CLEANUP reason=WINDOW_FAILED'&&!cleanup){cleanup=true;const record='ROS_RUNTIME_CLOSE_READY schema=1 run='+run+' side='+side+' pid='+process.pid+' stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1';process.stdout.write(record.slice(0,31));setImmediate(()=>process.stdout.write(record.slice(31)+'\\r\\n'));return;}if(cleanup&&line==='ROS_RUNTIME_CLOSE_RELEASE schema=1 run='+run+' side='+side+' pid='+process.pid+' stage=RUNTIME_CLOSE reason=WINDOW_FAILED'){console.log('CHILD_RESOURCE_CLEANUP side='+side);process.exit(4);}process.exit(3);});`;
 ctl=controller({scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true},async(side,stage,meta)=>{
  assert(['CLEANUP','RUNTIME_CLOSE'].includes(stage));commands.push({side,stage,...meta});
  children[side].stdin.write(`${stage==='CLEANUP'?'ROS_CLEANUP':'ROS_RUNTIME_CLOSE_RELEASE'} schema=1 run=${run} side=${side} pid=${meta.pid} stage=${stage} reason=${meta.reason}\n`);
 },async()=>{throw Error('NO_BUSINESS_ADMISSION_AFTER_FAILURE');});
 for(const side of ['A','B']){
  const child=children[side]=spawn(process.execPath,['-e',helper,side,run],{stdio:['pipe','pipe','pipe']});outputs[side]='';let tail='';
  exits.push(new Promise(resolve=>child.once('close',(code,signal)=>resolve({side,code,signal}))));
  child.stdout.on('data',b=>{outputs[side]+=b;tail+=b;for(;;){const i=tail.indexOf('\n');if(i<0)break;const row=tail.slice(0,i).replace(/\r$/,'');tail=tail.slice(i+1);if(row.startsWith('ROS_MAIN '))started[side]=true;ctl.line(side,row);}});
 }
 const until=async pred=>{const end=Date.now()+5000;while(!pred()&&Date.now()<end)await new Promise(r=>setTimeout(r,5));assert(pred(),'finite child deadline');};
 try{
  await until(()=>started.A&&started.B);children[failedSide].stdin.write('FAIL\n');
  await until(()=>Object.values(children).every(p=>p.exitCode!==null));
  const results=await Promise.all(exits),result=await ctl.finish();
  assert(results.every(p=>p.code===4&&!p.signal));assert.equal(result.pass,false);
  assert.equal(result.reported_first_host,'ROS_CLEANUP_FAILURE_'+failedSide);
  assert.equal(commands.length,4);assert(commands.every(x=>x.reason==='WINDOW_FAILED'));
  for(const side of ['A','B'])assert(outputs[side].includes('CHILD_RESOURCE_CLEANUP side='+side));
  return {failedSide,commands,exits:results,result};
 }finally{for(const p of Object.values(children))if(p.exitCode===null)p.stdin.write('ABORT\n');await Promise.all(exits);}
}
(async()=>{console.log(JSON.stringify({scope:'TWO_LOCAL_CHILDREN_NO_ROS_SDK',cases:[await test('A'),await test('B')]},null,2));})().catch(e=>{console.error(e);process.exitCode=1;});
