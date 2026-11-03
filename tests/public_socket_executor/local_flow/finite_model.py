"""Finite local platform model. Never executes input shell or contacts a device.

Only exact commands emitted by the reviewed local executor are modeled. Mutable
files are task-owned; permissions, deployment and control are real sandbox state.
"""
import hashlib,json,os,re,shutil,subprocess,sys,time
from pathlib import Path

TOKEN='/data/service/el0/access_token/nativetoken.json'
PERMISSION='ohos.permission.DISTRIBUTED_DATASYNC'

def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p,x):
 p=Path(p);p.parent.mkdir(parents=True,exist_ok=True);p.write_text(json.dumps(x,indent=2)+'\n',encoding='utf-8')
def event(sandbox,x):
 with (sandbox/'actions.jsonl').open('a',encoding='utf-8') as f:
  f.write(json.dumps(x)+'\n');f.flush();os.fsync(f.fileno())
def root_of(sandbox,remote):
 m=re.fullmatch(r'/data/local/tmp/mdds_recovery_([0-9a-f]{16})_([AB])_([1-9][0-9]*)(?:/([A-Za-z0-9_.+-]+))?',remote)
 if m:
  base=sandbox/'recovery'/m[1]/m[2]/m[3]
  return base/m[4] if m[4] else base
 m=re.fullmatch(r'/data/local/tmp/mdds_dds_([0-9a-f]{16})_([AB])(?:/([A-Za-z0-9_.+-]+))?',remote)
 if not m:return None
 return sandbox/'remote'/m[1]/m[2]/m[3] if m[3] else sandbox/'remote'/m[1]/m[2]
def live(p):
 import ctypes
 k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.restype=ctypes.c_void_p
 h=k.OpenProcess(0x1000,False,int(p))
 if not h:return ctypes.get_last_error()!=87
 rc=ctypes.c_ulong();ok=k.GetExitCodeProcess(ctypes.c_void_p(h),ctypes.byref(rc));k.CloseHandle(ctypes.c_void_p(h))
 return not ok or rc.value==259
def config():return read(Path(os.environ['FAKE_HDC_WINDOW_ROOT'])/'window.json')
def side():return os.environ['FAKE_HDC_SIDE']
def state(sandbox):return sandbox/side()/'nativetoken.json'
def fault(sandbox):
 p=sandbox/'fault.json';return read(p).get('fault','') if p.exists() else ''
def mapped(sandbox,path):
 if path==TOKEN:return state(sandbox)
 if path in ('/system/lib64/platformsdk/libsoftbus_client.z.so','/system/lib64/libc++.so','/system/lib64/platformsdk/libsoftbus_server.z.so'):return sandbox/'platform'/Path(path).name
 c=config();allowed=c['remote'][side()]
 recovery=re.fullmatch(r'/data/local/tmp/mdds_recovery_'+re.escape(c['run'])+'_'+side()+r'_[1-9][0-9]*(?:/[A-Za-z0-9_.+-]+)?',path)
 if path!=allowed and not path.startswith(allowed+'/') and not recovery:return None
 return root_of(sandbox,path)
def classify(command):
 if re.fullmatch(r'env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=[^ ]+ /data/local/tmp/mdds_recovery_[0-9a-f]{16}_[AB]_[1-9][0-9]*/native_permission_probe --verify-denied --expected-token [1-9][0-9]* --preflight-confirmed',command):return 'READ_ONLY'
 if command.startswith('test ! -e ') or command.startswith('chmod '):return 'MUTATION'
 if '/native_permission_probe --' in command:return 'MUTATION'
 if 'CONTROL_REMOTE_EXIT=' in command:return 'MUTATION'
 if 'LOAD_OBSERVER_REMOTE_EXIT=' in command:return 'LOCAL_CAPTURE'
 if '; echo REMOTE_EXIT=' in command:return 'BUSINESS_PROCESS'
 return 'READ_ONLY_OR_UNKNOWN'  # grammar recognition is done before effects

