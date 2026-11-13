"""Generate version-bound DDS C++ from the B2 semantic frontend (no DDS-Gen)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from compile_idl import compile_file
from idl_cpp import BackendError, CppBackend


def generate_cpp(source, clang, out, fastdds, includes=(), defines=()):
    source, out = Path(source).resolve(), Path(out).absolute()
    if not re.fullmatch(r'[A-Za-z][A-Za-z0-9_]*', source.stem):
        raise BackendError('source basename must be a portable C++ identifier')
    if source.stem.casefold() == 'b3support':
        raise BackendError('source basename B3Support is reserved by the backend')
    if out.exists():
        raise BackendError('output directory must not exist')
    evidence = compile_file(source, clang, includes, defines, profile='b2')
    evidence.update(fastdds=fastdds, backend='standards-b3-cpp-v1', idl_sha256=evidence.get('source_sha256'))
    files = {}
    if evidence['status'] == 'PASS':
        try:
            files = CppBackend(evidence['ast'], source.stem, fastdds).emit()
            support = Path(__file__).with_name('templates') / 'b3_support.hpp'
            evidence['backend_sources'] = {str(p): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in [Path(__file__), Path(__file__).with_name('idl_cpp.py'),
                          Path(__file__).with_name('idl_cpp_typeobjects.py'), support]}
            signature = hashlib.sha256(json.dumps(sorted(evidence['backend_sources'].values())).encode()).hexdigest()[:16]
            abi = int(fastdds == '2.14.5')
            prefix = f'''#ifndef B3_FASTDDS_214
#define B3_FASTDDS_214 {abi}
#elif B3_FASTDDS_214 != {abi}
#error Mixed Fast DDS ABIs in B3 generated artifacts
#endif
#ifndef B3_RUNTIME_SIGNATURE
#define B3_RUNTIME_SIGNATURE 0x{signature}ULL
#elif B3_RUNTIME_SIGNATURE != 0x{signature}ULL
#error Mixed B3 backend revisions in generated artifacts
#endif
'''
            files['B3Support.hpp'] = prefix + support.read_text(encoding='utf-8')
        except BackendError as error:
            evidence['status'] = 'FAIL'
            evidence['diagnostics'] = [{'code': 'E_BACKEND', 'message': str(error)}]
    evidence['generated'] = {name: hashlib.sha256(text.encode('utf-8')).hexdigest() for name, text in files.items()}
    out.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.b3-', dir=out.parent))
    try:
        for name, text in files.items():
            (staging / name).write_text(text, encoding='utf-8', newline='\n')
        (staging / 'generation.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
        # Never merge into or replace a destination owned by another invocation.
        os.rename(staging, out)
    finally:
        if staging.exists():
            if staging.resolve().parent != out.parent.resolve() or not staging.name.startswith('.b3-'):
                raise RuntimeError('refusing cleanup outside the owned staging directory')
            shutil.rmtree(staging)
    return evidence


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('--clang', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--fastdds', choices=['2.14.5', '3.6.2'], required=True)
    parser.add_argument('-I', dest='includes', action='append', default=[])
    parser.add_argument('-D', dest='defines', action='append', default=[])
    args = parser.parse_args(argv)
    try:
        evidence = generate_cpp(args.source, args.clang, args.out, args.fastdds, args.includes, args.defines)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(evidence['status'])
    for diagnostic in evidence['diagnostics']:
        print(diagnostic['code'] + ': ' + diagnostic['message'])
    return 0 if evidence['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
