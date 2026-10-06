'use strict';
const isShared=c=>c.scenario==='SHARED_W1'||c.scenario==='SHARED_W2';
const sharedPhases=['LISTENER_READY','NATIVE_READY','SHARED_INITIAL_READY','SHARED_INITIAL_MATCH','SHARED_INITIAL_DONE','P_OLD_DELETED','Q_GAP_DONE','P_NEW_READY','P_NEW_MATCH','SHARED_DATA_DONE','DELETE','DDS_DELETED'];
const cycleCount=c=>c.cycles===undefined?1:c.cycles;
const cycleStage=(c,n,name)=>cycleCount(c)>1?`C${n}_${name}`:name;
function phasesFor(c){const n=cycleCount(c);if(!Number.isInteger(n)||n<1||n>100)throw Error('Invalid cycle count');return [...sharedPhases.slice(0,2),...Array.from({length:n},(_,i)=>sharedPhases.slice(2,-2).map(s=>cycleStage(c,i+1,s))).flat(),...sharedPhases.slice(-2).map(s=>cycleStage(c,n,s))];}
function sharedReceipts(c,fail,issued){
 const ids={},matches={},matchCounts={},deletes={},writes={},takes={};const n=cycleCount(c);
 const kv=text=>Object.fromEntries([...text.matchAll(/(\w+)=([^ ]*)/g)].map(m=>[m[1],m[2]]));
 const other=s=>s==='A'?'B':'A',key=(s,g,l)=>s+g+l,validId=v=>/^[0-9a-f]{32}$/.test(v||'');
 const gate=(s,cycle,name)=>issued.includes(s+cycleStage(c,cycle,name));
 const expected=(cycle,phase,g,l)=>phase==='INITIAL'&&g==='P'&&l===cycle?[1,2,3]:phase==='REATTACH'&&g==='P'&&l===cycle+1?[1,2,3]:g==='Q'&&l===1&&['INITIAL','GAP','REATTACH'].includes(phase)?Array.from({length:3},(_,i)=>9*(cycle-1)+1+3*['INITIAL','GAP','REATTACH'].indexOf(phase)+i):[];
 const sampleKey=(s,cycle,g,l,phase,ordinal)=>key(s,g,l)+':'+cycle+':'+phase+':'+ordinal;
 function line(side,text){
  if(!/^SHARED_(ID|MATCH|DELETE|WRITE|TAKE) /.test(text))return false;
  const tokens=[...text.matchAll(/(\w+)=([^ ]*)/g)];if(new Set(tokens.map(x=>x[1])).size!==tokens.length){fail('SHARED_DUPLICATE_FIELD');return true;}
  const r=kv(text),cycle=Number(r.cycle??(n===1?1:NaN)),life=Number(r.life),k=key(side,r.group,life);
  if(r.side!==(side==='A'?'0':'1')||r.run!==c.run||!Number.isInteger(cycle)||cycle<1||cycle>n||!(r.group==='Q'&&life===1||r.group==='P'&&Number.isInteger(life)&&life>=1&&life<=n+1)){fail('SHARED_RECEIPT_IDENTITY');return true;}
  if(text.startsWith('SHARED_ID ')){
   if(ids[k]||![r.participant,r.writer,r.reader].every(validId)||![r.app,r.receiver,r.token].every(x=>/^[1-9][0-9]*$/.test(x||''))||cycle!==Math.max(1,life-1)){fail('SHARED_CREATE_IDENTITY');return true;}
   if(r.group==='P'&&life>1){const prior=key(side,'P',life-1),old=ids[prior];if(!old||!deletes[prior]||!gate(side,cycle,'Q_GAP_DONE')||old.name!==r.name||['app','receiver','token','writer','reader','participant'].some(x=>old[x]===r[x])){fail('SHARED_RECREATE_ORDER_OR_IDENTITY');return true;}}
   else{const first=c.scenario==='SHARED_W1'?'P':'Q',seen=Object.keys(ids).filter(x=>x[0]===side).length;if(cycle!==1||r.group!==(seen===0?first:first==='P'?'Q':'P')){fail('SHARED_CREATE_ORDER');return true;}}
   ids[k]=r;return true;
  }
  const own=ids[k];if(!own){fail('SHARED_NO_CREATE');return true;}
  if(n>1&&(r.app!==own.app||r.receiver!==own.receiver)){fail('SHARED_APPLICATION_RECEIVER');return true;}
  if(text.startsWith('SHARED_MATCH ')){
   const phase=r.phase||(r.group==='P'?(life===cycle?'INITIAL':'REATTACH'):(matchCounts[k+':'+cycle]?'REATTACH':'INITIAL'));
   const mk=k+':'+cycle+':'+phase;
   if(!['INITIAL','REATTACH'].includes(phase)||r.group==='P'&&life!==cycle+(phase==='REATTACH'?1:0)||!gate(side,cycle,phase==='INITIAL'?'SHARED_INITIAL_READY':'P_NEW_READY')||matches[mk]||r.writer!==own.writer||r.reader!==own.reader){fail('SHARED_MATCH_LOCAL_IDENTITY');return true;}
   matches[mk]=r;matchCounts[k+':'+cycle]=(matchCounts[k+':'+cycle]||0)+1;return true;
  }
  if(text.startsWith('SHARED_DELETE ')){
   const mid=r.group==='P'&&life<=n;
   const allowed=mid?cycle===life&&gate(side,cycle,'SHARED_INITIAL_DONE'):cycle===n&&issued.includes(side+cycleStage(c,n,'DELETE'));
   if(!allowed||deletes[k]||r.participant!==own.participant||r.entities_rc!=='0'||r.participant_rc!=='0'||r.input_open!=='0'){fail('SHARED_DELETE_BOUNDARY');return true;}deletes[k]=r;return true;
  }
  const write=text.startsWith('SHARED_WRITE '),records=write?writes:takes;
  const required={INITIAL:'SHARED_INITIAL_MATCH',GAP:'P_OLD_DELETED',REATTACH:'P_NEW_MATCH'}[r.phase],ordinal=Number(r.ordinal),row=sampleKey(side,cycle,r.group,life,r.phase,ordinal),peer=ids[key(other(side),r.group,life)];
  if(!required||!gate(side,cycle,required)||!expected(cycle,r.phase,r.group,life).includes(ordinal)||records[row]||!peer||!(/^[1-9][0-9]*$/.test(r.sn||''))){fail('SHARED_SAMPLE_PHASE_OR_DUPLICATE');return true;}
  if(write?(r.writer!==own.writer||r.rc!=='0'||r.serialized!=='60'):(r.writer!==peer.writer||r.reader!==own.reader||r.valid!=='1'||r.app!==own.app||r.receiver!==own.receiver)){fail('SHARED_SAMPLE_IDENTITY');return true;}
  if(Object.values(records).some(x=>x.side===r.side&&x.writer===r.writer&&x.sn===r.sn)){fail('SHARED_REUSED_DDS_SAMPLE');return true;}
  records[row]=r;return true;
 }
 function matchStage(cycle,phase){for(const s of ['A','B'])for(const g of ['P','Q']){const life=g==='P'?cycle+(phase==='REATTACH'?1:0):1,k=key(s,g,life),m=matches[k+':'+cycle+':'+phase],own=ids[k],peer=ids[key(other(s),g,life)];if(!m||!own||!peer||m.remote_writer!==peer.writer||m.remote_reader!==peer.reader||m.writer.slice(0,24)===m.remote_writer.slice(0,24)){fail('SHARED_EXACT_REMOTE_MATCH');return false;}}return true;}
 function dataStage(cycle,phase){for(const s of ['A','B'])for(const g of phase==='GAP'?['Q']:['P','Q']){const life=g==='P'?cycle+(phase==='REATTACH'?1:0):1;for(const ordinal of expected(cycle,phase,g,life)){const k=sampleKey(s,cycle,g,life,phase,ordinal),peer=sampleKey(other(s),cycle,g,life,phase,ordinal);if(!writes[k]||!takes[k]||!writes[peer]||writes[peer].sn!==takes[k].sn){fail('SHARED_INCOMPLETE_PHASE_LEDGER');return false;}}}return true;}
 function stage(name,side){
  const parsed=/^C([1-9][0-9]*)_(.*)$/.exec(name),cycle=parsed?Number(parsed[1]):1,phase=parsed?parsed[2]:name;
  if(phase==='SHARED_INITIAL_MATCH')return matchStage(cycle,'INITIAL');
  if(phase==='P_NEW_MATCH')return matchStage(cycle,'REATTACH');
  if(phase==='SHARED_INITIAL_DONE')return dataStage(cycle,'INITIAL');
  if(phase==='Q_GAP_DONE')return dataStage(cycle,'GAP');
  if(phase==='SHARED_DATA_DONE')return dataStage(cycle,'REATTACH');
  if(phase==='P_OLD_DELETED'&&['A','B'].some(s=>!deletes[key(s,'P',cycle)])){fail('SHARED_BOTH_DELETE_REQUIRED');return false;}
  if(phase==='DDS_DELETED'&&(!deletes[key(side,'P',n+1)]||!deletes[key(side,'Q',1)])){fail('SHARED_FINAL_DELETE_REQUIRED');return false;}
  return true;
 }
 return {line,stage,report:()=>({identities:ids,matches,matchCounts,deletes,writes,takes})};
}
module.exports={isShared,sharedPhases,phasesFor,cycleCount,cycleStage,sharedReceipts};
