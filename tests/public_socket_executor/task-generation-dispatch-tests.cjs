'use strict';
// Actual formal Node functions; all I/O/child boundaries are inert, no claims
// or processes are created. Python sequence/route checks remain mandatory.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo=path.resolve(__dirname,'../..'),source=path.join(repo,'scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const text=fs.readFileSync(source,'utf8'),start=text.indexOf('function createDataDispatcher(c,directory){'),end=text.indexOf('function readBoundPrivate(c){');
assert(start>=0&&end>start);const code=text.slice(start,end);
const grantPath=path.join(repo,'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/AUTHORIZATION.json');
if(!fs.existsSync(grantPath)){console.log('SKIP: external immutable task grant absent; not device evidence');process.exitCode=77;}
else{
const grantBytes=fs.readFileSync(grantPath),grant=JSON.parse(grantBytes),hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(hash(grantBytes),'1dbadad0591bad3f76ef98a6bab09ff80ee6b75491e54c447410c841b39bea51');
const root=path.join(grant.task_root,'iterations','iteration-5'),window=path.join(root,'windows','UNIT_IN_MEMORY_ONLY'),contractPath=path.join(root,'UNIT_CONTRACT_NOT_WRITTEN.json');
const stages=['LISTENER_READY','RELIABLE_READY','RELIABLE_DONE','BEST_EFFORT_READY','BEST_EFFORT_DONE','TEARDOWN_READY','DDS_DELETED'];
const frozen={task_root:root,task_authority_root:grant.task_root,execution_iteration_id:5,task_id:grant.task_id,authorization:grant.authorization,
 scope:'ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE',device_authorized:true,authorization_record:{path:grantPath,sha256:hash(grantBytes)},limits:grant.per_window_limits};
const bytes=Buffer.from(JSON.stringify(frozen));
function test(attempt){
 const c={scenario:'ROS2_DSOFTBUS',work_package:frozen.scope,main_mode:'dual',run:'aaaaaaaaaaaaaaaa',repo,ros_attempt:attempt,ros_authorization_mode:grant.authorization,ros_task_id:grant.task_id,
  task_authority_root:grant.task_root,targets:grant.targets,window_root:window,task_contract:{task_root:root,freeze_path:contractPath,freeze_sha256:hash(bytes)},
  timeout_seconds:300,record_count:20,stdout_budget:25165824,max_record_bytes:8192,stages};
 const files=new Map([[grantPath,grantBytes],[contractPath,bytes],[path.join(window,'window.json'),Buffer.from(JSON.stringify(c))]]),checks=[];
 const inertFs={readFileSync:(name)=>{const value=files.get(String(name));if(!value)throw Error('UNDECLARED_FIXTURE_READ:'+name);return value;},mkdirSync:()=>{},
  writeFileSync:(name,value,options)=>{name=String(name);if(options?.flag==='wx'&&files.has(name))throw Error('EXCLUSIVE_FILE_ALREADY_EXISTS');files.set(name,Buffer.from(value));}};
 const context={fs:inertFs,path,process,Buffer,isDds214:()=>false,isRos:()=>true,isInstalled:()=>false,readBoundPrivate:()=>{},rosControl:()=>({STAGES:stages}),
  require:name=>{if(name==='node:crypto')return crypto;if(name==='node:child_process')return {spawnSync:(exe,argv)=>{checks.push(argv);return {status:attempt===4?0:1,stdout:'INERT_SEQUENCE_SEAM',stderr:'',signal:null};}};throw Error('UNDECLARED_DEPENDENCY');}};
 vm.createContext(context);vm.runInContext(code+';globalThis.create=createDataDispatcher;',context);const dispatch=context.create(c,window);
 if(attempt===4){dispatch('A','INERT_NOT_EXECUTED',['UNIT']);assert.equal(checks.length,2);assert(checks.every(argv=>argv[1]==='--check-window'));assert.throws(()=>dispatch('A','INERT_NOT_EXECUTED',['UNIT']));}
 else {assert.throws(()=>dispatch('A','INERT_NOT_EXECUTED',['UNIT']),/FORMAL_DISPATCH_ADMISSION/);assert(!files.has(path.join(window,'DATA_ATTEMPT_STARTED')));}
 return {generation:5,attempt,formal_check_calls:checks.length,real_processes:0,real_claims:0};
}
console.log(JSON.stringify({scope:'REAL_NODE_DISPATCHER_INERT_COMMAND_SEAMS_NOT_DEVICE',results:[test(4),test(5)]}));
}
