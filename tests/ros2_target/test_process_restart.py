import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import process_restart_oracle as O
import run_process_restart as H

spec = importlib.util.spec_from_file_location('restart_probe', HERE.parents[1] / 'integrations/ros2/target/process_restart_probe.py')
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
SCENE = '1234567890abcdef'
RUNS = {'A': 'aaaaaaaaaaaaaaaa', 'B0': 'bbbbbbbbbbbbbbbb', 'B1': 'cccccccccccccccc'}


def transcript(instance):
    out = []
    def emit(event, **fields):
        out.append({'schema': 1, 'scenario': SCENE, 'instance': instance, 'run': RUNS[instance],
                    'side': instance[0], 'pid': 100 if instance == 'A' else 200 if instance == 'B0' else 300,
                    'role': 'requester' if instance == 'A' else 'responder', 'seq': len(out) + 1,
                    'monotonic_ns': (len(out) + 1) * 100_000_000, 'event': event, **fields})
    emit('NODE_READY')
    emit('ENTITY_READY')
    numbers = range(10) if instance == 'A' else range(5) if instance == 'B0' else range(5, 10)
    for n in numbers:
        if n == 5 and instance == 'A':
            emit('POST_ACCEPTED')
        text = 'r27_' + SCENE + '_' + str(n).zfill(2)
        data = b'\x00\x01\x00\x00' + struct.pack('<I', len(text) + 1) + text.encode() + b'\0'
        for event in (('SEND', 'RECEIVE') if instance == 'A' else ('RECEIVE', 'ECHO')):
            emit(event, sequence=n, text=text, cdr_base64=base64.b64encode(data).decode(),
                 cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data))
        if n in (4, 9):
            emit('PHASE_COMPLETE', phase='PRE' if n == 4 else 'POST', received=n + 1)
    emit('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
    emit('NODE_DESTROY_RETURN', returned=True)
    emit('SHUTDOWN_RETURN', returned=True)
    emit('FINAL', success=True, node_created=True)
    return out


def text(records):
    return '\n'.join('R25_PROBE ' + json.dumps(row) for row in records)


class LedgerTests(unittest.TestCase):
    def test_valid_ten_and_two_distinct_five(self):
        a, b0, b1 = (P.RestartLedger(SCENE, k) for k in ('A', 'B0', 'B1'))
        for n in range(10):
            message = a.send()
            self.assertEqual((b0 if n < 5 else b1).receive(message), n)
            self.assertEqual(a.receive(message), n)
        with self.assertRaises(ValueError):
            a.send()

    def test_no_retry_duplicate_stale_or_unrequested_reply(self):
        for instance in ('A', 'B0', 'B1'):
            ledger = P.RestartLedger(SCENE, instance)
            with self.assertRaises(ValueError):
                ledger.receive('foreign')
        a = P.RestartLedger(SCENE, 'A')
        with self.assertRaises(ValueError):
            a.receive(a.payload(0))
        first = a.send()
        with self.assertRaises(ValueError):
            a.send()
        a.receive(first)
        with self.assertRaises(ValueError):
            a.receive(first)
        b1 = P.RestartLedger(SCENE, 'B1')
        with self.assertRaises(ValueError):
            b1.receive(b1.payload(0))


class ContentTests(unittest.TestCase):
    def test_three_original_identities_and_content(self):
        records = {}
        for key in RUNS:
            source = transcript(key)
            records[key] = O.rows(text(source), SCENE, key, RUNS[key], source[0]['pid'])
            O.content(records[key], SCENE, key, 'ALL', final=True)
        O.pair_content(records['A'], records['B0'], SCENE, 'PRE')
        O.pair_content(records['A'], records['B1'], SCENE, 'POST')

    def test_instance_identity_cannot_be_relabeled(self):
        source = transcript('B1')
        for key, value in [('run', RUNS['B0']), ('instance', 'B0'), ('side', 'A'), ('pid', 200), ('seq', 77), ('monotonic_ns', True)]:
            with self.subTest(key=key):
                bad = copy.deepcopy(source)
                bad[3][key] = value
                with self.assertRaises(ValueError):
                    O.rows(text(bad), SCENE, 'B1', RUNS['B1'], 300)

    def test_duplicate_json_and_fail_record_rejected(self):
        source = transcript('A')
        with self.assertRaises(ValueError):
            O.rows(text(source).replace('"schema": 1', '"schema": 1, "schema": 1', 1), SCENE, 'A', RUNS['A'], 100)
        source[3]['event'] = 'FAIL'
        with self.assertRaises(ValueError):
            O.rows(text(source), SCENE, 'A', RUNS['A'], 100)

    def test_false_success_missing_phase_teardown_and_cdr(self):
        source = transcript('A')
        variants = []
        bad = copy.deepcopy(source);bad[2]['text'] = 'foreign';variants.append(bad)
        bad = copy.deepcopy(source);bad[3]['cdr_base64'] = base64.b64encode(b'bad').decode();variants.append(bad)
        bad = copy.deepcopy(source);bad[3]['sequence'] = 1;variants.append(bad)
        variants.append([r for r in source if r['event'] != 'POST_ACCEPTED'])
        variants.append([r for r in source if r['event'] != 'NODE_DESTROY_RETURN'])
        bad = copy.deepcopy(source);bad[-1]['success'] = False;variants.append(bad)
        bad = copy.deepcopy(source);bad[-4]['reason'] = 'WINDOW_FAILED';variants.append(bad)
        variants.append(source[:3] + [source[2]] + source[3:])
        for index, bad in enumerate(variants):
            with self.subTest(index=index), self.assertRaises(ValueError):
                O.content(bad, SCENE, 'A', 'ALL', final=True)


def owner_text(stage='OLD_BOTH_READY', **changes):
    row = dict(schema='1', run=RUNS['A'], side='A', stage=stage, listener='10', listener_entry='1', client_entry='2',
        old_dial='11', old_accepted='12', old_dial_owner='101', old_accepted_owner='102',
        new_dial='21' if stage == 'NEW_BOTH_ACTIVE' else '11', new_accepted='22' if stage == 'NEW_BOTH_ACTIVE' else '-1',
        new_dial_owner='201' if stage == 'NEW_BOTH_ACTIVE' else '101', new_accepted_owner='202' if stage == 'NEW_BOTH_ACTIVE' else '0',
        dial_shutdown_returned='0', accepted_shutdown_returned='0', dial_on_shutdown='1', accepted_on_shutdown='1',
        callbacks='0', receivers='0', wall_ms='2000' if stage == 'NEW_BOTH_ACTIVE' else '1000',
        source='SDK_PUBLIC_SOCKET', sdk_epoch='NOT_PROVEN', sdk_drain='NOT_PROVEN')
    row.update(changes)
    return 'NATIVE_PROCESS_OWNERS ' + ' '.join(k + '=' + v for k, v in row.items())


class OwnerTests(unittest.TestCase):
    def test_dual_event_order(self):
        self.assertIsNotNone(O.owners(owner_text(), RUNS['A'], 'OLD_BOTH_READY'))
        self.assertIsNone(O.owners(owner_text(), RUNS['A'], 'NEW_BOTH_ACTIVE'))
        self.assertIsNotNone(O.owners(owner_text() + '\n' + owner_text('NEW_BOTH_ACTIVE'), RUNS['A'], 'NEW_BOTH_ACTIVE'))

    def test_no_single_lane_false_fresh_or_wrong_source(self):
        for change in [{'source': 'HOST_OPAQUE_PUBLIC_API_SHAPE'}, {'accepted_on_shutdown': '0'}, {'callbacks': '1'},
                       {'run': RUNS['B0']}, {'sdk_epoch': 'PROVEN'}]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                O.owners(owner_text(**change), RUNS['A'], 'OLD_BOTH_READY')
        for change in [{'new_dial': '11'}, {'new_dial': '12'}, {'new_accepted': '21'}, {'new_dial_owner': '102'},
                       {'new_dial_owner': '202'}, {'listener': '9'}, {'wall_ms': '999'}]:
            with self.subTest(change=change), self.assertRaises(ValueError):
                O.owners(owner_text() + '\n' + owner_text('NEW_BOTH_ACTIVE', **change), RUNS['A'], 'NEW_BOTH_ACTIVE')
        for invalid in [owner_text('NEW_BOTH_ACTIVE'), owner_text() + '\n' + owner_text()]:
            with self.assertRaises(ValueError):
                O.owners(invalid, RUNS['A'], 'NEW_BOTH_ACTIVE')


class DeadlineTests(unittest.TestCase):
    def test_wait_cannot_accept_predicate_after_deadline(self):
        scene = H.RestartScene.__new__(H.RestartScene)
        scene.deadline = 35
        scene.processes = {}
        with patch.object(H.time, 'monotonic', side_effect=[34, 35]):
            with self.assertRaises(ValueError):
                scene.wait((), lambda t, s: True)

    def test_expired_budget_does_not_mutate_process_deadline(self):
        scene = H.RestartScene.__new__(H.RestartScene)
        scene.processes = {}
        with patch.object(H.time, 'monotonic', return_value=35), self.assertRaises(ValueError):
            scene.budget(35, 'BUSINESS')

    def test_only_owned_A_finite_extra_controls(self):
        for instance, name in [('B0', 'ARM'), ('B1', 'REPLACE'), ('A', 'END'), ('A', 'OFFLINE')]:
            proc = H.RestartProcess.__new__(H.RestartProcess)
            proc.instance = instance
            with self.subTest(instance=instance, name=name), self.assertRaises(ValueError):
                proc.extra_control(name)


class PollBoundaryTests(unittest.TestCase):
    def process(self, payload, newline):
        proc = H.RestartProcess.__new__(H.RestartProcess)
        proc.scenario, proc.instance, proc.run = SCENE, 'A', RUNS['A']
        proc.prefix = {'A': '/data/local/tmp/mdds_dds_' + RUNS['A'] + '_A'}
        proc.owned = {'A': {'child': {'pid': 100}}}
        proc.sequence = 0
        proc.poll_resample_budget = {'used': False}
        proc.write = lambda *args: None
        def command(side, argv, timeout):
            marker = re.search(r'R25_RC_[0-9a-f]{16}', argv[1])[0]
            return payload + newline + marker.encode() + b'=0' + newline
        proc.command = command
        return proc

    def test_inherited_shell_preserves_lf_and_crlf(self):
        for newline in (b'\n', b'\r\n'):
            raw_text = text(transcript('A')).encode().replace(b'\n', newline) + newline + b'R27_STDERR' + newline
            payload = raw_text + newline + b'R27_EXIT' + newline + b'normal_exit' + newline
            proc = self.process(payload, newline)
            with self.subTest(newline=newline):
                observed, status = proc.poll('A')
                self.assertEqual(observed.encode(), raw_text)
                self.assertEqual(status, 'normal_exit')

    def test_missing_or_duplicate_exit_boundary_rejected(self):
        for payload in (b'no exit boundary', b'\r\nR27_EXIT\r\n\r\nR27_EXIT\r\n'):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                self.process(payload, b'\r\n').poll('A')

    def test_empty_capture_resampled_once_with_original_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            payload = text(transcript('A')).encode() + b'\nR27_STDERR\n\nR27_EXIT\n'
            proc = self.process(payload, b'\n')
            del proc.command, proc.write  # Exercise actual inherited capture and shell.
            proc.output, proc.args = Path(directory), H.argparse.Namespace(hdc='hdc.exe')
            proc.stage_deadline = 200
            calls = []
            def run(argv, **kwargs):
                calls.append((argv, kwargs))
                marker = re.search(r'R25_RC_[0-9a-f]{16}', argv[-1])[0].encode()
                out = payload + b'\n' + marker + b'=0\n' if len(calls) == 2 else b''
                return H.argparse.Namespace(stdout=out, stderr=b'', returncode=0)
            with patch.object(H.NATIVE.subprocess, 'run', side_effect=run), patch.object(H.time, 'monotonic', return_value=100):
                observed, status = proc.poll('A')
                self.assertIn('R27_STDERR', observed)
                self.assertEqual(status, '')
                with self.assertRaisesRegex(OSError, 'Remote exit marker missing'):
                    proc.poll('A')
                other = self.process(payload, b'\n')
                other.poll_resample_budget = proc.poll_resample_budget
                other.command = lambda *args: b''
                with self.assertRaisesRegex(OSError, 'Remote exit marker missing'):
                    other.poll('A')
            self.assertEqual(len(calls), 3)  # No second resample, even in a later poll.
            self.assertEqual(proc.stage_deadline, 200)
            self.assertEqual([c[1]['timeout'] for c in calls], [15, 15, 15])
            request = H.read(proc.output / 'POLL_RESAMPLE_REQUEST.json')
            self.assertEqual(request['original_remote_exit'], 'UNKNOWN')
            self.assertEqual(request['failed_sequence'], 1)
            for path, digest in request['original_evidence'].items():
                self.assertEqual(H.sha(path), digest)
            self.assertEqual((proc.output / 'commands/0001-A.stdout').read_bytes(), b'')
            self.assertFalse((proc.output / 'remote/0001-A.json').exists())
            self.assertEqual(H.read(proc.output / 'remote/0002-A.json')['remote_exit'], 0)

    def test_resample_excludes_other_failures_and_expired_budget(self):
        cases = ('partial', 'stderr', 'host_failure', 'remote_failure', 'timeout', 'expired', 'invalid_sample')
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                proc = self.process(b'', b'\n')
                del proc.command, proc.write
                proc.output, proc.args = Path(directory), H.argparse.Namespace(hdc='hdc.exe')
                proc.stage_deadline = 110
                clock, calls = [100], []
                def run(argv, **kwargs):
                    calls.append(kwargs['timeout'])
                    marker = re.search(r'R25_RC_[0-9a-f]{16}', argv[-1])[0].encode()
                    if case == 'timeout':
                        raise H.NATIVE.subprocess.TimeoutExpired(argv, kwargs['timeout'])
                    if case == 'expired':
                        clock[0] = 110
                    out = b'partial' if case == 'partial' else b''
                    if case == 'remote_failure': out = b'\n' + marker + b'=7\n'
                    if case == 'invalid_sample' and len(calls) == 2: out = b'bad boundary\n' + marker + b'=0\n'
                    return H.argparse.Namespace(stdout=out, stderr=b'warning' if case == 'stderr' else b'',
                                                returncode=1 if case == 'host_failure' else 0)
                with patch.object(H.NATIVE.subprocess, 'run', side_effect=run), patch.object(H.time, 'monotonic', side_effect=lambda: clock[0]):
                    with self.assertRaises((OSError, ValueError)):
                        proc.poll('A')
                self.assertEqual(len(calls), 2 if case == 'invalid_sample' else 1)
                self.assertEqual(calls, [10] * len(calls))
                self.assertEqual(proc.stage_deadline, 110)


if __name__ == '__main__':
    unittest.main()
