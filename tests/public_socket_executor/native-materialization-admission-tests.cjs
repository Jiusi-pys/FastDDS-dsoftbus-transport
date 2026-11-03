'use strict';
// Explicit controlled inputs; virtual copies only, no writes/driver/claims.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=path.resolve(process.argv[2]||'');if(!process.argv[2])throw Error('Explicit controlled window required');
const c=JSON.parse(fs.readFileSync(path.join(root,'window.json'))),real=fs.readFileSync;
const copies={'runner.cjs':'runner-template.cjs','dds214-control.cjs':'dds214-control.cjs','ros-control.cjs':'ros-control.cjs','shared-control.cjs':'shared-control.cjs','record-pair.cjs':'record-pair.cjs','observe-native.cjs':'observe-native.cjs','bounded-output.cjs':'bounded-output.cjs','bounded-native-scene.cjs':'bounded-native-scene.cjs'};
const runner=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
function check(config=c,corrupt){
 fs.readFileSync=function(p,...args){const name=path.basename(String(p));if(path.dirname(path.resolve(String(p)))===root&&copies[name])return real(path.join(c.task_contract.task_root,'executor',copies[name]),...args);if(corrupt&&path.resolve(String(p))===path.join(c.task_contract.task_root,'executor/bounded-native-scene.cjs'))return Buffer.from('CORRUPTED_INERT_INPUT');return real(p,...args);};
 try{return runner.readBoundPrivate(config);}finally{fs.readFileSync=real;}
}
check();
assert.throws(()=>check(c,true),/missing\/changed/);
const downgrade={...c};delete downgrade.bounded_native_reaccept;assert.throws(()=>check(downgrade),/NATIVE_SCENE_BINDING/);
assert.throws(()=>check({...c,analyzer_sha256:'0'.repeat(64)}),/analyzer not bound/);
console.log('PASS 4 actual native materialization admission cases; NO_DEVICE_OPERATIONS');
