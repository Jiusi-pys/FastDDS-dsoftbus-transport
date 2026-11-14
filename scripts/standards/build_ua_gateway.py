"""Build D1 and its independent UA fixture from pinned open62541 source; Windows only."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
UA_COMMIT = 'b1e4008bc4611a422a86704a60c29d390fa486a5'
UA_ARCHIVE_SHA256 = '1a939f0c1e30eb8d99d918aabb2a34773bac6f21565d31bb3b959e8b367da2c1'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def snapshot():
    files = [p for area in ('opcua_gateway', 'c_backend', 'cpp_backend')
             for p in (ROOT / 'integrations/standards' / area).rglob('*') if p.is_file()]
    files += [ROOT / 'integrations/standards/idl/BoundedRecord.idl', Path(__file__),
              ROOT / 'scripts/standards/run_ua_acceptance.py', ROOT / 'scripts/standards/ua_fault_proxy.py']
    return {str(p.relative_to(ROOT)): sha(p) for p in files}


def build_ua_gateway(args):
    out = args.out.resolve()
    if os.name != 'nt' or out.exists() or not out.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('use a new Windows workspace build directory')
    for option in args.cmake_arg:
        if not re.fullmatch(r'-D(?:CMAKE_PREFIX_PATH|fastrtps_DIR|fastdds_DIR|fastcdr_DIR)(?::(?:PATH|STRING))?=.+', option):
            raise ValueError('unsupported --cmake-arg: only DDS dependency discovery paths may be set')
    archive, source = args.ua_archive.resolve(), args.ua_source.resolve()
    if sha(archive) != UA_ARCHIVE_SHA256:
        raise ValueError('pinned open62541 archive hash mismatch')
    upstream = {}
    with zipfile.ZipFile(archive) as zipped:
        for entry in zipped.infolist():
            if entry.is_dir():
                continue
            parts = Path(entry.filename).parts
            if parts[0] != 'open62541-' + UA_COMMIT or '..' in parts:
                raise ValueError('unexpected source archive path')
            file = source.joinpath(*parts[1:])
            digest = hashlib.sha256(zipped.read(entry)).hexdigest()
            if not file.is_file() or sha(file) != digest:
                raise ValueError('open62541 source differs from archive: ' + str(file))
            upstream[str(file)] = digest
    out.mkdir(parents=True)
    before = snapshot()
    summary = {'status': 'FAIL', 'fastdds': args.fastdds, 'source_snapshot': before,
               'open62541': {'version': '1.4.21', 'commit': UA_COMMIT,
                            'archive_sha256': UA_ARCHIVE_SHA256, 'source_sha256': upstream}}
    commands = []
    runtime = [p.resolve() for p in args.runtime_dir]
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(map(str, runtime)) + os.pathsep + env.get('PATH', '')
    env['IDL_CLANG'] = str(args.clang.resolve())

    def execute(name, argv):
        argv = [str(a).replace('\\', '/') if name.endswith('configure') else str(a) for a in argv]
        try:
            done = subprocess.run(argv, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=900)
            code, data = done.returncode, done.stdout
        except subprocess.TimeoutExpired as error:
            code, data = 'TIMEOUT', error.stdout or b''
        (out / (name + '.log')).write_bytes(data)
        commands.append({'name': name, 'command': argv, 'exit_code': code})
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        if code != 0:
            raise RuntimeError(name + ' failed: ' + str(out / (name + '.log')))

    try:
        execute('cmake-version', ['cmake', '--version'])
        execute('ua-configure', ['cmake', '-S', source, '-B', out / 'ua-build',
                                '-DCMAKE_INSTALL_PREFIX=' + str(out / 'ua-install'),
                                '-DUA_ENABLE_ENCRYPTION=OFF', '-DUA_BUILD_EXAMPLES=OFF',
                                '-DUA_BUILD_UNIT_TESTS=OFF', '-DUA_NAMESPACE_ZERO=REDUCED',
                                '-DBUILD_SHARED_LIBS=OFF'])
        execute('ua-build', ['cmake', '--build', out / 'ua-build', '--config', 'Release', '--parallel', '4'])
        execute('ua-install', ['cmake', '--install', out / 'ua-build', '--config', 'Release'])
        execute('generate', [sys.executable, '-B', ROOT / 'scripts/standards/generate_cpp.py',
                             ROOT / 'integrations/standards/idl/BoundedRecord.idl', '--clang',
                             args.clang, '--out', out / 'generated', '--fastdds', args.fastdds])
        execute('gateway-configure', ['cmake', '-S', ROOT / 'integrations/standards/opcua_gateway',
                                      '-B', out / 'build', *args.cmake_arg,
                                      '-DDDS_STANDARDS_FASTDDS=' + args.fastdds,
                                      '-DB3_GENERATED_DIR=' + str(out / 'generated'),
                                      '-DB3_IDL=' + str(ROOT / 'integrations/standards/idl/BoundedRecord.idl'),
                                      '-Dopen62541_DIR=' + str(out / 'ua-install/lib/cmake/open62541'),
                                      '-DD1_UA_PREFIX=' + str(out / 'ua-install'),
                                      '-DPython3_EXECUTABLE=' + sys.executable])
        execute('gateway-build', ['cmake', '--build', out / 'build', '--config', 'Release', '--parallel', '4'])
        execute('ctest', ['ctest', '--test-dir', out / 'build', '-C', 'Release', '--output-on-failure',
                          '--no-tests=error', '--output-junit', out / 'ctest.xml'])
        # A separate configure/build, without any DDS package or generated support.
        execute('server-configure', ['cmake', '-S', ROOT / 'integrations/standards/opcua_gateway/independent_server',
                                     '-B', out / 'server-build', '-Dopen62541_DIR=' + str(out / 'ua-install/lib/cmake/open62541'),
                                     '-DD1_UA_PREFIX=' + str(out / 'ua-install')])
        execute('server-build', ['cmake', '--build', out / 'server-build', '--config', 'Release', '--parallel', '4'])
        artifacts = [out / 'build/Release/ua_dds_gateway.exe', out / 'build/Release/d1_plain_probe.exe',
                     out / 'build/Release/d1_contract.exe', out / 'server-build/Release/d1_ua_server.exe',
                     out / 'ua-install/lib/open62541.lib', out / 'ua-install/include/open62541/config.h']
        if snapshot() != before or any(sha(p) != h for p, h in upstream.items()):
            raise RuntimeError('source changed during build')
        resolved = [json.loads((out / directory / 'd1-ua-dependency.json').read_text(encoding='utf-8'))
                    for directory in ('build', 'server-build')]
        for dependency in resolved:
            if Path(dependency['library']).resolve() != artifacts[4].resolve() or dependency['sha256'] != sha(artifacts[4]):
                raise RuntimeError('resolved UA library provenance mismatch')
        summary.update(status='PASS', source_unchanged=True,
                       resolved_ua=resolved,
                       artifact_sha256={str(p): sha(p) for p in artifacts},
                       bundle={'abi': args.fastdds, 'gateway': str(artifacts[0]), 'probe': str(artifacts[1]),
                               'server': str(artifacts[3]), 'runtime_dirs': list(map(str, runtime)),
                               'build_summary': str(out / 'summary.json')})
    finally:
        summary['commands'] = commands
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D1_BUILD PASS abi=' + args.fastdds, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fastdds', choices=['2.14.5', '3.6.2'], required=True)
    for name in ('out', 'ua-archive', 'ua-source', 'clang'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', action='append', type=Path, default=[])
    parser.add_argument('--cmake-arg', action='append', default=[], help='-D DDS dependency discovery paths only; pinned inputs cannot be overridden')
    try:
        build_ua_gateway(parser.parse_args())
    except Exception as error:
        parser.exit(1, str(error) + '\n')
