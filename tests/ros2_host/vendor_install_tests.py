"""Byte-exact vendor install regression, same-time old/new ELF fixtures only."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('slice_builder',REPO/'integrations/ros2/target/build_ros_slice.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class VendorInstall(unittest.TestCase):
    def test_same_timestamp_different_bytes_are_updated_and_recorded(self):
        with tempfile.TemporaryDirectory(dir=REPO/'build') as folder:
            root=Path(folder);outer=root/'build-libyaml_vendor';child=outer/'libyaml_vendor-prefix/src/libyaml_vendor-build';child.mkdir(parents=True)
            staged=outer/'libyaml_vendor-prefix/install/lib';staged.mkdir(parents=True)
            installed=root/'install';final=installed/'opt/libyaml_vendor/lib';final.mkdir(parents=True)
            source=child/'libyaml.so';source.write_bytes(b'\x7fELF_NEW_SONAME')
            for p in (staged/'libyaml.so',final/'libyaml.so'):
                p.write_bytes(b'\x7fELF_OLD_BYTES');os.utime(p,ns=(source.stat().st_mtime_ns,source.stat().st_mtime_ns))
            rows=m.synchronize_vendor_libraries('libyaml_vendor',outer,installed)
            self.assertEqual(len(rows),2)
            self.assertTrue(all(row['changed'] for row in rows))
            self.assertEqual((final/'libyaml.so').read_bytes(),source.read_bytes())
            self.assertTrue(all(not row['changed'] for row in m.synchronize_vendor_libraries('libyaml_vendor',outer,installed)))
    def test_unknown_vendor_rejected(self):
        self.assertRaises(ValueError,m.synchronize_vendor_libraries,'other',REPO/'build',REPO/'build')
if __name__=='__main__':
    (REPO/'build').mkdir(exist_ok=True);unittest.main()
