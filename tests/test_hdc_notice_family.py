"""Offline notice-family and real capture/cleanup integration; no tool process."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m);return m

notice=load('notice_family_under_test',ROOT/'scripts/hdc_goal/goal_notice.py')
builder=load('offline_readiness_builder_under_test',ROOT/'scripts/hdc_goal/prepare_readiness.py')
harness=load('pure_cleanup_harness',ROOT/'tests/test_hdc_goal_cleanup.py')

class FamilyTests(unittest.TestCase):
    def marker(self,**kw):return notice.Marker(clock=lambda:10**15,**kw)
    def feed(self,m,data):m.feed('ready.A','stdout',data,'MONITOR',1,2)
    def test_three_families_at_actual_fallback_and_every_fragment_boundary(self):
        samples=((b'auth synthetic CANARY\n','AUTH_WORD_ONLY'),
                 (b'[E000099] synthetic CANARY\n','ECODE_PREFIX_ONLY'),
                 (b'auth [E000099] synthetic CANARY\n','BOTH'))
        for data,family in samples:
            for split in range(len(data)+1):
                m=self.marker();self.feed(m,data[:split]);self.feed(m,data[split:])
                result=m.report();self.assertTrue(m.notice)
                self.assertEqual(result['classification'],'UNKNOWN')
                events=[x for x in result['events'] if x['rule_id']=='AUTH_CONTEXT_UNKNOWN']
                self.assertEqual(len(events),1);self.assertEqual(events[0]['trigger_family'],family)
                self.assertEqual(events[0]['schema'],'NOTICE_EVENT_V2')
                self.assertTrue('CANARY' not in json.dumps(result))
    def test_known_failure_request_denial_and_diagnostics_keep_policy(self):
        for data,kind in ((b'[Fail][E000004]:The communication channel is being established.\n','COMMAND_FAILURE'),
                          (b'please authorize this device\n','REQUEST_TEXT'),
                          (b'[E000003] denied\n','DENIED_TEXT'),
                          (b'auth_result=0\n','DIAGNOSTIC')):
            m=self.marker();self.feed(m,data);result=m.report()
            self.assertEqual(result['classification'],kind)
            self.assertTrue(all(x['trigger_family']=='NOT_APPLICABLE' for x in result['events']))
        m=self.marker();self.feed(m,b'[Fail][E000004]:The communication channel is being established.\nMDDS_READY_A_FAKE\n__MDDS_READY_RC=0\n')
        self.assertEqual(m.report()['classification'],'COMMAND_FAILURE');self.assertTrue(m.notice)
    def test_clock_record_failure_and_truncation_never_gain_a_family_attestation(self):
        m=notice.Marker(clock=lambda:0);self.feed(m,b'auth CANARY\n')
        event=m.report()['events'][0]
        self.assertEqual((event['rule_id'],event['trigger_family']),('CLOCK_INVALID','NOT_APPLICABLE'))
        m=self.marker(sink=lambda row:False);self.feed(m,b'auth CANARY\n')
        self.assertTrue(m.report()['record_failed']);self.assertTrue(m.notice)
        m=self.marker();self.feed(m,b'auth CANARY');m.close()
        self.assertEqual(m.report()['classification'],'UNKNOWN')
        self.assertTrue(all(x['trigger_family']=='NOT_APPLICABLE' for x in m.report()['events']))
        self.assertTrue('CANARY' not in json.dumps(m.report()))
    def test_family_aware_dedup_still_obeys_the_original_event_cap(self):
        m=self.marker()
        for data in (b'auth CANARY\n',b'[e000099] CANARY\n',b'auth [e000099] CANARY\n'):
            self.feed(m,data)
        self.assertEqual({x['trigger_family'] for x in m.report()['events']},{'AUTH_WORD_ONLY','ECODE_PREFIX_ONLY','BOTH'})
        with patch.object(notice,'MAX_EVENTS',1):
            m=self.marker();self.feed(m,b'auth CANARY\n');self.feed(m,b'[e000099] CANARY\n')
            result=m.report();self.assertTrue(result['overflow']);self.assertTrue(result['record_failed'])
            self.assertEqual(len(result['events']),1);self.assertEqual(len(result['emergency_events']),1)
        self.assertEqual(notice.MAX_EVENTS,96)
    def test_real_backend_capture_gate_then_f7dd_cleanup_with_real_marker(self):
        with harness.load_pair() as (driver,backend):
            fixture=harness.CleanupTests(methodName='runTest');fixture.driver_mod=driver;fixture.backend_mod=backend
            b,d,c=fixture.make();b.raw_notice=[];b.marker=self.marker(sink=b.notice_event)
            root=b.roots[1]
            def read(cap,observer):
                data=b'auth synthetic CANARY\n';observer('stdout',data,1,2);return [('stdout',data)]
            root.read=read
            with self.assertRaises(harness.ContractError):d.pump()
            with self.assertRaises(harness.ContractError):d.start('ready.B')
            result=d.finish();report=b.marker.report()
            self.assertEqual(report['events'][0]['trigger_family'],'AUTH_WORD_ONLY')
            self.assertEqual(d.accepted,{'server'})
            self.assertEqual(result['failure'],'OBSERVATION_OR_RECEIPT_UNKNOWN')
            self.assertTrue(all(x['state']=='TERMINAL' and x['business_blocked'] for x in result['cleanup']))
            self.assertEqual(result['finalization']['state'],'COMPLETE')
            self.assertTrue('CANARY' not in json.dumps(b.journal.rows))
            self.assertEqual([r.stop_calls for r in b.roots],[1,1])
    def test_source_binding_bad_path_rejects_before_any_content_read(self):
        rows=[{'path':str(builder.ACTIVE/n),'bytes':1,'sha256':'a'*64}
              for n in (*builder.SOURCES,'COMMANDS_D01.json','INPUT_SCHEMAS_D01.json')]
        self.assertEqual(len(builder.checked_source_rows({'sources':rows})),17)
        rows[-1]['path']='Z:/SYNTHETIC_PRIVATE_DO_NOT_OPEN'
        with patch.object(builder,'read_safe',side_effect=AssertionError('READ_FORBIDDEN')) as reader:
            with self.assertRaises(ValueError):builder.checked_source_rows({'sources':rows})
            reader.assert_not_called()
    def test_changed_contract_buffer_is_rejected_before_compile_or_execution(self):
        with patch('builtins.compile',side_effect=AssertionError('EXEC_FORBIDDEN')) as compiler:
            with self.assertRaises(ValueError):builder.checked_contract_bytes(b'UNTRUSTED',b'QUALIFIED')
            compiler.assert_not_called()
        self.assertEqual(builder.checked_contract_bytes(b'QUALIFIED',b'QUALIFIED'),b'QUALIFIED')
    def test_original_card_pin_and_combined_metadata_reserve_are_strict(self):
        with self.assertRaises(ValueError):builder.checked_card_bytes(b'SYNTHETIC_CHANGED_CARD')
        self.assertLessEqual(builder.checked_budget(200000,5600000,100000,40)['upper_bound_bytes'],8388608)
        with self.assertRaises(ValueError):builder.checked_budget(3000000,5600000,100000,40)
        with self.assertRaises(ValueError):builder.checked_budget(0,0,0,160)
        with self.assertRaises(ValueError):builder.checked_budget(0,0,0,119)
        self.assertEqual(builder.checked_budget(0,0,0,111)['external_files'],8)
    def test_builder_relocates_only_fixed_runtime_identifiers_and_cannot_execute(self):
        sample=(str(builder.BASE).replace('\\','/')+' C84F6D21 C84F6D21_READY_60S_D01 '+builder.OLD_SCOPE).encode()
        data=builder.relocate(sample)
        self.assertTrue(b'C84F6D21' not in data);self.assertIn(builder.NONCE.encode(),data)
        self.assertIn(builder.SCOPE.encode(),data)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(builder.main(['--actual-once']),78)
        self.assertEqual(len(builder.SOURCES),15);self.assertEqual(len(builder.MAINTAINED),3)
        self.assertNotIn('goal_win32_owned',sys.modules)

if __name__=='__main__':unittest.main(verbosity=2)
