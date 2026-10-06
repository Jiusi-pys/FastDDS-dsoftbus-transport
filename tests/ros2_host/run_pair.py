"""Actual independent ROS processes; no HDC, device driver or synthesized result."""
import argparse,json,os,subprocess,time,secrets,socket,re,hashlib
from pathlib import Path
from control_files import publish_control
p=argparse.ArgumentParser();p.add_argument('--exe',type=Path,required=True);p.add_argument('--bridge',type=Path,required=True);p.add_argument('--fastdds-prefix',type=Path,required=True);p.add_argument('--ros-prefix',type=Path,required=True);p.add_argument('--pixi-prefix',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--mode',choices=['reliable','best_effort','dual'],default='reliable');p.add_argument('--forward',choices=['0','1'],default='1');p.add_argument('--host-close-error',choices=['0','1'],default='0');p.add_argument('--paired-close',action='store_true');p.add_argument('--asymmetric-cleanup',action='store_true');p.add_argument('--reaccept-host',action='store_true');p.add_argument('--reaccept-origin',choices=['A','B'],default='A');p.add_argument('--owned-normal-reaccept',action='store_true');p.add_argument("--route-lifecycle",choices=["new","same"])
p.add_argument('--route-offline-writes',action='store_true');a=p.parse_args()
if a.reaccept_host and (a.mode!='dual' or a.forward!='1'):raise ValueError('Reaccept fixture requires dual opaque-byte forwarding')
if a.owned_normal_reaccept and not a.reaccept_host:raise ValueError('Owned normal model requires explicit Host reaccept')
if a.route_lifecycle and not a.owned_normal_reaccept:raise ValueError("route fixture requires retained-entity scene")
if a.route_offline_writes and not a.route_lifecycle:raise ValueError('offline-write fixture requires explicit route lifecycle')
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
 if a.reaccept_host:
  with conf.open('a') as stream:stream.write('host_reaccept=1\nreaccept_control='+str((a.output/(side+'-reaccept')).resolve())+'\n')
 if a.owned_normal_reaccept:
  with conf.open('a') as stream:stream.write('owned_normal_reaccept=1\nnative_reaccept_order='+('outgoing-first' if a.reaccept_origin=='A' else 'accepted-first')+'\n')
 if a.route_lifecycle:
  with conf.open("a") as stream:stream.write("route_lifecycle=1\n")
 if a.paired_close:
  with conf.open('a') as stream:stream.write('runtime_close_release='+str((a.output/(side+'-control.RUNTIME_CLOSE')).resolve())+'\n')
 env=os.environ.copy();env['PATH']=os.pathsep.join([str(a.fastdds_prefix/'bin'),str(a.ros_prefix/'bin'),str(a.pixi_prefix/'Library/bin'),env.get('PATH','')]);env['RMW_IMPLEMENTATION']='rmw_fastrtps_cpp';env['ROS_DOMAIN_ID']=str(domain);env['ROS_AUTOMATIC_DISCOVERY_RANGE']='SUBNET';env['FASTDDS_DSOFTBUS_REQUIRED_CONFIG']=str(conf.resolve());env['FASTDDS_ENVIRONMENT_FILE']='';env['SKIP_DEFAULT_XML']='1'
 env['FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX']=str((a.output/(side+'-control')).resolve())
 if a.owned_normal_reaccept:env['FDDST_BOUNDED_NATIVE_SCENE']='ROUTE_RETURN_ONCE' if a.route_offline_writes else 'BOUNDED_NORMAL_ONCE';env.pop('FDDST_HOST_REACCEPT_STAGE',None)
 elif a.reaccept_host:env['FDDST_HOST_REACCEPT_STAGE']='1';env.pop('FDDST_BOUNDED_NATIVE_SCENE',None)
 else:env.pop('FDDST_HOST_REACCEPT_STAGE',None);env.pop('FDDST_BOUNDED_NATIVE_SCENE',None)
 for k in ['FASTRTPS_DEFAULT_PROFILES_FILE','FASTDDS_DEFAULT_PROFILES_FILE','ROS_LOCALHOST_ONLY']:env.pop(k,None)
 out=(a.output/(side+'.stdout')).open('wb');err=(a.output/(side+'.stderr')).open('wb');streams[side]=(out,err)
 argv=[str(a.exe.resolve()),side,run,a.mode];commands[side]=argv;processes[side]=subprocess.Popen(argv,env=env,stdout=out,stderr=err)
phase_stems=['RELIABLE','BEST_EFFORT'] if a.mode=='dual' else ['RELIABLE' if a.mode=='reliable' else 'BEST_EFFORT']
stages=[stem+suffix for stem in phase_stems for suffix in ('_READY','_DONE')]+['TEARDOWN_READY']
if a.owned_normal_reaccept:stages=['PRE_READY','PRE_DONE','POST_DONE','TEARDOWN_READY']
releases=[];cleanup_sent=False;deadline=time.monotonic()+65;timed=False
held_b_cleanup=False;early_peer_close=False;runtime_releases=[]
reaccept_state='WAIT_ORIGINAL'
def reaccept_file(side,action):return a.output/(side+'-reaccept.'+action)
def reaccept_text(side,action):return f'HOST_REACCEPT run={run} side={"AB".index(side)} action={action}\n'
def signal_reaccept(side,action):publish_control(reaccept_file(side,action),reaccept_text(side,action))
def reaccept_seen(side,action):
 path=reaccept_file(side,action)
 return path.is_file() and path.read_text()==reaccept_text(side,action)
def native_control(side,phase):publish_control(reaccept_file(side,phase),f'NATIVE_CONTROL run={run} side={side} phase={phase}\n')
def native_seen(text,stage):return bool(re.search(r'^NATIVE_REACCEPT_EVENT .*stage='+stage+r' ',text,re.M))
def app_stage_seen(text,side,stage):return bool(re.search(r'^ROS_STAGE run='+run+r' side='+side+r' pid='+str(processes[side].pid)+r' stage='+stage+r'$',text,re.M))
def product_offline(text):return bool(re.search(r'^NATIVE_ROUTE_EVENT run='+run+r' .*stage=OFFLINE .*available=0 source=HOST_PUBLIC_NODE_MODEL ',text,re.M))
def topic_refusals(text):
 # PRE writes are sequence 1/2; the new OFFLINE ordinal 1001 is sequence 3.
 # This reads observation only; never supplies IDs, route values or ownership.
 # The record's submission_calls counts repeated submissions, not SDK calls;
 # with the same refused status are legitimate and do not waive route admission.
 publishers=dict(re.findall(r'^ROS_PUBLISHER .*gid=([0-9a-f]+).*phase=(reliable|best_effort) ',text,re.M))
 offline_events=[dict(re.findall(r'(\w+)=([^ ]+)',line)) for line in text.splitlines() if line.startswith('NATIVE_ROUTE_EVENT ') and 'stage=OFFLINE ' in line]
 if len(offline_events)!=1:return False
 revision=offline_events[0].get('revision');pre={}
 for line in text.splitlines():
  if not line.startswith('NATIVE_SENDER_SUBMISSION '):continue
  item=dict(re.findall(r'(\w+)=([^ ]+)',line))
  if item.get('run')==run and item.get('segment')=='PRE' and item.get('status')=='0' and item.get('queue_admitted')=='1' and item.get('sequence_hi')=='0' and item.get('sequence_lo') in ('1','2'):
   pre.setdefault((item.get('writer'),item.get('sender')),set()).add(item['sequence_lo'])
 rejected=set()
 for line in text.splitlines():
  if not line.startswith('NATIVE_SENDER_SUBMISSION '):continue
  item=dict(re.findall(r'(\w+)=([^ ]+)',line))
  if item.get('run')==run and item.get('available')=='0' and item.get('status')=='7' and item.get('sequence_hi')=='0' and item.get('sequence_lo')=='3' and item.get('source')=='HOST_PUBLIC_NODE_MODEL' and item.get('segment')=='OFFLINE' and item.get('queue_admitted')=='0' and item.get('route_known')=='1' and item.get('route_sample')=='ADMISSION_GATE' and item.get('route_revision')==revision and pre.get((item.get('writer'),item.get('sender')))=={'1','2'}:
   for gid,qos in publishers.items():
    if gid[:32]==item.get('writer'):rejected.add(qos)
 return rejected=={'reliable','best_effort'}
while any(x.poll() is None for x in processes.values()):
 texts={s:(a.output/(s+'.stdout')).read_text(errors='replace') for s in 'AB'}
 if a.route_lifecycle:
  ready=all(re.search(r"^ROS_STAGE run="+run+r" side="+s+r" pid="+str(processes[s].pid)+r" stage=PRE_DONE$",texts[s],re.M) for s in "AB")
  if reaccept_state=="WAIT_ORIGINAL" and ready:
   for s in "AB":native_control(s,"ARM")
   reaccept_state="ARMING"
  elif reaccept_state=="ARMING" and all(native_seen(texts[s],"ARMED") for s in "AB"):
   for s in "AB":native_control(s,"NODE_OFFLINE")
   reaccept_state="OFFLINE"
  elif reaccept_state=="OFFLINE" and all(native_seen(texts[s],"PEER_READY") for s in "AB"):
   if a.route_offline_writes:
    if all(product_offline(texts[s]) and app_stage_seen(texts[s],s,'OFFLINE_READY') for s in 'AB'):
     for s in 'AB':publish_control(a.output/(s+'-control.OFFLINE_READY'),f'ROS_RELEASE run={run} side={s} stage=OFFLINE_READY\n')
     reaccept_state='OFFLINE_WRITING'
   else:
    for s in "AB":native_control(s,"NODE_ONLINE_NEW" if a.route_lifecycle=="new" else "NODE_ONLINE_SAME")
    reaccept_state="RESOLVING"
  elif reaccept_state=='OFFLINE_WRITING' and all(app_stage_seen(texts[s],s,'OFFLINE_DONE') for s in 'AB'):
   for s in 'AB':native_control(s,'OFFLINE_CAPTURE')
   reaccept_state='OFFLINE_CAPTURE'
  elif reaccept_state=='OFFLINE_CAPTURE' and all(topic_refusals(texts[s]) for s in 'AB'):
   for s in 'AB':
    publish_control(a.output/(s+'-control.OFFLINE_DONE'),f'ROS_RELEASE run={run} side={s} stage=OFFLINE_DONE\n')
    native_control(s,'NODE_ONLINE_NEW' if a.route_lifecycle=='new' else 'NODE_ONLINE_SAME')
   reaccept_state='RESOLVING'
  elif reaccept_state=="RESOLVING" and all("HOST_NODE_QUERY run="+run in texts[s] for s in "AB"):
   for s in "AB":native_control(s,"REPLACE")
   reaccept_state="REPLACING"
  elif reaccept_state=="REPLACING" and all(native_seen(texts[s],"OWNER_PUBLISHED") for s in "AB"):
   for s in "AB":publish_control(a.output/(s+"-control.REACCEPT_DONE"),f"ROS_RELEASE run={run} side={s} stage=REACCEPT_DONE\n")
   reaccept_state="COMPLETE"
 elif a.owned_normal_reaccept:
  ready=all(re.search(r'^ROS_STAGE run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' stage=PRE_DONE$',texts[s],re.M) for s in 'AB')
  if reaccept_state=='WAIT_ORIGINAL' and ready:
   for s in 'AB':native_control(s,'ARM')
   reaccept_state='ARMING'
  elif reaccept_state=='ARMING' and all(native_seen(texts[s],'ARMED') for s in 'AB'):
   native_control(a.reaccept_origin,'END');reaccept_state='ENDING'
  elif reaccept_state=='ENDING' and native_seen(texts['A'],'OLD_OWNER_SETTLED') and native_seen(texts['B'],'PEER_READY'):
   native_control('A','REPLACE');reaccept_state='REPLACING'
  elif reaccept_state=='REPLACING' and all(native_seen(texts[s],'OWNER_PUBLISHED') for s in 'AB'):
   for s in 'AB':publish_control(a.output/(s+'-control.REACCEPT_DONE'),f'ROS_RELEASE run={run} side={s} stage=REACCEPT_DONE\n')
   reaccept_state='COMPLETE'
 elif a.reaccept_host:
  ready=all(re.search(r'^ROS_STAGE run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' stage=RELIABLE_REACCEPT_READY$',texts[s],re.M) for s in 'AB')
  if reaccept_state=='WAIT_ORIGINAL' and ready:
   for s in 'AB':signal_reaccept(s,'PAUSE')
   reaccept_state='PAUSING'
  elif reaccept_state=='PAUSING' and all(reaccept_seen(s,'PAUSED') for s in 'AB'):
   signal_reaccept(a.reaccept_origin,'BREAK_OUTGOING' if a.reaccept_origin=='A' else 'BREAK_ACCEPTED');reaccept_state='ENDING'
  elif reaccept_state=='ENDING' and reaccept_seen(a.reaccept_origin,'BROKEN') and all(reaccept_seen(s,'TERMINAL') for s in 'AB'):
   signal_reaccept('B','RESUME');reaccept_state='SETTLING'
  elif reaccept_state=='SETTLING' and reaccept_seen('B','SETTLED'):
   signal_reaccept('A','RESUME');reaccept_state='REPLACING'
  elif reaccept_state=='REPLACING' and all(reaccept_seen(s,'REPLACED') for s in 'AB'):
   for s in 'AB':publish_control(a.output/(s+'-control.RELIABLE_REACCEPT_READY'),f'ROS_RELEASE run={run} side={s} stage=RELIABLE_REACCEPT_READY\n')
   reaccept_state='COMPLETE'
 for stage in stages:
  if stage in releases:continue
  if all(re.search(r'^ROS_STAGE run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' stage='+stage+r'$',texts[s],re.M) for s in 'AB'):
   for s in 'AB':
    if stage=='TEARDOWN_READY':
     if s=='B' and a.asymmetric_cleanup:held_b_cleanup=True;continue
     publish_control(a.output/(s+'-control.CLEANUP'),f'ROS_CLEANUP schema=1 run={run} side={s} pid={processes[s].pid} stage=CLEANUP reason=NORMAL_COMPLETION\n')
    else:publish_control(a.output/(s+'-control.'+stage),f'ROS_RELEASE run={run} side={s} stage={stage}\n')
   releases.append(stage)
 if held_b_cleanup:
  ready_a=re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run='+run+r' side=A pid='+str(processes['A'].pid)+r' ',texts['A'],re.M)
  closed_a='FDDST_ROS_CLOSED run='+run in (a.output/'A.stderr').read_text(errors='replace') or processes['A'].poll() is not None
  if ready_a or closed_a:
   early_peer_close=bool(closed_a)
   publish_control(a.output/'B-control.CLEANUP',f'ROS_CLEANUP schema=1 run={run} side=B pid={processes["B"].pid} stage=CLEANUP reason=NORMAL_COMPLETION\n')
   held_b_cleanup=False
 if a.paired_close and not runtime_releases and all(re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run='+run+r' side='+s+r' pid='+str(processes[s].pid)+r' ',texts[s],re.M) for s in 'AB'):
  for s in 'AB':
   control_pid=processes[s].pid+(1 if close_control=='INVALID_PID' else 0)
   reason='WINDOW_FAILED' if close_control=='WINDOW_FAILED' else 'NORMAL_COMPLETION'
   publish_control(a.output/(s+'-control.RUNTIME_CLOSE'),f'ROS_RUNTIME_CLOSE_RELEASE schema=1 run={run} side={s} pid={control_pid} stage=RUNTIME_CLOSE reason={reason}\n')
   runtime_releases.append(s)
 if not cleanup_sent and any(re.search(r'^ROS_FAILURE schema=1 ',(a.output/(s+'.stderr')).read_text(errors='replace'),re.M) or 'NATIVE_REACCEPT_REFUSAL ' in texts[s] for s in 'AB'):
  for s in 'AB':
   f=a.output/(s+'-control.CLEANUP')
   if processes[s].poll() is None and not f.exists():publish_control(f,f'ROS_CLEANUP schema=1 run={run} side={s} pid={processes[s].pid} stage=CLEANUP reason=WINDOW_FAILED\n')
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
if a.owned_normal_reaccept:
 import sys
 sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/public_socket'))
 from analyze_bounded_native_reaccept import analyze,rows
 ledger=analyze(texts,run,'outgoing-first' if a.reaccept_origin=='A' else 'accepted-first','HOST_OPAQUE_PUBLIC_API_SHAPE',a.route_lifecycle)
 if a.route_offline_writes:
  offline={'source':'HOST_PUBLIC_NODE_MODEL','expected_writes':4,'writes':{},'takes':{},'errors':[]}
  for side,peer in (('A','B'),('B','A')):
   events=rows(texts[side],'NATIVE_ROUTE_EVENT')
   if [x.get('stage') for x in events]!=['OFFLINE','RESOLVED'] or not all(x.get('run')==run and x.get('side')==side and x.get('source')=='HOST_PUBLIC_NODE_MODEL' and x.get('sdk_epoch')=='NOT_PROVEN' for x in events):offline['errors'].append('product_route_events:'+side)
   elif events[0].get('available')!='0' or events[1].get('available')!='1' or int(events[1]['revision'])<=int(events[0]['revision']):offline['errors'].append('product_route_revision:'+side)
   if not topic_refusals(texts[side]):offline['errors'].append('original_topic_sender_offline_refusal:'+side)
   writes=[x for x in rows(texts[side],'ROS_WRITE') if x.get('segment')=='OFFLINE']
   takes=[x for x in rows(texts[side],'ROS_TAKE') if x.get('segment')=='OFFLINE']
   offline['writes'][side]=len(writes);offline['takes'][side]=len(takes)
   if len(writes)!=2 or {x.get('phase') for x in writes}!={'reliable','best_effort'} or not all(x.get('run')==run and x.get('side')==side and x.get('ordinal')=='1001' and x.get('payload')==f'{run}:{x["phase"]}:{side}:1001' and x.get('publish_return')=='OK' for x in writes):offline['errors'].append('offline_write_ledger:'+side)
   if len(takes)>2 or len({x.get('phase') for x in takes})!=len(takes) or not all(x.get('run')==run and x.get('side')==side and x.get('ordinal')=='1001' and x.get('phase') in ('reliable','best_effort') and x.get('payload')==f'{run}:{x["phase"]}:{peer}:1001' for x in takes):offline['errors'].append('offline_history_ledger:'+side)
   if len(events)==2:
    lines=texts[side].splitlines();off=next((i for i,line in enumerate(lines) if line.startswith('NATIVE_ROUTE_EVENT ') and 'stage=OFFLINE ' in line),-1);resolved=next((i for i,line in enumerate(lines) if line.startswith('NATIVE_ROUTE_EVENT ') and 'stage=RESOLVED ' in line),-1)
    if not all(off<i<resolved for i,line in enumerate(lines) if line.startswith('ROS_WRITE ') and 'segment=OFFLINE ' in line):offline['errors'].append('offline_write_causality:'+side)
  offline['pass']=not offline['errors'];ledger['offline']=offline
  ledger['errors'].extend(offline['errors']);ledger['pass']=not ledger['errors']
 ledger['boards']={}
 for s in 'AB':
  emitted=rows(texts[s],'ROS_MAIN')
  ledger['boards'][s]={'pid':emitted[0].get('pid') if len(emitted)==1 else None,'phases':{q:{event:len([x for x in rows(texts[s],event) if x.get('run')==run and x.get('phase')==q]) for event in ('ROS_WRITE','ROS_TAKE')} for q in phases}}
else:ledger=module.analyze(texts,run,phases)
result['ledger']=ledger
result['route_offline_writes']=a.route_offline_writes
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
if a.owned_normal_reaccept:
 result['reaccept_host']={'state':reaccept_state,'origin':a.reaccept_origin,'source':'HOST_OPAQUE_PUBLIC_API_SHAPE','sdk_epoch':'NOT_PROVEN'}
 if reaccept_state!='COMPLETE' or not ledger['pass']:errors.append('owned_normal_incomplete')
elif a.reaccept_host:
 result['reaccept_host']={'state':reaccept_state,'origin':a.reaccept_origin,'source':'HOST_OPAQUE_BYTES_PUBLIC_CALLBACK_SHAPE','sdk_epoch':'NOT_PROVEN'}
 if reaccept_state!='COMPLETE':errors.append('reaccept_incomplete')
 for side in 'AB':
  anchors=re.findall(r'^ROS_REACCEPT_ANCHOR run='+run+r' side='+side+r' pid='+str(processes[side].pid)+r' point=(BEFORE|AFTER) node=(\S+) publisher=(\S+) subscriber=(\S+) context=(\S+) gid=(\S+)$',texts[side],re.M)
  if len(anchors)!=2 or anchors[0][0]!='BEFORE' or anchors[1][0]!='AFTER' or anchors[0][1:]!=anchors[1][1:]:errors.append('reaccept_entity_identity:'+side)
  err=(a.output/(side+'.stderr')).read_text(errors='replace')
  changes=re.findall(r'HOST_REACCEPT_REPLACED run='+run+r' side=\d+ old_id=(\d+) new_id=(\d+) source=HOST_PUBLIC_C_API sdk_epoch=NOT_PROVEN',err)
  if len(changes)!=1 or changes[0][0]==changes[0][1]:errors.append('reaccept_native_replacement:'+side)
result['errors']=errors;result['pass']=not timed and all(x.returncode==0 for x in processes.values()) and not errors
(a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0 if result['pass'] else 1)
