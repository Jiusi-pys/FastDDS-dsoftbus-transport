'use strict';
// Live V4 capture and, for ROS, formal load identity complete before release.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {createBoundedSink,createLines}=require('./bounded-output.cjs');
const LOAD_FILES=['load-status.txt','load-exe.txt','load-cwd.txt','load-cmdline.txt','load-environ.txt','load-stat-before.txt','load-stat-after.txt','load-uptime.txt','load-maps.txt'];
function createLoadObserver({config,directory,spawn=require('node:child_process').spawn,record=()=>{},hostTimeoutMs=12000,drainMs=250,onRosReady=()=>{}}){
 const configured214=['ROS2_DSOFTBUS','DDS214_LAYER_BASELINE'].includes(config.scenario);
 const states={},ended=new Set(),inputErrors={},rosPids={};
 let loadClosureUnconfirmed=false; // Pair-wide latch; late close never upgrades it.
 const digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 function boundedChild(executable,args,side,label){
  return new Promise(resolve=>{
   let child,closed=false,finished=false,timer,drainTimer,forceTimer,error=null,exitObserved=null,termination=null;
   const output={stdout:0,stderr:0,retained:0,overflow:false};
   const retained={stdout:[],stderr:[]};
   const save=(suffix,value)=>{try{fs.writeFileSync(path.join(directory,side,`load-pre-release-${label}${suffix}.json`),JSON.stringify(value,null,2),{flag:'wx'});return null;}catch(e){return String(e);}};
   const onData=stream=>bytes=>{output[stream]+=bytes.length;
    const n=Math.min(bytes.length,Math.max(0,8192-output.retained));if(n)retained[stream].push(bytes.subarray(0,n));output.retained+=n;
    if(output.stdout+output.stderr>8192)output.overflow=true;};
   const finish=(code,signal)=>{if(finished)return;finished=true;clearTimeout(timer);clearTimeout(drainTimer);clearTimeout(forceTimer);
    if(child&&!closed)loadClosureUnconfirmed=true;
    const result={run:config.run,side,label,argv:[executable,...args],code,signal,closed,exit_observed:exitObserved,
      collection_cleanup:closed?'CHILD_STREAMS_CLOSED':'CHILD_STREAMS_UNCONFIRMED',termination,error,output,
      output_text:{stdout:Buffer.concat(retained.stdout).toString('utf8'),stderr:Buffer.concat(retained.stderr).toString('utf8')},
      pass:closed&&!error&&!signal&&code===0&&!output.overflow};
    const receiptError=save('',result);if(receiptError){result.receipt_error=receiptError;result.pass=false;result.error||='LOAD_CHILD_RECEIPT_WRITE_FAILED';}
    if(!closed){child?.unref?.();child?.stdout?.unref?.();child?.stderr?.unref?.();}
    resolve(result);};
   try{
    child=spawn(executable,args,{shell:false,windowsHide:true});
    child.stdout?.on('data',onData('stdout'));child.stderr?.on('data',onData('stderr'));
    child.on('error',e=>{error||=String(e);});
    child.on('exit',(code,signal)=>{exitObserved={code,signal,host_monotonic_ns:String(process.hrtime.bigint())};
      if(!finished&&!drainTimer)drainTimer=setTimeout(()=>{error||='EXIT_DRAIN_INCOMPLETE';finish(code,signal);},drainMs);});
    child.on('close',(code,signal)=>{closed=true;
      if(finished){save('-late-close',{run:config.run,side,label,code,signal,original_result:'UNCONFIRMED_NOT_UPGRADED'});return;}
      finish(code,signal);});
    timer=setTimeout(()=>{error||='HOST_LOAD_SUBCOMMAND_TIMEOUT';
      termination={run:config.run,side,label,requester:'load_pre_release_deadline',reason:error,target_host_pid:child.pid??null,deadline_ms:hostTimeoutMs,requested_signal:'SIGTERM',kill_result:null};
      const requestError=save('-termination-request',termination);if(requestError)termination.request_receipt_error=requestError;
      try{termination.kill_result=Boolean(child.kill('SIGTERM'));}catch(e){termination.kill_error=String(e);}
      const resultError=save('-termination-result',termination);if(resultError)termination.result_receipt_error=resultError;
      if(!drainTimer)drainTimer=setTimeout(()=>{
       if(closed||finished)return;
       termination.escalation_signal='SIGKILL';
       const escalationError=save('-escalation-request',termination);if(escalationError)termination.escalation_receipt_error=escalationError;
       try{termination.escalation_result=Boolean(child.kill('SIGKILL'));}catch(e){termination.escalation_error=String(e);}
       const escalationResultError=save('-escalation-result',termination);if(escalationResultError)termination.escalation_result_receipt_error=escalationResultError;
       forceTimer=setTimeout(()=>finish(null,null),drainMs);
      },drainMs);
    },hostTimeoutMs);
   }catch(e){error=String(e);finish(null,null);}
  });
 }
 async function formalRosCapture(side,collector){
  const root=path.join(directory,side,'load');
  try{
  fs.mkdirSync(root);fs.copyFileSync(path.join(directory,side,'load-collector.json'),path.join(root,'load-collector.json'),fs.constants.COPYFILE_EXCL);
  const transfer=[];
  // At most three bounded transfers per side at once; both sides may progress.
  for(let offset=0;offset<LOAD_FILES.length;offset+=3){
   if(loadClosureUnconfirmed)break;
   const batch=await Promise.all(LOAD_FILES.slice(offset,offset+3).map(async name=>{
    const target=path.join(root,name),args=['-t',config.targets[side],'file','recv',config.remote[side]+'/'+name,target];
    const child=await boundedChild(config.hdc_path,args,side,'recv-'+name.replaceAll('.','-'));
    let present=false,hash=null,fileError=null;
    // Await every started child in this batch, even if a sibling's file fails.
    // Do not read a destination that an unclosed child may still be writing.
    if(child.closed)try{present=fs.statSync(target).isFile()&&fs.statSync(target).size>0;if(present)hash=digest(target);}catch(e){fileError=String(e);}
    return {name,pass:child.pass&&present,present,child_error:child.error||null,receipt_error:child.receipt_error||null,
      closed:child.closed,file_error:fileError,sha256:hash};
   }));transfer.push(...batch);
  }
  const analysisPath=path.join(directory,side,'load-pre-release-analysis.json');
  const formal=path.join(path.resolve(directory,'../..'),'executor','load_origin.py');
  const analyzed=loadClosureUnconfirmed?null:await boundedChild('python',[formal,directory,side,'--output',analysisPath],side,'formal-analysis');
  let report={complete:false,errors:['ANALYSIS_NOT_CONFIRMED']},reportHash=null;
  if(analyzed?.closed)try{const bytes=fs.readFileSync(analysisPath);report=JSON.parse(bytes.toString('utf8'));reportHash=crypto.createHash('sha256').update(bytes).digest('hex');}catch(e){report={complete:false,errors:['ANALYSIS_REPORT_MISSING_OR_INVALID:'+String(e)]};}
  const pass=!loadClosureUnconfirmed&&transfer.length===LOAD_FILES.length&&transfer.every(x=>x.pass&&!x.file_error)&&analyzed?.pass===true&&report.complete===true&&report.run===config.run&&report.board===side&&report.pid===String(collector.target_pid)&&Array.isArray(report.errors)&&report.errors.length===0;
  const summary={run:config.run,side,host_collector_test:config.host_collector_test===true,transfer,formal_exit:analyzed?.code??null,
    formal_child_closed:analyzed?.closed??null,formal_analysis:analyzed?'STARTED':'NOT_RUN_UNCONFIRMED',
    collection_cleanup:loadClosureUnconfirmed?'UNCONFIRMED':'CHILD_STREAMS_CLOSED',
    formal_complete:report.complete===true,formal_report_sha256:reportHash,pass};
  fs.writeFileSync(path.join(directory,side,'load-pre-release-summary.json'),JSON.stringify(summary,null,2),{flag:'wx'});
  return {...collector,pass,error:pass?null:'ROS_FORMAL_LOAD_ADMISSION_FAILED',formal_capture:summary};
  }catch(e){
   const failure={run:config.run,side,reason:'ROS_LOAD_CAPTURE_IO_FAILURE',detail:String(e),host_collector_test:config.host_collector_test===true,pass:false};
   try{fs.writeFileSync(path.join(directory,side,'load-pre-release-error.json'),JSON.stringify(failure,null,2),{flag:'wx'});}catch(writeError){failure.receipt_error=String(writeError);}
   return {...collector,pass:false,error:failure.reason,formal_capture:failure};
  }
 }
 function start(side,pid){
  const root=config.remote[side];if(!Number.isSafeInteger(pid)||pid<1||root!==`/data/local/tmp/mdds_dds_${config.run}_${side}`)throw Error('Invalid observed identity');
  const command=`env -i PATH=/system/bin:/system/xbin timeout 10 /system/bin/sh ${root}/capture-load.sh ${config.run} ${pid} ${root} ${side}${['INSTALLED_RUNTIME','ROS2_DSOFTBUS','DDS214_LAYER_BASELINE'].includes(config.scenario)?' '+config.main_member:''}; rc=$?; echo LOAD_OBSERVER_REMOTE_EXIT=$rc`;
  const state=states[side]={run:config.run,side,target_pid:pid,pid_source:'DDS_MAIN_RUN_SIDE_BOUND',command,pass:false};
  const dir=path.join(directory,side);fs.mkdirSync(dir,{recursive:true});
  fs.writeFileSync(path.join(dir,'load-collector.command.json'),JSON.stringify([config.hdc_path,'-t',config.targets[side],'shell',command]),{flag:'wx'});
  const stdout=fs.openSync(path.join(dir,'load-collector.stdout.log'),'wx'),stderr=fs.openSync(path.join(dir,'load-collector.stderr.log'),'wx');
  state.promise=new Promise(resolve=>{
   let child,timer,drainTimer,settled=false;const exits=[];
   const sink=createBoundedSink({cap:65536,write:()=>{},overflow:()=>{state.error||='OBSERVER_OUTPUT_OVERFLOW';}});
   const lines=createLines(t=>{const m=/^LOAD_OBSERVER_REMOTE_EXIT=([0-9]+)$/.exec(t);if(m){if(exits.length<2)exits.push(Number(m[1]));else state.error||='DUPLICATE_REMOTE_EXIT';}},r=>{state.error||=r;});
   const complete=(code,signal,confirmed)=>{
    if(settled)return;lines.end(confirmed);settled=true;clearTimeout(timer);clearTimeout(drainTimer);
    if(exits.length!==1)state.error||='REMOTE_EXIT_MARKER_MISSING_OR_DUPLICATE';
    if(signal)state.error||='COLLECTOR_CANCELLED';
    Object.assign(state,{hdcExit:code,signal,collectorClosed:confirmed,collectionCleanup:confirmed?'HDC_STREAMS_CLOSED':'UNCONFIRMED',remoteExit:exits.length===1?exits[0]:null,remoteExitCount:exits.length,output_bytes:sink.state(),pass:confirmed&&!state.error&&code===0&&!signal&&exits.length===1&&exits[0]===0});
    if(!confirmed){child?.unref?.();child?.stdout?.unref?.();child?.stderr?.unref?.();}
    state.terminationOrigin=state.termination?'LOCAL_REQUEST_RECORDED':signal?'EXTERNAL_OR_UNOBSERVED_ORIGIN':'NONE_OBSERVED';const out={...state};delete out.promise;fs.writeFileSync(path.join(dir,'load-collector.json'),JSON.stringify(out,null,2),{flag:'wx'});fs.closeSync(stdout);fs.closeSync(stderr);record({event:'LOAD_COLLECTOR_END',side,pass:state.pass,target_pid:pid,collectionCleanup:state.collectionCleanup});resolve(out);
   };
   const data=(fd,bytes,parse)=>{if(settled){state.late_bytes=(state.late_bytes||0)+bytes.length;return;}const before=sink.state().retained;sink.feed(bytes);const n=sink.state().retained-before;if(n)fs.writeSync(fd,bytes.subarray(0,n));if(parse)lines(bytes);};
   try{
    child=spawn(config.hdc_path,['-t',config.targets[side],'shell',command],{shell:false,windowsHide:true});state.collectorPid=child.pid??null;
    child.stdout.on('data',b=>data(stdout,b,true));child.stderr.on('data',b=>data(stderr,b,false));
    child.on('error',e=>{state.error||=String(e);});child.on('close',(code,signal)=>{if(!settled)complete(code,signal,true);else fs.writeFileSync(path.join(dir,'load-collector.late-close.json'),JSON.stringify({code,signal,late_bytes:state.late_bytes||0,original_result:'UNCONFIRMED_NOT_UPGRADED'}),{flag:'wx'});});
    child.on('exit',(code,signal)=>{if(settled)return;state.exitObserved={code,signal,observer_pid:process.pid,host_monotonic_ns:String(process.hrtime.bigint())};clearTimeout(timer);if(!drainTimer)drainTimer=setTimeout(()=>{state.error||='EXIT_DRAIN_INCOMPLETE';complete(code,signal,false);},drainMs);});
    timer=setTimeout(()=>{state.error||='HOST_OBSERVER_TIMEOUT';state.termination={requester:'load_observer_deadline',reason:'HOST_OBSERVER_TIMEOUT',target_pid:child.pid??null,deadline_ms:hostTimeoutMs,observer_pid:process.pid,host_monotonic_ns:String(process.hrtime.bigint()),signal_requested:'SIGTERM',result:null};try{fs.writeFileSync(path.join(dir,'load-termination-request.json'),JSON.stringify({run:config.run,side,cycle:0,phase:'INITIAL_LIVE_CAPTURE',...state.termination},null,2),{flag:'wx'});state.termination.request_persisted_before_kill=true;}catch(e){state.termination.receipt_error=String(e);}drainTimer=setTimeout(()=>complete(null,null,false),drainMs);try{state.killRequested=Boolean(child.kill('SIGTERM'));state.termination.result=state.killRequested;}catch(e){state.kill_error=String(e);}try{fs.writeFileSync(path.join(dir,'load-termination-result.json'),JSON.stringify({run:config.run,side,...state.termination},null,2),{flag:'wx'});}catch(e){state.termination.result_receipt_error=String(e);}},hostTimeoutMs);
   }catch(e){state.error=String(e);complete(null,null,true);}
  });record({event:'LOAD_COLLECTOR_BEGIN',side,target_pid:pid,command});
 }
 const parsers=Object.fromEntries(['A','B'].map(side=>[side,createLines(line=>{
  if(config.scenario==='DDS214_LAYER_BASELINE'){
   const main=/^DDS214_MAIN scenario=DDS214_LAYER_BASELINE run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) mode=normal$/.exec(line);
   if(main){if(main[1]!==config.run||main[2]!==side||rosPids[side])inputErrors[side]||='DDS214_MAIN_IDENTITY';else rosPids[side]=Number(main[3]);return;}
   const ready=/^DDS214_STAGE run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) stage=MATCH$/.exec(line);
   if(ready){if(ended.has(side)||inputErrors[side]||ready[1]!==config.run||ready[2]!==side||Number(ready[3])!==rosPids[side]||states[side])inputErrors[side]||='DDS214_CAPTURE_IDENTITY';else start(side,Number(ready[3]));}return;
  }
  if(config.scenario==='ROS2_DSOFTBUS'){
   const main=/^ROS_MAIN scenario=ROS2_DSOFTBUS run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) mode=dual$/.exec(line);
   if(main){if(main[1]!==config.run||main[2]!==side||rosPids[side])inputErrors[side]||='ROS_MAIN_IDENTITY';else rosPids[side]=Number(main[3]);return;}
   const ready=/^ROS_STAGE run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) stage=RELIABLE_READY$/.exec(line);
   if(ready){if(ended.has(side)||ready[1]!==config.run||ready[2]!==side||Number(ready[3])!==rosPids[side]||states[side]){inputErrors[side]||='ROS_LOAD_PID_OR_STAGE';return;}start(side,rosPids[side]);states[side].pid_source='ROS_MAIN_AND_RELIABLE_READY_BOUND';onRosReady(side,rosPids[side]);}
   return;
  }
  const m=new RegExp('^'+(config.main_prefix||'DDS_MAIN')+' run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) mode='+(config.main_mode||'dds')+' ').exec(line);if(m){if(ended.has(side)){inputErrors[side]||='PID_OBSERVED_AFTER_STREAM_END';return;}if(m[1]!==config.run||m[2]!==side)throw Error('Process identity mismatch');if(!states[side])start(side,Number(m[3]));else states[side].error='MULTIPLE_NATIVE_PIDS';}
 },reason=>{inputErrors[side]||=reason;})]));
 let admissionTask;
 async function admission(){if(!admissionTask)admissionTask=(async()=>{const pairs=await Promise.all(['A','B'].map(async side=>{
   let value=states[side]?await states[side].promise:{pass:false,error:'NO_NATIVE_PID_RECORD'};
   if(states[side]?.error||inputErrors[side])value={...value,pass:false,error:states[side]?.error||inputErrors[side]};
   if(configured214&&value.collectorClosed===false)loadClosureUnconfirmed=true;
   if(configured214&&value.pass&&!loadClosureUnconfirmed){try{value=await formalRosCapture(side,value);}catch(e){value={...value,pass:false,error:'ROS_LOAD_CAPTURE_UNEXPECTED',detail:String(e)};}}
   return [side,value];
  }));if(configured214)for(const pair of pairs){const value=pair[1];pair[1]={...value,captureCollectorClosed:value.collectorClosed,
     loadChainClosed:!loadClosureUnconfirmed,collectorClosed:value.collectorClosed===true&&!loadClosureUnconfirmed,
     collectionCleanup:loadClosureUnconfirmed?'UNCONFIRMED':value.collectionCleanup,
     pass:value.pass&&!loadClosureUnconfirmed};}
  return Object.fromEntries(pairs);})();return admissionTask;}
 return {output(side,stream,data){if(stream==='stdout')parsers[side](data);},end(side,confirmed){ended.add(side);parsers[side].end(confirmed);},admission,async finish(){const result=await admission();
  try{fs.writeFileSync(path.join(directory,'load-collectors.json'),JSON.stringify(result,null,2),{flag:'wx'});}
  catch(e){for(const side of ['A','B'])result[side]={...result[side],pass:false,receipt_error:'LOAD_COLLECTORS_RECEIPT_WRITE_FAILED:'+String(e)};}
  return result;}};
}
module.exports={createLoadObserver};
