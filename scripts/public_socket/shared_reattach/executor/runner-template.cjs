'use strict';
const fs=require('node:fs'),path=require('node:path');
const CASES=['SHARED_W1','SHARED_W2','F1M','INSTALLED_RUNTIME'];
const INSTALLED_STAGES=['LISTENER_READY','MATCH','DATA_DONE','DDS_DELETED'];
const isInstalled=c=>c.scenario==='INSTALLED_RUNTIME';
// The installed runtime consumer reports INST_* receipts, never SHARED_*/N10/N100
// events, so its stage and sample contracts are validated separately instead of
// being expressed in the probe's grammar.
function installedReceipts(c,fail){
 const identities={},matches={},writes={A:{},B:{}},takes={A:{},B:{}},deletes={},exits={},perSide=c.expected_writes.A;
 const kv=text=>Object.fromEntries([...text.matchAll(/(\w+)=([^ ]*)/g)].map(m=>[m[1],m[2]]));
 const sideOf=r=>r.side;
 function line(side,text){
  if(!/^INST_(ID|MATCH|WRITE|TAKE|DELETE|EXIT) /.test(text))return false;
  const r=kv(text),other=side==='A'?'B':'A';
  // The consumer prints pid on INST_MAIN and INST_STAGE, not on INST_ID, so the
  // pid identity is enforced where it is actually emitted: the launcher/collector
  // main-line pattern and the controller's stage-line pid pin.
  if(r.run!==c.run||sideOf(r)!==side){fail('INSTALLED_RECEIPT_IDENTITY');return true;}
  if(text.startsWith('INST_ID ')){
   if(identities[side]||![r.participant,r.writer,r.reader].every(x=>/^[0-9a-f]{32}$/.test(x||''))){fail('INSTALLED_ID_RECEIPT');return true;}
   identities[side]=r;return true;
  }
  if(text.startsWith('INST_MATCH ')){
   const own=identities[side];if(!own||matches[side]||Number(r.pub_matched)<1||Number(r.sub_matched)<1||r.writer!==own.writer||r.reader!==own.reader){fail('INSTALLED_MATCH_RECEIPT');return true;}
   matches[side]=r;return true;
  }
  if(text.startsWith('INST_WRITE ')){
   const own=identities[side],o=Number(r.ordinal);
   if(!own||r.writer!==own.writer||r.rc!=='0'||!(o>=1&&o<=perSide)||writes[side][o]){fail('INSTALLED_WRITE_RECEIPT');return true;}
   writes[side][o]=r;return true;
  }
  if(text.startsWith('INST_TAKE ')){
   const own=identities[side],o=Number(r.ordinal);
   // The peer's own create receipt may still be in flight: the two boards stream
   // independently, so the cross-board writer identity is checked at DATA_DONE
   // where both sides must have reported it, not at line arrival.
   if(!own||r.reader!==own.reader||r.payload_ok!=='1'||r.writer_field!==String(other==='A'?1:2)||r.run_field!==String(BigInt('0x'+c.run))||!(o>=1&&o<=perSide)||takes[side][o]){fail('INSTALLED_TAKE_RECEIPT');return true;}
   takes[side][o]=r;return true;
  }
  if(text.startsWith('INST_DELETE ')){
   if(deletes[side]||r.entities_rc!=='0'||r.participant_rc!=='0'){fail('INSTALLED_DELETE_RECEIPT');return true;}
   deletes[side]=r;return true;
  }
  if(text.startsWith('INST_EXIT ')){
   if(exits[side]||r.result!=='0'||r.restored!=='1'){fail('INSTALLED_EXIT_RECEIPT');return true;}
   exits[side]=r;return true;
  }
  return false;
 }
 function stage(name){
  if(name!=='DATA_DONE')return true;
  for(const side of ['A','B']){
   const other=side==='A'?'B':'A',own=identities[side],peer=identities[other];
   if(!own||!peer||!matches[side]){fail('INSTALLED_MATCH_BEFORE_DATA');return false;}
   // This consumer learns the remote endpoint identity from the received sample,
   // not from the match line, so the cross-board correlation below is the take's
   // remote_writer against the peer's own created writer.
   if(matches[side].writer!==own.writer||matches[side].reader!==own.reader){fail('INSTALLED_MATCH_LOCAL_IDENTITY');return false;}
   for(let o=1;o<=perSide;++o){
    const w=writes[side][o],t=takes[side][o];
    if(!w||!t){fail('INSTALLED_INCOMPLETE_SAMPLE_LEDGER');return false;}
    if(t.remote_writer!==peer.writer){fail('INSTALLED_SAMPLE_SOURCE');return false;}
   }
  }
  return true;
 }
 function finish(){
  for(const side of ['A','B'])if(!deletes[side]||!exits[side])return 'INSTALLED_MISSING_LIFECYCLE_RECEIPT';
  return null;
 }
 return {line,stage,finish,report:()=>({identities,matches,writes,takes,deletes,exits})};
}
// The real entry verifies all materialized/window bytes before loading task code.
let sharedControlModule;
const sharedControl=()=>sharedControlModule||(sharedControlModule=require('./shared-control.cjs'));
const isShared=(...args)=>sharedControl().isShared(...args);
const phasesFor=(...args)=>sharedControl().phasesFor(...args);
const cycleCount=(...args)=>sharedControl().cycleCount(...args);
const sharedReceipts=(...args)=>sharedControl().sharedReceipts(...args);
const phases=c=>isInstalled(c)?INSTALLED_STAGES:isShared(c)?phasesFor(c):['LISTENER_READY','NATIVE_READY',...(c.scenario==='F1M'?['MATCH','SAMPLE_1','SAMPLE_2','SAMPLE_3']:['MATCH']),'DATA_DONE','DELETE','DDS_DELETED'];
const LIBDIRS=['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'];
function buildCommands(c,expectedTokens){
 if(!expectedTokens)throw Error("Explicit token identity binding required");
 if(!/^[0-9a-f]{16}$/.test(c.run)||!CASES.includes(c.scenario)||JSON.stringify(c.system_library_dirs)!==JSON.stringify(LIBDIRS))throw Error('Invalid frozen DDS command');
 if(isInstalled(c)){
  if(c.main_prefix!=='INST_MAIN'||c.main_mode!=='installed_runtime')throw Error('Installed process identity contract');
  const budget=c.timeout_seconds;if(!Number.isInteger(budget)||budget<60||budget>600)throw Error('Installed window deadline contract');
  if(!/^[a-z0-9.]{1,40}$/.test(c.service_prefix||'')||!/^[A-Za-z0-9_.]{1,64}$/.test(c.package_name||''))throw Error('Installed service identity');
  const commands={};for(const side of ['A','B']){
   const root=`/data/local/tmp/mdds_dds_${c.run}_${side}`;if(c.remote[side]!==root||c.token[side]!==expectedTokens[side]||!/^([1-9][0-9]*)$/.test(expectedTokens[side])||!/^[0-9a-f]{64}$/.test(c.peer[side]))throw Error('Identity');
   // Fixed argv, never assembled through a shell evaluation.
   const args=[side==='A'?'0':'1',c.peer[side],c.package_name,c.service_prefix,c.run,c.token[side],root+'/control'];
   commands[side]=`cd ${root} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=${root}:${LIBDIRS.join(':')} SKIP_DEFAULT_XML=1 timeout ${budget} ${root}/${c.main_member} ${args.join(' ')}; rc=$?; echo REMOTE_EXIT=$rc; exit $rc`;
  }return commands;
 }
 if(c.delete_first!==(c.scenario==='SHARED_W2'?'A':'B'))throw Error('Invalid frozen DDS command');
 const cycles=cycleCount(c);if(!Number.isInteger(cycles)||cycles<1||cycles>100||cycles>1&&!isShared(c))throw Error("Invalid cycles");const budget=cycles>1?180+330*cycles:180;if(cycles>1&&c.timeout_seconds!==budget)throw Error("Cycle deadline contract");
 const commands={};for(const side of ['A','B']){
  const root=`/data/local/tmp/mdds_dds_${c.run}_${side}`;if(c.remote[side]!==root||c.token[side]!==expectedTokens[side]||!/^([1-9][0-9]*)$/.test(expectedTokens[side])||!/^[0-9a-f]{64}$/.test(c.peer[side]))throw Error('Identity');
  const args=`--run ${c.run} --side ${side} --control ${root}/control --peer ${c.peer[side]} --token ${c.token[side]} --delete-first ${c.delete_first} --scenario ${c.scenario}${cycles>1?` --cycles ${cycles}`:""}`;
  commands[side]=`cd ${root} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=${root}:${LIBDIRS.join(':')} SKIP_DEFAULT_XML=1 timeout ${budget} ${root}/public_socket_dds_experiment ${args}; rc=$?; echo REMOTE_EXIT=$rc; exit $rc`;
 }return commands;
}
function createController(c,release,admission){
 const stages=phases(c);const progress={A:0,B:0},pids={},identities={},matches={},programResults={};let first=null,chain=Promise.resolve(),receiptSequence=0,cleanupOverflow=false;const failureCleanup=[];const released=[],issued=[],deletion_order=[];const installed=isInstalled(c);const firstSide=installed?null:c.delete_first,secondSide=installed?null:(c.delete_first==='A'?'B':'A');
 const secondary=[];const fail=why=>{if(first!==null){if(secondary.length<8)secondary.push(String(why));return;}first=String(why);try{release.cancel?.({requester:'controller',first_failure:first});}catch(e){if(secondary.length<8)secondary.push(String(e));}};const kv=line=>Object.fromEntries([...line.matchAll(/(\w+)=([^ ]*)/g)].map(m=>[m[1],m[2]]));
 const queue=fn=>{chain=chain.then(async()=>{if(!first)await fn()}).catch(e=>fail(String(e)))};
 const send=async(s,t)=>{if(first)return;issued.push(s+t);await release(s,t);released.push(s+t)};
 const shared=sharedReceipts(c,fail,issued);
 const receipts=installed?installedReceipts(c,fail):null;
 return {stop:fail,line(side,text){
  if(isShared(c)&&text.startsWith('SHARED_RESULT ')){
   const r=kv(text);if(r.run!==c.run||r.side!==(side==='A'?'0':'1')||Number(r.cycles)!==cycleCount(c)||programResults[side]){fail('PROGRAM_RESULT_IDENTITY_OR_DUPLICATE');return;}
   programResults[side]={sequence:++receiptSequence,fields:r};if(r.success!=='1')fail('REMOTE_PROGRAM_FAILURE_'+side);return;
  }
  if(first&&isShared(c)&&text.startsWith('SHARED_DELETE ')){
   const r=kv(text);if(r.phase==='FINAL'){
    if(r.run!==c.run||r.side!==(side==='A'?'0':'1')){fail('CLEANUP_RECEIPT_IDENTITY');return;}
    if(failureCleanup.length<4)failureCleanup.push({sequence:++receiptSequence,side,scope:'FAILED_RUN_CLEANUP_OBSERVATION_ONLY',fields:r});else cleanupOverflow=true;
    return;
   }
  }
  if(isShared(c)&&shared.line(side,text))return;
  if(installed&&receipts.line(side,text))return;
  if(!installed&&(text.startsWith('DDS_IDENTITIES ')||text.startsWith('DDS_MATCH '))){const data=kv(text),target=text.startsWith('DDS_MATCH ')?matches:identities;if(data.side!==(side==='A'?'0':'1')||target[side]){fail('DUPLICATE_OR_WRONG_ENDPOINT_RECEIPT');return;}target[side]=data;return;}
  const m=(installed?/^INST_STAGE run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) stage=([A-Z_0-9]+)$/:new RegExp('^DDS_STAGE run=([0-9a-f]{16}) side=([AB]) pid=([1-9][0-9]*) stage=([A-Z_0-9]+)$')).exec(text);if(!m)return;
  if(m[1]!==c.run||m[2]!==side||m[4]!==stages[progress[side]]||(pids[side]&&pids[side]!==m[3])){fail('CONTROL_IDENTITY_OR_ORDER');return;}pids[side]=m[3];const index=progress[side]++;
  if(index<(installed?stages.length-1:stages.length-2)&&progress.A>index&&progress.B>index){queue(async()=>{
    if(isShared(c)&&!shared.stage(stages[index]))return;
    if(installed&&!receipts.stage(stages[index]))return;
    if(index===0){const proof=await admission();if(first)return;if(!proof.A?.pass||!proof.B?.pass){fail('LOAD_CAPTURE_FAILED');return;}}
    if(!installed&&stages[index]==='MATCH'){for(const s of ['A','B']){const d=s==='A'?'B':'A',i=identities[s],j=identities[d],m=matches[s];if(!i||!j||!m||![i.writer,i.reader,j.writer,j.reader].every(x=>/^[0-9a-f]{32}$/.test(x))||m.writer!==i.writer||m.reader!==i.reader||m.remote_writer!==j.writer||m.remote_reader!==j.reader||i.writer.slice(0,24)===j.writer.slice(0,24)){fail('CROSS_BOARD_MATCH_IDENTITY');return;}}}
    for(const s of ['A','B'])await send(s,stages[index]);
   });}
  if(!installed&&index===stages.length-2&&progress.A>=stages.length-1&&progress.B>=stages.length-1)queue(()=>send(firstSide,stages[stages.length-2]));
  if(!installed&&index===stages.length-1){if(isShared(c)&&!shared.stage(stages[index],side))return;if((deletion_order.length===0?firstSide:secondSide)!==side||!issued.includes(side+stages[stages.length-2])){fail('DDS_DELETE_ORDER');return;}deletion_order.push(side);if(side===firstSide)queue(()=>send(secondSide,stages[stages.length-2]));}
 },end(side,confirmed){if(!confirmed)fail('COLLECTOR_INCOMPLETE_'+side);else if(progress[side]!==stages.length)fail('EARLY_COLLECTOR_CLOSE_'+side);},async finish(){let pending;do{pending=chain;await pending;}while(pending!==chain);if(installed){const late=receipts.finish();if(late)fail(late);}return {pass:!first&&progress.A===stages.length&&progress.B===stages.length&&released.length===(installed?2*(stages.length-1):2*(stages.length-2)+2)&&(installed||deletion_order.length===2),first_failure:first||((progress.A!==stages.length||progress.B!==stages.length)?'INCOMPLETE_STAGES':null),secondary_failures:secondary,program_results:programResults,failure_cleanup_observations:failureCleanup,cleanup_observation_overflow:cleanupOverflow,released,issued,deletion_order,identities,matches,shared:isShared(c)?shared.report():null};}};
}
function createControlRelease(c,directory,{spawn=require('node:child_process').spawn,timeoutMs=7000,drainMs=250}={}){
 if(!path.isAbsolute(c.hdc_path)||!Number.isInteger(timeoutMs)||timeoutMs<1||!Number.isInteger(drainMs)||drainMs<1||drainMs>1000)throw Error('Explicit control executable/deadline required');
 const receipts=[],active=new Map();let sequence=0;const origin=process.hrtime.bigint();
 const release=(side,stage)=>new Promise((resolve,reject)=>{
  if(!['A','B'].includes(side)||!phases(c).includes(stage)||active.size||sequence>=2*phases(c).length){reject(Error('CONTROL_SCOPE_OR_CAPACITY'));return;}
  const root=c.remote[side],filename=`${root}/control.${stage}`;
  if(root!==`/data/local/tmp/mdds_dds_${c.run}_${side}`){reject(Error('CONTROL_REMOTE_IDENTITY'));return;}
  const command=`env -i PATH=/system/bin:/system/xbin timeout 5 /system/bin/sh -c 'test ! -e ${filename} && printf "${c.run} ${cycleCount(c)>1?side+" ":""}${stage}\\n" > ${filename}.tmp && mv ${filename}.tmp ${filename}'; rc=$?; echo CONTROL_REMOTE_EXIT=$rc; exit $rc`;
  const seq=++sequence,argv=['-t',c.targets[side],'shell',command];
  const r={run:c.run,side,stage,cycle:Number(/^C(\d+)_/.exec(stage)?.[1]||(isShared(c)&&cycleCount(c)===1&&!['LISTENER_READY','NATIVE_READY'].includes(stage)?1:0)),command_id:`${c.run}:${side}:${stage}:${seq}`,sequence:seq,observer_pid:process.pid,collector_pid:null,executable:c.hdc_path,argv_sha256:require('node:crypto').createHash('sha256').update(JSON.stringify(argv)).digest('hex'),cwd:process.cwd(),environment:'INHERITED_NOT_LOGGED',deadline_ms:timeoutMs,drain_ms:drainMs,events:[],termination:null,error:null};
  let child,timer,drainTimer,done=false,bytes=0,retained=0;const chunks={stdout:[],stderr:[]},counts={stdout:0,stderr:0};
  const event=(name,detail={})=>{if(r.events.length<8)r.events.push({event:name,host_monotonic_ns:String(process.hrtime.bigint()),local_ns:String(process.hrtime.bigint()-origin),...detail});else r.error||='EVENT_LIMIT';};
  const complete=(code,signal,confirmed)=>{
   if(done)return;done=true;clearTimeout(timer);clearTimeout(drainTimer);active.delete(seq);
   const stdout=Buffer.concat(chunks.stdout).toString('utf8'),stderr=Buffer.concat(chunks.stderr).toString('utf8');
   const exits=[...stdout.matchAll(/^CONTROL_REMOTE_EXIT=(\d+)\r?$/gm)].map(m=>Number(m[1]));
   Object.assign(r,{code,signal,collector_closed:confirmed,collection_state:confirmed?'CLOSED':'INCOMPLETE',stdout,stderr,bytes:{received:bytes,retained,stdout:counts.stdout,stderr:counts.stderr,overflow:bytes-retained},remote_exit:exits.length===1?exits[0]:null,remote_exit_count:exits.length,pass:confirmed&&!r.error&&code===0&&!signal&&exits.length===1&&exits[0]===0});
   r.termination_origin=r.termination?'LOCAL_REQUEST_RECORDED':signal?'EXTERNAL_OR_UNOBSERVED_ORIGIN':'NONE_OBSERVED';
   receipts.push(r);fs.writeFileSync(path.join(directory,`control-${seq}.json`),JSON.stringify(r,null,2),{flag:'wx'});
   if(!confirmed){child?.unref?.();child?.stdout?.unref?.();child?.stderr?.unref?.();}
   r.pass?resolve():reject(Error('CONTROL_WRITE_FAILED'));
  };
  const terminate=({requester,reason,first_failure=null})=>{
   if(done||r.termination)return;r.error||=reason;
   r.termination={requested:true,requester,reason,first_failure,command_id:r.command_id,target_pid:child?.pid??null,signal_requested:'SIGTERM',deadline_ms:timeoutMs,result:null};event('TERMINATION_REQUEST',{reason});
   const request={run:r.run,side:r.side,cycle:r.cycle,phase:r.stage,command_id:r.command_id,observer_pid:process.pid,host_monotonic_ns:String(process.hrtime.bigint()),...r.termination};
   try{fs.writeFileSync(path.join(directory,`control-${seq}.termination-request.json`),JSON.stringify(request,null,2),{flag:'wx'});r.termination.request_persisted_before_kill=true;}catch(e){r.termination.receipt_error=String(e);r.error||='TERMINATION_RECEIPT_FAILED';}
   drainTimer=setTimeout(()=>{event('DRAIN_EXPIRED');complete(null,null,false);},drainMs);
   try{r.termination.result=Boolean(child.kill('SIGTERM'));}catch(e){r.termination.request_error=String(e);}
   try{fs.writeFileSync(path.join(directory,`control-${seq}.termination-result.json`),JSON.stringify({...request,result:r.termination.result,request_error:r.termination.request_error||null,host_monotonic_ns:String(process.hrtime.bigint())},null,2),{flag:'wx'});}catch(e){r.termination.result_receipt_error=String(e);r.error||='TERMINATION_RECEIPT_FAILED';}
  };
  const data=(stream,b)=>{if(done)return;counts[stream]+=b.length;bytes+=b.length;const n=Math.min(b.length,Math.max(0,65536-retained));if(n)chunks[stream].push(b.subarray(0,n));retained+=n;if(n<b.length)r.error||='OUTPUT_OVERFLOW';};
  try{
   child=spawn(c.hdc_path,argv,{shell:false,windowsHide:true});r.collector_pid=child.pid??null;event('START');active.set(seq,{r,terminate});
   child.stdout.on('data',b=>data('stdout',b));child.stderr.on('data',b=>data('stderr',b));
   child.on('error',e=>{r.error||=String(e);event('ERROR');});
   child.on('exit',(code,signal)=>{if(done)return;event('EXIT',{code,signal});clearTimeout(timer);if(!drainTimer)drainTimer=setTimeout(()=>{r.error||='EXIT_DRAIN_INCOMPLETE';event('DRAIN_EXPIRED');complete(code,signal,false);},drainMs);});
   child.on('close',(code,signal)=>{if(done)return;event('CLOSE',{code,signal});complete(code,signal,true);});
   timer=setTimeout(()=>terminate({requester:'stage_deadline',reason:'PHASE_DEADLINE'}),timeoutMs);
  }catch(e){r.error=String(e);event('SPAWN_FAILED');complete(null,null,false);}
 });
 release.cancel=({requester='controller',first_failure,command_id}={})=>{for(const {r,terminate} of active.values())if(!command_id||command_id===r.command_id)terminate({requester,reason:'CONTROLLER_FAILURE',first_failure});};
 release.receipts=receipts;return release;
}
// The process identity rule is read from the frozen window config, so the
// launcher gate, the load collector and the receipt checks cannot drift apart.
function mainLinePattern(c,side){
 if(c.main_prefix!=='INST_MAIN'||c.main_mode!=='installed_runtime')throw Error('Installed process identity contract');
 // Built from the scenario's fixed literals, never from free configuration, so a
 // recorded program that is not this consumer can never start B or its collector.
 if(!/^[0-9a-f]{16}$/.test(c.run)||!['A','B'].includes(side))throw Error('Installed main run/side');
 return new RegExp('^INST_MAIN run='+c.run+' side='+side+' pid=[1-9][0-9]* mode=installed_runtime ');}
