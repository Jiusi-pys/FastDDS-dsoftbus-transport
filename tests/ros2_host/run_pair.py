"""Actual independent ROS processes; no HDC, device driver or synthesized result."""
import argparse,json,os,subprocess,time,secrets,socket,re,hashlib
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--bridge',type=Path,required=True);p.add_argument('--fastdds-prefix',type=Path,required=True);p.add_argument('--ros-prefix',type=Path,required=True);p.add_argument('--pixi-prefix',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',choices=['reliable','best_effort','dual'],default='reliable');p.add_argument('--forward',choices=['0','1'],default='1');p.add_argument('--host-close-error',choices=['0','1'],default='0');p.add_argument('--paired-close',action='store_true');p.add_argument('--asymmetric-cleanup',action='store_true');a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=False);run=secrets.token_hex(8);domain=201
# Optional test-only controller response, never an application/SDK injection.
close_control=os.environ.get('FDDST_HOST_TEST_CLOSE_CONTROL','NORMAL_COMPLETION')
if close_control not in ('NORMAL_COMPLETION','WINDOW_FAILED','INVALID_PID'):raise ValueError('Host close control fixture')
if close_control!='NORMAL_COMPLETION' and not a.paired_close:raise ValueError('paired fixture required')
ports=[]
for _ in range(2):
 with socket.socket() as s:s.bind(('127.0.0.1',0));ports.append(s.getsockname()[1])
assert len(set(ports))==2
processes={};streams={};commands={}
input_hashes={str(f.resolve()):hashlib.sha256(f.read_bytes()).hexdigest() for f in [a.exe,a.bridge,a.fastdds_prefix/'bin/fastrtps-2.14.dll']}
for index,side in enumerate('AB'):
 conf=a.output/(side+'.conf');conf.write_text('\n'.join([
  'bridge_library='+str(a.bridge.resolve()),'fastdds_module='+str((a.fastdds_prefix/'bin/fastrtps-2.14.dll').resolve()),'fastcdr_module='+str((a.ros_prefix/'bin/fastcdr-2.2.dll').resolve()),'locator_version=V1\nnative_name_contract=PACKAGE_SCOPED_V1\nauthorized_package=host.ros.test\nservice_prefix=host.ros.test.v1\nbackend=HOST_OPAQUE_BYTES_TEST_ONLY',
  'run='+run,'side='+str(index),'port='+str(ports[index]),'peer_port='+str(ports[1-index]),
  'domain='+str(domain),'forward='+a.forward,'host_close_error='+a.host_close_error])+'\n')
 if a.paired_close:
  with conf.open('a') as stream:stream.write('runtime_close_release='+str((a.output/(side+'-control.RUNTIME_CLOSE')).resolve())+'\n')
 env=os.environ.copy();env['PATH']=os.pathsep.join([str(a.fastdds_prefix/'bin'),str(a.ros_prefix/'bin'),str(a.pixi_prefix/'Library/bin'),env.get('PATH','')]);env['RMW_IMPLEMENTATION']='rmw_fastrtps_cpp';env['ROS_DOMAIN_ID']=str(domain);env['ROS_AUTOMATIC_DISCOVERY_RANGE']='SUBNET';env['FASTDDS_DSOFTBUS_REQUIRED_CONFIG']=str(conf.resolve());env['FASTDDS_ENVIRONMENT_FILE']='';env['SKIP_DEFAULT_XML']='1'
 env['FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX']=str((a.output/(side+'-control')).resolve())
 for k in ['FASTRTPS_DEFAULT_PROFILES_FILE','FASTDDS_DEFAULT_PROFILES_FILE','ROS_LOCALHOST_ONLY']:env.pop(k,None)
 out=(a.output/(side+'.stdout')).open('wb');err=(a.output/(side+'.stderr')).open('wb');streams[side]=(out,err)
 argv=[str(a.exe.resolve()),side,run,a.mode];commands[side]=argv;processes[side]=subprocess.Popen(argv,env=env,stdout=out,stderr=err)
