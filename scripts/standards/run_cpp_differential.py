"""Build independent B3 and DDS-Gen outputs; compare wire, keys and TypeIdentifiers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from compile_idl import digest
from generate_cpp import generate_cpp
from generate_types import GENERATORS
from idl_frontend import LINE_MARKER


def execute(argv, directory, label, env=None, timeout=180):
    record = {'command': list(map(str, argv)), 'exit_code': None}
    try:
        result = subprocess.run(record['command'], capture_output=True, env=env, timeout=timeout)
        stdout, stderr = result.stdout, result.stderr
        record['exit_code'] = result.returncode
    except subprocess.TimeoutExpired as error:
        stdout, stderr = error.stdout or b'', error.stderr or b''
        record['error'] = 'timeout'
    except OSError as error:
        stdout, stderr = b'', str(error).encode()
        record['error'] = str(error)
    (directory / (label + '.stdout.log')).write_bytes(stdout)
    (directory / (label + '.stderr.log')).write_bytes(stderr)
    (directory / (label + '.command.json')).write_text(json.dumps(record, indent=2), encoding='utf-8')
    if record['exit_code'] != 0:
        raise RuntimeError(f'{label} failed: exit={record["exit_code"]}; see retained logs')
    return stdout.decode('utf-8', 'replace')


def run_differential(args):
    root = Path(__file__).resolve().parents[2]
    out = args.out.absolute()
    out.mkdir(parents=True, exist_ok=False)
    report = {'status': 'FAIL', 'fastdds': args.fastdds, 'fixtures': [], 'device_test': 'NOT_RUN'}
    report['harness_sources'] = {str(p): digest(p) for p in [Path(__file__),
        root / 'integrations/standards/cpp_backend/CMakeLists.txt', root / 'integrations/standards/cpp_backend/backend_contract.cpp']}
    try:
        if digest(args.generator) != args.generator_sha256.lower():
            raise ValueError('generator SHA256 mismatch')
        version = execute([args.java, '-jar', args.generator, '-version'], out, 'generator-version', timeout=30)
        if not re.search(r'fastddsgen version ' + re.escape(GENERATORS[args.fastdds]) + r'\s*$', version):
            raise ValueError('generator version does not match the selected Fast DDS release')
        report['tools'] = {str(Path(p).resolve()): digest(p) for p in [args.java, args.generator, args.clang]}
        env = dict(os.environ)
        env['PATH'] = os.pathsep.join(map(str, args.runtime_dir)) + os.pathsep + env.get('PATH', '')
        for stem in ('BoundedRecord', 'B3Cases'):
            source = root / 'integrations/standards/idl' / (stem + '.idl')
            case = out / stem
            case.mkdir()
            new_dir, reference = case / 'new', case / 'reference'
            generated = generate_cpp(source, args.clang, new_dir, args.fastdds)
            if generated['status'] != 'PASS':
                raise RuntimeError(f'{stem} new backend rejected the input: {generated["diagnostics"]}')
            # Both parsers receive the same explicitly recorded preprocessor output.
            preprocessed = execute(generated['command'], case, 'reference-preprocess', timeout=30)
            if hashlib.sha256(preprocessed.encode('utf-8')).hexdigest() != generated['preprocessed_sha256']:
                raise RuntimeError('preprocessor changed between frontend and reference stages')
            input_dir = case / 'reference-input'
            input_dir.mkdir()
            input_file = input_dir / source.name
            input_file.write_text('\n'.join(line for line in preprocessed.splitlines() if not LINE_MARKER.fullmatch(line)) + '\n', encoding='utf-8')
            reference.mkdir()
            command = [args.java, '-jar', args.generator, '-ppDisable', '-flat-output-dir', '-d', reference, input_file]
            execute(command, case, 'reference-generate', timeout=60)
            hashes = {p.name: digest(p) for p in reference.iterdir() if p.suffix in ('.h', '.hpp', '.cxx', '.ipp')}
            manifest = {'status': 'PASS', 'fastdds': args.fastdds, 'idl_sha256': digest(source),
                        'preprocessed_sha256': generated['preprocessed_sha256'],
                        'dependencies': generated['dependencies'],
                        'reference_input_sha256': digest(input_file), 'generator_version': GENERATORS[args.fastdds],
                        'generator_sha256': args.generator_sha256.lower(), 'command': list(map(str, command)), 'generated': hashes}
            if not hashes:
                raise RuntimeError('reference generator produced no C++')
            (reference / 'generation.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
            executables, observed = {}, {}
            for name, directory in [('new', new_dir), ('reference', reference)]:
                build = case / (name + '-build')
                configure = ['cmake', '-S', root / 'integrations/standards/cpp_backend', '-B', build,
                    f'-DDDS_STANDARDS_FASTDDS={args.fastdds}', f'-DB3_GENERATED_DIR={directory}', f'-DB3_IDL={source}'] + args.cmake_arg
                execute(configure, case, name + '-configure')
                execute(['cmake', '--build', build, '--config', 'Release', '--target', 'b3_contract'], case, name + '-build')
                exe = build / ('b3_contract' if args.build_only else 'Release/b3_contract.exe')
                # Single-configuration host builds may place the executable at the root.
                if not exe.is_file() and not args.build_only:
                    exe = build / ('b3_contract.exe' if os.name == 'nt' else 'b3_contract')
                executables[name] = str(exe)
                if args.build_only:
                    continue
                output = execute([exe], case, name + '-contract', env=env, timeout=30)
                if 'B3_CONTRACT PASS' not in output:
                    raise RuntimeError('contract returned without PASS marker')
                observed[name] = sorted(line for line in output.splitlines() if line.startswith(('B3_WIRE ', 'B3_KEY ', 'B3_TYPE ')))
            item = {'fixture': stem, 'source_sha256': digest(source), 'executables': executables,
                    'comparison': 'NOT_RUN' if args.build_only else 'PASS'}
            item['executable_sha256'] = {name: digest(path) for name, path in executables.items()}
            report['fixtures'].append(item)
            if args.build_only:
                continue
            if observed['new'] != observed['reference']:
                (case / 'comparison.json').write_text(json.dumps(observed, indent=2), encoding='utf-8')
                item['comparison'] = 'FAIL'
                raise RuntimeError(stem + ' wire/key/TypeIdentifier comparison differs')
            item['compared_records'] = len(observed['new'])
            if args.dds:
                item['dds'] = []
                for rep in (0, 2):
                    for writer_variant, reader_variant in [('new', 'reference'), ('reference', 'new')]:
                        label = f'dds-{rep}-{writer_variant}-to-{reader_variant}'
                        argv = [executables[reader_variant], '--dds', 'reader', str(args.domain), label, str(rep)]
                        with (case / (label + '-reader.stdout.log')).open('wb') as stdout, (case / (label + '-reader.stderr.log')).open('wb') as stderr:
                            reader = subprocess.Popen(argv, stdout=stdout, stderr=stderr, env=env)
                            try:
                                execute([executables[writer_variant], '--dds', 'writer', str(args.domain), label, str(rep)], case, label + '-writer', env=env, timeout=22)
                                reader_code = reader.wait(timeout=20)
                                if reader_code:
                                    raise RuntimeError(f'{label} reader exit {reader_code}')
                            finally:
                                if reader.poll() is None:
                                    reader.kill()
                                    reader.wait()
                        item['dds'].append({'representation': rep, 'writer': writer_variant, 'reader': reader_variant, 'status': 'PASS', 'reader_command': argv, 'reader_exit_code': reader_code})
        combined_source = root / 'integrations/standards/idl/B3Combined.idl'
        combined_dir = out / 'combined'
        combined = generate_cpp(combined_source, args.clang, combined_dir, args.fastdds)
        if combined['status'] != 'PASS':
            raise RuntimeError('combined IDL generation failed')
        directories = [out / stem / 'new' for stem in ('BoundedRecord', 'B3Cases')] + [combined_dir]
        combined_build = out / 'combination-build'
        execute(['cmake', '-S', root / 'integrations/standards/cpp_backend/combination', '-B', combined_build,
                 f'-DDDS_STANDARDS_FASTDDS={args.fastdds}', '-DB3_DIRS=' + ';'.join(map(str, directories))] + args.cmake_arg,
                 out, 'combination-configure')
        execute(['cmake', '--build', combined_build, '--config', 'Release', '--target', 'b3_combination'], out, 'combination-build')
        combined_exe = combined_build / ('b3_combination' if args.build_only else 'Release/b3_combination.exe')
        if not combined_exe.exists() and not args.build_only:
            combined_exe = combined_build / ('b3_combination.exe' if os.name == 'nt' else 'b3_combination')
        report['combination'] = {'executable': str(combined_exe), 'sha256': digest(combined_exe),
                                 'status': 'BUILDS' if args.build_only else 'PASS'}
        if not args.build_only:
            output = execute([combined_exe], out, 'combination-contract', env=env, timeout=30)
            if 'B3_COMBINATION PASS' not in output:
                raise RuntimeError('combination returned without PASS marker')
        report['status'] = 'BUILDS' if args.build_only else 'PASS'
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report['error'] = str(error)
    (out / 'differential-summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('java', 'generator', 'clang', 'out'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--generator-sha256', required=True)
    parser.add_argument('--fastdds', choices=GENERATORS, required=True)
    parser.add_argument('--cmake-arg', action='append', default=[])
    parser.add_argument('--runtime-dir', action='append', default=[], type=Path)
    parser.add_argument('--dds', action='store_true')
    parser.add_argument('--domain', type=int, default=219)
    parser.add_argument('--build-only', action='store_true', help='Cross-build only; comparisons and execution remain NOT_RUN')
    args = parser.parse_args(argv)
    if args.dds and args.build_only:
        parser.error('--dds and --build-only are mutually exclusive')
    if not 0 <= args.domain <= 230:
        parser.error('domain must be between 0 and 230')
    report = run_differential(args)
    print(report['status'])
    if 'error' in report:
        print(report['error'])
    return 0 if report['status'] in ('PASS', 'BUILDS') else 1


if __name__ == '__main__':
    raise SystemExit(main())
