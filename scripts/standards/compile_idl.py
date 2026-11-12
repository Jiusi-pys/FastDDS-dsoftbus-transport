"""Preprocess IDL with an explicit Clang executable and emit a validated JSON AST.

Usage: python scripts/standards/compile_idl.py input.idl --clang PATH --out FILE
Clang is a build-time dependency, not part of the DDS runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from idl_frontend import Diagnostic, LINE_MARKER, Token, marker_file, parse


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compile_file(source, clang, includes=(), defines=()):
    source, clang = Path(source).resolve(), Path(clang).resolve()
    evidence = {"source": str(source), "preprocessor": str(clang), "status": "FAIL"}
    try:
        evidence['source_sha256'] = digest(source)
        evidence['preprocessor_sha256'] = digest(clang)
        version = subprocess.run([str(clang), '--version'], capture_output=True, timeout=15)
        evidence['version_exit_code'] = version.returncode
        evidence['version'] = (version.stdout + version.stderr).decode('utf-8', 'replace')
        if version.returncode or not re.search(r'clang version \d+', evidence['version']):
            raise ValueError('preprocessor must identify itself as Clang')
        command = [str(clang), '-E', '-x', 'c++', '-std=c++03', '-undef', '-nostdinc',
                   '-Werror', '-Wno-invalid-pp-token']
        command += [arg for path in includes for arg in ('-I', str(Path(path).resolve()))]
        command += [arg for value in defines for arg in ('-D', value)]
        command += [str(source)]
        evidence['command'] = command
        process = subprocess.run(command, capture_output=True, timeout=30)
        evidence['preprocessor_exit_code'] = process.returncode
        evidence['stderr'] = process.stderr.decode('utf-8', 'replace')
        text = process.stdout.decode('utf-8')
        evidence['preprocessed_sha256'] = hashlib.sha256(process.stdout).hexdigest()
        evidence['dependencies'] = {str(source): digest(source)}
        for line in text.splitlines():
            marker = LINE_MARKER.fullmatch(line)
            if marker and not marker_file(marker[2]).startswith('<'):
                path = Path(marker_file(marker[2])).resolve()
                if path.is_file():
                    evidence['dependencies'][str(path)] = digest(path)
        if process.returncode:
            raise Diagnostic('E_PREPROCESS', 'Clang preprocessing failed; see stderr',
                             Token('', str(source), 1, 1))
        evidence['ast'] = parse(text, str(source))
        evidence['status'] = 'PASS'
        evidence['diagnostics'] = []
    except Diagnostic as error:
        evidence['diagnostics'] = [error.to_dict()]
    except (OSError, ValueError, UnicodeError, subprocess.TimeoutExpired) as error:
        evidence['diagnostics'] = [{"code": "E_TOOL", "message": str(error),
                                    "location": {"file": str(source), "line": 1, "column": 1}}]
    return evidence


def output_conflicts(output, inputs):
    output = Path(output)
    for input_ in inputs:
        input_ = Path(input_)
        if output.resolve() == input_.resolve():
            return True
        if output.exists() and input_.exists() and output.samefile(input_):
            return True
    return False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('-I', dest='includes', action='append', default=[])
    parser.add_argument('-D', dest='defines', action='append', default=[])
    args = parser.parse_args(argv)
    # The evidence artifact must never overwrite compiler input or tool binaries.
    protected = {args.source.resolve(), args.clang.resolve()}
    try:
        if output_conflicts(args.out, protected):
            parser.error('--out must differ from source and Clang executable, including file aliases')
    except OSError as error:
        parser.error(f'cannot verify output file identity: {error}')
    result = compile_file(args.source, args.clang, args.includes, args.defines)
    try:
        if output_conflicts(args.out, result.get('dependencies', {})):
            parser.error('--out must differ from included source files, including file aliases')
    except OSError as error:
        parser.error(f'cannot verify output file identity: {error}')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Replace an artifact atomically; do not truncate through an output symlink
    # or hardlink introduced after the identity check.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='\n',
                                         dir=args.out.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(json.dumps(result, indent=2, ensure_ascii=True) + '\n')
        os.replace(temporary, args.out)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(result['status'])
    for diagnostic in result['diagnostics']:
        location = diagnostic['location']
        print(f"{location['file']}:{location['line']}:{location['column']}: "
              f"{diagnostic['code']}: {diagnostic['message']}", file=sys.stderr)
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
