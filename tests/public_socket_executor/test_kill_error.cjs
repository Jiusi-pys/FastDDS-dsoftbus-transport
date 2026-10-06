'use strict';
// Explicit event injection: Windows OS kill errors are not assumed reproducible.
const assert=require('node:assert/strict'),{EventEmitter}=require('node:events');
const {runRecordPair}=require('../../scripts/public_socket/record-pair.cjs');
(async()=>{
 const result=await runRecordPair({targets:{A:'LOCAL',B:'LOCAL'},commands:{A:'LOCAL',B:'LOCAL'},timeoutMs:20,drainGraceMs:20,
 spawn(){const child=new EventEmitter();child.pid=123;child.stdout=new EventEmitter();child.stderr=new EventEmitter();child.kill=()=>{throw Error('SECONDARY_KILL_ERROR');};queueMicrotask(()=>child.stdout.emit('data',Buffer.from('local output')));return child;},
 onOutput(){throw Error('ORIGINAL_OUTPUT_FAILURE');}});
 assert.equal(result.pass,false);assert.match(result.A.error,/ORIGINAL_OUTPUT_FAILURE/);
 assert.match(result.A.termination.kill_error,/SECONDARY_KILL_ERROR/);
 console.log('PASS injected kill error preserves original collector error');
})().catch(e=>{console.error(e);process.exitCode=1;});
