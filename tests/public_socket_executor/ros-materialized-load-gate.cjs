'use strict';
// Local WSL /proc + formal materialized observer/controller. No HDC executable.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const crypto=require('node:crypto');
const childProcess=require('node:child_process');

const repo=path.resolve(__dirname,'../..');
const mode=process.argv[2]||'positive';
if(!['positive','old-script','missing-file','truncated-file','wrong-pid','debugger-local-rejected'].includes(mode))throw Error('Unsupported bounded local mode');
const run=crypto.randomBytes(8).toString('hex');
const group=path.join(repo,'.artifacts','ros2-load-materialized');
const task=path.join(group,'run-'+run);const window=path.join(task,'windows','host-gate');
const wsl=['-d','Ubuntu-20.04','-u','root','--exec'];
const main='ordinary_ros2_transport_consumer';
const roots=Object.fromEntries(['A','B'].map(side=>[side,`/data/local/tmp/mdds_dds_${run}_${side}`]));
const children={},pids={};
const cSource=`#include <stdio.h>\n#include <unistd.h>\nint main(void){FILE*f=fopen("pid.txt","w");if(!f)return 10;fprintf(f,"%ld\\n",(long)getpid());fclose(f);for(int i=0;i<5000;i++){if(!access("stop",F_OK))return 0;usleep(10000);}return 11;}\n`;
function wslPath(p){p=path.resolve(p).replaceAll('\\','/');if(!/^[A-Za-z]:\//.test(p))throw Error('Non-Windows local path');return '/mnt/'+p[0].toLowerCase()+p.slice(2);}
function sync(...args){const r=childProcess.spawnSync('wsl',[...wsl,...args],{encoding:'utf8',timeout:20000});if(r.error||r.status!==0)throw Error('WSL local setup failed: '+args.join(' ')+' '+String(r.error||r.stderr));return r.stdout.trim();}
function compile(side){
 const root=roots[side];sync('/bin/mkdir','-p',root);
 const r=childProcess.spawnSync('wsl',[...wsl,'/usr/bin/cc','-x','c','-o',`${root}/${main}`,'-'],{input:cSource,encoding:'utf8',timeout:20000});
 if(r.status!==0)throw Error('Local C helper compile failed: '+r.stderr);
 const shellScript=mode==='old-script'?path.join(repo,'.artifacts/ros2-device-acceptance/exec-service-name-20261002/windows/data-1/A/aux/capture-load.sh'):path.join(task,'executor','capture-load.sh');
 sync('/bin/cp',wslPath(shellScript),`${root}/capture-load.sh`);
 sync('/bin/sh','-c',`printf '%s\\n' ${root}/${main} > ${root}/required-load-paths.txt`);
 const cmd=`cd ${root} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=${root}:/system/lib64/platformsdk SKIP_DEFAULT_XML=1 ROS_LOG_DIR=${root}/ros-log RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=201 ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET FASTDDS_DSOFTBUS_REQUIRED_CONFIG=${root}/ros.conf FASTDDS_DSOFTBUS_NATIVE_TOKEN=${side==='A'?'123':'456'} FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=${root}/control ${root}/${main} ${side} ${run} dual`;
 children[side]=childProcess.spawn('wsl',[...wsl,'/bin/sh','-c',cmd],{stdio:'ignore',windowsHide:true});
 for(let attempt=0;attempt<15;attempt++){
  const r=childProcess.spawnSync('wsl',[...wsl,'/bin/cat',`${root}/pid.txt`],{encoding:'utf8',timeout:5000});
  const pid=r.status===0?r.stdout.trim():'';
  if(/^[1-9][0-9]*$/.test(pid)){pids[side]=pid;return;}
 }
 throw Error('Actual local helper PID unavailable '+side);
}
async function stopHelpers(){for(const side of ['A','B'])if(children[side]){
 try{sync('/usr/bin/touch',`${roots[side]}/stop`);}catch{}
 await new Promise(resolve=>{if(children[side].exitCode!==null)return resolve();const t=setTimeout(()=>{try{children[side].kill('SIGTERM');}catch{}resolve();},5000);children[side].once('close',()=>{clearTimeout(t);resolve();});});
}}
function localSpawn(executable,args,options){
 if(executable==='python')return childProcess.spawn('python',args,{shell:false,windowsHide:true});
 if(executable!=='LOCAL_HOST_DRIVER_NOT_HDC'||args[0]!=='-t'||!['HOST_A','HOST_B'].includes(args[1]))throw Error('Real device driver fallback forbidden');
 const side=args[1].slice(-1),root=roots[side];
 if(args[2]==='shell'&&args.length===4){
  const prefix=`env -i PATH=/system/bin:/system/xbin timeout 10 /system/bin/sh ${root}/capture-load.sh `;
  if(!args[3].startsWith(prefix)||!args[3].includes(` ${root} ${side} ${main}; rc=$?; echo LOAD_OBSERVER_REMOTE_EXIT=$rc`))throw Error('Unexpected collector shell command');
  const local=args[3].replace('env -i PATH=/system/bin:/system/xbin timeout 10 /system/bin/sh','env -i PATH=/usr/bin:/bin timeout 10 /bin/sh');
  return childProcess.spawn('wsl',[...wsl,'/bin/sh','-c',local],{shell:false,windowsHide:true});
 }
 if(args[2]==='file'&&args[3]==='recv'&&args.length===6){
  const remote=args[4],dest=args[5];
  if(!remote.startsWith(root+'/')||!dest.startsWith(path.join(window,side,'load')+path.sep))throw Error('Foreign local capture path');
  if((mode==='missing-file'||mode==='truncated-file')&&side==='B'&&remote.endsWith('/load-maps.txt')){
   if(mode==='missing-file')return childProcess.spawn(process.execPath,['-e','process.exit(1)'],{windowsHide:true});
   const cmd=`/bin/cp ${remote} ${wslPath(dest)} && : > ${wslPath(dest)}`;
   return childProcess.spawn('wsl',[...wsl,'/bin/sh','-c',cmd],{windowsHide:true});
  }
  return childProcess.spawn('wsl',[...wsl,'/bin/cp',remote,wslPath(dest)],{shell:false,windowsHide:true});
 }
 throw Error('Unsupported local capture command; no fallback');
}
function emit(observer,control,side,row){observer.output(side,'stdout',Buffer.from(row+'\n'));control.line(side,row);}
async function mainTest(){
 fs.mkdirSync(group,{recursive:true});
 fs.writeFileSync(path.join(group,'profile-'+run+'.json'),JSON.stringify({targets:{A:'HOST_A',B:'HOST_B'},tokens:{A:'123',B:'456'},hdc_path:path.resolve(repo,'LOCAL_HOST_DRIVER_NOT_HDC'),query_helper_path:path.resolve(repo,'LOCAL_QUERY_NEVER_USED'),permission_baseline_sha256:{A:'a'.repeat(64),B:'b'.repeat(64)}}));
 const materializer=path.join(repo,'scripts/public_socket/shared_reattach/materialize.py');
 const r=childProcess.spawnSync('python',[materializer,'--repo',repo,'--task',task,'--private-config',path.join(group,'profile-'+run+'.json')],{encoding:'utf8',timeout:20000});
 if(r.status!==0)throw Error('Formal materializer failed: '+r.stderr);
 fs.mkdirSync(window);for(const side of ['A','B'])compile(side);
 const config={scenario:'ROS2_DSOFTBUS',run,main_member:main,host_collector_test:true,simulated:false,
  hdc_path:'LOCAL_HOST_DRIVER_NOT_HDC',targets:{A:'HOST_A',B:'HOST_B'},remote:roots,
  system_library_dirs:['/system/lib64/platformsdk'],token:{A:'123',B:'456'},
  package_artifacts:[{name:main}],required_platform_paths:[]};
 fs.writeFileSync(path.join(window,'window.json'),JSON.stringify(config,null,2));
 const {createLoadObserver}=require(path.join(task,'executor','observe-native.cjs'));
 const {controller}=require(path.join(task,'executor','ros-control.cjs'));
 let control,debuggerSupport;
 if(mode==='debugger-local-rejected'){
  config.rmw_debugger={};config.local_contract_test=true;config.timeout_seconds=300;
  fs.writeFileSync(path.join(window,'window.json'),JSON.stringify(config,null,2));
  debuggerSupport=require(path.join(task,'executor','rmw-debugger.cjs')).createRmwDebugger(config,window,{onFailure:reason=>control?.stop(reason)});
 }
 const observer=createLoadObserver({config,directory:window,spawn:localSpawn,hostTimeoutMs:12000,onRosReady:debuggerSupport?.start});
 const actualAdmission=debuggerSupport?require(path.join(task,'executor','runner-template.cjs')).createLoadAdmission(config,observer,debuggerSupport):observer.admission;
 let admitted;
 const admission=async()=>{admitted=await actualAdmission();return admitted;};
 const released=[];control=controller(config,async(side,stage)=>{
  const receipt={run,side,stage,host_collector_test:true};
  fs.writeFileSync(path.join(window,`control-${side}-${stage}.json`),JSON.stringify(receipt),{flag:'wx'});released.push(receipt);
 },admission);
 for(const side of ['A','B']){
  const pid=mode==='wrong-pid'&&side==='B'?String(Number(pids[side])+1):pids[side];
  for(const row of [
   `ROS_MAIN scenario=ROS2_DSOFTBUS run=${run} side=${side} pid=${pid} mode=dual`,
   `ROS_LISTENER_READY run=${run} side=${side} pid=${pid} listener=7 fake_sdk=0`,
   `ROS_STAGE run=${run} side=${side} pid=${pid} stage=LISTENER_READY`,
   `ROS_PUBLISHER run=${run} side=${side} gid=${(side==='A'?'a':'b').repeat(32)} intra_process=0 phase=reliable topic=/fdds_${run}/reliable/from_${side}`,
   `ROS_GRAPH run=${run} side=${side} peer=/fdds_${run}/transport_${side==='A'?'B':'A'} visible=1 subscription_count=1 phase=reliable`,
   `ROS_STAGE run=${run} side=${side} pid=${pid} stage=RELIABLE_READY`])emit(observer,control,side,row);
 }
 const result=await control.finish(),loads=await observer.finish();
 if(debuggerSupport){
  const debug=await debuggerSupport.finish();assert.equal(debug.pass,false);
  for(const side of ['A','B']){
   assert(fs.existsSync(path.join(window,side,'load-pre-release-analysis.json')));
   assert(fs.readFileSync(path.join(window,side,'debugger-manager.stderr'),'utf8').includes('DEBUGGER_REAL_DISPATCH_REQUIRED'));
   assert(!fs.existsSync(path.join(window,side,'debugger')),'no debugger commands or authority from Host input');
  }
 }
 const releasedReady=released.filter(x=>x.stage==='RELIABLE_READY');
 if(mode==='positive'){
  assert.equal(loads.A.pass,true,JSON.stringify(loads.A));assert.equal(loads.B.pass,true,JSON.stringify(loads.B));
  assert.deepEqual(releasedReady.map(x=>x.side),['A','B']);
  for(const side of ['A','B']){const analysis=JSON.parse(fs.readFileSync(path.join(window,side,'load-pre-release-analysis.json')));assert.equal(analysis.complete,true,analysis.errors.join(','));assert.equal(analysis.pid,pids[side]);}
 }else if(mode==='debugger-local-rejected'){
  assert.equal(loads.A.formal_capture.pass,true);assert.equal(loads.B.formal_capture.pass,true);
  assert.equal(loads.A.pass,true);assert.equal(loads.B.pass,true,'debugger failure must not overwrite formal load result');
  assert.equal(admitted.A.pass,false);assert.equal(admitted.B.pass,false);
  assert.equal(releasedReady.length,0);
  assert(result.reported_first_host.startsWith('DEBUGGER_MANAGER_FAILED'),JSON.stringify(result));
 }else{
  assert.equal(releasedReady.length,0,JSON.stringify(releasedReady));
  assert.equal(result.reported_first_host,'ROS_LOAD_CAPTURE_FAILED',JSON.stringify(result));
  assert.equal(loads.B.pass,false,JSON.stringify(loads.B));
 }
 const summary={mode,run,window,host_scope:'HOST_LIVE_PROC_COLLECTOR_TEST_NOT_DEVICE_EVIDENCE',device_operations:0,
  materialization_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(task,'MATERIALIZATION.json'))).digest('hex'),
  actual_shell_sha256:crypto.createHash('sha256').update(fs.readFileSync(path.join(task,'executor','capture-load.sh'))).digest('hex'),
  actual_pids:pids,collector_pass:{A:loads.A.formal_capture?.pass??loads.A.pass,B:loads.B.formal_capture?.pass??loads.B.pass},
  load_admission_pass:{A:admitted?.A.pass??null,B:admitted?.B.pass??null},released,reported_first_host:result.reported_first_host};
 fs.writeFileSync(path.join(task,'HOST_LOAD_GATE_RESULT.json'),JSON.stringify(summary,null,2));
 console.log(JSON.stringify(summary));
}
mainTest().catch(e=>{console.error(e.stack||String(e));process.exitCode=1;}).finally(async()=>{await stopHelpers();});
