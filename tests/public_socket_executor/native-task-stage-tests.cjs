'use strict';
// Actual task verifier with inert file reads; no claim, process or SDK call.
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const repo=path.resolve(__dirname,'../..'),source=fs.readFileSync(path.join(repo,'scripts/public_socket/shared_reattach/executor/runner-template.cjs'),'utf8');
const begin=source.indexOf('function verifyTaskGrant(c){'),end=source.indexOf('function readBoundPrivate(c){');
const ros=require('../../scripts/public_socket/shared_reattach/executor/ros-control.cjs');
const grantPath=path.join(repo,'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/AUTHORIZATION.json');
const grantBytes=fs.readFileSync(grantPath),grant=JSON.parse(grantBytes),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
function check(native,alter){
 const root=path.join(grant.task_root,'iterations','iteration-40'),freezePath=path.join(root,'INERT_NOT_WRITTEN.json');
 const scene={contract:'BOUNDED_NORMAL_ONCE',order:'outgoing-first'};
 const frozen={task_root:root,task_authority_root:grant.task_root,execution_iteration_id:40,task_id:grant.task_id,authorization:grant.authorization,scope:'ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE',device_authorized:true,authorization_record:{path:grantPath,sha256:hash(grantBytes)},limits:grant.per_window_limits,...(native?{bounded_native_reaccept:scene}:{})};
 const bytes=Buffer.from(JSON.stringify(frozen));
 const c={repo,work_package:frozen.scope,targets:grant.targets,ros_attempt:17,task_authority_root:grant.task_root,ros_task_id:grant.task_id,timeout_seconds:300,record_count:20,stdout_budget:25165824,max_record_bytes:8192,task_contract:{task_root:root,freeze_path:freezePath,freeze_sha256:hash(bytes)},...(native?{bounded_native_reaccept:scene}:{})};
 c.stages=ros.stagesFor(c);if(alter)alter(c);
 const ctx={fs:{readFileSync:p=>p===grantPath?grantBytes:p===freezePath?bytes:(()=>{throw Error('UNDECLARED_READ');})()},path,Buffer,rosControl:()=>ros,require:n=>{assert.equal(n,'node:crypto');return crypto;}};
 vm.createContext(ctx);vm.runInContext(source.slice(begin,end)+';globalThis.verify=verifyTaskGrant;',ctx);return ctx.verify(c);
}
check(false);check(true);
assert.throws(()=>check(true,c=>{delete c.bounded_native_reaccept;c.stages=ros.STAGES;}),/NATIVE_SCENE|ACTUAL_WINDOW/);
assert.throws(()=>check(false,c=>{c.bounded_native_reaccept={contract:'BOUNDED_NORMAL_ONCE'};c.stages=ros.stagesFor(c);}),/NATIVE_SCENE|ACTUAL_WINDOW/);
assert.throws(()=>check(true,c=>{c.stages=ros.STAGES;}),/ACTUAL_WINDOW/);
console.log('PASS 5 actual task stage verifier cases; NO_DEVICE_ACTIONS');
