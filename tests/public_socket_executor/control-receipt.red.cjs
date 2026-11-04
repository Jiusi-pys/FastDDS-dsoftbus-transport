'use strict';
const assert=require('node:assert/strict'),cp=require('node:child_process'),fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {createControlRelease}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const run='0123456789abcdef';const helper=path.resolve(__dirname,'local-child.cjs');
const c={run,scenario:'SHARED_W1',delete_first:'B',cycles:10,hdc_path:process.execPath,remote:{B:`/data/local/tmp/mdds_dds_${run}_B`},targets:{B:'LOCAL_SIMULATION'}};
const directory=fs.mkdtempSync(path.join(os.tmpdir(),'mdds-control-local-'));
(async()=>{let launches=0;const release=createControlRelease(c,directory,{timeoutMs:60,drainMs:250,spawn(exe,args,options){assert.equal(exe,process.execPath);launches++;return cp.spawn(process.execPath,[helper,'hang'],options);}});await assert.rejects(release('B','C6_P_OLD_DELETED'));assert.equal(launches,1);const r=release.receipts[0];assert.equal(r.termination?.reason,'PHASE_DEADLINE','request initiator/reason must be observed before kill');assert.equal(r.termination?.requested,true);console.log('PASS local control termination receipt');})().catch(e=>{console.error(e);process.exitCode=1;});
