'use strict';
// Actual supervisor, inert command boundary. No HDC, target, or native claim.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),os=require('node:os'),{EventEmitter}=require('node:events');
const source=path.resolve(__dirname,'../../scripts/public_socket/shared_reattach/executor');
const copy=fs.mkdtempSync(path.join(os.tmpdir(),'debug-supervision-'));
for(const name of ['rmw-debugger.cjs'])fs.copyFileSync(path.join(source,name),path.join(copy,name));
fs.copyFileSync(path.resolve(source,'../../bounded-output.cjs'),path.join(copy,'bounded-output.cjs'));
const {createRmwDebugger}=require(path.join(copy,'rmw-debugger.cjs'));
async function test(kind){
 const root=fs.mkdtempSync(path.join(os.tmpdir(),'debug-protocol-')),failures=[];
 const c={scenario:'ROS2_DSOFTBUS',run:'aaaaaaaaaaaaaaaa',timeout_seconds:300,rmw_debugger:{}};
 let count=0;
 const supervisor=createRmwDebugger(c,root,{onFailure:reason=>failures.push(reason),spawn:(exe,argv)=>{
  const child=new EventEmitter();child.stdout=new EventEmitter();child.stderr=new EventEmitter();child.pid=8000+count++;
  const side=argv[argv.indexOf('--side')+1],pid=Number(argv[argv.indexOf('--pid')+1]),dir=path.join(root,side,'debugger');fs.mkdirSync(dir,{recursive:true});
  const row={run:c.run,side,pid};
  fs.writeFileSync(path.join(dir,'READY.json'),JSON.stringify({...row,run:kind==='wrong-run'?'bbbbbbbbbbbbbbbb':c.run}));
  fs.writeFileSync(path.join(dir,'RESULT.json'),JSON.stringify({...row,status:kind==='blocked'?'DEBUGGER_RECOVERY_BLOCKED':'READY',support_closed:kind!=='blocked'}));
  setImmediate(()=>child.emit('close',kind==='blocked'||kind==='exit1'?1:0,null));
  return child;
 }});
 try{
  supervisor.start('A',101);supervisor.start('B',102);await new Promise(resolve=>setImmediate(resolve));
  assert.equal(count,0,'PID registration must not start debugger before load admission');
  const initial=await supervisor.admission();
  await new Promise(resolve=>setImmediate(resolve));
  const admission=await supervisor.admission(),finish=await supervisor.finish();
  assert.equal(count,2,'repeat admission never starts a second manager');
  assert.equal(admission.pass,kind==='complete');assert.equal(finish.pass,kind==='complete');
  if(kind==='blocked'||kind==='exit1')assert(failures.length,'runtime failure must stop controller');
  return {kind,admission,finish,failures,real_hdc:0};
 }finally{fs.rmSync(root,{recursive:true});}
}
(async()=>{try{const results=[];for(const kind of ['complete','blocked','exit1','wrong-run'])results.push(await test(kind));console.log(JSON.stringify({scope:'REAL_NODE_SUPERVISOR_INERT_CHILD_NOT_DEVICE',results}));}finally{fs.rmSync(copy,{recursive:true});}})().catch(error=>{console.error(error);process.exitCode=1;});
