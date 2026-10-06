"""Bounded exact route-test network commands. No arbitrary command/target input.

Private raw evidence stays in the original window. The imported collector is
hash-bound before import and kills only its owned local client on timeout.
"""
import argparse,hashlib,importlib.util,json,re,subprocess,sys,time
from pathlib import Path

WRAPPER='; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def need(value,reason):
    if not value:raise ValueError(reason)
def write(path,value):
    with Path(path).open('x',encoding='utf-8') as stream:json.dump(value,stream,indent=2);stream.write('\n')
def command_for(step,driver,target):
    if step=='route-usb-adjacent':return [driver,'list','targets','-v']
    if step=='route-eth1-finally-up':command='ip link set dev eth1 up'
    else:
        match=re.fullmatch(r'route-eth1-(?:baseline|finally-[0-9])-(link|addr|routes)',step)
        need(match is not None,'ROUTE_NETWORK_STEP_NOT_ALLOWED')
        command={'link':'ip link show dev eth1','addr':'ip -4 addr show dev eth1','routes':'ip route show'}[match[1]]
    return [driver,'-t',target,'shell',command+WRAPPER]
def bind(window):
    window=Path(window).resolve();cfg=read(window/'window.json');repo=Path(cfg['repo']).resolve()
    need(Path(cfg['window_root']).resolve()==window,'ROUTE_WINDOW_IDENTITY')
    need(isinstance(cfg.get('route_lifecycle_native'),dict),'ROUTE_NATIVE_CONTRACT_REQUIRED')
    proof=Path(cfg['task_contract']['freeze_path']).resolve()
    need(sha(proof)==cfg['task_contract']['freeze_sha256'],'ROUTE_FROZEN_CONTRACT_DRIFT')
    frozen=read(proof);task=proof.parent
    need(Path(frozen['task_root']).resolve()==task,'ROUTE_TASK_ROOT_IDENTITY')
    private_path=task/'PRIVATE_CONFIG.json';private_hash=sha(private_path)
    need(cfg.get('private_config_sha256')==private_hash and frozen.get('private_config_sha256')==private_hash,'ROUTE_PRIVATE_INPUT_DRIFT')
    materialization=read(task/'MATERIALIZATION.json')
    need(materialization.get('private_config_sha256')==private_hash,'ROUTE_MATERIALIZED_PRIVATE_INPUT_DRIFT')
    private=read(private_path)
    need(private.get('schema')==1 and Path(private['task_root']).resolve()==task and Path(private['repo_root']).resolve()==repo,'ROUTE_PRIVATE_SCOPE_IDENTITY')
    need(cfg.get('targets')==private['targets'] and cfg.get('hdc_path')==private['hdc_path'],'ROUTE_ACTUAL_PRIVATE_TARGET_DRIVER_DRIFT')
    need(frozen.get('route_lifecycle_native')==cfg['route_lifecycle_native'] and private.get('route_lifecycle_native')==cfg['route_lifecycle_native'],'ROUTE_CAPTURE_MODE_DRIFT')
    need(frozen.get('route_network_capture')==cfg['route_network_capture'],'ROUTE_CAPTURE_BINDING_DRIFT')
    driver=Path(private['hdc_path']).resolve();driver_binding=frozen['device_driver']
    need(Path(driver_binding['path']).resolve()==driver and sha(driver)==driver_binding['sha256'],'ROUTE_FROZEN_DRIVER_DRIFT')
    binding=cfg['route_network_capture'];helper=task/'executor/route-network-capture.py';collector=task/'recovery/pregrant_recovery.py'
    entries={str((repo/row['path']).resolve()):row['sha256'] for row in frozen['execution_files']}
    for path,key in ((helper,'helper_sha256'),(collector,'collector_sha256')):
        need(entries.get(str(path.resolve()))==binding[key] and sha(path)==binding[key],'ROUTE_BOUND_CAPTURE_DEPENDENCY_DRIFT')
    current=Path(__file__).resolve();copy=window/'route-network-capture.py'
    need(current in (helper.resolve(),copy.resolve()) and sha(current)==binding['helper_sha256'],'ROUTE_ACTUAL_HELPER_DRIFT')
    need(cfg['executor_sha256']['route-network-capture.py']==binding['helper_sha256'],'ROUTE_WINDOW_HELPER_IDENTITY')
    need(sha(copy)==binding['helper_sha256'],'ROUTE_IMMUTABLE_WINDOW_HELPER_DRIFT')
    return cfg,collector,private
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--window',type=Path,required=True);parser.add_argument('--step',required=True);parser.add_argument('--budget-seconds',type=float,default=15);args=parser.parse_args()
    need(0<args.budget_seconds<=15,'ROUTE_CAPTURE_BUDGET_OUTSIDE_BOUND')
    window=args.window.resolve();window=window.parent if window.name=='window.json' else window
    cfg,collector,private=bind(window)
    argv=command_for(args.step,str(private['hdc_path']),str(private['targets']['A']))
    # All dependency checks above precede loading executable collector code.
    spec=importlib.util.spec_from_file_location('bound_route_collector',collector);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    output=window/'A'/args.step;output.mkdir(parents=True,exist_ok=False)
    write(output/'command.json',argv)
    child=None;stdout=b'';stderr=b'';timed=False;complete=False;failure=None;start=time.monotonic()
    try:
        child=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        write(output/'spawn.json',dict(pid=child.pid,process_identity=module.process_identity(child.pid),owned_local_client_only=True))
        stdout,stderr,timed,complete=module.collect_child(child,args.budget_seconds,1048576)
    except module.CaptureLimitExceeded as error:
        stdout=error.stdout;stderr=error.stderr;failure='CAPTURE_LIMIT_EXCEEDED'
    except BaseException as error:
        failure=type(error).__name__
    finally:
        if child is not None and child.poll() is None:
            try:
                child.kill()
                # Unexpected Peek/read/OS errors can leave the owned client
                # alive even after collector entry. Confirm this exact child's
                # exit before closing its pipes, with one finite wait budget.
                child.wait(timeout=2)
                if child.poll() is None:failure=failure or 'OWNED_CLIENT_EXIT_UNCONFIRMED'
            except BaseException:failure=failure or 'OWNED_CLIENT_EXIT_UNCONFIRMED'
        code=child.poll() if child is not None else None
        (output/'stdout.log').write_bytes(stdout);(output/'stderr.log').write_bytes(stderr)
        (output/'exit-code.txt').write_text(str(code),encoding='utf-8')
        write(output/'result.json',dict(exit_code=code,timeout=timed,capture_complete=complete,failure=failure,
            max_output_bytes=1048576,command_seconds=args.budget_seconds,max_command_seconds=15,drain_budget_seconds=2,owned_wait_seconds=2,elapsed_seconds=time.monotonic()-start,
            owned_local_client_only=True,remote_termination_proven=False))
        if child is not None:
            for stream in (child.stdout,child.stderr):
                if stream is not None:stream.close()
    need(code==0 and not timed and complete and failure is None,'ROUTE_COMMAND_CAPTURE_INCOMPLETE')
if __name__=='__main__':
    try:main()
    except Exception as error:
        # Validation reasons are fixed strings; OS/parser exceptions may embed
        # private paths or input text, so expose only their class here.
        reason=str(error) if type(error) is ValueError else 'ROUTE_CAPTURE_FAILED'
        print(type(error).__name__+': '+reason,file=sys.stderr);raise SystemExit(1)
