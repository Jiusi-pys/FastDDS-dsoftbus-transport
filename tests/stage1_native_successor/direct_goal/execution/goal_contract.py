"""Fixed first-current-DDS plan data. Importing/rendering never starts a tool."""
from dataclasses import dataclass
from pathlib import Path,PurePosixPath
import hashlib,json,re

HDC='C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe'
HDC_SHA='99043a8267aec70e2aa78c056a8d2de59505e1c8bb96ced3c9ac6aad7c834b7c'
TARGETS={'A':'3e01ff55454d202020104033bf453b00','B':'3e01ff55454d202020104433991c3b00'}
OWN='mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16'
PACKAGE='com.kaihong.mdds'
SCOPE='ONE_NEW_FIRST_CURRENT_STOCK_DDS_AB_PROBE_AND_DATA'
NONCE='5c7a9e21-directdds-d01'
DEVICE_ROOT='/data/local/tmp/mdds_direct_20261030_5c7a9e21'
RUN_ROOT=Path('E:/Codex-FastDDS-stage1-directdds-once-20261030_5c7a9e21')
ACTIVE_ROOT=Path('E:/Codex-FastDDS-stage1-directdds-mainpath-20261030-d01/goal-active-d02')
ENDPOINT='127.0.0.1:8710'
LIMITS=dict(total_ms=240000,client_ms=15000,long_ms=125000,bind_ms=2000,stop_ms=2000,
            clients=128,concurrent_clients=3,private=1355161600,ws=268435456,
            aggregate_private=5420646400,aggregate_ws=1073741824,
            start_commit=8589934592,start_physical=4294967296,
            stop_commit=4294967296,stop_physical=2147483648,commit_delta=6442450944,
            server_capture=4194304,client_capture=65536,decoded=327680,
            active_bytes=33554432,run_bytes=67108864,total_bytes=100663296,
            files=1536,cache_peak=131072,system_epochs=13000,process_samples=26000,api_journal_bytes=50331648,
            disk_min=1073741824,closing_reserve=262144,board_total_kib=4194304,board_available_kib=1048576,board_disk_kib=65536,native_fsize=1048576)

class ContractError(Exception):pass
def need(ok,code):
    if not ok:raise ContractError(code)
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode('ascii')
def sha(value):return hashlib.sha256(value).hexdigest()
def shell_quote(value):
    need(isinstance(value,str) and value and len(value)<=4096 and not any(ord(c)<32 or ord(c)>126 for c in value),'SHELL_VALUE')
    return "'"+value.replace("'","'\"'\"'")+"'"
def local_path(value):
    p=Path(value);need(p.is_absolute() and p.resolve().is_relative_to(RUN_ROOT.resolve()),'LOCAL_RUN_PATH');return str(p)

@dataclass(frozen=True)
class GoalStep:
    id:str
    phase:str
    side:str
    args:tuple
    kind:str='short'
    expected_remote:int=0
    requires:tuple=()
    executable:str=HDC
    executable_sha256:str=HDC_SHA
    requires_started:tuple=()
    requires_terminal:tuple=()
    byte_source:str=''
    def argv(self):return (self.executable,*self.args)

def registry_digest(steps):
    from dataclasses import asdict
    return sha(canonical([asdict(step) for step in steps]))

def device_args(side,body):
    need(side in TARGETS,'FIXED_TARGET')
    return ('-p','-s',ENDPOINT,'-t',TARGETS[side],'shell',body)

def shell_step(identifier,phase,side,body,*,kind='short',expected=0,requires=()):
    need(re.fullmatch(r'[a-z0-9_.-]{1,64}',identifier) is not None,'STEP_ID')
    # The body is created only by the fixed builders in this module, never
    # deserialized from an authorization file or accepted as interactive input.
    wrapped='( '+body+' ); rc=$?; printf "\\nS1_GOAL_STEP:'+identifier+':%s\\n" "$rc"; exit 0'
    return GoalStep(identifier,phase,side,device_args(side,wrapped),kind,expected,tuple(requires))

