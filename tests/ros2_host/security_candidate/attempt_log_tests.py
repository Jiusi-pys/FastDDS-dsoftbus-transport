"""Filesystem regression: a later invocation must preserve a failed attempt."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('closure', Path(__file__).with_name('run_ros_security_closure.py'))
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class AttemptLogs(unittest.TestCase):
    def test_second_run_preserves_first_failed_raw_logs(self):
        parent = ROOT / 'build' / 'secure-ros-completion-cb9ad6e' / 'log-tests'
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(dir=parent))
        outputs = []

        def child(command, **kwargs):
            out = Path(command[command.index('--output') + 1])
            out.mkdir(parents=True)
            outputs.append(out)
            marker = b'FIRST_FAILED_RAW\n' if len(outputs) == 1 else b'SECOND_FAILED_RAW\n'
            (out / 'A.stderr').write_bytes(marker)
            (out / 'result.json').write_text(json.dumps({'pass': False, 'errors': ['TEST_FAILURE']}), encoding='utf-8')
            return subprocess.CompletedProcess(command, 37, 'runner stdout', 'runner stderr')

        inputs = dict(name='same-case', work=work, exe=Path('unused.exe'), bridge=Path('unused.dll'),
                      fastdds_prefix=work, ros_prefix=work, pixi_prefix=work,
                      run_id='0123456789abcdef', keystore=None, mode='dual', extra=[],
                      expect_pass=True, expect_stderr=[])
        with patch.object(driver.subprocess, 'run', side_effect=child):
            first = driver.run_case(**inputs)
            raw = (outputs[0] / 'A.stderr').read_bytes()
            second = driver.run_case(**inputs)
        self.assertFalse(first['passed'])
        self.assertFalse(second['passed'])
        self.assertEqual((outputs[0] / 'A.stderr').read_bytes(), raw)
        self.assertNotEqual(outputs[0], outputs[1])

    def test_timeout_keeps_complete_runner_streams_and_failure_receipt(self):
        parent = ROOT / 'build' / 'secure-ros-completion-cb9ad6e' / 'log-tests'
        parent.mkdir(parents=True, exist_ok=True)
        work = Path(tempfile.mkdtemp(dir=parent))
        with patch.object(driver.subprocess, 'run', side_effect=subprocess.TimeoutExpired('runner', 1, b'partial stdout', b'partial stderr')):
            result = driver.run_case('timeout', work, Path('unused.exe'), Path('unused.dll'), work, work, work,
                '0123456789abcdef', None, 'dual', [], False, [])
        self.assertFalse(result['passed'])
        attempt = Path(result['attempt_dir'])
        self.assertEqual((attempt / 'runner.stdout').read_bytes(), b'partial stdout')
        self.assertEqual((attempt / 'runner.stderr').read_bytes(), b'partial stderr')
        self.assertTrue((attempt / 'case.json').is_file())


if __name__ == '__main__':
    unittest.main()
