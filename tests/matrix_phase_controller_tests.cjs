'use strict';
const assert=require('node:assert/strict');
const {createMatrixController}=require('../scripts/MatrixPhaseController.cjs');
const run='0123456789abcdef',caseId='M1';
const phases=['INITIAL','PRE','CHANGE','POST','OBSERVE','CLEANUP'];
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(releaseOverride){
  const releases=[],events=[],aborts=[];
  const c=createMatrixController({run,caseId,release:releaseOverride|| (async(side,phase,body)=>{releases.push({side,phase,body});}),
    abort:async(side,body)=>aborts.push({side,body}),record:e=>events.push(e)});
  return {c,releases,events,aborts};
}
function line(side,phase,extra={}){
  const fields={event:'PHASE_DONE',run,case:caseId,side,pid:side==='A'?41:42,order:phases.indexOf(phase)+1,phase,...extra};
  return 'MATRIX '+Object.entries(fields).map(([k,v])=>k+'='+v).join(' ')+'\n';
}
(async()=>{
  let passed=0;
  async function test(name,fn){await fn();passed++;process.stdout.write('PASS '+name+'\n');}
  await test('both real phase receipts required',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('A','INITIAL')));await tick();assert.equal(f.releases.length,0);f.c.output('B','stdout',Buffer.from(line('B','INITIAL')));await tick();assert.equal(f.releases.length,2);assert.deepEqual(f.releases.map(x=>x.phase),['PRE','PRE']);});
  await test('complete ordered barriers contain metadata only',async()=>{const f=fixture();for(const p of phases){for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,p)));await tick();}const r=await f.c.finish();assert.equal(r.pass,true);assert.equal(f.releases.length,10);for(const x of f.releases)assert.equal(x.body,`MATRIX_RELEASE ${run} ${caseId} ${x.phase}\n`);});
  await test('wrong run never releases',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('A','INITIAL',{run:'ffffffffffffffff'})));assert.equal((await f.c.finish()).pass,false);assert.equal(f.releases.length,0);});
  await test('out of order rejected',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('A','PRE')));assert.match((await f.c.finish()).first_failure,/order|phase/i);assert.equal(f.releases.length,0);});
  await test('duplicate phase rejected',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('A','INITIAL')+line('A','INITIAL')));assert.equal((await f.c.finish()).pass,false);});
  await test('partial output parsed without sleep assumptions',async()=>{const f=fixture();const t=line('A','INITIAL');f.c.output('A','stdout',Buffer.from(t.slice(0,9)));f.c.output('A','stdout',Buffer.from(t.slice(9)));f.c.output('B','stdout',Buffer.from(line('B','INITIAL')));await tick();assert.equal(f.releases.length,2);});
  await test('side binding enforced',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('B','INITIAL')));assert.equal((await f.c.finish()).pass,false);});
  await test('release failure preserved and abort is metadata only',async()=>{const f=fixture(async()=>{throw Error('injected-control-io');});for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,'INITIAL')));await tick();const r=await f.c.finish();assert.match(r.first_failure,/injected-control-io/);assert.equal(f.aborts.length,2);assert.equal(f.aborts[0].body,`MATRIX_ABORT ${run} ${caseId}\n`);});
  await test('first business failure not overwritten by cleanup',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(`MATRIX event=FIRST_FAILURE run=${run} case=${caseId} side=A pid=41 stage=PRE predicate=payload\n`));f.c.output('B','stdout',Buffer.from(`MATRIX event=CLEANUP_FAILURE run=${run} case=${caseId} side=B pid=42 predicate=delete\n`));const r=await f.c.finish();assert.match(r.first_failure,/payload/);assert.equal(r.cleanup_failures.length,1);});
  await test('PID change rejected',async()=>{const f=fixture();for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,'INITIAL')));await tick();f.c.output('A','stdout',Buffer.from(line('A','PRE',{pid:99})));assert.equal((await f.c.finish()).pass,false);});
  await test('early next phase cannot escape uncompleted releases',async()=>{let unlock;const blocked=new Promise(r=>{unlock=r;});const calls=[];const f=fixture(async(s,p)=>{calls.push([s,p]);if(p==='PRE')await blocked;});for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,'INITIAL')));for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,'PRE')));await tick();assert.equal(calls.length,2);unlock();await tick();assert.equal(calls.length,4);});
  await test('duplicate fields rejected',async()=>{const f=fixture();f.c.output('A','stdout',Buffer.from(line('A','INITIAL').trimEnd()+' run=bad\n'));assert.equal((await f.c.finish()).pass,false);});
  await test('cleanup-only failure does not invent business failure',async()=>{const f=fixture();for(const p of phases){for(const s of ['A','B'])f.c.output(s,'stdout',Buffer.from(line(s,p)));await tick();}f.c.output('B','stdout',Buffer.from(`MATRIX event=CLEANUP_FAILURE run=${run} case=${caseId} side=B pid=42 predicate=delete\n`));const r=await f.c.finish();assert.equal(r.pass,false);assert.equal(r.first_failure,null);assert.equal(r.cleanup_failures.length,1);});
  console.log('matrix phase controller '+passed+'/13 PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
