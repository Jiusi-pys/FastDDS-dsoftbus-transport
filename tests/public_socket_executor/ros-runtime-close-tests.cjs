'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const run='1234567890abcdef';
const tick=()=>new Promise(r=>setImmediate(r));
function setup(){
 const commands=[];
 const ctl=controller({scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true},async(side,stage,meta)=>{commands.push({side,stage,...meta});},async()=>({A:{pass:true},B:{pass:true}}));
 for(const s of ['A','B'])ctl.line(s,`ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${s} pid=${s==='A'?101:102} mode=dual`);
 return {ctl,commands};
}
const ready=(s,changes='')=>`ROS_RUNTIME_CLOSE_READY schema=1 run=${run} side=${s} pid=${s==='A'?101:102} stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1${changes}`;
(async()=>{
 const positive=setup();
 const stage=async name=>{for(const s of ['A','B'])positive.ctl.line(s,`ROS_STAGE run=${run} side=${s} pid=${s==='A'?101:102} stage=${name}`);await tick();};
 for(const s of ['A','B'])positive.ctl.line(s,`ROS_LISTENER_READY run=${run} side=${s} pid=${s==='A'?101:102} listener=7 fake_sdk=0`);
 await stage('LISTENER_READY');
 const texts={};for(const s of ['A','B'])texts[s]=fs.readFileSync(path.join(__dirname,'../ros2_host/fixtures',s+'.stdout'),'utf8').replaceAll('42ba5e959d97d571',run).split(/\r?\n/);
 for(const phase of ['reliable','best_effort']){
  for(const s of ['A','B'])for(const row of texts[s])if(row.includes('phase='+phase)&&/^ROS_(PUBLISHER|GRAPH) /.test(row))positive.ctl.line(s,row);
  await stage(phase==='reliable'?'RELIABLE_READY':'BEST_EFFORT_READY');
  for(const s of ['A','B'])for(const row of texts[s])if(row.includes('phase='+phase)&&!/^ROS_(PUBLISHER|GRAPH) /.test(row))positive.ctl.line(s,row);
  await stage(phase==='reliable'?'RELIABLE_DONE':'BEST_EFFORT_DONE');
 }
 await stage('TEARDOWN_READY');
 positive.ctl.line('A',ready('A'));await tick();
 assert.equal(positive.commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,0);
 positive.ctl.line('B',ready('B'));await tick();
 assert.equal(positive.commands.filter(x=>x.stage==='RUNTIME_CLOSE'&&x.reason==='NORMAL_COMPLETION').length,2,'pair release before later DDS deletion results');
 for(const s of ['A','B']){
  positive.ctl.line(s,`ROS_DDS_DELETED run=${run} side=${s} pid=${s==='A'?101:102} dds_component_result=0 api_return=NOT_OBSERVED scope=FACTORY_POST_DELETE`);
  positive.ctl.line(s,`ROS_STAGE run=${run} side=${s} pid=${s==='A'?101:102} stage=DDS_DELETED`);
  positive.ctl.line(s,`ROS_CONTEXT_CLOSED run=${run} side=${s} result=0`);
 }
 assert.equal((await positive.ctl.finish()).pass,true);
 // Failure-side coordination is distinct from successful business release.
 const f=setup();f.ctl.stop('INJECTED_PRIMARY_FAILURE');await tick();
 f.ctl.line('A',ready('A'));await tick();
 assert.equal(f.commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,1);
 assert.equal(f.commands.find(x=>x.stage==='RUNTIME_CLOSE').reason,'WINDOW_FAILED');
 f.ctl.line('B',ready('B'));await tick();
 const result=await f.ctl.finish();assert.equal(result.pass,false);
 assert.equal(f.commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,2);
 assert.equal(result.reported_first_host,'INJECTED_PRIMARY_FAILURE');
 for(const mutation of [x=>x.replace('pid=101','pid=999'),x=>x.replace(run,'0'.repeat(16)),x=>x.replace('active_pumps=0','active_pumps=1'),x=>x+' pid=101']){
  const n=setup();n.ctl.stop('PRIMARY');await tick();n.ctl.line('A',mutation(ready('A')));await tick();
  assert.equal(n.commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,0);
  assert.equal((await n.ctl.finish()).pass,false);
 }
 const early=setup();early.ctl.line('A',ready('A'));await tick();
 assert.equal(early.commands.filter(x=>x.stage==='RUNTIME_CLOSE').length,0);
 assert.equal((await early.ctl.finish()).pass,false);
 console.log('PASS runtime-close failure controls and identity negatives');
})().catch(e=>{console.error(e);process.exitCode=1;});
