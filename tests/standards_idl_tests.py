"""B1 corpus, semantic AST, preprocessor and command-line contracts."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr
import io

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts/standards'))
from idl_frontend import Diagnostic, parse
from compile_idl import compile_file, main

CORPUS = ROOT / 'tests/standards_idl'


class CorpusTests(unittest.TestCase):
    def test_corpus(self):
        cases = json.loads((CORPUS / 'corpus.json').read_text())
        self.assertEqual({p.relative_to(CORPUS).as_posix() for p in CORPUS.rglob('*.idl')},
                         {c['file'] for c in cases})
        for case in cases:
            with self.subTest(case=case['file']):
                path = CORPUS / case['file']
                if case['diagnostic']:
                    with self.assertRaises(Diagnostic) as caught:
                        parse(path.read_text(), str(path))
                    self.assertEqual(caught.exception.code, case['diagnostic'])
                    self.assertGreaterEqual(caught.exception.token.line, 1)
                    self.assertGreaterEqual(caught.exception.token.column, 1)
                else:
                    ast = parse(path.read_text(), str(path))
                    self.assertEqual(ast['profile'], 'b1-core')
                    json.dumps(ast)  # The complete AST must be serializable.

    def test_ast_and_binding(self):
        ast = parse('typedef long T; module M { typedef short T; struct S { T x; ::T y; }; };')
        self.assertEqual([r['target'] for r in ast['references']], ['::M::T', '::T'])
        fields = ast['declarations'][1]['declarations'][1]['members']
        self.assertEqual(fields[0]['type'], {'kind': 'named', 'name': '::M::T'})
        self.assertEqual(fields[1]['name'], 'y')

    def test_constant_values(self):
        ast = parse((CORPUS / 'positive/operators.idl').read_text())
        self.assertEqual([n['value'] for n in ast['declarations']], [43, -2, -1, 5])

    def test_decimal_unary_preserves_precision(self):
        ast = parse((CORPUS / 'positive/fixed_negative_precision.idl').read_text())
        self.assertEqual(ast['declarations'][0]['value'], '-1234567890123456789012345678901')

    def test_module_reopen_merges_ast(self):
        ast = parse((CORPUS / 'positive/reopen.idl').read_text())
        self.assertEqual(len(ast['declarations']), 1)
        self.assertEqual([n['value'] for n in ast['declarations'][0]['declarations']], [1, 2])

    def test_exact_diagnostic_location(self):
        with self.assertRaises(Diagnostic) as caught:
            parse('module M {\n  struct S { Missing x; };\n};', 'example.idl')
        self.assertEqual(caught.exception.to_dict(), {
            'code': 'E_UNRESOLVED', 'message': 'unknown name Missing',
            'location': {'file': 'example.idl', 'line': 2, 'column': 14}})

    def test_line_markers(self):
        with self.assertRaises(Diagnostic) as caught:
            parse('# 17 "included.idl" 1\nstruct S { Unknown x; };')
        self.assertEqual((caught.exception.token.file, caught.exception.token.line), ('included.idl', 17))


class PreprocessorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        value = os.environ.get('IDL_CLANG')
        if not value:
            raise RuntimeError('IDL_CLANG must name the tested Clang executable; preprocessing is required')
        cls.clang = Path(value)

    def test_cpp03_preprocessing(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'dialect.idl'
            source.write_text('#if __cplusplus == 199711L\nconst long CPP03=1;\n'
                              '#else\n#error wrong preprocessing dialect\n#endif\n')
            result = compile_file(source, self.clang)
            self.assertEqual(result['status'], 'PASS', result)
            self.assertEqual(result['ast']['declarations'][0]['value'], 1)
            self.assertIn('c++', result['command'])
            self.assertIn('-std=c++03', result['command'])

    def test_cli_protects_file_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, include, fake_tool = [root / p for p in ('main.idl', 'types.idl', 'disposable-clang.exe')]
            source.write_text('#include "types.idl"\nconst long X=1;\n')
            include.write_text('typedef long Number;\n')
            fake_tool.write_bytes(b'disposable fixture, never executed')
            originals = {p: p.read_bytes() for p in (source, include, fake_tool)}
            (root / 'nested').mkdir()
            targets = [(source, self.clang), (root / 'nested/../main.idl', self.clang)]
            for index, protected in enumerate((source, include, fake_tool)):
                alias = root / f'hardlink{index}.json'
                os.link(protected, alias)
                targets.append((alias, fake_tool if protected == fake_tool else self.clang))
            for out, clang in targets:
                with self.subTest(out=out):
                    command = [sys.executable, str(ROOT / 'scripts/standards/compile_idl.py'),
                               str(source), '--clang', str(clang), '--out', str(out)]
                    result = subprocess.run(command, capture_output=True, timeout=30)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(b'--out must differ', result.stderr)
                    for path, original in originals.items():
                        self.assertEqual(path.read_bytes(), original)

    def test_identity_check_failure_is_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, out = Path(tmp) / 'main.idl', Path(tmp) / 'artifact.json'
            source.write_text('const long X=1;')
            out.write_bytes(b'original')
            with patch.object(Path, 'samefile', side_effect=OSError('identity check failed')):
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as caught:
                    main([str(source), '--clang', str(self.clang), '--out', str(out)])
            self.assertEqual(caught.exception.code, 2)
            self.assertEqual(out.read_bytes(), b'original')

    def test_compiler_backed_corpus(self):
        for case in json.loads((CORPUS / 'corpus.json').read_text()):
            with self.subTest(case=case['file']):
                result = compile_file(CORPUS / case['file'], self.clang)
                expected = case.get('preprocess_diagnostic', case['diagnostic'])
                self.assertEqual(result['status'], 'FAIL' if expected else 'PASS', result)
                self.assertEqual([d['code'] for d in result['diagnostics']], [expected] if expected else [])
                if not expected:
                    self.assertIn('symbols', result['ast'])
                    json.dumps(result['ast'])

    def test_include_macros_conditionals_and_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inc = root / 'types.idl'
            inc.write_text('#ifndef TYPES_IDL\n#define TYPES_IDL\ntypedef long Number;\n#endif\n')
            source = root / 'main.idl'
            source.write_text('#include "types.idl"\n#include "types.idl"\n#define TWICE(x) ((x)*2)\n'
                              '#if ENABLED\nconst Number LIMIT=TWICE(SIZE);\n#else\ninvalid\n#endif\n')
            result = compile_file(source, self.clang, [root], ['ENABLED=1', 'SIZE=3'])
            self.assertEqual(result['status'], 'PASS', result)
            self.assertEqual(result['ast']['declarations'][1]['value'], 6)
            self.assertEqual(result['dependencies'][str(inc.resolve())], hashlib.sha256(inc.read_bytes()).hexdigest())
            self.assertEqual(result['preprocessor_sha256'], hashlib.sha256(self.clang.read_bytes()).hexdigest())
            self.assertIn('clang version', result['version'])

    def test_included_diagnostic_location(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            inc = root / 'bad.idl'
            inc.write_text('// line one\nstruct S { Unknown x; };\n')
            source = root / 'main.idl'
            source.write_text('#include "bad.idl"\n')
            result = compile_file(source, self.clang)
            self.assertEqual(result['status'], 'FAIL')
            diagnostic = result['diagnostics'][0]
            self.assertEqual(diagnostic['code'], 'E_UNRESOLVED')
            self.assertEqual(Path(diagnostic['location']['file']), inc)
            self.assertEqual(diagnostic['location']['line'], 2)

    def test_preprocessor_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'bad.idl'
            for text in ('#error no\n', '#include "missing.idl"\n', '#if 1\ntypedef long X;\n'):
                with self.subTest(text=text):
                    path.write_text(text)
                    result = compile_file(path, self.clang)
                    self.assertEqual(result['status'], 'FAIL')
                    self.assertEqual(result['diagnostics'][0]['code'], 'E_PREPROCESS')
                    self.assertNotEqual(result['preprocessor_exit_code'], 0)

    def test_missing_tool(self):
        result = compile_file(CORPUS / 'positive/module.idl', CORPUS / 'missing-clang')
        self.assertEqual(result['diagnostics'][0]['code'], 'E_TOOL')

    def test_cli_success_and_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'ast.json'
            for name, expected in [('positive/module.idl', 0), ('negative/missing.idl', 1)]:
                command = [sys.executable, str(ROOT / 'scripts/standards/compile_idl.py'),
                           str(CORPUS / name), '--clang', str(self.clang), '--out', str(out)]
                result = subprocess.run(command, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, expected, result.stderr)
                evidence = json.loads(out.read_text())
                self.assertEqual(evidence['status'], 'PASS' if expected == 0 else 'FAIL')
                self.assertEqual('ast' in evidence, expected == 0)


if __name__ == '__main__':
    unittest.main()
