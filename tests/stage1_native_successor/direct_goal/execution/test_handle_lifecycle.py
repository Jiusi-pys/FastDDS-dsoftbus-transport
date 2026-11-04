"""Targeted lifecycle tests. Fake errors are distinct from real owned probes."""
import io,json,time,unittest,tempfile,sys,hashlib
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import goal_win32_owned as win
from goal_handle_lifecycle import HandleBinding,ImageUnavailable,LifecycleError,observe_binding,bounded_drain,terminal_receipt_qualified
from win32_lifecycle_fixture import LocalLifecycleProcess,OBSERVATIONS

NATURAL_IMAGE_QUERIES=[]

class FakeOps:
    def wait(self,h):return 0
    def pid_birth(self,h):return 17,19
    def image(self,h):raise AssertionError('postterminal image must not be queried')
    def file_id(self,h):return (1,2,3)
    def exit_code(self,h):return 0

class ContractTests(unittest.TestCase):
    def test_fake31_terminal_original_binding_is_separate(self):
        # No Root/backend constructor or permit. Dummy handle APIs are replaced.
        probe=SimpleNamespace(authority=True,closed=False,owner={'pid':17,'birth':19,'image':'fixture.exe'},pi=SimpleNamespace(hProcess=123),image_pin=234,handles=[123,234],binding=HandleBinding(123,234,17,19,'fixture.exe',(1,2,3)),receipt={'signal_authority':True})
        with patch.object(win,'IdentityOps',return_value=FakeOps(),create=True),patch.object(win,'ident',side_effect=win.Fault('FAKE_ONLY_QUERY_IMAGE_31')):
            self.assertEqual(win.OwnedRoot.identity(probe),'TERMINAL')
        self.assertTrue(probe.receipt['terminal_binding_verified'])
        self.assertFalse(probe.receipt['signal_authority'])
    def test_cleanup_must_drain_beyond_one_poll(self):
        from goal_windows_backend import GoalWindowsBackend
        from goal_notice import Marker
        class Probe:
            case='server';closed=False;dest=Path('E:/not_written_fixture');reads=[{'name':'stdout','eof':False},{'name':'stderr','eof':False}]
            def __init__(self):self.calls=0;self.receipt={'native_exit':0,'stop_attempts':0,'EOF':False,'cleanup_errors':[]}
            def state(self):return 'TERMINAL'
            def read(self,remaining,observer):
                self.calls+=1
                if self.calls==1:observer('stdout',b'KNOWN_TAIL\n',1,2)
                else:
                    self.receipt['EOF']=True
                    for row in self.reads:row['eof']=True
                return []
            def drain(self,deadline,remaining,observer):
                from goal_handle_lifecycle import bounded_drain
                return bounded_drain(lambda cap:self.read(cap,observer),lambda:self.receipt['EOF'],deadline=deadline,remaining=remaining)
            def close(self):self.closed=True
        root=Probe();backend=SimpleNamespace(journal=SimpleNamespace(case=None,failed=False),captured={'server':0},marker=Marker(cases=('server',)),notice_gate=lambda:None,w=SimpleNamespace(put=lambda *a:None))
        result=GoalWindowsBackend.cleanup(backend,root,time.monotonic()+.1)
        self.assertTrue(result['EOF']);self.assertGreaterEqual(root.calls,2)
    def test_live_failure_and_required_terminal_failures_are_not_ignored(self):
        binding=HandleBinding(123,234,17,19,'fixture.exe',(1,2,3))
        class Live(FakeOps):
            def wait(self,h):return 258
            def image(self,h):raise ImageUnavailable(31)
        with self.assertRaises(ImageUnavailable):observe_binding(binding,Live())
        for which in ['pid','birth','pin','wait','exit_api']:
            class Broken(FakeOps):
                def pid_birth(self,h):return (18,19) if which=='pid' else (17,20) if which=='birth' else (17,19)
                def file_id(self,h):return (1,2,4) if which=='pin' else (1,2,3)
                def wait(self,h):return 0xffffffff if which=='wait' else 0
                def exit_code(self,h):
                    if which=='exit_api':raise LifecycleError('FAKE_EXIT_API31')
                    return 0
            with self.subTest(which=which),self.assertRaises(LifecycleError):observe_binding(binding,Broken())
    def test_failed_image_query_only_transitions_on_same_handle_terminal_proof(self):
        class Race(FakeOps):
            count=0
            def wait(self,h):self.count+=1;return 258 if self.count==1 else 0
            def image(self,h):raise ImageUnavailable(31)
        result=observe_binding(HandleBinding(123,234,17,19,'fixture.exe',(1,2,3)),Race())
        self.assertEqual(result['phase'],'TERMINAL');self.assertEqual(result['transition_error']['error'],31)
        class Changed(Race):
            def image(self,h):return 'different.exe'
        with self.assertRaises(LifecycleError):observe_binding(HandleBinding(123,234,17,19,'fixture.exe',(1,2,3)),Changed())
    def test_lost_authority_or_closed_or_changed_handle_never_recovers(self):
        for change in ['lost','closed','handle']:
            probe=SimpleNamespace(authority=change!='lost',closed=change=='closed',owner={'pid':17,'birth':19,'image':'fixture.exe'},pi=SimpleNamespace(hProcess=124 if change=='handle' else 123),image_pin=234,handles=[123,234],binding=HandleBinding(123,234,17,19,'fixture.exe',(1,2,3)),receipt={'signal_authority':True})
            with patch.object(win,'IdentityOps',return_value=FakeOps()),self.assertRaises(Exception):win.OwnedRoot.identity(probe)
            self.assertFalse(probe.authority);self.assertFalse(probe.receipt['terminal_binding_verified'])
    def test_pipe_true_zero_broken_and_error_classification(self):
        seen=[];capture=io.BytesIO();probe=SimpleNamespace(reads=[{'h':77,'eof':False,'name':'stdout'}],receipt={'EOF':False},captures=[capture],case='fixture',output=[bytearray()])
        class Pipe:
            def PeekNamedPipe(self,h,a,b,c,out,d):out._obj.value=4;return 1
            def ReadFile(self,h,buf,n,out,over):out._obj.value=0;return 1
        with patch.object(win,'K',Pipe()):self.assertEqual(win.OwnedRoot.read(probe,8,lambda *args:seen.append(args)),[])
        self.assertFalse(probe.receipt['EOF']);self.assertFalse(seen)
        class Broken(Pipe):
            def ReadFile(self,*args):win.C.set_last_error(109);return 0
        with patch.object(win,'K',Broken()):win.OwnedRoot.read(probe,8,lambda *args:seen.append(args))
        self.assertTrue(probe.receipt['EOF']);self.assertFalse(seen)
        probe.reads[0]['eof']=False
        class Error(Pipe):
            def ReadFile(self,*args):win.C.set_last_error(5);return 0
        with patch.object(win,'K',Error()),self.assertRaises(Exception):win.OwnedRoot.read(probe,8,lambda *args:None)
    def test_drain_cancel_deadline_cap_and_errors_stay_distinct(self):
        ticks=[0.0]
        def pause(seconds):ticks[0]+=seconds
        result=bounded_drain(lambda cap:[],lambda:False,deadline=.01,remaining=4,clock=lambda:ticks[0],pause=pause)
        self.assertEqual(result['status'],'DEADLINE');self.assertFalse(result['EOF'])
        self.assertEqual(bounded_drain(lambda cap:[],lambda:False,deadline=1,remaining=4,clock=lambda:0,pause=pause,cancelled=lambda:True)['status'],'CANCELLED')
        with self.assertRaises(LifecycleError):bounded_drain(lambda cap:[('stdout',b'12345')],lambda:False,deadline=1,remaining=4,clock=lambda:0,pause=lambda n:None)
        def failed(cap):raise LifecycleError('FAKE_READ_FAILURE')
        with self.assertRaises(LifecycleError):bounded_drain(failed,lambda:False,deadline=1,remaining=4,clock=lambda:0,pause=lambda n:None)
    def test_unknown_wait_revokes_and_eof_cannot_be_derived_from_terminal(self):
        probe=SimpleNamespace(closed=False,authority=True,pi=SimpleNamespace(hProcess=123),receipt={'created':True,'signal_authority':True})
        with patch.object(win,'K',SimpleNamespace(WaitForSingleObject=lambda *a:0xffffffff)),self.assertRaises(Exception):win.OwnedRoot.state(probe)
        self.assertFalse(probe.authority);self.assertFalse(probe.receipt['terminal_binding_verified'])
        self.assertFalse(terminal_receipt_qualified({'created':True,'bound':True,'terminal':True,'EOF':False,'native_exit':0,'cleanup_errors':[]}))
    def test_fixture_unknown_and_late_writer_error_latch_before_any_new_create(self):
        import win32_lifecycle_fixture as fixture
        hold={'active':False,'reason':None};observed=[];closed=[]
        with patch.object(fixture,'HOLD',hold),patch.object(fixture,'OBSERVATIONS',observed):
            process=LocalLifecycleProcess(Path('E:/synthetic_never_created'));process.receipt['created']=True
            process.state=lambda:'LIVE';process.close=lambda:closed.append(True)
            def failed_writer():raise RuntimeError('controlled fake writer-close failure')
            process.release_writer=failed_writer
            with self.assertRaises(RuntimeError):process.finish()
            self.assertTrue(closed);self.assertTrue(hold['active']);self.assertEqual(observed[0]['fixture_terminal_outcome'],'UNKNOWN')
            next_process=LocalLifecycleProcess(Path('E:/synthetic_next_never_created'))
            with self.assertRaises(RuntimeError):next_process.start()
            self.assertFalse(next_process.receipt['created'])
    def test_late_read_eof_and_cancel_are_observed_but_never_timely_success(self):
        tick=[0.0];ended=[False]
        def late(cap):tick[0]=1.01;ended[0]=True;return [('stdout',b'known')]
        result=bounded_drain(late,lambda:ended[0],deadline=1,remaining=10,clock=lambda:tick[0],pause=lambda *args:None)
        self.assertTrue(result['EOF']);self.assertEqual(result['status'],'DEADLINE')
        tick[0]=0;ended[0]=False;cancel=[False]
        def cancelled_read(cap):ended[0]=True;cancel[0]=True;return []
        result=bounded_drain(cancelled_read,lambda:ended[0],deadline=1,remaining=10,clock=lambda:tick[0],pause=lambda *args:None,cancelled=lambda:cancel[0])
        self.assertTrue(result['EOF']);self.assertEqual(result['status'],'CANCELLED')
        from goal_windows_backend import GoalWindowsBackend
        root=SimpleNamespace(case='FAKE_CLOSED_LATE',closed=True,receipt={'terminal':True,'EOF':True,'cleanup_errors':[],'drain':{'status':'DEADLINE','EOF':True}})
        backend=SimpleNamespace(journal=SimpleNamespace(case=None))
        self.assertEqual(GoalWindowsBackend.cleanup(backend,root,1)['state'],'UNKNOWN')
    def test_observe_memory_error_terminal_race_is_recorded_not_live_bypass(self):
        from goal_windows_backend import GoalWindowsBackend
        for api,phases,accepted in [('GetProcessMemoryInfo(root)',['LIVE','TERMINAL'],True),('GetProcessMemoryInfo(root)',['LIVE','LIVE'],False),('GetPerformanceInfo',['LIVE','TERMINAL'],False)]:
            with self.subTest(api=api,phases=phases):
                steps=iter(phases);record=[]
                root=SimpleNamespace(case='MARKED_FAKE',pi=SimpleNamespace(hProcess=123),identity=lambda **kwargs:next(steps))
                def observe(*args):raise win.ObservationError(api,31,[{'api':api,'ok':False}])
                backend=SimpleNamespace(samples=0,epochs=0,latest={'MARKED_FAKE':{}},journal=SimpleNamespace(case=None,resource=False,emit=lambda row:record.append(row)),trace=SimpleNamespace(observe=observe),w=SimpleNamespace(ObservationError=win.ObservationError))
                if accepted:self.assertIsNone(GoalWindowsBackend.observe(backend,root,'RUNNING'))
                else:
                    with self.assertRaises(win.ObservationError):GoalWindowsBackend.observe(backend,root,'RUNNING')
                self.assertEqual(record[0]['resource_API_error']['win32_error'],31)

