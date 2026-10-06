'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),{spawn}=require('node:child_process');
const {createControlRelease}=require('../../scripts/public_socket/shared_reattach/executor/runner-template.cjs');
const tmp=path.resolve(__dirname,'../../build/ros2-target/tmp');fs.mkdirSync(tmp,{recursive:true});
const run='fedcba9876543210',root=fs.mkdtempSync(path.join(tmp,'runtime-close-release-'));
const config={scenario:'ROS2_DSOFTBUS',run,paired_runtime_close:true,hdc_path:process.execPath,
 targets:{A:'LOCAL_ONLY_A',B:'LOCAL_ONLY_B'},remote:{A:`/data/local/tmp/mdds_dds_${run}_A`,B:`/data/local/tmp/mdds_dds_${run}_B`}};
let dispatched=0;
const spawnLocal=(exe,args,options)=>{
 assert.equal(exe,process.execPath);assert.equal(args[0],'-t');assert.equal(args[2],'shell');
 assert(['LOCAL_ONLY_A','LOCAL_ONLY_B'].includes(args[1]));
 const side=args[1].slice(-1);assert(args[3].includes(`ROS_RUNTIME_CLOSE_RELEASE schema=1 run=${run} side=${side} pid=123 stage=RUNTIME_CLOSE reason=WINDOW_FAILED`));
 assert(args[3].includes(`test ! -e ${config.remote[side]}/control.RUNTIME_CLOSE`));++dispatched;
 return spawn(process.execPath,['-e',"process.stdout.write('CONTROL_REMOTE_');setImmediate(()=>process.stdout.write('EXIT=0\\n'));"],{...options,shell:false});
};
(async()=>{
 const control=createControlRelease(config,root,{spawn:spawnLocal});
 await control('A','RUNTIME_CLOSE',{pid:'123',reason:'WINDOW_FAILED'});
 await control('B','RUNTIME_CLOSE',{pid:'123',reason:'WINDOW_FAILED'});
 assert.equal(dispatched,2);
 await assert.rejects(control('A','RUNTIME_CLOSE',{pid:'123',reason:'SUCCESS'}));
 const legacy=createControlRelease({...config,paired_runtime_close:false},root,{spawn:spawnLocal});
 await assert.rejects(legacy('A','RUNTIME_CLOSE',{pid:'123',reason:'WINDOW_FAILED'}));
 assert.equal(dispatched,2);
 const rows=fs.readdirSync(root).filter(n=>/^control-\d+\.json$/.test(n)).map(n=>JSON.parse(fs.readFileSync(path.join(root,n))));
 assert.equal(rows.length,2);assert(rows.every(r=>r.pass&&r.collector_closed&&r.remote_exit_count===1));
 console.log(JSON.stringify({scope:'REAL_LOCAL_CHILD_AND_FORMAL_CONTROL_PARSER_NO_HDC',root,dispatched,pass:true}));
})().catch(e=>{console.error(e);process.exitCode=1;});
