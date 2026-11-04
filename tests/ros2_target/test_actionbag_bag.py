"""Host adversarial controls only; synthetic records never prove target ABI."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('bag_runner_under_test', Path(__file__).with_name('run_actionbag_bag.py'))
BAG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BAG)


class BagAbiControls(unittest.TestCase):
    def setUp(self):
        self.run, self.side, self.pid = '1234567890abcdef', 'A', 1234
        self.package = '/data/local/tmp/mdds_dds_' + self.run + '_A/package'
        names = {'rosbag2_py.' + name for name in BAG.MODULES} | {
            'rclpy._rclpy_pybind11', 'ros2bag.verb.record', 'ros2bag.verb.play', 'ros2bag_sqlite3_cli'}
        self.files, origins = {}, {}
        for name in names:
            member = 'python/' + name.replace('.', '/') + '.so'
            self.files[member] = hashlib.sha256(name.encode()).hexdigest()
            origins[name] = {'path': self.package + '/' + member, 'sha256': self.files[member]}
        value = ('r25bag_' + self.run).encode() + b'\0'
        payloads = [b'\0\1\0\0' + len(value).to_bytes(4, 'little') + value,
                    b'\0\1\0\0\0', b'\0\1\0\0\0']
        cdr = [{'type': name, 'bytes': len(data), 'base64': base64.b64encode(data).decode(),
                'sha256': hashlib.sha256(data).hexdigest()} for name, data in zip(
                    ['String', 'Resume.Request', 'Resume.Response'], payloads)]
        qos = [dict(history=1, depth=depth, reliability=reliability, durability=durability,
            liveliness=liveliness, deadline={'sec': 2, 'nsec': 13}, lifespan={'sec': 3, 'nsec': 17},
            liveliness_lease_duration={'sec': 4, 'nsec': 19}, avoid_ros_namespace_conventions=avoid)
            for depth, reliability, durability, liveliness, avoid in [(7, 1, 2, 1, False), (11, 2, 1, 3, True)]]
        fields = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
        self.row = {'schema': 1, 'run': self.run, 'side': self.side, 'pid': self.pid, 'seq': 1,
            'role': 'bag-abi', 'event': 'BAG_ABI_READY', 'node_created': False,
            'soabi': 'cpython-312-aarch64-linux-ohos', 'origins': origins,
            'capsules': {**{name: fields for name in ['String', 'Resume.Request', 'Resume.Response']}, 'Resume': ['_TYPE_SUPPORT']},
            'cdr': cdr, 'qos_setters': 'NATIVE_CAST_COMPLETED_NOT_MAP_READBACK', 'qos_expected': qos,
            'qos_native_roundtrip': copy.deepcopy(qos), 'presets': [['none', 'default'], ['resilient', 'durable']],
            'preset_default': 'none', 'invalid_preset_exit': 2, 'playback_paused': True, 'playback_ack_timeout_ms': 5000}

    def text(self, rows=None):
        return '\n'.join('R25_PROBE ' + json.dumps(row) for row in (rows if rows is not None else [self.row]))

    def check(self, final=False, rows=None):
        return BAG.bag_abi_business(self.text(rows), self.run, self.side, self.pid, self.files, self.package, final=final)

    def tail(self):
        common = {key: self.row[key] for key in ('schema', 'run', 'side', 'pid', 'role')}
        return [self.row, {**common, 'seq': 2, 'event': 'FINISH_ACCEPTED', 'reason': 'NORMAL_COMPLETION'},
                {**common, 'seq': 3, 'event': 'FINAL', 'success': True, 'node_created': False}]

    def test_complete_synthetic_envelope(self):
        self.assertTrue(self.check(final=True, rows=self.tail()))

    def test_pending_is_not_final_success(self):
        self.assertFalse(BAG.bag_abi_business('', self.run, self.side, self.pid, self.files, self.package))
        with self.assertRaises(ValueError):
            BAG.bag_abi_business('', self.run, self.side, self.pid, self.files, self.package, final=True)
        with self.assertRaises(ValueError):
            self.check(final=True)

    def test_wrong_identity_and_node_creation(self):
        for key, value in [('pid', True), ('run', '0' * 16), ('role', 'publisher'), ('node_created', True)]:
            with self.subTest(key=key):
                original = self.row[key]
                self.row[key] = value
                with self.assertRaises(ValueError):
                    self.check()
                self.row[key] = original

    def test_missing_native_roundtrip_not_getter_equality(self):
        self.row['qos_native_roundtrip'] = []
        with self.assertRaises(ValueError):
            self.check()

    def test_one_wrong_qos_duration(self):
        self.row['qos_native_roundtrip'][0]['deadline']['nsec'] += 1
        with self.assertRaises(ValueError):
            self.check()

    def test_unselected_origin_and_missing_extension(self):
        name = 'rosbag2_py._reader'
        original = self.row['origins'][name]
        self.row['origins'][name] = {**original, 'path': '/system/lib64/_reader.so'}
        with self.assertRaises(ValueError):
            self.check()
        del self.row['origins'][name]
        with self.assertRaises(ValueError):
            self.check()

    def test_self_consistent_wrong_cdr_payload_rejected(self):
        data = base64.b64decode(self.row['cdr'][0]['base64'])[:-2] + b'x\0'
        self.row['cdr'][0].update(base64=base64.b64encode(data).decode(), sha256=hashlib.sha256(data).hexdigest())
        with self.assertRaises(ValueError):
            self.check()

    def test_wrong_parser_defaults_and_unbounded_ack(self):
        for key, value in [('preset_default', 'n'), ('invalid_preset_exit', 0), ('playback_ack_timeout_ms', 0), ('playback_paused', False)]:
            original = self.row[key]
            self.row[key] = value
            with self.assertRaises(ValueError):
                self.check()
            self.row[key] = original

    def test_finish_failure_and_duplicate_readiness(self):
        rows = self.tail()
        rows[1]['reason'] = 'WINDOW_FAILED'
        with self.assertRaises(ValueError):
            self.check(final=True, rows=rows)
        with self.assertRaises(ValueError):
            self.check(rows=[self.row, {**self.row, 'seq': 2}])

    def test_duplicate_json_keys_fail(self):
        text = self.text().replace('"schema": 1', '"schema": 1, "schema": 1')
        with self.assertRaises(ValueError):
            BAG.bag_records(text, self.run, self.side, self.pid)

    def test_record_play_not_admitted_by_prepare(self):
        with tempfile.TemporaryDirectory(prefix='r25-bag-controls-') as temp:
            path = Path(temp) / 'input.json'
            path.write_text(json.dumps({'schema': 1, 'case': 'bag_record_A_to_B'}), encoding='utf-8')
            with self.assertRaises(ValueError):
                BAG.bag_prepare(path)

    def test_dispatch_uses_same_exec_wrapper_without_another_node(self):
        runner = BAG.BagCliRunner.__new__(BAG.BagCliRunner)
        runner.run = self.run
        runner.prefix = {'A': self.package[:-8]}
        argv = runner.argv('A')
        self.assertEqual(argv[0], self.package + '/run-token-first')
        self.assertEqual(argv[1:4], ['--script', self.package + '/scripts/bag_runtime_probe.py', '--role'])
        self.assertIn('bag-abi', argv)
        self.assertNotIn('ros2', argv)

    def gate_fixture(self, root, mutation=None):
        def write(name, value):
            path = root / (name + '.json')
            path.write_text(json.dumps(value), encoding='utf-8')
            return {'path': str(path), 'sha256': BAG.bag_sha(path)}

        sources = {str(path.resolve()): BAG.bag_sha(path) for path in BAG.SOURCE_PATHS}
        relative = {Path(path).relative_to(BAG.REPO).as_posix(): sha for path, sha in sources.items()}
        composition = {path: sha for path, sha in relative.items() if path.endswith(
            ('/compose_actionbag_bag_runtime.py', '/bag_runtime_probe.py'))}
        runtime = write('runtime', {'synthetic': True})
        freeze = write('freeze', {'schema': 1, 'scope': 'BAG_ABI_SOURCE_FREEZE', 'sources': sources})
        compfreeze = write('composition-freeze', {'files': composition})
        gates, descriptors = {}, {}
        for name, (stage, scope, status) in BAG.GATE_ROLES.items():
            family, phase = name.split('_')
            result = {'stage': stage, 'scope': scope, 'status': status,
                'utc': '2026-10-18T00:00:0' + str(['initial', 'review', 'final'].index(phase)) + '+00:00',
                'source_freeze': copy.deepcopy(composition if family == 'composition' else relative),
                'source_freeze_descriptor': compfreeze if family == 'composition' else freeze,
                'runtime_receipt': runtime, 'build_result': runtime,
                'blocking_findings': [], 'new_findings': [], 'source_fixes_required': False}
            if phase in ('review', 'final'):
                result['initial_result'] = descriptors[family + '_initial']
            if phase == 'final':
                result['review_result'] = descriptors[family + '_review']
            if mutation:
                mutation(name, result, write)
            descriptors[name] = write(name, result)
            gates[name] = {'status': 'PASS', 'result': descriptors[name],
                'bindings': {descriptors[name]['path']: descriptors[name]['sha256']}}
        return gates, sources, runtime, freeze

    def gate_check(self, mutation=None):
        with tempfile.TemporaryDirectory(prefix='r25-bag-gates-') as temp:
            args = self.gate_fixture(Path(temp), mutation)
            BAG.bag_gate_results(*args, {})

    def test_complete_synthetic_producer_chain(self):
        self.gate_check()

    def test_pass_wrapper_does_not_promote_failed_command(self):
        def mutation(name, result, write):
            if name == 'composition_initial':
                result.clear()
                result.update(exit_code=1, stage='initial', status='PASS')
        with self.assertRaises(ValueError):
            self.gate_check(mutation)

    def test_ineligible_or_mislabelled_producers(self):
        for key, value in [('status', 'FAIL'), ('status', 'BLOCKED'), ('status', 'SKIPPED'),
                           ('stage', 'review'), ('scope', 'OTHER_TESTS')]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.gate_check(lambda name, result, write: result.update({key: value}) if name == 'harness_initial' else None)

    def test_self_consistent_old_source_chain(self):
        def mutation(name, result, write):
            if name.startswith('harness_'):
                result['source_freeze']['tests/ros2_target/run_actionbag_bag.py'] = '0' * 64
        with self.assertRaises(ValueError):
            self.gate_check(mutation)

    def test_wrong_runtime_and_full_freeze(self):
        for field in ('runtime_receipt', 'source_freeze_descriptor'):
            with self.subTest(field=field), self.assertRaises(ValueError):
                self.gate_check(lambda name, result, write: result.update({field: write('other', {'other': True})})
                               if name == 'harness_final' else None)

    def test_review_findings_must_be_closed(self):
        for key, value in [('blocking_findings', ['BAH-01']), ('new_findings', ['new']), ('source_fixes_required', True),
                           ('blocking_findings', None), ('new_findings', None)]:
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                self.gate_check(lambda name, result, write: result.update({key: value}) if name == 'harness_review' else None)

    def test_wrong_producer_chain_with_valid_hashes(self):
        for target, field in [('harness_review', 'initial_result'), ('harness_final', 'initial_result'),
                              ('harness_final', 'review_result'), ('composition_final', 'review_result')]:
            with self.subTest(target=target, field=field), self.assertRaises(ValueError):
                self.gate_check(lambda name, result, write: result.update({field: write('other-producer', {'status': 'PASS'})})
                               if name == target else None)

    def test_final_cannot_precede_review(self):
        with self.assertRaises(ValueError):
            self.gate_check(lambda name, result, write: result.update(utc='2026-10-17T00:00:00+00:00')
                           if name == 'harness_final' else None)

    def test_unselected_hdc_rejected_before_constructor(self):
        argv = ['runner', 'run', '--output', 'not-created', '--prepared', 'not-read', '--release', 'not-read',
                '--hdc', str(Path(tempfile.gettempdir()) / 'NOT_THE_APPROVED_HDC.exe')]
        with patch('sys.argv', argv), patch.object(BAG, 'BagCliRunner') as runner, self.assertRaises(ValueError):
            BAG.bag_runner_main()
        runner.assert_not_called()
        with self.assertRaises(ValueError):
            BAG.BagCliRunner(SimpleNamespace(hdc=Path(argv[-1])))

    def test_selected_hdc_reaches_only_gated_constructor(self):
        argv = ['runner', 'run', '--output', 'not-created', '--prepared', 'not-read', '--release', 'not-read',
                '--hdc', BAG.NATIVE.HDCPATH]
        with patch('sys.argv', argv), patch.object(BAG, 'BagCliRunner') as runner:
            runner.return_value.execute.return_value = 0
            self.assertEqual(BAG.bag_runner_main(), 0)
            runner.assert_called_once()


if __name__ == '__main__':
    unittest.main()
