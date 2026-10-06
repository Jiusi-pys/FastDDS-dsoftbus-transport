'use strict';
const {spawn}=require('node:child_process');
const assert=require('node:assert/strict');
const path=require('node:path');
const executable=path.resolve(process.argv[2]);
async function trial(close){return new Promise((resolve,reject)=>{
 const p=spawn(executable,['export_pipe'],{shell:false,stdio:['ignore','pipe','pipe']});
 let first=false,once=false,stderr='',bytes=0;const deadline=setTimeout(()=>{p.kill();reject(Error('LOCAL_EXPORT_DRAIN_TIMEOUT_NOT_SDK_CANCEL'));},5000);
 p.stdout.on('data',b=>{bytes+=b.length;first ||= b.toString().includes('value=-777');if(!once){once=true;if(close)p.stdout.destroy();else{p.stdout.pause();setTimeout(()=>p.stdout.resume(),100);}}});
 p.stderr.on('data',b=>stderr+=b.toString());p.on('error',reject);
 p.on('close',(code,signal)=>{clearTimeout(deadline);try{assert.equal(code,0);assert.equal(signal,null);assert(first);assert(stderr.includes('first=-777'));if(close)assert(stderr.includes('errors=1'));else assert(stderr.includes('ok=1'));resolve({mode:close?'collector_closed':'temporarily_not_draining',pass:true,bytes,first_retained:true,device_operations:0});}catch(e){reject(e);}});
});}
(async()=>console.log(JSON.stringify([await trial(false),await trial(true)],null,2)))().catch(e=>{console.error(e);process.exitCode=1;});
