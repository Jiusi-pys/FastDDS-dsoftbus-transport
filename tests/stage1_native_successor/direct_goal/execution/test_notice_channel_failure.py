"""User-disclosed bytes and synthetic records only; no live constructor/API."""
import importlib.util
import json
from pathlib import Path
import sys
import time
import unittest
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from goal_notice import Marker
from goal_contract import ContractError, GoalStep
from goal_driver import GoalDriver
from goal_windows_backend import GoalWindowsBackend

TEXT = b'[Fail][E000004]:The communication channel is being established.'
BASELINE = Path('E:/Codex-FastDDS-stage1-hdc-e000004-diagnostic-20261031-d01/baseline_goal_notice.py')


def marker(data, case='readonly.1', sink=None):
    value = Marker(sink, cases=('server', 'readonly.1'))
    value.feed(case, 'stdout', data, 'MONITOR', 1, 2)
    return value


class ChannelFailureTests(unittest.TestCase):
    def setUp(self):
        self.assertNotIn('goal_win32_owned', sys.modules)

    def test_prior_counterexample_and_precise_failure(self):
        spec = importlib.util.spec_from_file_location('old_notice_only', BASELINE)
        old = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(old)
        previous = old.Marker(cases=('readonly.1',))
        # LF is synthetic framing, not a claim about the undisclosed file bytes.
        previous.feed('readonly.1', 'stdout', TEXT+b'\n', 'MONITOR', 1, 2)
        self.assertEqual(previous.level, 'UNKNOWN')
        self.assertEqual(previous.events[0]['rule_id'], 'AUTH_CONTEXT_UNKNOWN')
        now = marker(TEXT+b'\n')
        self.assertEqual(now.level, 'COMMAND_FAILURE')
        self.assertTrue(now.notice)
        self.assertEqual(now.reason(), 'HDC_CHANNEL_ESTABLISHING_FAILURE')
        self.assertEqual(now.events[0]['rule_id'], 'CODE_E000004_CHANNEL_ESTABLISHING')
        self.assertTrue(now.events[0]['complete_record'])

    def test_complete_records_fragmentation_and_partial_remain_blocking(self):
        for ending in (b'\n', b'\r\n'):
            record = TEXT+ending
            for split in range(1, len(record)):
                with self.subTest(ending=ending, split=split):
                    value = Marker(cases=('readonly.1',))
                    value.feed('readonly.1', 'stdout', record[:split], 'MONITOR', 1, 2)
                    self.assertFalse(value.notice)
                    value.feed('readonly.1', 'stdout', record[split:], 'MONITOR', 3, 4)
                    value.eof('readonly.1', 'stdout')
                    self.assertEqual(value.level, 'COMMAND_FAILURE')
        for incomplete in (TEXT, TEXT[:18], TEXT+b'\r'):
            value = marker(incomplete)
            value.eof('readonly.1', 'stdout')
            self.assertEqual(value.level, 'UNKNOWN')
            self.assertEqual(value.events[-1]['rule_id'], 'PARTIAL_RECORD')
        for invalid in (TEXT+b'\xff\n', b'[E000004]'+b'x'*513+b'\n'):
            self.assertEqual(marker(invalid).level, 'UNKNOWN')

    def test_same_code_variants_and_auth_priority_not_relaxed(self):
        for variant in (b'[Fail][E000004]:different text\n', TEXT+b' extra\n',
                        TEXT+b'__RO_RC=0\n', b'auth_path=/[E000004]/item\n'):
            self.assertEqual(marker(variant).level, 'UNKNOWN')
        for auth, kind in ((b'[E000002]', 'REQUEST_TEXT'),
                           (b'[E000003]', 'DENIED_TEXT'),
                           (b'please authorize this device', 'REQUEST_TEXT')):
            for mixed in (TEXT+b'\n'+auth+b'\n', auth+b'\n'+TEXT+b'\n'):
                result = marker(mixed)
                self.assertEqual(result.level, kind)
                self.assertTrue(result.notice)
            # Code tokens are explicit anywhere, whereas the original natural
            # request grammar is full-record anchored. Concatenated prose is
            # ambiguous and must remain UNKNOWN, not be made more permissive.
            together = marker(TEXT+auth+b'\n')
            self.assertEqual(together.level, kind if auth.startswith(b'[E000') else 'UNKNOWN')
            self.assertTrue(together.notice)
        unknown = marker(TEXT+b'\nauth unclassified synthetic value\n')
        self.assertEqual(unknown.level, 'UNKNOWN')
        self.assertEqual(marker(b'auth_result=0\n').level, 'DIAGNOSTIC')
        self.assertEqual(marker(TEXT+b'\nauth_result=0\n').level, 'COMMAND_FAILURE')

    def test_real_backend_gate_and_driver_never_accept_or_retry_failure(self):
        # Exact real method bodies; only memory roots/grant/syscall seams supplied.
        backend = object.__new__(GoalWindowsBackend)
        backend.marker = Marker(cases=('server', 'readonly.1'))
        backend.stopped = False
        backend.captured = {'readonly.1': 0}
        backend.latest = {}
        backend.last_sample = time.monotonic()
        backend.journal = SimpleNamespace(case=None, failed=False)
        accepted = []
        step = GoalStep('readonly.1', 'preflight', 'A', ('SYNTHETIC_NOT_EXECUTED',))
        grant = SimpleNamespace(registry={step.id:step}, halt=Mock(),
                                mark_terminal=Mock(), accept=lambda *a:accepted.append(a))
        backend.grant = grant
        backend.server_ok = lambda: True
        backend.w = SimpleNamespace(snapshot=lambda *a: [])
        backend.storage = Mock()
        forged = TEXT+b'\n__RO_RC=0\nS1_GOAL_STEP:readonly.1:0\n'

        class Root:
            case = 'readonly.1'
            owner = {'pid': 17}
            closed = False
            goal_started = time.monotonic()
            terminal_at = time.monotonic()
            output = [forged, b'']
            receipt = dict(EOF=True, native_exit=0, cleanup_errors=[], stop_attempts=0)
            def read(self, cap, observer):
                observer('stdout', forged, 1, 2)
                return [('stdout', forged)]
            def state(self): return 'TERMINAL'
            def identity(self): return 'TERMINAL'

        root = Root()
        backend.roots = [root]
        driver = GoalDriver({step.id:step}, backend)
        driver.live[step.id] = root
        driver.used.add(step.id)
        with patch('goal_evidence.evaluate_step', return_value={'remote_RC':0}) as evaluate:
            with self.assertRaises(ContractError) as error:
                driver.pump()
            self.assertEqual(error.exception.args, ('HDC_CHANNEL_ESTABLISHING_FAILURE',))
            with self.assertRaises(ContractError):
                backend.outcome(root, step)
            evaluate.assert_not_called()
        self.assertEqual(driver.accepted, set())
        self.assertEqual(accepted, [])
        self.assertTrue(backend.stopped)
        self.assertEqual(driver.failure, 'OBSERVATION_OR_RECEIPT_UNKNOWN')
        with self.assertRaises(ContractError):
            driver.start(step.id)
        self.assertEqual(driver.used, {step.id})
        self.assertEqual(driver.client_creates, 0)
        backend.storage.assert_not_called()
        self.assertNotIn('goal_win32_owned', sys.modules)

    def test_failure_event_privacy_sink_error_and_server_policy(self):
        rows = []
        value = marker(TEXT+b'\r\n', sink=lambda row: rows.append(row) or True)
        encoded = json.dumps(value.report())
        self.assertNotIn('communication channel', encoded)
        self.assertNotIn('E000004]:', encoded)
        self.assertNotIn('sha256', encoded)
        self.assertIsNone(rows[0]['remote_occurrence_ns'])
        self.assertEqual(rows[0]['event_type'], 'COMMAND_FAILURE')
        broken = marker(TEXT+b'\n', sink=lambda row: False)
        self.assertTrue(broken.record_failed)
        self.assertEqual(broken.level, 'UNKNOWN')
        backend = object.__new__(GoalWindowsBackend)
        for failure in (marker(TEXT+b'\n', case='server'), value, broken):
            backend.marker = failure
            with self.assertRaises(ContractError):
                backend.notice_gate()
        backend.marker = marker(b'auth context unclassified\n', case='server')
        backend.notice_gate()  # Unchanged explicitly approved server-only exception.
        backend.marker = marker(b'auth context unclassified\n')
        with self.assertRaises(ContractError): backend.notice_gate()


if __name__ == '__main__':
    unittest.main()