def transfer_step(identifier,phase,side,local,relative,*,receive=False,requires=()):
    need(side in TARGETS,'FIXED_TARGET');parts=PurePosixPath(relative).parts
    need(parts and not PurePosixPath(relative).is_absolute() and all(re.fullmatch(r'[A-Za-z0-9_.+-]+',p) and p not in ('.','..') for p in parts),'DEVICE_RELATIVE')
    remote=DEVICE_ROOT+'/'+relative
    args=('-p','-s',ENDPOINT,'-t',TARGETS[side],'file','recv' if receive else 'send')
    paths=(remote,local_path(local)) if receive else (str(Path(local)),remote)
    return GoalStep(identifier,phase,side,(*args,*paths),'receive' if receive else 'send',0,tuple(requires))

def publication_steps(window,side,name,source,requires=()):
    need(window in ('probe','data') and side in TARGETS,'WINDOW_SIDE')
    allowed={'pair','admission.'+side,'cancel.'+side,*{'release.'+side+'.'+str(n) for n in range(1,5)}}
    need(name in allowed,'CONTROL_NAME')
    key=window+'.'+side.lower()+'.'+name.lower()
    incoming=DEVICE_ROOT+'/'+window+'/'+name+'.incoming';dest=DEVICE_ROOT+'/'+window+'/'+name
    send=transfer_step(key+'.send',window,side,source,window+'/'+name+'.incoming',requires=requires)
    body='set -C; test -f '+shell_quote(incoming)+' && test ! -L '+shell_quote(incoming)+' && test ! -e '+shell_quote(dest)+' && test ! -L '+shell_quote(dest)+' && mv '+shell_quote(incoming)+' '+shell_quote(dest)
    from dataclasses import replace
    verify=replace(shell_step(key+'.verify',window,side,'sha256sum '+shell_quote(incoming),requires=(send.id,)),byte_source=local_path(source))
    publish=shell_step(key+'.publish',window,side,body,requires=(verify.id,))
    return send,verify,publish

def wait_regular(path):
    """One fixed remote observation, never a retry of a command/transfer.

    /proc/uptime supplies the elapsed-clock seconds for one absolute deadline.
    Its availability and shell utilities are runtime prerequisites, not assumed
    board facts. The independent original-handle client deadline remains 15s.
    """
    q=shell_quote(path)
    clock='read tick rest < /proc/uptime || exit 122; seconds=${tick%%.*}; case "$seconds" in ""|*[!0-9]*) exit 122;; esac; '
    return clock+'end=$((seconds+10)); while test ! -e '+q+'; do '+clock+'test "$seconds" -lt "$end" || exit 124; sleep 0.05 || exit 122; done; test -f '+q+' && test ! -L '+q

def launch_step(window,side):
    need(window in ('probe','data') and side in TARGETS,'WINDOW_SIDE');d=DEVICE_ROOT+'/'+window
    argv=[DEVICE_ROOT+'/goal_supervisor','120',d+'/app.out',d+'/app.err',d+'/tmp',DEVICE_ROOT+'/own_goal_launcher','--wait-owned',side,d,'15000']
    body=' '.join(shell_quote(s) for s in argv)+'; rc=$?; printf "\\nS1_GOAL_REMOTE_RC:%s\\n" "$rc"; exit 0'
    return shell_step(window+'.'+side.lower()+'.launch',window,side,body,kind='long')

