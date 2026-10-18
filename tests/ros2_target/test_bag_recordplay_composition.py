"""Host-only controls for exact record/play script and index derivation."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest
import uuid
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[2] / 'integrations/ros2/target/compose_bag_recordplay_runtime.py'
spec = importlib.util.spec_from_file_location('candidate_recordplay_composition', SOURCE)
M = importlib.util.module_from_spec(spec)
spec.loader.exec_module(M)


class BagRecordplayCompositionControls(unittest.TestCase):
    def setUp(self):
        self.before = M.LOAD(M.PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
        self.closure = M.LOAD(M.PARENT / 'ELF_CLOSURE.json')
        self.after = {**self.before, M.MEMBER: M.PROBE_SHA,
                      'COMPOSITION.json': 'a' * 64, 'LAUNCHER_PACKAGE.json': 'b' * 64}
        self.after.update({M.INDEX_PREFIX + name: pins[1] for name, pins in M.INDEX_PINS.items()})
        self.after.update(M.TYPESUPPORT)

    def reject(self, message):
        with self.assertRaisesRegex(ValueError, message):
            M.validate_recordplay_derivation(self.before, self.after, self.closure)

    def test_only_script_manifests_and_exact_index_delta_is_allowed(self):
        result = M.validate_recordplay_derivation(self.before, self.after, self.closure)
        self.assertEqual(result['native_elf_files_unchanged'], 217)
        self.assertEqual(result['new_readelf_commands'], 220)
        self.assertEqual(result['new_native_build_commands'], 0)
        self.assertEqual(result['inherited_payload_files'], 3216)
        self.assertEqual(len(result['normalized_rosidl_indexes']), 12)

    def test_exact_dynamic_typesupport_roots_are_required(self):
        for name, digest in M.TYPESUPPORT.items():
            with self.subTest(name=name):
                self.after[name] = 'c' * 64
                self.reject('typesupport derivation')
                del self.after[name]
                self.reject('member set')
                self.after[name] = digest

    def test_reused_typesupport_and_readelf_are_hash_bound(self):
        sources, readelf = M.recordplay_typesupport()
        self.assertEqual({name: M.SHA(path) for name, path in sources.items()}, M.TYPESUPPORT)
        self.assertEqual(M.SHA(readelf), M.READELF_SHA)
        with patch.object(M, 'SHA', return_value='0' * 64), self.assertRaisesRegex(ValueError, 'typesupport changed'):
            M.recordplay_typesupport()
        actual = M.SHA
        with patch.object(M, 'SHA', side_effect=lambda path: '0' * 64 if path == readelf else actual(path)), \
                self.assertRaisesRegex(ValueError, 'readelf changed'):
            M.recordplay_typesupport()

    def test_off_project_deployment_input_is_rejected_before_creating_output(self):
        output = M.REPO / 'build/r25-native-actionbag' / ('bag-recordplay-contract-' + uuid.uuid4().hex)
        with tempfile.TemporaryDirectory(prefix='recordplay-input-contract-') as temporary:
            foreign = Path(temporary) / 'tool.exe'
            foreign.write_bytes(b'not a deployment input')
            parent = ({str(foreign): M.SHA(foreign)}, {}, M.PARENT / 'package', {}, {}, {}, {}, {})
            with patch.object(M, 'recordplay_parent', return_value=parent), \
                    patch.object(M, 'recordplay_rosidl_indexes', return_value=({}, {})), \
                    patch.object(M, 'recordplay_typesupport', return_value=({}, foreign)):
                with self.assertRaisesRegex(ValueError, 'escapes root'):
                    M.compose_bag_recordplay_runtime(output)
            self.assertFalse(output.exists())

    def test_each_index_requires_exact_parent_and_normalized_hash(self):
        for name in M.INDEX_PINS:
            path = M.INDEX_PREFIX + name
            for inventory, replacement in ((self.before, 'c' * 64), (self.after, self.before[path]),
                                            (self.after, 'c' * 64)):
                with self.subTest(name=name, replacement=replacement):
                    old = inventory[path]
                    inventory[path] = replacement
                    self.reject('rosidl index derivation')
                    inventory[path] = old

    def test_actual_indexes_normalize_for_native_line_reader_without_mutating_parent(self):
        parent = M.PARENT / 'package'
        before = {name: (parent / (M.INDEX_PREFIX + name)).read_bytes() for name in M.INDEX_PINS}
        payloads, report = M.recordplay_rosidl_indexes(parent)
        self.assertEqual(len(payloads), 12)
        self.assertEqual(sum(row['references'] for row in report['indexes'].values()), 276)
        self.assertEqual(sum(row['crlf_replacements'] for row in report['indexes'].values()), 264)
        for name, raw in before.items():
            normalized = payloads[M.INDEX_PREFIX + name]
            self.assertEqual(normalized, raw.replace(b'\r\n', b'\n'))
            self.assertEqual((parent / (M.INDEX_PREFIX + name)).read_bytes(), raw)
            self.assertEqual(normalized.endswith(b'\n'), raw.endswith(b'\n'))
        original_lines = before['std_msgs'].split(b'\n')
        normalized_lines = payloads[M.INDEX_PREFIX + 'std_msgs'].split(b'\n')
        for filename in (b'String.msg', b'String.idl'):
            self.assertFalse(any(line.rsplit(b'/', 1)[-1] == filename for line in original_lines))
            self.assertTrue(any(line.rsplit(b'/', 1)[-1] == filename for line in normalized_lines))

    def test_definitions_and_other_resource_indexes_stay_immutable(self):
        for path in ('share/std_msgs/msg/String.msg',
                     'share/ament_index/resource_index/rclcpp_components/rosbag2_transport'):
            with self.subTest(path=path):
                old = self.after[path]
                self.after[path] = 'c' * 64
                self.reject('Inherited payload')
                self.after[path] = old

    def test_new_script_missing_or_mutated(self):
        for digest in (None, 'c' * 64):
            with self.subTest(digest=digest):
                self.after[M.MEMBER] = digest
                self.reject('member set')

    def test_extra_member_is_rejected(self):
        self.after['scripts/unreviewed.py'] = 'c' * 64
        self.reject('member set')

    def test_same_size_member_replacement_is_rejected(self):
        self.after['scripts/unreviewed.py'] = self.after.pop('run')
        self.reject('member set')

    def test_inherited_native_and_python_changes_are_rejected(self):
        for name in ('actionbag_python', 'lib/libsqlite3.so', 'scripts/actionbag_runtime_probe.py'):
            with self.subTest(name=name):
                old = self.after[name]
                self.after[name] = 'c' * 64
                self.reject('Inherited payload')
                self.after[name] = old

    def test_record_bytes_cannot_be_regenerated(self):
        name = 'python/ros2bag-0.26.11.dist-info/RECORD'
        self.after[name] = 'c' * 64
        self.reject('Inherited payload')

    def test_old_manifests_cannot_be_reused(self):
        for name in M.MANIFESTS:
            with self.subTest(name=name):
                old = self.after[name]
                self.after[name] = self.before[name]
                self.reject('Both manifests')
                self.after[name] = old

    def test_new_probe_must_not_be_in_parent(self):
        self.before[M.MEMBER] = M.PROBE_SHA
        self.reject('member set')

    def test_elf_closure_wrong_hash_or_missing_object_is_rejected(self):
        original = copy.deepcopy(self.closure)
        name = next(iter(original['objects']))
        self.closure['objects'][name]['sha256'] = 'c' * 64
        self.reject('native ELF')
        self.closure = original
        del self.closure['objects'][name]
        self.reject('native ELF')

    def test_unsafe_or_alias_member_is_rejected(self):
        for name in ('../escape', 'scripts/BAG_RECORDPLAY_PROBE.py', 'scripts/NUL.py'):
            with self.subTest(name=name):
                self.after[name] = 'c' * 64
                with self.assertRaises(ValueError):
                    M.validate_recordplay_derivation(self.before, self.after, self.closure)
                del self.after[name]

    def test_output_outside_owned_build_refused_before_parent_read(self):
        with tempfile.TemporaryDirectory(prefix='recordplay-output-') as temporary:
            output = Path(temporary) / 'candidate'
            with patch.object(M, 'recordplay_parent') as parent, self.assertRaisesRegex(ValueError, 'fresh direct'):
                M.compose_bag_recordplay_runtime(output)
            parent.assert_not_called()
            self.assertFalse(output.exists())

    def test_existing_output_refused_without_replacement(self):
        with tempfile.TemporaryDirectory(dir=M.REPO / 'build/r25-native-actionbag', prefix='bag-recordplay-existing-') as temporary:
            output = Path(temporary)
            marker = output / 'keep.txt'
            marker.write_bytes(b'preserved')
            with patch.object(M, 'recordplay_parent') as parent, self.assertRaisesRegex(ValueError, 'fresh output'):
                M.compose_bag_recordplay_runtime(output)
            parent.assert_not_called()
            self.assertEqual(marker.read_bytes(), b'preserved')

    def test_parent_and_other_artifact_descendants_are_rejected_before_any_io(self):
        for output in (M.PARENT, M.PARENT / 'bag-recordplay-new',
                       M.PARENT / 'package/bag-recordplay-new',
                       M.PARENT / 'package/scripts/bag-recordplay-new',
                       M.REPO / 'build/r25-native-actionbag/bag-recordplay-initial-test-build/bag-recordplay-new',
                       M.EVIDENCE / 'bag-abi4-window/bag-recordplay-new'):
            with self.subTest(output=output), patch.object(M, 'recordplay_parent') as parent, \
                    patch.object(M.BASE, 'runtime_path') as resolve, patch.object(Path, 'mkdir') as mkdir:
                with self.assertRaisesRegex(ValueError, 'fresh direct'):
                    M.compose_bag_recordplay_runtime(output)
                parent.assert_not_called()
                resolve.assert_not_called()
                mkdir.assert_not_called()

    def test_direct_child_name_cannot_smuggle_path_components(self):
        for name in ('unreviewed-output', 'bag-recordplay-NUL.', 'bag-recordplay-a/b', 'bag-recordplay-../x'):
            with self.subTest(name=name), patch.object(M, 'recordplay_parent') as parent, \
                    patch.object(Path, 'mkdir') as mkdir:
                with self.assertRaisesRegex(ValueError, 'fresh direct'):
                    M.compose_bag_recordplay_runtime(M.REPO / 'build/r25-native-actionbag' / name)
                parent.assert_not_called()
                mkdir.assert_not_called()


if __name__ == '__main__':
    unittest.main()
