'use strict';
// Optional bounded debugger support, within an already-accounted ROS window.
const fs=require('node:fs'),path=require('node:path');
const {createBoundedSink}=require('./bounded-output.cjs');
function createRmwDebugger(c,directory,{spawn=require('node:child_process').spawn,onFailure=()=>{}}={}){
 const states={};
 const enabled=c.scenario==='ROS2_DSOFTBUS'&&c.rmw_debugger;
 function start(side,pid){
  if(!enabled)return;
  if(!['A','B'].includes(side)||!Number.isSafeInteger(pid)||pid<1||states[side])throw Error('RMW_DEBUGGER_PID_IDENTITY');
  states[side]={run:c.run,side,pid,closed:false,overflow:false,error:null};
 }
 function launch(side){
  const state=states[side],pid=state.pid;
  if(state.promise)return;
  const argv=state.argv=[path.join(path.resolve(directory,'../..'),'executor','rmw_debugger.py'),'--window',directory,'--side',side,'--pid',String(pid)];
  const logs={stdout:[],stderr:[]};
  state.promise=new Promise(resolve=>{
   const child=spawn('python',argv,{shell:false,windowsHide:true});state.local_pid=child.pid;
   let force,drain,settled=false;
   const report=reason=>{state.error||=reason;onFailure(reason+'_'+side);};
   const complete=(code,signal,confirmed)=>{if(settled)return;settled=true;clearTimeout(deadline);clearTimeout(force);clearTimeout(drain);clearInterval(watch);
    state.closed=confirmed;state.code=code;state.signal=signal;
    for(const stream of ['stdout','stderr'])fs.writeFileSync(path.join(directory,side,'debugger-manager.'+stream),Buffer.concat(logs[stream]),{flag:'wx'});
    if(!confirmed||code!==0||signal||state.overflow)report('DEBUGGER_MANAGER_FAILED');
    resolve(state);
   };
   const watch=setInterval(()=>{const file=path.join(directory,side,'debugger','RESULT.json');if(!fs.existsSync(file))return;
    try{const row=JSON.parse(fs.readFileSync(file));if(row.run!==c.run||row.side!==side||row.pid!==pid||row.support_closed!==true)report('DEBUGGER_RECOVERY_NOT_CONFIRMED');}catch{report('DEBUGGER_RESULT_INVALID');}
   },100);
   const deadline=setTimeout(()=>{report('DEBUGGER_MANAGER_DEADLINE');
    fs.writeFileSync(path.join(directory,side,'debugger-manager.termination.json'),JSON.stringify({run:c.run,side,target_local_pid:child.pid,reason:state.error,remote_termination:'NOT_PROVEN'}),{flag:'wx'});
    try{child.kill('SIGTERM');}catch{}
    force=setTimeout(()=>{try{child.kill('SIGKILL');}catch{}},2000);
    drain=setTimeout(()=>complete(null,'HOST_CAPTURE_UNCONFIRMED',false),4000);
   },(c.timeout_seconds+20)*1000);
   const sink=createBoundedSink({cap:65536,write:()=>{},overflow:()=>{state.overflow=true;}});
   for(const stream of ['stdout','stderr'])child[stream].on('data',data=>{const before=sink.state().retained;sink.feed(data);const count=sink.state().retained-before;if(count)logs[stream].push(data.subarray(0,count));});
   child.on('error',error=>{report('DEBUGGER_MANAGER_ERROR:'+String(error));});
   child.on('close',(code,signal)=>complete(code,signal,true));
  });
 }
 async function admission(){
  if(!enabled)return {pass:true,scope:'NOT_REQUESTED'};
  if(!states.A||!states.B)return {pass:false,error:'DEBUGGER_PID_NOT_OBSERVED'};
  for(const side of ['A','B'])launch(side);
  const end=Date.now()+40000,result={};
  while(Date.now()<end){
   for(const side of ['A','B'])if(!result[side]){
    const state=states[side];if(!state)return {pass:false,error:'DEBUGGER_PID_NOT_OBSERVED'};
    const resultFile=path.join(directory,side,'debugger','RESULT.json');
    if(state.error||state.closed&&(state.code!==0||state.signal||state.overflow))return {pass:false,error:'DEBUGGER_MANAGER_FAILED'};
    if(fs.existsSync(resultFile)){
     const row=JSON.parse(fs.readFileSync(resultFile,'utf8'));
     if(row.run!==c.run||row.side!==side||row.pid!==state.pid||row.support_closed!==true)return {pass:false,error:'DEBUGGER_RECOVERY_NOT_CONFIRMED'};
    }
    for(const name of ['READY','UNAVAILABLE']){
     const file=path.join(directory,side,'debugger',name+'.json');if(!fs.existsSync(file))continue;
     const row=JSON.parse(fs.readFileSync(file,'utf8'));
     if(row.run!==c.run||row.side!==side||row.pid!==state.pid){state.error='DEBUGGER_RECEIPT_IDENTITY';onFailure(state.error+'_'+side);return {pass:false,error:state.error};}
     if(name==='UNAVAILABLE'&&(row.support_closed!==true||row.inferior_resumed_or_absent!==true))return {pass:false,error:'DEBUGGER_UNAVAILABLE_RECOVERY_NOT_CONFIRMED'};
     result[side]={status:name,evidence:file};
    }
    if(state.closed&&!result[side])return {pass:false,error:'DEBUGGER_MANAGER_CLOSED_WITHOUT_ADMISSION'};
   }
   if(result.A&&result.B)return {pass:true,sides:result,scope:'DIAGNOSTIC_READY_OR_CONFIRMED_UNAVAILABLE_NOT_NATIVE_PASS'};
   await new Promise(resolve=>setTimeout(resolve,50));
  }
  return {pass:false,error:'DEBUGGER_READY_DEADLINE'};
 }
 async function finish(){
  if(!enabled)return {pass:true,scope:'NOT_REQUESTED'};
  const result={};
  for(const side of ['A','B']){
   const state=states[side];if(!state?.promise){result[side]={pass:false,error:'NOT_STARTED'};continue;}
   let timer;const done=await Promise.race([state.promise,new Promise(resolve=>{timer=setTimeout(()=>resolve(null),20000);})]);clearTimeout(timer);
   const file=path.join(directory,side,'debugger','RESULT.json');let row;
   try{row=JSON.parse(fs.readFileSync(file,'utf8'));}catch{}
   result[side]={...done,pass:!!done&&done.closed&&done.code===0&&!done.signal&&!done.overflow&&!done.error&&row?.run===c.run&&row?.side===side&&row?.pid===state.pid&&row?.support_closed===true,diagnostic:row||null};
  }
  fs.writeFileSync(path.join(directory,'debugger-summary.json'),JSON.stringify(result,null,2),{flag:'wx'});
  return {pass:result.A.pass&&result.B.pass,sides:result};
 }
 return {start,admission,finish};
}
module.exports={createRmwDebugger};
