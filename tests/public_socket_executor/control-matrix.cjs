'use strict';
const assert=require('node:assert/strict'),cp=require('node:child_process'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {config}=require('./shared-cycle-tests.cjs');
const {createControlRelease,createController}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const {createLoadObserver}=require('../../scripts/public_socket/observe-native.cjs');
const helper=path.join(__dirname,'local-child.cjs');
const localConfig=()=>({...config(10),hdc_path:process.execPath,targets:{A:'LOCAL_A',B:'LOCAL_B'}});
const dir=()=>fs.mkdtempSync(path.join(os.tmpdir(),'mdds-local-control-'));
function localSpawn(mode,onData=()=>{}){return (exe,args,opts)=>{assert.equal(exe,process.execPath,'No real HDC/PATH fallback');const child=cp.spawn(process.execPath,[helper,typeof mode==='function'?mode():mode],opts);child.stdout.on('data',onData);return child;};}
async function run(){
 // R2: real child triggers first failure while controller waits for A release.
 let controller;const c=localConfig(),release=createControlRelease(c,dir(),{spawn:localSpawn('stop',()=>controller.stop('ORIGINAL_FAILURE')),timeoutMs:1000});
 controller=createController(c,release,async()=>({A:{pass:true},B:{pass:true}}));for(const s of ['A','B'])controller.line(s,`DDS_STAGE run=${c.run} side=${s} pid=${s==='A'?10:20} stage=LISTENER_READY`);
 const failed=await controller.finish();assert.equal(failed.first_failure,'ORIGINAL_FAILURE');assert.deepEqual(failed.issued,['ALISTENER_READY']);assert.equal(release.receipts[0].termination.first_failure,'ORIGINAL_FAILURE');assert.equal(release.receipts[0].termination.requester,'controller');
 // R3: control timeout has its own reason, independent of a whole-run timeout.
 const timeout=createControlRelease(c,dir(),{spawn:localSpawn('hang'),timeoutMs:60});await assert.rejects(timeout('B','C6_P_OLD_DELETED'));assert.equal(timeout.receipts[0].termination.reason,'PHASE_DEADLINE');assert.equal(timeout.receipts[0].pass,false);
 // R6: cancellation for retired command ID cannot target the new child.
 let mode='hang';const scoped=createControlRelease(c,dir(),{spawn:localSpawn(()=>mode),timeoutMs:1000});const old=scoped('A','C6_P_OLD_DELETED');setTimeout(()=>scoped.cancel({requester:'local_test',first_failure:'TEST_ABORT'}),25);await assert.rejects(old);const oldId=scoped.receipts[0].command_id;mode='control';const next=scoped('A','C6_Q_GAP_DONE');scoped.cancel({requester:'late_old_stage',first_failure:'OLD',command_id:oldId});await next;assert.equal(scoped.receipts[1].termination,null);assert.equal(scoped.receipts[1].pass,true);
 // R7: an empty exit0 is incomplete and cannot release the dependent side.
 const empty=createControlRelease(c,dir(),{spawn:localSpawn('empty'),timeoutMs:1000});const p=createController(c,empty,async()=>({A:{pass:true},B:{pass:true}}));for(const s of ['A','B'])p.line(s,`DDS_STAGE run=${c.run} side=${s} pid=${s==='A'?10:20} stage=LISTENER_READY`);const stopped=await p.finish();assert.equal(stopped.pass,false);assert.deepEqual(stopped.issued,['ALISTENER_READY']);assert.equal(empty.receipts[0].remote_exit_count,0);
 // R10: actual local load child returns no receipt. Cleanup cannot replace first failure.
 const observer=createLoadObserver({config:c,directory:dir(),spawn:localSpawn('empty')});for(const s of ['A','B'])observer.output(s,'stdout',Buffer.from(`DDS_MAIN run=${c.run} side=${s} pid=${s==='A'?10:20} mode=dds headers=LOCAL\n`));const loads=await observer.admission();assert.equal(loads.A.pass,false);assert.equal(loads.B.pass,false);const issued=[];const loadController=createController(c,async s=>issued.push(s),async()=>loads);for(const s of ['A','B'])loadController.line(s,`DDS_STAGE run=${c.run} side=${s} pid=${s==='A'?10:20} stage=LISTENER_READY`);await loadController.finish();loadController.stop('LATER_CLEANUP_SIGTERM');assert.equal((await loadController.finish()).first_failure,'LOAD_CAPTURE_FAILED');assert.deepEqual(issued,[]);
 // Real load collector's final return marker has no newline.
 const good=createLoadObserver({config:c,directory:dir(),spawn:localSpawn('load')});for(const s of ['A','B'])good.output(s,'stdout',Buffer.from(`DDS_MAIN run=${c.run} side=${s} pid=${s==='A'?10:20} mode=dds headers=LOCAL\n`));assert.ok(Object.values(await good.admission()).every(x=>x.pass));
 console.log('PASS R2/R3/R6/R7/R10 actual local OS children; synthetic control/load text only');
}
run().catch(e=>{console.error(e);process.exitCode=1;});
