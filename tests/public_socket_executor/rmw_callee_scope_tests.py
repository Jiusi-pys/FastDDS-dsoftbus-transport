"""Production callee breakpoint scope; injected registers, never device evidence."""
import importlib.util
from pathlib import Path
import sys
import time
import types
import unittest
from unittest.mock import patch

with patch.dict(sys.modules, {'lldb': types.ModuleType('lldb')}):
    spec = importlib.util.spec_from_file_location('callee_trace', Path(__file__).resolve().parents[2]/'scripts/public_socket/rmw_lldb_trace.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

class Breakpoint:
    def __init__(self, identity): self.identity=identity; self.enabled=True; self.calls=[]
    def GetID(self): return self.identity
    def SetEnabled(self, value): self.calls.append(value); self.enabled=value

class CalleeScope(unittest.TestCase):
    def setUp(self):
        self.trace = module.Trace.__new__(module.Trace)
        t=self.trace; t.start=time.monotonic(); t.hits=0; t.disabled=False
        t.cfg=dict(seconds=10,hit_limit=256); t.slide=0; t.layout=dict(cast_return=0x42,got=0x80)
        t.breakpoints={1:'CAST_CALLEE',2:'CAST_CALL'}; t.callees={0x1000}
        self.bp=Breakpoint(1); self.call_bp=Breakpoint(2); self.rows=[]
        t.emit=lambda event,**fields:self.rows.append(dict(event=event,**fields))
        self.registers=dict(x30=0x42,x0=0x5000,x1=0x6000,x2=0x7000,x3=72)
        t.reg=lambda f,n:self.registers[n]; t.module_at=lambda address:'libc++_shared.so'
        t.word=lambda address:0x1000 if address==0x80 else 0x9000
        t.rtti_snapshot=lambda *args:{'status':'INJECTED_NOT_DEVICE'}
        self.frame=types.SimpleNamespace(GetThread=lambda:types.SimpleNamespace(GetThreadID=lambda:123),GetPC=lambda:0x1000)
        self.location=types.SimpleNamespace(GetBreakpoint=lambda:self.bp)

    def test_first_matching_callee_is_disabled_without_changing_original_point(self):
        self.trace.hit(self.frame,self.location)
        self.assertFalse(self.bp.enabled)
        self.assertTrue(self.call_bp.enabled)
        self.assertEqual(self.bp.calls,[False])
        self.assertEqual(self.trace.callees,{0x1000})
        self.assertEqual(self.rows[0]['capture_scope'],'FIRST_MATCH_PER_CALLEE')

    def test_unrelated_return_address_does_not_consume_original_observation(self):
        self.registers['x30']=0x99
        self.trace.hit(self.frame,self.location)
        self.assertTrue(self.bp.enabled)
        self.assertEqual(self.bp.calls,[])
        self.assertEqual(self.rows,[])

    def test_later_original_call_does_not_reenable_or_duplicate_callee_breakpoint(self):
        self.trace.hit(self.frame,self.location)
        self.trace.target=types.SimpleNamespace(BreakpointCreateByAddress=lambda address:self.fail('callee reinstalled'))
        self.trace.hit(self.frame,types.SimpleNamespace(GetBreakpoint=lambda:self.call_bp))
        self.assertFalse(self.bp.enabled)
        self.assertEqual(self.bp.calls,[False])
        self.assertEqual([r['event'] for r in self.rows],['CAST_CALLEE','CAST_CALL'])

    def test_disable_failure_propagates_instead_of_claiming_narrowed_observation(self):
        def fail(value): raise RuntimeError('INJECTED_DISABLE_FAILURE')
        self.bp.SetEnabled=fail
        with self.assertRaisesRegex(RuntimeError,'INJECTED_DISABLE_FAILURE'):
            self.trace.hit(self.frame,self.location)
        self.assertEqual(self.rows,[])

if __name__=='__main__': unittest.main()
