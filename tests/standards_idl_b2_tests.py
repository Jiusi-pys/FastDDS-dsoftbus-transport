"""B2 core extensions, profile combinations, annotations and semantic oracles."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from fractions import Fraction
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/standards'))
from compile_idl import compile_file
from idl_extended import parse_extended
from idl_frontend import Diagnostic
from idl_numeric import Binary, NumericError, power2
from idl_profiles import ANNOTATIONS, BLOCKS, GROUPS, PROFILES

CORPUS = ROOT / 'tests/standards_idl/b2'
CASES = json.loads((CORPUS / 'corpus.json').read_text(encoding='utf-8'))


def symbol(ast, name):
    return next(n for n in ast['symbols'] if n['qualified_name'] == name)


def rational(value):
    return Fraction(int(value['coefficient'])) * power2(value['exponent2'])


class LanguageTests(unittest.TestCase):
    def test_inventory_and_coverage(self):
        self.assertEqual({p.relative_to(CORPUS).as_posix() for p in CORPUS.rglob('*.idl')}, {c['file'] for c in CASES})
        legacy = CORPUS.parent
        legacy_cases = json.loads((legacy / 'corpus.json').read_text())
        self.assertEqual({p.relative_to(legacy).as_posix() for p in legacy.rglob('*.idl')},
                         {c['file'] for c in legacy_cases} | {'b2/'+c['file'] for c in CASES})
        for negative in (False, True):
            covered = {c['coverage'] for c in CASES if bool(c['diagnostic']) == negative}
            self.assertTrue(set(BLOCKS) | set(GROUPS) <= covered)
            profile_cases = {c['profile'] for c in CASES if c['coverage'] == 'profiles' and bool(c['diagnostic']) == negative}
            self.assertEqual(profile_cases, set(PROFILES)-{'b2'})

    def test_direct_corpus(self):
        for case in CASES:
            with self.subTest(case=case['file'], profile=case['profile']):
                path = CORPUS / case['file']
                if case['diagnostic']:
                    with self.assertRaises(Diagnostic) as caught:
                        parse_extended(path.read_text(encoding='utf-8'), str(path), case['profile'])
                    self.assertEqual(caught.exception.code, case['diagnostic'])
                else:
                    ast = parse_extended(path.read_text(encoding='utf-8'), str(path), case['profile'])
                    self.assertEqual(ast['schema'], 2)
                    self.assertEqual(ast['profile'], case['profile'])
                    targets = {n['qualified_name'] for n in ast['symbols']}
                    self.assertTrue(all(r['target'] in targets for r in ast['references']))
                    json.dumps(ast)

    def test_template_specialization(self):
        ast = parse_extended((CORPUS / 'positive/templates.idl').read_text())
        self.assertEqual(symbol(ast, '::Actual::Grid')['type']['dimensions'], [4])
        self.assertEqual(symbol(ast, '::Actual::X')['type'], {'kind': 'primitive', 'name': 'long'})
        generic = symbol(ast, '::T::Grid')['type']['dimensions'][0]
        self.assertEqual(generic, {'deferred': 'N'})

    def test_template_alias_specializes(self):
        ast = parse_extended((CORPUS / 'positive/template_alias.idl').read_text())
        self.assertEqual(symbol(ast, '::Actual::Inner::X')['type']['name'], '::Actual::Y')
        self.assertTrue(any(r['target'] == '::Actual::Inner::Value' for r in ast['references']))

    def test_inherited_reference_canonical_target(self):
        ast = parse_extended((CORPUS / 'positive/diamond.idl').read_text())
        self.assertEqual(ast['references'][-1]['target'], '::A::T')

    def test_custom_defaults_and_unknown_retained(self):
        ast = parse_extended('@annotation Note { long priority default 7; }; @Note @Vendor::Future(X+1) struct S { long x; };')
        annotations = symbol(ast, '::S')['annotations']
        self.assertEqual(annotations[0]['arguments']['priority'], 7)
        self.assertEqual(annotations[0]['argument_kinds']['priority'], 'integer')
        self.assertTrue(annotations[1]['ignored'])
        self.assertEqual(annotations[1]['name'], 'Vendor::Future')

    def test_standard_annotation_defaults(self):
        ast = parse_extended('@autoid @appendable struct S { @key @optional long x; }; @service interface I { @oneway void f(in long x); };')
        self.assertEqual(symbol(ast, '::S')['annotations'][0]['arguments'], {'value': 'HASH'})
        self.assertEqual(symbol(ast, '::S::x')['annotations'][0]['arguments'], {'value': True})
        self.assertEqual(symbol(ast, '::I')['annotations'][0]['arguments'], {'platform': '*'})

    def test_location_after_latin1_identifier(self):
        with self.assertRaises(Diagnostic) as caught:
            parse_extended('struct Café {\n  Missing value;\n};', 'latin.idl')
        self.assertEqual(caught.exception.code, 'E_UNRESOLVED')
        self.assertEqual(caught.exception.token.file, 'latin.idl')
        self.assertEqual(caught.exception.token.line, 2)
        self.assertEqual(caught.exception.token.column, 3)

    def test_every_standard_annotation_signature(self):
        applications = {
            'id': '@id(1)', 'autoid': '@autoid(HASH)', 'optional': '@optional',
            'position': '@position(1)', 'value': '@value(3)',
            'extensibility': '@extensibility(MUTABLE)', 'final': '@final',
            'appendable': '@appendable', 'mutable': '@mutable', 'key': '@key',
            'must_understand': '@must_understand', 'default_literal': '@default_literal',
            'default': '@default(1)', 'range': '@range(min=0,max=2)',
            'min': '@min(0)', 'max': '@max(2)', 'unit': '@unit("m")',
            'bit_bound': '@bit_bound(8)', 'external': '@external', 'nested': '@nested',
            'verbatim': '@verbatim(text="// metadata")', 'service': '@service("DDS")',
            'oneway': '@oneway', 'ami': '@ami',
        }
        self.assertEqual(set(applications), set(ANNOTATIONS))
        for name, application in applications.items():
            with self.subTest(annotation=name):
                if name in ('id', 'key', 'optional', 'must_understand', 'default', 'external'):
                    source = f'struct S {{ {application} long x; }};'
                elif name in ('position', 'value', 'default_literal'):
                    source = f'enum E {{ {application} A,B }};'
                elif name in ('range', 'min', 'max', 'unit'):
                    source = f'{application} typedef long T;'
                elif name in ('service', 'ami'):
                    source = f'{application} interface I {{}};'
                elif name == 'oneway':
                    source = f'interface I {{ {application} void f(in long x); }};'
                else:
                    source = f'{application} struct S {{ long x; }};' if name != 'bit_bound' else f'{application} enum E {{ A,B }};'
                ast = parse_extended(source)
                self.assertTrue(any(a['name'] == name and not a['ignored'] for n in ast['symbols'] for a in n.get('annotations', [])))
                wrong = source.replace(application, f'@{name}(unknown_member=1)', 1)
                with self.assertRaises(Diagnostic) as caught:
                    parse_extended(wrong)
                self.assertEqual(caught.exception.code, 'E_ANNOTATION')

    def test_semantic_ast_keeps_relationships(self):
        ast = parse_extended((CORPUS / 'positive/ccm.idl').read_text())
        component = symbol(ast, '::C')
        self.assertEqual(component['supports'], ['::I'])
        self.assertEqual(symbol(ast, '::C::out_events')['target'], '::Event')
        self.assertTrue(symbol(ast, '::C::source')['multiple'])
        self.assertEqual(symbol(ast, '::H')['manages'], '::C')
        self.assertEqual(symbol(ast, '::H')['primarykey'], '::Key')

    def test_adjacent_strings_in_directives_and_context(self):
        ast = parse_extended('interface I { void f() context("tr" "ace"); }; '
                             'typeid I "IDL:Ex/" "I:1.0"; '
                             'typeprefix :: "example." "org"; '
                             'module M { typedef long T; }; import "IDL:" "Ex/I:1.0";')
        self.assertEqual(symbol(ast, '::I')['repository_id'], 'IDL:Ex/I:1.0')
        self.assertEqual(symbol(ast, '::I::f')['contexts'], ['trace'])
        self.assertEqual(ast['declarations'][-1]['target'], '::I')
        self.assertEqual(ast['declarations'][2]['value'], 'example.org')

    def test_review_annotation_coercion_and_bounds(self):
        ast = parse_extended('@max(0.1) typedef float F; const F X=0.1; '
                             'struct S { @default(16777217.0) float x; @default(-0.0) float zero; };')
        maximum = symbol(ast, '::F')['annotations'][0]['arguments']['value']
        self.assertEqual(maximum['format'], 'float')
        self.assertEqual(rational(maximum), rational(symbol(ast, '::X')['value']))
        default = symbol(ast, '::S::x')['annotations'][0]['arguments']['value']
        self.assertEqual(default['format'], 'float')
        self.assertEqual(rational(default), 16777216)
        self.assertTrue(symbol(ast, '::S::zero')['annotations'][0]['arguments']['value']['negative_zero'])
        with self.assertRaises(Diagnostic) as caught:
            parse_extended('@max(0.1) typedef float F; const F X=0.10000002;')
        self.assertEqual(caught.exception.code, 'E_RANGE')

    def test_review_custom_numeric_context(self):
        ast = parse_extended('@annotation A { long long n default 4294967296; long double x default 1.0e400; }; '
                             '@A struct S { long x; }; @A(n=4294967297,x=1.0e401) struct T { long x; };')
        arguments = symbol(ast, '::S')['annotations'][0]['arguments']
        self.assertEqual(arguments['n'], 4294967296)
        self.assertEqual(arguments['x']['format'], 'long double')
        self.assertGreater(rational(arguments['x']), power2(1000))
        self.assertEqual(symbol(ast, '::T')['annotations'][0]['arguments']['n'], 4294967297)

    def test_review_annotation_scope_restores(self):
        ast = parse_extended((CORPUS / 'positive/annotation_local_const.idl').read_text())
        self.assertEqual(symbol(ast, '::S')['annotations'][0]['arguments']['x'], 8)
        self.assertEqual(symbol(ast, '::After')['value'], 99)
        self.assertTrue(any(r['target'] == '::A::N' for r in ast['references']))
        with self.assertRaises(Diagnostic) as caught:
            parse_extended('@annotation A { const long N=7; long x; }; const long Outside=A::N;')
        self.assertEqual(caught.exception.code, 'E_SCOPE')

    def test_review_forward_ports_and_cycle(self):
        ast = parse_extended((CORPUS / 'positive/forward_port_component.idl').read_text())
        self.assertEqual(symbol(ast, '::C::endpoint')['target'], '::P')
        self.assertTrue(symbol(ast, '::P')['complete'])
        with self.assertRaises(Diagnostic) as caught:
            parse_extended((CORPUS / 'negative/port_cycle.idl').read_text())
        self.assertEqual(caught.exception.code, 'E_PORT_CYCLE')

    def test_review_primary_key_ancestry(self):
        ast = parse_extended((CORPUS / 'positive/ccm.idl').read_text())
        self.assertEqual(symbol(ast, '::Key')['bases'], ['::Components::PrimaryKeyBase'])
        with self.assertRaises(Diagnostic) as caught:
            parse_extended((CORPUS / 'negative/primary_key_ancestry.idl').read_text())
        self.assertEqual(caught.exception.code, 'E_TYPE')

    def test_generic_primary_key_defers_then_enforces(self):
        ast = parse_extended((CORPUS / 'positive/generic_primarykey.idl').read_text())
        self.assertTrue(symbol(ast, '::T::H')['primarykey_constraint']['deferred'])
        self.assertFalse(symbol(ast, '::Actual::H')['primarykey_constraint']['deferred'])
        self.assertEqual(symbol(ast, '::Actual::H')['primarykey'], '::Key')
        with self.assertRaises(Diagnostic) as caught:
            parse_extended((CORPUS / 'negative/generic_primarykey_wrong.idl').read_text())
        self.assertEqual(caught.exception.code, 'E_TYPE')

    def test_consistent_repeated_annotation(self):
        ast = parse_extended((CORPUS / 'positive/annotation_repeat.idl').read_text())
        self.assertEqual(sum(n['qualified_name'] == '::A' for n in ast['symbols']), 1)
        self.assertEqual(symbol(ast, '::S')['annotations'][0]['arguments']['x'], 1)

    def test_standard_any_target_numeric_context(self):
        ast = parse_extended((CORPUS / 'positive/standard_any_extended_default.idl').read_text())
        self.assertEqual(symbol(ast, '::S::x')['annotations'][0]['arguments']['value']['format'], 'long double')
        self.assertEqual(symbol(ast, '::S::y')['annotations'][0]['arguments']['value'], 4294967296)


class NumericTests(unittest.TestCase):
    def test_ieee_binary64_oracle(self):
        ast = parse_extended('const double X=0.1+0.2; const double Y=1.0/10.0;')
        self.assertEqual(rational(symbol(ast, '::X')['value']), Fraction.from_float(0.1+0.2))
        self.assertEqual(rational(symbol(ast, '::Y')['value']), Fraction.from_float(1.0/10.0))

    def test_ieee_binary32_oracle(self):
        ast = parse_extended('const float X=16777217.0; const float Y=0.1;')
        self.assertEqual(rational(symbol(ast, '::X')['value']), Fraction(16777216))
        expected = struct.unpack('!f', struct.pack('!f', 0.1))[0]
        self.assertEqual(rational(symbol(ast, '::Y')['value']), Fraction.from_float(expected))

    def test_ties_to_even_and_subnormal(self):
        self.assertEqual(Binary.rounded(1+power2(-53)).value, Fraction(1))
        self.assertEqual(Binary.rounded(1+3*power2(-53)).value, 1+power2(-51))
        self.assertEqual(Binary.rounded(power2(-1074)).value, power2(-1074))
        self.assertEqual(Binary.rounded(power2(-1075)).value, Fraction(0))
        self.assertEqual(Binary.rounded(3*power2(-1075)).value, power2(-1073))

    def test_extended_precision_and_range(self):
        value = Binary.rounded(1+power2(-63), 'long double')
        self.assertEqual(value.value, 1+power2(-63))
        self.assertEqual(Binary.rounded(1+power2(-64), 'long double').value, 1)
        with self.assertRaises(NumericError):
            Binary.rounded(power2(16384), 'long double')

    def test_fixed_exact_and_truncation(self):
        ast = parse_extended((CORPUS / 'positive/fixed_precision.idl').read_text())
        self.assertEqual(symbol(ast, '::X')['value'], '-1234567890123456789012345678901')
        self.assertEqual(symbol(ast, '::Y')['value'], '0.'+'3'*31)
        self.assertEqual(symbol(ast, '::Z')['value'], '12.500')

    def test_integer_promotions(self):
        ast = parse_extended((CORPUS / 'positive/integer_promotions.idl').read_text())
        self.assertEqual(symbol(ast, '::B')['value'], 4294967297)
        self.assertEqual(symbol(ast, '::C')['value'], -2**63)
        self.assertEqual(symbol(ast, '::D')['value'], 2**32-1)

    def test_signed_zero_and_extreme_export(self):
        ast = parse_extended((CORPUS / 'positive/float_signed_zero.idl').read_text())
        self.assertTrue(symbol(ast, '::N')['value']['negative_zero'])
        self.assertFalse(symbol(ast, '::P')['value']['negative_zero'])
        self.assertTrue(symbol(ast, '::F')['value']['negative_zero'])
        huge = parse_extended('const long double X=1.0e4932;')['declarations'][0]['value']
        self.assertLessEqual(len(huge['coefficient'].lstrip('-')), 20)
        self.assertGreater(rational(huge), power2(16000))

    def test_binary64_deterministic_independent_samples(self):
        # Host Python/struct is an independent IEEE oracle, not the frontend's
        # rational implementation. Exercise normal and subnormal conversions.
        import random
        rng = random.Random(142)
        for _ in range(160):
            bits = rng.randrange(0x7ff0000000000000)
            number = struct.unpack('!d', bits.to_bytes(8, 'big'))[0]
            expected = Fraction.from_float(number)
            self.assertEqual(Binary.rounded(expected).value, expected)
            source = 'const double X='+repr(number)+';'
            self.assertEqual(rational(parse_extended(source)['declarations'][0]['value']), expected)
        for _ in range(40):
            a, b = rng.uniform(-10000, 10000), rng.uniform(1, 10000)
            for operator, expected in (('+', a+b), ('-', a-b), ('*', a*b), ('/', a/b)):
                source = f'const double X=({a!r}){operator}({b!r});'
                self.assertEqual(rational(parse_extended(source)['declarations'][0]['value']), Fraction.from_float(expected))


class CompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        value = os.environ.get('IDL_CLANG')
        if not value:
            raise RuntimeError('IDL_CLANG is required; compiler-backed checks cannot be skipped')
        cls.clang = Path(value)

    def test_compiler_corpus(self):
        for case in CASES:
            with self.subTest(case=case['file'], profile=case['profile']):
                result = compile_file(CORPUS / case['file'], self.clang, profile=case['profile'])
                expected = case.get('preprocess_diagnostic', case['diagnostic'])
                self.assertEqual(result['status'], 'FAIL' if expected else 'PASS', result)
                self.assertEqual([d['code'] for d in result['diagnostics']], [expected] if expected else [])

    def test_imports_and_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            child = root / 'types.idl'
            child.write_text('module Types { typedef long Id; };')
            main = root / 'main.idl'
            main.write_text('import "IDL:Types:" "1.0"; struct S { Types::Id value; };')
            result = compile_file(main, self.clang, profile='b2', import_bindings={'IDL:Types:1.0': child})
            self.assertEqual(result['status'], 'PASS', result)
            self.assertIn(str(child), result['dependencies'])
            self.assertEqual(len(result['import_stages']), 1)
            self.assertIn('::Types::Id', {n['qualified_name'] for n in result['ast']['symbols']})
            alias = root / 'out.json'
            os.link(child, alias)
            before = child.read_bytes()
            command = [sys.executable, str(ROOT / 'scripts/standards/compile_idl.py'), str(main),
                       '--clang', str(self.clang), '--profile', 'b2', '--out', str(alias),
                       '--idl-import', 'IDL:Types:1.0='+str(child)]
            process = subprocess.run(command, capture_output=True, timeout=30)
            self.assertNotEqual(process.returncode, 0)
            self.assertEqual(child.read_bytes(), before)

    def test_import_cycle_and_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            a, b = root / 'a.idl', root / 'b.idl'
            a.write_text('import "IDL:B:1.0"; module A { typedef long T; };')
            b.write_text('import "IDL:A:1.0"; module B { typedef long T; };')
            self.assertEqual(compile_file(a, self.clang, profile='b2', import_bindings={'IDL:A:1.0': a, 'IDL:B:1.0': b})['diagnostics'][0]['code'], 'E_IMPORT_CYCLE')
            a.write_text('import "IDL:Missing:1.0";')
            self.assertEqual(compile_file(a, self.clang, profile='b2')['diagnostics'][0]['code'], 'E_IMPORT')

    def test_repository_binding_shared_include(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'shared.idl').write_text('#ifndef SHARED\n#define SHARED\nmodule Shared { typedef long Id; };\n#endif\n')
            a, b, main = root / 'a.idl', root / 'b.idl', root / 'main.idl'
            a.write_text('#include "shared.idl"\nmodule A { typedef Shared::Id Id; };')
            b.write_text('#include "shared.idl"\nmodule B { typedef Shared::Id Id; };')
            main.write_text('import "IDL:A:1.0"; import "IDL:B:1.0"; struct S { A::Id x; B::Id y; };')
            result = compile_file(main, self.clang, profile='b2', import_bindings={'IDL:A:1.0': a, 'IDL:B:1.0': b})
            self.assertEqual(result['status'], 'PASS', result)
            self.assertEqual(sum(n['qualified_name'] == '::Shared::Id' for n in result['ast']['symbols']), 1)

    def test_repository_binding_identity_mismatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            child, main = root / 'types.idl', root / 'main.idl'
            child.write_text('module Different { typedef long Id; };')
            main.write_text('import "IDL:Expected:1.0";')
            result = compile_file(main, self.clang, profile='b2', import_bindings={'IDL:Expected:1.0': child})
            self.assertEqual(result['diagnostics'][0]['code'], 'E_IMPORT')

    def test_cli_profile_ast(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'ast.json'
            command = [sys.executable, str(ROOT / 'scripts/standards/compile_idl.py'),
                       str(CORPUS / 'positive/profile_rpc-dds.idl'), '--clang', str(self.clang),
                       '--profile', 'rpc-dds', '--out', str(output)]
            result = subprocess.run(command, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr)
            artifact = json.loads(output.read_text())
            self.assertEqual(artifact['ast']['profile'], 'rpc-dds')
            self.assertEqual(artifact['ast']['schema'], 2)


if __name__ == '__main__':
    unittest.main()