def handle(core,sandbox,reject):
 c=config();s=side();remote=c['remote'][s];r=root_of(sandbox,remote)
 recovery=re.fullmatch(r'test ! -e (/data/local/tmp/mdds_recovery_'+re.escape(c['run'])+'_'+s+r'_[1-9][0-9]*) && mkdir \1',core)
 if recovery:
  directory=mapped(sandbox,recovery[1])
  if directory.exists():return '',1
  directory.mkdir(parents=True);return '',0
 chmod=re.fullmatch(r'chmod 700 (/data/local/tmp/mdds_recovery_'+re.escape(c['run'])+'_'+s+r'_[1-9][0-9]*/native_permission_probe)',core)
 if chmod:
  p=mapped(sandbox,chmod[1])
  if not p.is_file():return '',1
  write(p.with_name(p.name+'.mode.json'),dict(mode='700'));return '',0
 exists=re.fullmatch(r'test -d (.+) && test ! -L \1',core)
 if exists:
  p=mapped(sandbox,exists[1]);return ('',0 if p and p.is_dir() else 1)
 listing=re.fullmatch(r'ls -1A (.+)',core)
 if listing:
  p=mapped(sandbox,listing[1])
  if not p or not p.is_dir():return '',1
  return '\n'.join(sorted(x.name for x in p.iterdir() if not x.name.endswith('.mode.json')))+'\n',0
 absent=re.fullmatch(r'test ! -e (.+) && test ! -L \1',core)
 if absent:
  p=mapped(sandbox,absent[1]);return ('',0 if p and not p.exists() else 1)
 file_check=re.fullmatch(r"test -f (.+) && test ! -L \1 && sha256sum \1 && stat -c 'FILE=%F\|%a\|%u\|%g\|%s' \1",core)
 if file_check:
  p=mapped(sandbox,file_check[1])
  if not p or not p.is_file():return '',1
  mode=read(p.with_name(p.name+'.mode.json'))['mode'] if p.with_name(p.name+'.mode.json').exists() else '644'
  return hashlib.sha256(p.read_bytes()).hexdigest()+'  '+file_check[1]+f'\nFILE=regular file|{mode}|0|0|{p.stat().st_size}\n',0
 verify=re.fullmatch(r'env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH=([^ ]+) (/data/local/tmp/mdds_recovery_'+re.escape(c['run'])+'_'+s+r'_[1-9][0-9]*/native_permission_probe) --verify-denied --expected-token ([1-9][0-9]*) --preflight-confirmed',core)
 if verify:
  p=mapped(sandbox,verify[2]);rows=read(state(sandbox));owned=[x for x in rows if x.get('processName')=='fastdds_dsoftbus_probe']
  if not p or not p.is_file() or len(owned)!=1 or str(owned[0]['tokenId'])!=verify[3] or owned[0]['permissions']!=[]:return 'HOST_FAKE_PERMISSION_STATE_REJECT\n',1
  return 'native_permission verification_permission=-1 writes=NOT_PERFORMED token_switch=NOT_PERFORMED communication=NOT_RUN\nresult=PASS scope=HOST_FAKE_PERMISSION_STATE\n',0
 if core=='ps -A':
  rows=[]
  for p in (sandbox/'processes').glob('*.json'):
   x=read(p)
   if live(x['pid']):rows.append(str(x['pid'])+' mdds_installed_consumer')
  return '\n'.join(rows)+'\n',0
 if core in ("env -i PATH=/system/bin:/system/xbin /system/bin/sh -c 'for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v \"$mdds_tool\" || exit 3; done'", "env -i PATH=/system/bin:/system/xbin /system/bin/sh -c 'for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v $mdds_tool || exit 3; done'"):return '\n'.join('/system/bin/'+x for x in ('cat','readlink','stat','sha256sum','mv','env','timeout'))+'\n',0
 meta="test -f "+TOKEN+" && test ! -L "+TOKEN+" && stat -c 'META=%F|%a|%u|%g|%s|%i|%Y|%Z' "+TOKEN+" && ls -lZ "+TOKEN+" && getenforce"
 if core==meta:
  p=state(sandbox);m=read(sandbox/s/'metadata.json');st=p.stat()
  return f"META=regular file|{m['mode']}|{m['uid']}|{m['gid']}|{st.st_size}|{st.st_ino}|{int(st.st_mtime)}|{int(st.st_ctime)}\nu:object_r:{m['label']}:s0 nativetoken.json\n{m['enforcement']}\n",0
 mkdir_command='test ! -e '+remote+' && mkdir '+remote
 if core in (mkdir_command,"env -i PATH=/system/bin:/system/xbin /system/bin/sh -c '"+mkdir_command+"'"):
  if r.exists():return '',1
  r.mkdir(parents=True);return '',0
 if core in ('chmod 700 '+remote+'/native_permission_probe','chmod 700 '+remote+'/softbus_record_probe','chmod 700 '+remote+'/softbus_record_probe && test -x '+remote+'/softbus_record_probe','chmod 700 '+remote+'/'+c.get('main_member','mdds_installed_consumer')+' && test -x '+remote+'/'+c.get('main_member','mdds_installed_consumer')):
  name=core.split()[2].rsplit('/',1)[1]
  if not (r/name).is_file():return '',1
  write(r/(name+'.mode.json'),{'mode':'700'});return '',0
 loader=':'.join([remote]+c['system_library_dirs'])
 prefix='env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+loader+' '
 for action in ('grant','revoke'):
  expected=prefix+remote+'/native_permission_probe --'+action+' --expected-token '+c['token'][s]+' --preflight-confirmed'
  if core==expected:
   if not (r/'native_permission_probe').is_file():return '',1
   baseline=(sandbox/s/'baseline-token.json').read_bytes();doc=json.loads(baseline)
   if action=='grant':
    own=[x for x in doc if x['processName']=='fastdds_dsoftbus_probe']
    if len(own)!=1 or str(own[0]['tokenId'])!=c['token'][s]:return '',1
    own[0]['permissions']=[PERMISSION];state(sandbox).write_bytes(json.dumps(doc,separators=(',',':')).encode())
   else:state(sandbox).write_bytes(baseline)
   if action=='revoke' and c.get('scenario') in ('INSTALLED_RUNTIME','ROS2_DSOFTBUS') and fault(sandbox)=='metadata_not_restored':
    meta=read(sandbox/s/'metadata.json');meta['mode']='600';write(sandbox/s/'metadata.json',meta)
   actual=read(state(sandbox));allowed=any(x['processName']=='fastdds_dsoftbus_probe' and x['permissions']==[PERMISSION] for x in actual)
   return f'verification_permission={0 if allowed else -1}\nresult=PASS\n',0
 query=prefix+'timeout 10 '+remote+'/softbus_record_probe --list-nodes --expected-token '+c['token'][s]+' --preflight-confirmed'
 if core==query:
  if not (r/'softbus_record_probe').is_file() or not any(x['permissions']==[PERMISSION] for x in read(state(sandbox))):return '',1
  identities=read(sandbox/'identities.json');local=identities[s];peer=identities['B' if s=='A' else 'A']
  if fault(sandbox)=='missing_query_output':return 'LOCAL_QUERY_OUTPUT_TRUNCATED\n',0
  return f'''scope=READONLY_ROUTE_QUERY role=list-nodes
stage=GetLocalNodeDeviceInfo action=RETURN rc=0
route_kind=local network_id={local}
stage=GetAllNodeDeviceInfo action=RETURN rc=0 count=1 data_present=1
route_kind=remote index=0 network_id={peer}
stage=FreeNodeInfo action=RETURN
stage=route_summary query_valid=true remote_count=1 route_available=1 free_returned=1 allocation_present=1 node_observation_cap=32 communication=NOT_RUN
result=PASS scope=READONLY_ROUTE_QUERY fatal=0 untracked_first_id=0 untracked_count=0 cleanup=NO_SOCKET_RESOURCES token_restored=1 quiescence=NOT_VERIFIED dds=NOT_RUN transport=NOT_IMPLEMENTED
''',0
 raise reject(66,'UNSUPPORTED_FINITE_COMMAND')