class RealOwnedTests(unittest.TestCase):
    def test_natural_exit_original_handle_and_postexit_query_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            process=LocalLifecycleProcess(Path(directory)/'normal')
            try:
                process.start();self.assertEqual(process.wait_terminal(),0)
                try:
                    image=win.IdentityOps().image(process.pi.hProcess);diagnostic={'success':True,'error':None,'image_matches_created':image==process.owner['image']}
                except ImageUnavailable as error:diagnostic={'success':False,'error':error.error,'image_matches_created':'NOT_AVAILABLE'}
                NATURAL_IMAGE_QUERIES.append({'case':'REAL_NONHDC_NATURAL_EXIT','pid':process.owner['pid'],**diagnostic})
                self.assertEqual(process.identity(),'TERMINAL');self.assertFalse(process.receipt['signal_authority'])
                drained=process.drain(time.monotonic()+.5,4096,lambda *args:None)
                self.assertTrue(drained['EOF']);self.assertEqual(bytes(process.output[0]),b'KNOWN_CHILD_STDOUT\n');self.assertEqual(bytes(process.output[1]),b'KNOWN_CHILD_STDERR\n')
                self.assertTrue(terminal_receipt_qualified(process.receipt))
            finally:process.finish()
    def test_actual_pipe_late_parent_owned_writer_tail_is_drained(self):
        with tempfile.TemporaryDirectory() as directory:
            process=LocalLifecycleProcess(Path(directory)/'tail',hold_writer=True);before_capture=[]
            try:
                process.start();self.assertEqual(process.wait_terminal(),0);self.assertEqual(process.identity(),'TERMINAL')
                def observer(stream,data,before,after):
                    index=0 if stream=='stdout' else 1;before_capture.append((stream,len(process.captures[index].getvalue()),len(data)))
                    if stream=='stdout' and process.writer is not None:process.release_writer(b'KNOWN_LATE_TAIL\n')
                drained=process.drain(time.monotonic()+.5,4096,observer)
                self.assertTrue(drained['EOF']);self.assertGreaterEqual(drained['polls'],2)
                self.assertEqual(bytes(process.output[0]),b'KNOWN_CHILD_STDOUT\nKNOWN_LATE_TAIL\n');self.assertEqual(before_capture[0][1],0)
            finally:process.finish()
    def test_actual_pipe_held_writer_drain_deadline_is_not_eof(self):
        with tempfile.TemporaryDirectory() as directory:
            process=LocalLifecycleProcess(Path(directory)/'held',hold_writer=True)
            try:
                process.start();self.assertEqual(process.wait_terminal(),0);process.identity()
                drained=process.drain(time.monotonic()+.05,4096,lambda *args:None)
                self.assertEqual(drained['status'],'DEADLINE');self.assertFalse(drained['EOF']);self.assertFalse(terminal_receipt_qualified(process.receipt))
            finally:process.finish() # Owned writer/handles close; no false EOF rewrite.
    def test_original_owned_live_child_can_be_stopped_once_and_drained(self):
        with tempfile.TemporaryDirectory() as directory:
            process=LocalLifecycleProcess(Path(directory)/'stop',mode='sleep')
            try:
                process.start();self.assertEqual(process.identity(),'LIVE');deadline=time.monotonic()+2;process.stop(deadline)
                self.assertEqual(process.receipt['stop_attempts'],1);self.assertEqual(process.receipt['native_exit'],125)
                self.assertTrue(process.drain(deadline,4096,lambda *args:None)['EOF'])
                process.stop(deadline);self.assertEqual(process.receipt['stop_attempts'],1)
            finally:process.finish()

