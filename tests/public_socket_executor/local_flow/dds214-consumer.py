"""Local subprocess/control model only; never DDS, native SDK or device proof."""
import os,sys,time,hashlib
from pathlib import Path
from finite_model import read,write,root_of,event
w=Path(sys.argv[1]);side=sys.argv[2];sandbox=Path(sys.argv[3]);c=read(w/'window.json');run=c['run'];peer='B' if side=='A' else 'A';pid=os.getpid();root=root_of(sandbox,c['remote'][side])
def emit(s):print(s,flush=True)
def wait_file(p):
    end=time.monotonic()+20
    while not p.exists() and time.monotonic()<end:time.sleep(.01)
    if not p.exists():raise TimeoutError('LOCAL_CONTROL_OR_MAILBOX_MISSING')
def gate(stage,wire):
    p=root/('control.'+stage);wait_file(p)
    if p.read_text()!=f'{wire} run={run} side={side} stage={stage}\n':raise ValueError('CONTROL_IDENTITY')
def gid(s,role):return hashlib.md5((run+s+role).encode()).hexdigest()
write(sandbox/'processes'/f'{run}-{side}.json',dict(pid=pid,starttime=time.monotonic_ns(),remote_argv=[c['remote'][side]+'/'+c['main_member'],side,run,'normal'],scope='LOCAL_MODEL_NOT_DDS_OR_DEVICE'))
event(sandbox,dict(event='PROCESS_START',run=run,side=side,pid=pid,scope='LOCAL_MODEL'))
try:
    emit(f'DDS214_MAIN scenario=DDS214_LAYER_BASELINE run={run} side={side} pid={pid} mode=normal')
    emit(f'DDS214_LOCAL_MODEL run={run} side={side} scope=EXECUTOR_ONLY_NOT_NATIVE')
    emit(f'ROS_LISTENER_READY run={run} side={side} pid={pid} listener=1 fake_sdk=0 source=LOCAL_MODELED_EXPECTED_TEXT')
    gate('LISTENER_READY','ROS_RELEASE')
    emit(f'DDS214_ID run={run} side={side} participant={gid(side,"participant")} writer={gid(side,"writer")} reader={gid(side,"reader")} writer_topic=dds214_{run}_from_{side} reader_topic=dds214_{run}_from_{peer}')
    emit(f'DDS214_MATCH run={run} side={side} writers=1 readers=1')
    emit(f'DDS214_STAGE run={run} side={side} pid={pid} stage=MATCH');gate('MATCH','DDS214_RELEASE')
    for n in range(1,6):
        emit(f'DDS214_WRITE run={run} side={side} ordinal={n} ok=1 writer={gid(side,"writer")} sn={n}')
        write(sandbox/'mailboxes'/f'{run}-{side}-{n}.json',dict(run=run,side=side,ordinal=n,sn=n,writer=gid(side,'writer')))
    for n in range(1,6):
        p=sandbox/'mailboxes'/f'{run}-{peer}-{n}.json';wait_file(p);r=read(p)
        if r!=dict(run=run,side=peer,ordinal=n,sn=n,writer=gid(peer,'writer')):raise ValueError('LOCAL_MAILBOX_IDENTITY')
        emit(f'DDS214_TAKE run={run} side={side} ordinal={n} writer={r["writer"]} reader={gid(side,"reader")} sn={r["sn"]} payload_ok=1')
    emit(f'DDS214_WAIT run={run} side={side} rc=0 scope=LOCAL_MODEL_NOT_DDS_WAIT')
    emit(f'DDS214_DATA_DONE run={run} side={side} writes=5 takes=5');emit(f'DDS214_STAGE run={run} side={side} pid={pid} stage=DATA_DONE');gate('DATA_DONE','DDS214_RELEASE')
    emit(f'DDS214_DELETE_ENTITIES run={run} side={side} rc=0')
    emit(f'DDS214_DELETE_PARTICIPANT_ENTER run={run} side={side}')
    emit(f'FDDST_ROS_CLOSED run={run} side={0 if side=="A" else 1} pump_joined=1 runtime_closed=1 local_only=1 source=LOCAL_MODELED_EXPECTED_TEXT')
    emit(f'DDS214_DELETE_PARTICIPANT_RETURN run={run} side={side} rc=0');emit(f'DDS214_EXIT run={run} side={side} result=0')
except Exception as e:
    emit(f'DDS214_FAILURE run={run} side={side} stage=LOCAL_FLOW reason=LOCAL_FLOW_FAILURE');sys.exit(4)
finally:event(sandbox,dict(event='PROCESS_EXIT',run=run,side=side,pid=pid,scope='LOCAL_MODEL'))