def special(command,sandbox,reject):
 c=config();s=side();remote=c['remote'][s];r=root_of(sandbox,remote)
 if c.get('scenario')=='ROS2_DSOFTBUS':
  name=remote+'/control.CLEANUP';prefix='ROS_CLEANUP schema=1 run='+c['run']+' side='+s+' pid='
  pattern=re.escape(f'env -i PATH=/system/bin:/system/xbin timeout 5 /system/bin/sh -c \'test ! -e {name} && printf "{prefix}')+r'([1-9][0-9]*)'+re.escape(' stage=CLEANUP reason=')+r'(WINDOW_FAILED|NORMAL_COMPLETION)'+re.escape(f'\\n" > {name}.tmp && mv {name}.tmp {name}\'; rc=$?; echo CONTROL_REMOTE_EXIT=$rc; exit $rc')
  match=re.fullmatch(pattern,command)
  if match:
   process=read(sandbox/'processes'/f'{c["run"]}-{s}.json')
   if str(process['pid'])!=match[1] or not live(process['pid']):raise reject(67,'CLEANUP_PROCESS_IDENTITY')
   p=r/'control.CLEANUP'
   if p.exists():return 'CONTROL_REMOTE_EXIT=1\n',0
   p.write_text(prefix+match[1]+' stage=CLEANUP reason='+match[2]+'\n');return 'CONTROL_REMOTE_EXIT=0\n',0
 if c.get('scenario') in ('ROS2_DSOFTBUS','DDS214_LAYER_BASELINE'):
  stages=('LISTENER_READY','MATCH','DATA_DONE') if c['scenario']=='DDS214_LAYER_BASELINE' else ('LISTENER_READY','RELIABLE_READY','RELIABLE_DONE','BEST_EFFORT_READY','BEST_EFFORT_DONE')
  for stage in stages:
   wire='DDS214_RELEASE' if c['scenario']=='DDS214_LAYER_BASELINE' and stage!='LISTENER_READY' else 'ROS_RELEASE'
   name=remote+'/control.'+stage;contents=f'{wire} run={c["run"]} side={s} stage={stage}'
   expected=f'env -i PATH=/system/bin:/system/xbin timeout 5 /system/bin/sh -c \'test ! -e {name} && printf "{contents}\\n" > {name}.tmp && mv {name}.tmp {name}\'; rc=$?; echo CONTROL_REMOTE_EXIT=$rc; exit $rc'
   if command==expected:
    p=r/('control.'+stage)
    if p.exists():return 'CONTROL_REMOTE_EXIT=1\n',0
    p.write_text(contents+'\n');return 'CONTROL_REMOTE_EXIT=0\n',0
 control=re.fullmatch(r'env -i PATH=/system/bin:/system/xbin timeout 5 /system/bin/sh -c \'test ! -e (.+)/control\.([A-Z_]+) && printf "([0-9a-f]{16}) ([A-Z_]+)\\n" > (.+)\.tmp && mv (.+)\.tmp (.+)\'; rc=\$\?; echo CONTROL_REMOTE_EXIT=\$rc; exit \$rc',command)
 if control:
  root,stage,run,printed,tmp,mv,dst=control.groups();name=remote+'/control.'+stage
  if root!=remote or run!=c['run'] or stage!=printed or stage not in ('LISTENER_READY','MATCH','DATA_DONE') or (tmp,mv,dst)!=(name,name,name):raise reject(67,'CONTROL_IDENTITY')
  p=r/('control.'+stage)
  if p.exists():return 'CONTROL_REMOTE_EXIT=1\n',0
  p.write_text(run+' '+stage+'\n');return 'CONTROL_REMOTE_EXIT=0\n',0
 load=f'env -i PATH=/system/bin:/system/xbin timeout 10 /system/bin/sh {remote}/capture-load.sh {c["run"]} '
 m=re.fullmatch(re.escape(load)+r'([1-9][0-9]*) '+re.escape(remote)+' '+s+' '+re.escape(c.get('main_member',''))+r'; rc=\$\?; echo LOAD_OBSERVER_REMOTE_EXIT=\$rc',command)
 if m:
  p=sandbox/'processes'/f'{c["run"]}-{s}.json'
  if not p.exists():return 'LOAD_OBSERVER_REMOTE_EXIT=1\n',0
  x=read(p)
  if str(x['pid'])!=m[1] or not live(x['pid']):return 'LOAD_OBSERVER_REMOTE_EXIT=1\n',0
  # Model live capture of the local child identity, not a device /proc claim.
  cmd=x['remote_argv'];pid=x['pid'];start=x['starttime'];exe=remote+'/'+c['main_member']
  files={'load-exe.txt':exe+'\n','load-cwd.txt':remote+'\n','load-cmdline.txt':'\0'.join(cmd)+'\0','load-environ.txt':'\0'.join(['PATH=/system/bin:/system/xbin','LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs']),'SKIP_DEFAULT_XML=1'])+'\0','load-stat-before.txt':f'{pid} (local_fake_consumer) S '+'0 '*18+str(start)+' 0\n','load-stat-after.txt':f'{pid} (local_fake_consumer) S '+'0 '*18+str(start)+' 0\n','load-uptime.txt':str(time.monotonic())+' 0\n','load-maps.txt':'\n'.join('1000-2000 r-xp 0 00:01 1 '+p for p in [remote+'/'+a['name'] for a in c['package_artifacts']]+c['required_platform_paths'])+'\n','load-status.txt':f'status=COMPLETE\nrun={c["run"]}\npid={pid}\nstarttime={start}\nvector_format=NUL_V1\nscope=LOCAL_MODELED_CAPTURE\n'}
  if c.get('scenario')=='ROS2_DSOFTBUS':
   files['load-environ.txt']='\0'.join([
    'PATH=/system/bin:/system/xbin','LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs']),
    'SKIP_DEFAULT_XML=1','ROS_LOG_DIR='+remote+'/ros-log','RMW_IMPLEMENTATION=rmw_fastrtps_cpp','ROS_DOMAIN_ID=201',
    'ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET','FASTDDS_DSOFTBUS_REQUIRED_CONFIG='+remote+'/ros.conf',
    'FASTDDS_DSOFTBUS_NATIVE_TOKEN='+c['token'][s],'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1',
    'FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX='+remote+'/control'])+'\0'
  if c.get('scenario')=='DDS214_LAYER_BASELINE':
   files['load-environ.txt']='\0'.join(['PATH=/system/bin:/system/xbin','LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs']),'SKIP_DEFAULT_XML=1','FASTDDS_DSOFTBUS_REQUIRED_CONFIG='+remote+'/dds214.conf','FASTDDS_DSOFTBUS_NATIVE_TOKEN='+c['token'][s],'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1','FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX='+remote+'/control'])+'\0'
  if fault(sandbox)=='wrong_load':files['load-exe.txt']='/data/local/tmp/wrong/mdds_installed_consumer\n'
  for name,value in files.items():(r/name).write_text(value,encoding='utf-8')
  return 'LOAD_OBSERVER_REMOTE_EXIT=0\n',0
 # Match the exact formal command computed by a read-only argv builder.
 exe=remote+'/'+c.get('main_member','mdds_installed_consumer')
 args=['0' if s=='A' else '1',c.get('peer',{}).get(s,''),c.get('package_name',''),c.get('service_prefix',''),c['run'],c['token'][s],remote+'/control']
 business='cd '+remote+' && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs'])+' SKIP_DEFAULT_XML=1 timeout '+str(c.get('timeout_seconds',180))+' '+exe+' '+' '.join(args)+'; rc=$?; echo REMOTE_EXIT=$rc; exit $rc'
 if c.get('scenario')=='ROS2_DSOFTBUS':
  if c.get('main_member')!='ordinary_ros2_transport_consumer' or c.get('main_mode')!='dual':raise reject(69,'ROS_LOCAL_SCENARIO_IDENTITY')
  business=f'cd {remote} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs'])+f' SKIP_DEFAULT_XML=1 ROS_LOG_DIR={remote}/ros-log RMW_IMPLEMENTATION=rmw_fastrtps_cpp ROS_DOMAIN_ID=201 ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET FASTDDS_DSOFTBUS_REQUIRED_CONFIG={remote}/ros.conf FASTDDS_DSOFTBUS_NATIVE_TOKEN={c["token"][s]} FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX={remote}/control timeout {c["timeout_seconds"]} {exe} {s} {c["run"]} dual; rc=$?; echo REMOTE_EXIT=$rc; exit $rc'
 if c.get('scenario')=='DDS214_LAYER_BASELINE':
  if c.get('main_member')!='fastdds_dsoftbus_dds214_baseline' or c.get('main_mode')!='normal':raise reject(69,'DDS214_LOCAL_SCENARIO_IDENTITY')
  business=f'cd {remote} && env -i PATH=/system/bin:/system/xbin LD_LIBRARY_PATH='+':'.join([remote]+c['system_library_dirs'])+f' SKIP_DEFAULT_XML=1 FASTDDS_DSOFTBUS_REQUIRED_CONFIG={remote}/dds214.conf FASTDDS_DSOFTBUS_NATIVE_TOKEN={c["token"][s]} FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX={remote}/control timeout {c["timeout_seconds"]} {exe} {s} {c["run"]} normal; rc=$?; echo REMOTE_EXIT=$rc; exit $rc'
 if command==business:
  if not (r/c['main_member']).is_file():return 'REMOTE_EXIT=1\n',0
  producer='dds214-consumer.py' if c.get('scenario')=='DDS214_LAYER_BASELINE' else 'ros-consumer.py' if c.get('scenario')=='ROS2_DSOFTBUS' else 'consumer.py'
  p=subprocess.Popen([sys.executable,str(Path(__file__).with_name(producer)),str(Path(os.environ['FAKE_HDC_WINDOW_ROOT'])),s,str(sandbox)],shell=False)
  rc=p.wait(timeout=c['timeout_seconds']+2)
  return 'REMOTE_EXIT='+str(rc)+'\n',0
 raise reject(66,'UNSUPPORTED_SPECIAL_COMMAND')
