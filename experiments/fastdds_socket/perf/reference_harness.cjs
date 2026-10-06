'use strict';
// Actual host DDS, separate A/B processes. Controller carries metadata only.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),dgram=require('node:dgram');
const {spawn}=require('node:child_process');
const cap=8*1024*1024;
async function main(){
  const defaultPlan=path.resolve(__dirname,'../../../tests/fixtures/performance/MEASUREMENT_PLAN.json');
  const [exe,mode,destination,contract='HOST_CONTRACT',repeatText='1',planFile=defaultPlan,schemaFile=path.join(path.dirname(planFile),'LOG_SCHEMA.json')]=process.argv.slice(2);
  if(!exe||!['TRACE','MEASURE'].includes(mode)||!destination||!['HOST_CONTRACT','FULL'].includes(contract)||!['1','2','3'].includes(repeatText))throw Error('exe TRACE|MEASURE output HOST_CONTRACT|FULL repeat');
  const repeat=Number(repeatText),run=crypto.randomBytes(8).toString('hex');
  if(mode==='TRACE'&&repeat!==1)throw Error('TRACE order is frozen at repeat 1');
  let output=destination;
  if(destination.startsWith('--root=')){const root=path.resolve(destination.slice(7));fs.mkdirSync(root,{recursive:true});output=path.join(root,mode+'-'+Date.now()+'-'+run);}
  fs.mkdirSync(output);output=path.resolve(output);
  const dirs={};for(const side of ['A','B']){dirs[side]=path.join(output,side+'-control');fs.mkdirSync(dirs[side]);}
  let base;
  for(let attempt=0;attempt<16&&!base;attempt++){
    const candidate=24000+crypto.randomInt(16000),sockets=[];
    try{for(const offset of [0,2,10,12]){const s=dgram.createSocket('udp4');sockets.push(s);await new Promise((resolve,reject)=>{s.once('error',reject);s.bind(candidate+offset,'127.0.0.1',resolve);});}base=candidate;}
    catch{}finally{for(const s of sockets)try{s.close();}catch{}}
  }
  if(!base)throw Error('No UDP ports');
  const names=['RTT_1K','LOAD_1K','RTT_4K','LOAD_4K','RTT_256K','LOAD_256K'];
  const order=names.slice((repeat-1)*2).concat(names.slice(0,(repeat-1)*2));
  const stages=[];for(const name of order)for(const phase of mode==='MEASURE'?['WARMUP','FORMAL']:['FORMAL'])stages.push({case:name,phase});
  const config={schema:1,run,mode,repeat,scope:contract==='HOST_CONTRACT'?'HOST_CONTRACT':'HOST_REFERENCE_UDP',native_proof:'NONE',clock_unit:'ns',executable:path.resolve(exe),base_port:base,host_contract:contract==='HOST_CONTRACT',stages,control_payload_bytes:0};
  // Actual observed input/artifact bytes are bound before either child launches.
  // A live source hash cannot attest what was compiled: no such claim is made.
  const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
  const planBytes=fs.readFileSync(path.resolve(planFile)),schemaBytes=fs.readFileSync(path.resolve(schemaFile));
  fs.writeFileSync(path.join(output,'measurement-plan.json'),planBytes,{flag:'wx'});
  fs.writeFileSync(path.join(output,'log-schema.json'),schemaBytes,{flag:'wx'});
  config.measurement_plan_sha256=sha(planBytes);config.log_schema_sha256=sha(schemaBytes);
  config.executable_sha256=sha(fs.readFileSync(path.resolve(exe)));
  config.build_manifest_sha256=null;config.source_state_sha256=null;
  config.compiled_source_identity='UNOBSERVED_NO_BUILD_TIME_RECEIPT';
  config.package_artifacts=[{name:path.basename(exe),sha256:config.executable_sha256}];
  for(const name of fs.readdirSync(path.dirname(path.resolve(exe))).filter(n=>/^(fastdds|fastcdr|foonathan_memory)[-0-9.]*\.dll$/i.test(n)).sort())
    config.package_artifacts.push({name,sha256:sha(fs.readFileSync(path.join(path.dirname(path.resolve(exe)),name)))});
  config.package_artifacts_scope='PRELAUNCH_FILE_HASHES_NOT_LOADED_MODULE_OR_COMPILED_SOURCE_PROOF';
  if(contract==='HOST_CONTRACT'){
    config.contract_window_ns=200000000;
    config.contract_counts=Object.fromEntries(names.map((name,i)=>[name,{WARMUP:mode==='MEASURE'?2:0,FORMAL:[3,10,3,10,2,2][i]}]));
  }
  for(const field of ['measurement_plan_sha256','log_schema_sha256','executable_sha256']) {
    if(!/^[0-9a-f]{64}$/.test(config[field]||''))throw Error('Missing pre-run provenance: '+field);
  }
  if(!Array.isArray(config.package_artifacts)||!config.package_artifacts.length)throw Error('Missing pre-run package hashes');
  fs.writeFileSync(path.join(output,'window.json'),JSON.stringify(config,null,2));
  const control=fs.createWriteStream(path.join(output,'control.jsonl'),{flags:'wx'});
  const children={},results={},streams=[],totals={A:0,B:0},overflow={A:0,B:0},buffers={A:'',B:''};
  const seen={A:new Set(),B:new Set()},ready=new Set();let current=0,failure=null,watchdog=false;
  function record(event){control.write(JSON.stringify({...event,controller_ns:process.hrtime.bigint().toString()})+'\n');}
  function publish(side,suffix,body){const target=path.join(dirs[side],suffix);fs.writeFileSync(target+'.tmp',body,{flag:'wx'});fs.renameSync(target+'.tmp',target);record({action:'release',side,suffix});}
  function abort(reason){if(failure)return;failure=reason;record({action:'abort',reason});for(const side of ['A','B'])try{publish(side,'ABORT.release','PERF_ABORT '+run+'\n');}catch{}}
  function release(side){const s=stages[current];publish(side,`stage-${current}.release`,`PERF_RELEASE ${run} ${current} ${s.case} ${s.phase}\n`);}
  function line(side,text){
    if(!text.startsWith('PERF_CONTROL '))return;
    const fields=Object.fromEntries(text.slice(13).trim().split(/ +/).map(v=>v.split('=')));
    record({action:'receipt',side,...fields});
    if(fields.side!==side||fields.run!==run){abort('receipt_identity');return;}
    if(fields.event==='FAILURE'){abort('fixture_failure');return;}
    if(fields.event==='CLOSED')return;
    if(fields.event==='READY'){
      if(ready.has(side)){abort('duplicate_ready');return;}ready.add(side);if(ready.size===2)release('B');return;
    }
    if(!stages[current]||Number(fields.stage)!==current||fields.case!==stages[current].case||fields.phase!==stages[current].phase){abort('stage_identity_order');return;}
    if(seen[side].has(fields.event)){abort('duplicate_receipt');return;}seen[side].add(fields.event);
    if(fields.event==='B_ARMED'){
      if(side!=='B'){abort('wrong_arm_side');return;}release('A');
    }else if(fields.event==='A_DONE'||fields.event==='B_DONE'){
      if(fields.event!==side+'_DONE'||!seen.B.has('B_ARMED')){abort('done_without_arm');return;}
      if(seen.A.has('A_DONE')&&seen.B.has('B_DONE'))for(const s of ['A','B'])publish(s,`stage-${current}.flush`,`PERF_FLUSH ${run} ${current}\n`);
    }else if(fields.event==='FLUSHED'){
      if(!seen.A.has('A_DONE')||!seen.B.has('B_DONE')){abort('premature_flush');return;}
      if(seen.A.has('FLUSHED')&&seen.B.has('FLUSHED')){++current;seen.A.clear();seen.B.clear();if(current<stages.length)release('B');}
    }else abort('unexpected_receipt');
  }
  const timer=setTimeout(()=>{watchdog=true;abort('watchdog_210s');for(const child of Object.values(children))child.kill();},210000);
  await Promise.all(['A','B'].map(side=>new Promise(resolve=>{
    const out=fs.createWriteStream(path.join(output,side+'.raw.stdout.log'),{flags:'wx'}),err=fs.createWriteStream(path.join(output,side+'.raw.stderr.log'),{flags:'wx'});streams.push(out,err);
    const args=[run,side,dirs[side],mode,repeatText,String(base),contract];
    fs.writeFileSync(path.join(output,side+'.command.json'),JSON.stringify([path.resolve(exe),...args]));
    const child=children[side]=spawn(path.resolve(exe),args,{cwd:dirs[side],env:{...process.env,SKIP_DEFAULT_XML:'1'},windowsHide:true,shell:false});let error=null,stderrBytes=0;
    child.on('error',e=>{error=String(e);abort('spawn_error');});
    child.stdout.on('data',data=>{
      const remaining=Math.max(0,cap-totals[side]),kept=data.subarray(0,remaining);totals[side]+=kept.length;overflow[side]+=data.length-kept.length;out.write(kept);
      if(overflow[side])abort('stdout_overflow');
      // Always drain, and retain only a bounded line fragment.
      buffers[side]+=data.toString('utf8');let index;
      while((index=buffers[side].indexOf('\n'))>=0){const row=buffers[side].slice(0,index).replace(/\r$/,'');buffers[side]=buffers[side].slice(index+1);line(side,row);}
      if(buffers[side].length>65536){buffers[side]='';abort('line_overflow');}
    });
    child.stderr.on('data',data=>{const n=Math.min(data.length,Math.max(0,cap-stderrBytes));err.write(data.subarray(0,n));stderrBytes+=n;if(n<data.length)abort('stderr_overflow');});
    child.on('close',(exit_code,signal)=>{results[side]={exit_code,signal,error,pid:child.pid};if(exit_code!==0)abort('child_exit');resolve();});
  })));
  clearTimeout(timer);await Promise.all([...streams,control].map(s=>new Promise(resolve=>s.end(resolve))));
  const pass=!failure&&!watchdog&&current===stages.length&&Object.values(results).every(r=>r.exit_code===0&&!r.error);
  const result={schema:1,run,mode,scope:config.scope,native_proof:'NONE',pass,failure,watchdog,processes:results,completed_stages:current,stdout_bytes:totals,log_overflow_bytes:overflow};
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({...result,output}));process.exitCode=pass?0:1;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
