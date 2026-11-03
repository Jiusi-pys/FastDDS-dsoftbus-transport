"""ROS scenario text/control producer for executor tests, never ROS or SDK proof.

Uses actual task-owned peer mailboxes and process/control files. The real ROS
application is independently tested; this child tests only executor mechanics.
"""
import hashlib
import os
from pathlib import Path
import sys
import time
from finite_model import read,write,root_of,event,fault

w=Path(sys.argv[1]);side=sys.argv[2];sandbox=Path(sys.argv[3]);c=read(w/'window.json')
run=c['run'];peer='B' if side=='A' else 'A';root=root_of(sandbox,c['remote'][side]);pid=os.getpid()
gid=lambda board,phase:hashlib.md5((run+board+phase).encode()).hexdigest()
def emit(text):print(text,flush=True)
def wait_file(path):
    end=time.monotonic()+15
    while not path.exists() and time.monotonic()<end:time.sleep(.01)
    if not path.exists():raise TimeoutError('LOCAL_CONTROL_OR_MAILBOX_MISSING')
def stage(name,wait=True):
    emit(f'ROS_STAGE run={run} side={side} pid={pid} stage={name}')
    if wait:
        path=root/('control.CLEANUP' if name=='TEARDOWN_READY' else 'control.'+name);wait_file(path)
        expected=f'ROS_CLEANUP schema=1 run={run} side={side} pid={pid} stage=CLEANUP reason=NORMAL_COMPLETION\n' if name=='TEARDOWN_READY' else f'ROS_RELEASE run={run} side={side} stage={name}\n'
        if path.read_text()!=expected:raise ValueError('CONTROL_IDENTITY')
        if name=='TEARDOWN_READY':emit(f'ROS_CLEANUP_ENTERED schema=1 run={run} side={side} pid={pid} stage=CLEANUP reason=NORMAL_COMPLETION')
write(sandbox/'processes'/f'{run}-{side}.json',dict(run=run,side=side,pid=pid,starttime=time.monotonic_ns(),
    remote_argv=[c['remote'][side]+'/'+c['main_member'],side,run,'dual'],scope='LOCAL_FAKE_CONSUMER_NOT_ROS_OR_DEVICE'))
event(sandbox,dict(event='CONSUMER_STARTED',run=run,side=side,pid=pid))
try:
    mode=fault(sandbox)
    reported_run='f'*16 if mode=='wrong_run' else run
    reported_side=peer if mode=='wrong_side' else side
    reported_pid=0 if mode=='wrong_pid' else pid
    prefix='WRONG_MAIN' if mode=='wrong_main' else 'ROS_MAIN'
    emit(f'{prefix} scenario=ROS2_DSOFTBUS run={reported_run} side={reported_side} pid={reported_pid} mode=dual')
    emit(f'ROS_LOCAL_MODEL scope=LOCAL_FAKE_CONSUMER_NOT_ROS_OR_DEVICE run={run} side={side}')
    if mode in ('wrong_main','wrong_run','wrong_side','wrong_pid','early_collector') or (mode=='b_not_ready' and side=='B'):raise ValueError('INTENTIONAL_LOCAL_NEGATIVE_'+mode)
    emit(f'ROS_LISTENER_READY run={run} side={side} pid={pid} listener=1 fake_sdk=0 source=LOCAL_MODELED_EXPECTED_TEXT')
    stage('LISTENER_READY')
    for phase in ('reliable','best_effort'):
        stem='RELIABLE' if phase=='reliable' else 'BEST_EFFORT'
        emit(f'ROS_PUBLISHER run={run} side={side} gid={gid(side,phase)} intra_process=0 phase={phase} topic=/fdds_{run}/{phase}/from_{side}')
        # The remote process exists and participates in the same control round;
        # this is a modeled graph record, not a real ROS graph assertion.
        wait_file(sandbox/'processes'/f'{run}-{peer}.json')
        emit(f'ROS_GRAPH run={run} side={side} peer=/fdds_{run}/transport_{peer} visible=1 subscription_count=1 phase={phase}')
        stage(stem+'_READY')
        for ordinal in range(1,6):
            payload=f'{run}:{phase}:{side}:{ordinal}'
            write(sandbox/'mailbox'/run/phase/side/f'{ordinal}.json',dict(run=run,phase=phase,side=side,ordinal=ordinal,payload=payload,gid=gid(side,phase)))
            emit(f'ROS_WRITE run={run} side={side} ordinal={ordinal} payload={payload} phase={phase} topic=/fdds_{run}/{phase}/from_{side}')
        for ordinal in range(1,6):
            path=sandbox/'mailbox'/run/phase/peer/f'{ordinal}.json';wait_file(path);sample=read(path)
            if sample['run']!=run or sample['phase']!=phase or sample['side']!=peer or sample['ordinal']!=ordinal:raise ValueError('MAILBOX_IDENTITY')
            if mode=='missing_take' and side=='B' and phase=='reliable' and ordinal==5:continue
            writer='0'*32 if mode=='wrong_writer' and side=='B' else sample['gid']
            emit(f'ROS_TAKE run={run} side={side} ordinal={sample["ordinal"]} gid={writer} payload={sample["payload"]} phase={phase} topic={phase}/from_{peer} publication_sequence={ordinal}')
        req=sandbox/'services'/run/(phase+'-request.json');res=sandbox/'services'/run/(phase+'-response.json')
        if side=='A':
            write(req,dict(run=run,phase=phase,a=3,b=4,id=1,writer=gid('A',phase+'service')))
            emit(f'ROS_SERVICE_SENT run={run} phase={phase} request_id=1 a=3 b=4');wait_file(res);reply=read(res)
            if reply['run']!=run or reply['phase']!=phase or reply['id']!=1:raise ValueError('SERVICE_RESPONSE_IDENTITY')
            emit(f'ROS_SERVICE_RESPONSE run={run} sum={reply["sum"]} phase={phase} request_id={reply["id"]}')
        else:
            wait_file(req);request=read(req)
            if request['run']!=run or request['phase']!=phase:raise ValueError('SERVICE_REQUEST_IDENTITY')
            value=request['a']+request['b'];write(res,dict(run=run,phase=phase,id=request['id'],sum=value))
            emit(f'ROS_SERVICE_REQUEST run={run} a={request["a"]} b={request["b"]} sum={value} phase={phase} request_id={request["id"]} writer={request["writer"]}')
        emit(f'ROS_BUSINESS_DONE run={run} side={side} writes=5 takes=5 phase={phase}');stage(stem+'_DONE')
    stage('TEARDOWN_READY')
    emit(f'ROS_DDS_DELETED run={run} side={side} pid={pid} dds_component_result=0 api_return=NOT_OBSERVED scope=FACTORY_POST_DELETE')
    stage('DDS_DELETED',False)
    if mode!='missing_runtime_close':emit(f'ROS_CONTEXT_CLOSED run={run} side={side} result=0')
    event(sandbox,dict(event='CONSUMER_COMPLETED',run=run,side=side,pid=pid,rc=0))
except Exception as error:
    event(sandbox,dict(event='CONSUMER_FAILED',run=run,side=side,pid=pid,error=str(error)))
    print(str(error),file=sys.stderr,flush=True);sys.exit(1)
