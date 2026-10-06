'use strict';
// Direct-DDS scene only; existing target transport records retain their grammar.
const STAGES=['LISTENER_READY','MATCH','DATA_DONE','DDS_DELETED'];
function fields(text){const r={};for(const token of text.split(' ').slice(1)){const m=/^([a-z_]+)=(\S+)$/.exec(token);if(!m||Object.hasOwn(r,m[1]))throw Error('DDS214_RECEIPT_MALFORMED');r[m[1]]=m[2];}return r;}
function commands(c,tokens,dirs){
 if(c.main_member!=='fastdds_dsoftbus_dds214_baseline'||c.main_prefix!=='DDS214_MAIN'||c.main_mode!=='normal'||c.timeout_seconds!==240)throw Error('DDS214_APPLICATION_CONTRACT');
 const result={};for(const s of ['A','B']){const root=c.remote[s];if(root!==`/data/local/tmp/mdds_dds_${c.run}_${s}`||c.token[s]!==tokens[s]||!/^[1-9][0-9]*$/.test(tokens[s]))throw Error('DDS214_COMMAND_IDENTITY');
 result[s]=`cd ${root} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=${root}:${dirs.join(':')} SKIP_DEFAULT_XML=1 FASTDDS_DSOFTBUS_REQUIRED_CONFIG=${root}/dds214.conf FASTDDS_DSOFTBUS_NATIVE_TOKEN=${tokens[s]} FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX=${root}/control timeout ${c.timeout_seconds} ${root}/${c.main_member} ${s} ${c.run} normal; rc=$?; echo REMOTE_EXIT=$rc; exit $rc`;}
 return result;
}
function controller(c,release,admission){
 const pids={},listeners={},ids={},matches={},stages={A:0,B:0},writes={A:{},B:{}},takes={A:{},B:{}},waits={},done={},entities={},deleted={},closed={},exits={};
 let first=null,chain=Promise.resolve();const issued=[],released=[],secondary=[];
 const fail=e=>{if(!first)first=String(e);else if(secondary.length<8)secondary.push(String(e));};
 const queue=fn=>{chain=chain.then(async()=>{if(!first)await fn();}).catch(fail);};
 const send=async(s,t)=>{if(first)return;issued.push(s+':'+t);await release(s,t);released.push(s+':'+t);};
 function validData(){for(const s of ['A','B']){const peer=s==='A'?'B':'A';if(!ids[s]||!ids[peer]||!done[s]||waits[s]!=='0'||Object.keys(writes[s]).length!==5||Object.keys(takes[s]).length!==5)return false;let previous=0n;
  for(let n=1;n<=5;n++){const w=writes[s][n],t=takes[s][n],source=writes[peer][n];if(!w||!t||!source||w.ok!=='1'||w.writer!==ids[s].writer||t.writer!==ids[peer].writer||t.reader!==ids[s].reader||t.payload_ok!=='1'||t.sn!==source.sn||BigInt(w.sn)<=previous)return false;previous=BigInt(w.sn);}}
  return ids.A.participant!==ids.B.participant&&ids.A.writer!==ids.B.writer;
 }
 function line(side,text){
  if(!['A','B'].includes(side)){fail('DDS214_SIDE');return;}
  if(text.startsWith('DDS214_LOCAL_MODEL ')){if(c.simulated!==true)fail('DDS214_LOCAL_MODEL_NOT_DEVICE');return;}
  if(!/^(DDS214_|ROS_LISTENER_READY |ROS_DDS_DELETED |FDDST_ROS_CLOSED )/.test(text))return;
  let r;try{r=fields(text);}catch(e){fail(e.message);return;}
  const kind=text.split(' ')[0];
  if(kind==='FDDST_ROS_CLOSED'){if(r.run!==c.run||r.side!==(side==='A'?'0':'1')||r.runtime_closed!=='1'||r.pump_joined!=='1'||closed[side])fail('DDS214_RUNTIME_CLOSE');else closed[side]=true;return;}
  if(r.run!==c.run||r.side!==side){fail('DDS214_RECEIPT_IDENTITY');return;}
  if(kind==='DDS214_MAIN'){if(pids[side]||r.scenario!=='DDS214_LAYER_BASELINE'||r.mode!=='normal'||!/^[1-9][0-9]*$/.test(r.pid||''))fail('DDS214_MAIN_IDENTITY');else pids[side]=r.pid;return;}
  if(kind==='DDS214_FAILURE'){fail('DDS214_APPLICATION_FAILURE_'+side+':'+r.stage+':'+r.reason);return;}
  if(kind==='ROS_LISTENER_READY'){if(r.pid!==pids[side]||r.fake_sdk!=='0'||!/^\d+$/.test(r.listener||'')||Number(r.listener)>2147483647||listeners[side]){fail('DDS214_LISTENER_IDENTITY');return;}listeners[side]=r;if(listeners.A&&listeners.B)queue(async()=>{for(const s of ['A','B'])await send(s,'LISTENER_READY');});return;}
  if(kind==='DDS214_ID'){if(ids[side]||!['participant','writer','reader'].every(k=>/^[0-9a-f]{32}$/.test(r[k]||''))||r.writer_topic!==`dds214_${c.run}_from_${side}`||r.reader_topic!==`dds214_${c.run}_from_${side==='A'?'B':'A'}`)fail('DDS214_ENTITY_IDENTITY');else ids[side]=r;return;}
  if(kind==='DDS214_MATCH'){if(!ids[side]||matches[side]||r.writers!=='1'||r.readers!=='1')fail('DDS214_MATCH');else matches[side]=true;return;}
  if(kind==='DDS214_WRITE'||kind==='DDS214_TAKE'){const dest=kind==='DDS214_WRITE'?writes:takes;if(!/^[1-5]$/.test(r.ordinal||'')||!/^[1-9][0-9]*$/.test(r.sn||'')||dest[side][r.ordinal])fail('DDS214_SAMPLE_IDENTITY');else dest[side][r.ordinal]=r;return;}
  if(kind==='DDS214_WAIT'){if(waits[side]!==undefined||r.rc!=='0')fail('DDS214_WAIT');waits[side]=r.rc;return;}
  if(kind==='DDS214_DATA_DONE'){if(done[side]||r.writes!=='5'||r.takes!=='5')fail('DDS214_DATA_DONE');else done[side]=true;return;}
  if(kind==='DDS214_STAGE'){
   const expected=stages[side]===0?'MATCH':'DATA_DONE';if(r.pid!==pids[side]||r.stage!==expected||stages[side]>=2){fail('DDS214_STAGE_IDENTITY');return;}stages[side]++;
   if(r.stage==='MATCH'&&(!listeners.A||!listeners.B||!matches[side])){fail('DDS214_MATCH_PREREQUISITE');return;}
   if(r.stage==='DATA_DONE'&&!issued.includes(side+':MATCH')){fail('DDS214_DATA_BEFORE_RELEASE');return;}
   if(stages.A>=stages[side]&&stages.B>=stages[side])queue(async()=>{
    if(r.stage==='MATCH'){if(!listeners.A||!listeners.B||!matches.A||!matches.B){fail('DDS214_MATCH_PREREQUISITE');return;}const proof=await admission();if(first)return;if(!proof.A?.pass||!proof.B?.pass){fail('DDS214_LOAD_CAPTURE_FAILED');return;}}
    else if(!validData()){fail('DDS214_SAMPLE_LEDGER_INCOMPLETE');return;}
    for(const s of ['A','B'])await send(s,r.stage);
   });return;
  }
  if(kind==='DDS214_DELETE_ENTITIES'){if(entities[side]||r.rc!=='0')fail('DDS214_DELETE_ENTITIES');else entities[side]=true;return;}
  if(kind==='DDS214_DELETE_PARTICIPANT_RETURN'){if(!entities[side]||deleted[side]||r.rc!=='0')fail('DDS214_DELETE_PARTICIPANT');else deleted[side]=true;return;}
  if(kind==='DDS214_EXIT'){if(exits[side]||r.result!=='0')fail('DDS214_APPLICATION_EXIT');else exits[side]=true;return;}
 }
 return {line,stop:fail,end(s,confirmed){if(!confirmed)fail('DDS214_COLLECTOR_INCOMPLETE_'+s);},async finish(){let p;do{p=chain;await p;}while(p!==chain);if(!first&&(!validData()||!['A','B'].every(s=>deleted[s]&&closed[s]&&exits[s])||released.length!==6))fail('DDS214_FINAL_INCOMPLETE');return {pass:!first,first_failure:first,secondary_failures:secondary,issued,released,pids,identities:ids,stages};}};
}
module.exports={STAGES,commands,controller};
