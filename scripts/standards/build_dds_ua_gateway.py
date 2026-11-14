"""Build the finite D2 DDS->UA host profile with pinned native and independent UA stacks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile

import build_ua_gateway as d1

ROOT = Path(__file__).resolve().parents[2]
ASYNCUA_WHEEL_SHA256 = '81d0c66b93b2ffdf97f672572d1ac975cc0b86a62b4c4e67034485dc0a2a2994'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot():
    files = [p for p in (ROOT / 'integrations/standards/dds_ua_server').rglob('*') if p.is_file()]
    files += [ROOT / 'scripts/standards' / name for name in
              ('build_dds_ua_gateway.py', 'run_dds_ua_acceptance.py', 'd2_ua_client.py')]
    return {**d1.snapshot(), **{str(p.relative_to(ROOT)): sha(p) for p in files}}


def verify_client(manifest):
    data = json.loads(Path(manifest).read_text(encoding='utf-8'))
    if data['asyncua_version'] != '2.1.0' or data['packages']['asyncua'] != '2.1.0':
        raise ValueError('asyncua 2.1.0 required')
    for collection in ('files', 'wheel_sha256'):
        if not data[collection] or any(sha(p) != digest for p, digest in data[collection].items()):
            raise ValueError('client toolchain changed: ' + collection)
    python = Path(data['python']).resolve()
    site = python.parents[1] / 'Lib/site-packages'
    actual = {str(p.resolve()) for p in site.rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc'}
    declared = {str(Path(p).resolve()) for p in data['files'] if Path(p).resolve().is_relative_to(site)}
    if actual != declared or str(python) not in data['files']:
        raise ValueError('client file inventory mismatch')
    wheels = [Path(p) for p, digest in data['wheel_sha256'].items() if digest == ASYNCUA_WHEEL_SHA256]
    if len(wheels) != 1:
        raise ValueError('official pinned asyncua wheel required')
    with zipfile.ZipFile(wheels[0]) as wheel:
        for name in wheel.namelist():
            if name.startswith('asyncua/') and not name.endswith('/'):
                if (site / name).read_bytes() != wheel.read(name):
                    raise ValueError('installed asyncua differs from pinned wheel')
    return data


def build(args):
    out = args.out.resolve()
    if os.name != 'nt' or out.exists() or not out.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('use a new Windows workspace build directory')
    for option in args.cmake_arg:
        if not re.fullmatch(r'-D(?:CMAKE_PREFIX_PATH|fastrtps_DIR|fastdds_DIR|fastcdr_DIR)(?::(?:PATH|STRING))?=.+', option):
            raise ValueError('only DDS dependency discovery paths may be overridden')
    manifest = args.client_manifest.resolve()
    client = verify_client(manifest)
    before = snapshot()
    out.mkdir(parents=True)
    env = os.environ.copy()
    runtime = [str(p.resolve()) for p in args.runtime_dir]
    env['PATH'] = os.pathsep.join(runtime) + os.pathsep + env.get('PATH', '')
    commands = []
    summary = dict(status='FAIL', fastdds=args.fastdds, source_snapshot=before,
                   client_manifest=str(manifest), client_manifest_sha256=sha(manifest))

    def execute(name, argv):
        argv = [str(p).replace('\\', '/') if name.endswith('configure') else str(p) for p in argv]
        try:
            process = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=1800)
            code, output = process.returncode, process.stdout
        except subprocess.TimeoutExpired as error:
            code, output = 'TIMEOUT', error.stdout or b''
        (out / (name + '.log')).write_bytes(output)
        commands.append(dict(name=name, command=argv, exit_code=code))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        if code != 0:
            raise RuntimeError(name + ' failed; see ' + str(out / (name + '.log')))

    try:
        dep = out / 'dependency'
        argv = [sys.executable, '-B', ROOT / 'scripts/standards/build_ua_gateway.py', '--fastdds', args.fastdds,
                '--out', dep, '--ua-archive', args.ua_archive, '--ua-source', args.ua_source, '--clang', args.clang]
        for path in runtime:
            argv += ['--runtime-dir', path]
        argv += ['--cmake-arg=' + option for option in args.cmake_arg]
        execute('dependency-build', argv)
        dependency = json.loads((dep / 'summary.json').read_text(encoding='utf-8'))
        if dependency['status'] != 'PASS' or len(dependency['commands']) != 10 or any(c['exit_code'] != 0 for c in dependency['commands']):
            raise RuntimeError('dependency build gate failed')
        execute('gateway-configure', ['cmake', '-S', ROOT / 'integrations/standards/dds_ua_server', '-B', out / 'build', *args.cmake_arg,
                '-DDDS_STANDARDS_FASTDDS=' + args.fastdds, '-DB3_GENERATED_DIR=' + str(dep / 'generated'),
                '-DB3_IDL=' + str(ROOT / 'integrations/standards/idl/BoundedRecord.idl'),
                '-Dopen62541_DIR=' + str(dep / 'ua-install/lib/cmake/open62541'), '-DD1_UA_PREFIX=' + str(dep / 'ua-install'),
                '-DPython3_EXECUTABLE=' + sys.executable])
        execute('gateway-build', ['cmake', '--build', out / 'build', '--config', 'Release', '--parallel', '4'])
        execute('ctest', ['ctest', '--test-dir', out / 'build', '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', out / 'ctest.xml'])
        artifacts = [out / 'build/Release' / (name + '.exe') for name in ('dds_ua_gateway', 'd2_peer', 'd2_contract')]
        resolved = json.loads((out / 'build/d1-ua-dependency.json').read_text(encoding='utf-8'))
        library = dep / 'ua-install/lib/open62541.lib'
        if Path(resolved['library']).resolve() != library.resolve() or resolved['sha256'] != sha(library):
            raise RuntimeError('resolved UA library provenance mismatch')
        if snapshot() != before or sha(manifest) != summary['client_manifest_sha256'] or verify_client(manifest) != client:
            raise RuntimeError('source/client changed during build')
        summary.update(status='PASS', source_unchanged=True, resolved_ua=resolved,
                       dependency_summary=str(dep / 'summary.json'), dependency_summary_sha256=sha(dep / 'summary.json'),
                       artifact_sha256={**dependency['artifact_sha256'], **{str(p): sha(p) for p in artifacts}},
                       bundle=dict(abi=args.fastdds, gateway=str(artifacts[0]), peer=str(artifacts[1]), runtime_dirs=runtime,
                                   client_python=client['python'], client_manifest=str(manifest), build_summary=str(out / 'summary.json')))
    finally:
        summary['commands'] = commands
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D2_BUILD PASS abi=' + args.fastdds, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fastdds', choices=['2.14.5', '3.6.2'], required=True)
    for name in ('out', 'ua-archive', 'ua-source', 'clang', 'client-manifest'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', action='append', type=Path, default=[])
    parser.add_argument('--cmake-arg', action='append', default=[])
    try:
        build(parser.parse_args())
    except Exception as error:
        parser.exit(1, str(error) + '\n')
