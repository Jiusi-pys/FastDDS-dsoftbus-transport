"""Host selection controls with mocked ELF metadata; NOT target build/runtime evidence."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[3]/'integrations/ros2/target/package_elf.py'
SPEC = importlib.util.spec_from_file_location('packager_under_test', SOURCE)
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)
BUILDER_SPEC = importlib.util.spec_from_file_location('builder_under_test', Path(__file__).with_name('build_and_bundle.py'))
BUILDER = importlib.util.module_from_spec(BUILDER_SPEC)
BUILDER_SPEC.loader.exec_module(BUILDER)


class SelectionControls(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='rosbag_packager_mock_')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.rows = {}
        self.main = self.elf('consumer', None, ['libbase.so', 'libc.so'])
        self.base = self.elf('libbase.so', 'libbase.so', ['libc.so'])
        self.bridge = self.elf('libbridge.so', 'libbridge.so', ['libbase.so'])
        self.plugin = self.elf('libplugin.so', 'libplugin.so', ['libhelper.so'])
        self.helper = self.elf('libhelper.so', 'libhelper.so', [])

    def elf(self, name, soname, needed, data=None):
        path = self.root/name
        path.write_bytes(data or name.encode())
        self.rows[path] = dict(soname=soname, needed=needed,
                              sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                              readelf_stdout='MOCK_NOT_ELF', readelf_stderr='')
        return path

    def invoke(self, extra=(), libraries=None):
        args = ['--readelf', str(self.root/'mock_readelf'), '--main', str(self.main),
                '--output', str(self.root/'out'), '--external', 'libc.so']
        for path in ([self.base] if libraries is None else libraries):
            args += ['--library', str(path)]
        args += [str(x) for x in extra]
        with patch.object(PACKAGER, 'inspect', side_effect=lambda tool, path: dict(self.rows[Path(path)])):
            PACKAGER.main(args)
        receipt = json.loads((self.root/'out/PACKAGE_INPUTS.json').read_text())
        for row in receipt['members']:
            self.assertEqual((self.root/'out'/row['name']).read_bytes(), Path(row['source']).read_bytes())
        return receipt

    def test_legacy_bridge_caller_flat_members_unchanged(self):
        receipt = self.invoke(['--bridge', self.bridge])
        self.assertEqual([r['name'] for r in receipt['members']], ['consumer', 'libbase.so', 'libbridge.so'])
        self.assertEqual(receipt['main'], 'consumer')
        self.assertEqual(receipt['external'], ['libc.so'])
        self.assertEqual([r['role'] for r in receipt['roots']], ['main', 'bridge'])

    def test_consumer_only(self):
        receipt = self.invoke(['--consumer-only'])
        self.assertEqual([r['name'] for r in receipt['members']], ['consumer', 'libbase.so'])

    def test_dynamic_plugin_and_transitive_dependency_are_roots(self):
        receipt = self.invoke(['--consumer-only', '--dynamic-root', self.plugin], [self.base, self.helper])
        self.assertEqual([r['name'] for r in receipt['members']], ['consumer', 'libbase.so', 'libhelper.so', 'libplugin.so'])
        self.assertEqual(receipt['roots'][1]['soname'], 'libplugin.so')

    def test_missing_mode_rejected(self):
        with self.assertRaises(SystemExit):
            self.invoke()

    def test_both_modes_rejected(self):
        with self.assertRaises(SystemExit):
            self.invoke(['--consumer-only', '--bridge', self.bridge])

    def test_dynamic_root_without_soname_rejected(self):
        self.rows[self.plugin]['soname'] = None
        with self.assertRaisesRegex(ValueError, 'no SONAME'):
            self.invoke(['--consumer-only', '--dynamic-root', self.plugin])

    def test_unresolved_dynamic_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unresolved.*libhelper'):
            self.invoke(['--consumer-only', '--dynamic-root', self.plugin])

    def test_conflicting_catalog_soname_rejected(self):
        other = self.elf('alternate.so', 'libbase.so', [], b'different')
        with self.assertRaisesRegex(ValueError, 'Conflicting library'):
            self.invoke(['--consumer-only'], [self.base, other])

    def test_dynamic_platform_root_rejected(self):
        self.rows[self.plugin]['soname'] = 'libc.so'
        with self.assertRaisesRegex(ValueError, 'Platform library'):
            self.invoke(['--consumer-only', '--dynamic-root', self.plugin])

    def test_bridge_collision_rejected(self):
        self.rows[self.bridge]['soname'] = 'libbase.so'
        with self.assertRaisesRegex(ValueError, 'Package collision'):
            self.invoke(['--bridge', self.bridge])

    def test_missing_static_dependency_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Unresolved.*libbase'):
            self.invoke(['--consumer-only'], [])

    def test_fresh_output_required(self):
        (self.root/'out').mkdir()
        with self.assertRaisesRegex(ValueError, 'Fresh output'):
            self.invoke(['--consumer-only'])


class ResourceCopyControls(unittest.TestCase):
    # Pure byte controls, not target loader/resource discovery execution.
    expected = b'msg/First.msg\nmsg/Second.idl'
    digest = hashlib.sha256(expected).hexdigest()

    def test_lf_accepted_byte_identical(self):
        self.assertEqual(BUILDER.lf_index(self.expected, self.digest), self.expected)

    def test_crlf_accepted_preserves_entries_order_and_missing_final_newline(self):
        original = self.expected.replace(b'\n', b'\r\n')
        self.assertEqual(BUILDER.lf_index(original, self.digest), self.expected)
        self.assertEqual(original, b'msg/First.msg\r\nmsg/Second.idl')

    def test_stray_cr_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Stray CR'):
            BUILDER.lf_index(b'msg/First.msg\rmsg/Second.idl', self.digest)

    def test_changed_entry_rejected(self):
        with self.assertRaisesRegex(ValueError, 'entries/order'):
            BUILDER.lf_index(self.expected.replace(b'First', b'Other'), self.digest)

    def test_changed_order_rejected(self):
        with self.assertRaisesRegex(ValueError, 'entries/order'):
            BUILDER.lf_index(b'msg/Second.idl\nmsg/First.msg', self.digest)

    def test_added_final_newline_rejected(self):
        with self.assertRaisesRegex(ValueError, 'final newline'):
            BUILDER.lf_index(self.expected+b'\n', self.digest)

    def test_exact_plugin_index_routing(self):
        relative = 'share/ament_index/resource_index/rosbag2_storage__pluginlib__plugin/rosbag2_storage_sqlite3'
        line = b'share/rosbag2_storage_sqlite3/plugin_description.xml\n'
        for data in [line, line.replace(b'\n', b'\r\n')]:
            derived, operation = BUILDER.runtime_resource(relative, data)
            self.assertEqual(derived, line)
            self.assertEqual(operation, 'CRLF_TO_LF_ONLY_EXACT_INDEX')
        with self.assertRaisesRegex(ValueError, 'entries/order'):
            BUILDER.runtime_resource(relative, line.replace(b'.xml', b'_other.xml'))

    def test_xml_message_definition_and_unlisted_index_remain_identical(self):
        original = b'first\r\nsecond\rthird\n'
        for relative in ['share/std_msgs/msg/String.msg',
                         'share/rosbag2_storage_sqlite3/plugin_description.xml',
                         'share/ament_index/resource_index/rosidl_interfaces/other_msgs']:
            self.assertEqual(BUILDER.runtime_resource(relative, original), (original, 'BYTE_IDENTICAL'))


if __name__ == '__main__':
    print('HOST_MOCK_SELECTION_CONTROLS_ONLY; no real ELF/build/device execution', flush=True)
    unittest.main()
