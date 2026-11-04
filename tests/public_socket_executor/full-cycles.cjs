'use strict';
// Real local child streams + actual controller/collector. Synthetic DDS text only.
const assert=require('node:assert/strict'),cp=require('node:child_process'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {config,trial}=require('./shared-cycle-tests.cjs');
const {createController,createControlRelease}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const {runRecordPair}=require('../../scripts/public_socket/record-pair.cjs');
const {createLines}=require('../../scripts/public_socket/bounded-output.cjs');
const helper=path.join(__dirname,'local-child.cjs');
async function test(cycles,fail92=false){
 const rows={A:[],B:[]};assert.equal((await trial('',cycles,(s,t)=>rows[s].push(t))).pass,true);
 const directory=fs.mkdtempSync(path.join(os.tmpdir(),'mdds-local-cycles-')),files={},children={},ends=[],launches=[];
 for(const side of ['A','B']){const blocks=[];let block=[];for(const row of rows[side]){block.push(row);if(row.startsWith('DDS_STAGE ')){blocks.push(block);block=[];}}assert.equal(block.length,0);files[side]=path.join(directory,side+'.json');fs.writeFileSync(files[side],JSON.stringify(blocks));}
 const c={...config(cycles),hdc_path:process.execPath,targets:{A:'LOCAL_A',B:'LOCAL_B'}};let controlIndex=0;
 const commandRelease=fail92?createControlRelease(c,directory,{timeoutMs:500,spawn(exe,args,options){assert.equal(exe,process.execPath);return cp.spawn(exe,[helper,++controlIndex===92?'hang':'control'],options);}}):null;
 const release=async(s,t)=>{if(commandRelease)await commandRelease(s,t);await new Promise((resolve,reject)=>children[s].stdin.write(t+'\n',e=>e?reject(e):resolve()));};
 if(commandRelease)release.cancel=reason=>commandRelease.cancel(reason);
 const controller=createController(c,release,async()=>({A:{pass:true},B:{pass:true}}));
 const lines=Object.fromEntries(['A','B'].map(s=>[s,createLines(t=>controller.line(s,t),e=>controller.stop(e))]));
 const result=await runRecordPair({executable:process.execPath,targets:{A:'A',B:'B'},commands:{A:'LOCAL_A',B:'LOCAL_B'},timeoutMs:20000,drainGraceMs:250,
  spawn(executable,args,options){assert.equal(executable,process.execPath);const side=args[1];assert.ok(side==='A'||side==='B');const argv=[helper,'transcript',files[side],side];launches.push({executable,argv});return children[side]=cp.spawn(executable,argv,{...options,stdio:['pipe','pipe','pipe']});},
  onOutput:(s,stream,b)=>{if(stream==='stdout')lines[s](b);},onStreamEnd:(s,ok)=>{ends.push([s,ok]);lines[s].end(ok);controller.end(s,ok);},onTransition:e=>{if(e.event==='OUTPUT_ERROR'||e.event==='EXIT_DRAIN_INCOMPLETE')controller.stop(e.event);}});
 const phase=await controller.finish();assert.equal(result.timedOut,false);
 if(fail92){assert.equal(result.pass,false);assert.equal(phase.pass,false);assert.equal(phase.first_failure,'Error: CONTROL_WRITE_FAILED');assert.equal(phase.issued.length,92);assert.equal(phase.released.length,91);assert.equal(commandRelease.receipts[91].stage,'C6_P_OLD_DELETED');assert.equal(commandRelease.receipts[91].side,'B');assert.equal(commandRelease.receipts[91].termination.reason,'PHASE_DEADLINE');console.log('PASS R3 actual local control command92 timeout; whole timedOut=false; first failure preserved; no dependent release');return;}
 assert.equal(result.pass,true,JSON.stringify(result));assert.equal(phase.pass,true,JSON.stringify(phase));assert.equal(phase.released.length,16*cycles+6);assert.equal(phase.released[91],'BC6_P_OLD_DELETED');assert.deepEqual(ends.map(x=>x[1]),[true,true]);assert.equal(launches.length,2);
 console.log(JSON.stringify({case:'R1_R4',cycles,control_releases:phase.released.length,boundary92:phase.released[91],pass:true,scope:'LOCAL_SUBPROCESSES_SYNTHETIC_CONTROL_TEXT_ONLY',launches}));
}
(async()=>{await test(10);await test(100);await test(10,true);})().catch(e=>{console.error(e);process.exitCode=1;});