def validate_registry(steps):
    ids=set();longs=0
    for step in steps:
        need(isinstance(step,GoalStep) and step.id not in ids,'DUPLICATE_STEP');ids.add(step.id)
        need(step.kind in ('server','short','long','send','receive','controller'),'STEP_KIND')
        if step.kind=='server':need(step.executable==HDC and step.executable_sha256==HDC_SHA and step.args==('-s',ENDPOINT,'-m') and not step.side,'FIXED_SERVER')
        elif step.id=='targets':need(step.executable==HDC and step.args==('-p','-s',ENDPOINT,'list','targets','-v') and not step.side,'FIXED_TARGETS')
        elif step.kind!='controller':need(step.executable==HDC and step.executable_sha256==HDC_SHA and step.side in TARGETS and step.args[:5]==('-p','-s',ENDPOINT,'-t',TARGETS[step.side]),'CLIENT_PREFIX_TARGET')
        else:need(not step.side and Path(step.executable).name=='goal_controller.exe' and step.args[0]=='--coordinate','FIXED_CONTROLLER')
        longs+=step.kind=='long'
    need(sum(s.kind=='server' for s in steps)==1 and sum(s.kind not in ('server','controller') for s in steps)<=LIMITS['clients'] and longs==4,'FINITE_BATCH_SHAPE')
    dependencies=lambda s:set(s.requires)|set(s.requires_started)|set(s.requires_terminal)
    need(all(dependencies(s)<=ids and s.id not in dependencies(s) for s in steps),'STEP_DEPENDENCY')
    registry={s.id:s for s in steps};done=set()
    while len(done)<len(registry):
        ready={s.id for s in steps if dependencies(s)<=done}-done
        need(bool(ready),'DEPENDENCY_CYCLE');done.update(ready)
    return registry

