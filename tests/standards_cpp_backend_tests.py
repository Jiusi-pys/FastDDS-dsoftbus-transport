"""B3 generation boundaries and artifact ownership; native acceptance is separate."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/standards'))
from generate_cpp import generate_cpp
from idl_cpp import BackendError

CLANG = Path(os.environ.get('IDL_CLANG', 'C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native/llvm/bin/clang.exe'))


class BackendTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def compile(self, text, version='3.6.2', name='Input', **kwargs):
        source = self.root / (name + '.idl')
        source.write_text(text, encoding='utf-8')
        output = self.root / (name + '-' + version)
        result = generate_cpp(source, CLANG, output, version, **kwargs)
        return result, output

    def test_both_abi_shapes_and_all_output_hashes(self):
        for version in ('2.14.5', '3.6.2'):
            with self.subTest(version=version):
                result, output = self.compile('@final struct Message { @key long id; string<8> label; };', version)
                self.assertEqual(result['status'], 'PASS')
                for name, digest in result['generated'].items():
                    self.assertEqual(hashlib.sha256((output / name).read_bytes()).hexdigest(), digest)
                self.assertEqual(json.loads((output / 'generation.json').read_text())['fastdds'], version)
                self.assertTrue(result['backend_sources'])

    def test_preprocessor_dependencies_and_override(self):
        include = self.root / 'included.idl'
        include.write_text('typedef long Number;', encoding='utf-8')
        result, output = self.compile('#include "included.idl"\n@final struct Data { Number id; string<LIMIT> text; };', defines=['LIMIT=12'])
        self.assertEqual(result['status'], 'PASS')
        self.assertIn(str(include.resolve()), result['dependencies'])
        self.assertIn('<= 12', (output / 'InputCdrAux.ipp').read_text())

    def test_existing_output_and_source_directory_are_preserved(self):
        source = self.root / 'Input.idl'
        source.write_text('@final struct Data { long id; };')
        before = source.read_bytes()
        with self.assertRaises(BackendError):
            generate_cpp(source, CLANG, self.root, '3.6.2')
        self.assertEqual(source.read_bytes(), before)

    def test_bad_frontend_input_is_not_published_as_cpp(self):
        result, output = self.compile('struct Data { Missing id; };')
        self.assertEqual(result['status'], 'FAIL')
        self.assertNotEqual(result['diagnostics'][0]['code'], 'E_BACKEND')
        self.assertEqual(result['generated'], {})
        self.assertEqual(list(output.iterdir()), [output / 'generation.json'])

    def test_unsupported_data_models_fail_explicitly(self):
        cases = [
            'interface Service { void send(); };',
            '@mutable struct Data { long id; };',
            '@final struct Data { @optional long id; };',
            '@final struct Data { string text; };',
            '@final struct Data { sequence<long> items; };',
            '@final struct Data { long double number; };',
            '@final struct Data { @key sequence<long,2> ids; };',
            'struct Data; struct Data { sequence<Data,2> children; };',
            '@final struct Base { long id; }; struct Child : Base { long extra; };',
            '@final struct Data { long values[65535][2]; };',
            'module b3 { @final struct Data { long id; }; };',
            'module std { @final struct Data { long id; }; };',
            'const long b3=1; @final struct Data { long id; };',
            '@final struct uint32_t { long id; };',
            '@final struct Data { long b3_storage; };',
            '@final struct Data { long _class; };',
        ]
        for index, source in enumerate(cases):
            with self.subTest(source=source):
                result, _ = self.compile(source, name='Case' + str(index))
                self.assertEqual(result['status'], 'FAIL')
                self.assertEqual(result['diagnostics'][0]['code'], 'E_BACKEND')
                self.assertEqual(result['generated'], {})

    def test_reserved_basename(self):
        for name in ('B3Support', 'b3support', 'B3SUPPORT'):
            with self.subTest(name=name), self.assertRaises(BackendError):
                self.compile('struct Data { long id; };', name=name)

    def test_alias_constants_and_generated_support_name_collision(self):
        for version in ('2.14.5', '3.6.2'):
            with self.subTest(version=version):
                result, _ = self.compile('typedef long Count; const Count Limit=4; typedef boolean Flag; const Flag Enabled=TRUE; @final struct Data { Count id; };', version, name='Aliases')
                self.assertEqual(result['status'], 'PASS')
                for index, other in enumerate(('struct FooPubSubType { long value; };', 'typedef long FooPubSubType;', 'module FooPubSubType { struct Other { long value; }; };')):
                    result, _ = self.compile('struct Foo { long id; };' + other, version, name='Collision' + str(index))
                    self.assertEqual(result['status'], 'FAIL')
                    self.assertEqual(result['diagnostics'][0]['code'], 'E_BACKEND')
                    self.assertIn('name collides', result['diagnostics'][0]['message'])
                    self.assertEqual(result['generated'], {})

    def test_enum_array_nested_sequence_and_multi_module(self):
        result, _ = self.compile('module A { enum Mode { OFF, ON }; typedef long Grid[2][3]; @final struct Inner { long id; }; }; module B { @appendable struct Data { A::Mode mode; A::Grid grid; sequence<A::Inner,3> values; }; };')
        self.assertEqual(result['status'], 'PASS')

    def test_registration_name_collision_is_abi_specific(self):
        cases = [
            'const long register_Data_type_identifier=1; struct Data { long value; };',
            'typedef long register_Data_type_identifier; struct Data { long value; };',
            'module register_Data_type_identifier { struct Other { long value; }; }; struct Data { long value; };',
            'const long register_Mode_type_identifier=1; enum Mode { OFF, ON }; struct Data { Mode value; };',
            'const long register_Count_type_identifier=1; typedef long Count; struct Data { Count value; };',
        ]
        for version in ('2.14.5', '3.6.2'):
            for index, body in enumerate(cases):
                with self.subTest(version=version, index=index):
                    result, _ = self.compile('module M {' + body + '};', version, name='Registry' + str(index))
                    self.assertEqual(result['status'], 'FAIL' if version == '3.6.2' else 'PASS')
                    if version == '3.6.2':
                        self.assertEqual(result['generated'], {})
                        self.assertEqual(result['diagnostics'][0]['code'], 'E_BACKEND')
                        self.assertIn('registration name collides', result['diagnostics'][0]['message'])

    def test_nested_collection_sdk_boundary_is_explicit(self):
        raw = 'module M { struct S { long n; }; struct Data { sequence<S,2> groups[2][2]; }; };'
        result, _ = self.compile(raw, '2.14.5', name='RawNested')
        self.assertEqual(result['status'], 'PASS')
        result, _ = self.compile(raw, '3.6.2', name='RawNested')
        self.assertEqual(result['status'], 'FAIL')
        self.assertEqual(result['generated'], {})
        self.assertEqual(result['diagnostics'][0]['code'], 'E_BACKEND')
        self.assertIn('named inner collection', result['diagnostics'][0]['message'])
        supported = 'module M { struct S { long n; }; typedef sequence<S,2> Group; struct Data { Group groups[2][2]; sequence<long,2> simple[2][2]; sequence<sequence<long,3>,2> layers; }; };'
        for version in ('2.14.5', '3.6.2'):
            result, _ = self.compile(supported, version, name='NamedNested')
            self.assertEqual(result['status'], 'PASS')

    def test_cli_failure_and_no_overwrite(self):
        source = self.root / 'Input.idl'
        source.write_text('interface Service {};')
        output = self.root / 'cli'
        command = [sys.executable, '-B', str(ROOT / 'scripts/standards/generate_cpp.py'), str(source), '--clang', str(CLANG), '--out', str(output), '--fastdds', '3.6.2']
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('E_BACKEND', result.stdout)
        before = (output / 'generation.json').read_bytes()
        repeat = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(repeat.returncode, 2)
        self.assertEqual((output / 'generation.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
