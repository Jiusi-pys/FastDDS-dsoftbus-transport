'use strict';
const dgram=require('node:dgram'),crypto=require('node:crypto');
const {spawn}=require('node:child_process');
(async()=>{
 const [exe,test]=process.argv.slice(2);if(!exe||!test)throw Error('exe and test required');let base;
 for(let n=0;n<8&&!base;++n){const candidate=24000+crypto.randomInt(16000),held=[];try{
  for(const offset of [0,2,10,12]){const socket=dgram.createSocket('udp4');held.push(socket);await new Promise((r,j)=>{socket.once('error',j);socket.bind(candidate+offset,'127.0.0.1',r);});}base=candidate;
 }catch{}finally{for(const socket of held)try{socket.close();}catch{}}}
 if(!base)throw Error('no free reference port group');
 const child=spawn(exe,[test,String(base)],{shell:false,windowsHide:true,stdio:'inherit',env:{...process.env,SKIP_DEFAULT_XML:'1'}});
 child.on('error',e=>{console.error(e);process.exitCode=1;});child.on('close',(code,signal)=>{console.log(JSON.stringify({test,base,exit:code,signal,scope:'REAL_DDS_HOST_CONTRACT_ONLY'}));process.exitCode=code===0&&!signal?0:1;});
})().catch(e=>{console.error(e);process.exitCode=1;});