def build_registry(payloads,readonly_commands,controller):
    """Fixed finite recipe. Sources/payloads come from the frozen manifest.

    Dynamic BOOT/admission/pair/release bytes use fixed local paths under this
    run; the driver must qualify their content/provenance before step acceptance.
    """
    from dataclasses import replace
    need(len(payloads)==11 and len(readonly_commands)==5,'EXACT_INPUT_SHAPE')
    steps=[GoalStep('server','channel','',('-s',ENDPOINT,'-m'),'server')]
    for n,row in enumerate(readonly_commands):
        argv=tuple(row['argv']);need(argv[0].replace('\\','/')==HDC and argv[1:4]==('-p','-s',ENDPOINT),'QUALIFIED_READONLY_PREFIX')
        side=next((s for s,target in TARGETS.items() if '-t' in argv and argv[argv.index('-t')+1]==target),'');kind='short'
        if n==0:
            need(argv[4:]==('list','targets','-v'),'FIXED_TARGETS')
            # targets is the one client without a -t selector, verified below.
            steps.append(GoalStep('targets','channel','',argv[1:],kind,0,('server',)))
        else:steps.append(GoalStep('readonly.'+str(n),'channel',side,argv[1:],kind,0,('targets',)))
    for side in TARGETS:
        s=side.lower();d=DEVICE_ROOT
        directories=[d,d+'/lib',d+'/probe',d+'/probe/tmp',d+'/data',d+'/data/tmp',d+'/ro',d+'/ro/tmp']
        body='umask 077; test ! -e '+shell_quote(d)+' && test ! -L '+shell_quote(d)+' && mkdir '+' '.join(shell_quote(x) for x in directories)
        steps.append(shell_step('stage.'+s+'.mkdir','staging',side,body,requires=tuple('readonly.'+str(n) for n in range(1,5))))
        sends=[]
        for n,payload in enumerate(payloads):
            step=transfer_step('stage.'+s+'.file'+str(n),'staging',side,payload['source'],payload['target'],requires=('stage.'+s+'.mkdir',));steps.append(step);sends.append(step.id)
        verify=' && '.join('test -f '+shell_quote(d+'/'+p['target'])+' && test ! -L '+shell_quote(d+'/'+p['target'])+' && test "$(sha256sum '+shell_quote(d+'/'+p['target'])+' | cut -d " " -f1)" = '+shell_quote(p['sha256']) for p in payloads)
        executables=[d+'/'+p['target'] for p in payloads if p.get('executable')]
        body=verify+' && chmod 0700 '+' '.join(shell_quote(x) for x in executables)
        steps.append(shell_step('stage.'+s+'.verified','staging',side,body,requires=tuple(sends)))
        # Only this new program-produced query metadata is eligible for the
        # future explicit source/content allowlist; no old SDK/HDC stream.
        out=d+'/ro/query.metadata';err=d+'/ro/query.stderr';helper=[d+'/goal_supervisor','8',out,err,d+'/ro/tmp','/lib/ld-musl-aarch64.so.1','--library-path',d+'/lib',d+'/readonly_frontend','--query-own',d+'/lib/libstage1_actionbag_identity.so']
        body=' '.join(shell_quote(x) for x in helper)+'; rc=$?; if test "$rc" = 0; then sed -n '+shell_quote('/^OWN_QUERY /p')+' '+shell_quote(out)+'; fi; printf "\\nS1_GOAL_REMOTE_RC:%s\\n" "$rc"; exit 0'
        steps.append(shell_step('own.'+s,'own',side,body,requires=('stage.'+s+'.verified',)))
    for window in ('probe','data'):
        for side in TARGETS:
            s=side.lower();launch=launch_step(window,side)
            dependencies=('own.a','own.b') if window=='probe' else ('probe.a.result','probe.b.result')
            steps.append(GoalStep(launch.id,launch.phase,launch.side,launch.args,launch.kind,0,dependencies))
            source=DEVICE_ROOT+'/'+window+'/boot.'+side
            body=wait_regular(source)+' && cat '+shell_quote(source)
            boot=replace(shell_step(window+'.'+s+'.boot',window,side,body),requires_started=(launch.id,));steps.append(boot)
            steps.extend(publication_steps(window,side,'admission.'+side,RUN_ROOT/'control'/window/('admission.'+side),requires=(boot.id,)))
            if window=='data':
                source=DEVICE_ROOT+'/data/node.'+side
                steps.append(shell_step('data.'+s+'.node','data',side,wait_regular(source)+' && cat '+shell_quote(source),requires=('data.'+s+'.admission.'+s+'.publish',)))
                steps.extend(publication_steps('data',side,'pair',RUN_ROOT/'control/data/pair',requires=('data.a.node','data.b.node')))
                ready='data.'+s+'.journal-ready'
                steps.append(shell_step(ready,'data',side,wait_regular(DEVICE_ROOT+'/data/events.'+side),requires=('data.'+s+'.pair.publish',)))
                for poll in range(12):steps.append(transfer_step('poll.'+str(poll)+'.'+s,'data',side,RUN_ROOT/'snapshots'/('events.'+side+'.'+str(poll)),'data/events.'+side,receive=True,requires=(ready,)))
                for sequence in range(1,5):steps.extend(publication_steps('data',side,'release.'+side+'.'+str(sequence),RUN_ROOT/'control/data'/('release.'+side+'.'+str(sequence)),requires=('data.'+s+'.pair.publish',)))
            names=['probe.'+side,'exit.'+side] if window=='probe' else ['data-result.'+side,'exit.'+side]
            body=' && '.join('test -f '+shell_quote(DEVICE_ROOT+'/'+window+'/'+name)+' && cat '+shell_quote(DEVICE_ROOT+'/'+window+'/'+name) for name in names)
            dep=(window+'.'+s+'.admission.'+s+'.publish',)
            steps.append(replace(shell_step(window+'.'+s+'.result',window,side,body,requires=dep),requires_terminal=(launch.id,)))
            if window=='data':
                cancellation=publication_steps('data',side,'cancel.'+side,RUN_ROOT/'control/data'/('cancel.'+side))
                steps.extend((replace(cancellation[0],requires_started=(launch.id,)),*cancellation[1:]))
    # The existing Windows controller is a separately pinned NON-HDC process;
    # its process lifetime does not authenticate the Native event producers.
    steps.append(GoalStep('controller','data','',('--coordinate',str(RUN_ROOT/'control/data'),'110000'),'controller',0,('data.a.node','data.b.node'),controller['path'].replace('\\','/'),controller['sha256']))
    return steps
