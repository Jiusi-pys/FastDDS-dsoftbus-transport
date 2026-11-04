"""Host admission/configuration guards; not a native Action execution."""
import argparse
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_action_natural as M


class AdmissionTests(unittest.TestCase):
    def test_duplicate_json_keys_rejected(self):
        with self.assertRaises(ValueError):
            M.read_json_text('{"schema":1,"schema":1}')

    def test_only_two_natural_action_cases(self):
        for case in ('topic_pub_AB', 'imports_cdr_pair', 'action_restart', ''):
            with self.subTest(case=case), self.assertRaises(ValueError):
                M.validate_selection_shape({'schema': 1, 'case': case,
                                            'ordinary_inputs': {}, 'routes': {}})

    def test_boolean_schema_and_additional_field_rejected(self):
        good = {'schema': 1, 'case': 'action_send_goal_AB', 'ordinary_inputs': {}, 'routes': {}}
        for change in ({'schema': True}, {'allow_unqualified': True}):
            value = {**good, **change}
            with self.assertRaises(ValueError):
                M.validate_selection_shape(value)

    def test_user_cannot_request_legacy_validation_bypass(self):
        with self.assertRaisesRegex(ValueError, 'validation'):
            M.NaturalActionRunner(argparse.Namespace(validation_probe=True))

    def test_forbidden_constructor_outputs_rejected_before_read_or_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            evidence = Path(temporary).resolve()
            root = evidence / 'current-stack-windows'
            forbidden = (evidence / 'package/new-child', evidence / 'sealed/new-child',
                         root / 'execute-invalid', root / ('prepared-' + 'a' * 16),
                         root / ('execute-' + 'a' * 16) / 'nested')
            with patch.object(M, 'EVIDENCE', evidence), patch.object(M, 'OUTPUT_ROOT', root), \
                    patch.object(M, 'read') as read, patch.object(Path, 'mkdir') as mkdir, \
                    patch.object(M.NaturalActionRunner, 'deploy') as deploy:
                for output in forbidden:
                    with self.subTest(output=output), self.assertRaises(ValueError):
                        M.NaturalActionRunner(argparse.Namespace(output=output, prepared=evidence / 'missing.json', mode='run'))
                read.assert_not_called()
                mkdir.assert_not_called()
                deploy.assert_not_called()

    def test_output_modes_freshness_and_validation_parent(self):
        with tempfile.TemporaryDirectory() as temporary:
            evidence = Path(temporary).resolve()
            root = evidence / 'current-stack-windows'
            with patch.object(M, 'EVIDENCE', evidence), patch.object(M, 'OUTPUT_ROOT', root):
                for mode, prefix in [('prepare', 'prepared'), ('run', 'execute'), ('analyze', 'analyze')]:
                    output = root / (prefix + '-' + 'a' * 16)
                    self.assertEqual(M.checked_output(output, mode), output)
                    output.mkdir(parents=True)
                    with self.assertRaisesRegex(ValueError, 'Fresh'):
                        M.checked_output(output, mode)
                prepared = root / ('prepared-' + 'a' * 16) / 'PLAN.json'
                prepared.write_text('{}')
                validation = prepared.parent / 'validation'
                self.assertEqual(M.checked_output(validation, 'validation', prepared), validation)
                other = root / 'other.json'
                other.write_text('{}')
                with self.assertRaisesRegex(ValueError, 'Exact prepared'):
                    M.checked_output(validation, 'validation', other)
                with self.assertRaises(ValueError):
                    M.checked_output(validation, 'unknown', prepared)

    def test_public_prepare_rejects_output_before_reading_input(self):
        with patch.object(M, 'read') as read, patch.object(Path, 'mkdir') as mkdir:
            with self.assertRaises(ValueError):
                M.prepare(argparse.Namespace(output=M.EVIDENCE / 'sealed/new-child', inputs='missing.json'))
            read.assert_not_called()
            mkdir.assert_not_called()

    def test_prepare_cli_dispatches_function(self):
        with patch.object(M, 'prepare', return_value=0) as prepare:
            self.assertEqual(M.natural_cli_main(['prepare', '--inputs', 'selection.json', '--output', 'output']), 0)
            self.assertEqual(prepare.call_args.args[0].mode, 'prepare')

    def test_duplicate_routes_entry_rejected_before_adapter_or_write(self):
        with patch.object(M.NATIVE, 'actionbag_peer_receipt') as adapter, \
                patch.object(Path, 'open') as write, patch('sys.stderr', new_callable=io.StringIO) as error:
            with self.assertRaises(SystemExit) as caught:
                M.natural_cli_main(['routes', '--peer-evidence', 'old-view', '--output', 'package/new-file.json'])
            self.assertEqual(caught.exception.code, 2)
            self.assertIn('invalid choice', error.getvalue())
            adapter.assert_not_called()
            write.assert_not_called()

    def test_prepared_non_run_field_drift_rejected_before_legacy_init(self):
        checked = {'schema': 1, 'run': 'a' * 16, 'case': 'action_send_goal_AB', 'files': {'a': 'digest'}}
        for key, value in [('case', 'action_send_goal_BA'), ('files', {}), ('extra', True)]:
            prepared = {**checked, 'run': 'b' * 16, key: value}
            with self.subTest(key=key), self.assertRaises(ValueError):
                M.require_prepared_equal(prepared, checked)

    def test_run_must_be_exact_hex_and_other_fields_equal(self):
        checked = {'run': 'a' * 16, 'schema': 1}
        M.require_prepared_equal({'run': 'b' * 16, 'schema': 1}, checked)
        for run in ('', 'G' * 16, 'a' * 15, '../' + 'a' * 13, None):
            with self.subTest(run=run), self.assertRaises(ValueError):
                M.require_prepared_equal({'run': run, 'schema': 1}, checked)


