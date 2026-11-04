"""Host fixtures and mocks only; never evidence of device DSoftBus execution."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_action_restart as OLD
import action_natural_restart_oracle as O
import run_action_natural_restart as H

spec = importlib.util.spec_from_file_location('natural_restart_target', H.PROBE)
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
SCENE, RUNS, FILES = OLD.SCENE, OLD.RUNS, OLD.FILES


def payload(event, phase, index=1, unknown=False):
    row = OLD.payload(event, phase, index)
    if event == 'SERVER_GOAL':
        row['status'] = 2
    if event.endswith('_RESULT') and phase == 'OLD' and not unknown:
        values = O.sequence(7)
        raw = b'\0\1\0\0' + struct.pack('<I', len(values)) + struct.pack('<' + 'i' * len(values), *values)
        row.update(status=4, values=values, cdr_base64=base64.b64encode(raw).decode(),
                   cdr_bytes=len(raw), cdr_sha256=hashlib.sha256(raw).hexdigest())
    return row


def resequence(records):
    for rows in records.values():
        for index, row in enumerate(rows, 1):
            row.update(seq=index, monotonic_ns=index)
    return records


def fixtures(old='SUCCEEDED', client_status=False):
    records = OLD.fixtures()
    remove = {'CONTROL_ACCEPTED', 'OLD_PENDING', 'OLD_EXECUTING', 'OLD_HELD', 'OLD_SERVER_ABANDON',
              'OLD_LOCAL_DISPOSE', 'OLD_FINAL_STATE'}
    for key, original in records.items():
        out = []
        for row in original:
            event, phase = row['event'], row.get('phase')
            if event in remove or (event == 'CLIENT_STATUS' and not client_status):
                continue
            row = copy.deepcopy(row)
            if event in ('ENTITY_READY', 'ACTION_READY', 'ACTION_UNAVAILABLE'):
                row['client_object_id'] = 12345 if key == 'A' else 0
            if event == 'ACTION_UNAVAILABLE':
                row['old_done'] = old != 'PENDING'
            if event == 'SERVER_GOAL':
                row['status'] = 2
            if phase == 'OLD' and event.endswith('_FEEDBACK'):
                for index in range(1, 7):
                    out.append(dict(row, **payload(event, 'OLD', index)))
                if key == 'B0':
                    out.append(dict(row, event='SERVER_RESULT', **payload('SERVER_RESULT', 'OLD')))
                continue
            if event == 'CLIENT_FEEDBACK_COMPLETE' and phase == 'OLD':
                row['count'] = 6
                out.append(row)
                if old != 'PENDING':
                    out.append(dict(row, event='CLIENT_RESULT', **payload('CLIENT_RESULT', 'OLD', unknown=old == 'UNKNOWN_EMPTY')))
                continue
            if event == 'PHASE_COMPLETE' and key == 'B0':
                row['phase'] = 'OLD_FINITE'
            out.append(row)
            if event == 'FINISH_ACCEPTED':
                if key == 'A':
                    out.append(dict(row, event='OLD_FINAL_STATE', done=old != 'PENDING',
                                    cancelled=False, response_observed=old != 'PENDING'))
                    if old == 'PENDING':
                        out.append(dict(row, event='OLD_LOCAL_DISPOSE', cancelled=True, remote_cancel=False))
                if key == 'B0':
                    out.append(dict(row, event='OLD_SERVER_FINAL', status=4, result_set=True, remote_cancel=False))
        records[key] = out
    return resequence(records)


def parse(records):
    return {key: O.rows('\n'.join('R25_PROBE ' + json.dumps(r) for r in rows), SCENE, key, RUNS[key],
                       100 + O.INSTANCES.index(key)) for key, rows in records.items()}


class OracleTests(unittest.TestCase):
    def test_three_old_outcomes_are_separate_from_new_success(self):
        for outcome in ('SUCCEEDED', 'PENDING', 'UNKNOWN_EMPTY'):
            with self.subTest(outcome=outcome):
                result = O.all_content(parse(fixtures(outcome)), SCENE, FILES)
                self.assertEqual(result['old']['client_outcome'], outcome)
                self.assertEqual(result['old_goal_recovery'], 'NOT_CLAIMED')
                self.assertEqual((result['pre']['feedback_count'], result['new']['feedback_count']), (5, 7))
                self.assertTrue(result['new_goal_success_after_replacement'])

    def test_intermediate_client_status_is_optional(self):
        for observed in (False, True):
            records = fixtures(client_status=observed)
            for row in O.select(records['A'], 'CLIENT_STATUS'):
                row['status'] = 0
            self.assertTrue(O.all_content(parse(records), SCENE, FILES)['new_goal_success_after_replacement'])

    def test_missing_feedback_result_and_readiness_rejected(self):
        for event, phase in [('CLIENT_FEEDBACK', 'NEW'), ('CLIENT_RESULT', 'NEW'),
                             ('CLIENT_FEEDBACK', 'PRE'), ('ACTION_UNAVAILABLE', None)]:
            records = fixtures()
            records['A'].remove(O.select(records['A'], event, phase)[0])
            with self.subTest(event=event, phase=phase), self.assertRaises(ValueError):
                O.all_content(parse(resequence(records)), SCENE, FILES)

    def test_old_result_cannot_substitute_new(self):
        records = fixtures()
        new = O.one(records['A'], 'CLIENT_RESULT', 'NEW')
        new.update(payload('CLIENT_RESULT', 'OLD'))
        with self.assertRaises(ValueError):
            O.all_content(parse(records), SCENE, FILES)

    def test_old_same_process_causal_order_required(self):
        for key, moving, before in [('B0', 'SERVER_FEEDBACK', 'SERVER_GOAL'),
                                    ('B0', 'SERVER_RESULT', 'SERVER_FEEDBACK'),
                                    ('A', 'CLIENT_FEEDBACK', 'CLIENT_GOAL')]:
            records = fixtures()
            row = O.select(records[key], moving, 'OLD')[0]
            anchor = O.select(records[key], before, 'OLD')[0]
            records[key].remove(row)
            records[key].insert(records[key].index(anchor), row)
            with self.subTest(key=key, moving=moving), self.assertRaises(ValueError):
                O.all_content(parse(resequence(records)), SCENE, FILES)

    def test_old_late_feedback_remains_an_observation(self):
        records = fixtures()
        feedback = O.select(records['A'], 'CLIENT_FEEDBACK', 'OLD')[-1]
        records['A'].remove(feedback)
        result = O.one(records['A'], 'CLIENT_RESULT', 'OLD')
        records['A'].insert(records['A'].index(result) + 1, feedback)
        self.assertEqual(O.all_content(parse(resequence(records)), SCENE, FILES)['old']['client_outcome'], 'SUCCEEDED')

    def test_payload_uuid_duplicate_and_client_replacement_rejected(self):
        changes = [('CLIENT_GOAL', 'NEW', 'uuid', O.identity(SCENE, 'OLD')[0]),
                   ('CLIENT_FEEDBACK', 'NEW', 'index', 2), ('CLIENT_RESULT', 'NEW', 'status', 5),
                   ('CLIENT_FEEDBACK', 'PRE', 'cdr_bytes', True), ('CLIENT_ACCEPTED', 'OLD', 'accepted', False),
                   ('ACTION_READY', 'NEW', 'client_object_id', 9876)]
        for event, phase, key, value in changes:
            records = fixtures()
            O.select(records['A'], event, phase)[0][key] = value
            with self.subTest(event=event, key=key), self.assertRaises(ValueError):
                O.all_content(parse(records), SCENE, FILES)

    def test_business_permission_and_foreign_role_rejected(self):
        for event in ('CONTROL_ACCEPTED', 'OLD_PENDING', 'OLD_HELD', 'SERVER_GOAL'):
            records = fixtures()
            records['A'].insert(4, dict(records['A'][3], event=event))
            with self.subTest(event=event), self.assertRaises(ValueError):
                O.all_content(parse(resequence(records)), SCENE, FILES)

    def test_teardown_and_remote_cancel_not_forged(self):
        for event, field, value in [('SHUTDOWN_RETURN', 'returned', False), ('OLD_SERVER_FINAL', 'remote_cancel', True),
                                     ('OLD_SERVER_FINAL', 'result_set', False)]:
            records = fixtures()
            O.one(records['B0'], event)[field] = value
            with self.subTest(event=event), self.assertRaises(ValueError):
                O.all_content(parse(records), SCENE, FILES)


class ProbeTests(unittest.TestCase):
    def client(self):
        p = P.NaturalActionRestartProbe.__new__(P.NaturalActionRestartProbe)
        p.goals, p.results, p.handles, p.statuses, p.received = {}, {}, {}, {}, set()
        p.unavailable = p.rediscovered = False
        p.client = SimpleNamespace(server_is_ready=Mock(return_value=True))
        p.responses, p.emit = Mock(), Mock()
        p.send = Mock(side_effect=lambda phase: p.goals.update({phase: object()}))
        return p

    def test_client_progresses_only_from_local_result_and_readiness(self):
        p = self.client()
        p.client_step()
        self.assertEqual([c.args[0] for c in p.send.call_args_list], ['PRE'])
        p.received.add('PRE')
        p.client_step()
        self.assertEqual([c.args[0] for c in p.send.call_args_list], ['PRE', 'OLD'])
        p.results['OLD'] = SimpleNamespace(done=lambda: True)
        p.client.server_is_ready.return_value = False
        p.client_step()
        self.assertTrue(p.unavailable)
        p.client.server_is_ready.return_value = True
        p.client_step()
        p.client_step()
        self.assertEqual([c.args[0] for c in p.send.call_args_list], ['PRE', 'OLD', 'NEW'])
        ids = [c.kwargs['client_object_id'] for c in p.emit.call_args_list]
        self.assertEqual(ids, [id(p.client)] * 3)

    def test_no_new_without_local_disappearance(self):
        p = self.client()
        p.goals = {'PRE': object(), 'OLD': object()}
        p.received.add('PRE')
        p.results['OLD'] = SimpleNamespace(done=lambda: False)
        p.client_step()
        p.send.assert_not_called()

    def test_all_business_permission_calls_fail(self):
        p = self.client()
        for phase in ('OLD', 'POST', 'START_OLD', 'COMPLETE_PRE', 'COMPLETE_NEW', 'END'):
            with self.subTest(phase=phase), self.assertRaises(RuntimeError):
                p.control(phase)

    def test_server_uses_finite_work_and_public_success(self):
        p = P.NaturalActionRestartProbe.__new__(P.NaturalActionRestartProbe)
        p.args = SimpleNamespace(scenario=SCENE, instance='B1')
        p.handles, p.feedback, p.completed, p.deadline = {}, {}, set(), 100
        p.content = Mock()
        handle = SimpleNamespace(goal_id=SimpleNamespace(uuid=P.identity(SCENE, 'NEW')[0]),
                                 request=SimpleNamespace(order=8), status=2, publish_feedback=Mock())
        handle.succeed = Mock(side_effect=lambda: setattr(handle, 'status', 4))
        action = SimpleNamespace(Fibonacci=SimpleNamespace(Feedback=lambda **k: SimpleNamespace(**k),
                                                           Result=lambda **k: SimpleNamespace(**k)))
        with patch.dict(sys.modules, {'example_interfaces.action': action}), patch.object(P.time, 'monotonic', return_value=0), \
                patch.object(P.time, 'sleep') as sleep:
            result = p.execute_goal(handle)
        self.assertEqual(result.sequence, O.sequence(8))
        self.assertEqual([c.args for c in sleep.call_args_list], [(0.2,)] * 7)
        self.assertEqual(handle.publish_feedback.call_count, 7)
        handle.succeed.assert_called_once_with()
        self.assertEqual(p.completed, {'NEW'})


class AdmissionTests(unittest.TestCase):
    def test_route_sdk_exception_is_exact_path_and_hash_only(self):
        with tempfile.TemporaryDirectory() as directory:
            sdk, other = Path(directory) / 'libc++_shared.so', Path(directory) / 'other.so'
            sdk.write_bytes(b'fixed query product')
            other.write_bytes(sdk.read_bytes())
            digest = H.sha(sdk)
            with patch.object(H, 'SDK_LIBCXX', sdk), patch.object(H, 'SDK_LIBCXX_SHA', digest), \
                    patch.object(H.BASE, 'runtime_path', side_effect=ValueError('Outside project evidence')) as boundary:
                self.assertEqual(H.route_binding(sdk, digest), sdk.resolve())
                boundary.assert_not_called()
                with self.assertRaises(ValueError):
                    H.route_binding(sdk, '0' * 64)
                with self.assertRaises(ValueError):
                    H.route_binding(other, digest)
                sdk.write_bytes(b'changed product')
                with self.assertRaises(ValueError):
                    H.route_binding(sdk, digest)

    def test_output_overlap_and_wrong_shape_rejected(self):
        for path in (H.REPO, H.EVIDENCE, H.EVIDENCE / 'execution-invalid',
                     H.EVIDENCE / 'nested/execution-1234567890abcdef'):
            with self.subTest(path=str(path)), self.assertRaises((ValueError, OSError)):
                H.fresh(path, 'execution')

    def test_only_lifecycle_controls_reach_existing_native_writer(self):
        p = H.NaturalActionProcess.__new__(H.NaturalActionProcess)
        for instance in ('A', 'B0', 'B1'):
            p.instance = instance
            for control in ('ARM', 'REPLACE', 'POST', 'OLD', 'START_OLD', 'COMPLETE_NEW', 'END'):
                with patch.object(H.P.RestartProcess, 'extra_control') as writer:
                    if instance == 'A' and control in ('ARM', 'REPLACE'):
                        p.extra_control(control)
                        writer.assert_called_once_with(control)
                    else:
                        with self.assertRaises(ValueError):
                            p.extra_control(control)
                        writer.assert_not_called()

    def test_config_remote_hash_uses_exact_inherited_local_file(self):
        with tempfile.TemporaryDirectory() as directory:
            p = H.NaturalActionProcess.__new__(H.NaturalActionProcess)
            p.output, p.prefix, p.write = Path(directory), {'A': '/data/local/tmp/test'}, Mock()
            config = p.output / 'A.private.conf'
            config.write_text('observed configuration')
            p.shell = Mock(return_value=(H.sha(config) + '  /data/local/tmp/test/ros.conf\n').encode())
            with patch.object(H.P.RestartProcess, 'configure') as original:
                p.configure('A')
                original.assert_called_once_with('A')
            p.shell.return_value = b'wrong /data/local/tmp/test/ros.conf'
            with patch.object(H.P.RestartProcess, 'configure'), self.assertRaises(ValueError):
                p.configure('A')

    def test_qualification_rejects_initial_old_or_device_receipts(self):
        for row in ({'status': 'PASS_INITIAL_HOST'}, {'schema': 1, 'status': 'PASS_FINAL_HOST', 'scope': H.SCOPE,
                     'device_tested': True, 'package': {}}, {'schema': 1, 'status': 'PASS_FINAL_HOST', 'scope': 'R30'}):
            with patch.object(H.BASE, 'runtime_path', side_effect=lambda p, *a: Path(p)), \
                    patch.object(H, 'read', return_value=row), self.assertRaises(ValueError):
                H.qualification('RESULT.json', 'DERIVATION.json', {})


if __name__ == '__main__':
    unittest.main()
