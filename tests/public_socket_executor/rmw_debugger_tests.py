"""Bounded debugger support protocol tests; no HDC or target authority."""
import importlib.util
import threading
import unittest
import json
import tempfile
import sys
import types
import io
import time
from unittest.mock import patch
from pathlib import Path

SOURCE=Path(__file__).resolve().parents[2]/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
spec=importlib.util.spec_from_file_location('debugger_under_test',SOURCE)
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class Protocol(unittest.TestCase):
    def test_unknown_instruction_image_fails_before_command(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);image=root/'unit.so';image.write_bytes(b'UNKNOWN_UNIT_ELF')
            script=SOURCE.parents[2]/'rmw_lldb_trace.py'
            with patch.object(m,'Command',side_effect=AssertionError('NO_HDC')) as command:
                self.assertRaises(ValueError,m.verify_instruction_image,script,image);command.assert_not_called()
    def test_forward_list_exact_and_empty(self):
        self.assertEqual(m.forward_rows('[Empty]\r\n'),[])
        self.assertEqual(m.forward_rows('tcp:19001 tcp:19001 [Forward]\n'),[('tcp:19001','tcp:19001','Forward')])
        for text in ('','E001005','[Fail]','tcp:19001 tcp:19001','junk\ntcp:19001 tcp:19001 [Forward]'):
            with self.subTest(text=text):self.assertRaises(RuntimeError,m.forward_rows,text)

    def test_actual_global_forward_list_and_rm_argv(self):
        text='UNIT_A    tcp:19001 tcp:19001    [Forward]\nUNIT_B tcp:19002 tcp:19002 [Forward]\n'
        self.assertEqual(m.owned_forward_rows(text,'UNIT_A'),[('tcp:19001','tcp:19001','Forward')])
        self.assertEqual(m.forward_remove_argv(['HDC','-t','UNIT_A'],'tcp:19001'),['HDC','-t','UNIT_A','fport','rm','tcp:19001','tcp:19001'])
        for invalid in ('tcp:19001 tcp:19001 [Forward]\n',text+text,'E001005\n',''):
            self.assertRaises(RuntimeError,m.forward_entries,invalid)

    def command(self,text,**changes):
        class Recorded:pass
        c=Recorded();c.seconds=0;c.finished=threading.Event();c.finished.set()
        c.buffers={'stdout':bytearray(text.encode()),'stderr':bytearray()}
        c.result=dict(exit_code=0,overflow=False,capture_complete=True,**changes)
        return c

    def test_remote_envelope_is_not_outer_exit(self):
        m.remote_result(self.command('MDDS_REMOTE_EXIT=0\n'))
        for text in ('','E001005\n','MDDS_REMOTE_EXIT=0\nMDDS_REMOTE_EXIT=0\n','MDDS_REMOTE_EXIT=1\n'):
            self.assertRaises(RuntimeError,m.remote_result,self.command(text))

    def test_server_nonzero_is_confirmed_exit_not_successful_attach(self):
        self.assertEqual(m.server_exit(self.command('MDDS_REMOTE_EXIT=1\n')),1)
        for text in ('','MDDS_REMOTE_EXIT=0\nMDDS_REMOTE_EXIT=1\n'):
            self.assertRaises(RuntimeError,m.server_exit,self.command(text))

    def test_unfinished_or_overflow_never_closed(self):
        c=self.command('MDDS_REMOTE_EXIT=0\n');c.result['capture_complete']=False
        self.assertRaises(RuntimeError,m.server_exit,c)
        c.result['capture_complete']=True;c.result['overflow']=True
        self.assertRaises(RuntimeError,m.server_exit,c)
        c.result['overflow']=False;c.result['timed_out']=True
        self.assertRaises(RuntimeError,m.remote_result,c)

    def test_pid_instance_not_number_only(self):
        # /proc stat starts at field3 after its parenthesized comm.
        fields=['S']+['0']*18+['987']
        text='MDDS_DEBUG_EXE=/unit/main\nMDDS_DEBUG_CMDLINE_HEX_BEGIN\n'+b'/unit/main\0A\0'.hex(' ')+'\nMDDS_DEBUG_CMDLINE_HEX_END\nMDDS_DEBUG_STAT=123 (main) '+' '.join(fields)+'\n'
        m.process_identity(text,123,'/unit/main','987')
        for pid,exe,start in [(124,'/unit/main','987'),(123,'/other/main','987'),(123,'/unit/main','988')]:
            self.assertRaises(RuntimeError,m.process_identity,text,pid,exe,start)
    def test_current_probe_has_no_missing_pipeline_utility_or_mask(self):
        command=m.process_command(123)
        self.assertNotIn("tr '",command);self.assertNotIn('head -',command)
        self.assertIn('od -v -An -tx1',command)
        self.assertIn('|| exit',command)
    def test_cmdline_hex_missing_or_truncated_is_rejected(self):
        fields=['S']+['0']*18+['987']
        base='MDDS_DEBUG_EXE=/unit/main\nMDDS_DEBUG_STAT=123 (main) '+' '.join(fields)+'\n'
        for text in (base+'MDDS_DEBUG_ARG0=/bin/sh: tr: not found\n',base+'MDDS_DEBUG_CMDLINE_HEX_BEGIN\n2f 75 6e 69 74\nMDDS_DEBUG_CMDLINE_HEX_END\n'):
            self.assertRaises(RuntimeError,m.process_identity,text,123,'/unit/main','987')

    def test_local_or_simulated_authority_refused_without_command(self):
        for key in ('local_contract_test','simulated'):
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);cfg={'run':'a'*16,key:True}
                (root/'window.json').write_text(json.dumps(cfg));(root/'DATA_ATTEMPT_STARTED').write_text('invalid')
                with patch.object(m,'Command',side_effect=AssertionError('NO_HDC')) as command:
                    self.assertRaises(ValueError,m.main,root,'A',123);command.assert_not_called()

    def boundary(self,case):
        # Test-only authority seam; every HDC/LLDB command is inert. Exercise
        # the actual manager decisions, not target or admission proof.
        with tempfile.TemporaryDirectory() as folder:
            window=Path(folder);(window/'A').mkdir();run='a'*16;remote='/data/local/tmp/mdds_dds_'+run+'_A'
            cfg=dict(run=run,timeout_seconds=300,hdc_path='NEVER_EXECUTED',targets={'A':'UNIT'},remote={'A':remote},package=folder,main_member='unit-main',
                     rmw_debugger=dict(ports={'A':19001},client={'path':str(window/'lldb.exe')},script={'path':str(window/'trace.py')},hit_limit=256,seconds=1))
            (window/'window.json').write_text(json.dumps(cfg));calls=[];clock=[0.0]
            (window/'A/load-pre-release-analysis.json').write_text(json.dumps(dict(complete=True,run=run,board='A',pid=123,starttime='987')))
            (window/'A/load-pre-release-summary.json').write_text('{}')
            class Command:
                def __init__(self,root,label,argv,seconds,env=None):
                    calls.append(label);self.seconds=seconds;self.ready=threading.Event();self.finished=threading.Event();self.finished.set()
                    self.result=dict(exit_code=0,timed_out=False,capture_complete=True,overflow=False)
                    out='MDDS_REMOTE_EXIT=0\n'
                    if label in ('pid-before-attach','pid-after-detach','pid-before-tracer-proof','pid-before-os-resume'):
                        start='999' if case=='reused-pid' else '987'
                        out=f'MDDS_DEBUG_EXE={remote}/unit-main\nMDDS_DEBUG_CMDLINE_HEX_BEGIN\n'+(remote+'/unit-main\0').encode().hex(' ')+f'\nMDDS_DEBUG_CMDLINE_HEX_END\nMDDS_DEBUG_STAT=123 (unit-main) S '+' '.join(['0']*18)+f' {start}\nMDDS_REMOTE_EXIT=0\n'
                    elif label=='forward-before':out='UNIT tcp:19001 tcp:19001 [Forward]\n' if case=='foreign-forward' else '[Empty]\n'
                    elif label=='forward':out='[Fail] already exists\n' if case=='forward-failed' else 'Forwardport result:OK\n'
                    elif label in ('forward-after','forward-remove-before'):
                        out='UNIT tcp:19001 tcp:19001 [Forward]\n'
                        if case=='after-selector-conflict' or (label=='forward-remove-before' and case=='before-remove-conflict'):out+='FOREIGN tcp:19001 tcp:19001 [Reverse]\n'
                        if label=='forward-after' and case=='startup-expired-after-forward':clock[0]=31.0
                    elif label=='server':
                        fallback=case in ('withheld-server-text','contradicting-visible-pid','server-birth-reused')
                        if not fallback:self.ready.set()
                        prefix='' if fallback else 'MDDS_DEBUG_SERVER_PID=456\nListening to port 19001\n'
                        if case=='contradicting-visible-pid':prefix='MDDS_DEBUG_SERVER_PID=457\n'
                        out=prefix+('' if case=='missing-server-exit' else 'MDDS_REMOTE_EXIT=0\n')
                    elif label=='client':
                        trace=root/'trace';trace.mkdir();row={'run':run,'side':'A','pid':123,'execution_state':'RUNNING_EVENT_CONSUMED'}
                        if case=='missing-running-event':row.pop('execution_state')
                        (trace/'READY.json').write_text(json.dumps(row));(trace/'END.json').write_text(json.dumps(row))
                        if case=='client-failed':self.result['exit_code']=1;(trace/'FAILED.json').write_text('{}')
                        out=''
                    elif label in ('inferior-status','inferior-after-os-resume'):out='State:\t'+('T (stopped)' if case=='group-stopped' and label=='inferior-status' else 'R (running)')+'\nTracerPid:\t0\nMDDS_REMOTE_EXIT=0\n'
                    elif label=='tracer-proof-status':out='State:\tt (tracing stop)\nTracerPid:\t456\nMDDS_REMOTE_EXIT=0\n'
                    elif label=='tracer-proof-identity':
                        argv=[remote+'/lldb-server','gdbserver','127.0.0.1:19001','--attach','123']
                        out='MDDS_DEBUG_EXE='+argv[0]+'\nMDDS_DEBUG_STAT=456 (lldb-server) S '+' '.join(['0']*18)+' 998\nMDDS_DEBUG_CMDLINE_HEX_BEGIN\n'+(b'\0'.join(x.encode() for x in argv)+b'\0').hex(' ')+'\nMDDS_DEBUG_CMDLINE_HEX_END\nMDDS_REMOTE_EXIT=0\n'
                    elif label=='server-identity':
                        out='MDDS_DEBUG_PROCESS_ABSENT=1\nMDDS_REMOTE_EXIT=0\n'
                        if case=='server-birth-reused':
                            argv=[remote+'/lldb-server','gdbserver','127.0.0.1:19001','--attach','123']
                            out='MDDS_DEBUG_EXE='+argv[0]+'\nMDDS_DEBUG_STAT=456 (lldb-server) S '+' '.join(['0']*18)+' 999\nMDDS_DEBUG_CMDLINE_HEX_BEGIN\n'+(b'\0'.join(x.encode() for x in argv)+b'\0').hex(' ')+'\nMDDS_DEBUG_CMDLINE_HEX_END\nMDDS_REMOTE_EXIT=0\n'
                    elif label=='server-cleanup' and case=='cleanup-failed':out='MDDS_REMOTE_EXIT=1\n'
                    elif label=='forward-remove':out='[Fail] unable\n' if case=='remove-failed' else 'Remove forward ruler success\n'
                    elif label=='forward-removed-check':out='[Empty]\n'
                    self.buffers={'stdout':bytearray(out.encode()),'stderr':bytearray()}
            with patch.object(m,'validate_inputs',return_value={'starttime':'987'}),patch.object(m,'Command',Command),patch.object(m.time,'monotonic',side_effect=lambda:clock[0]):
                try:rc=m.main(window,'A',123)
                except RuntimeError:rc=1
            file=window/'A/debugger/RESULT.json';result=json.loads(file.read_text()) if file.exists() else None
            return rc,result,calls

    def test_actual_manager_complete(self):
        rc,row,_=self.boundary('complete');self.assertEqual(rc,0);self.assertTrue(row['support_closed'])
    def test_withheld_server_text_requires_actual_owned_tracer_before_client(self):
        rc,row,calls=self.boundary('withheld-server-text');self.assertEqual(rc,0);self.assertTrue(row['support_closed'])
        self.assertLess(calls.index('tracer-proof-identity'),calls.index('client'))
    def test_successful_detach_group_stop_uses_guarded_continue_and_rechecks(self):
        rc,row,calls=self.boundary('group-stopped');self.assertEqual(rc,0);self.assertTrue(row['support_closed'])
        self.assertLess(calls.index('owned-os-resume'),calls.index('inferior-after-os-resume'))
    def test_conflicting_visible_pid_rejected_before_client(self):
        rc,row,calls=self.boundary('contradicting-visible-pid');self.assertNotEqual(rc,0);self.assertNotIn('client',calls);self.assertFalse(row['support_closed'])
    def test_original_server_birth_cannot_be_overwritten_for_cleanup(self):
        rc,row,calls=self.boundary('server-birth-reused');self.assertNotEqual(rc,0);self.assertNotIn('server-cleanup',calls);self.assertFalse(row['support_closed']);self.assertIn('BIRTH_CHANGED',row['failure'])
    def test_all_startup_actions_share_one_remaining_deadline(self):
        with patch.object(m.time,'monotonic',return_value=29):self.assertEqual(m.startup_seconds(30),1)
        with patch.object(m.time,'monotonic',return_value=30):self.assertRaises(RuntimeError,m.startup_seconds,30)
    def test_actual_main_does_not_attach_after_shared_deadline_expired(self):
        rc,row,calls=self.boundary('startup-expired-after-forward');self.assertNotEqual(rc,0);self.assertNotIn('server',calls);self.assertNotIn('client',calls)
        self.assertIn('STARTUP_DEADLINE',row['failure'])

    def test_foreign_forward_or_reused_pid_never_mutated(self):
        for case in ('foreign-forward','reused-pid'):
            rc,row,calls=self.boundary(case);self.assertNotEqual(rc,0);self.assertNotIn('forward',calls);self.assertNotIn('forward-remove',calls)
            self.assertTrue(row['support_closed']);self.assertEqual(row['status'],'INPUT_REJECTED')

    def test_ambiguous_forward_is_blocked_not_removed(self):
        rc,row,calls=self.boundary('forward-failed');self.assertNotEqual(rc,0);self.assertFalse(row['support_closed']);self.assertNotIn('forward-remove',calls)

    def test_global_selector_conflict_blocks_attach_and_delete(self):
        rc,row,calls=self.boundary('after-selector-conflict');self.assertNotEqual(rc,0);self.assertNotIn('server',calls);self.assertNotIn('forward-remove',calls)
        rc,row,calls=self.boundary('before-remove-conflict');self.assertNotEqual(rc,0);self.assertFalse(row['support_closed']);self.assertNotIn('forward-remove',calls)

    def test_cleanup_or_terminal_evidence_failure_blocks_restoration(self):
        for case in ('cleanup-failed','missing-server-exit','remove-failed'):
            rc,row,_=self.boundary(case);self.assertNotEqual(rc,0);self.assertFalse(row['support_closed'])

    def test_observation_failure_remains_failure_when_support_restored(self):
        rc,row,_=self.boundary('client-failed');self.assertNotEqual(rc,0);self.assertTrue(row['support_closed']);self.assertEqual(row['status'],'OBSERVATION_INCOMPLETE');self.assertIn('INCOMPLETE',row['failure'])

    def test_breakpoint_installation_without_running_event_not_ready(self):
        rc,row,_=self.boundary('missing-running-event');self.assertEqual(row['status'],'UNAVAILABLE')
        self.assertIn('RUNNING_NOT_CONFIRMED',row['failure'])

    def test_actual_trace_no_hit_deadline_stops_and_detaches_session(self):
        api=types.ModuleType('lldb')
        for key,value in dict(eStateExited=1,eStateDetached=2,eStateInvalid=3,eStateStopped=4,eStateRunning=5,eStopReasonSignal=6).items():setattr(api,key,value)
        with patch.dict(sys.modules,{'lldb':api}):
            source=SOURCE.parents[2]/'rmw_lldb_trace.py'
            spec=importlib.util.spec_from_file_location('deadline_trace',source);trace_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(trace_module)
        class Error:
            def Fail(self):return False
            def Success(self):return True
        class Process:
            state=api.eStateRunning;continued=0;stops=0;detached=0
            def GetState(self):return self.state
            def Continue(self):self.continued+=1;return Error()
            def Stop(self):self.stops+=1;self.state=api.eStateStopped;return Error()
            def Detach(self):
                self.detached+=1;self.keep_stopped=False;self.state=api.eStateDetached;return Error()
            def GetExitStatus(self):return -1
        class Event:
            def GetType(self):return 1
        class Listener:
            count=0
            def GetNextEvent(self,event):
                self.count+=1;return self.count==1
        api.SBEvent=Event
        api.SBProcess=types.SimpleNamespace(EventIsProcessEvent=lambda e:True,
            GetProcessFromEvent=lambda e:types.SimpleNamespace(GetProcessID=lambda:123),
            GetStateFromEvent=lambda e:api.eStateRunning,GetRestartedFromEvent=lambda e:False,eBroadcastBitStateChanged=1)
        with tempfile.TemporaryDirectory() as folder:
            trace=trace_module.Trace.__new__(trace_module.Trace);trace.process=Process();trace.start=time.monotonic()-2
            trace.cfg={'seconds':1,'hit_limit':256,'run':'a'*16,'side':'A','pid':123};trace.stream=io.StringIO();trace.output=Path(folder);trace.seq=0
            trace.debugger=types.SimpleNamespace(GetListener=lambda:Listener());trace.slide=0;trace.rmw_sha='HOST_MODEL'
            events=[];trace.emit=lambda event,**fields:events.append(event)
            trace.run()
            self.assertEqual(trace.process.stops,1);self.assertEqual(trace.process.detached,1);self.assertFalse(trace.process.keep_stopped)
            self.assertIn('OBSERVATION_LIMIT',events);self.assertTrue((Path(folder)/'END.json').is_file())

    def test_start_record_failure_leaves_intent_unknown(self):
        class Child:
            pid=123;stdout=io.BytesIO();stderr=io.BytesIO();killed=False
            def kill(self):self.killed=True
            def wait(self,timeout):return 0
        child=Child();original=m.write
        def writer(path,value):
            if path.name.endswith('.started.json'):raise OSError('INJECTED_RECORD_FAILURE')
            original(path,value)
        with tempfile.TemporaryDirectory() as folder,patch.object(m.subprocess,'Popen',return_value=child),patch.object(m,'write',writer):
            root=Path(folder)
            self.assertRaises(RuntimeError,m.Command,root,'server',['INERT_TEST_COMMAND'],1)
            self.assertTrue((root/'server.intent.json').is_file());self.assertFalse((root/'server.started.json').exists());self.assertTrue(child.killed)

    def test_wrong_pid_trace_initialization_never_controls_rejected_process(self):
        api=types.ModuleType('lldb')
        for key,value in dict(eStateExited=1,eStateDetached=2,eStateInvalid=3,eStateStopped=4).items():setattr(api,key,value)
        with patch.dict(sys.modules,{'lldb':api}):
            source=SOURCE.parents[2]/'rmw_lldb_trace.py';spec=importlib.util.spec_from_file_location('wrong_pid_trace',source)
            trace=importlib.util.module_from_spec(spec);spec.loader.exec_module(trace)
        class Process:
            detached=0
            def IsValid(self):return True
            def GetState(self):return api.eStateStopped
            def GetProcessID(self):return 999
            def Detach(self):self.detached+=1;raise AssertionError('REJECTED_PROCESS_MUST_NOT_BE_CONTROLLED')
        process=Process()
        def initialize(self,cfg):self.process=process;raise RuntimeError('DEBUGGER_PID_MISMATCH')
        with tempfile.TemporaryDirectory() as f:
            root=Path(f);config=root/'config.json';config.write_text(json.dumps(dict(run='a'*16,side='A',pid=123,hit_limit=256,seconds=1,output=str(root/'trace'))))
            with patch.object(trace.Trace,'__init__',initialize):self.assertRaises(RuntimeError,trace.run_config,str(config))
            self.assertEqual(process.detached,0);self.assertFalse(json.loads((root/'trace/FAILED.json').read_text())['debugger_detached'])

if __name__=='__main__':unittest.main()
