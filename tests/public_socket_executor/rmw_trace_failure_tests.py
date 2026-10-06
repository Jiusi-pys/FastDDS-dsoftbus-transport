"""Production Trace/run_config error terminals; injected SB API, no processes/device authority."""
"""Execute production Trace.run; fake SB API only, no inferior/device process."""
from pathlib import Path
import importlib.util,json,sys,types,time,hashlib,subprocess
from unittest.mock import patch
root=Path(__file__).resolve().parents[2]/'scripts/public_socket'
api=types.ModuleType('lldb')
for k,v in {'eStateStopped':1,'eStateRunning':2,'eStateExited':3,'eStateDetached':4,'eStateInvalid':5,'eStopReasonBreakpoint':6,'eStopReasonSignal':7}.items():setattr(api,k,v)
class Error:
 def __init__(self,failed=False):self.failed=failed
 def Fail(self):return self.failed
 def Success(self):return not self.failed
 def __str__(self):return 'INJECTED_RECONTINUE_FAILURE' if self.failed else 'success'
class Event:
 def GetType(self):return 1
api.SBEvent=Event
class ProcessAPI:
 eBroadcastBitStateChanged=1
 @staticmethod
 def EventIsProcessEvent(e):return True
 @staticmethod
 def GetProcessFromEvent(e):return e.process
 @staticmethod
 def GetStateFromEvent(e):return e.state
 @staticmethod
 def GetRestartedFromEvent(e):return getattr(e,'restarted',False)
