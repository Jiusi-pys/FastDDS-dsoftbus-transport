const assert=require('node:assert/strict');
const {createController,buildCommands:rawBuildCommands}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const buildCommands=c=>rawBuildCommands(c,{A:'123',B:'456'});
const run='0123456789abcdef';const config=scenario=>({run,scenario,delete_first:scenario==='SHARED_W2'?'A':'B',system_library_dirs:['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'],remote:Object.fromEntries(['A','B'].map(s=>[s,`/data/local/tmp/mdds_dds_${run}_${s}`])),token:{A:'123',B:'456'},peer:{A:'a'.repeat(64),B:'b'.repeat(64)}});
const id=(s,g,l)=>{const x=(s==='A'?'a':'b')+(g==='P'?(l==='1'?'1':'3'):'2');return x+'0'.repeat(22)};
const wr=(s,g,l)=>id(s,g,l)+'00000102',rd=(s,g,l)=>id(s,g,l)+'00000207';
const app=(g,l)=>g==='P'?(l==='1'?1:7):4;
async function trial(scenario,mutation=''){
 const c=config(scenario),released=[];const p=createController(c,async(s,t)=>released.push(s+t),async()=>({A:{pass:true},B:{pass:true}}));
 const line=(s,text)=>p.line(s,text),tick=()=>new Promise(r=>setImmediate(r));
 const stage=async name=>{for(const s of ['A','B'])line(s,`DDS_STAGE run=${run} side=${s} pid=${s==='A'?10:20} stage=${name}`);await tick();};
 const own=(s,g,l)=>`run=${run} side=${s==='A'?0:1} group=${g} life=${l}`;
 const created=(s,g,l)=>line(s,`SHARED_ID ${own(s,g,l)} participant=${id(s,g,l)}000001c1 writer=${wr(s,g,l)} reader=${rd(s,g,l)} app=${mutation==='reuse-app'&&l==='2'?1:app(g,l)} receiver=${app(g,l)+2} token=${mutation==='reuse-token'&&l==='2'?2:app(g,l)+1} name=${s+g}`);
 const matched=(s,g,l)=>line(s,`SHARED_MATCH ${own(s,g,l)} writer=${wr(s,g,l)} reader=${rd(s,g,l)} remote_writer=${wr(s==='A'?'B':'A',g,mutation==='old-match'&&l==='2'?'1':l)} remote_reader=${rd(s==='A'?'B':'A',g,l)}`);
 const deleted=(s,g,l)=>line(s,`SHARED_DELETE ${own(s,g,l)} participant=${id(s,g,l)}000001c1 entities_rc=0 participant_rc=0 input_open=0`);
 const samples=(phase,g,l,start)=>{for(const s of ['A','B'])for(let n=start;n<start+3;n++)line(s,`SHARED_WRITE ${own(s,g,l)} phase=${phase} ordinal=${n} writer=${wr(s,g,l)} sn=${n} rc=0 serialized=60`);for(const s of ['A','B'])for(let n=start;n<start+3;n++){if(mutation==='missing-q'&&phase==='GAP'&&s==='B'&&n===6)continue;line(s,`SHARED_TAKE ${own(s,g,l)} phase=${phase} ordinal=${n} writer=${wr(s==='A'?'B':'A',g,l)} reader=${rd(s,g,l)} sn=${n} valid=1 app=${app(g,l)} receiver=${app(g,l)+2}`);}};
 await stage('LISTENER_READY');await stage('NATIVE_READY');for(const g of scenario==='SHARED_W1'?['P','Q']:['Q','P'])for(const s of ['A','B'])created(s,g,'1');await stage('SHARED_INITIAL_READY');for(const g of ['P','Q'])for(const s of ['A','B'])matched(s,g,'1');await stage('SHARED_INITIAL_MATCH');samples('INITIAL','P','1',1);samples('INITIAL','Q','1',1);await stage('SHARED_INITIAL_DONE');
 if(mutation==='early-gap')samples('GAP','Q','1',4);
 for(const s of ['A','B'])if(!(mutation==='missing-delete'&&s==='B'))deleted(s,'P','1');await stage('P_OLD_DELETED');samples('GAP','Q','1',4);await stage('Q_GAP_DONE');for(const s of ['A','B'])created(s,'P','2');await stage('P_NEW_READY');for(const g of ['P','Q'])for(const s of ['A','B'])matched(s,g,g==='P'?'2':'1');await stage('P_NEW_MATCH');samples('REATTACH','P','2',1);samples('REATTACH','Q','1',7);await stage('SHARED_DATA_DONE');await stage('DELETE');for(const s of [c.delete_first,c.delete_first==='A'?'B':'A']){deleted(s,'P','2');deleted(s,'Q','1');line(s,`DDS_STAGE run=${run} side=${s} pid=${s==='A'?10:20} stage=DDS_DELETED`);await tick();}return p.finish();
}
(async()=>{for(const c of ['SHARED_W1','SHARED_W2']){assert.match(buildCommands(config(c)).A,new RegExp('--scenario '+c));assert.equal((await trial(c)).pass,true);}for(const m of ['reuse-app','reuse-token','old-match','missing-q','early-gap','missing-delete'])assert.equal((await trial('SHARED_W1',m)).pass,false,m);console.log('PASS shared controller two orders and six semantic negatives');})().catch(e=>{console.error(e);process.exitCode=1});
