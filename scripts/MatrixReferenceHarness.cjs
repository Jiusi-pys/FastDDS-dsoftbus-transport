'use strict';
// Real Fast DDS reference processes only. No HDC, DSoftBus or permissions.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),dgram=require('node:dgram');
const {spawn}=require('node:child_process');
const {createMatrixController}=require('./MatrixPhaseController.cjs');
function argumentsOf(argv){
  const out={};for(let i=0;i<argv.length;i+=2){if(!argv[i].startsWith('--')||i+1===argv.length||out[argv[i]])throw Error('Invalid reference arguments');out[argv[i]]=argv[i+1];}
  for(const k of ['--exe','--case'])if(!out[k])throw Error('Missing '+k);
  if(Number(!!out['--matrix'])+Number(!!out['--trace-fixture'])!==1)throw Error('Exactly one explicit matrix contract source required');
  if(Number(!!out['--output'])+Number(!!out['--output-root'])!==1)throw Error('Exactly one output destination form required');
  if(Object.keys(out).some(k=>!['--exe','--case','--matrix','--trace-fixture','--output','--output-root','--python'].includes(k)))throw Error('Unknown option');return out;
}
async function freeBase(){
  for(let attempt=0;attempt<8;attempt++){
    const base=24000+crypto.randomInt(16000),held=[];
    try{
      for(const offset of [0,1,2,3,10,11,12,13]){
        const socket=dgram.createSocket('udp4');held.push(socket);
        await new Promise((resolve,reject)=>{socket.once('error',reject);socket.bind(base+offset,'127.0.0.1',resolve);});
      }
      for(const socket of held)await new Promise(resolve=>socket.close(resolve));return base;
    }catch(error){for(const socket of held)try{socket.close();}catch{} }
  }
  throw Error('No free bounded reference port group');
}
async function runReference(args){
  const exe=path.resolve(args['--exe']),caseId=args['--case'];
  let matrixBytes,contractOrigin;
  if(args['--matrix']){contractOrigin=path.resolve(args['--matrix']);matrixBytes=fs.readFileSync(contractOrigin);}
  else {
    contractOrigin=path.resolve(args['--trace-fixture']);const fixture=JSON.parse(fs.readFileSync(contractOrigin,'utf8'));
    matrixBytes=Buffer.from(fixture.matrix_base64,'base64');
    if(crypto.createHash('sha256').update(matrixBytes).digest('hex')!==fixture.matrix_sha256)throw Error('Frozen source contract hash mismatch');
  }
  const matrix=JSON.parse(matrixBytes.toString('utf8'));
  if(!matrix.cases.some(c=>c.case_id===caseId))throw Error('Case not in frozen matrix');
  if(!fs.statSync(exe).isFile())throw Error('Missing reference executable');
  let output;
  if(args['--output'])output=path.resolve(args['--output']);
  else {const parent=path.resolve(args['--output-root']);fs.mkdirSync(parent,{recursive:true});output=path.join(parent,caseId+'-'+Date.now()+'-'+crypto.randomBytes(4).toString('hex'));}
  fs.mkdirSync(output);const run=crypto.randomBytes(8).toString('hex'),base=await freeBase();
  const matrixPath=path.join(output,'case-matrix.json');fs.writeFileSync(matrixPath,matrixBytes,{flag:'wx'});
  const dirs=Object.fromEntries(['A','B'].map(s=>[s,path.join(output,s+'-control')]));for(const dir of Object.values(dirs))fs.mkdirSync(dir);
  const cfg={run,case_id:caseId,mode:'HOST_REFERENCE_UDP',native_proof:'NONE',base_port:base,executable:exe,
    matrix:matrixPath,contract_origin:contractOrigin,matrix_sha256:crypto.createHash('sha256').update(matrixBytes).digest('hex'),control_dirs:dirs};
  fs.writeFileSync(path.join(output,'window.json'),JSON.stringify(cfg,null,2),{flag:'wx'});
  const events=fs.createWriteStream(path.join(output,'control.jsonl'),{flags:'wx'}),start=process.hrtime.bigint();
  const record=e=>events.write(JSON.stringify({...e,host_monotonic_ns:String(process.hrtime.bigint()-start)})+'\n');
  async function publish(side,phase,body){
    if(!['PRE','CHANGE','POST','OBSERVE','CLEANUP','ABORT'].includes(phase))throw Error('Invalid phase');
    const final=path.join(dirs[side],phase+'.release'),temp=final+'.tmp';
    if(fs.existsSync(final))throw Error('Release already exists');
    await fs.promises.writeFile(temp,body,{flag:'wx'});await fs.promises.rename(temp,final);
  }
  const control=createMatrixController({run,caseId,release:publish,abort:(s,b)=>publish(s,'ABORT',b),record});
  const states={},results={},streams=[];let timedOut=false;
  const timer=setTimeout(()=>{timedOut=true;record({event:'HOST_REFERENCE_SAFETY_TIMEOUT'});for(const s of Object.values(states))s.kill();},210000);
  const jobs=['A','B'].map(side=>new Promise(resolve=>{
    const command=[exe,run,caseId,side,dirs[side],String(base)];
    fs.writeFileSync(path.join(output,side+'.command.json'),JSON.stringify(command,null,2),{flag:'wx'});
    const stdout=fs.createWriteStream(path.join(output,side+'.raw.stdout.log'),{flags:'wx'}),stderr=fs.createWriteStream(path.join(output,side+'.raw.stderr.log'),{flags:'wx'});streams.push(stdout,stderr);
    const child=states[side]=spawn(exe,command.slice(1),{cwd:dirs[side],env:{...process.env,SKIP_DEFAULT_XML:'1'},shell:false,windowsHide:true,stdio:['ignore','pipe','pipe']});
    let error=null;child.on('error',e=>{error=String(e);});
    child.stdout.on('data',data=>{stdout.write(data);control.output(side,'stdout',data);});
    child.stderr.on('data',data=>stderr.write(data));
    child.on('close',(code,signal)=>{results[side]={exit_code:code,signal,error,pid:child.pid};resolve();});
  }));
  await Promise.all(jobs);clearTimeout(timer);const controlled=await control.finish();
  await Promise.all(streams.map(s=>new Promise(resolve=>s.end(resolve))));await new Promise(resolve=>events.end(resolve));
  const result={scope:'REAL_FASTDDS_HOST_REFERENCE_UDP_NOT_DSOFTBUS',run,case:caseId,timedOut,processes:results,control:controlled,
    pass:!timedOut&&controlled.pass&&['A','B'].every(s=>results[s].exit_code===0&&!results[s].error),device_tested:false};
  fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(result,null,2),{flag:'wx'});
  const analysisCommand=[args['--python']||'python',path.join(__dirname,'analyze_dds_matrix.py'),output,
    '--matrix',matrixPath,'--output',path.join(output,'matrix-analysis.json'),'--allow-host-reference'];
  fs.writeFileSync(path.join(output,'analysis-command.json'),JSON.stringify(analysisCommand,null,2),{flag:'wx'});
  const analysisExit=await new Promise(resolve=>{
    const out=fs.createWriteStream(path.join(output,'analysis.stdout.log'),{flags:'wx'}),err=fs.createWriteStream(path.join(output,'analysis.stderr.log'),{flags:'wx'});
    const child=spawn(analysisCommand[0],analysisCommand.slice(1),{shell:false,windowsHide:true});
    let failed=false;child.on('error',e=>{failed=true;err.write(String(e));});child.stdout.on('data',d=>out.write(d));child.stderr.on('data',d=>err.write(d));
    child.on('close',code=>{out.end(()=>err.end(()=>resolve(failed?1:code)));});
  });
  const final={pass:result.pass&&analysisExit===0,run,case:caseId,analysis_exit:analysisExit,scope:result.scope};
  fs.writeFileSync(path.join(output,'reference-gate.json'),JSON.stringify(final,null,2),{flag:'wx'});
  console.log(JSON.stringify({case:caseId,run,pass:final.pass,analysis_exit:analysisExit,output,control:controlled.first_failure,processes:results}));
  return final;
}
if(require.main===module)runReference(argumentsOf(process.argv.slice(2))).then(r=>{process.exitCode=r.pass?0:1;}).catch(e=>{console.error(e);process.exitCode=1;});
module.exports={runReference};
