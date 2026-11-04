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
    def test_overlong_fragment_tail_is_not_a_new_record(self):
        for prefix in (b'x'*513, b'x'*1025, b'x'*4096):
            with self.subTest(prefix_bytes=len(prefix)):
                m=self.marker();self.feed(m,prefix)
                self.feed(m,b'auth_result=0\n')
                report=m.report()
                self.assertEqual(report['classification'],'UNKNOWN')
                self.assertEqual([e['rule_id'] for e in report['events']],['RECORD_TOO_LONG'])
                self.assertFalse(any(e['complete_record'] for e in report['events']))
                body=b'[E000003] denied'
                self.feed(m,body+b'\n')
                denial=[e for e in m.report()['events'] if e['rule_id']=='CODE_E000003']
                self.assertEqual(len(denial),1)
                start=len(prefix)+len(b'auth_result=0\n')
                self.assertEqual((denial[0]['offset_start'],denial[0]['offset_end']),(start,start+len(body)))
                self.assertTrue(denial[0]['complete_record'])
    def test_overlong_unterminated_record_remains_partial_at_boundaries(self):
        for boundary in ('advance','eof','close'):
            with self.subTest(boundary=boundary):
                m=self.marker();self.feed(m,b'x'*513)
                getattr(m,boundary)(*('ready.A','stdout') if boundary=='eof' else ())
                events=m.report()['events']
                self.assertTrue(any(e['rule_id']=='PARTIAL_RECORD' for e in events))
                self.assertFalse(any(e['complete_record'] for e in events))
                self.assertEqual(m.report()['classification'],'UNKNOWN')
    def test_overlong_discard_is_stream_local(self):
        m=self.marker();self.feed(m,b'x'*513)
        m.feed('ready.A','stderr',b'please authorize this device\n','MONITOR',1,2)
        self.feed(m,b'auth_result=0\n')
        events=m.report()['events']
        self.assertTrue(any(e['rule_id']=='REQUEST_SENTENCE' and e['stream']=='stderr' for e in events))
        self.assertFalse(any(e['rule_id']=='DIAG_FIELD' for e in events))
        self.assertEqual(m.report()['classification'],'UNKNOWN')
    def test_complete_overlong_row_resynchronizes_at_its_newline(self):
        for first in (b'x'*512,b'x'*513):
            m=self.marker();self.feed(m,first+b'\n[E000002] request\n')
            events=m.report()['events']
            self.assertEqual(any(e['rule_id']=='RECORD_TOO_LONG' for e in events),len(first)>notice.MAX_LINE)
            request=[e for e in events if e['rule_id']=='CODE_E000002']
            self.assertEqual(len(request),1);self.assertTrue(request[0]['complete_record'])
            self.assertEqual(request[0]['offset_start'],len(first)+1)
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
    def test_public_failure_print_chain_endings_and_every_fragment_boundary(self):
        # Official comparison source 8efffcf2 server_for_client.cpp421-442,
        # 982/1398. Public print-chain inputs, not copied private captures.
        messages=((b'[E000004]:The communication channel is being established.',
                   'CODE_E000004_CHANNEL_ESTABLISHING','HDC_CHANNEL_ESTABLISHING_FAILURE'),
                  (b'ExecuteCommand need connect-key? please confirm a device by help info',
                   'CODE_DEVICE_SELECTION_FAILURE','HDC_DEVICE_SELECTION_FAILURE'))
        for body,rule,reason in messages:
            wire=b'[Fail]'+body+b'\r\n'
            for data in (wire,wire.replace(b'\n',b'\r\n'),b'[Fail]'+body+b'\n'):
                for split in range(len(data)+1):
                    m=self.marker()
                    if data[:split]:
                        self.feed(m,data[:split])
                        if not data[:split].endswith(b'\n'):
                            self.assertEqual(m.report()['classification'],'NONE')
                    if data[split:]:self.feed(m,data[split:])
                    report=m.report();self.assertEqual(report['classification'],'COMMAND_FAILURE')
                    self.assertTrue(m.notice);self.assertEqual(m.reason(),reason)
                    self.assertEqual([(e['rule_id'],e['complete_record'],e['offset_end']) for e in report['events']],[(rule,True,len(data)-1)])
                    self.assertTrue(all(e['trigger_family']=='NOT_APPLICABLE' for e in report['events']))
    def test_known_failure_pair_keeps_later_request_denial_and_unknown_priority(self):
        pair=(b'[Fail][E000004]:The communication channel is being established.\r\r\n'
              b'Please wait for several seconds and try again.\r\r\n'
              b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info\r\r\n')
        m=self.marker();self.feed(m,pair)
        self.assertEqual(m.report()['classification'],'COMMAND_FAILURE')
        self.assertEqual(m.reason(),'HDC_CHANNEL_ESTABLISHING_FAILURE')
        self.assertEqual([e['rule_id'] for e in m.report()['events']],['CODE_E000004_CHANNEL_ESTABLISHING','CODE_DEVICE_SELECTION_FAILURE'])
        for suffix,kind,reason in ((b'[E000002] request\n','REQUEST_TEXT','AUTH_REQUEST_TEXT'),
                                   (b'[E000003] denied\n','DENIED_TEXT','AUTH_DENIED_TEXT'),
                                   (b'auth CANARY\n','UNKNOWN','NOTICE_AMBIGUOUS')):
            m=self.marker();self.feed(m,pair+suffix)
            self.assertEqual(m.report()['classification'],kind);self.assertEqual(m.reason(),reason)
            self.assertTrue(m.notice);self.assertNotIn('CANARY',json.dumps(m.report()))
    def test_known_failure_grammar_rejects_extra_characters_controls_and_partial_records(self):
        for body in (b'[Fail][E000004]:The communication channel is being established.',
                     b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info'):
            for data in (body+b'\r\r\r\n',body+b'X\r\n',body+b' \n',b' '+body+b'\n',
                         body+b'\x00\n',body[:-2]+b'\r'+body[-2:]+b'\n'):
                m=self.marker();self.feed(m,data)
                self.assertEqual(m.report()['classification'],'UNKNOWN');self.assertTrue(m.notice)
                self.assertFalse(any(e['event_type']=='COMMAND_FAILURE' for e in m.report()['events']))
            m=self.marker();self.feed(m,body+b'\r\r');m.close()
            self.assertEqual(m.report()['classification'],'UNKNOWN')
            self.assertFalse(any(e['event_type']=='COMMAND_FAILURE' for e in m.report()['events']))
        # Keep the original failing vector. Cutting the only ECODE cue does
        # not create a global notice: NONE is no notice, never business PASS.
        for body,kind in ((b'[Fail][E000004]:The communication channel is being established.','NONE'),
                          (b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info','UNKNOWN')):
            m=self.marker();self.feed(m,body[:10]+b'\r'+body[10:]+b'\n')
            self.assertEqual(m.report()['classification'],kind)
            self.assertFalse(any(e['event_type']=='COMMAND_FAILURE' for e in m.report()['events']))
            if kind=='NONE':self.assertFalse(m.notice);self.assertEqual(m.report()['events'],[])
    def test_known_failure_backend_still_blocks_B_and_preserves_cleanup_facts(self):
        for data,reason in ((b'[Fail][E000004]:The communication channel is being established.\r\r\n','HDC_CHANNEL_ESTABLISHING_FAILURE'),
                            (b'[Fail]ExecuteCommand need connect-key? please confirm a device by help info\r\r\n','HDC_DEVICE_SELECTION_FAILURE')):
            with harness.load_pair() as (driver,backend):
                fixture=harness.CleanupTests(methodName='runTest');fixture.driver_mod=driver;fixture.backend_mod=backend
                b,d,c=fixture.make();b.raw_notice=[];b.marker=self.marker(sink=b.notice_event)
                def read(cap,observer):
                    observer('stdout',data,1,2);return [('stdout',data)]
                b.roots[1].read=read
                with self.assertRaisesRegex(harness.ContractError,reason):d.pump()
                with self.assertRaises(harness.ContractError):d.start('ready.B')
                result=d.finish();self.assertEqual(d.accepted,{'server'})
                self.assertTrue(all(x['state']=='TERMINAL' and x['business_blocked'] for x in result['cleanup']))
                self.assertEqual(result['finalization']['state'],'COMPLETE')
                self.assertNotIn('NOTICE_OBSERVATION_UNRELIABLE',json.dumps(result))
                self.assertEqual([r.stop_calls for r in b.roots],[1,1])
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
