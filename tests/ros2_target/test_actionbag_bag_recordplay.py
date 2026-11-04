"""Host-only controls; fake process and CLI records never qualify devices."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import runpy
import shutil
import struct
import sys
import tempfile
from types import SimpleNamespace as NS
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).with_name('run_actionbag_bag_recordplay.py')
spec = importlib.util.spec_from_file_location('candidate_recordplay_runner', SOURCE)
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)
RUN, RECORD = '0123456789abcdef', 'fedcba9876543210'
with patch.object(sys, 'path', [str(SOURCE.parent), *sys.path]):
    ORACLE_FIXTURE = runpy.run_path(str(SOURCE.with_name('test_bag_sqlite_oracle.py')))


class BagRecordplayRunnerControls(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='bag-runner-controls-')
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)
        self.r = object.__new__(M.BagRecordPlayRunner)
        self.r.output, self.r.run, self.r.record_run, self.r.case = self.output, RUN, RUN, M.CASES[0]
        self.r.prefix = {s: '/data/local/tmp/mdds_dds_' + RUN + '_' + s for s in 'AB'}
        self.r.owned = {s: {'child': {'pid': 22, 'starttime': 3, 'exe': 'selected', 'argv': ['selected'], 'ppid': 21},
                            'supervisor': {'pid': 21}} for s in 'AB'}
        self.r.roles = {'A': 'bag-publisher', 'B': None}
        self.r.normal_stop_attempted = self.r.normal_stop_confirmed = self.r.resume_attempted = False
        self.r.errors, self.r.blocked, self.r.launch_pending = [], [], {}
        self.r.finished_fixtures, self.r.closed, self.r.maps_observed = set(), set(), {}
        self.r.sequence, self.r.stage_deadline = 0, None
        self.r.guard = lambda *args, **kwargs: True
        self.r.root_guard = lambda *args: None
        self.commands = []
        self.r.shell = self.shell
        self.r.verify_files = lambda *args: None
        self.clock = 0
        self.files = {'lib/librmw_fastrtps_shared_cpp.so': '0fc8f4a214849f4a5abe15f66baa23404448d0cebaf972607d40578200dcf369',
                      'lib/librmw_fastrtps_cpp.so': '6b637fe1292e664507811041e5576bad9291c9bae60b87237f7b92fa88476681'}

    def shell(self, side, script, *args):
        self.commands.append((side, script))
        return b'0'

    def monotonic(self):
        self.clock += 1
        return self.clock

    def rows(self, role='bag-publisher', final=False):
        rows = []
        record = RUN if role == 'bag-publisher' else RECORD
        def add(event, **fields):
            rows.append(dict(schema=1, run=RUN, side='A', pid=22, role=role, seq=len(rows) + 1, event=event, **fields))
        add('NODE_READY', name=('/bagpub_' if role == 'bag-publisher' else '/bagsub_') + RUN)
        if role == 'bag-publisher':
            add('BAG_DEFINITION', encoding='ros2msg', encoded_message_definition='string data\n', type_hash='')
        add('ENTITY_READY', entity=role, name=('/bag_' if role == 'bag-publisher' else '/bagplay_') + RUN)
        if role == 'bag-publisher':
            add('BAG_PUBLISHER_INFO')
        else:
            add('BAG_REPLAY_READY', publisher_count=1, service='/bagplayer_' + RUN + '/resume')
            add('BAG_RESUME_ACCEPTED', record_run=record)
            add('BAG_RESUME_CALLED', service='/bagplayer_' + RUN + '/resume')
            add('BAG_RESUME_RETURNED', success=True)
        for sequence in range(10):
            text = 'r25bag_' + record + '_' + str(sequence).zfill(3)
            raw = text.encode() + b'\0'
            data = b'\0\1\0\0' + struct.pack('<I', len(raw)) + raw
            add('BAG_PUBLISH' if role == 'bag-publisher' else 'BAG_RECEIVE', sequence=sequence, text=text,
                cdr_base64=base64.b64encode(data).decode(), cdr_sha256=hashlib.sha256(data).hexdigest(),
                cdr_bytes=len(data), **({'matched': 1} if role == 'bag-publisher' else {}))
        if role == 'bag-publisher':
            add('BAG_ACKED', published=10, timeout_seconds=5)
        if final:
            add('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
            add('NODE_DESTROY_RETURN', returned=True)
            add('SHUTDOWN_RETURN', returned=True)
            add('FINAL', success=True, node_created=True)
        return rows

    def text(self, rows):
        return '\n'.join('R25_PROBE ' + json.dumps(row) for row in rows)

    def business(self, rows=None, role='bag-publisher', final=False):
        return M.recordplay_business(self.text(rows if rows is not None else self.rows(role, final)), RUN,
            RUN if role == 'bag-publisher' else RECORD, 22, role, final=final)

    def map_receipts(self):
        for side in 'AB':
            self.r.write(side + '.business.providers.json', {'sha256': {}})

    def recording_text(self):
        return {'A': '', 'B': "Recording...\nSubscribed to topic '/bag_" + RUN + "'"}

    def test_ten_cdr_and_complete_normal_teardown_both_roles(self):
        for role in ('bag-publisher', 'bag-subscriber'):
            with self.subTest(role=role):
                self.assertTrue(self.business(role=role, final=True)['complete'])

    def test_incomplete_business_waits_but_cannot_be_final(self):
        rows = self.rows()[:7]
        self.assertFalse(self.business(rows)['complete'])
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            self.business(rows, final=True)

    def test_probe_identity_and_sequence_cannot_be_spliced(self):
        for key, value in (('run', RECORD), ('side', 'B'), ('pid', 23), ('seq', 0), ('schema', True)):
            with self.subTest(key=key):
                rows = self.rows(); rows[3][key] = value
                with self.assertRaisesRegex(ValueError, 'identity/sequence'):
                    self.business(rows)

    def test_duplicate_payload_and_cdr_mutation_are_rejected(self):
        for key, value in (('sequence', 0), ('text', 'wrong'), ('cdr_sha256', 'a' * 64), ('matched', 0), ('cdr_bytes', True)):
            with self.subTest(key=key):
                rows = self.rows(); rows[5][key] = value
                with self.assertRaises(ValueError):
                    self.business(rows)

    def test_no_ack_or_bad_final_cannot_pass(self):
        rows = self.rows(final=True)
        rows[-1]['success'] = False
        with self.assertRaisesRegex(ValueError, 'teardown/final'):
            self.business(rows, final=True)
        rows = self.rows(); rows[-1]['timeout_seconds'] = 6
        with self.assertRaisesRegex(ValueError, 'acknowledgement'):
            self.business(rows)

    def test_wrong_resume_service_and_pre_resume_data_are_rejected(self):
        rows = self.rows('bag-subscriber'); rows[4]['service'] = '/wrong'
        with self.assertRaisesRegex(ValueError, 'service'):
            self.business(rows, role='bag-subscriber')
        rows = self.rows('bag-subscriber')
        rows[2], rows[6] = rows[6], rows[2]
        for index, row in enumerate(rows): row['seq'] = index + 1
        with self.assertRaisesRegex(ValueError, 'before Resume'):
            self.business(rows, role='bag-subscriber')

    def test_exact_stock_argv_and_distinct_play_run(self):
        root = self.r.prefix['B']
        record = M.recordplay_argv(M.CASES[0], RUN, RUN, 'B', root)
        self.assertEqual(record[1:4], ['ros2', 'bag', 'record'])
        self.assertEqual(record[record.index('--max-cache-size') + 1], '0')
        play = M.recordplay_argv(M.CASES[1], RUN, RECORD, 'B', root)
        self.assertIn('/bag_' + RECORD + ':=/bagplay_' + RUN, play)
        self.assertIn('--start-paused', play)
        self.assertEqual(play[play.index('--wait-for-all-acked') + 1], '5000')
        for case, record_run in ((M.CASES[0], RECORD), (M.CASES[1], RUN), ('unknown', RUN)):
            with self.assertRaises(ValueError):
                M.recordplay_argv(case, RUN, record_run, 'B', root)

    def test_normal_recorder_term_is_exactly_once_with_persisted_intent(self):
        self.map_receipts()
        self.r.normal_recorder_stop(self.recording_text(), self.business())
        self.assertTrue(self.r.normal_stop_confirmed)
        self.assertEqual(self.commands, [('B', 'kill -TERM 22')])
        self.assertTrue((self.output / 'NORMAL_STOP_INTENT.json').is_file())
        with self.assertRaises(ValueError):
            self.r.normal_recorder_stop(self.recording_text(), self.business())
        self.assertEqual(len(self.commands), 1)

    def test_stop_missing_business_maps_or_cli_readiness_has_no_signal(self):
        for texts, business in ((self.recording_text(), None), ({'B': ''}, self.business()), (self.recording_text(), self.business())):
            with self.subTest(texts=texts):
                with self.assertRaises(ValueError):
                    self.r.normal_recorder_stop(texts, business)
        self.assertEqual(self.commands, [])
        self.assertFalse(self.r.normal_stop_attempted)

    def test_ambiguous_term_never_repeats_or_sends_b_control(self):
        self.map_receipts()
        def uncertain(*args):
            raise OSError('uncertain dispatch')
        self.r.shell = uncertain
        with self.assertRaises(OSError):
            self.r.normal_recorder_stop(self.recording_text(), self.business())
        self.assertTrue(self.r.normal_stop_attempted)
        self.assertFalse(self.r.normal_stop_confirmed)
        self.r.shell = self.shell
        self.r.owned = {'B': self.r.owned['B']}
        self.r.snapshot = lambda *args: {'pid': 21}
        with patch.object(M.time, 'monotonic', self.monotonic), patch.object(M.time, 'sleep'):
            self.r.cleanup(True)
        self.assertEqual(self.commands, [])
        cleanup = M.LOAD(self.output / 'CLEANUP.json')['sides']['B']
        self.assertFalse(cleanup['absent'])
        self.assertEqual(cleanup['forced_signals'], [])
        self.assertTrue(self.r.blocked)

    def test_partial_launch_never_signals_unknown_child(self):
        self.r.owned = {'B': {'supervisor': {'pid': 21}}}
        self.r.launch_pending = {'B': {'state': 'UNKNOWN'}}
        self.r.cleanup(True)
        self.assertEqual(self.commands, [])
        self.assertTrue(self.r.blocked)

    def test_cleanup_observation_tail_advances_to_guarded_term(self):
        self.r.owned = {'A': self.r.owned['A']}
        clock, terminated, remaining = [0.0], [False], []
        def guard(side, role='child', absent_ok=False):
            remaining.append(self.r.stage_deadline - clock[0])
            self.assertGreaterEqual(remaining[-1], 1)
            clock[0] += 0.31
            return not terminated[0]
        def shell(side, command, *args):
            self.commands.append((side, command))
            self.assertGreater(self.r.stage_deadline - clock[0], 1)
            if command == 'kill -TERM 22':
                terminated[0] = True
            return b'0'
        self.r.guard, self.r.shell = guard, shell
        with patch.object(M.time, 'monotonic', lambda: clock[0]), \
                patch.object(M.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            self.r.cleanup(False)
        cleanup = M.LOAD(self.output / 'CLEANUP.json')['sides']['A']
        self.assertEqual(self.commands, [('A', 'kill -TERM 22')])
        self.assertEqual(cleanup['forced_signals'], ['TERM'])
        self.assertTrue(cleanup['absent'])
        self.assertFalse(self.r.blocked)

    def test_cleanup_exit_race_requires_separate_absence_and_never_masks_identity_or_hdci(self):
        for message in ('Remote setup/capture command exit 1', 'Owned process identity capture incomplete'):
            with self.subTest(message=message):
                self.r.guard = lambda *a, **k: (_ for _ in ()).throw(OSError(message))
                self.r.shell = lambda *a: b'ABSENT'
                self.assertFalse(self.r.cleanup_observe('A'))
                self.r.shell = lambda *a: b'PRESENT'
                with self.assertRaisesRegex(OSError, message):
                    self.r.cleanup_observe('A')
        for message in ('Owned PID identity changed; refusing action', 'HDC infrastructure/capture failed; see receipt'):
            with self.subTest(message=message), patch.object(self.r, 'shell') as shell:
                self.r.guard = lambda *a, **k: (_ for _ in ()).throw(OSError(message))
                with self.assertRaisesRegex(OSError, message):
                    self.r.cleanup_observe('A')
                shell.assert_not_called()

    def test_cleanup_real_rpc_timeout_blocks_without_signal_retry(self):
        self.r.owned = {'A': self.r.owned['A']}
        self.r.guard = lambda *a, **k: (_ for _ in ()).throw(OSError('HDC infrastructure/capture failed; see timedout'))
        self.r.cleanup(False)
        self.assertEqual(self.commands, [])
        self.assertIn('HDC infrastructure', self.r.blocked[0])

    def test_cleanup_supervisor_sleep_cannot_dispatch_in_subsecond_tail(self):
        self.r.owned = {'A': self.r.owned['A']}
        clock, observations = [100.0], []
        def observe(side, role='child'):
            observations.append((role, self.r.stage_deadline - clock[0]))
            if role == 'child':
                return False
            clock[0] += 1.95
            return True
        self.r.cleanup_observe = observe
        with patch.object(M.time, 'monotonic', lambda: clock[0]), \
                patch.object(M.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
            self.r.cleanup(False)
        self.assertTrue(all(remaining >= 1 for role, remaining in observations))
        self.assertEqual(sum(role == 'supervisor' for role, remaining in observations), 1)
        self.assertFalse(M.LOAD(self.output / 'CLEANUP.json')['sides']['A']['absent'])
        self.assertTrue(self.r.blocked)

    def test_cleanup_observer_reserves_budget_for_each_rpc(self):
        self.r.stage_deadline = 10
        with patch.object(M.time, 'monotonic', return_value=9.1), \
                patch.object(self.r, 'guard') as guard, patch.object(self.r, 'shell') as shell:
            with self.assertRaisesRegex(OSError, 'observation budget exhausted'):
                self.r.cleanup_observe('A')
            guard.assert_not_called()
            shell.assert_not_called()
        with patch.object(M.time, 'monotonic', side_effect=[8.0, 9.1]), \
                patch.object(self.r, 'guard', side_effect=OSError('Remote setup/capture command exit 1')), \
                patch.object(self.r, 'shell') as shell:
            with self.assertRaisesRegex(OSError, 'absence confirmation budget exhausted'):
                self.r.cleanup_observe('A')
            shell.assert_not_called()

    def test_resume_exact_owned_single_use_control(self):
        self.r.case, self.r.record_run, self.r.roles['A'] = M.CASES[1], RECORD, 'bag-subscriber'
        self.map_receipts()
        business = self.business(self.rows('bag-subscriber')[:3], role='bag-subscriber')
        self.r.resume_playback(business)
        self.assertEqual(len(self.commands), 1)
        self.assertIn('control.BAG_RESUME', self.commands[0][1])
        self.assertIn('pid=22 record_run=' + RECORD, self.commands[0][1])
        with self.assertRaises(ValueError):
            self.r.resume_playback(business)
        self.assertEqual(len(self.commands), 1)

    def test_collection_requires_normal_absence_before_any_remote_read(self):
        self.r.write('CLEANUP.json', {'sides': {'A': {'absent': False}}})
        with self.assertRaisesRegex(ValueError, 'normal process absence'):
            self.r.closed_bag()
        self.assertEqual(self.commands, [])

    def test_real_oracle_accepts_empty_native_metadata_but_rejects_other_values(self):
        fixture = ORACLE_FIXTURE['BagSqliteOracleControls']()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        missing = object()
        for value in (None, {}, missing, {'unexpected': 'data'}, [], '', False, 0):
            with self.subTest(value=value):
                if value is missing:
                    fixture.metadata.pop('custom_data', None)
                else:
                    fixture.metadata['custom_data'] = value
                fixture.sql('UPDATE metadata SET metadata=? WHERE id=2',
                    (ORACLE_FIXTURE['yaml'].safe_dump(fixture.metadata),))
                fixture.write_metadata()
                fixture.rehash()
                if value is None or type(value) is dict and not value:
                    result = M.ORACLE['verify_recorded_bag'](fixture.root, fixture.expected)
                    self.assertEqual(result['status'], 'PASS_CLOSED_BAG_CONTENT_ONLY')
                    self.assertFalse(result['device_execution_verified'])
                else:
                    with self.assertRaisesRegex(M.ORACLE['BagContentError'], 'custom metadata'):
                        M.ORACLE['verify_recorded_bag'](fixture.root, fixture.expected)

    def test_collection_and_replay_source_call_real_oracle(self):
        # Only remote I/O and device receipts are fixtures. The actual collection,
        # filesystem descriptors, SQLite oracle and replay-source consumer run.
        fixture = ORACLE_FIXTURE['BagSqliteOracleControls']()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        temporary = tempfile.TemporaryDirectory(dir=M.OUTPUT_ROOT, prefix='bag-recordplay-host-fixture-')
        self.addCleanup(temporary.cleanup)
        self.r.output = Path(temporary.name)
        self.r.write('CLEANUP.json', {'sides': {side: {'absent': True, 'forced_signals': []} for side in 'AB'}})
        for name in ('NORMAL_STOP_INTENT.json', 'NORMAL_STOP_DISPATCH.json', 'CHILD_EXITS.json'):
            self.r.write(name, {'host_fixture_only': True})
        self.r.write('SOURCE_EXPECTATIONS.json', fixture.expected)
        inventory = {name: {'bytes': (fixture.root / name).stat().st_size, 'sha256': digest}
                     for name, digest in fixture.expected['files'].items()}
        self.r.remote_bag_inventory = lambda root: copy.deepcopy(inventory)
        def receive(side, argv, timeout):
            self.assertEqual((side, argv[:2], timeout), ('B', ['file', 'recv'], 30))
            shutil.copyfile(fixture.root / Path(argv[2]).name, argv[3])
            return b'transfer finish'
        self.r.command = receive
        self.r.closed_bag()
        content = M.LOAD(self.r.output / 'BAG_CONTENT.json')
        self.assertEqual(content['message_count'], 10)
        self.assertFalse(content['device_execution_verified'])
        result = {'host_fixture_only': True, 'result': 'PASS', 'scope': 'TARGET_BAG_RECORDPLAY_BOUNDED',
            'case': M.CASES[0], 'source_sha256': M.SHA(SOURCE), 'run': RUN,
            'failures': [], 'blocked': [], 'communication_verified': True, 'record_play': 'RECORD_ONLY_PASS',
            'runtime_sha256': '1' * 64, 'evidence': {str(p): M.SHA(p) for p in self.r.output.iterdir() if p.is_file()}}
        path = self.r.output / 'RESULT.json'
        self.r.write('RESULT.json', result)
        admitted = M.recordplay_source_record({'path': str(path), 'sha256': M.SHA(path)}, {})
        self.assertEqual(admitted['run'], RUN)
        self.assertEqual(admitted['collection'], self.r.bag_receipt)
        result['result'], result['blocked'] = 'BLOCKED', ['host fixture failure']
        self.r.write('RESULT.json', result)
        with self.assertRaisesRegex(ValueError, 'Successful actual recording'):
            M.recordplay_source_record({'path': str(path), 'sha256': M.SHA(path)}, {})

    def test_remote_bag_rejects_sidecars_links_and_unsafe_names(self):
        valid = 'FILE 200:1\n' + 'a' * 64 + '  metadata.yaml\nFILE 300:1\n' + 'b' * 64 + '  bag_0.db3\n'
        self.r.shell = lambda *args: valid.encode()
        inventory = self.r.remote_bag_inventory(self.r.prefix['B'] + '/bag')
        self.assertEqual(set(inventory), {'metadata.yaml', 'bag_0.db3'})
        for raw in (valid + 'extra\n', valid.replace(':1', ':2'), valid.replace('bag_0.db3', '../bad.db3'),
                    valid.replace('metadata.yaml', 'bag_0.db3'), valid.replace('bag_0.db3', 'bag_0.db3-wal')):
            with self.subTest(raw=raw):
                self.r.shell = lambda *args: raw.encode()
                with self.assertRaises(ValueError):
                    self.r.remote_bag_inventory(self.r.prefix['B'] + '/bag')

    def test_readiness_wrapper_without_producer_gates_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'Complete independent gates'):
            M.recordplay_gates({'status': 'PASS'}, {}, {}, {}, {})

    def gate_fixture(self, form):
        def save(path, value):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value), encoding='utf-8')
            return {'path': str(path), 'sha256': M.SHA(path)}
        names = ('integrations/ros2/target/compose_bag_recordplay_runtime.py',
                 'tests/ros2_target/test_bag_recordplay_composition.py',
                 'integrations/ros2/target/bag_recordplay_probe.py',
                 'tests/ros2_target/test_bag_recordplay_probe.py')
        sources = {name: '1' * 64 for name in names}
        sources['tests/ros2_target/run_actionbag_bag_recordplay.py'] = '2' * 64
        runtime = save(self.output / 'runtime.json', {})
        freeze = save(self.output / 'freeze.json', {'sources': sources})
        gates = {}
        for family in ('composition', 'harness'):
            selected = sources if family == 'harness' else {name: sources[name] for name in names}
            frozen = freeze if family == 'harness' else save(self.output / 'composition-freeze.json', {'sources': selected})
            gates[family] = {}
            descriptors = {}
            for stage in ('initial', 'review', 'final'):
                folder = self.output / family / stage
                result = {'status': 'PASS', 'scope': 'BAG_RECORDPLAY_HARNESS_HOST_ONLY' if family == 'harness'
                    else 'TWO_FILE_BAG_RECORDPLAY_STATIC_DERIVATION', 'sources': selected, 'source_freeze': frozen,
                    'runtime_receipt': runtime, 'utc': '2026-10-18T06:00:00+00:00'}
                if stage == 'initial': result['initial_gate'] = 'PASS'
                if stage == 'review': result.update(blocking_findings=[], assessment='NO_ACTIONABLE_FINDINGS_FOUND')
                if stage == 'final': result['host_final'] = 'PASS'
                for previous in ('initial', 'review'):
                    if previous in descriptors: result[previous + '_result'] = descriptors[previous]
                descriptor = save(folder / 'RESULT.json', result)
                descriptors[stage] = descriptor
                members = {'RESULT.json': descriptor['sha256']} if form == 'dict' else [descriptor]
                gates[family][stage] = {'result': descriptor, 'seal': save(folder / 'SEAL.json', {'files': members})}
        return gates, sources, runtime, freeze

    def test_producer_gates_accept_real_relative_seals_and_descriptor_lists(self):
        for form in ('dict', 'list'):
            with self.subTest(form=form), patch.object(M.COMPOSER, 'ROOT', self.output):
                args = self.gate_fixture(form)
                bindings = {}
                M.recordplay_gates(*args, bindings)
                self.assertIn(args[0]['composition']['initial']['result']['path'], bindings)
                self.assertIn(args[0]['harness']['final']['result']['path'], bindings)

    def test_producer_seal_rejects_escape_duplicate_and_unsealed_result(self):
        for members in ({'../outside': 'a' * 64}, {'C:/outside': 'a' * 64}, {'': 'a' * 64},
                        {'unused.json': 'a' * 64}, 'duplicate'):
            with self.subTest(members=members), patch.object(M.COMPOSER, 'ROOT', self.output):
                args = self.gate_fixture('dict')
                gate = args[0]['composition']['initial']
                path = Path(gate['seal']['path'])
                if members == 'duplicate': members = [gate['result'], gate['result']]
                if isinstance(members, dict) and 'unused.json' in members:
                    unused = path.parent / 'unused.json'
                    unused.write_text('{}', encoding='utf-8')
                    members = {'unused.json': M.SHA(unused)}
                path.write_text(json.dumps({'files': members}), encoding='utf-8')
                gate['seal']['sha256'] = M.SHA(path)
                with self.assertRaises(ValueError):
                    M.recordplay_gates(*args, {})

    def test_real_constructor_admits_bound_routes_without_abi_constructor(self):
        def save(name, value):
            path = self.output / name
            path.write_text(json.dumps(value), encoding='utf-8')
            return {'path': str(path), 'sha256': M.SHA(path)}
        routes = {'observed_utc': M.datetime.now(M.timezone.utc).isoformat(), 'cross_confirmed': True,
                  'bindings': {}, 'sides': {}}
        for side in 'AB':
            raw, udid = self.output / (side + '.stdout'), self.output / (side + '.udid')
            raw.write_text(side, encoding='utf-8'); udid.write_text(side, encoding='utf-8')
            routes['bindings'][str(raw)] = M.SHA(raw)
            routes['sides'][side] = {'target': M.NATIVE.TARGETS[side], 'host_exit': 0, 'remote_exit': 0,
                'stdout_file': str(raw), 'stdout_sha256': M.SHA(raw), 'udid_file': str(udid), 'udid_sha256': M.SHA(udid)}
        plan = {'schema': 1, 'case': M.CASES[0], 'run': RUN, 'recording': None,
            'package': str(self.output / 'package'), 'files': {}, 'selection_file': {'path': 'fixture-selection'},
            'inputs': {'external': save('external.json', {}), 'readiness': save('readiness.json', {}),
                'identity': save('identity.json', {'main_execution_result': 'PASS_IDENTITY_READINESS_ONLY',
                    'boards': [{'side': 'A', 'new_token': 672006291}, {'side': 'B', 'new_token': 672042405}]}),
                'routes': save('routes.json', routes)}}
        prepared = Path(save('prepared.json', plan)['path'])
        args = NS(output=self.output / ('bag-recordplay-run-' + RUN + '-execution'), prepared=prepared,
                  hdc=Path('C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/toolchains/hdc.exe'))
        def parse(raw):
            return {'local': raw, 'remotes': ['B' if raw == 'A' else 'A'], 'udid_bytes': 1}
        with patch.object(M, 'recordplay_prepare', return_value=plan), \
                patch.object(M, 'OUTPUT_ROOT', self.output), \
                patch.object(M.NATIVE, 'actionbag_parse_peer', side_effect=parse), \
                patch.object(M.BAG.BagCliRunner, '__init__', side_effect=AssertionError('ABI constructor called')):
            runner = M.BagRecordPlayRunner(args)
        self.assertEqual(runner.roles, {'A': 'bag-publisher', 'B': None})
        self.assertFalse(runner.normal_stop_attempted)
        self.assertEqual(runner.udids, {'A': 'A', 'B': 'B'})
        self.assertTrue((args.output / 'PLAN.json').is_file())
        self.assertIn('--disable-keyboard-controls', runner.argv('B'))

    def test_output_boundaries_reject_before_any_input_or_path_io(self):
        valid = 'bag-recordplay-run-' + RUN + '-prepared'
        paths = [self.output / 'package' / valid, self.output / 'old-evidence' / valid,
                 self.output / 'recording' / valid, self.output / valid / 'nested',
                 self.output / '..' / self.output.name / valid, self.output / 'unscoped',
                 self.output / valid.replace(RUN, 'short'), self.output / (valid + '.bak')]
        with patch.object(M, 'OUTPUT_ROOT', self.output), patch.object(M.BASE, 'runtime_path') as resolver, \
                patch.object(M, 'recordplay_prepare') as prepare, patch.object(M, 'LOAD') as load, \
                patch.object(M.BAG, 'bag_selected_hdc') as hdc:
            for path in paths:
                with self.subTest(path=str(path)):
                    with self.assertRaisesRegex(ValueError, 'fresh named direct child'):
                        M.recordplay_output(path, 'prepared')
                    with self.assertRaisesRegex(ValueError, 'fresh named direct child'):
                        M.BagRecordPlayRunner(NS(output=path))
                    with patch.object(sys, 'argv', ['runner', 'prepare', '--inputs', 'unread', '--output', str(path)]):
                        with self.assertRaisesRegex(ValueError, 'fresh named direct child'):
                            M.recordplay_main()
            resolver.assert_not_called(); prepare.assert_not_called(); load.assert_not_called(); hdc.assert_not_called()

    def test_fresh_output_is_mode_specific_and_existing_paths_are_rejected(self):
        path = self.output / ('bag-recordplay-run-' + RUN + '-prepared')
        with patch.object(M, 'OUTPUT_ROOT', self.output):
            self.assertEqual(M.recordplay_output(path, 'prepared'), path)
            self.assertFalse(path.exists())
            with self.assertRaises(ValueError): M.recordplay_output(path, 'execution')
            with self.assertRaises(ValueError): M.recordplay_output(path, 'unknown')
            path.mkdir()
            with self.assertRaisesRegex(ValueError, 'Fresh recordplay output'):
                M.recordplay_output(path, 'prepared')
            self.assertEqual(list(path.iterdir()), [])

    def test_prepare_creates_only_admitted_output_and_uses_exclusive_directory(self):
        path = self.output / ('bag-recordplay-run-' + RUN + '-prepared')
        argv = ['runner', 'prepare', '--inputs', 'host-fixture', '--output', str(path)]
        with patch.object(M, 'OUTPUT_ROOT', self.output), patch.object(sys, 'argv', argv), \
                patch.object(M, 'recordplay_prepare', return_value={'fixture_only': True}) as prepare:
            self.assertEqual(M.recordplay_main(), 0)
            self.assertEqual(M.LOAD(path / 'PLAN.json'), {'fixture_only': True})
            with self.assertRaisesRegex(ValueError, 'Fresh recordplay output'):
                M.recordplay_main()
            self.assertEqual(prepare.call_count, 1)

    def test_workflow_record_and_play_control_order(self):
        for case in M.CASES:
            with self.subTest(case=case):
                self.r.case = case
                self.r.record_run = RUN if case == M.CASES[0] else RECORD
                role = 'bag-publisher' if case == M.CASES[0] else 'bag-subscriber'
                self.r.roles['A'] = role
                self.r.normal_stop_attempted = self.r.normal_stop_confirmed = self.r.resume_attempted = False
                self.r.finished_fixtures, self.r.closed = set(), set()
                events = []
                self.commands.clear()
                self.r.launch = lambda side: events.append(('launch', side))
                def poll(side):
                    if side == 'B': return self.recording_text()['B'], ''
                    rows = self.rows(role)
                    if case == M.CASES[1] and not self.r.resume_attempted: rows = rows[:3]
                    return self.text(rows), ''
                self.r.poll = poll
                self.r.ready = lambda side, text, phase: phase == 'LISTENER' or (
                    side == 'A' and side in self.r.finished_fixtures) or (side == 'B' and
                    (self.r.normal_stop_confirmed if case == M.CASES[0] else self.r.resume_attempted))
                def maps(side, phase):
                    events.append(('maps', side, phase))
                    if phase == 'business': self.r.write(side + '.business.providers.json', {})
                self.r.maps = maps
                self.r.publish = lambda side, name, data: events.append(('release', side, name))
                self.r.finish_fixture = lambda side, reason: self.r.finished_fixtures.add(side)
                self.r.wait_exit = lambda budget: events.append(('exit', budget))
                with patch.object(M.time, 'sleep'):
                    self.r.workflow()
                self.assertEqual(events[-1], ('exit', 20))
                self.assertEqual(self.r.closed, {'A', 'B'})
                self.assertEqual(len(self.commands), 1)
                self.assertEqual(self.r.normal_stop_confirmed, case == M.CASES[0])
                self.assertEqual(self.r.resume_attempted, case == M.CASES[1])
                self.assertLess(events.index(('maps', 'B', 'business')), events.index(('maps', 'A', 'close')))
                self.assertEqual([e for e in events if e[0] == 'release'][-2:],
                    [('release', 'A', 'control.RUNTIME_CLOSE'), ('release', 'B', 'control.RUNTIME_CLOSE')])
                clock = M.LOAD(self.output / 'CLOCK.json')
                self.assertEqual(clock['business_deadline'] - clock['business_origin'], 35)
                self.assertEqual(clock['normal_close_release_deadline'] - clock['business_origin'], 50)
                # Separate cases use distinct evidence directories in actual execution.
                for name in ('NORMAL_STOP_INTENT.json', 'NORMAL_STOP_DISPATCH.json'):
                    (self.output / name).unlink(missing_ok=True)

    def test_expectations_use_actual_graph_qos_and_packaged_definition(self):
        member = 'share/std_msgs/msg/String.msg'
        path = self.output / member
        path.parent.mkdir(parents=True)
        path.write_bytes(b'string data\n')
        business = self.business()
        definition = business['single']['BAG_DEFINITION']
        definition['source_sha256'] = M.SHA(path)
        qos = {'history': 'unknown', 'depth': 0, 'reliability': 'reliable', 'durability': 'volatile',
            'liveliness': 'automatic', 'avoid_ros_namespace_conventions': False,
            **{name: {'sec': 0, 'nsec': 0} for name in ('deadline', 'lifespan', 'liveliness_lease_duration')}}
        business['single']['BAG_PUBLISHER_INFO'].update(node_name='bagpub_' + RUN, node_namespace='/',
            topic_type='std_msgs/msg/String', gid='1' * 32, type_description_hash='', offered_qos_profiles=[qos])
        expected = M.recordplay_expectations(business, RUN, self.output, {member: M.SHA(path)})
        self.assertEqual(expected['offered_qos_profiles'][0]['depth'], 0)
        self.assertEqual(expected['ros_distro'], '')
        for key, value in (('depth', 10), ('history', 'keep_last'), ('depth', False)):
            changed = copy.deepcopy(business)
            changed['single']['BAG_PUBLISHER_INFO']['offered_qos_profiles'][0][key] = value
            with self.assertRaisesRegex(ValueError, 'graph QoS'):
                M.recordplay_expectations(changed, RUN, self.output, {member: M.SHA(path)})

    def test_unknown_case_refused_before_package_or_device_work(self):
        with patch.object(M, 'LOAD', return_value={'schema': 1, 'case': 'imports_cdr_pair'}), \
                patch.object(M.BASE, 'runtime_inventory') as inventory:
            with self.assertRaisesRegex(ValueError, 'two fixed'):
                M.recordplay_prepare(self.output / 'invalid.json')
            inventory.assert_not_called()

    def test_optional_flow_pair_only_in_exact_original_stdout_phase(self):
        stdout = 'ROS_CONNECT_ALLOWED run=' + RUN + '\n' + '\n'.join(M.ORDINARY.ERROR_PAIR) + '\n' + \
                 'ROS_RUNTIME_CLOSE_READY schema=1 run=' + RUN + ' side=A pid=22\n'
        stderr = '\n'.join('FDDST_ENDPOINT topic=ros_discovery_info role=' + role + ' data_sharing=OFF' for role in ('writer', 'reader', 'reader'))
        self.assertEqual(M.recordplay_errors(stdout, stderr, RUN, 'A', 22, self.files)['actual_pairs'], 1)
        for out, err in ((stdout, stderr + '\nERROR other'), (stdout + '\nERROR other', stderr),
                         (stdout.replace(M.ORDINARY.ERROR_PAIR[0], ''), stderr),
                         (stdout.replace('ROS_CONNECT_ALLOWED', 'MISSING'), stderr)):
            with self.subTest(out=out, err=err), self.assertRaises((ValueError, OSError)):
                M.recordplay_errors(out, err, RUN, 'A', 22, self.files)


if __name__ == '__main__':
    unittest.main()
