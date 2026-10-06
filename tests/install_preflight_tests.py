"""Offline installed preflight contracts; synthetic captures are not device evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

TOOL = Path(__file__).resolve().parents[1] / 'scripts/install_preflight.py'


class InstalledPreflight(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('installed_preflight', TOOL)
        self.tool = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.tool)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.prefix = self.root / 'installation'
        self.deps = self.root / 'external-dependencies'
        (self.prefix / 'lib').mkdir(parents=True)
        self.deps.mkdir()
        (self.prefix / 'lib/bridge.so').write_bytes(b'bridge')
        (self.deps / 'libfastrtps.so.2.14').write_bytes(b'actual-dependency')
        (self.deps / 'libsoftbus_client.z.so').write_bytes(b'platform')
        (self.deps / 'libidentity.so').write_bytes(b'identity')
        (self.deps / 'libfastcdr.so.2.2').write_bytes(b'cdr')
        self.manifest = dict(schema=1, package='FastDDSDSoftBusTransportRos2Target',
            system='OHOS', architecture='aarch64', compiler='Clang', compiler_version='15',
            sdk_api='23', stl='c++_shared', experimental=True,
            files=['lib/bridge.so'], dependencies=[dict(name='fastrtps', version='2.14.5',
                file='libfastrtps.so.2.14', sha256=hashlib.sha256(b'actual-dependency').hexdigest())])
        self.manifest['dependencies'].append(dict(name='fastcdr', version='2.2.5',
            file='libfastcdr.so.2.2', sha256=hashlib.sha256(b'cdr').hexdigest()))
        self.config = dict(schema=1, capability='experimental', profile='BYTES_NATIVE',
            platform=dict(system='OHOS', architecture='aarch64', sdk_api='23', stl='c++_shared',
                firmware='OpenHarmony-6.1.0.31'),
            namespace='example', domain=0, service_slot=1, initial_peer_slots=[2],
            identity=dict(package='example.app', service_name_prefix='example.app.dds',
                process_name='fastdds_dsoftbus_probe', token_id='123',
                permissions=['ohos.permission.DISTRIBUTED_DATASYNC'],
                library=str(self.deps / 'libidentity.so')),
            socket_library=str(self.deps / 'libsoftbus_client.z.so'))
        self.runtime = dict(bridge_library=str(self.prefix / 'lib/bridge.so'),
            fastdds_module=str(self.deps / 'libfastrtps.so.2.14'),
            fastcdr_module=str(self.deps / 'libfastcdr.so.2.2'),
            softbus_module=self.config['socket_library'], backend='OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            run='a' * 16, side='0', domain='0', authorized_package='example.app',
            service_prefix='example.app.dds', peer_network_id='b' * 64,
            connect_release=str(self.root / 'connect'), locator_version='V1', namespace='example',
            local_udid='synthetic-A', peer_udid='synthetic-B', service_slot='1', peer_slot='2',
            profile='BYTES_NATIVE', native_name_contract='PACKAGE_SCOPED_V1',
            local_initiator_name='example.app.dds.A', peer_initiator_name='example.app.dds.B')
        self.config['runtime_config'] = str(self.root / 'runtime.conf')
        self.config['module_files'] = {k: self.runtime[k] for k in
            ['bridge_library', 'fastdds_module', 'fastcdr_module', 'softbus_module']}
        self.info = dict(architecture='aarch64', format='elf64-littleaarch64', needed=[])

    def check(self, capture=None):
        share = self.prefix / 'share/FastDDSDSoftBusTransportRos2Target'
        share.mkdir(parents=True, exist_ok=True)
        (share / 'install-manifest.json').write_text(json.dumps(self.manifest))
        config = self.root / 'config.json'
        config.write_text(json.dumps(self.config))
        Path(self.config['runtime_config']).write_text(''.join(k + '=' + v + '\n' for k, v in self.runtime.items()))
        with patch.object(self.tool, 'inspect_binary', side_effect=capture or (lambda *a: self.info)):
            return self.tool.preflight(self.prefix, config, [self.deps], Path('explicit-readobj'))

    def test_valid_inputs_are_only_experimental(self):
        r = self.check()
        self.assertIn('INSTALL_INPUTS_VALID', r['statuses'])
        self.assertIn('EXPERIMENTAL_ONLY', r['statuses'])
        self.assertIn('PLATFORM_CONTRACT_UNVERIFIED', r['statuses'])
        self.assertFalse(r['production_ready'])
        self.assertEqual(r['exit_code'], 0)

    def test_relocated_prefix_has_no_original_path_dependency(self):
        self.check()
        moved = self.root / 'moved'
        self.prefix.rename(moved)
        self.prefix = moved
        self.runtime['bridge_library'] = str(moved / 'lib/bridge.so')
        self.config['module_files']['bridge_library'] = self.runtime['bridge_library']
        self.assertIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_missing_dependency(self):
        (self.deps / 'libfastrtps.so.2.14').unlink()
        self.assertIn('RUNTIME_PREREQUISITE_MISSING', self.check()['statuses'])

    def test_wrong_abi(self):
        self.info['architecture'] = 'x86_64'
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_wrong_version_binary(self):
        (self.deps / 'libfastrtps.so.2.14').write_bytes(b'other-version')
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_unknown_profile(self):
        self.config['profile'] = 'MAGIC'
        self.assertIn('UNSUPPORTED_PROFILE', self.check()['statuses'])

    def test_raw_profile_not_silently_promoted(self):
        self.config['profile'] = 'RAW_PLAIN'
        self.assertIn('UNSUPPORTED_PROFILE', self.check()['statuses'])

    def test_missing_identity(self):
        del self.config['identity']
        self.assertIn('RUNTIME_PREREQUISITE_MISSING', self.check()['statuses'])

    def test_forged_qualification(self):
        for key in ['production', 'production_ready', 'qualification_profile', 'pf_pass']:
            with self.subTest(key=key):
                self.config[key] = True
                r = self.check()
                self.assertNotIn('INSTALL_INPUTS_VALID', r['statuses'])
                self.assertFalse(r['production_ready'])
                del self.config[key]

    def test_production_without_platform_contract_is_refused(self):
        self.config['capability'] = 'production'
        r = self.check()
        self.assertEqual(r['exit_code'], 2)
        self.assertIn('PLATFORM_CONTRACT_UNVERIFIED', r['statuses'])
        self.assertFalse(r['production_ready'])

    def test_transitive_missing_library(self):
        r = self.check(lambda *a: dict(self.info, needed=['libabsent.so']))
        self.assertIn('RUNTIME_PREREQUISITE_MISSING', r['statuses'])

    def test_no_worktree_or_environment_fallback(self):
        (self.deps / 'libfastrtps.so.2.14').unlink()
        (self.root / 'libfastrtps.so.2.14').write_bytes(b'actual-dependency')
        with patch.dict('os.environ', {'LD_LIBRARY_PATH': str(self.root), 'PATH': str(self.root)}):
            self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_unknown_platform(self):
        self.config['platform']['firmware'] = 'another-vendor'
        r = self.check()
        self.assertIn('PLATFORM_CONTRACT_UNVERIFIED', r['statuses'])
        self.assertTrue(any(x['code'] == 'PLATFORM_NOT_OBSERVED' for x in r['checks']))

    def test_configuration_ranges(self):
        self.config['service_slot'] = -1
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_absolute_and_escaping_package_paths_rejected(self):
        self.manifest['files'] = ['../external-dependencies/libidentity.so']
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_missing_identity_dso(self):
        self.config['identity']['library'] = str(self.deps / 'missing.so')
        self.assertIn('RUNTIME_PREREQUISITE_MISSING', self.check()['statuses'])

    def test_incomplete_actual_runtime_config(self):
        del self.runtime['local_udid']
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_runtime_config_identity_mismatch(self):
        self.runtime['service_slot'] = '3'
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_wrong_object_format(self):
        self.info['format'] = 'elf64-bigaarch64'
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_windows_extended_api_contract_is_not_a_missing_file(self):
        self.manifest.update(package='FastDDSDSoftBusTransport', system='Windows',
                             architecture='x86_64', sdk_api='N/A', stl='MSVC_MD')
        self.manifest['dependencies'][0].update(name='fastdds', version='3.6.2.0')
        self.config['capability'] = 'host'
        self.config['platform'].update(system='Windows', architecture='x86_64', sdk_api='N/A', stl='MSVC_MD')
        capture = lambda *a: dict(architecture='x86_64', format='COFF-x86-64',
                                 needed=['ext-ms-onecore-appmodel-deployment-internal-l1-1-0.dll'])
        r = self.check(capture)
        self.assertIn('INSTALL_INPUTS_VALID', r['statuses'])
        self.assertTrue(any(x['code'] == 'WINDOWS_API_SET_UNVERIFIED' for x in r['checks']))

    def test_parser_elf_and_pe_imports(self):
        elf = 'Format: elf64-littleaarch64\nArch: aarch64\nNeededLibraries [\n libc.so\n libfoo.so\n]\n'
        pe = 'Format: COFF-x86-64\nArch: x86_64\nImport {\n Name: KERNEL32.dll\n}\n'
        self.assertEqual(self.tool.parse_binary_info(elf)['needed'], ['libc.so', 'libfoo.so'])
        self.assertEqual(self.tool.parse_binary_info(pe)['needed'], ['KERNEL32.dll'])

    def test_delay_imports_are_separate_from_required_load_closure(self):
        capture = 'Format: COFF-x86-64\nArch: x86_64\nImport {\n Name: KERNEL32.dll\n}\nDelayImport {\n Name: OptionalFeature.dll\n Import {\n  Symbol: Feature\n }\n}\n'
        info = self.tool.parse_binary_info(capture)
        self.assertEqual(info['needed'], ['KERNEL32.dll'])
        self.assertEqual(info['delay_needed'], ['OptionalFeature.dll'])

    def host_configuration(self):
        keys = ['bridge_library', 'fastdds_module', 'fastcdr_module', 'backend', 'run',
                'side', 'domain', 'authorized_package', 'service_prefix', 'locator_version', 'native_name_contract']
        values = {k: self.runtime[k] for k in keys}
        values.update(backend='HOST_OPAQUE_BYTES_TEST_ONLY', authorized_package='host.ros.test',
                      service_prefix='host.ros.test.dds', port='36170', peer_port='36171', forward='0')
        return values

    def parse_host(self, values):
        path = self.root / 'host.conf'
        path.write_text(''.join(k + '=' + v + '\n' for k, v in values.items()))
        return self.tool.runtime_configuration(path, False)

    def test_host_forward_required_and_validated(self):
        values = self.host_configuration()
        self.assertEqual(self.parse_host(values)['forward'], '0')
        values.pop('forward')
        with self.assertRaises(ValueError):
            self.parse_host(values)

    def test_host_distinct_ports(self):
        values = self.host_configuration()
        values['peer_port'] = values['port']
        with self.assertRaises(ValueError):
            self.parse_host(values)

    def test_native_identity_byte_limits(self):
        for key, value in [('local_udid', 'a' * 65), ('peer_udid', 'a\x00b'),
                           ('local_initiator_name', 'a\x00b'), ('peer_initiator_name', 'a' * 256)]:
            with self.subTest(key=key):
                old = self.runtime[key]
                self.runtime[key] = value
                self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])
                self.runtime[key] = old

    def test_native_scoped_names_follow_source_contract(self):
        for package, prefix in [('a' * 65, 'a' * 65 + '.dds'),
                                ('example.app', 'example.app..dds'),
                                ('example.app', 'example.app.dds.example.app'),
                                ('example.app', 'example.app.md1.test')]:
            with self.subTest(prefix=prefix):
                self.config['identity'].update(package=package, service_name_prefix=prefix)
                self.runtime.update(authorized_package=package, service_prefix=prefix)
                self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_scoped_full_name_capacity(self):
        self.config['namespace'] = self.runtime['namespace'] = 'n' * 116
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_listener_and_initiator_names_do_not_collide(self):
        for key, slot in [('local_initiator_name', 1), ('peer_initiator_name', 2)]:
            with self.subTest(key=key):
                old = self.runtime[key]
                self.runtime[key] = 'example.app.dds.md1.' + 'example'.encode().hex() + '.00000000.' + format(slot, '06x') + '.01'
                self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])
                self.runtime[key] = old

    def test_malformed_identity_and_modules_return_structured_error(self):
        for key in ['identity', 'module_files']:
            with self.subTest(key=key):
                old = self.config[key]
                self.config[key] = []
                result = self.check()
                self.assertEqual(result['exit_code'], 2)
                self.assertIn('RUNTIME_PREREQUISITE_MISSING', result['statuses'])
                self.config[key] = old

    def test_permissions_are_exact_list_entries(self):
        self.config['identity']['permissions'] = 'xohos.permission.DISTRIBUTED_DATASYNCx'
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])

    def test_maintenance_candidate_identity_is_explicit(self):
        self.manifest['dependencies'][0]['version'] = '2.14.7'
        self.manifest['dependencies'][1]['version'] = '2.2.8'
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])
        self.manifest['ros214_candidate'] = dict(version='2.14.7', upstream_commit='010b908dc3bb2b6f2818d61ac8db462d01b79a1f',
            fastcdr_version='2.2.8', fastcdr_commit='1bc9c919311ffce555b445560bf7ae062a4e1c72', interface_abi=21405)
        self.assertIn('INSTALL_INPUTS_VALID', self.check()['statuses'])
        self.manifest['ros214_candidate']['upstream_commit'] = '0' * 40
        self.assertNotIn('INSTALL_INPUTS_VALID', self.check()['statuses'])


if __name__ == '__main__':
    unittest.main()
