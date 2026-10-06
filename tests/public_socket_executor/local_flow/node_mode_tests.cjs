'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const repo=path.resolve(__dirname,'../../..'),root=path.resolve(process.argv[2]);
if(path.basename(root)!=='local-verified-20260930-current')throw Error('Saved local positive fixture only');
const runner=require(path.join(repo,'scripts/public_socket/shared_reattach/executor/runner-template.cjs'));
const c=JSON.parse(fs.readFileSync(path.join(root,'windows/data-2/window.json'))),f=JSON.parse(fs.readFileSync(c.task_contract.freeze_path));
assert.equal(runner.readBoundPrivate(c).hdc_path,c.hdc_path);
const dir=fs.mkdtempSync(path.join(root,'node-mode-negative-'));let cases=0;
for(const key of ['local_contract_test','execution_admitted','device_authorized','device_evidence'])for(const value of ['false','true',0,1,null]){
 const contract=path.join(dir,`contract-${cases}.json`),bytes=Buffer.from(JSON.stringify({...f,[key]:value}));fs.writeFileSync(contract,bytes,{flag:'wx'});
 const altered={...c,task_contract:{...c.task_contract,freeze_path:contract,freeze_sha256:crypto.createHash('sha256').update(bytes).digest('hex')}};
 assert.throws(()=>runner.readBoundPrivate(altered),/JSON boolean/);cases++;
}
fs.writeFileSync(path.join(dir,'RESULT.json'),JSON.stringify({cases,pass:true,spawned:0}));console.log(`PASS ${cases} actual Node mode type rejections; zero dispatch`);
