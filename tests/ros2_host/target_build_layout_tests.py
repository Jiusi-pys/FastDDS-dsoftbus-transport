"""Output containment and exact source transforms; no compiler/device invoked."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import tarfile
import io
import unittest
from unittest.mock import patch

REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('target_build',REPO/'integrations/ros2/target/rebuild.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class LayoutTests(unittest.TestCase):
    def test_default_outputs_are_below_repository_build(self):
        root=m.build_root(REPO,None)
        self.assertEqual(root,REPO/'build/ros2-target')
        self.assertTrue(root.is_relative_to(REPO))
    def test_external_and_source_outputs_rejected(self):
        for path in (REPO.parent/'external',REPO,REPO/'src',REPO/'ros2/src'):
            with self.subTest(path=path):self.assertRaises(ValueError,m.build_root,REPO,path)
    def test_default_resolves_existing_output_link_before_creation(self):
        original=Path.resolve
        expected=REPO/'build/ros2-target'
        def resolved(path,*args,**kwargs):
            if path==expected:return REPO.parent/'foreign-link-target'
            return original(path,*args,**kwargs)
        with patch.object(Path,'resolve',resolved):
            self.assertRaises(ValueError,m.build_root,REPO,None)
    def test_exact_eol_equivalence_only(self):
        with tempfile.TemporaryDirectory(dir=REPO/'build') as folder:
            path=Path(folder)/'input.cpp';path.write_bytes(b'a\r\nb\r\n')
            row=m.exact_eol(path,hashlib.sha256(b'a\nb\n').hexdigest())
            self.assertEqual(path.read_bytes(),b'a\nb\n');self.assertTrue(row['changed'])
            self.assertRaises(ValueError,m.exact_eol,path,'0'*64)
            self.assertEqual(path.read_bytes(),b'a\nb\n')
    def test_foreign_resume_manifest_rejected(self):
        self.assertRaises(ValueError,m.validate_selection,{'repo':'C:/unrelated','root':'C:/other'},REPO,m.build_root(REPO,None))
    def test_archive_tree_tamper_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=REPO/'build') as folder:
            root=Path(folder);tree=root/'source';tree.mkdir();archive=root/'source.tar'
            with tarfile.open(archive,'w') as out:
                entry=tarfile.TarInfo('header.hpp');entry.size=3;out.addfile(entry,io.BytesIO(b'one'))
            (tree/'header.hpp').write_bytes(b'one');m.verify_archive_tree(archive,tree)
            (tree/'header.hpp').write_bytes(b'two');self.assertRaises(ValueError,m.verify_archive_tree,archive,tree)
    def test_changed_transform_input_is_rejected(self):
        self.assertRaises(ValueError,m.verify_transform_inputs,{'hook.hpp':'old'},{'hook.hpp':'changed'})

if __name__=='__main__':
    (REPO/'build').mkdir(exist_ok=True)
    unittest.main()
