"""Local child I/O tests: no HDC, SDK, devices or permission operations."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import unittest

path = Path(__file__).resolve().parents[2] / "scripts/public_socket/pregrant_recovery.py"
spec = importlib.util.spec_from_file_location("recovery_capture", path)
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)


class OwnedPipeCapture(unittest.TestCase):
    def child(self, code):
        return subprocess.Popen([sys.executable, "-c", code], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_stdout_and_stderr(self):
        child = self.child("import sys;sys.stdout.write('x'*2048);sys.stderr.write('y'*1024)")
        out, err, timeout, complete = capture.collect_child(child, 3, 4096)
        self.assertEqual((out, err, timeout, complete, child.returncode), (b"x"*2048, b"y"*1024, False, True, 0))

    def test_timeout_retains_prefix_and_terminates_owned_child(self):
        child = self.child("import time;print('prefix',flush=True);time.sleep(10)")
        out, err, timeout, complete = capture.collect_child(child, .3, 4096)
        self.assertTrue(timeout and complete)
        self.assertIn(b"prefix", out)
        self.assertIsNotNone(child.poll())

    def test_overflow_is_not_pass(self):
        child = self.child("import os,time;os.write(1,b'x'*65536);time.sleep(10)")
        with self.assertRaises(capture.CaptureLimitExceeded) as error:
            capture.collect_child(child, 3, 1024)
        self.assertLessEqual(len(error.exception.stdout)+len(error.exception.stderr), 1024)
        self.assertIsNotNone(child.poll())

    def test_default_collector_unchanged(self):
        child = self.child("print('legacy')")
        out, err, timeout, complete = capture.collect_child(child, 3)
        self.assertEqual((out.strip(), err, timeout, complete), (b"legacy", b"", False, True))

    def test_exited_parent_delayed_pipe_output_is_captured(self):
        code = "import subprocess,sys;subprocess.Popen([sys.executable,'-c',\"import time;time.sleep(.4);print('delayed',flush=True)\"],stdout=sys.stdout,stderr=sys.stderr)"
        child = self.child(code)
        out, err, timeout, complete = capture.collect_child(child, 3, 4096)
        self.assertIn(b"delayed", out)
        self.assertTrue(complete and not timeout)

    def test_exited_parent_continuous_pipe_is_incomplete(self):
        code = "import subprocess,sys;subprocess.Popen([sys.executable,'-c',\"import time;[(print('tick',flush=True),time.sleep(.1)) for _ in range(30)]\"],stdout=sys.stdout,stderr=sys.stderr)"
        child = self.child(code)
        import time
        before = time.monotonic()
        out, err, timeout, complete = capture.collect_child(child, 3, 4096)
        self.assertFalse(complete)
        self.assertLess(time.monotonic()-before, 3)
        self.assertIn(b"tick", out)


if __name__ == "__main__":
    unittest.main()
