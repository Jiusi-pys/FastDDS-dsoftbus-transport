'use strict';
// Reuse a completed actual local window's existing claim; no second dispatch.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const repo=path.resolve(__dirname,'../../..'),root=path.resolve(process.argv[2]);
const runner=require(path.join(repo,'scripts/public_socket/shared_reattach/executor/runner-template.cjs'));
const c=JSON.parse(fs.readFileSync(path.join(root,'windows/data-2/window.json'))),w=c.window_root;
assert.equal(c.simulated,true);assert.equal(path.basename(c.hdc_path),'fake-hdc.exe');
const before=fs.readFileSync(path.join(root,'installed-runtime/attempt-ledger/data.json'));
const dispatch=runner.createDataDispatcher(c,w);
assert.throws(()=>dispatch('A',c.hdc_path,['-t','SIM-A','shell','LOCAL_NEVER_SPAWNED']),/EEXIST/);
assert.throws(()=>dispatch('B',c.hdc_path,['-t','SIM-B','shell','LOCAL_NEVER_SPAWNED']),/FOREIGN_OR_RESTARTED_DISPATCH/);
assert.deepEqual(fs.readFileSync(path.join(root,'installed-runtime/attempt-ledger/data.json')),before);
const privateConfig=runner.readBoundPrivate(c);assert.equal(privateConfig.hdc_path,c.hdc_path);
console.log('PASS same completed claim rejects duplicate A and foreign/restarted B; zero spawned commands');
