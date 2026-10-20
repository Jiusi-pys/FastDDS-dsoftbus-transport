"""HOST-only evidence models; synthetic logs are never native communication."""
import base64
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import struct
import tempfile
import types
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
P = runpy.run_path(str(REPO / 'integrations/ros2/target/endurance_roundtrip_probe.py'))
O = runpy.run_path(str(HERE / 'endurance_roundtrip_oracle.py'))
spec = importlib.util.spec_from_file_location('r36_test_runner', HERE / 'run_endurance_roundtrip.py')
N = importlib.util.module_from_spec(spec)
spec.loader.exec_module(N)
RUN = '0123456789abcdef'
PIDS = {'A': 111, 'B': 222}


def fixture_rows():
    result = {}
    for side in 'AB':
        rows = []
        def emit(event, **fields):
            rows.append(dict(schema=1, run=RUN, side=side, pid=PIDS[side],
                             role='requester' if side == 'A' else 'responder',
                             seq=len(rows) + 1, event=event, **fields))
        def resources(received, timestamp):
            rss = 85000 + received // 60
            emit('RESOURCES', received=received, VmRSS=rss, VmHWM=rss + 10,
                 Threads=24, fd_count=20, monotonic_ns=timestamp)
        base = 100_000_000_000
        outgoing, incoming = ('request', 'reply') if side == 'A' else ('reply', 'request')
        emit('NODE_READY', name='/roundtrip_' + RUN + '_' + side)
        emit('ENTITY_READY', entity='requester' if side == 'A' else 'responder',
             outgoing='/roundtrip_' + RUN + '/' + outgoing,
             incoming='/roundtrip_' + RUN + '/' + incoming)
        resources(0, base)
        for sequence in range(1801):
            text = 'r36' + RUN + '_' + str(sequence).zfill(4)
            raw = b'\x00\x01\x00\x00' + struct.pack('<I', len(text) + 1) + text.encode() + b'\0'
            fields = dict(sequence=sequence, text=text, cdr_base64=base64.b64encode(raw).decode(),
                          cdr_sha256=hashlib.sha256(raw).hexdigest(), cdr_bytes=len(raw))
            clock = base + 20_000_000 + sequence * 1_000_000_000
            first, second = ('ROUNDTRIP_SEND', 'ROUNDTRIP_RECEIVE') if side == 'A' else (
                'ROUNDTRIP_RECEIVE', 'ROUNDTRIP_ECHO')
            emit(first, monotonic_ns=clock, **fields)
            emit(second, monotonic_ns=clock + 2_000_000, **fields)
            if (sequence + 1) % 60 == 0 or sequence == 1800:
                resources(sequence + 1, clock + 3_000_000)
        emit('ROUNDTRIP_COMPLETE', received=1801, quiet_seconds=2.1,
             monotonic_ns=clock + 2_102_000_000)
        emit('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
        emit('NODE_DESTROY_RETURN', returned=True)
        emit('SHUTDOWN_RETURN', returned=True)
        emit('FINAL', success=True, node_created=True)
        result[side] = rows
    return result


def encoded(rows):
    return {side: '\n'.join('R25_PROBE ' + json.dumps(row, separators=(',', ':'))
                           for row in values) for side, values in rows.items()}


def resequence(rows):
    for values in rows.values():
        for index, row in enumerate(values):
            row['seq'] = index + 1


class EnduranceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.good = fixture_rows()

    def reject(self, rows):
        with self.assertRaises((ValueError, KeyError)):
            O['verify_roundtrip_logs'](encoded(rows), RUN, PIDS)

    def test_finite_1801_accounting_keeps_one_outstanding_and_33_byte_cdr(self):
        a, b = P['RoundtripLedger'](RUN, 'A'), P['RoundtripLedger'](RUN, 'B')
        for index in range(1801):
            text = a.next_send()
            self.assertEqual(len(text.encode()), 24)
            a.sent_request(text)
            with self.assertRaises(ValueError):
                a.next_send()
            self.assertEqual(b.receive(text), index)
            self.assertEqual(a.receive(text), index)
        with self.assertRaises(ValueError):
            a.next_send()
        with self.assertRaises(ValueError):
            b.receive(text)

    def test_full_host_fixture_measures_real_span_rtt_and_resource_trend(self):
        result = O['verify_roundtrip_logs'](encoded(self.good), RUN, PIDS)
        self.assertEqual(result['roundtrips'], 1801)
        self.assertEqual((result['lost'], result['duplicate'], result['out_of_order_or_foreign']), (0, 0, 0))
        self.assertEqual(result['latency']['median_ms'], 2)
        self.assertGreaterEqual(result['local_payload_span_seconds']['A'], 1800)
        self.assertEqual(result['resource_trends']['A']['samples'], 32)
        self.assertGreater(result['resource_trends']['A']['rss_slope_KiB_per_minute_after_warmup'], 0)
        self.assertEqual(result['resource_leak_freedom'], 'NOT_PROVEN')

    def test_missing_exchange_is_not_a_pass(self):
        rows = deepcopy(self.good)
        rows['A'] = [r for r in rows['A'] if not (r['event'] == 'ROUNDTRIP_RECEIVE' and r['sequence'] == 1700)]
        resequence(rows)
        self.reject(rows)

    def test_duplicate_is_not_deduplicated(self):
        rows = deepcopy(self.good)
        index = next(i for i, r in enumerate(rows['B']) if r['event'] == 'ROUNDTRIP_ECHO' and r['sequence'] == 1000)
        rows['B'].insert(index, deepcopy(rows['B'][index]))
        resequence(rows)
        self.reject(rows)

    def test_foreign_payload_or_reordered_sequence_rejects(self):
        for field, value in [('text', 'foreign'), ('sequence', 999)]:
            rows = deepcopy(self.good)
            next(r for r in rows['B'] if r['event'] == 'ROUNDTRIP_RECEIVE' and r['sequence'] == 1000)[field] = value
            self.reject(rows)

    def test_full_cdr_hash_and_length_are_required(self):
        rows = deepcopy(self.good)
        next(r for r in rows['A'] if r['event'] == 'ROUNDTRIP_SEND')['cdr_sha256'] = '0' * 64
        self.reject(rows)

    def test_cannot_relabel_short_host_window_as_30_minutes(self):
        rows = deepcopy(self.good)
        for values in rows.values():
            for row in values:
                if 'monotonic_ns' in row:
                    row['monotonic_ns'] //= 10
        self.reject(rows)

    def test_missing_final_resource_sample_and_bad_high_water_reject(self):
        rows = deepcopy(self.good)
        rows['B'] = [r for r in rows['B'] if not (r['event'] == 'RESOURCES' and r['received'] == 1801)]
        resequence(rows)
        self.reject(rows)
        rows = deepcopy(self.good)
        sample = next(r for r in rows['B'] if r['event'] == 'RESOURCES')
        sample['VmHWM'] = sample['VmRSS'] - 1
        self.reject(rows)

    def test_boolean_count_timestamp_and_wrong_pid_reject(self):
        for field, value in [('monotonic_ns', True), ('pid', 333)]:
            rows = deepcopy(self.good)
            next(r for r in rows['A'] if r['event'] == 'ROUNDTRIP_SEND')[field] = value
            self.reject(rows)

    def test_normal_finalization_is_mandatory(self):
        rows = deepcopy(self.good)
        next(r for r in rows['A'] if r['event'] == 'FINAL')['success'] = False
        self.reject(rows)

    def test_old_case_and_frozen_native_helper_bytes_are_unchanged(self):
        self.assertEqual(hashlib.sha256((HERE / 'run_sustained_roundtrip.py').read_bytes()).hexdigest(),
                         'c3d4096efb1fc17357fb144794c72bf1a410e5a831c9102173da7b7a483a838b')
        self.assertEqual(hashlib.sha256((REPO / 'integrations/ros2/target/sustained_roundtrip_probe.py').read_bytes()).hexdigest(),
                         '41c9d455f0aec1782508dde13a2c383f5519a5adc5e085389bd4b90afaf1cf7e')
        self.assertEqual(runpy.run_path(str(REPO / 'integrations/ros2/target/sustained_roundtrip_probe.py'))['COUNT'], 200)

    def exercise_dispatch_boundary(self, *, release_scope, age, consumed=False, native_rebuild=False, bindings=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            prepared, release = root / 'PLAN.json', root / 'RELEASE.json'
            prepared.write_text('{}', encoding='utf8')
            release.write_text(json.dumps(dict(schema=1, scope=release_scope, run=RUN,
                               prepared_sha256=N.sha(prepared), runner_sha256=N.sha(N.__file__))), encoding='utf8')
            marker = root / (RUN + '.CONSUMED.json')
            if consumed:
                marker.write_text('original', encoding='ascii')
            obj = N.SustainedRunner.__new__(N.SustainedRunner)
            obj.args = types.SimpleNamespace(prepared=prepared, release=release)
            obj.output, obj.run, obj.plan = root, RUN, {'bindings': {} if bindings is None else bindings}
            obj.native_rebuild = native_rebuild
            obj.observed = datetime.now(timezone.utc) - timedelta(seconds=age)
            obj.errors, obj.blocked, obj.owned, obj.stage = [], [], {}, 'INPUTS'
            written, cleanup_calls = {}, []
            obj.write = lambda name, value: written.__setitem__(name, value)
            obj.cleanup = lambda failed: cleanup_calls.append(failed)
            obj.deploy = lambda: self.fail('No device deployment is authorized by this host negative fixture')
            self.assertEqual(obj.execute(), 1)
            self.assertEqual(cleanup_calls, [True])
            self.assertNotEqual(written['RESULT.json']['status'], 'PASS')
            if consumed:
                self.assertEqual(marker.read_text(encoding='ascii'), 'original')
            else:
                self.assertFalse(marker.exists())
            return written['RESULT.json']

    def test_old_release_cannot_authorize_new_extended_case(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_SUSTAINED_ROUNDTRIP_RELEASE', age=1)

    def test_old_freshness_limit_remains_240_seconds(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_R36_ENDURANCE_RELEASE', age=241)

    def test_consumed_release_never_replays(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_R36_ENDURANCE_RELEASE', age=1, consumed=True)

    def test_old_release_cannot_authorize_source_bound_native_case(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_R36_ENDURANCE_RELEASE', age=1,
                                        native_rebuild=True)

    def test_native_case_preserves_240_second_freshness_limit(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_R37_NATIVE_CLOSE_ENDURANCE_RELEASE', age=241,
                                        native_rebuild=True)

    def test_native_case_consumed_release_never_replays(self):
        self.exercise_dispatch_boundary(release_scope='MAIN_ONE_SHOT_R37_NATIVE_CLOSE_ENDURANCE_RELEASE', age=1,
                                        consumed=True, native_rebuild=True)

    def test_native_preparation_binds_host_regression_inputs(self):
        """HOST model: prepare-derived source drift must fail before consume/deploy."""
        host_sources = [REPO / 'tests/ros2_host/locator_observation_tests.cpp',
                        REPO / 'tests/ros2_host/locator_observation/CMakeLists.txt']
        native_sources = [REPO / 'integrations/ros2/target' / name for name in
                          ('NativeObservation.hpp', 'NativeObservation.cpp', 'TargetBridge.cpp')]
        actual_sha = N.sha
        native_bindings = {str(p.resolve()): actual_sha(p) for p in native_sources}
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            derivation, routes, external, identity, evidence = [root / name for name in
                ('DERIVATION.json', 'routes.json', 'external.json', 'identity.json', 'query-model')]
            for path in (derivation, external, identity, evidence):
                path.write_text('{}', encoding='utf8')
            routes.write_text(json.dumps({'cross_confirmed': True,
                'bindings': {str(evidence): actual_sha(evidence)}}), encoding='utf8')
            # Isolate package/provider admission; exercise the actual preparation source binding.
            with (patch.object(N.NB, 'EVIDENCE', root),
                  patch.object(N, 'fresh_output', return_value=root / 'prepared'),
                  patch.object(N.BASE, 'runtime_path', side_effect=lambda p, *a, **k: Path(p)),
                  patch.object(N, 'checked_package', return_value=(root / 'package', {})),
                  patch.object(N.NB, 'build_bindings', return_value=native_bindings),
                  patch.object(N.BAG, 'EXTERNAL_SHA', actual_sha(external)),
                  patch.object(N.BAG, 'IDENTITY_SHA', actual_sha(identity))):
                prepared = N.prepare_sustained(derivation, routes, external, identity, root / 'prepared')
            for source in host_sources:
                self.assertEqual(prepared['bindings'].get(str(source.resolve())), actual_sha(source))
            source_bindings = {str(p.resolve()): prepared['bindings'][str(p.resolve())]
                               for p in [*N.SOURCES, *native_sources]}
            for source in [*host_sources, native_sources[0]]:
                with self.subTest(source=source.relative_to(REPO).as_posix()):
                    def changed_sha(path):
                        return '0' * 64 if Path(path).resolve() == source.resolve() else actual_sha(path)
                    with patch.object(N, 'sha', side_effect=changed_sha):
                        result = self.exercise_dispatch_boundary(
                            release_scope='MAIN_ONE_SHOT_R37_NATIVE_CLOSE_ENDURANCE_RELEASE', age=1,
                            native_rebuild=True, bindings=source_bindings)
                    self.assertEqual(result['failures'], ['INPUTS: Input changed before dispatch'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