def load_tests(loader,tests,pattern):
    # Only the two affected legacy error-boundary methods. No registered grant,
    # process factory or unrelated old suite is selected.
    for name in ['test_partial_eof_late_cleanup_and_record_error_remain_blocking','test_close_and_last_record_failures_revoke_success']:
        tests.addTests(loader.loadTestsFromName('test_goal_workflow.GoalWorkflowTests.'+name))
    return tests

if __name__=='__main__':
    destination=None
    if len(sys.argv)==3 and sys.argv[1]=='--evidence':
        destination=Path(sys.argv[2]);base=Path('E:/Codex-FastDDS-stage1-win32-terminal-20261030-d01').resolve()
        if not destination.is_absolute() or not destination.resolve().is_relative_to(base):raise SystemExit(64)
        sys.argv=sys.argv[:1]
    result=unittest.main(verbosity=2,exit=False).result
    if destination:
        report={'success':result.wasSuccessful(),'tests':result.testsRun,'errors':len(result.errors),'failures':len(result.failures),'real_owned_processes':OBSERVATIONS,'natural_postexit_image_queries':NATURAL_IMAGE_QUERIES,'natural31_reproduced':any(row['error']==31 for row in NATURAL_IMAGE_QUERIES),'Fake31_injection':'separate ContractTests only, not natural reproduction','HDC_Native_Token_SDK_Job_management':0}
        with destination.open('x',encoding='utf-8') as out:json.dump(report,out,indent=2)
    raise SystemExit(0 if result.wasSuccessful() else 1)
