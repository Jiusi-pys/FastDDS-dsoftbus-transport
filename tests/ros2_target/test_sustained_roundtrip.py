"""Synthetic host controls only; no ROS, SDK, board, or transport is exercised."""
import base64
import copy
import hashlib
import json
from pathlib import Path
import runpy
import struct
import unittest
from unittest.mock import patch
import types

REPO = Path(__file__).resolve().parents[2]
P = runpy.run_path(str(REPO / 'integrations/ros2/target/sustained_roundtrip_probe.py'))
O = runpy.run_path(str(Path(__file__).with_name('sustained_roundtrip_oracle.py')))
RUN = '0123456789abcdef'


def fixture_rows(side):
    rows = []
    def add(event, **fields):
        rows.append(dict(schema=1, run=RUN, side=side, pid=101 if side == 'A' else 102,
                         role='requester' if side == 'A' else 'responder', seq=len(rows)+1,
                         event=event, **fields))
    def sample(received):
        add('RESOURCES', received=received, VmRSS=1234, VmHWM=1300, Threads=9,
            fd_count=20, monotonic_ns=1_000_000_000 + (received - 1) * 100_000_000 + 2_000_000 if received else 999_000_000)
    add('NODE_READY', name='/roundtrip_' + RUN + '_' + side)
    outgoing, incoming = ('request', 'reply') if side == 'A' else ('reply', 'request')
    add('ENTITY_READY', entity='requester' if side == 'A' else 'responder',
        outgoing='/roundtrip_' + RUN + '/' + outgoing, incoming='/roundtrip_' + RUN + '/' + incoming)
    sample(0)
    events = ('ROUNDTRIP_SEND', 'ROUNDTRIP_RECEIVE') if side == 'A' else ('ROUNDTRIP_RECEIVE', 'ROUNDTRIP_ECHO')
    for sequence in range(200):
        text = 'r26_' + RUN + '_' + str(sequence).zfill(3)
        raw = text.encode() + b'\0'
        data = b'\0\1\0\0' + struct.pack('<I', len(raw)) + raw
        for index, event in enumerate(events):
            add(event, sequence=sequence, text=text, cdr_base64=base64.b64encode(data).decode(),
                cdr_sha256=hashlib.sha256(data).hexdigest(), cdr_bytes=len(data),
                monotonic_ns=1_000_000_000 + sequence * 100_000_000 + index * 1_000_000)
        if (sequence + 1) % 50 == 0:
            sample(sequence + 1)
    add('ROUNDTRIP_COMPLETE', received=200, quiet_seconds=2.01, monotonic_ns=23_000_000_000)
    add('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
    add('NODE_DESTROY_RETURN', returned=True)
    add('SHUTDOWN_RETURN', returned=True)
    add('FINAL', success=True, node_created=True)
    return rows


def texts(rows):
    return {side: '\n'.join('R25_PROBE ' + json.dumps(row) for row in entries) for side, entries in rows.items()}


class LedgerTests(unittest.TestCase):
    def test_full_roundtrip_sequence(self):
        a, b = P['RoundtripLedger'](RUN, 'A'), P['RoundtripLedger'](RUN, 'B')
        for sequence in range(200):
            message = a.next_send()
            a.sent_request(message)
            self.assertEqual(b.receive(message), sequence)
            self.assertEqual(a.receive(message), sequence)
        self.assertEqual((a.sent, a.received, b.received), (200, 200, 200))
        with self.assertRaises(ValueError):
            a.next_send()

    def test_unsolicited_reply(self):
        a = P['RoundtripLedger'](RUN, 'A')
        with self.assertRaises(ValueError):
            a.receive(a.payload(0))

    def test_no_application_retry(self):
        a = P['RoundtripLedger'](RUN, 'A')
        a.sent_request(a.next_send())
        with self.assertRaises(ValueError):
            a.next_send()

    def test_corrupt_duplicate_missing_reordered(self):
        for value in ('foreign', 'r26_' + RUN + '_001', 'r26_' + RUN + '_200'):
            with self.subTest(value=value):
                b = P['RoundtripLedger'](RUN, 'B')
                with self.assertRaises(ValueError):
                    b.receive(value)
        b = P['RoundtripLedger'](RUN, 'B')
        b.receive(b.payload(0))
        with self.assertRaises(ValueError):
            b.receive(b.payload(0))

    def test_type_and_run_boundaries(self):
        for run, side in ((RUN.upper(), 'A'), (RUN, 'C'), ('short', 'A')):
            with self.assertRaises(ValueError):
                P['RoundtripLedger'](run, side)
        b = P['RoundtripLedger'](RUN, 'B')
        for sequence in (True, -1, 200, 1.0):
            with self.assertRaises(ValueError):
                b.payload(sequence)


class OracleTests(unittest.TestCase):
    def setUp(self):
        self.rows = {side: fixture_rows(side) for side in 'AB'}

    def verify(self, rows=None, final=True):
        return O['verify_roundtrip_logs'](texts(self.rows if rows is None else rows), RUN,
                                          {'A': 101, 'B': 102}, final=final)

    def test_complete_synthetic_fixture(self):
        result = self.verify()
        self.assertEqual(result['roundtrips'], 200)
        self.assertEqual(result['native_path'], 'REQUIRES_SEPARATE_EVIDENCE')

    def test_missing_duplicate_foreign_and_order(self):
        for mutate in (lambda r: r.pop(10), lambda r: r.insert(10, r[10]),
                       lambda r: r[10].update(run='f' * 16), lambda r: r[10].update(sequence=100)):
            with self.subTest(mutate=mutate):
                rows = copy.deepcopy(self.rows)
                mutate(rows['A'])
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_exact_cdr_hash_length_and_text(self):
        for key, value in (('cdr_sha256', '0'*64), ('cdr_bytes', True), ('text', 'foreign'),
                           ('cdr_base64', base64.b64encode(b'bad').decode())):
            with self.subTest(key=key):
                rows = copy.deepcopy(self.rows)
                rows['B'][3][key] = value
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_resource_samples_required(self):
        for key, value in (('Threads', 0), ('fd_count', True), ('VmRSS', -1), ('received', 1)):
            with self.subTest(key=key):
                rows = copy.deepcopy(self.rows)
                rows['A'][2][key] = value
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_finish_and_quiet_interval(self):
        for offset, key, value in ((-1, 'success', 1), (-2, 'returned', False),
                                   (-4, 'reason', 'WINDOW_FAILED'), (-5, 'quiet_seconds', 1.9)):
            with self.subTest(offset=offset):
                rows = copy.deepcopy(self.rows)
                rows['A'][offset][key] = value
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_pacing_time_and_identity(self):
        for key, value in (('monotonic_ns', 1), ('pid', True), ('schema', True), ('event', 'FAIL')):
            with self.subTest(key=key):
                rows = copy.deepcopy(self.rows)
                rows['A'][5][key] = value
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_partial_business_cannot_pass(self):
        rows = {s: r[:100] for s, r in self.rows.items()}
        with self.assertRaises(ValueError):
            self.verify(rows, final=False)

    def test_preclose_content_only(self):
        rows = {s: r[:-4] for s, r in self.rows.items()}
        self.assertEqual(self.verify(rows, final=False)['status'], 'PASS')
        with self.assertRaises(ValueError):
            self.verify(rows)

    def test_duplicate_json_key(self):
        with self.assertRaises(ValueError):
            O['roundtrip_rows']('R25_PROBE {"schema":1,"schema":1}', RUN, 'A', 101)

    def test_resources_cannot_regress_or_follow_completion(self):
        for mode in ('clock', 'position', 'received_type'):
            with self.subTest(mode=mode):
                rows = copy.deepcopy(self.rows)
                sample = next(r for r in rows['A'] if r['event'] == 'RESOURCES' and r['received'] == 200)
                if mode == 'clock':
                    sample['monotonic_ns'] = 1
                elif mode == 'position':
                    rows['A'].remove(sample)
                    index = next(i for i, r in enumerate(rows['A']) if r['event'] == 'ROUNDTRIP_COMPLETE')
                    rows['A'].insert(index + 1, sample)
                    for seq, row in enumerate(rows['A'], 1):
                        row['seq'] = seq
                else:
                    rows['A'][2]['received'] = False
                with self.assertRaises(ValueError):
                    self.verify(rows)

    def test_node_topic_and_observed_quiet_clock(self):
        for index, key, value in ((0, 'name', '/foreign'), (1, 'outgoing', '/foreign'),
                                  (-5, 'monotonic_ns', 21_000_000_000)):
            rows = copy.deepcopy(self.rows)
            rows['A'][index][key] = value
            with self.assertRaises(ValueError):
                self.verify(rows)


class RunnerDeadlineTests(unittest.TestCase):
    def test_business_verification_and_receipt_delay(self):
        module = runpy.run_path(str(Path(__file__).with_name('run_sustained_roundtrip.py')))
        cls = module['SustainedRunner']
        globals_ = cls.workflow.__globals__
        logs = texts({s: fixture_rows(s)[:-4] for s in 'AB'})
        original = globals_['O'].verify_roundtrip_logs
        for delay_phase in ('verify', 'receipt'):
            for finish_time in (34.999, 35.0, 36.0):
                with self.subTest(phase=delay_phase, time=finish_time):
                    tick, calls = [0.0], []
                    runner = cls.__new__(cls)
                    runner.run, runner.closed = RUN, set()
                    runner.owned = {s: {'child': {'pid': 101 if s == 'A' else 102}} for s in 'AB'}
                    runner.launch = runner.paired_checkpoint = runner.wait_exit = lambda *a: None
                    runner.maps = runner.publish = lambda *a: None
                    runner.finish_fixture = lambda *a: calls.append('finish')
                    def poll(side):
                        tick[0] = 34.99
                        return logs[side], ''
                    def verify(*args, **kwargs):
                        answer = original(*args, **kwargs)
                        tick[0] = finish_time if delay_phase == 'verify' else 34.99
                        return answer
                    def write(name, value):
                        if name == 'BUSINESS.json' and delay_phase == 'receipt':
                            tick[0] = finish_time
                    runner.poll, runner.write = poll, write
                    clock = types.SimpleNamespace(monotonic=lambda: tick[0], sleep=lambda n: None)
                    with patch.dict(globals_, time=clock), patch.object(globals_['O'], 'verify_roundtrip_logs', verify):
                        if finish_time < 35:
                            runner.workflow()
                            self.assertEqual(calls, ['finish', 'finish'])
                        else:
                            with self.assertRaisesRegex(ValueError, '35s business deadline'):
                                runner.workflow()
                            self.assertEqual(calls, [])

    def test_checkpoint_readiness_delay(self):
        module = runpy.run_path(str(Path(__file__).with_name('run_sustained_roundtrip.py')))
        cls = module['SustainedRunner']
        for ready_time in (34.999, 35.0, 36.0):
            with self.subTest(time=ready_time):
                tick = [34.99]
                runner = cls.__new__(cls)
                runner.stage_deadline = 35.0
                runner.poll = lambda s: ('fixture', '')
                def ready(*args):
                    tick[0] = ready_time
                    return True
                runner.ready = ready
                clock = types.SimpleNamespace(monotonic=lambda: tick[0], sleep=lambda n: None)
                with patch.dict(cls.paired_checkpoint.__globals__, time=clock):
                    if ready_time < 35:
                        self.assertEqual(runner.paired_checkpoint('LISTENER'), {'A': 'fixture', 'B': 'fixture'})
                    else:
                        with self.assertRaisesRegex(ValueError, 'Checkpoint deadline'):
                            runner.paired_checkpoint('LISTENER')


if __name__ == '__main__':
    unittest.main()
