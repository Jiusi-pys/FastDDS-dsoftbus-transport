"""Synthetic records and pinned public source only; never open device output."""
import importlib.util
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
decoder=load('startup_cause',ROOT/'scripts/hdc_goal/startup_cause.py')
instrument=load('instrument',ROOT/'tests/native_startup_diagnostics/instrument.py')
def frame(**kw):
    d=dict(zip(decoder.KEYS,('S1GOALCAUSE1',1,12,7,12,4,3,4)));d.update(kw)
    return ''.join(f'{k}={v}\n' for k,v in d.items()).encode()
class CauseTests(unittest.TestCase):
    def test_valid_separates_gate_and_main(self):
        d=decoder.decode(frame());self.assertEqual(d['first_reason'],'PARTICIPANT_NULL')
        self.assertEqual(d['gate_reason'],'WAIT_FAILED');self.assertFalse(d['success_inferred'])
        self.assertEqual(d['association'],'FIXED_PATH_ONLY');self.assertEqual(d['SDK_quiet'],'UNKNOWN')
    def test_every_public_reason(self):
        for i,stage in ((1,1),(2,1),(3,6),(4,7),(5,9),(6,10),(7,12),(8,1),(9,3)):
            with self.subTest(reason=i):self.assertEqual(decoder.decode(frame(first_reason=i,failure_stage=stage))['first_reason'],decoder.REASONS[i])
    def test_lf_is_the_only_separator(self):
        for separator in (b'\v',b'\f',b'\x1c',b'\x1d',b'\x1e'):
            with self.subTest(separator=separator),self.assertRaises(ValueError):decoder.decode(frame().replace(b'\n',separator)[:-1]+b'\n')
    def test_incompatible_reason_stage_is_rejected(self):
        for reason,correct in ((1,1),(2,1),(3,6),(4,7),(5,9),(6,10),(7,12)):
            for stage in range(14):
                if stage!=correct:
                    with self.subTest(reason=reason,stage=stage),self.assertRaises(ValueError):decoder.decode(frame(first_reason=reason,failure_stage=stage))
        for reason,correct in ((1,2),(2,3),(3,4)):
            for stage in range(6):
                if stage!=correct:
                    with self.subTest(gate_reason=reason,stage=stage),self.assertRaises(ValueError):decoder.decode(frame(gate_reason=reason,gate_failure_stage=stage))
    def test_generic_catches_preserve_entered_stage(self):
        for reason in (8,9):
            for stage in range(1,14):
                self.assertEqual(decoder.decode(frame(first_reason=reason,failure_stage=stage))['failure_stage'],decoder.STAGES[stage])
        for stage in range(1,5):
            self.assertEqual(decoder.decode(frame(gate_reason=4,gate_failure_stage=stage))['gate_failure_stage'],decoder.GATE_STAGES[stage])
        with self.assertRaises(ValueError):decoder.decode(frame(gate_reason=4,gate_failure_stage=5))
    def test_record_does_not_prove_return_status_or_phase(self):
        d=decoder.decode(frame())
        self.assertNotIn('original_status_unchanged',d)
        self.assertEqual(d['producer_contract'],'NS_ABI_STATUS_PHASE_UNCHANGED_BY_DESIGN')
        self.assertFalse(d['success_inferred'])
    def test_absent_snapshot_not_success(self):
        d=decoder.decode(frame(available=0,entered_stage=0,first_reason=0,failure_stage=0,gate_entered_stage=0,gate_reason=0,gate_failure_stage=0))
        self.assertFalse(d['available']);self.assertFalse(d['success_inferred'])
    def test_reject_schema(self):
        b=frame()
        for bad in (b'',b[:-1],b+b'x=1\n',b+b'available=1\n',b.replace(b'CAUSE1',b'CAUSE2'),b.replace(b'\n',b'\r\n'),b.replace(b'available=1',b'available=\xff')):
            with self.subTest(kind=len(bad)),self.assertRaises(ValueError):decoder.decode(bad)
    def test_reject_number_and_enum(self):
        for k,v in (('available',2),('entered_stage',14),('gate_entered_stage',6),('first_reason',10),('gate_reason',5),('failure_stage','012'),('first_reason','-1'),('first_reason',' 1'),('first_reason','1.0')):
            with self.subTest(field=k,value=v),self.assertRaises(ValueError):decoder.decode(frame(**{k:v}))
    def test_reject_contradictions(self):
        for kw in ({'first_reason':0},{'failure_stage':0},{'gate_reason':0},{'gate_failure_stage':0},{'available':0}):
            with self.subTest(kw=kw),self.assertRaises(ValueError):decoder.decode(frame(**kw))
    def test_source_overlay_preserves_return_sequence(self):
        old=instrument.inputs();new=instrument.overlay()
        for name,b in new.items():
            self.assertEqual(re.findall(rb'return (NS_[A-Z_]+|r.status);',old[name]),re.findall(rb'return (NS_[A-Z_]+|r.status);',b))
        self.assertEqual(old['native_contract.h'],(instrument.SOURCE/'native_contract.h').read_bytes())
        self.assertEqual(old['run_contract.h'],(instrument.SOURCE/'run_contract.h').read_bytes())
    def test_overlay_order_lifetime_and_original_phase(self):
        o={k:v.decode() for k,v in instrument.overlay().items()};s=o['direct_goal/direct_main.cpp']
        self.assertLess(s.index('const bool users_done='),s.index('const auto startup_cause='))
        self.assertLess(s.index('const auto startup_cause='),s.index('api.destroy(handle)'))
        self.assertLess(s.index('stage1_startup::publish('),s.index('files.publish("data-result."'))
        old=instrument.inputs()['consumer_run.cpp'].decode('utf-8-sig')
        self.assertEqual(re.findall(r'out.phase=\d+',old),re.findall(r'out.phase=\d+',o['consumer_run.cpp']))
    def test_source_drift_fails_closed(self):
        with self.assertRaises(ValueError):instrument.once('xx','x','z')
        with self.assertRaises(ValueError):instrument.consumer('unrecognized source')
if __name__=='__main__':unittest.main()
