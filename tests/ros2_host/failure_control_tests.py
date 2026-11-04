import unittest
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from failure_control import primary_failure_visible, release_failed_peers


class FailureControlTests(unittest.TestCase):
    def test_real_stdout_primary_requests_peer_cleanup(self):
        row='\nROS_FAILURE schema=1 run=0123456789abcdef side=A pid=64416 seq=1 stage=LEDGER reason=SAMPLE_SERVICE_INCOMPLETE failure_ref=0\n'
        self.assertTrue(primary_failure_visible(row, ''))

    def test_stderr_primary_remains_supported(self):
        self.assertTrue(primary_failure_visible('', 'ROS_FAILURE schema=1 run=test\n'))

    def test_cleanup_output_does_not_replace_primary(self):
        self.assertFalse(primary_failure_visible('ROS_CLEANUP_FAILURE schema=1 run=test\n', ''))
        self.assertFalse(primary_failure_visible('ROS_BUSINESS_DONE result=PASS\n', ''))

    def test_stdout_failure_releases_live_peer_without_overwriting_existing_control(self):
        class Process:
            def __init__(self,pid,exit_code=None): self.pid,self.exit_code=pid,exit_code
            def poll(self): return self.exit_code
        artifacts=Path(__file__).resolve().parents[2]/'build/send-refusal-e0824d7-20261010/failure-control-tests'
        artifacts.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=artifacts) as directory:
            root=Path(directory); original='original control\n'
            (root/'A-control.CLEANUP').write_text(original)
            self.assertTrue(primary_failure_visible('ROS_FAILURE schema=1 run=test\n',''))
            self.assertEqual(release_failed_peers(root,'test',{'A':Process(1),'B':Process(2)}),['B'])
            self.assertEqual((root/'A-control.CLEANUP').read_text(),original)
            self.assertEqual((root/'B-control.CLEANUP').read_text(),'ROS_CLEANUP schema=1 run=test side=B pid=2 stage=CLEANUP reason=WINDOW_FAILED\n')
            self.assertEqual(release_failed_peers(root,'test',{'A':Process(1,4),'B':Process(2,4)}),[])


if __name__=='__main__':
    unittest.main()
