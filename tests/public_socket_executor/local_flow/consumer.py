"""Stage-controlled local text producer, not an SDK/DDS implementation."""
import hashlib,json,os,sys,time
from pathlib import Path
from finite_model import read,write,root_of,event,fault

w=Path(sys.argv[1]);side=sys.argv[2];sandbox=Path(sys.argv[3]);c=read(w/'window.json');run=c['run'];other='B' if side=='A' else 'A';root=root_of(sandbox,c['remote'][side]);pid=os.getpid()
guid=lambda s,kind:hashlib.md5((run+s+kind).encode()).hexdigest()
def emit(s):print(s,flush=True)
def stage(name,wait=True):
 emit(f'INST_STAGE run={run} side={side} pid={pid} stage={name}')
 if not wait:return
 deadline=time.monotonic()+15;p=root/('control.'+name)
 while time.monotonic()<deadline:
  if p.exists():
   if p.read_text()!=run+' '+name+'\n':raise ValueError('CONTROL_IDENTITY')
   return
  time.sleep(.015)
 raise TimeoutError('CONTROL_NOT_RELEASED_'+name)
args=[c['remote'][side]+'/'+c['main_member'],'0' if side=='A' else '1',c['peer'][side],c['package_name'],c['service_prefix'],run,c['token'][side],c['remote'][side]+'/control']
write(sandbox/'processes'/f'{run}-{side}.json',dict(run=run,side=side,pid=pid,starttime=time.monotonic_ns(),remote_argv=args,scope='LOCAL_FAKE_CONSUMER_NOT_DEVICE_PROC'))
event(sandbox,dict(event='CONSUMER_STARTED',run=run,side=side,pid=pid))
try:
 mode=fault(sandbox);reported_run='f'*16 if mode=='wrong_run' else run;reported_side=other if mode=='wrong_side' else side;reported_pid=0 if mode=='wrong_pid' else pid;prefix='WRONG_MAIN' if mode=='wrong_main' else 'INST_MAIN'
 emit(f'{prefix} run={reported_run} side={reported_side} pid={reported_pid} mode=installed_runtime source=LOCAL_FAKE_CONSUMER')
 if mode in ('wrong_main','wrong_run','wrong_side','wrong_pid','early_collector') or (mode=='b_not_ready' and side=='B'):raise ValueError('INTENTIONAL_LOCAL_PRODUCER_NEGATIVE_'+mode)
 stage('LISTENER_READY')
 emit(f'INST_ID run={run} side={side} participant={guid(side,"p")} writer={guid(side,"w")} reader={guid(side,"r")}')
 emit(f'INST_MATCH run={run} side={side} writer={guid(side,"w")} reader={guid(side,"r")} pub_matched=1 sub_matched=1')
 stage('MATCH')
 # Writes produce local immutable mailbox records; takes read the other child's
 # actual file. No expected value is inserted as an observed delivery count.
 for ordinal in range(1,6):
  write(sandbox/'mailbox'/run/side/f'{ordinal}.json',dict(run=run,ordinal=ordinal,writer=guid(side,'w'),writer_field=1 if side=='A' else 2,payload='local-fixture-payload'))
  emit(f'INST_WRITE run={run} side={side} writer={guid(side,"w")} ordinal={ordinal} rc=0')
 for ordinal in range(1,6):
  p=sandbox/'mailbox'/run/other/f'{ordinal}.json';deadline=time.monotonic()+5
  while not p.exists() and time.monotonic()<deadline:time.sleep(.01)
  sample=read(p)
  if sample['run']!=run or sample['ordinal']!=ordinal or sample['payload']!='local-fixture-payload':raise ValueError('BAD_MAILBOX')
  if mode=='missing_take' and side=='B' and ordinal==5:continue
  writer='0'*32 if mode=='wrong_writer' and side=='B' else sample['writer']
  emit(f'INST_TAKE run={run} side={side} reader={guid(side,"r")} remote_writer={writer} ordinal={sample["ordinal"]} writer_field={sample["writer_field"]} run_field={int(run,16)} payload_ok=1')
 stage('DATA_DONE')
 emit(f'INST_DELETE run={run} side={side} entities_rc=0 participant_rc=0')
 stage('DDS_DELETED',False)
 if mode!='missing_runtime_close':emit(f'INST_EXIT run={run} side={side} result=0 restored=1')
 event(sandbox,dict(event='CONSUMER_COMPLETED',run=run,side=side,pid=pid,rc=0))
except Exception as e:
 event(sandbox,dict(event='CONSUMER_FAILED',run=run,side=side,pid=pid,error=str(e)))
 print(str(e),file=sys.stderr,flush=True);sys.exit(1)
