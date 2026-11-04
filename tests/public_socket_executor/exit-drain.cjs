'use strict';
const assert=require('node:assert/strict'),cp=require('node:child_process'),path=require('node:path');

const injectClose=process.argv.includes('--inject-close');
const {runRecordPair}=require('../../scripts/public_socket/record-pair.cjs');
const helper=path.join(__dirname,'local-child.cjs');
(async()=>{const closes=[],events=[];
 const result=await runRecordPair({executable:process.execPath,targets:{A:'LOCAL_A',B:'LOCAL_B'},commands:{A:'SIM_A',B:'SIM_B'},timeoutMs:3000,drainGraceMs:50,
  spawn(requested,args,options){assert.ok(requested===process.execPath);const child=cp.spawn(process.execPath,[helper,args[1]==='LOCAL_A'?'pipe':'pair-success'],options);
   if(!injectClose){closes.push(new Promise(r=>child.on('close',r)));return child;}
   // Windows did not retain the pipe in this fixture. Explicit delivery-delay
   // injection is separate from the underlying real OS process experiment.
   const proxy=new(require('node:events').EventEmitter)();Object.assign(proxy,{stdout:child.stdout,stderr:child.stderr,pid:child.pid,kill:s=>child.kill(s),unref:()=>child.unref()});child.on('error',e=>proxy.emit('error',e));child.on('exit',(c,s)=>proxy.emit('exit',c,s));child.on('close',(c,s)=>setTimeout(()=>proxy.emit('close',c,s),150));closes.push(new Promise(r=>proxy.on('close',r)));return proxy;
  },onTransition:e=>events.push(e)});
 await Promise.all(closes);assert.equal(result.timedOut,false);
 if(!injectClose){assert.equal(result.pass,true);console.log('PASS real local OS normal-close case; held-pipe gap NOT_REPRODUCED on this platform');return;}
 assert.equal(result.pass,false,'process exit is not bounded pipe completion');assert.equal(result.A.error,'EXIT_DRAIN_INCOMPLETE');assert.equal(result.A.closed,false,'as-of incomplete result must not be upgraded by late close');assert.ok(events.some(e=>e.event==='LATE_CLOSE'));console.log('PASS R5 real local children WITH EXPLICIT CLOSE-DELIVERY EVENT INJECTION; late close not upgraded');
})().catch(e=>{console.error(e);process.exitCode=1;});
