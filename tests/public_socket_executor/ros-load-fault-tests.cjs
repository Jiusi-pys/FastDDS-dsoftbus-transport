'use strict';
// Faults target the real observer/controller and local OS children, never HDC.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const os=require('node:os');
const path=require('node:path');
const cp=require('node:child_process');
const {createLoadObserver}=require('../../scripts/public_socket/observe-native.cjs');
const {controller}=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');

const run='1234567890abcdef';
const modes=['read-error','receipt-write','timeout','held-pipe','analysis-held'];
async function child(mode){
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'ros-load-fault-'));
 const config={scenario:'ROS2_DSOFTBUS',run,main_member:'ordinary_ros2_transport_consumer',hdc_path:'LOCAL_ONLY_NEVER_HDC',
  targets:{A:'LOCAL_A',B:'LOCAL_B'},remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`}};
 let injected=false;const recvCounts={LOCAL_A:0,LOCAL_B:0};let analysisCalls=0;
 const originalWrite=fs.writeFileSync,originalRead=fs.readFileSync;
 const localSpawn=(_exe,args)=>{
  if(args[2]==='shell')return cp.spawn(process.execPath,['-e',"process.stdout.write('LOAD_OBSERVER_REMOTE_EXIT=0\\n')"],{windowsHide:true});
  if(args[2]==='file'&&args[3]==='recv'){
   recvCounts[args[1]]++;
   const dest=args[5];
   if(mode==='analysis-held')originalWrite(dest,'local fault-test bytes, not a valid capture');
   if(args[1]==='LOCAL_A'&&dest.endsWith('load-status.txt')){
    injected=true;
    if(mode==='read-error')originalWrite(dest,'actual-local-bytes');
    if(mode==='timeout')return cp.spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{windowsHide:true});
    if(mode==='held-pipe'){
     // Windows closes this OS child's pipes promptly on termination. Inject
     // only an early `exit` notification while the real child remains alive;
     // its later real `close` must be recorded without upgrading the failure.
     const held=cp.spawn(process.execPath,['-e','setTimeout(()=>{},450)'],{windowsHide:true});
     setTimeout(()=>held.emit('exit',0,null),5);
     return held;
    }
   }
   return cp.spawn(process.execPath,['-e',mode==='held-pipe'?'setTimeout(()=>{},150)':'process.exit(0)'],{windowsHide:true});
  }
  if(_exe==='python'){analysisCalls++;
   if(mode==='analysis-held'&&args[2]==='A'){const held=cp.spawn(process.execPath,['-e','setTimeout(()=>{},450)'],{windowsHide:true});setTimeout(()=>held.emit('exit',0,null),5);return held;}
   return cp.spawn(process.execPath,['-e','process.exit(1)'],{windowsHide:true});}
  throw Error('Unexpected local-only command');
 };
 if(mode==='read-error')fs.readFileSync=function(file,...rest){
  if(String(file).endsWith('load-status.txt')&&String(file).includes(path.join(directory,'A','load')))throw Error('INJECTED_LOCAL_READ_ERROR');
  return originalRead.call(fs,file,...rest);
 };
 if(mode==='receipt-write')fs.writeFileSync=function(file,...rest){
  if(String(file).endsWith('load-pre-release-recv-load-status-txt.json')&&String(file).includes(path.join(directory,'A')))throw Error('INJECTED_LOCAL_RECEIPT_WRITE_ERROR');
  return originalWrite.call(fs,file,...rest);
 };
 try{
  const observer=createLoadObserver({config,directory,spawn:localSpawn,hostTimeoutMs:['held-pipe','analysis-held'].includes(mode)?1500:70,drainMs:50});
  const released=[],control=controller(config,async(s,p)=>released.push(s+p),observer.admission);
  for(const side of ['A','B']){
   const pid=side==='A'?101:102;
   for(const row of [
    `ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${side} pid=${pid} mode=dual`,
    `ROS_LISTENER_READY run=${run} side=${side} pid=${pid} listener=7 fake_sdk=0`,
    `ROS_STAGE run=${run} side=${side} pid=${pid} stage=LISTENER_READY`,
    `ROS_PUBLISHER run=${run} side=${side} gid=${(side==='A'?'a':'b').repeat(32)} intra_process=0 phase=reliable topic=/fdds_${run}/reliable/from_${side}`,
    `ROS_GRAPH run=${run} side=${side} peer=/fdds_${run}/transport_${side==='A'?'B':'A'} visible=1 subscription_count=1 phase=reliable`,
    `ROS_STAGE run=${run} side=${side} pid=${pid} stage=RELIABLE_READY`]){
    observer.output(side,'stdout',Buffer.from(row+'\n'));control.line(side,row);
   }
  }
  const result=await control.finish(),loads=await observer.admission();
  assert.equal(injected,true);
  assert.equal(result.reported_first_host,'ROS_LOAD_CAPTURE_FAILED');
  assert.equal(loads.A.pass,false);assert.equal(released.some(x=>x.endsWith('RELIABLE_READY')),false);
  if(mode==='timeout'){
   const receipt=JSON.parse(originalRead(path.join(directory,'A','load-pre-release-recv-load-status-txt.json'),'utf8'));
   assert.equal(receipt.pass,false);assert.equal(receipt.termination?.requested_signal,'SIGTERM');
   assert.equal(typeof receipt.termination?.kill_result,'boolean');
   assert.equal(typeof receipt.closed,'boolean');
  }
  if(mode==='held-pipe'){
   const base=path.join(directory,'A','load-pre-release-recv-load-status-txt');
   const receipt=JSON.parse(originalRead(base+'.json','utf8'));
   assert.equal(receipt.closed,false);assert.equal(receipt.collection_cleanup,'CHILD_STREAMS_UNCONFIRMED');
   assert.equal(loads.A.collectorClosed,false,'aggregate must include unclosed recv');
   assert.equal(loads.A.collectionCleanup,'UNCONFIRMED');
   assert.equal(loads.B.loadChainClosed,false,'pair must latch unknown close');
   assert.equal(analysisCalls,0,'no analysis after unknown close');
   assert(recvCounts.LOCAL_A<=3&&recvCounts.LOCAL_B<=3,'no later recv batches');
   await new Promise(resolve=>setTimeout(resolve,700));
   const late=JSON.parse(originalRead(base+'-late-close.json','utf8'));
   assert.equal(late.original_result,'UNCONFIRMED_NOT_UPGRADED');
   assert.equal((await observer.admission()).A.collectorClosed,false,'late close cannot heal aggregate');
  }
  if(mode==='analysis-held'){
   assert.equal(loads.A.formal_capture.formal_child_closed,false);
   assert.equal(loads.A.captureCollectorClosed,true);
   assert.equal(loads.A.collectorClosed,false);
   assert.equal(loads.A.collectionCleanup,'UNCONFIRMED');
   assert.equal(loads.B.loadChainClosed,false);
   await new Promise(resolve=>setTimeout(resolve,700));
   assert.equal((await observer.admission()).A.collectorClosed,false);
  }
  console.log('PASS '+mode+' classified as bounded ROS_LOAD_CAPTURE_FAILED');
 }finally{fs.writeFileSync=originalWrite;fs.readFileSync=originalRead;}
}
if(process.argv[2]==='--child')child(process.argv[3]).catch(e=>{console.error(e);process.exitCode=1;});
else{
 let failed=false;
 for(const mode of modes){const p=cp.spawnSync(process.execPath,[__filename,'--child',mode],{encoding:'utf8',timeout:3500});
  if(p.status!==0){failed=true;console.error(mode,'exit=',p.status,'stderr=',p.stderr.trim());}
  else console.log(p.stdout.trim());
 }
 process.exitCode=failed?1:0;
}