phase_stems=['RELIABLE','BEST_EFFORT'] if a.mode=='dual' else ['RELIABLE' if a.mode=='reliable' else 'BEST_EFFORT']
stages=[stem+suffix for stem in phase_stems for suffix in ('_READY','_DONE')]+['TEARDOWN_READY']
releases=[];cleanup_sent=False;deadline=time.monotonic()+65;timed=False
held_b_cleanup=False;early_peer_close=False;runtime_releases=[]
while any(x.poll() is None for x in processes.values()):
 texts={s:(a.output/(s+'.stdout')).read_text(errors='replace') for s in 'AB'}
 for stage in stages:
  if stage in releases:continue
  if all(re.search(r'^ROS_STAGE run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' stage='+stage+r'$',texts[s],re.M) for s in 'AB'):
   for s in 'AB':
    if stage=='TEARDOWN_READY':
     if s=='B' and a.asymmetric_cleanup:held_b_cleanup=True;continue
     (a.output/(s+'-control.CLEANUP')).write_text(f'ROS_CLEANUP schema=1 run={run} side={s} pid={processes[s].pid} stage=CLEANUP reason=NORMAL_COMPLETION\n')
    else:(a.output/(s+'-control.'+stage)).write_text(f'ROS_RELEASE run={run} side={s} stage={stage}\n')
   releases.append(stage)
 if held_b_cleanup:
  ready_a=re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run='+run+r' side=A pid='+str(processes['A'].pid)+r' ',texts['A'],re.M)
  closed_a='FDDST_ROS_CLOSED run='+run in (a.output/'A.stderr').read_text(errors='replace') or processes['A'].poll() is not None
  if ready_a or closed_a:
   early_peer_close=bool(closed_a)
   (a.output/'B-control.CLEANUP').write_text(f'ROS_CLEANUP schema=1 run={run} side=B pid={processes["B"].pid} stage=CLEANUP reason=NORMAL_COMPLETION\n')
   held_b_cleanup=False
 if a.paired_close and not runtime_releases and all(re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' ',texts[s],re.M) for s in 'AB'):
  for s in 'AB':
   control_pid=processes[s].pid+(1 if close_control=='INVALID_PID' else 0)
   reason='WINDOW_FAILED' if close_control=='WINDOW_FAILED' else 'NORMAL_COMPLETION'
   (a.output/(s+'-control.RUNTIME_CLOSE')).write_text(f'ROS_RUNTIME_CLOSE_RELEASE schema=1 run={run} side={s} pid={control_pid} stage=RUNTIME_CLOSE reason={reason}\n')
   runtime_releases.append(s)
 if not cleanup_sent and any(re.search(r'^ROS_FAILURE schema=1 ',(a.output/(s+'.stderr')).read_text(errors='replace'),re.M) for s in 'AB'):
  for s in 'AB':
   f=a.output/(s+'-control.CLEANUP')
   if processes[s].poll() is None and not f.exists():f.write_text(f'ROS_CLEANUP schema=1 run={run} side={s} pid={processes[s].pid} stage=CLEANUP reason=WINDOW_FAILED\n')
  cleanup_sent=True
 if time.monotonic()>deadline:
  timed=True
  for x in processes.values():
   if x.poll() is None:x.kill()
  break
 time.sleep(.05)
for x in processes.values():x.wait(timeout=5)
for pair in streams.values():
 for f in pair:f.close()
result={'run':run,'mode':a.mode,'forward':a.forward,'timed_out':timed,'releases':releases,'cleanup_sent_after_failure':cleanup_sent,'processes':{s:{'pid':x.pid,'exit_code':x.returncode,'argv':commands[s]} for s,x in processes.items()},'device_tested':False,'inputs':input_hashes,'paired_close':a.paired_close,'asymmetric_cleanup':a.asymmetric_cleanup,'early_peer_close':early_peer_close,'runtime_close_releases':runtime_releases}
import importlib.util
result['host_close_control_fixture']=close_control
spec=importlib.util.spec_from_file_location('formal_ros_analysis',Path(__file__).resolve().parents[2]/'scripts/public_socket/analyze_ros2.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
phases=['reliable','best_effort'] if a.mode=='dual' else [a.mode]
texts={s:(a.output/(s+'.stdout')).read_text(errors='replace') for s in 'AB'}
ledger=module.analyze(texts,run,phases);result['ledger']=ledger
errors=list(ledger['errors'])
if a.asymmetric_cleanup and early_peer_close:errors.append('peer_runtime_closed_before_b_cleanup')
if a.paired_close and runtime_releases!=['A','B']:errors.append('paired_runtime_release_incomplete')
if any(hashlib.sha256(Path(f).read_bytes()).hexdigest()!=h for f,h in input_hashes.items()):errors.append('input_mutated_during_run')
for s in 'AB':
 if ledger['boards'].get(s,{}).get('pid')!=str(processes[s].pid):errors.append('actual_process_pid:'+s)
 err=(a.output/(s+'.stderr')).read_text(errors='replace')
 if err.count('FDDST_ROS_CLOSED run='+run)!=1 or any(x in err for x in ['FDDST_CLOSE_INCOMPLETE','Failed to delete participant','FDDST_PUMP_ERROR_OBSERVED','FDDST_UNPUBLISHED_OWNER_RETAINED_INCOMPLETE']):errors.append('runtime_close:'+s)
 if not re.search(r'FDDST_HOST_IO run='+run+r' side=\d+ sends=[1-9][0-9]* ingress=[1-9][0-9]* send_errors=0 fake_sdk=1',err):errors.append('host_path:'+s)
 expected_load=str((a.fastdds_prefix/'bin/fastrtps-2.14.dll').resolve()).lower()
 loaded=re.findall(r'FDDST_LOADED module=fastrtps-2.14.dll path=([^\r\n]+)',err)
 if len(loaded)!=1 or str(Path(loaded[0]).resolve()).lower()!=expected_load:errors.append('fastdds_load:'+s)
 result[s]={'writes':sum(x.get('ROS_WRITE',0) for x in ledger['boards'].get(s,{}).get('phases',{}).values()),'takes':sum(x.get('ROS_TAKE',0) for x in ledger['boards'].get(s,{}).get('phases',{}).values())}
result['errors']=errors;result['pass']=not timed and all(x.returncode==0 for x in processes.values()) and not errors
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0 if result['pass'] else 1)
