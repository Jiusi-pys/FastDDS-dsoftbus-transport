"""Real API23 Windows LLDB event-loop regression; no ROS/SDK/device authority.

Run with executor Python and --sdk-bin/--output. All generated objects remain
under build/ros2-target. --legacy-source preserves an immutable RED input.
"""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path

def terminate_owned_fixture(exe,pid):
    import ctypes
    k=ctypes.WinDLL('kernel32',use_last_error=True);k.OpenProcess.restype=ctypes.c_void_p
    h=k.OpenProcess(0x1001,False,pid)
    if not h:return
    try:
        k.QueryFullProcessImageNameW.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_ulong)]
        size=ctypes.c_ulong(32768);buf=ctypes.create_unicode_buffer(size.value)
        assert k.QueryFullProcessImageNameW(h,0,buf,ctypes.byref(size)) and Path(buf.value).resolve()==Path(exe).resolve()
        k.TerminateProcess.argtypes=[ctypes.c_void_p,ctypes.c_uint];assert k.TerminateProcess(h,99)
    finally:
        k.CloseHandle.argtypes=[ctypes.c_void_p];k.CloseHandle(h)

def sdk_exercise(exe,out,source,legacy=False,unknown=False,continue_failure=False,already_running=False,callee_once=False):
    import importlib.util,lldb,time,io
    spec=importlib.util.spec_from_file_location('fixture_trace',source)
    m=importlib.util.module_from_spec(spec);sys.modules[spec.name]=m;spec.loader.exec_module(m)
    d=lldb.SBDebugger.Create();d.SetAsync(False);target=d.CreateTarget(exe)
    bp=target.BreakpointCreateByName('cleanup_point')
    launch=lldb.SBLaunchInfo([]);launch.SetLaunchFlags(0)
    error=lldb.SBError();p=target.Launch(launch,error)
    if error.Fail():raise RuntimeError(str(error))
    if p.GetSelectedThread().GetFrameAtIndex(0).GetFunctionName()!='cleanup_point':
        p.Kill();raise RuntimeError('HOST_INITIAL_BREAKPOINT_NOT_OBSERVED')
    t=m.Trace.__new__(m.Trace);m.SESSION=t
    t.cfg=dict(run='0'*15+'1',side='A',pid=p.GetProcessID(),seconds=3,hit_limit=4)
    t.start=time.monotonic();t.process=p;t.target=target;t.debugger=d;t.output=Path(out)
    t.output.mkdir();t.hits=0;t.rmw_sha='HOST_FIXTURE_NOT_RMW';t.slide=0;t.seq=0;t.stream=io.StringIO()
    t.breakpoints={} if unknown else {bp.GetID():'HOST_POINT'}
    rows=[]
    def emit(event,**fields):
        t.seq+=1
        if 'scope' in fields:fields['operation_scope']=fields.pop('scope')
        rows.append(dict(event=event,scope='HOST_WINDOWS_LLDB_LOOP_NOT_RMW_NOT_DEVICE',**fields))
        (t.output/'events.json').write_text(json.dumps(rows,indent=2))
    def hit(frame,location):
        t.hits+=1;emit('HOST_BREAKPOINT',pc=frame.GetPC(),location=location.GetID());return False
    t.emit=emit
    if callee_once:
        # Exercise production Trace.hit against an actual SDK breakpoint. The
        # x86 caller PC is the Host analogue of the observed AArch64 LR, not a
        # claim that this fixture executes RMW or an ARM instruction profile.
        caller=p.GetSelectedThread().GetFrameAtIndex(1).GetPC()
        t.layout={'cast_return':caller};t.breakpoints={bp.GetID():'CAST_CALLEE'}
        t.reg=lambda frame,name:frame.GetThread().GetFrameAtIndex(1).GetPC() if name=='x30' else None
    else:t.hit=hit
    if continue_failure:
        class FailureAtRealBreakpoint:
            calls=0
            def __getattr__(self,name):return getattr(p,name)
            def Continue(self):
                self.calls+=1
                if self.calls==2:
                    error=lldb.SBError();error.SetErrorString('INJECTED_HOST_CONTINUE_FAILURE');return error
                return p.Continue()
        t.process=FailureAtRealBreakpoint()
    if legacy:
        # The frozen source's own callback entry, not a replacement loop.
        bp.SetScriptCallbackFunction('fixture_trace.on_breakpoint')
    d.SetAsync(True);failure=None
    if already_running:
        error=p.Continue()
        if error.Fail():raise RuntimeError('HOST_SETUP_CONTINUE_FAILED:'+str(error))
    try:t.run()
    except Exception as error:failure=str(error)
    result=dict(scope='HOST_WINDOWS_LLDB_LOOP_NOT_RMW_NOT_DEVICE',pid=p.GetProcessID(),
                hits=t.hits,state=p.GetState(),exit_status=p.GetExitStatus(),error=failure,
                breakpoint_hits=bp.GetHitCount(),breakpoint_enabled=bp.IsEnabled(),
                callee_records=sum(r['event']=='CAST_CALLEE' for r in rows))
    (t.output/'result.json').write_text(json.dumps(result,indent=2))
    if p.GetState() not in (lldb.eStateExited,lldb.eStateDetached,lldb.eStateInvalid):p.Kill()
    terminate_owned_fixture(exe,result['pid'])

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--sdk-bin',required=True);ap.add_argument('--output',required=True)
    ap.add_argument('--legacy-source');ap.add_argument('--source-under-test');ap.add_argument('--callee-only',action='store_true');args=ap.parse_args()
    if args.legacy_source and args.source_under_test:raise ValueError('Choose one source revision')
    repo=Path(__file__).resolve().parents[2];root=Path(args.output).resolve()
    if not root.is_relative_to(repo/'build/ros2-target'):raise ValueError('HOST_OUTPUT_OUTSIDE_BUILD_ROOT')
    root.mkdir(parents=True,exist_ok=False);sdk=Path(args.sdk_bin).resolve()
    source=Path(args.legacy_source or args.source_under_test).resolve() if args.legacy_source or args.source_under_test else repo/'scripts/public_socket/rmw_lldb_trace.py'
    c=root/'main.c';c.write_text('__declspec(noinline) int cleanup_point(int a){return a+1;} int mainCRTStartup(void){int v=cleanup_point(5);volatile int i=0;for(;i<200000000;++i){}return cleanup_point(cleanup_point(v));}')
    commands=[]
    def command(argv,label):
        run=subprocess.run(argv,cwd=repo,capture_output=True,timeout=15)
        (root/(label+'.stdout')).write_bytes(run.stdout);(root/(label+'.stderr')).write_bytes(run.stderr)
        commands.append(dict(argv=[str(x) for x in argv],exit=run.returncode))
        if run.returncode:raise RuntimeError(label+' failed')
    command([str(sdk/'clang.exe'),'--target=x86_64-pc-windows-msvc','-g','-O0','-c',str(c),'-o',str(root/'main.obj')],'compile')
    command([str(sdk/'lld-link.exe'),'/entry:mainCRTStartup','/subsystem:console','/nodefaultlib','/debug','/out:'+str(root/'main.exe'),str(root/'main.obj')],'link')
    cases=[('callee-once',False)] if args.callee_only else [('normal',False),('unknown-breakpoint',True),('continue-failure',False),('already-running',False),('callee-once',False)]
    for name,unknown in cases:
        inject=name=='continue-failure'
        if args.legacy_source and name!='normal':continue
        script=root/(name+'.lldb');fixture=Path(__file__).resolve()
        script.write_text('command script import '+json.dumps(str(fixture))+'\nscript rmw_trace_sdk_fixture.sdk_exercise('+','.join(repr(x) for x in (str(root/'main.exe'),str(root/name),str(source),bool(args.legacy_source),unknown,inject,name=='already-running',name=='callee-once'))+')\nquit\n')
        command([str(sdk/'lldb.exe'),'--batch','--source',str(script)],name)
        result=json.loads((root/name/'result.json').read_text())
        if args.legacy_source:
            # Original loop leaves this exact locally-created fixture stopped.
            assert result['hits']==0 and result['exit_status']!=8
        elif name=='callee-once':
            assert result['hits']==1 and result['breakpoint_hits']==1 and not result['breakpoint_enabled'],result
            assert result['callee_records']==1 and result['exit_status']==8 and result['error'] is None,result
        elif unknown:assert result['error']=='UNOWNED_BREAKPOINT_STOP' and result['hits']==0
        elif inject:
            assert result['error'] and result['error'].startswith('CONTINUE_FAILED:'),result
            assert not (root/name/'END.json').exists()
        else:
            # Normal includes the current initial owned stop plus two later
            # calls; already-running starts after the initial fixture stop.
            assert result['hits']==(2 if name=='already-running' else 3) and result['exit_status']==8 and result['error'] is None,result
            assert json.loads((root/name/'READY.json').read_text())['execution_state']=='RUNNING_EVENT_CONSUMED'
            if name=='already-running':
                rows=json.loads((root/name/'events.json').read_text())
                assert any(r.get('event')=='INITIAL_RESUME_DECISION' and r.get('entered') is False for r in rows)
    (root/'summary.json').write_text(json.dumps(dict(scope='HOST_REAL_SDK_DEBUGGER_LOOP_ONLY',result='RED_CONFIRMED' if args.legacy_source else 'PASS',source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),commands=commands),indent=2))

if __name__=='__main__':main()
