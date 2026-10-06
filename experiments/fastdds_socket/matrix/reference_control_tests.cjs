'use strict';
// Actual fixture rejection tests. L1 side A legitimately has no initial Reader,
// so these metadata contracts do not need a fabricated DDS peer or sample.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {spawn}=require('node:child_process');
const exe=process.argv[2];if(!exe)throw Error('matrix_reference executable required');
const root=fs.mkdtempSync(path.join(os.tmpdir(),'matrix-control-contract-'));
const run='1122334455667788',caseId='L1';
const release=phase=>`MATRIX_RELEASE ${run} ${caseId} ${phase}\n`;
const cases=[
 ['wrong_run','PRE',`MATRIX_RELEASE 1122334455667789 ${caseId} PRE\n`,'release_content_PRE'],
 ['wrong_case','PRE',`MATRIX_RELEASE ${run} M1 PRE\n`,'release_content_PRE'],
 ['wrong_phase','PRE',release('CHANGE'),'release_content_PRE'],
 ['malformed','PRE',release('PRE').trim(),'release_content_PRE'],
 ['future_phase','POST',release('POST'),'release_out_of_order_POST'],
 ['abort','ABORT',`MATRIX_ABORT ${run} ${caseId}\n`,'controller_abort'],
 ['consumed_replaced',null,null,'release_replaced_PRE'],
];
(async()=>{
 let failures=0;
 for(let n=0;n<cases.length;n++){
  const [name,phase,content,wanted]=cases[n],dir=path.join(root,name);fs.mkdirSync(dir);
  if(phase)fs.writeFileSync(path.join(dir,phase+'.release'),content);
  const child=spawn(exe,[run,caseId,'A',dir,String(31000+n*20)],{windowsHide:true});
  let stdout='',stderr='',sent=false,replaced=false;
  child.stdout.on('data',data=>{
   stdout+=data;
   if(name==='consumed_replaced'&&!sent&&/event=PHASE_DONE[^\r\n]*phase=INITIAL\r?\n/.test(stdout)){
    sent=true;fs.writeFileSync(path.join(dir,'PRE.release'),release('PRE'));
   }
   if(name==='consumed_replaced'&&!replaced&&/event=PHASE_DONE[^\r\n]*phase=PRE\r?\n/.test(stdout)){
    replaced=true;const file=path.join(dir,'PRE.release');fs.writeFileSync(file+'.replacement',release('PRE'));
    fs.utimesSync(file+'.replacement',new Date(),new Date(Date.now()+2000));fs.renameSync(file+'.replacement',file);
    fs.writeFileSync(path.join(dir,'CHANGE.release'),release('CHANGE'));
    setTimeout(()=>{if(child.exitCode===null)fs.writeFileSync(path.join(dir,'ABORT.release'),`MATRIX_ABORT ${run} ${caseId}\n`);},400);
   }
  });
  child.stderr.on('data',data=>stderr+=data);
  const timeout=setTimeout(()=>child.kill(),5000);
  const code=await new Promise(resolve=>child.once('exit',resolve));clearTimeout(timeout);
  fs.writeFileSync(path.join(dir,'stdout.log'),stdout);fs.writeFileSync(path.join(dir,'stderr.log'),stderr);
  const pass=code===1&&stdout.includes(`predicate=${wanted} `)&&!stdout.includes('MATRIX_REFERENCE scope=HOST_REFERENCE_UDP result=PASS');
  console.log(`test=${name} result=${pass?'PASS':'FAIL'} exit=${code} logs=${dir}`);if(!pass)++failures;
 }
 console.log(`checks=${cases.length} failures=${failures}`);process.exitCode=failures?1:0;
})().catch(error=>{console.error(error);process.exitCode=1;});
