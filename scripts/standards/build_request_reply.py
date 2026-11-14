"""Build the D3 bounded request/reply library and independent public-DDS peer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot():
    files = [p for p in (ROOT / 'integrations/standards/request_reply').rglob('*') if p.is_file()]
    files += [ROOT / 'integrations/standards/idl/RpcMessage.idl']
    directory = ROOT / 'scripts/standards'
    files += list(directory.glob('idl_*.py')) + list((directory / 'templates').glob('*.hpp'))
    files += [directory / name for name in ('generate_cpp.py', 'compile_idl.py', 'build_request_reply.py', 'run_request_reply_acceptance.py')]
    return {str(p.relative_to(ROOT)): sha(p) for p in sorted(set(files))}


def build_request_reply(args):
    out = args.out.resolve()
    if os.name != 'nt' or out.exists() or not out.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('use a new Windows workspace build directory')
    for option in args.cmake_arg:
        if not re.fullmatch(r'-D(?:CMAKE_PREFIX_PATH|fastrtps_DIR|fastdds_DIR|fastcdr_DIR)(?::(?:PATH|STRING))?=.+', option):
            raise ValueError('only DDS dependency discovery paths may be overridden')
    before = snapshot()
    clang = args.clang.resolve()
    runtime = [str(p.resolve()) for p in args.runtime_dir]
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(runtime) + os.pathsep + env.get('PATH', '')
    out.mkdir(parents=True)
    summary = dict(status='FAIL', fastdds=args.fastdds, source_snapshot=before, clang=str(clang), clang_sha256=sha(clang))
    commands = []

    def execute(name, argv):
        argv = [str(x).replace('\\', '/') if name == 'configure' else str(x) for x in argv]
        try:
            process = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=900)
            code, output = process.returncode, process.stdout
        except subprocess.TimeoutExpired as error:
            code, output = 'TIMEOUT', error.stdout or b''
        (out / (name + '.log')).write_bytes(output)
        commands.append(dict(name=name, argv=argv, exit_code=code))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        if code != 0:
            raise RuntimeError(name + ' failed: ' + str(out / (name + '.log')))

    try:
        execute('generate', [sys.executable, '-B', ROOT / 'scripts/standards/generate_cpp.py', ROOT / 'integrations/standards/idl/RpcMessage.idl',
                             '--clang', clang, '--out', out / 'generated', '--fastdds', args.fastdds])
        execute('configure', ['cmake', '-S', ROOT / 'integrations/standards/request_reply', '-B', out / 'build', *args.cmake_arg,
                              '-DDDS_STANDARDS_FASTDDS=' + args.fastdds, '-DD3_GENERATED_DIR=' + str(out / 'generated'),
                              '-DD3_IDL=' + str(ROOT / 'integrations/standards/idl/RpcMessage.idl')])
        execute('build', ['cmake', '--build', out / 'build', '--config', 'Release', '--parallel', '4'])
        execute('ctest', ['ctest', '--test-dir', out / 'build', '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', out / 'ctest.xml'])
        generation = json.loads((out / 'generated/generation.json').read_text(encoding='utf-8'))
        generated = {str(out / 'generated' / name): digest for name, digest in generation['generated'].items()}
        if snapshot() != before or sha(clang) != summary['clang_sha256'] or any(sha(p) != h for p, h in generated.items()):
            raise RuntimeError('source/toolchain/generated input changed during build')
        dependency = json.loads((out / 'build/d3-dependency.json').read_text(encoding='utf-8'))
        if Path(dependency['runtime']).resolve().parent not in {Path(p) for p in runtime}:
            raise RuntimeError('resolved DDS DLL outside declared runtime directories')
        artifacts = [out / 'build/Release' / (name + '.exe') for name in ('d3_actor', 'd3_peer', 'd3_contract')]
        artifacts += [Path(dependency['library']), Path(dependency['runtime'])]
        summary.update(status='PASS', source_unchanged=True, generated_sha256=generated, dependency=dependency,
                       artifact_sha256={str(p): sha(p) for p in artifacts},
                       bundle=dict(abi=args.fastdds, actor=str(artifacts[0]), peer=str(artifacts[1]), runtime_dirs=runtime,
                                   build_summary=str(out / 'summary.json')))
    finally:
        summary['commands'] = commands
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D3_BUILD PASS abi=' + args.fastdds, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fastdds', choices=['2.14.5', '3.6.2'], required=True)
    for name in ('out', 'clang'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', action='append', type=Path, default=[])
    parser.add_argument('--cmake-arg', action='append', default=[])
    try:
        build_request_reply(parser.parse_args())
    except Exception as error:
        parser.exit(1, str(error) + '\n')