api.SBProcess=ProcessAPI
with patch.dict(sys.modules,{'lldb':api}):
 s=importlib.util.spec_from_file_location('actual_trace',root/'rmw_lldb_trace.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Frame:
 def __init__(self,thread,valid=True):self.thread=thread;self.valid=valid
 def IsValid(self):return self.valid
 def GetThread(self):return self.thread
 def GetPC(self):return 4096
 def GetFunctionName(self):return 'host_unit_owned_point'
class Thread:
 def __init__(self,reason):self.reason=reason
 def GetStopReason(self):return self.reason
 def GetStopReasonDataCount(self):return 2
 def GetStopReasonDataAtIndex(self,i):return 1
 def GetFrameAtIndex(self,i):return Frame(self,i==0)
 def GetThreadID(self):return 11
class Location:
 def IsValid(self):return True
 def GetBreakpoint(self):return Breakpoint()
class Breakpoint:
 def GetID(self):return 1
 def SetEnabled(self,value):pass
 def FindLocationByID(self,i):return Location()
class Target:
 def FindBreakpointByID(self,i):return Breakpoint()
 def breakpoint_iter(self):return iter([Breakpoint()])
class Listener:
 def __init__(self,p):self.p=p
 def GetNextEvent(self,event):
  if not self.p.queue:return False
  event.state=self.p.queue.pop(0);event.process=self.p;self.p.state=event.state;return True
class Process:
 def __init__(self,case):self.case=case;self.state=api.eStateStopped;self.queue=[];self.continues=0;self.detaches=0
 def GetState(self):return self.state
 def IsValid(self):return True
 def GetProcessID(self):return 123
 def GetNumThreads(self):return 1
 def GetThreadAtIndex(self,i):
  thread=Thread(api.eStopReasonSignal if self.case in ('repeated_signal','bad_signal_stack') else api.eStopReasonBreakpoint)
  if self.case=='bad_signal_stack':thread.GetFrameAtIndex=lambda i:Frame(thread,False)
  return thread
 def Continue(self):
  self.continues+=1
  if self.continues>1 and self.case=='owned_bp_continue_failure':return Error(True)
  self.state=api.eStateRunning;self.queue.extend([api.eStateRunning] if self.case=='detach_failure' else [api.eStateRunning,api.eStateStopped]);return Error()
 def Stop(self):self.state=api.eStateStopped;return Error()
 def Detach(self):self.detaches+=1;self.state=api.eStateDetached;return Error(self.case=='detach_failure')
 def GetExitStatus(self):return -1
class Debugger:
 def __init__(self,p):self.listener=Listener(p)
 def GetListener(self):return self.listener

import tempfile,unittest
class FailureTerminal(unittest.TestCase):
 def check_initial_events(self,case):
  with tempfile.TemporaryDirectory() as folder:
   output=Path(folder);process=Process('initial_events');process.state=api.eStateStopped
   class Events:
    index=0
    def GetNextEvent(self,event):
     self.index+=1
     sequence=[(api.eStateStopped,True),None,(api.eStateRunning,False),None,(api.eStateExited,False),None] if case=='restart' else [(api.eStateExited,False),None]
     if self.index>len(sequence) or sequence[self.index-1] is None:return False
     event.state,event.restarted=sequence[self.index-1];process.state=event.state;event.process=process
     if case=='foreign':event.process=types.SimpleNamespace(GetProcessID=lambda:999)
     return True
   listener=Events();trace=m.Trace.__new__(m.Trace);trace.cfg=dict(run='a'*16,side='A',pid=123,seconds=1,hit_limit=256)
   trace.output=output;trace.stream=(output/'events.jsonl').open('x');trace.seq=0;trace.start=time.monotonic()
   trace.process=process;trace.debugger=types.SimpleNamespace(GetListener=lambda:listener);trace.slide=0;trace.rmw_sha='0'*64
   try:
    with patch.object(subprocess,'Popen',side_effect=AssertionError('NO_EXTERNAL_PROCESS')):
     if case=='restart':trace.run();self.assertTrue((output/'READY.json').exists())
     else:
      self.assertRaisesRegex(RuntimeError,'DEBUGGER_EVENT_PID_MISMATCH' if case=='foreign' else 'INITIAL_PROCESS_STATE_NOT_RESUMABLE',trace.run)
      self.assertFalse((output/'READY.json').exists());self.assertFalse((output/'END.json').exists())
    self.assertEqual(process.continues,0)
   finally:trace.stream.close()
 def test_pending_restart_waits_for_owned_running_event(self):self.check_initial_events('restart')
 def test_foreign_event_rejected_without_resume(self):self.check_initial_events('foreign')
 def test_exited_initial_process_never_resumed(self):self.check_initial_events('exit')
 def test_setup_running_has_no_duplicate_continue(self):
  with tempfile.TemporaryDirectory() as folder:
   output=Path(folder)/'trace';output.mkdir();process=Process('already_running')
   process.state=api.eStateRunning;process.queue=[api.eStateRunning]
   class RunningListener(Listener):
    empty_seen=False
    def GetNextEvent(self,event):
     if not self.p.queue and not self.empty_seen:self.empty_seen=True;return False
     if not self.p.queue and self.p.state==api.eStateRunning:self.p.queue.append(api.eStateExited)
     return super().GetNextEvent(event)
   def forbidden_continue():process.continues+=1;return Error(True)
   process.Continue=forbidden_continue
   trace=m.Trace.__new__(m.Trace);trace.cfg=dict(run='a'*16,side='A',pid=123,seconds=1,hit_limit=256)
   trace.output=output;trace.stream=(output/'events.jsonl').open('x');trace.seq=0;trace.start=time.monotonic()
   trace.process=process;trace.debugger=types.SimpleNamespace(GetListener=lambda:RunningListener(process));trace.slide=0;trace.rmw_sha='0'*64
   try:
    with patch.object(subprocess,'Popen',side_effect=AssertionError('NO_EXTERNAL_PROCESS')):trace.run()
   finally:trace.stream.close()
   self.assertEqual(process.continues,0);self.assertTrue((output/'READY.json').exists());self.assertTrue((output/'END.json').exists())
 def check(self,case,expected):
  with tempfile.TemporaryDirectory() as folder:
   output=Path(folder)/'trace';config=Path(folder)/'config.json'
   cfg=dict(run='a'*16,side='A',pid=123,seconds=1,hit_limit=256,output=str(output));config.write_text(json.dumps(cfg))
   process=Process(case)
   def initialize(trace,cfg):
    output.mkdir();trace.cfg=cfg;trace.output=output;trace.stream=(output/'events.jsonl').open('x');trace.seq=0
    trace.start=time.monotonic()-2 if case=='detach_failure' else time.monotonic()
    trace.process=process;trace.debugger=Debugger(process);trace.target=Target();trace.slide=0;trace.rmw_sha='0'*64
    trace.breakpoints={1:'HOST_OWNED_POINT'};trace.hits=0;trace.disabled=False
   try:
    with patch.object(m.Trace,'__init__',initialize),patch.object(subprocess,'Popen',side_effect=AssertionError('NO_EXTERNAL_PROCESS')):
     self.assertRaisesRegex(RuntimeError,expected,m.run_config,str(config))
   finally:
    if hasattr(m.SESSION,'stream'):m.SESSION.stream.close()
   failed=json.loads((output/'FAILED.json').read_text());self.assertIn(expected,failed['error'])
   self.assertFalse((output/'END.json').exists());self.assertEqual((output/'READY.json').exists(),case!='bad_signal_stack')
   if case=='bad_signal_stack':
    rows=[json.loads(x) for x in (output/'events.jsonl').read_text().splitlines()]
    signal=next(x for x in rows if x['event']=='SIGNAL_STOP');self.assertEqual(signal['signals'][0]['number'],1);self.assertEqual(signal['frames'],[])
   m.SESSION.stream.close()
 def test_real_continue_error_preserved(self):self.check('owned_bp_continue_failure','CONTINUE_FAILED')
 def test_repeated_signal_not_normal_end(self):self.check('repeated_signal','REPEATED_SIGNAL_STOP')
 def test_detach_failure_not_normal_end(self):self.check('detach_failure','DEBUGGER_DETACH_FAILED')
 def test_signal_number_preserved_with_unavailable_stack(self):self.check('bad_signal_stack','SIGNAL_FRAMES_UNAVAILABLE')
if __name__=='__main__':unittest.main()
