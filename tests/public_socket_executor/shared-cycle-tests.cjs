'use strict';
const assert=require('node:assert/strict');
const {createController,buildCommands:rawBuildCommands,phases}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const buildCommands=c=>rawBuildCommands(c,{A:'123',B:'456'});
const run='0123456789abcdef';
const config=(cycles=2)=>({run,cycles,scenario:'SHARED_W1',delete_first:'B',timeout_seconds:180+cycles*330,system_library_dirs:['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'],remote:Object.fromEntries(['A','B'].map(s=>[s,`/data/local/tmp/mdds_dds_${run}_${s}`])),token:{A:'123',B:'456'},peer:{A:'a'.repeat(64),B:'b'.repeat(64)}});
const id=(s,g,l)=>BigInt((s==='A'?10000:20000)+(g==='Q'?100000:0)+10*l).toString(16).padStart(24,'0');
const wr=(s,g,l)=>id(s,g,l)+'00000102',rd=(s,g,l)=>id(s,g,l)+'00000207';
const app=(g,l)=>(g==='Q'?1000:0)+l*3;
async function trial(mutation='',cycles=2,tap=()=>{}){
 const c=config(cycles),p=createController(c,async()=>{},async()=>({A:{pass:true},B:{pass:true}}));let cycle=1;
 const line=(s,text)=>{tap(s,text);p.line(s,text);},tick=()=>new Promise(r=>setImmediate(r));
 const stage=async name=>{const tag=['LISTENER_READY','NATIVE_READY'].includes(name)?name:`C${cycle}_${name}`;for(const s of ['A','B'])line(s,`DDS_STAGE run=${run} side=${s} pid=${s==='A'?10:20} stage=${mutation==='stale-stage'&&cycle===2&&name==='SHARED_INITIAL_READY'?'C1_SHARED_INITIAL_READY':mutation==='jump-stage'&&cycle===2&&name==='SHARED_INITIAL_READY'?'C2_Q_GAP_DONE':tag}`);if(mutation==='duplicate-stage'&&cycle===2&&name==='SHARED_INITIAL_READY')line('A',`DDS_STAGE run=${run} side=A pid=10 stage=${tag}`);await tick();};
 const own=(s,g,l)=>`run=${mutation==='wrong-run'&&cycle===2?'f'.repeat(16):run} side=${mutation==='wrong-side'&&cycle===2?1:(s==='A'?0:1)} cycle=${mutation==='wrong-cycle'&&cycle===2?1:cycle} group=${g} life=${l}`;
 const created=(s,g,l)=>line(s,`SHARED_ID ${own(s,g,l)} participant=${id(s,g,l)}000001c1 writer=${wr(s,g,l)} reader=${rd(s,g,l)} app=${app(g,l)} receiver=${app(g,l)+2} token=${app(g,l)+1} name=${s+g}`);
 const matched=(s,g,l,phase)=>line(s,`SHARED_MATCH ${own(s,g,l)} phase=${phase} writer=${wr(s,g,l)} reader=${rd(s,g,l)} remote_writer=${wr(s==='A'?'B':'A',g,l)} remote_reader=${rd(s==='A'?'B':'A',g,l)} app=${app(g,l)} receiver=${app(g,l)+2}`);
 const deleted=(s,g,l)=>line(s,`SHARED_DELETE ${own(s,g,l)} participant=${id(s,g,l)}000001c1 entities_rc=0 participant_rc=0 input_open=0 app=${app(g,l)} receiver=${app(g,l)+2}`);
 const samples=(phase,g,l,start)=>{for(const s of ['A','B'])for(let n=start;n<start+3;n++){let sn=n+(g==='P'&&phase==='INITIAL'&&cycle>1?3:0);line(s,`SHARED_WRITE ${own(s,g,l)} phase=${phase} ordinal=${n} writer=${wr(s,g,l)} sn=${sn} rc=0 serialized=60 app=${app(g,l)} receiver=${app(g,l)+2}`);}for(const s of ['A','B'])for(let n=start;n<start+3;n++){let sn=n+(g==='P'&&phase==='INITIAL'&&cycle>1?3:0);line(s,`SHARED_TAKE ${own(s,g,l)} phase=${phase} ordinal=${n} writer=${wr(s==='A'?'B':'A',g,l)} reader=${rd(s,g,l)} sn=${sn} valid=1 app=${app(g,l)} receiver=${app(g,l)+2}`);}};
 await stage('LISTENER_READY');await stage('NATIVE_READY');for(const g of ['P','Q'])for(const s of ['A','B'])created(s,g,1);
 for(cycle=1;cycle<=c.cycles;cycle++){
  await stage('SHARED_INITIAL_READY');for(const g of ['P','Q'])for(const s of ['A','B'])matched(s,g,g==='P'?cycle:1,'INITIAL');await stage('SHARED_INITIAL_MATCH');samples('INITIAL','P',cycle,1);samples('INITIAL','Q',1,9*cycle-8);await stage('SHARED_INITIAL_DONE');
  if(mutation==='early-create'&&cycle===2)created('A','P',3);
  for(const s of ['A','B'])if(!(mutation==='missing-delete'&&cycle===2&&s==='B'))deleted(s,'P',cycle);
  await stage('P_OLD_DELETED');samples('GAP','Q',1,9*cycle-5);await stage('Q_GAP_DONE');
  for(const s of ['A','B'])created(s,'P',mutation==='wrong-life'&&cycle===2?2:cycle+1);
  await stage('P_NEW_READY');for(const g of ['P','Q'])for(const s of ['A','B'])matched(s,g,g==='P'?cycle+1:1,'REATTACH');await stage('P_NEW_MATCH');samples('REATTACH','P',cycle+1,1);samples('REATTACH','Q',1,9*cycle-2);await stage('SHARED_DATA_DONE');
 }
 cycle=c.cycles;await stage('DELETE');for(const s of ['B','A']){deleted(s,'P',c.cycles+1);if(mutation!=='missing-final-q')deleted(s,'Q',1);line(s,`DDS_STAGE run=${run} side=${s} pid=${s==='A'?10:20} stage=C${cycle}_DDS_DELETED`);await tick();}
 return p.finish();
}
module.exports={config,trial};
if(require.main===module)(async()=>{
 assert.match(buildCommands(config()).A,/--cycles 2/);
 assert.ok(phases(config()).includes('C2_SHARED_INITIAL_READY'));
 assert.equal((await trial()).pass,true,'two actual control cycles');
 for(const m of ['wrong-side','jump-stage','duplicate-stage','wrong-run','wrong-cycle','stale-stage','missing-delete','early-create','wrong-life','missing-final-q'])assert.equal((await trial(m)).pass,false,m);
 console.log('PASS cycle controller and ten negatives');
})().catch(e=>{console.error(e);process.exitCode=1;});
