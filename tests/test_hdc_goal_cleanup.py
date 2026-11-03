"""Pure targeted tests: real maintained backend/driver, no live backend ctor/API."""
import contextlib
import copy
import importlib.util
import json
from pathlib import Path
import sys
import time
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'scripts'/'hdc_goal'

class ContractError(Exception):pass

def need(ok,code):
    if not ok:raise ContractError(code)

@contextlib.contextmanager
def load_pair(source=SOURCE):
    # Only the bundle's pure contract is a seam. Both actual maintained class
    # implementations and their unchanged notice gate run, without __init__.
    contract=types.ModuleType('goal_contract')
    contract.LIMITS={'total_ms':60000,'work_ms':56000,'clients':2,'concurrent_clients':1,
      'server_capture':4194304,'client_capture':65536,'stream_capture':32768,
      'long_ms':15000,'client_ms':15000,'stop_ms':2000,'server_ready_ms':10000}
    contract.RUN_ROOT=Path('SYNTHETIC_NOT_A_REAL_RUN')
    contract.ContractError=ContractError;contract.need=need
    saved={n:sys.modules.get(n) for n in ('goal_contract','goal_driver','goal_windows_backend')}
    sys.modules['goal_contract']=contract
    try:
        for name in ('goal_driver','goal_windows_backend'):
            spec=importlib.util.spec_from_file_location(name,source/(name+'.py'))
            module=importlib.util.module_from_spec(spec);sys.modules[name]=module
            spec.loader.exec_module(module)
        yield sys.modules['goal_driver'],sys.modules['goal_windows_backend']
    finally:
        for n,v in saved.items():
            if v is None:sys.modules.pop(n,None)
            else:sys.modules[n]=v

class SecretError(Exception):
    def __str__(self):raise AssertionError('exception text must not be read')
    def __repr__(self):raise AssertionError('exception repr must not be read')

class Clock:
    def __init__(self):self.value=time.monotonic()
    def __call__(self):return self.value

class Marker:
    def __init__(self):
        self.notice=False;self.feed_calls=[];self.close_calls=0;self.fail=None
        self.rule='AUTH_CONTEXT_UNKNOWN'
    def feed(self,case,stream,data,phase,b,a):
        self.feed_calls.append((case,stream,phase))
        if self.fail=='feed':raise SecretError('SYNTHETIC_CANARY')
        self.notice=True
    def report(self):
        if self.fail=='report':raise SecretError('SYNTHETIC_CANARY')
        return {'classification':'UNKNOWN','record_failed':self.fail=='record',
          'overflow':False,'failure_rules':[],'emergency_events':[],
          'events':[{'case':'ready.A','event_type':'UNKNOWN','rule_id':self.rule,'complete_record':True}] if self.notice else []}
    def eof(self,*args):
        if self.fail=='eof':raise SecretError('SYNTHETIC_CANARY')
    def advance(self):
        if self.fail=='advance':raise SecretError('SYNTHETIC_CANARY')
    def close(self):
        self.close_calls+=1
        if self.fail=='close':raise SecretError('SYNTHETIC_CANARY')

class Journal:
    def __init__(self):self.failed=False;self.rows=[];self.closed=0;self.fail=None
    def emit(self,row):
        if self.fail=='emit':self.failed=True;raise SecretError('SYNTHETIC_CANARY')
        self.rows.append(copy.deepcopy(row))
    def close(self):
        self.closed+=1
        if self.fail=='close':raise SecretError('SYNTHETIC_CANARY')
        if self.fail=='late':self.failed=True

class Root:
    def __init__(self,case,clock):
        self.case=case;self.owner={'pid':1 if case=='server' else 2}
        self.clock=clock;self.goal_started=time.monotonic();self.closed=False
        self.dest=Path(case);self.fail=None;self.close_calls=0;self.stop_calls=0
        self.reads=[{'name':s,'eof':False} for s in ('stdout','stderr')]
        self.receipt={'case':case,'terminal':False,'terminal_binding_verified':False,
          'EOF':False,'native_exit':None,'signal_authority':True,'stop_attempts':0,
          'cleanup_errors':[]}
    def state(self):
        if self.fail in ('identity','state'):
            self.receipt['signal_authority']=False
            raise SecretError('SYNTHETIC_CANARY')
        return 'TERMINAL' if self.receipt['terminal'] else 'LIVE'
    def identity(self):
        if self.fail=='terminal_identity':raise SecretError('SYNTHETIC_CANARY')
        if self.receipt['terminal']:self.receipt['terminal_binding_verified']=True
    def stop(self,deadline):
        need(self.receipt['signal_authority'] is True,'NO_AUTHORITY')
        need(self.receipt['stop_attempts']==0,'STOP_ONCE')
        need(self.clock()<deadline,'STOP_DEADLINE')
        self.stop_calls+=1;self.receipt['stop_attempts']=1
        if self.fail=='wait':raise SecretError('SYNTHETIC_CANARY')
        self.receipt.update(terminal=True,terminal_binding_verified=True,native_exit=125,signal_authority=False)
    def read(self,remaining,observer):
        if self.case!='server':observer('stdout',b'SYNTHETIC_AUTH_CONTEXT',1,2)
        return []
    def drain(self,deadline,remaining,observer):
        if self.fail=='drain':raise SecretError('SYNTHETIC_CANARY')
        held=self.fail=='held_writer' or self.clock()>=deadline
        self.receipt['EOF']=not held
        for r in self.reads:r['eof']=not held
        self.receipt['drain']={'status':'DEADLINE' if held else 'EOF','bytes':0,'EOF':not held}
        return self.receipt['drain']
    def close(self):
        self.close_calls+=1;self.closed=True;self.receipt['signal_authority']=False
        if self.fail=='close':raise SecretError('SYNTHETIC_CANARY')

class CleanupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ctx=load_pair();cls.driver_mod,cls.backend_mod=cls.ctx.__enter__()
    @classmethod
    def tearDownClass(cls):cls.ctx.__exit__(None,None,None)
    def make(self):
        b=self.backend_mod.GoalWindowsBackend.__new__(self.backend_mod.GoalWindowsBackend)
        clock=Clock();b.marker=Marker();b.journal=Journal();b.keys_before=[]
        b.capture_failed=False;b.captured={'server':0,'ready.A':0};b.stream_bytes={}
        b.latest={};b.last_sample=time.monotonic();b.baseline=None;b.stopped=False
        b.pins=[11,22,33];b.closed_pins=[];b.records={};b.fail_put=False;b.fail_pin=None
        def close(h):
            b.closed_pins.append(h)
            if b.fail_pin=='all_raise' or h==b.fail_pin:raise SecretError('SYNTHETIC_CANARY')
            return True
        def put(path,receipt):
            if b.fail_put:raise SecretError('SYNTHETIC_CANARY')
            need(str(path) not in b.records,'RECORD_ONCE');b.records[str(path)]=copy.deepcopy(receipt)
        b.w=types.SimpleNamespace(K=types.SimpleNamespace(CloseHandle=close),put=put,
          key_metadata=lambda:[],snapshot=lambda known:[],_journal=b.journal)
        registry={s:types.SimpleNamespace(id=s,kind='server' if s=='server' else 'challenge',
          requires=() if s=='server' else ('server',) if s=='ready.A' else ('server','ready.A'),
          requires_started=(),requires_terminal=() if s!='ready.B' else ('ready.A',))
          for s in ('server','ready.A','ready.B')}
        b.grant=types.SimpleNamespace(server_unknown_continue=False,registry=registry,
          deadline=clock()+60,halt=lambda:None)
        b.roots=[Root('server',clock),Root('ready.A',clock)]
        b.server_ok=lambda:True;b.storage=lambda:None;b.observe=lambda *args:None
        d=self.driver_mod.GoalDriver(registry,b,clock=clock)
        d.live={r.case:r for r in b.roots};d.used={'server','ready.A'};d.accepted={'server'}
        return b,d,clock
    def assert_no_secret(self,value):
        self.assertTrue('SYNTHETIC_CANARY' not in json.dumps(value))
    def test_real_monitor_gate_halts_A_and_B_but_cleanup_keeps_OS_facts(self):
        b,d,c=self.make()
        with self.assertRaises(ContractError):d.pump()
        self.assertEqual(d.failure,'OBSERVATION_OR_RECEIPT_UNKNOWN')
        with self.assertRaises(ContractError):d.start('ready.B')
        result=d.finish()
        self.assertEqual(result['failure'],'OBSERVATION_OR_RECEIPT_UNKNOWN')
        self.assertEqual(result['accepted'],['server'])
        self.assertTrue(all(r['state']=='TERMINAL' and r['EOF'] is True for r in result['cleanup']))
        self.assertTrue(all(r['business_blocked'] for r in result['cleanup']))
        self.assertEqual(result['finalization']['state'],'COMPLETE')
        self.assertTrue(result['finalization']['business_blocked'])
        self.assertEqual([r.stop_calls for r in b.roots],[1,1])
        self.assertEqual([r.close_calls for r in b.roots],[1,1])
        self.assertEqual(b.closed_pins,[33,22,11]);self.assertEqual(b.journal.closed,1)
        self.assertEqual(b.marker.feed_calls,[('ready.A','stdout','MONITOR')])
        self.assert_no_secret(result)
    def test_normal_exit_closes_without_a_signal_and_without_business_failure(self):
        b,d,c=self.make()
        for root in b.roots:
            root.receipt.update(terminal=True,terminal_binding_verified=True,native_exit=0,signal_authority=False)
        result=d.finish()
        self.assertIsNone(result['failure'])
        self.assertTrue(all(r['state']=='TERMINAL' for r in result['cleanup']))
        self.assertEqual([r.stop_calls for r in b.roots],[0,0])
    def test_identity_wait_EOF_and_deadline_are_unknown_but_other_root_closes(self):
        for failure in ('identity','state','wait','held_writer','terminal_identity','drain','close'):
            with self.subTest(failure=failure):
                b,d,c=self.make();b.roots[1].fail=failure
                result=d.finish();row=next(r for r in result['cleanup'] if r['step']=='ready.A')
                self.assertEqual(row['state'],'UNKNOWN')
                self.assertTrue(result['failure'] is not None)
                self.assertEqual(b.roots[0].close_calls,1);self.assertEqual(b.roots[1].close_calls,1)
                self.assertEqual(b.closed_pins,[33,22,11])
                if failure in ('identity','state'):self.assertEqual(b.roots[1].stop_calls,0)
                self.assert_no_secret(result)
        b,d,c=self.make();c.value=d.hard_deadline+1
        result=d.finish();self.assertTrue(all(r['state']=='UNKNOWN' for r in result['cleanup']))
        self.assertEqual([r.stop_calls for r in b.roots],[0,0])
    def test_notice_finalize_and_record_failure_are_not_success(self):
        for failure in ('eof','advance','report','record','close'):
            with self.subTest(failure=failure):
                b,d,c=self.make();b.marker.notice=True;b.marker.fail=failure
                result=d.finish();self.assertIsNotNone(result['failure'])
                self.assertTrue(any(r['state']=='UNKNOWN' for r in result['cleanup']) or result['finalization']['state']=='UNKNOWN')
                self.assertEqual([r.close_calls for r in b.roots],[1,1]);self.assertEqual(b.closed_pins,[33,22,11])
                self.assert_no_secret(result)
    def test_observation_faults_with_record_failed_false_stay_cleanup_unknown(self):
        for rule in ('CLOCK_INVALID','THREAD_INVALID','BINDING_INVALID','UTF8_INVALID',
                     'SINK_FAILURE','EVENT_CAP','PARTIAL_RECORD','UNKNOWN_FUTURE_RULE'):
            with self.subTest(rule=rule):
                b,d,c=self.make();b.marker.notice=True;b.marker.rule=rule
                result=d.finish()
                self.assertTrue(all(x['state']=='UNKNOWN' for x in result['cleanup']))
                self.assertEqual(result['finalization']['state'],'UNKNOWN')
                self.assertTrue(all('NOTICE_OBSERVATION_UNRELIABLE' in x['cleanup_errors'] for x in result['cleanup']))
                self.assertEqual([r.close_calls for r in b.roots],[1,1])
                self.assertEqual(b.closed_pins,[33,22,11])
    def test_closed_root_EOF_without_separate_drain_needs_no_effects(self):
        b,d,c=self.make();root=b.roots[0];root.closed=True
        root.receipt.update(terminal=True,terminal_binding_verified=True,EOF=True,native_exit=0)
        result=b.cleanup(root,c()+2)
        self.assertEqual(result['state'],'TERMINAL')
        self.assertEqual((root.stop_calls,root.close_calls,len(b.records)),(0,0,0))
        root.receipt['drain']={'status':'DEADLINE'}
        self.assertEqual(b.cleanup(root,c()+2)['state'],'UNKNOWN')
        self.assertEqual((root.stop_calls,root.close_calls,len(b.records)),(0,0,0))
    def test_root_record_pin_and_journal_failures_are_separate_and_finite(self):
        for failure in ('put','pin','emit','journal_close','late'):
            with self.subTest(failure=failure):
                b,d,c=self.make()
                if failure=='put':b.fail_put=True
                elif failure=='pin':b.fail_pin=33
                else:b.journal.fail={'emit':'emit','journal_close':'close','late':'late'}[failure]
                result=d.finish();self.assertIsNotNone(result['failure'])
                self.assertEqual(b.closed_pins,[33,22,11]);self.assertEqual(b.journal.closed,1)
                self.assertIsNone(b.w._journal);self.assert_no_secret(result)
    def test_finish_and_late_start_do_not_repeat_any_effect(self):
        b,d,c=self.make();d.halt('OPERATOR_CANCEL')
        first=d.finish();counts=([r.stop_calls for r in b.roots],list(b.closed_pins),len(b.records),b.journal.closed)
        self.assertIs(first,d.finish())
        self.assertEqual(counts,([r.stop_calls for r in b.roots],b.closed_pins,len(b.records),b.journal.closed))
        with self.assertRaises(ContractError):d.start('ready.B')
        self.assertEqual(first['failure'],'OPERATOR_CANCEL')
    def test_reentry_is_unknown_without_a_second_cleanup(self):
        b,d,c=self.make();original=b.roots[1].close
        def close():
            middle=d.finish();self.assertTrue(middle['failure'] is not None);original()
        b.roots[1].close=close
        result=d.finish();self.assertEqual(result['failure'],'CLEANUP_REENTRY_UNKNOWN')
        self.assertEqual([r.stop_calls for r in b.roots],[1,1])
    def test_import_and_fixture_never_load_live_WinDLL_or_backend_constructor(self):
        with patch('ctypes.WinDLL',side_effect=AssertionError('LIVE_FORBIDDEN'),create=True):
            with load_pair() as (driver,backend):
                self.assertEqual(backend.GoalWindowsBackend.cleanup.__module__,'goal_windows_backend')
        self.assertNotIn('goal_win32_owned',sys.modules)

if __name__=='__main__':unittest.main(verbosity=2)
