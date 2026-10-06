"""Version-bound source preparation regressions; no SDK or device execution."""
import argparse
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('prepare', ROOT/'integrations/ros2/prepare_fastdds_source.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
UPSTREAM = ROOT/'build/ros214-security-20261007/upstream-fastdds'


class Candidate(unittest.TestCase):
    def tree(self):
        tmp = tempfile.TemporaryDirectory(dir=ROOT/'build/ros214-security-20261007')
        self.addCleanup(tmp.cleanup)
        dst = Path(tmp.name)
        meta = json.loads((ROOT/'integrations/ros2/candidates/2.14.7/UPSTREAM.json').read_text(encoding='utf-8'))
        for row in meta['files']:
            out = dst/row['path'];out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(UPSTREAM/row['path'], out)
        return dst

    def test_exact_candidate_and_repeated_application(self):
        source = self.tree()
        receipt = prepare.prepare(source, version='2.14.7')
        self.assertEqual(receipt['version'], '2.14.7')
        self.assertEqual(receipt['upstream_commit'], '010b908dc3bb2b6f2818d61ac8db462d01b79a1f')
        self.assertEqual(len(receipt['files']), 11)
        self.assertEqual(len(receipt['headers']), 6)
        text = (source/'src/cpp/fastdds/domain/DomainParticipantImpl.cpp').read_text(encoding='utf-8')
        self.assertIn('copy_attributes', text)
        self.assertIn('capture_residual_snapshot', text)
        self.assertIn('handle_', text)
        with self.assertRaises(ValueError):
            prepare.prepare(source, version='2.14.7')

    def test_drift_is_refused_before_mutation(self):
        source = self.tree()
        f = source/'src/cpp/fastdds/domain/DomainParticipantImpl.cpp'
        f.write_bytes(f.read_bytes()+b'// drift\n')
        before = (source/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp').read_bytes()
        with self.assertRaises(ValueError):
            prepare.prepare(source, version='2.14.7')
        self.assertEqual(before, (source/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp').read_bytes())

    def test_wrong_version_cannot_select_candidate(self):
        with self.assertRaises(ValueError):
            prepare.prepare(ROOT/'build/ros214-security-20261007/upstream-fastdds', version='2.14.8')


if __name__ == '__main__':
    unittest.main()
