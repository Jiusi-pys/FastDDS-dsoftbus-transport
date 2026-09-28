'use strict';
// Live V4 capture completes before first performance release. No hot-loop HDC.
const fs=require('node:fs'),path=require('node:path');
const {createBoundedSink,createLines}=require('./bounded-output.cjs');
function createLoadObserver({config,directory,spawn=require('node:child_process').spawn,record=()=>{},hostTimeoutMs=12000,drainMs=250}){
 const states={},ended=new Set(),inputErrors={};
 function start(side,pid){
  const root=config.remote[side];if(!Number.isSafeInteger(pid)||pid<1||root!==`/data/local/tmp/mdds_dds_${config.run}_${side}`)throw Error('Invalid observed identity');
  const command=`env -i PATH=/system/bin:/system/xbin timeout 10 /system/bin/sh ${root}/capture-load.sh ${config.run} ${pid} ${root} ${side}; rc=$?; echo LOAD_OBSERVER_REMOTE_EXIT=$rc`;
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
 const parsers=Object.fromEntries(['A','B'].map(side=>[side,createLines(line=>{const m=/^DDS_MAIN run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) mode=(dds) /.exec(line);if(m){if(ended.has(side)){inputErrors[side]||='PID_OBSERVED_AFTER_STREAM_END';return;}if(m[1]!==config.run||m[2]!==side||m[4]!=='dds')throw Error('Probe identity mismatch');if(!states[side])start(side,Number(m[3]));else states[side].error='MULTIPLE_NATIVE_PIDS';}},reason=>{inputErrors[side]||=reason;})]));
 async function admission(){const result={};for(const side of ['A','B']){result[side]=states[side]?await states[side].promise:{pass:false,error:'NO_NATIVE_PID_RECORD'};if(states[side]?.error||inputErrors[side])result[side]={...result[side],pass:false,error:states[side]?.error||inputErrors[side]};}return result;}
 return {output(side,stream,data){if(stream==='stdout')parsers[side](data);},end(side,confirmed){ended.add(side);parsers[side].end(confirmed);},admission,async finish(){const result=await admission();fs.writeFileSync(path.join(directory,'load-collectors.json'),JSON.stringify(result,null,2),{flag:'wx'});return result;}};
}
module.exports={createLoadObserver};
