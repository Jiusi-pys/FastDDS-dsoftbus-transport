'use strict';
const assert=require('node:assert/strict'),cp=require('node:child_process'),path=require('node:path');
const {runRecordPair}=require('../../scripts/public_socket/record-pair.cjs');
const helper=path.resolve(__dirname,'local-child.cjs');
(async()=>{let launched=0;const result=await runRecordPair({executable:process.execPath,targets:{A:'LOCAL_A',B:'LOCAL_B'},commands:{A:'LOCAL',B:'LOCAL'},timeoutMs:1000,drainGraceMs:250,onBeforeStart(){throw Error('LOCAL_VALIDATION_REFUSED');},spawn(exe,args,options){assert.equal(exe,process.execPath);launched++;return cp.spawn(process.execPath,[helper,'pair-success'],options);}});assert.equal(launched,0,'local admission must reject before capable dispatch');assert.equal(result.pass,false);console.log('PASS no local-validation dispatch');})().catch(e=>{console.error(e);process.exitCode=1;});
