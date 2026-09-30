'use strict';
// Host-only actual DDS characterization. This controller transfers phase receipts only.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),dgram=require('node:dgram');
const {spawn}=require('node:child_process');
const {createMatrixController}=require('../../../scripts/MatrixPhaseController.cjs');
async function main(){
  const [exe,caseId,destination,traceFixture,python]=process.argv.slice(2);if(!exe||!caseId||!destination)throw Error('exe case fresh-output [trace-fixture python] required');
  if(Boolean(traceFixture)!==Boolean(python))throw Error('trace-fixture and Python must be provided together');
  let output=destination;
  if(destination.startsWith('--root=')){const parent=path.resolve(destination.slice(7));fs.mkdirSync(parent,{recursive:true});output=path.join(parent,caseId+'-'+Date.now()+'-'+crypto.randomBytes(4).toString('hex'));}
  let matrixBytes=null,matrixHash=null;
  if(traceFixture){const fixture=JSON.parse(fs.readFileSync(traceFixture,'utf8'));matrixBytes=Buffer.from(fixture.current_matrix_base64,'base64');matrixHash=crypto.createHash('sha256').update(matrixBytes).digest('hex');if(matrixHash!==fixture.current_matrix_sha256)throw Error('Current matrix hash mismatch');if(!JSON.parse(matrixBytes).cases.some(x=>x.case_id===caseId))throw Error('Case not in current matrix');}
  fs.mkdirSync(output);const run=crypto.randomBytes(8).toString('hex');let base;
  for(let attempt=0;attempt<8&&!base;attempt++){const candidate=24000+crypto.randomInt(16000),held=[];try{for(const offset of [0,2,10,12]){const s=dgram.createSocket('udp4');held.push(s);await new Promise((r,j)=>{s.once('error',j);s.bind(candidate+offset,'127.0.0.1',r);});}base=candidate;}catch{}finally{for(const s of held)try{s.close();}catch{}}}if(!base)throw Error('No free ports');
  const dirs={};for(const s of ['A','B']){dirs[s]=path.resolve(output,s+'-control');fs.mkdirSync(dirs[s]);}
  if(matrixBytes)fs.writeFileSync(path.join(output,'case-matrix.json'),matrixBytes,{flag:'wx'});
  fs.writeFileSync(path.join(output,'window.json'),JSON.stringify({run,case_id:caseId,mode:'HOST_REFERENCE_UDP',registered_type:'DSoftBusKeyedSample',native_proof:'NONE',executable:path.resolve(exe),base_port:base,matrix_sha256:matrixHash},null,2));
  const ctlFile=fs.createWriteStream(path.join(output,'control.jsonl'),{flags:'wx'});
  async function publish(side,phase,body){const p=path.join(dirs[side],phase+'.release');await fs.promises.writeFile(p+'.tmp',body,{flag:'wx'});await fs.promises.rename(p+'.tmp',p);}
  const ctl=createMatrixController({run,caseId,release:publish,abort:(s,b)=>publish(s,'ABORT',b),record:e=>ctlFile.write(JSON.stringify(e)+'\n')});
  const children={},results={},streams=[];let watchdog=false;const timer=setTimeout(()=>{watchdog=true;for(const c of Object.values(children))c.kill();},210000);
  await Promise.all(['A','B'].map(side=>new Promise(resolve=>{
    const args=[run,caseId,side,dirs[side],String(base)],out=fs.createWriteStream(path.join(output,side+'.raw.stdout.log'),{flags:'wx'}),err=fs.createWriteStream(path.join(output,side+'.raw.stderr.log'),{flags:'wx'});streams.push(out,err);
    fs.writeFileSync(path.join(output,side+'.command.json'),JSON.stringify([path.resolve(exe),...args]));
    const c=children[side]=spawn(path.resolve(exe),args,{cwd:dirs[side],env:{...process.env,SKIP_DEFAULT_XML:'1'},windowsHide:true,shell:false});let error=null;
    c.on('error',e=>error=String(e));c.stdout.on('data',d=>{out.write(d);ctl.output(side,'stdout',d);});c.stderr.on('data',d=>err.write(d));c.on('close',(exit_code,signal)=>{results[side]={exit_code,signal,error,pid:c.pid};resolve();});
  })));
  clearTimeout(timer);const control=await ctl.finish();await Promise.all([...streams,ctlFile].map(s=>new Promise(r=>s.end(r))));
  const pass=!watchdog&&control.pass&&Object.values(results).every(s=>s.exit_code===0&&!s.error);
  const receipt={run,case_id:caseId,scope:'HOST_REFERENCE_UDP_NOT_DSOFTBUS',pass,watchdog,processes:results,control};fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(receipt,null,2));
  let analysisExit=null;
  if(traceFixture){
    const args=[path.resolve(__dirname,'../../../scripts/analyze_dds_keyed.py'),path.resolve(output),'--matrix',path.resolve(output,'case-matrix.json'),'--output',path.resolve(output,'keyed-analysis.json'),'--allow-host-reference'];
    fs.writeFileSync(path.join(output,'analysis-command.json'),JSON.stringify([python,...args]));
    analysisExit=await new Promise(resolve=>{const out=fs.createWriteStream(path.join(output,'analysis.stdout.log'),{flags:'wx'}),err=fs.createWriteStream(path.join(output,'analysis.stderr.log'),{flags:'wx'});const c=spawn(python,args,{shell:false,windowsHide:true});let error=false;c.on('error',e=>{error=true;err.write(String(e));});c.stdout.on('data',d=>out.write(d));c.stderr.on('data',d=>err.write(d));c.on('close',code=>out.end(()=>err.end(()=>resolve(error?1:code))));});
  }
  const gate={...receipt,analysis_exit:analysisExit,pass:pass&&(!traceFixture||analysisExit===0),gate_scope:traceFixture?'CPP_AND_OFFLINE_ANALYZER':'CPP_CHARACTERIZATION_ONLY'};
  fs.writeFileSync(path.join(output,'reference-gate.json'),JSON.stringify(gate,null,2));console.log(JSON.stringify({...gate,output}));process.exitCode=gate.pass?0:1;
}
main().catch(e=>{console.error(e);process.exitCode=1;});
