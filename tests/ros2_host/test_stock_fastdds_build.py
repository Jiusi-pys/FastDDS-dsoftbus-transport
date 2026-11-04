"""Integrity failures must reject a stock-source qualification."""
import importlib.util
from pathlib import Path
import tarfile
import tempfile
import unittest

SOURCE = Path(__file__).resolve().parents[2] / 'integrations/ros2/stock/build_stock_fastdds.py'
spec = importlib.util.spec_from_file_location('stock_builder', SOURCE)
B = importlib.util.module_from_spec(spec)
spec.loader.exec_module(B)


class StockQualification(unittest.TestCase):
    def test_byte_change_is_not_a_stock_build(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            file = root / 'Factory.cpp'
            file.write_bytes(b'upstream\n')
            expected = B.inventory(root)
            file.write_bytes(b'project hook\n')
            with self.assertRaisesRegex(ValueError, 'source bytes'):
                B.verify_source(root, expected)

    def test_extra_source_member_is_rejected(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            (root / 'stock.cpp').write_bytes(b'upstream')
            expected = B.inventory(root)
            (root / 'hook.cpp').write_bytes(b'hook')
            with self.assertRaises(ValueError):
                B.verify_source(root, expected)

    def test_link_or_traversal_archive_is_rejected(self):
        for name, kind in [('../outside', tarfile.REGTYPE), ('link', tarfile.SYMTYPE),
                           ('hardlink', tarfile.LNKTYPE), ('/absolute', tarfile.REGTYPE)]:
            with self.subTest(name=name):
                member = tarfile.TarInfo(name)
                member.type = kind
                with self.assertRaises(ValueError):
                    B.check_members([member])

    def test_regular_upstream_member_is_accepted(self):
        B.check_members([tarfile.TarInfo('src/cpp/Factory.cpp')])


if __name__ == '__main__':
    unittest.main()
