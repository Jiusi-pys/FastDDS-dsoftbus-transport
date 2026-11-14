"""Fresh, pinned Windows builds for the bounded D4 service/history profile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile
import psutil
import build_ua_gateway as d1
import build_dds_ua_gateway as d2
import build_request_reply as d3

ROOT = Path(__file__).resolve().parents[2]
sha = d3.sha


def snapshot():
    files = [p for area in ('opcua_complete', 'cpp_backend') for p in (ROOT / 'integrations/standards' / area).rglob('*') if p.is_file()]
    files += [ROOT / 'integrations/standards/dds_ua_server/ua_type.hpp', ROOT / 'integrations/standards/opcua_gateway/verify_ua.cmake', ROOT / 'integrations/standards/idl/BoundedRecord.idl']
    files += [ROOT / 'scripts/standards' / name for name in ('build_ua_gateway.py', 'build_dds_ua_gateway.py', 'build_ua_complete.py', 'd4_ua_fixture.py', 'd4_ua_client.py', 'run_ua_complete_acceptance.py')]
    return {**d3.snapshot(), **{str(p.relative_to(ROOT)): sha(p) for p in sorted(set(files))}}


def verify_upstream(archive, source):
    if sha(archive) != d1.UA_ARCHIVE_SHA256:
        raise ValueError('pinned open62541 archive mismatch')
    result = {}
    with zipfile.ZipFile(archive) as zipped:
        for item in zipped.infolist():
            if item.is_dir():
                continue
            parts = Path(item.filename).parts
            if parts[0] != 'open62541-' + d1.UA_COMMIT or '..' in parts:
                raise ValueError('upstream archive path')
            file = source.joinpath(*parts[1:])
            digest = hashlib.sha256(zipped.read(item)).hexdigest()
            if sha(file) != digest:
                raise ValueError('upstream source differs: ' + str(file))
            result[str(file)] = digest
    return result


def build(args):
    out = args.out.resolve()
    if os.name != 'nt' or out.exists() or not out.is_relative_to((ROOT / 'build').resolve()):
        raise ValueError('new Windows workspace output required')
    for option in args.cmake_arg:
        if not re.fullmatch(r'-D(?:CMAKE_PREFIX_PATH|fastrtps_DIR|fastdds_DIR|fastcdr_DIR)(?::(?:PATH|STRING))?=.+', option):
            raise ValueError('only DDS discovery paths may be overridden')
    client = d2.verify_client(args.client_manifest)
    upstream = verify_upstream(args.ua_archive.resolve(), args.ua_source.resolve())
    before = snapshot()
    clang = args.clang.resolve()
    out.mkdir(parents=True)
    runtime = [str(p.resolve()) for p in args.runtime_dir]
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join(runtime) + os.pathsep + env.get('PATH', '')
    summary = dict(status='FAIL', fastdds=args.fastdds, source_snapshot=before, upstream_sha256=upstream,
                   archive=str(args.ua_archive.resolve()), archive_sha256=sha(args.ua_archive),
                   clang=str(clang), clang_sha256=sha(clang), client_manifest=str(args.client_manifest.resolve()),
                   client_manifest_sha256=sha(args.client_manifest))
    commands = []

    def execute(name, argv):
        argv = [str(a).replace('\\', '/') if name.endswith('configure') else str(a) for a in argv]
        with (out / (name + '.log')).open('wb') as log:
            process = subprocess.Popen(argv, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=900)
            except subprocess.TimeoutExpired:
                for child in psutil.Process(process.pid).children(recursive=True):
                    try:
                        child.kill()
                    except psutil.NoSuchProcess:
                        pass
                process.kill()
                process.wait(timeout=10)
                code = 'TIMEOUT'
        commands.append(dict(name=name, argv=argv, exit_code=code))
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        if code != 0:
            raise RuntimeError(name + ' failed: ' + str(out / (name + '.log')))

    try:
        d3argv = [sys.executable, '-B', ROOT / 'scripts/standards/build_request_reply.py', '--fastdds', args.fastdds, '--out', out / 'd3', '--clang', clang]
        for path in runtime:
            d3argv += ['--runtime-dir', path]
        d3argv += ['--cmake-arg=' + option for option in args.cmake_arg]
        execute('d3-build', d3argv)
        execute('ua-configure', ['cmake', '-S', args.ua_source, '-B', out / 'ua-build', '-DCMAKE_INSTALL_PREFIX=' + str(out / 'ua-install'),
                                '-DUA_ENABLE_ENCRYPTION=OFF', '-DUA_ENABLE_QUERY=ON', '-DUA_ENABLE_HISTORIZING=ON', '-DUA_BUILD_EXAMPLES=OFF',
                                '-DUA_BUILD_UNIT_TESTS=OFF', '-DUA_NAMESPACE_ZERO=REDUCED', '-DBUILD_SHARED_LIBS=OFF', '-DUA_MSVC_FORCE_STATIC_CRT=OFF'])
        execute('ua-build', ['cmake', '--build', out / 'ua-build', '--config', 'Release', '--parallel', '4'])
        execute('ua-install', ['cmake', '--install', out / 'ua-build', '--config', 'Release'])
        execute('record-generate', [sys.executable, '-B', ROOT / 'scripts/standards/generate_cpp.py', ROOT / 'integrations/standards/idl/BoundedRecord.idl', '--clang', clang, '--out', out / 'records', '--fastdds', args.fastdds])
        execute('d4-configure', ['cmake', '-S', ROOT / 'integrations/standards/opcua_complete', '-B', out / 'build', *args.cmake_arg,
                               '-DDDS_STANDARDS_FASTDDS=' + args.fastdds, '-DD3_GENERATED_DIR=' + str(out / 'd3/generated'), '-DD3_IDL=' + str(ROOT / 'integrations/standards/idl/RpcMessage.idl'),
                               '-DB3_GENERATED_DIR=' + str(out / 'records'), '-DB3_IDL=' + str(ROOT / 'integrations/standards/idl/BoundedRecord.idl'),
                               '-Dopen62541_DIR=' + str(out / 'ua-install/lib/cmake/open62541'), '-DD1_UA_PREFIX=' + str(out / 'ua-install')])
        execute('d4-build', ['cmake', '--build', out / 'build', '--config', 'Release', '--parallel', '4'])
        execute('ctest', ['ctest', '--test-dir', out / 'build', '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', out / 'ctest.xml'])
        if snapshot() != before or any(sha(p) != digest for p, digest in upstream.items()) or sha(clang) != summary['clang_sha256']:
            raise RuntimeError('source/toolchain changed during build')
        generated = {}
        for directory in ('records', 'd3/generated'):
            manifest = json.loads((out / directory / 'generation.json').read_text(encoding='utf-8'))
            for name, digest in manifest['generated'].items():
                file = out / directory / name
                if sha(file) != digest:
                    raise RuntimeError('generated file changed')
                generated[str(file)] = digest
        ua = json.loads((out / 'build/d1-ua-dependency.json').read_text(encoding='utf-8'))
        dds = json.loads((out / 'build/d3-dependency.json').read_text(encoding='utf-8'))
        d3_record = json.loads((out / 'd3/summary.json').read_text(encoding='utf-8'))
        if dds != d3_record['dependency'] or Path(dds['runtime']).resolve().parent not in {Path(p).resolve() for p in runtime}:
            raise RuntimeError('D4 resolved a different DDS dependency')
        artifacts = [out / 'build/Release' / (name + '.exe') for name in ('d4_service_gateway', 'd4_history_gateway', 'd4_native_peer', 'd4_contract')]
        artifacts += [Path(ua['library']), Path(dds['library']), Path(dds['runtime'])]
        headers = {str(p): sha(p) for p in (out / 'ua-install/include').rglob('*') if p.is_file()}
        summary.update(status='PASS', source_unchanged=True, generated_sha256=generated, ua_header_sha256=headers, artifact_sha256={str(p): sha(p) for p in artifacts},
                       bundle=dict(abi=args.fastdds, service=str(artifacts[0]), history=str(artifacts[1]), peer=str(artifacts[2]), runtime_dirs=runtime,
                                   client_manifest=str(args.client_manifest.resolve()), client_python=client['python'], build_summary=str(out / 'summary.json')))
    finally:
        summary['commands'] = commands
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('D4_BUILD PASS abi=' + args.fastdds, flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fastdds', choices=['3.6.2', '2.14.5'], required=True)
    for name in ('out', 'ua-source', 'ua-archive', 'clang', 'client-manifest'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--cmake-arg', action='append', default=[])
    try:
        build(parser.parse_args())
    except Exception as error:
        parser.exit(1, str(error) + '\n')
