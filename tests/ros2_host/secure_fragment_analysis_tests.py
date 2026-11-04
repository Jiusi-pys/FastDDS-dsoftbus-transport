"""Counterexamples for the strict current-scene checker, not historical repair."""
import copy
import unittest
from unittest.mock import patch

from check_secure_fragment_scene import check
from analyze_ros2 import analyze


RUN = '1234567890abcdef'
PIDS = {'A': 101, 'B': 202}


def fixture():
    """Small synthetic geometry checks derivation; never asserts an old run's SN."""
    texts, errors, configs = {}, {}, {}
    for side, peer in (('A', 'B'), ('B', 'A')):
        pid = PIDS[side]
        lines = [f'ROS_MAIN run={RUN} side={side} pid={pid} mode=dual']
        for phase in ('reliable', 'best_effort'):
            gid = ('a' if side == 'A' else 'b') * 30 + ('01' if phase == 'reliable' else '02')
            remote = ('a' if peer == 'A' else 'b') * 30 + ('01' if phase == 'reliable' else '02')
            lines += [f'ROS_PUBLISHER run={RUN} side={side} gid={gid} intra_process=0 phase={phase} topic=/fdds_{RUN}/{phase}/from_{side}',
                      f'ROS_GRAPH run={RUN} side={side} peer=/fdds_{RUN}/transport_{peer} visible=1 subscription_count=1 phase={phase}']
            for ordinal in range(1, 6):
                lines += [f'ROS_WRITE run={RUN} side={side} ordinal={ordinal} payload={RUN}:{phase}:{side}:{ordinal} phase={phase} topic=/fdds_{RUN}/{phase}/from_{side}',
                          f'ROS_TAKE run={RUN} side={side} ordinal={ordinal} gid={remote} payload={RUN}:{phase}:{peer}:{ordinal} phase={phase} topic={phase}/from_{peer} publication_sequence={ordinal+4}']
            lines += [f'ROS_TARGET_WRITE run={RUN} side={side} pid={pid} phase={phase} ordinal=5 bytes=31 payload_hash={100 if side == "A" else 200} hash_scope=DIAGNOSTIC_VALUE_ONLY',
                      f'ROS_TARGET_TAKE run={RUN} side={side} pid={pid} phase={phase} ordinal=5 bytes=31 publication_sequence=9 payload_hash={100 if peer == "A" else 200} hash_scope=DIAGNOSTIC_VALUE_ONLY',
                      f'ROS_LEDGER_STATE run={RUN} side={side} pid={pid} phase={phase} callbacks=5 invalid_callbacks=0 bad=0 accepted=5 accepted_mask=31 target5_accepted=1 service_done={1 if side == "A" else 0} requests={0 if side == "A" else 1} source=ACTUAL_APPLICATION_VALUES',
                      f'ROS_BUSINESS_DONE run={RUN} side={side} writes=5 takes=5 phase={phase}']
            if side == 'A':
                lines += [f'ROS_SERVICE_SENT run={RUN} phase={phase} request_id=1 a=3 b=4',
                          f'ROS_SERVICE_RESPONSE run={RUN} phase={phase} request_id=1 sum=7']
            else:
                lines += [f'ROS_SERVICE_REQUEST run={RUN} phase={phase} request_id=1 a=3 b=4 sum=7 writer={"c"*32}']
        lines += [f'ROS_CONTEXT_CLOSED run={RUN} side={side} result=0 result_scope=APPLICATION_EXIT_STATUS shutdown_return=1',
                  f'ROS_NATIVE_SUMMARY run={RUN} side={side} send_enter=22 send_return=22 send_error=0 ingress=22 detailed_stored=44 metadata_limit=8192 overflow=0 malformed=0 sdk_epoch=NOT_PROVEN',
                  f'ROS_RECEIVE_DECISION_SUMMARY run={RUN} side={side} total=40 refused=0 counter_overflow=0 unknown_branch=0 unknown_reason=0 capture_dropped=2 scope=ORIGINAL_BINDING_AND_ADAPTER_DECISIONS final_host_threads_joined=1 sdk_epoch=NOT_PROVEN',
                  f'ROS_SEND_ADMISSION_SUMMARY run={RUN} side={side} total=22 refused=0 structural_overflow=0 counter_overflow=0 scope=ORIGINAL_LOCAL_SUBMISSION native_entry=NOT_INFERRED final_host_threads_joined=1']
        direction = 'SDK_INGRESS' if side == 'A' else 'SDK_SEND'
        for first, payload in ((1, 17), (2, 17), (3, 6)):
            lines.append(f'ROS_NATIVE_FRAGMENT run={RUN} side={side} observation={first+90} direction={direction} socket=2 writer={"b"*30}02 sequence_hi=0 sequence_lo=9 scope=STRUCTURAL_APP_OBSERVER first={first} count=1 fragment_size=17 sample_size=40 payload_bounds_known=1 key_flag=0 payload_bytes={payload}')
        texts[side] = '\n'.join(lines) + '\n'
        errors[side] = '\n'.join([
            'FDDST_SECURITY_LOG category=SECURITY message=Initialized security manager',
            'FDDST_SECURITY_LOG category=SECURITY message=Participant authenticated',
            'FDDST_PAYLOAD_POOL_BOUND scope=1 pool=01 domain=02 M=98304 B=2097152 N=64 config=0123456789abcdef profile=TEST_ONLY_BOUNDED',
            f'FDDST_HOST_FRAGMENT_CAPTURE run={RUN} side={side} flush_return=1 exception=0 output_error=0 complete=1 profile=TEST_ONLY_BOUNDED scope=STRUCTURAL_APP_OBSERVER',
            'FDDST_HOST_CLEANUP threads_joined=1 retained_owner_metadata=3 retained_id_metadata=3 callbacks=0 receives=0 send_entered=0 attempts=22 calls=22 returns=22',
            f'FDDST_ROS_CLOSED run={RUN} side={"AB".index(side)} pump_joined=1 runtime_closed=1 local_only=1']) + '\n'
        configs[side] = {'run': RUN, 'side': str('AB'.index(side)), 'backend': 'HOST_OPAQUE_BYTES_TEST_ONLY',
                        'host_fragment_observation': '1', 'payload_guard_profile': 'TEST_ONLY_BOUNDED',
                        'payload_max_sample_bytes': '98304', 'payload_pool_budget_bytes': '2097152',
                        'payload_max_allocations': '64'}
    scene = {'run': RUN, 'mode': 'dual', 'forward': '1', 'timed_out': False, 'pass': True,
             'processes': {s: {'pid': PIDS[s], 'exit_code': 0} for s in 'AB'}}
    scene['ledger'] = analyze(texts, RUN, ('reliable', 'best_effort'))
    return scene, texts, errors, configs


