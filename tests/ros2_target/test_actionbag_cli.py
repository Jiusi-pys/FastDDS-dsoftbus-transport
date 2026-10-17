"""Bounded synthetic Host controls only. No target imports or device execution."""
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import stat
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import yaml

SPEC = importlib.util.spec_from_file_location('ordinary_cli_host_subject', Path(__file__).with_name('run_actionbag_cli.py'))
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)
PSPEC = importlib.util.spec_from_file_location('ordinary_probe_host_subject', M.PROBE)
P = importlib.util.module_from_spec(PSPEC)
PSPEC.loader.exec_module(P)
RUN = '0123456789abcdef'
PIDS = {'A': 101, 'B': 202}
PLAN = json.loads((M.ROOT / 'build/r25-native-actionbag/ordinary-cli-design/VALIDATION_PLAN.json').read_text())


def ordinary_modeled_records(side, role, events):
    return '\n'.join('R25_PROBE ' + json.dumps({'schema': 1, 'run': RUN, 'side': side, 'pid': PIDS[side],
        'role': role, 'seq': i + 1, **event}, separators=(',', ':')) for i, event in enumerate(events))


class OrdinaryCliTests(unittest.TestCase):
    def test_records(self):
        text = ordinary_modeled_records('B', 'subscriber', [{'event': 'ENTITY_READY', 'entity': 'subscriber'},
            {'event': 'DATA', 'data': 'payload_' + RUN, 'matched': 1}])
        self.assertEqual(len(M.ordinary_records(text, RUN, 'B', 202, 'subscriber')), 2)
        for old, new in [('"seq":2', '"seq":1'), ('"schema":1', '"schema":true'), ('"pid":202', '"pid":203'), ('"role":"subscriber"', '"role":"publisher"')]:
            with self.subTest(old=old), self.assertRaises(ValueError):
                M.ordinary_records(text.replace(old, new), RUN, 'B', 202, 'subscriber')
        with self.assertRaises(ValueError): M.ordinary_records(text.replace('"schema":1', '"schema":1,"schema":1'), RUN, 'B', 202)
        with self.assertRaises(ValueError): M.ordinary_records(ordinary_modeled_records('B', 'subscriber', [{'event': 'FAIL', 'reason': 'wrong payload'}, {'event': 'DATA', 'data': 'correct'}]), RUN, 'B', 202)
        for case in M.CASES:
            for side in 'AB':
                argv, role = M.ordinary_cli_argv(case, RUN, side, '/data/local/tmp/mdds_dds_' + RUN + '_' + side, PLAN)
                self.assertTrue(argv[0].endswith('/package/run-token-first'))
                self.assertFalse(any('${' in x for x in argv))
                if role: self.assertIn('--finish-control', argv)
                elif 'graph' in case or 'echo' in case: self.assertIn('--no-daemon', argv)
        with self.assertRaises(ValueError): M.ordinary_cli_argv('action_graph_remote', RUN, 'A', '/bad', PLAN)
        with self.assertRaises(ValueError): M.ordinary_cli_argv('node_graph_AB', RUN, '', '/bad', PLAN)

    def test_business(self):
        entity = {'event': 'ENTITY_READY', 'entity': 'subscriber'}
        text = ordinary_modeled_records('B', 'subscriber', [entity, {'event': 'DATA', 'data': 'payload_' + RUN, 'matched': 1}])
        publication = "publisher: beginning loop\npublishing #1: std_msgs.msg.String(data='payload_" + RUN + "')\n"
        self.assertTrue(M.ordinary_business('topic_pub_AB', RUN, {'A': publication, 'B': text}, PIDS, PLAN))
        self.assertFalse(M.ordinary_business('topic_pub_AB', RUN, {'A': '', 'B': text}, PIDS, PLAN))
        wrong = ordinary_modeled_records('B', 'subscriber', [entity, {'event': 'DATA', 'data': 'wrong', 'matched': 1}, {'event': 'DATA', 'data': 'payload_' + RUN, 'matched': 1}])
        with self.assertRaises(ValueError): M.ordinary_business('topic_pub_AB', RUN, {'A': publication, 'B': wrong}, PIDS, PLAN)
        with self.assertRaises(ValueError): M.ordinary_business('topic_pub_AB', RUN, {'A': publication + publication, 'B': text}, PIDS, PLAN)
        publisher = ordinary_modeled_records('B', 'publisher', [{'event': 'ENTITY_READY', 'entity': 'publisher'},
            {'event': 'PUBLISH', 'data': 'payload_' + RUN, 'matched': 1}])
        self.assertTrue(M.ordinary_business('topic_echo_AB', RUN, {'A': 'data: payload_' + RUN + '\n---\n', 'B': publisher}, PIDS, PLAN))
        self.assertFalse(M.ordinary_business('topic_echo_AB', RUN, {'A': '', 'B': publisher}, PIDS, PLAN))  # Stock timeout can exit0.
        with self.assertRaises(ValueError): M.ordinary_business('topic_echo_AB', RUN, {'A': 'data: wrong\n---\n', 'B': publisher}, PIDS, PLAN)
        uuid = 'a' * 32
        server = ordinary_modeled_records('B', 'action-server', [{'event': 'ENTITY_READY', 'entity': 'action-server'},
            {'event': 'GOAL_ACCEPTED', 'uuid': uuid, 'order': 6}] +
            [{'event': 'FEEDBACK', 'uuid': uuid, 'sequence': M.FIBONACCI[:n]} for n in range(3, 8)] +
            [{'event': 'RESULT_INTENT', 'uuid': uuid, 'sequence': M.FIBONACCI, 'status': 'SUCCEEDED'}])
        output = 'Goal accepted with ID: ' + uuid + '\n\n'
        for name, sequence in [('Feedback', M.FIBONACCI[:n]) for n in range(3, 8)] + [('Result', M.FIBONACCI)]:
            output += name + ':\n    ' + yaml.safe_dump({'sequence': sequence}, sort_keys=False) + '\n'
        output += 'Goal finished with status: SUCCEEDED\n'
        self.assertTrue(M.ordinary_business('action_send_goal_AB', RUN, {'A': output, 'B': server}, PIDS, PLAN))
        # Preserve the actual stock formatting and mutate one YAML block at a
        # time. Wrong-before-correct must not disappear during construction.
        blocks = [('Feedback', M.FIBONACCI[:n]) for n in range(3, 8)] + [('Result', M.FIBONACCI)]
        offset = 0
        for name, sequence in blocks:
            marker = name + ':\n    ' + yaml.safe_dump({'sequence': sequence}, sort_keys=False) + '\n'
            position = output.index(marker, offset)
            offset = position + len(marker)
            for duplicate in [
                name + ':\n    sequence: [999]\n' + marker[len(name) + 6:],
                marker[:-1] + 'sequence: [999]\n\n',
                name + ':\n    sequence: ' + json.dumps(sequence) + '\nsequence: ' + json.dumps(sequence) + '\n\n',
                name + ':\n    {sequence: [999], sequence: ' + json.dumps(sequence) + '}\n\n',
                name + ':\n    "sequence": [999]\nsequence: ' + json.dumps(sequence) + '\n\n',
            ]:
                mutated = output[:position] + duplicate + output[position + len(marker):]
                with self.subTest(duplicate_yaml_block=name, sequence=sequence, duplicate=duplicate), self.assertRaises(ValueError):
                    M.ordinary_business('action_send_goal_AB', RUN, {'A': mutated, 'B': server}, PIDS, PLAN)
        self.assertEqual(M.ordinary_yaml('data: payload_' + RUN, 'data'), {'data': 'payload_' + RUN})
        for duplicate in ('data: wrong\ndata: payload_' + RUN, 'data: payload_' + RUN + '\ndata: wrong'):
            with self.assertRaises(ValueError): M.ordinary_yaml(duplicate, 'data')
        for bad in ('Action server is not available after waiting20 seconds.', 'Goal was rejected.', 'Timed out waiting for result after20 seconds.'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                M.ordinary_business('action_send_goal_AB', RUN, {'A': bad, 'B': server}, PIDS, PLAN)
        for bad in [output.replace(uuid, 'b' * 32), output.replace('status: SUCCEEDED', 'status: ABORTED'),
                    output + 'Feedback:\n    sequence: [0, 1]\n\n', output.replace('- 8', '- 9'),
                    output.replace('sequence:', 'other:')]:
            with self.subTest(action_bad=bad[-100:]), self.assertRaises(ValueError):
                M.ordinary_business('action_send_goal_AB', RUN, {'A': bad, 'B': server}, PIDS, PLAN)
        self.assertFalse(M.ordinary_business('action_send_goal_AB', RUN, {'A': '', 'B': server}, PIDS, PLAN))
        cdr = []
        for name, field, value in [('Goal', 'order', 6), ('Result', 'sequence', M.FIBONACCI),
                                  ('Feedback', 'sequence', M.FIBONACCI[:4]), ('String', 'data', 'payload_' + RUN)]:
            data = b'\0\1\0\0modeled-not-native-cdr'
            cdr.append({'type': name, 'field': field, 'value': value, 'bytes': len(data),
                'sha256': hashlib.sha256(data).hexdigest(), 'encapsulation': data[:4].hex(), 'base64': base64.b64encode(data).decode()})
        no_node = {s: ordinary_modeled_records(s, 'imports-cdr', [{'event': 'IMPORTS_CDR_READY', 'node_created': False,
            'soabi': 'cpython-312-aarch64-linux-ohos', 'cdr': cdr}]) for s in 'AB'}
        self.assertTrue(M.ordinary_business('imports_cdr_pair', RUN, no_node, PIDS, PLAN))
        with self.assertRaises(ValueError): M.ordinary_business('imports_cdr_pair', RUN, {**no_node, 'A': no_node['A'].replace('00010000', '00060000')}, PIDS, PLAN)

    def test_maps(self):
        package = '/data/local/tmp/mdds_dds_' + RUN + '_A/package'
        names = ['actionbag_python', 'lib/libpython3.12.so.1.0', 'python/rclpy/_rclpy_pybind11.cpython-312-aarch64-linux-ohos.so'] + [
            'lib/' + n for n in ('libfastrtps.so.2.14', 'libfastcdr.so.2', 'librmw_fastrtps_cpp.so', 'librmw_fastrtps_shared_cpp.so',
                'libfastdds_dsoftbus_ros2_target_bridge.so', 'libmdds_actionbag_token_bridge.so', 'libc++_shared.so')]
        external = {'/system/lib64/platformsdk/libsoftbus_client.z.so': 'f' * 64, '/system/lib64/shared module.so': 'e' * 64}
        lines = ['100-200 r-xp 0 00:00 1 ' + p for p in [*(package + '/' + n for n in names), *external]]
        lines += ['100-200 rw-p 0 00:00 0 [anon:ld-musl-aarch64.so.1.bss]']
        paths, anon = M.ordinary_map_paths('\n'.join(lines), package, dict.fromkeys(names, 'a' * 64), external, True, names[1])
        self.assertIn('/system/lib64/shared module.so', paths)
        self.assertEqual(anon, ['[anon:ld-musl-aarch64.so.1.bss]'])
        for bad in ['/system/lib64/unknown.so', '/data/local/tmp/ros2-generic/Lib/libfastrtps.so.2.14',
                    package + '/lib/libfastcdr.so.2 (deleted)', package.replace('/package', '') + '/libfastrtps.so.2.14', 'relative.so']:
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                M.ordinary_map_paths('\n'.join(lines + ['100-200 r-xp 0 00:00 1 ' + bad]), package, names, external, True, names[1])
        for bad in [M.RUNTIME + '/lib/libpython3.12.so.1.0', '/outside/libmdds_runtime_token_bridge.so', '/outside/libfastrtps.so.2.14']:
            with self.subTest(explicit_but_forbidden=bad), self.assertRaises(ValueError):
                M.ordinary_map_paths('\n'.join(lines + ['100-200 r-xp 0 00:00 1 ' + bad]), package, names, {**external, bad: 'a' * 64}, True, names[1])
        files = {'lib/librmw_fastrtps_shared_cpp.so': '0fc8f4a214849f4a5abe15f66baa23404448d0cebaf972607d40578200dcf369',
                 'lib/librmw_fastrtps_cpp.so': '6b637fe1292e664507811041e5576bad9291c9bae60b87237f7b92fa88476681'}
        diagnostics = 'ROS_CONNECT_ALLOWED run=' + RUN + ' side=A pid=101\n' + '\n'.join(M.ERROR_PAIR) + '\n' + ordinary_modeled_records('A', 'hold-node', [{'event': 'NODE_READY'}]) + '\n'
        # Exact guard emission shape and original stream: apply_endpoint_guard.py
        # and sealed native Action stderr lines5-7; never synthetic stdout context.
        endpoints = '\n'.join('FDDST_ENDPOINT role=' + role + ' data_sharing=OFF topic=ros_discovery_info' for role in ['writer', 'reader', 'reader'])
        self.assertEqual(M.ordinary_error_observations(diagnostics, endpoints, RUN, 'A', 101, 'hold-node', True, files)['actual_pairs'], 1)
        with self.assertRaises(OSError): M.ordinary_error_observations('ROS_CONNECT_ALLOWED\n', endpoints, RUN, 'A', 101, None, True, files)
        with self.assertRaises(ValueError): M.ordinary_error_observations(diagnostics + '\nOther Error', endpoints, RUN, 'A', 101, 'hold-node', True, files)
        with self.assertRaises(ValueError): M.ordinary_error_observations(diagnostics.replace(M.ERROR_PAIR[0], M.ERROR_PAIR[1]), endpoints, RUN, 'A', 101, 'hold-node', True, files)
        with self.assertRaises(ValueError): M.ordinary_error_observations(diagnostics.replace(M.ERROR_PAIR[0], 'Extra Error ' + M.ERROR_PAIR[0]), endpoints, RUN, 'A', 101, 'hold-node', True, files)
        for stderr in ['', endpoints.replace('role=writer', 'role=reader'), endpoints + '\n' + endpoints.splitlines()[1],
                       endpoints.replace('data_sharing=OFF', 'data_sharing=ON'), endpoints + '\nUnknown Error']:
            with self.subTest(stderr_context=stderr), self.assertRaises(ValueError):
                M.ordinary_error_observations(diagnostics, stderr, RUN, 'A', 101, 'hold-node', True, files)
        with self.assertRaises(ValueError):
            M.ordinary_error_observations(diagnostics + endpoints, '', RUN, 'A', 101, 'hold-node', True, files)
        delayed = diagnostics.replace('\n'.join(M.ERROR_PAIR) + '\n', '') + '\n' + '\n'.join(M.ERROR_PAIR)
        with self.assertRaisesRegex(OSError, 'BLOCKED_LOG_PHASE_TRIAGE'):
            M.ordinary_error_observations(delayed, endpoints, RUN, 'A', 101, 'hold-node', True, files)
        self.assertEqual(M.ordinary_error_observations('', '', RUN, 'A', 101, 'imports-cdr', False, files)['actual_pairs'], 0)
        with self.assertRaises(ValueError): M.ordinary_error_observations('', endpoints, RUN, 'A', 101, 'imports-cdr', False, files)

    def test_ownership(self):
        runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
        child = {'pid': 101, 'ppid': 90, 'starttime': 123, 'exe': '/owned/package/actionbag_python', 'argv': ['/owned/package/actionbag_python', 'ros2']}
        runner.owned = {'A': {'child': child}}
        for changed in [{'starttime': 124}, {'ppid': 91}, {'exe': '/system/bin/sh'}, {'argv': ['other']}]:
            with patch.object(runner, 'snapshot', return_value={**child, **changed}), self.assertRaises(OSError):
                runner.guard('A')
        with patch.object(runner, 'snapshot', return_value=None): self.assertFalse(runner.guard('A', absent_ok=True))
        with self.assertRaises(ValueError): runner.publish('A', 'control.ACTION_RELEASE', 'forbidden')
        runner.launch_pending = {'A': {'state': 'LAUNCH_PENDING'}}
        with self.assertRaises(OSError): runner.launch('A')
        with tempfile.TemporaryDirectory(prefix='ordinary-host-', dir=M.REPO / 'build') as folder:
            runner.output, runner.sequence, runner.stage_deadline = Path(folder), 0, None
            runner.args = SimpleNamespace(hdc=Path(M.NATIVE.HDCPATH))
            with patch.object(M.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, b'[Fail] transfer failed', b'')), self.assertRaises(OSError):
                runner.command('A', ['file', 'send', 'C:\\short\\p.tar', '/owned.upload.tar'])
            with patch.object(runner, 'command', return_value=b'hostzero without remote exit marker'), self.assertRaises(OSError): runner.shell('A', 'true')
            with patch.object(runner, 'command', return_value=b'transfer missing completion'), self.assertRaises(OSError): runner.send('A', Path(folder) / 'not-used', '/owned')
            with patch.object(M.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['hdc'], 1, output=b'partial')), self.assertRaises(OSError): runner.command('A', ['shell', 'true'], 1)

    def test_offline_verdict(self):
        # These are private modeled histories, never issued execution evidence.
        plan = {'run': RUN, 'case': 'topic_pub_AB', 'case_definition': next(r for r in PLAN['ordered_cases'] if r['id'] == 'topic_pub_AB')}
        owned = {s: {'child': {'pid': PIDS[s]}, 'supervisor': {'pid': PIDS[s] - 1}} for s in 'AB'}
        baseline = {'RESULT.json': {'run': RUN, 'case': plan['case'], 'result': 'PASS', 'failures': [], 'blocked': []},
            'CHILD_EXITS.json': {s: 'R25_CHILD_EXIT run=' + RUN + ' side=' + s + ' pid=' + str(PIDS[s]) + ' rc=0' for s in 'AB'},
            'CLEANUP.json': {'identity_mutations': 0, 'directories_retained': True, 'sides': {s: {'absent': True, 'forced_signals': []} for s in 'AB'}},
            'CLOCK.json': {'first_dispatch': 0.0, 'business_origin': 5.0, 'business_deadline': 40.0, 'normal_close_release_deadline': 55.0, 'origin': 'BEFORE_FIRST_LISTENER_RELEASE'},
            'commands/0001-A.json': {'argv': ['modeled-hdc', '-t', 'modeled-A', 'shell', 'true'], 'host_exit': 0},
            'remote/0001-A.json': {'remote_exit': 0}}
        for side in 'AB':
            baseline['releases/' + side + '-control.LISTENER_READY.json'] = {'owned': owned[side], 'monotonic': 6.0,
                'contents': 'ROS_RELEASE run=' + RUN + ' side=' + side + ' stage=LISTENER_READY\n'}
            baseline['releases/' + side + '-control.RUNTIME_CLOSE.json'] = {'owned': owned[side], 'monotonic': 42.0,
                'contents': 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + RUN + ' side=' + side + ' pid=' + str(PIDS[side]) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n'}
        baseline['releases/B-control.PROBE_FINISH.json'] = {'owned': owned['B'], 'monotonic': 30.0,
            'contents': 'R25_PROBE_FINISH schema=1 run=' + RUN + ' side=B pid=202 reason=NORMAL_COMPLETION\n'}
        with tempfile.TemporaryDirectory(prefix='ordinary-offline-', dir=M.REPO / 'build') as folder:
            root = Path(folder)
            for name, row in baseline.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(row))
            healthy = M.ordinary_execution_verdict(root, plan, owned)
            self.assertEqual(healthy['result'], 'SKIPPED')  # Never new acceptance PASS.
            self.assertEqual(healthy['original_execution_result'], 'PASS')
            self.assertEqual(healthy['execution_qualification'], 'NOT_EVALUATED_CONTENT_ONLY')
            self.assertEqual(healthy['execution_evidence_issues'], [])
            self.assertEqual(healthy['execution_evidence']['RESULT.json']['sha256'], hashlib.sha256((root / 'RESULT.json').read_bytes()).hexdigest())
            for status in ('FAIL', 'BLOCKED'):
                original = {**baseline['RESULT.json'], 'result': status, 'failures': ['original deadline exceeded'] if status == 'FAIL' else [],
                    'blocked': ['original provider postflight mismatch'] if status == 'BLOCKED' else []}
                (root / 'RESULT.json').write_text(json.dumps(original))
                result = M.ordinary_execution_verdict(root, plan, owned)
                self.assertEqual(result['result'], status)
                self.assertEqual(result['original_execution_record'], original)
            (root / 'RESULT.json').write_text(json.dumps(baseline['RESULT.json']))
            negatives = [
                ('RESULT.json', None), ('CHILD_EXITS.json', None), ('CLEANUP.json', None), ('CLOCK.json', None),
                ('CHILD_EXITS.json', {**baseline['CHILD_EXITS.json'], 'A': baseline['CHILD_EXITS.json']['A'].replace('rc=0', 'rc=7')}),
                ('CLEANUP.json', {**baseline['CLEANUP.json'], 'sides': {'A': {'absent': True, 'forced_signals': ['TERM']}, 'B': {'absent': True, 'forced_signals': []}}}),
                ('CLEANUP.json', {**baseline['CLEANUP.json'], 'sides': {'A': {'absent': False, 'forced_signals': []}, 'B': {'absent': True, 'forced_signals': []}}}),
                ('commands/0001-A.json', {**baseline['commands/0001-A.json'], 'timed_out': True}),
                ('commands/0001-A.json', {**baseline['commands/0001-A.json'], 'host_exit': None}),
                ('remote/0001-A.json', {'remote_exit': 1}),
                ('CLOCK.json', {**baseline['CLOCK.json'], 'normal_close_release_deadline': 100.0}),
                ('releases/A-control.RUNTIME_CLOSE.json', {**baseline['releases/A-control.RUNTIME_CLOSE.json'], 'monotonic': 56.0}),
                ('RESULT.json', {**baseline['RESULT.json'], 'blocked': ['contradicts PASS']}),
            ]
            for name, value in negatives:
                path = root / name
                if value is None: path.unlink()
                else: path.write_text(json.dumps(value))
                result = M.ordinary_execution_verdict(root, plan, owned)
                with self.subTest(history=name, changed=value):
                    self.assertEqual(result['result'], 'BLOCKED')
                    self.assertTrue(result['execution_evidence_issues'])
                    self.assertEqual(result['execution_qualification'], 'NOT_EVALUATED_CONTENT_ONLY')
                path.write_text(json.dumps(baseline[name]))

    def test_prepare(self):
        with tempfile.TemporaryDirectory(prefix='ordinary-input-', dir=M.REPO / 'build') as folder:
            root = Path(folder)
            selected = root / 'selection.json'
            selected.write_text('{}')
            with self.assertRaises(ValueError): M.ordinary_prepare(SimpleNamespace(inputs=selected, output=root / 'prepared'))
            # A prepared plan cannot bypass full eligibility validation by listing
            # self-consistent hashes. No device primitive is reached.
            plan = {'schema': 1, 'scope': 'FIXED_ORDINARY_CASE_PREPARED_NO_DEVICE_DISPATCH', 'case': 'node_graph_AB',
                'run': RUN, 'selection_file': {'path': str(selected), 'sha256': hashlib.sha256(selected.read_bytes()).hexdigest()}}
            prepared = root / 'PLAN.json'
            prepared.write_text(json.dumps(plan))
            with patch.object(M.NATIVE.ActionbagNativeRunner, 'command', side_effect=AssertionError('must not call device')):
                with self.assertRaises(ValueError): M.OrdinaryCliRunner(SimpleNamespace(prepared=prepared, output=root / 'run'))

    def test_cleanup(self):
        runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
        runner.owned = {'A': {'child': {'pid': 101}, 'supervisor': {'pid': 90}}}
        runner.launch_pending, runner.roles, runner.maps_observed = {}, {'A': 'hold-node'}, {}
        runner.errors, runner.blocked = [], []
        runner.verify_files, runner.write = Mock(), Mock()
        runner.shell = Mock()
        runner.guard = Mock(side_effect=[True, True, True, True, False, False, False])
        runner.snapshot = Mock(return_value=None)
        clock = {'now': 0.0}
        def sleep(seconds): clock['now'] += 10
        with patch.object(M.time, 'monotonic', side_effect=lambda: clock['now']), patch.object(M.time, 'sleep', side_effect=sleep):
            runner.cleanup(False)
        self.assertTrue(any('Forced owned termination' in e for e in runner.errors))
        self.assertEqual(runner.shell.call_args.args[1], 'kill -TERM 101')
        self.assertEqual(runner.write.call_args.args[1]['sides']['A']['forced_signals'], ['TERM'])
        runner.errors, runner.blocked = [], []
        runner.guard = Mock(side_effect=OSError('ownership changed'))
        runner.shell.reset_mock()
        runner.cleanup(False)
        runner.shell.assert_not_called()
        self.assertTrue(runner.blocked)

    def test_cleanup_absent_at_control_publication(self):
        # Host replay of the retained ABSENT snapshot after WINDOW_FAILED.
        # No device primitive is used; both child and supervisor are still checked.
        for exit_before in ('control.PROBE_FINISH', 'control.RUNTIME_CLOSE'):
            with self.subTest(exit_before=exit_before):
                runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
                child = {'pid': 101, 'ppid': 90, 'starttime': 123, 'exe': '/owned/package/actionbag_python', 'argv': ['owned']}
                runner.owned = {'A': {'child': child, 'supervisor': {'pid': 90}}}
                runner.run, runner.prefix = RUN, {'A': '/owned'}
                runner.launch_pending, runner.roles, runner.maps_observed = {}, {'A': 'imports-cdr'}, {}
                runner.finished_fixtures, runner.errors, runner.blocked = set(), [], []
                runner.root_guard, runner.verify_files, runner.write = Mock(), Mock(), Mock()
                state = {'alive': True}
                def shell(side, command):
                    if command.startswith('if test -e '):
                        if exit_before in command: state['alive'] = False
                        return b'0'
                    if command.startswith('if test -d /proc/'): return b'ABSENT\n'
                    if command.startswith('umask 077;'): return b''
                    raise AssertionError('Unexpected operation: ' + command)
                runner.shell = Mock(side_effect=shell)
                native_snapshot = M.NATIVE.ActionbagNativeRunner.snapshot
                runner.snapshot = Mock(side_effect=lambda side, pid: dict(child) if pid == 101 and state['alive'] else native_snapshot(runner, side, pid))
                runner.cleanup(True)
                status = runner.write.call_args.args[1]['sides']['A']
                self.assertEqual(status, {'forced_signals': [], 'absent': True})
                self.assertEqual(runner.blocked, [])
                self.assertEqual(runner.errors, [])
                self.assertIn(unittest.mock.call('A', 90), runner.snapshot.call_args_list)
                runner.verify_files.assert_called_once_with('A')
                releases = [c.args[0] for c in runner.write.call_args_list if c.args[0].startswith('releases/')]
                self.assertEqual(releases, [] if exit_before == 'control.PROBE_FINISH' else ['releases/A-control.PROBE_FINISH.json'])
                self.assertFalse(any('kill -' in c.args[1] for c in runner.shell.call_args_list))
                # Absence remains an error for the ordinary/default publish path.
                with self.assertRaisesRegex(OSError, 'Owned PID identity changed'):
                    runner.publish('A', 'control.RUNTIME_CLOSE', 'normal')
                with self.assertRaises(ValueError):
                    runner.publish('A', 'control.LISTENER_READY', 'normal', cleanup_absent_ok=True)

    def test_cleanup_live_identity_drift_at_control_publication(self):
        child = {'pid': 101, 'ppid': 90, 'starttime': 123, 'exe': '/owned/package/actionbag_python', 'argv': ['owned']}
        for changed in ({'starttime': 124}, {'ppid': 91}, {'exe': '/system/bin/sh'}, {'argv': ['other']}, {'pid': 102}):
            with self.subTest(changed=changed):
                runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
                runner.owned = {'A': {'child': child, 'supervisor': {'pid': 90}}}
                runner.run, runner.prefix = RUN, {'A': '/owned'}
                runner.launch_pending, runner.roles, runner.maps_observed = {}, {'A': 'imports-cdr'}, {}
                runner.finished_fixtures, runner.errors, runner.blocked = set(), [], []
                runner.root_guard, runner.verify_files, runner.write = Mock(), Mock(), Mock()
                runner.snapshot = Mock(side_effect=[dict(child), {**child, **changed}])
                runner.shell = Mock(return_value=b'0')
                runner.cleanup(True)
                status = runner.write.call_args.args[1]['sides']['A']
                self.assertFalse(status['absent'])
                self.assertEqual(status['forced_signals'], [])
                self.assertIn('Owned PID identity changed; refusing action', status['error'])
                runner.root_guard.assert_not_called()
                runner.verify_files.assert_not_called()
                self.assertEqual(len(runner.shell.call_args_list), 1)  # Existence query only.
                self.assertEqual(runner.write.call_args_list[0].args[0], 'CLEANUP.json')

    def test_clock_and_graph(self):
        fixture = ordinary_modeled_records('B', 'hold-node', [{'event': 'NODE_READY', 'name': '/ab_' + RUN + '_B'}, {'event': 'ENTITY_READY', 'entity': 'hold-node'}])
        self.assertTrue(M.ordinary_business('node_graph_AB', RUN, {'A': '', 'B': fixture}, PIDS, PLAN, final=False))
        with self.assertRaises(ValueError): M.ordinary_business('node_graph_AB', RUN, {'A': '', 'B': fixture}, PIDS, PLAN, final=True)
        self.assertTrue(M.ordinary_business('node_graph_AB', RUN, {'A': '/ab_' + RUN + '_B\n', 'B': fixture}, PIDS, PLAN, final=True))
        for delay in (0, 21):
            clock, releases, writes = {'now': 0.0}, [], {}
            runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
            runner.run, runner.case, runner.roles = RUN, 'node_graph_AB', {'A': None, 'B': 'hold-node'}
            runner.owned, runner.design, runner.definition = {s: {'child': {'pid': PIDS[s]}} for s in 'AB'}, PLAN, next(c for c in PLAN['ordered_cases'] if c['id'] == 'node_graph_AB')
            runner.closed = set()
            runner.launch = Mock()
            runner.finish_fixture = Mock()
            runner.wait_exit = Mock()
            runner.write = lambda name, value: writes.update({name: value})
            def publish(side, name, data):
                if clock['now'] >= runner.stage_deadline: raise ValueError('modeled strict remaining budget')
                releases.append((side, name, clock['now']))
                if name == 'control.LISTENER_READY': clock['now'] += 15
            def maps(side, label):
                if side == 'A' and label == 'close': clock['now'] += delay
            def poll(side):
                if runner.stage in ('LISTENER', 'LAUNCH'):
                    return 'ROS_LISTENER_READY run=' + RUN + ' side=' + side + ' pid=' + str(PIDS[side]) + ' listener=0 fake_sdk=0', ''
                ready = 'ROS_RUNTIME_CLOSE_READY schema=1 run=' + RUN + ' side=' + side + ' pid=' + str(PIDS[side]) + ' stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1'
                return ready + ('\n' + fixture if side == 'B' else ''), ''
            runner.publish, runner.maps, runner.poll = publish, maps, poll
            with patch.object(M.time, 'monotonic', side_effect=lambda: clock['now']), patch.object(M.time, 'sleep', side_effect=lambda seconds: clock.update(now=clock['now'] + seconds)):
                if delay:
                    with self.assertRaises(ValueError): runner.workflow()
                else: runner.workflow()
            self.assertEqual(writes['CLOCK.json']['business_origin'], 0.0)
            self.assertEqual(writes['CLOCK.json']['normal_close_release_deadline'], 50.0)
            self.assertEqual([r[2] for r in releases if r[1] == 'control.LISTENER_READY'], [0.0, 15.0])
            if delay: self.assertFalse(any(name == 'control.RUNTIME_CLOSE' for side, name, when in releases))

    def test_supervisor(self):
        with tempfile.TemporaryDirectory(prefix='ordinary-supervisor-', dir=M.REPO / 'build') as folder:
            runner = M.OrdinaryCliRunner.__new__(M.OrdinaryCliRunner)
            runner.output, runner.run, runner.case, runner.design = Path(folder), RUN, 'topic_echo_AB', PLAN
            runner.prefix = {s: '/data/local/tmp/mdds_dds_' + RUN + '_' + s for s in 'AB'}
            runner.routes = {s: {'local': ('a' if s == 'A' else 'b') * 64} for s in 'AB'}
            runner.udids, runner.tokens = {'A': 'modeled-A', 'B': 'modeled-B'}, {'A': 672006291, 'B': 672042405}
            runner.expected_argv = {s: ['modeled-exec'] for s in 'AB'}
            runner.send, runner.shell, runner.write = Mock(), Mock(), Mock()
            bash = Path('C:/Program Files/Git/bin/bash.exe')
            self.assertTrue(bash.is_file(), 'Selected local shell syntax checker is required for this Windows Host control')
            for side in 'AB':
                runner.configure(side)
                script = runner.output / (side + '.supervisor.sh')
                text = script.read_text()
                self.assertIn('wait "$child"', text)
                self.assertIn(' >stdout 2>stderr &', text)
                self.assertNotIn('control.ACTION_RELEASE', text)
                self.assertNotIn('ros2-generic', text)
                self.assertNotIn('FASTDDS_BUILTIN_TRANSPORTS', text)
                result = subprocess.run([str(bash), '-n', str(script)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, check=False)
                self.assertEqual(result.returncode, 0, repr(result.stderr))

    def test_probe(self):
        probe = P.OrdinaryRuntimeProbe.__new__(P.OrdinaryRuntimeProbe)
        probe.args, probe.sequence, probe.finish_identity = SimpleNamespace(run=RUN, side='A', role='hold-node'), 0, None
        probe.emit = Mock()
        with tempfile.TemporaryDirectory(prefix='ordinary-probe-', dir=M.REPO / 'build') as folder:
            probe.finish = Path(folder) / 'control.PROBE_FINISH'
            good = 'R25_PROBE_FINISH schema=1 run=' + RUN + ' side=A pid=101 reason=NORMAL_COMPLETION\n'
            probe.finish.write_text(good)
            info = SimpleNamespace(st_mode=stat.S_IFREG | 0o600, st_dev=1, st_ino=2, st_size=len(good), st_mtime_ns=3)
            with patch.object(P.os, 'getpid', return_value=101), patch.object(Path, 'lstat', return_value=info):
                self.assertTrue(probe.finish_requested())
                for bad in [good.replace('pid=101', 'pid=102'), good + good, good.replace('NORMAL_COMPLETION', 'WINDOW_FAILED')]:
                    probe.finish.write_text(bad)
                    with self.assertRaises(RuntimeError): probe.finish_requested()
            probe.finish.write_text(good)
            replaced = SimpleNamespace(**{**vars(info), 'st_ino': 4})
            with patch.object(Path, 'lstat', side_effect=[info, replaced]), self.assertRaises(RuntimeError): probe.finish_requested()
        # Generated ROS sequence setters copy their input; this model must too.
        fake = SimpleNamespace(Feedback=lambda sequence: SimpleNamespace(sequence=list(sequence)), Result=lambda sequence: SimpleNamespace(sequence=list(sequence)))
        action = SimpleNamespace(GoalResponse=SimpleNamespace(ACCEPT='ACCEPT', REJECT='REJECT'))
        with patch.dict('sys.modules', {'example_interfaces.action': SimpleNamespace(Fibonacci=fake), 'example_interfaces': SimpleNamespace(), 'rclpy.action': action, 'rclpy': SimpleNamespace()}), patch.object(P.time, 'sleep'), patch.object(P.time, 'monotonic', return_value=0):
            probe.goal_count, probe.failed, probe.deadline = 0, None, 40
            self.assertEqual(probe.goal(SimpleNamespace(order=6)), 'ACCEPT')
            self.assertEqual(probe.goal(SimpleNamespace(order=6)), 'REJECT')
            self.assertIsNotNone(probe.failed)
            handle = SimpleNamespace(goal_id=SimpleNamespace(uuid=bytes.fromhex('a' * 32)), request=SimpleNamespace(order=6), publish_feedback=Mock(), succeed=Mock())
            result = probe.execute_goal(handle)
            self.assertEqual(result.sequence, M.FIBONACCI)
            self.assertEqual([c.args[0].sequence for c in handle.publish_feedback.call_args_list], [M.FIBONACCI[:n] for n in range(3, 8)])
            handle.succeed.assert_called_once()
        node = Mock()
        node.destroy_node.side_effect = RuntimeError('modeled cleanup failure')
        rclpy = SimpleNamespace(init=Mock(), create_node=Mock(return_value=node), try_shutdown=Mock())
        qos = SimpleNamespace(QoSProfile=Mock(), ReliabilityPolicy=SimpleNamespace(RELIABLE=1), DurabilityPolicy=SimpleNamespace(VOLATILE=1))
        probe.args.role, probe.node_name, probe.failed = 'hold-node', 'modeled-node', None
        probe.finish_requested = Mock(return_value=True)
        with patch.dict('sys.modules', {'rclpy': rclpy, 'rclpy.qos': qos, 'std_msgs.msg': SimpleNamespace(String=object)}), self.assertRaises(RuntimeError):
            probe.communicate()
        rclpy.init.assert_called_once_with(args=[])
        rclpy.create_node.assert_called_once_with('modeled-node')
        rclpy.try_shutdown.assert_called_once()
        self.assertTrue(any(c.args[0] == 'FAIL' and c.kwargs.get('cleanup_errors') for c in probe.emit.call_args_list))


if __name__ == '__main__':
    unittest.main()
