'use strict';
const fs=require('node:fs'),path=require('node:path');const {createDataDispatcher}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const task=process.argv[2],name=process.argv[3],run=name==='one'?'1'.repeat(16):'2'.repeat(16),directory=path.join(task,'windows',name);fs.mkdirSync(directory,{recursive:true});
const c={run,cycles:10,work_package:'N10_OBSERVED_RETRY_AND_CONDITIONAL_N100',window_root:directory,task_contract:{task_root:task}};
try{createDataDispatcher(c,directory)('A',process.execPath,['LOCAL_UNIT_NO_REMOTE_LAUNCH']);console.log('CLAIMED_LOCAL_UNIT');}catch(e){console.error(e.code||e.message);process.exitCode=11;}