def refresh(inputs):
    inputs[0]['ledger'] = analyze(inputs[1], RUN, ('reliable', 'best_effort'))


class StrictFragmentScene(unittest.TestCase):
    def test_complete_positive_derives_nonproduction_geometry_and_sequence(self):
        outcome = check(*fixture())
        self.assertTrue(outcome['passed'], outcome)
        self.assertEqual(outcome['facts']['current_target']['sequence_lo'], 9)
        self.assertEqual(outcome['facts']['current_target']['wire']['SDK_SEND']['derived_fragment_count'], 3)
        self.assertEqual(outcome['facts']['diagnostic_layers']['A']['receive']['capture_dropped'], '2')

    def failed(self, change, expected_check=None):
        inputs = copy.deepcopy(fixture())
        change(inputs)
        outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        if expected_check:
            self.assertFalse(outcome['checks'][expected_check], outcome)

    def test_missing_final_ingress_fragment_is_not_complete(self):
        self.failed(lambda i: i[1].update(A='\n'.join(line for line in i[1]['A'].splitlines()
                     if not (line.startswith('ROS_NATIVE_FRAGMENT ') and 'first=3 ' in line))),
                    'target_ingress_range_complete')

    def test_nonzero_entered_native_error_cannot_be_erased(self):
        self.failed(lambda i: i[1].update(B=i[1]['B'].replace('send_error=0', 'send_error=1')),
                    'native_calls_balanced_B')

    def test_latched_caller_failure_cannot_be_hidden_by_native_summary(self):
        self.failed(lambda i: i[2].update(A=i[2]['A'] + 'FDDST_PUMP_ERROR_OBSERVED\n'),
                    'final_pump_and_runtime_close_A')

    def test_incomplete_capture_is_not_absence_evidence(self):
        self.failed(lambda i: i[2].update(A=i[2]['A'].replace('complete=1', 'complete=0')),
                    'fragment_capture_complete_A')

    def test_missing_actual_callback_ledger_cannot_use_text_takes_only(self):
        self.failed(lambda i: i[1].update(A='\n'.join(line for line in i[1]['A'].splitlines()
                     if not line.startswith('ROS_LEDGER_STATE '))))

    def test_bad_actual_callback_fails_even_with_twenty_text_receipts(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('bad=0', 'bad=1')),
                    'actual_callback_ledger_A_best_effort')

    def test_wrong_payload_value_hash(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('payload_hash=200', 'payload_hash=201')),
                    'validated_payload_value_matches')

    def test_diagnostic_and_wire_sn_cannot_replace_actual_business_take_sn(self):
        inputs = fixture()
        for side in 'AB':
            altered = []
            for line in inputs[1][side].splitlines():
                if side == 'A' and line.startswith('ROS_TARGET_TAKE ') and 'phase=best_effort' in line:
                    line = line.replace('publication_sequence=9 ', 'publication_sequence=10 ')
                if line.startswith('ROS_NATIVE_FRAGMENT '):
                    line = line.replace('sequence_lo=9 ', 'sequence_lo=10 ')
                altered.append(line)
            inputs[1][side] = '\n'.join(altered) + '\n'
        outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'target_business_receipt_identity_mismatch')
        self.assertNotIn('current_target', outcome['facts'])

    def test_cross_attempt_pid_rejected(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('pid=101 phase=best_effort ordinal=5',
                                                            'pid=999 phase=best_effort ordinal=5')),
                    'identity_A_ROS_TARGET_TAKE')

    def test_cross_run_fragment_rejected(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('ROS_NATIVE_FRAGMENT run='+RUN,
                                    'ROS_NATIVE_FRAGMENT run=ffffffffffffffff')),
                    'identity_A_ROS_NATIVE_FRAGMENT')

    def test_inconsistent_fragment_geometry_rejected(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('first=3 count=1 fragment_size=17',
                                                            'first=3 count=1 fragment_size=18')))

    def test_payload_fragment_shorter_than_declared_range_rejected(self):
        self.failed(lambda i: i[1].update(A=i[1]['A'].replace('payload_bytes=6', 'payload_bytes=5')))

    def test_oversized_geometry_is_rejected_before_any_range_expansion(self):
        inputs = fixture()
        inputs[1]['B'] = inputs[1]['B'].replace('sample_size=40', 'sample_size=4294967295')
        with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
            outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'sample_exceeds_actual_config')

    def test_invalid_huge_config_cannot_expand_geometry_bound(self):
        inputs = fixture()
        inputs[3]['A']['payload_max_sample_bytes'] = '4294967295'
        with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
            outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'invalid_actual_payload_config:A')

    def test_changed_valid_config_cannot_replace_frozen_scene_contract(self):
        inputs = fixture()
        inputs[3]['A']['payload_max_sample_bytes'] = '1000000'
        with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
            outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'frozen_scene_payload_contract_mismatch:A')

    def test_actual_pool_must_match_frozen_configuration_before_expansion(self):
        inputs = fixture()
        inputs[2]['A'] = inputs[2]['A'].replace('M=98304 ', 'M=1000000 ')
        with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
            outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'actual_pool_configuration_mismatch:A')

    def test_project_valid_huge_config_and_matched_pool_never_expand(self):
        inputs = fixture()
        for side in 'AB':
            inputs[3][side].update(payload_max_sample_bytes='4294901760',
                                   payload_pool_budget_bytes='4294901760')
            inputs[2][side] = inputs[2][side].replace('M=98304 B=2097152 ',
                                                     'M=4294901760 B=4294901760 ')
            inputs[1][side] = inputs[1][side].replace('sample_size=40', 'sample_size=4294901760')
        with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
            outcome = check(*inputs)
        self.assertFalse(outcome['passed'], outcome)
        self.assertEqual(outcome['parse_error'], 'frozen_scene_payload_contract_mismatch:A')

    def test_each_matched_valid_limit_change_is_outside_scoped_contract(self):
        for key, original, replacement in (
            ('payload_max_sample_bytes', 'M=98304 ', 'M=98308 '),
            ('payload_pool_budget_bytes', 'B=2097152 ', 'B=4194304 '),
            ('payload_max_allocations', 'N=64 ', 'N=65 ')):
            with self.subTest(key=key):
                inputs = fixture()
                inputs[3]['A'][key] = replacement.split('=', 1)[1].strip()
                inputs[2]['A'] = inputs[2]['A'].replace(original, replacement)
                with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
                    outcome = check(*inputs)
                self.assertFalse(outcome['passed'], outcome)
                self.assertEqual(outcome['parse_error'], 'frozen_scene_payload_contract_mismatch:A')

    def test_wire_width_violations_do_not_expand_any_range(self):
        for old, new, error in (
            ('sample_size=40', 'sample_size=4294967296', 'fragment_geometry_wire_width'),
            ('fragment_size=17', 'fragment_size=65536', 'fragment_geometry_wire_width'),
            ('first=1 ', 'first=4294967296 ', 'fragment_range_wire_width'),
            ('count=1 ', 'count=65536 ', 'fragment_range_wire_width')):
            with self.subTest(field=new):
                inputs = fixture()
                inputs[1]['B'] = inputs[1]['B'].replace(old, new)
                with patch('check_secure_fragment_scene.range', side_effect=AssertionError('range_expanded'), create=True):
                    outcome = check(*inputs)
                self.assertFalse(outcome['passed'], outcome)
                self.assertEqual(outcome['parse_error'], error)

    def test_stale_stored_result_cannot_override_original_business(self):
        def change(i):
            i[1]['A'] = '\n'.join(line for line in i[1]['A'].splitlines()
                                  if not (line.startswith('ROS_TAKE ') and 'phase=best_effort' in line and 'ordinal=5 ' in line))
        self.failed(change, 'stored_business_matches_originals')

    def test_safe_cleanup_is_not_business_delivery(self):
        self.failed(lambda i: i[0].update({'pass': False}), 'business_verdict')

    def test_drop_last_expected_negative_preserves_raw_business_fail(self):
        inputs = fixture()
        scene, texts, _, configs = inputs
        texts['A'] = '\n'.join(line for line in texts['A'].splitlines() if not (
            (line.startswith('ROS_NATIVE_FRAGMENT ') and 'first=3 ' in line) or
            (line.startswith(('ROS_TAKE ', 'ROS_TARGET_TAKE ')) and 'ordinal=5 ' in line and 'phase=best_effort' in line) or
            (line.startswith('ROS_BUSINESS_DONE ') and 'phase=best_effort' in line))) + '\n'
        texts['A'] = texts['A'].replace('phase=best_effort callbacks=5 invalid_callbacks=0 bad=0 accepted=5 accepted_mask=31 target5_accepted=1',
            'phase=best_effort callbacks=4 invalid_callbacks=0 bad=0 accepted=4 accepted_mask=15 target5_accepted=0')
        texts['A'] += f'ROS_FAILURE run={RUN} side=A pid=101 stage=LEDGER reason=SAMPLE_SERVICE_INCOMPLETE\n'
        texts['B'] += f'HOST_FRAGMENT_DROP schema=1 run={RUN} side=B pid=202 original_socket=2 writer={"b"*30}02 sequence_hi=0 sequence_lo=9 first=3 count=1 fragment_size=17 sample_size=40 bytes=80 api_called=1 actual_host_write=0 native_result=0 expected_negative=1 drops=1 identity=ORIGINAL_BORROWED_ARGS\n'
        for side in 'AB':
            texts[side] = texts[side].replace('result=0 result_scope', 'result=4 result_scope')
            scene['processes'][side]['exit_code'] = 4
        scene.update({'pass': False, 'cleanup_sent_after_failure': True})
        scene['fragment_control'] = {'mode': 'drop-last', 'state': 'DROP_ARMED',
                                    'scope': 'TEST_ONLY_NO_ACK_NO_RETRANSMISSION'}
        configs['B']['host_fragment_control'] = 'B-fragment-control'
        refresh(inputs)
        outcome = check(*inputs, expected='drop-last')
        self.assertTrue(outcome['passed'], outcome)
        self.assertFalse(outcome['raw_scene_pass'])
        self.assertEqual(outcome['raw_child_exit'], {'A': 4, 'B': 4})
        self.assertFalse(check(*inputs)['passed'])

    def test_late_release_uses_original_validated_callback_and_two_sided_done(self):
        inputs = fixture()
        scene, texts, _, _ = inputs
        anchor = f'ROS_TARGET_TAKE run={RUN} side=A pid=101 phase=best_effort'
        before = (f'ROS_CALLBACK_VALIDATED_PENDING run={RUN} side=A pid=101 phase=best_effort ordinal=5 gid={"b"*30}02 publication_sequence=9 bytes=31 payload_hash=200 exact=1 source=ORIGINAL_VALIDATED_RMW_CALLBACK\n'
                  f'ROS_TARGET_LEDGER_HELD run={RUN} side=A pid=101 phase=best_effort ordinal=5 accepted_before=4 callbacks=5 publication_sequence=9 payload_validated=1 retained_payload=0\n')
        texts['A'] = texts['A'].replace(anchor, before + anchor)
        scene['fragment_control'] = {'mode': 'late-release', 'state': 'RELEASED',
            'scope': 'TEST_ONLY_NO_ACK_NO_RETRANSMISSION', 'events': [
                {'event': 'TARGET_LEDGER_RELEASE', 'parent_monotonic_ns': 10,
                 'A_ledger_held': True, 'B_done_observed': True, 'no_early_completion_or_cleanup': True},
                {'event': 'DONE_RELEASE', 'parent_monotonic_ns': 11, 'A_done_observed': True, 'B_done_observed': True}]}
        outcome = check(*inputs, expected='late-release')
        self.assertTrue(outcome['passed'], outcome)
        scene['fragment_control']['events'][0]['no_early_completion_or_cleanup'] = False
        self.assertFalse(check(*inputs, expected='late-release')['passed'])


if __name__ == '__main__':
    unittest.main()