function createDataDispatcher(c,directory){
 if(isInstalled(c)){const ledger=path.join(path.resolve(directory,'../..'),'installed-runtime','attempt-ledger');
  if(c.simulated!==true&&(!Number.isInteger(c.installed_attempt)||![1,2].includes(c.installed_attempt)))throw Error('INSTALLED_ATTEMPT_SCOPE');
  const claim=path.join(ledger,c.simulated===true?'data.json':`data-${c.installed_attempt}.json`);const nonce=require('node:crypto').randomUUID();
  return (side,executable,argv)=>{if(!['A','B'].includes(side))throw Error('DATA_DISPATCH_SIDE');const receipt={scope:c.work_package,run:c.run,window:path.resolve(directory),side,host_pid:process.pid,kind:'DATA_DISPATCH_INTENT',remote_execution:'UNOBSERVED_AT_INTENT',executable,argv_sha256:require('node:crypto').createHash('sha256').update(JSON.stringify(argv)).digest('hex'),host_monotonic_ns:String(process.hrtime.bigint()),simulated:c.simulated===true};
   receipt.node_nonce=nonce;
   if(c.simulated!==true)receipt.attempt=c.installed_attempt;
   if(side==='A'){fs.mkdirSync(ledger,{recursive:true});fs.writeFileSync(claim,JSON.stringify(receipt,null,2),{flag:'wx'});fs.writeFileSync(path.join(directory,'DATA_ATTEMPT_STARTED'),JSON.stringify(receipt,null,2),{flag:'wx'});}
   else {const owner=JSON.parse(fs.readFileSync(claim,'utf8'));if(owner.run!==c.run||owner.window!==path.resolve(directory)||owner.node_nonce!==nonce||owner.host_pid!==process.pid)throw Error('FOREIGN_OR_RESTARTED_DISPATCH');}
   fs.writeFileSync(path.join(directory,`data-dispatch-${side}.json`),JSON.stringify(receipt,null,2),{flag:'wx'});};}
 const scope='N10_OBSERVED_RETRY_AND_CONDITIONAL_N100';
 if(c.work_package!==scope)return ()=>{};
 const task=path.resolve(directory,'../..'),nonce=require('node:crypto').randomUUID();
 if(path.basename(path.dirname(directory))!=='windows'||path.resolve(c.window_root)!==path.resolve(directory)||path.resolve(c.task_contract.task_root)!==task||![10,100].includes(c.cycles))throw Error('DATA_DISPATCH_SCOPE');
 const ledger=path.join(task,'observed-retry','attempt-ledger'),claim=path.join(ledger,`data-N${c.cycles}.json`);
 return (side,executable,argv)=>{
  if(!['A','B'].includes(side))throw Error('DATA_DISPATCH_SIDE');
  const receipt={scope,run:c.run,window:path.resolve(directory),cycles:c.cycles,side,host_pid:process.pid,node_nonce:nonce,kind:'DATA_DISPATCH_INTENT',remote_execution:'UNOBSERVED_AT_INTENT',executable,argv_sha256:require('node:crypto').createHash('sha256').update(JSON.stringify(argv)).digest('hex'),host_monotonic_ns:String(process.hrtime.bigint())};
  if(side==='A'){
   fs.mkdirSync(ledger,{recursive:true});fs.writeFileSync(claim,JSON.stringify(receipt,null,2),{flag:'wx'});
   fs.writeFileSync(path.join(directory,'DATA_ATTEMPT_STARTED'),JSON.stringify(receipt,null,2),{flag:'wx'});
  }else{
   const owner=JSON.parse(fs.readFileSync(claim,'utf8'));
   if(owner.run!==c.run||owner.window!==path.resolve(directory)||owner.node_nonce!==nonce||owner.host_pid!==process.pid)throw Error('FOREIGN_OR_RESTARTED_DISPATCH');
  }
  fs.writeFileSync(path.join(directory,`data-dispatch-${side}.json`),JSON.stringify(receipt,null,2),{flag:'wx'});
 };
}
function readBoundPrivate(c){
 const digest=b=>require('node:crypto').createHash('sha256').update(b).digest('hex');
 const proof=c.task_contract;if(!proof||!path.isAbsolute(proof.task_root))throw Error('Explicit task binding required');
 const contractBytes=fs.readFileSync(proof.freeze_path),privateBytes=fs.readFileSync(path.join(proof.task_root,'PRIVATE_CONFIG.json'));
 const frozen=JSON.parse(contractBytes),privateConfig=JSON.parse(privateBytes);
 for(const key of ['local_contract_test','execution_admitted','device_authorized','device_evidence'])if(Object.hasOwn(frozen,key)&&typeof frozen[key]!=='boolean')throw Error('Contract mode field must be a JSON boolean: '+key);
 if(digest(contractBytes)!==proof.freeze_sha256||digest(privateBytes)!==c.private_config_sha256||frozen.private_config_sha256!==c.private_config_sha256||privateConfig.task_root!==proof.task_root||privateConfig.hdc_path!==c.hdc_path||JSON.stringify(privateConfig.targets)!==JSON.stringify(c.targets))throw Error('Private identity/contract drift');
 const localInstalled=isInstalled(c)&&frozen.scope==='INSTALLED_RUNTIME_DUAL_BOARD_CONSUMER'&&frozen.local_contract_test===true;
 if(localInstalled){
  const d=frozen.local_driver,base=path.resolve(proof.task_root),sandbox=d&&path.resolve(d.sandbox_root||'');
  if(frozen.execution_admitted===true||frozen.device_authorized===true||frozen.device_evidence===true||!d||d.kind!=='LOCAL_FAKE_SANDBOX'||path.resolve(d.test_root||'')!==base||!sandbox.startsWith(base+path.sep)||!path.resolve(c.hdc_path).startsWith(base+path.sep)||path.basename(c.hdc_path)!=='fake-hdc.exe'||c.simulated!==true||c.targets.A!=='SIM-A'||c.targets.B!=='SIM-B'||typeof frozen.local_test_basis!=='string'||!frozen.local_test_basis)throw Error('Installed local contract boundary');
 }else if(frozen.execution_admitted!==true)throw Error('Independent execution gates incomplete');
 if(frozen.task_root!==proof.task_root||frozen.scope!==c.work_package)throw Error('Independent execution gates incomplete');
 const materializationBytes=fs.readFileSync(path.join(proof.task_root,'MATERIALIZATION.json'));
 if(digest(materializationBytes)!==frozen.materialization_sha256)throw Error('Materialization descriptor drift');
 const materialization=JSON.parse(materializationBytes),required=["executor-route-query/launch.ps1", "executor-route-query/prepare-query.py", "executor-route-query/query_receipt.py", "executor-route-query/route_query.py", "executor-route-query/run-window.ps1", "executor/bounded-output.cjs", "executor/capture-load.sh", "executor/load_origin.py", "executor/observe-native.cjs", "executor/prepare-window.py", "executor/private_config.py", "executor/record-pair.cjs", "executor/route_binding.py", "executor/run-window.ps1", "executor/runner-template.cjs", "executor/shared-control.cjs", "executor/task_contract.py"];
 if(materialization.scope!=='LOCAL_MATERIALIZATION_ONLY_NO_EXECUTION_AUTHORITY'||materialization.task!==proof.task_root||materialization.private_config_sha256!==c.private_config_sha256)throw Error('Materialization identity mismatch');
 const rows=materialization.files||[],names=rows.map(e=>e.destination.split(String.fromCharCode(92)).join('/')).sort();if(JSON.stringify(names)!==JSON.stringify(required))throw Error('Materialization membership mismatch');
 const declared=new Map((frozen.execution_files||[]).map(e=>[path.resolve(privateConfig.repo_root,e.path),e.sha256]));
 if(declared.size!==(frozen.execution_files||[]).length)throw Error('Duplicate executor binding');
 const taskEntries=[...declared.keys()].filter(p=>p.startsWith(path.join(proof.task_root,'executor')+path.sep)||p.startsWith(path.join(proof.task_root,'executor-route-query')+path.sep)).sort();
 if(JSON.stringify(taskEntries)!==JSON.stringify(required.map(p=>path.resolve(proof.task_root,p)).sort()))throw Error('Incomplete task execution closure');
 for(const e of rows){const file=path.resolve(proof.task_root,e.destination);if(declared.get(file)!==e.sha256||digest(fs.readFileSync(file))!==e.sha256)throw Error('Materialized executor missing/changed');}
 const external=materialization.external_files||[],externalNames=external.map(e=>e.path).sort(),guard=path.join(privateConfig.repo_root,'scripts','NativePermissionGuard.ps1');
 if(JSON.stringify(externalNames)!==JSON.stringify(['scripts/NativePermissionGuard.ps1','scripts/public_socket/analyze_shared.py']))throw Error('Required external membership mismatch');
 for(const e of external){const file=path.resolve(privateConfig.repo_root,e.path);if(declared.get(file)!==e.sha256||digest(fs.readFileSync(file))!==e.sha256)throw Error('Required external code missing/changed');}
 if(Object.keys(c.shared_script_sha256||{}).length!==1||c.shared_script_sha256['NativePermissionGuard.ps1']!==declared.get(guard))throw Error('Window external guard not bound');
 if(c.analyzer_sha256!==declared.get(path.join(privateConfig.repo_root,'scripts','public_socket','analyze_shared.py')))throw Error('Window analyzer not bound');
 if(path.dirname(path.resolve(c.window_root))!==path.join(proof.task_root,'windows'))throw Error('Foreign window');
 for(const [original,copy] of [['runner-template.cjs','runner.cjs'],['shared-control.cjs','shared-control.cjs'],['record-pair.cjs','record-pair.cjs'],['observe-native.cjs','observe-native.cjs'],['bounded-output.cjs','bounded-output.cjs']]){
  if(digest(fs.readFileSync(path.join(c.window_root,copy)))!==declared.get(path.join(proof.task_root,'executor',original)))throw Error('Window executable copy drift');
 }
 return privateConfig;
}
async function main(){
 const c=JSON.parse(fs.readFileSync(path.join(__dirname,'window.json'),'utf8'));const commands=buildCommands(c,readBoundPrivate(c).tokens);
 const {runRecordPair}=require('./record-pair.cjs');const {createLoadObserver}=require('./observe-native.cjs');const {createBoundedSink,createLines}=require('./bounded-output.cjs');
 const release=createControlRelease(c,__dirname);const controlReceipts=release.receipts;
 const observer=createLoadObserver({config:c,directory:__dirname});const controller=createController(c,release,observer.admission);
 const fds={},sinks={},lines={};for(const side of ['A','B']){fds[side]={stdout:fs.openSync(path.join(__dirname,side+'.raw.stdout.log'),'wx'),stderr:fs.openSync(path.join(__dirname,side+'.raw.stderr.log'),'wx')};sinks[side]=createBoundedSink({cap:c.stdout_budget,write:()=>{},overflow:()=>controller.stop('OUTPUT_OVERFLOW_'+side)});lines[side]=createLines(line=>controller.line(side,line),reason=>controller.stop(reason+'_'+side));}
 fs.writeFileSync(path.join(__dirname,'commands.json'),JSON.stringify(commands,null,2),{flag:'wx'});
 const transitions=[];const result=await runRecordPair({onBeforeStart:createDataDispatcher(c,__dirname),executable:c.hdc_path,targets:c.targets,commands,timeoutMs:(c.timeout_seconds+15)*1000,drainGraceMs:250,mainLineRegex:mainLinePattern(c,'A'),env:{...process.env,Path:path.dirname(c.hdc_path)+';'+(process.env.Path||process.env.PATH||'')},onTransition:e=>{transitions.push(e);if(e.event==='TERMINATION_REQUEST'||e.event==='TERMINATION_RESULT')fs.writeFileSync(path.join(__dirname,`business-${e.role}-${e.event.toLowerCase()}.json`),JSON.stringify({run:c.run,side:e.role,phase:'WINDOW_COLLECTION',...e},null,2),{flag:'wx'});if(e.event==='OUTPUT_ERROR'||e.event==='EXIT_DRAIN_INCOMPLETE'||e.event==='COLLECTOR_ERROR'||e.event==='DISPATCH_REFUSED_OR_SPAWN_FAILED')controller.stop(e.event+'_'+e.role+(e.error?':'+e.error:''));},onStreamEnd:(side,confirmed)=>{lines[side].end(confirmed);observer.end(side,confirmed);controller.end(side,confirmed);},onOutput:(side,stream,data)=>{const before=sinks[side].state().retained;sinks[side].feed(data);const n=sinks[side].state().retained-before;if(n)fs.writeSync(fds[side][stream],data.subarray(0,n));observer.output(side,stream,data);if(stream==='stdout')lines[side](data);}});
 const phase=await controller.finish(),loads=await observer.finish();for(const side of ['A','B'])for(const fd of Object.values(fds[side]))fs.closeSync(fd);
 const out={...result,run:c.run,scenario:c.scenario,activeCollectors:result.activeCollectors.map(({role,pid})=>({role,pid})),controller:phase,loads,transitions,controls:controlReceipts,log_retention:Object.fromEntries(['A','B'].map(s=>[s,sinks[s].state()])),pass:result.pass&&phase.pass&&Object.values(loads).every(x=>x.pass)&&Object.values(sinks).every(x=>x.state().complete)};
 fs.writeFileSync(path.join(__dirname,'result.json'),JSON.stringify(out,null,2),{flag:'wx'});process.exitCode=out.pass?0:1;
}
module.exports={readBoundPrivate,createDataDispatcher,createControlRelease,buildCommands,createController,phases,CASES,isInstalled,installedReceipts,INSTALLED_STAGES};
if(require.main===module)main().catch(e=>{console.error(e.stack||String(e));process.exitCode=1;});