class ConfigurationTests(unittest.TestCase):
    def test_current_A_is_bounded_and_B_is_ordinary(self):
        a = M.backend_fields('A', '/data/local/tmp/test_A')
        self.assertEqual(a, {'backend': 'OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL',
                            'native_reaccept_order': 'outgoing-first',
                            'native_reaccept_control': '/data/local/tmp/test_A/control.NATIVE_REACCEPT',
                            'process_restart_observation': '1'})
        self.assertEqual(M.backend_fields('B', '/data/local/tmp/test_B'),
                         {'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL'})
        with self.assertRaises(ValueError):
            M.backend_fields('B1', '/data/local/tmp/test_B')

    def test_only_existing_infrastructure_controls_allowed(self):
        runner = M.NaturalActionRunner.__new__(M.NaturalActionRunner)
        for control in ('START_OLD', 'COMPLETE_PRE', 'COMPLETE_NEW', 'NATIVE_REACCEPT', 'ARM', 'END'):
            with self.subTest(control=control), self.assertRaises(ValueError):
                runner.publish('A', 'control.' + control, 'permission')

    def test_no_replacement_diagnostics_are_accepted(self):
        for marker in ('NATIVE_REACCEPT_REFUSAL', 'NATIVE_REACCEPT_EVENT',
                       'NATIVE_PROCESS_OWNERS', 'NATIVE_REPEAT_EVENT', 'NATIVE_ROUTE_EVENT'):
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                M.check_no_replacement({'A': marker + ' run=x', 'B': ''})
        M.check_no_replacement({'A': 'ordinary diagnostics', 'B': ''})

    def test_config_sent_once_with_current_backend_and_stock_business(self):
        with tempfile.TemporaryDirectory() as temporary:
            runner = M.NaturalActionRunner.__new__(M.NaturalActionRunner)
            runner.output = Path(temporary)
            runner.run = 'a' * 16
            runner.case = 'action_send_goal_AB'
            runner.prefix = {s: '/data/local/tmp/mdds_dds_' + runner.run + '_' + s for s in 'AB'}
            runner.routes = {'A': {'local': '1' * 64}, 'B': {'local': '2' * 64}}
            runner.udids = {'A': 'a-udid', 'B': 'b-udid'}
            runner.tokens = {'A': 672006291, 'B': 672042405}
            runner.expected_argv = {'A': ['expected-A'], 'B': ['expected-B']}
            runner.design = {}
            sent, shells = [], []
            runner.send = lambda side, source, target: sent.append((side, target, source.read_text()))
            def shell(side, command):
                shells.append((side, command))
                if command.startswith('sha256sum '):
                    digest = hashlib.sha256((runner.output / (side + '.private.conf')).read_bytes()).hexdigest()
                    return (digest + '  ' + runner.prefix[side] + '/ros.conf\n').encode()
                return b''
            runner.shell = shell
            runner.write = lambda name, value: None
            with patch.object(M.ORDINARY, 'ordinary_cli_argv', return_value=(['run-token-first', 'ros2', 'action', 'send_goal'], None)):
                for side in 'AB':
                    runner.configure(side)
            for side in 'AB':
                configs = [data for target_side, target, data in sent if target_side == side and target.endswith('/ros.conf')]
                self.assertEqual(len(configs), 1)
                for key, value in M.backend_fields(side, runner.prefix[side]).items():
                    self.assertIn(key + '=' + value + '\n', configs[0])
                self.assertNotIn('process_restart_cycles=', configs[0])
                self.assertNotIn('route_lifecycle=', configs[0])
            self.assertEqual(len(shells), 4)
            self.assertEqual(sum('chmod 600' in command for _, command in shells), 2)
            self.assertEqual(sum(command.startswith('sha256sum ') for _, command in shells), 2)
            runner.shell = lambda side, command: b'0' * 64 + b'  wrong-path\n'
            with patch.object(M.ORDINARY, 'ordinary_cli_argv', return_value=(['run-token-first', 'ros2'], None)):
                with self.assertRaisesRegex(ValueError, 'configuration differs'):
                    runner.configure('A')


if __name__ == '__main__':
    unittest.main()
