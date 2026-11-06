"""Build unchanged ROS C++ dependencies with declared target and Host-tool inputs.

No device driver, deployment, claim, source patch, or dependency download.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import xml.etree.ElementTree as ET


PURE_TOOLS = {'rosidl_adapter', 'rosidl_cmake', 'rosidl_parser',
              'rosidl_generator_c', 'rosidl_generator_cpp', 'fastrtps_cmake_module',
              'python_cmake_module', 'pybind11_vendor'}

def synchronize_vendor_libraries(name,folder,install):
    """Copy actual relinked outputs; CMake timestamp-only install can miss <1s edits."""
    patterns={'libyaml_vendor':'libyaml.so*','spdlog_vendor':'libspdlog.so*','console_bridge_vendor':'lib/libconsole_bridge.so*'}
    if name not in patterns:raise ValueError('Unsupported vendor install verification')
    folder=Path(folder).resolve();install=Path(install).resolve()
    child=folder/(name+'-prefix/src/'+name+'-build')
    outputs=sorted(child.glob(patterns[name]));rows=[]
    if not outputs:raise ValueError('Vendor build output missing')
    for source in outputs:
        if not source.resolve().is_relative_to(child.resolve()) or not source.read_bytes().startswith(b'\x7fELF'):
            raise ValueError('Vendor output is not the declared local ELF')
        digest=hashlib.sha256(source.read_bytes()).hexdigest()
        for destination,owner in [(folder/(name+'-prefix/install/lib')/source.name,folder),(install/'opt'/name/'lib'/source.name,install)]:
            if not destination.resolve().is_relative_to(owner):raise ValueError('Vendor install path escape')
            destination.parent.mkdir(parents=True,exist_ok=True)
            before=hashlib.sha256(destination.read_bytes()).hexdigest() if destination.is_file() else None
            if before!=digest:shutil.copyfile(source,destination)
            after=hashlib.sha256(destination.read_bytes()).hexdigest()
            if after!=digest:raise ValueError('Vendor copied bytes mismatch')
            rows.append(dict(source=str(source),destination=str(destination),source_sha256=digest,before_sha256=before,after_sha256=after,changed=before!=after))
    return rows


def package_dirs(prefix):
    result = {}
    for path in prefix.rglob('*.cmake'):
        name = path.name
        if name.endswith('Config.cmake'):
            result[name[:-12]] = path.parent
        elif name.endswith('-config.cmake'):
            result[name[:-13]] = path.parent
    return result


def sources(root):
    result = {}
    for path in root.rglob('package.xml'):
        if any(p in ('test', 'tests', 'testdata', 'test_cases', 'fixtures') for p in path.parts):
            continue
        document = ET.parse(path).getroot()
        name = document.findtext('name')
        if (path.parent / 'CMakeLists.txt').exists():
            if name in result and result[name] != path.parent:
                raise ValueError('Ambiguous source package: ' + name)
            result[name] = path.parent
    return result


def main():
    p = argparse.ArgumentParser()
    for name in ('source', 'output', 'target_prefix', 'host_tools', 'sdk', 'cmake', 'python'):
        p.add_argument('--' + name.replace('_', '-'), type=Path, required=True)
    p.add_argument('--dependency-prefix', type=Path, action='append', default=[])
    p.add_argument('--ninja', type=Path, help='Explicit installed Host generator; target compiler still belongs to SDK')
    p.add_argument('--package', action='append', required=True)
    p.add_argument('--rebuild-package', action='append', default=[], help='Reconfigure/rebuild this selected package after newly installed generators; no deletion')
    p.add_argument('--exe-linker-flag', action='append', default=[], help='Extra link flag for executables; never a source change')
    p.add_argument('--python-target', type=Path, help='python-target directory of pull_python_target.py: build Python bindings too')
    p.add_argument('--resume', action='store_true', help='Append to this explicit local build output, never overwrite receipts')
    args = p.parse_args()
    for name in ('source', 'output', 'target_prefix', 'host_tools', 'sdk', 'cmake', 'python'):
        setattr(args, name, getattr(args, name).resolve())
    args.dependency_prefix = [path.resolve() for path in args.dependency_prefix]
    ninja = args.ninja.resolve() if args.ninja else args.sdk / 'build-tools/cmake/bin/ninja.exe'
    if args.output.exists() and not args.resume:
        raise ValueError('Existing output requires explicit --resume')
    args.output.mkdir(parents=True, exist_ok=args.resume)
    install = args.output / 'install'
    install.mkdir(exist_ok=args.resume)
    repo = Path(__file__).resolve().parents[3]
    inputs = [install, args.target_prefix] + args.dependency_prefix
    available = sources(args.source)
    host = package_dirs(args.host_tools)
    environment = os.environ.copy()
    environment['PATH'] = os.pathsep.join([
        str(args.python.parent), str(args.python.parent / 'Scripts'),
        str(args.python.parent / 'Library/bin'), environment.get('PATH', '')])
    environment['PYTHONPATH'] = str(args.host_tools / 'Lib/site-packages')
    # Generator discovery sees target registrations, not Host runtime extensions.
    environment['AMENT_PREFIX_PATH'] = os.pathsep.join(str(x) for x in inputs)
    active = set()
    complete = set()
    sequence = max((int(x.name.split('-')[0]) for x in args.output.glob('[0-9][0-9][0-9]-*.json')), default=0)

    def dirs():
        result = {k: v for k, v in host.items() if k.startswith('ament_') or k in PURE_TOOLS}
        for prefix in reversed(inputs):
            result.update(package_dirs(prefix))
        return result

    def command(name, argv):
        nonlocal sequence
        sequence += 1
        stem = args.output / f'{sequence:03d}-{name}'
        with stem.with_suffix('.stdout').open('wb') as out, stem.with_suffix('.stderr').open('wb') as err:
            result = subprocess.run([str(x) for x in argv], cwd=repo, env=environment, stdout=out, stderr=err)
        record = {'argv': [str(x) for x in argv], 'cwd': str(repo), 'exit_code': result.returncode,
                  'purpose': 'TARGET_BUILD_NO_DEVICE', 'toolchain': str(args.sdk / 'build/cmake/ohos.toolchain.cmake')}
        stem.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
        print(name, result.returncode, flush=True)
        return result.returncode, stem.with_suffix('.stdout').read_text(errors='replace') + stem.with_suffix('.stderr').read_text(errors='replace')

    def build(name):
        existing = dirs().get(name)
        installed_target = existing is not None and not existing.is_relative_to(args.host_tools)
        if name in complete or (installed_target and name not in args.rebuild_package):
            return
        if name in active:
            raise ValueError('Dependency cycle: ' + name)
        if name not in available:
            raise ValueError('Missing local source package: ' + name)
        active.add(name)
        source = available[name]
        folder = args.output / ('build-' + name)
        for attempt in range(30):
            current = dirs()
            argv = [args.cmake, '-S', source, '-B', folder, '-G', 'Ninja',
                    '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(),
                    '-DCMAKE_TOOLCHAIN_FILE=' + str(args.sdk / 'build/cmake/ohos.toolchain.cmake'),
                    '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release',
                    '-DBUILD_SHARED_LIBS=ON', '-DBUILD_TESTING=OFF', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                    '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                    '-DCMAKE_INSTALL_PREFIX=' + str(install),
                    '-DCMAKE_PROJECT_INCLUDE=' + str(repo / 'integrations/ros2/target' / ('python/Ros2PythonTarget.cmake' if args.python_target else 'RosTargetBuildOptions.cmake')),
                    '-DPython3_EXECUTABLE=' + str(args.python),
                    '-DCMAKE_PREFIX_PATH=' + ';'.join(str(x) for x in inputs)]
            if args.python_target:
                # Upstream's own find_package(Python3) calls land on this module: see its header.
                argv.extend(['-DCMAKE_MODULE_PATH=' + (repo / 'integrations/ros2/target/python').as_posix(),
                             '-DOHOS_DEPENDENCY_ROOTS=' + ';'.join(p.as_posix() for p in args.dependency_prefix),
                             '-DOHOS_PYTHON_HOST=' + Path(args.python).as_posix(),
                             # pybind11 asks the host interpreter unless these are set, and it clears them on the
                             # first configure unless it believes this interpreter was already used.
                             '-DPYTHON_MODULE_EXTENSION=.cpython-312-aarch64-linux-ohos.so', '-DPYTHON_IS_DEBUG=0',
                             '-DPYBIND11_PYTHON_EXECUTABLE_LAST=' + Path(args.python).as_posix(),
                             '-DOHOS_PYTHON_INCLUDE=' + (args.python_target / 'usr/include/python3.12').as_posix(),
                             '-DOHOS_PYTHON_LIBRARY=' + (args.python_target / 'usr/lib/libpython3.12.so.1.0').as_posix(),
                             '-DOHOS_NUMPY_INCLUDE=' + (args.python_target / 'third_party/numpy/core/include').as_posix(),
                             # The package's own copy: the Library one shares an include directory with every Host library.
                             '-Dpybind11_DIR=' + (Path(args.python).parent / 'Lib/site-packages/pybind11/share/cmake/pybind11').as_posix()])
            argv.extend('-D' + key + '_DIR:PATH=' + value.as_posix() for key, value in sorted(current.items()))
            for prefix in args.dependency_prefix:
                if (prefix/'include/openssl/opensslv.h').is_file():
                    if not all((prefix/'lib'/name).is_file() for name in ('libcrypto.so','libssl.so')):
                        raise ValueError('Declared shared target OpenSSL closure missing')
                    argv.extend(['-DOPENSSL_ROOT_DIR:PATH='+prefix.as_posix(),
                        '-DOPENSSL_INCLUDE_DIR:PATH='+(prefix/'include').as_posix(),
                        '-DOPENSSL_CRYPTO_LIBRARY:FILEPATH='+(prefix/'lib/libcrypto.so').as_posix(),
                        '-DOPENSSL_SSL_LIBRARY:FILEPATH='+(prefix/'lib/libssl.so').as_posix()])
            # Legacy Fast DDS exports include a bare tinyxml2 dependency. Only
            # declared target-prefix library directories participate in its
            # linker lookup, never the Host ROS/Python library directories.
            library_dirs = [prefix / leaf for prefix in inputs for leaf in ('lib', 'Lib')
                            if (prefix / leaf).is_dir()]
            if args.python_target:
                # rclpy links -latomic on Clang. OHOS has no such library: the compiler runtime provides atomics, so an
                # empty archive answers the request and adds nothing to the module.
                stubs = args.output / 'link-stubs'
                stubs.mkdir(parents=True, exist_ok=True)
                (stubs / 'libatomic.a').write_bytes(b'!<arch>'+bytes([10]))
                library_dirs.append(stubs)
            search_flags = ' '.join('-L"' + path.as_posix() + '"' for path in library_dirs)
            argv.extend(['-DCMAKE_SHARED_LINKER_FLAGS=' + search_flags, '-DCMAKE_MODULE_LINKER_FLAGS=' + search_flags,
                         '-DCMAKE_EXE_LINKER_FLAGS=' + ' '.join([search_flags] + args.exe_linker_flag)])
            # The upstream legacy FindFastRTPS module uses find_path/library;
            # OHOS's root policy must not reinterpret declared target prefixes
            # as sysroot-relative paths. Bind existing target files explicitly.
            if 'fastrtps' in current:
                prefix = current['fastrtps'].parents[2]
                library = prefix / 'lib/libfastrtps.so'
                if not library.is_file():
                    raise ValueError('Declared target Fast DDS library missing')
                argv.extend(['-DFastRTPS_INCLUDE_DIR:PATH=' + (prefix / 'include').as_posix(),
                             '-DFastRTPS_LIBRARY_RELEASE:FILEPATH=' + library.as_posix()])
            if 'fastcdr' in current:
                prefix = current['fastcdr'].parents[2]
                library = prefix / 'lib/libfastcdr.so'
                if not library.is_file():
                    raise ValueError('Declared target Fast CDR library missing')
                argv.append('-DFastCDR_LIBRARY_RELEASE:FILEPATH=' + library.as_posix())
            if name == 'tracetools':
                # C++ functional slice does not include LTTng/performance tracing.
                # Use the upstream declared option; transport/exit/guard checks
                # remain independent and are never disabled by this setting.
                argv.extend(['-DTRACETOOLS_DISABLED=ON', '-DTRACETOOLS_STATUS_CHECKING_TOOL=OFF'])
            rc, text = command(name + '-configure-' + str(attempt), argv)
            if rc == 0:
                break
            missing = re.findall(r'configuration file provided\s+by\s+"([A-Za-z0-9_]+)"', text)
            if not missing:
                raise RuntimeError(name + ': actual configure failure, inspect saved raw outputs')
            dependency = missing[0]
            if dependency in current:
                raise RuntimeError(name + ': configured dependency rejected: ' + dependency)
            build(dependency)
        else:
            raise RuntimeError(name + ': dependency preparation bound exceeded')
        cache = (folder / 'CMakeCache.txt').read_text(errors='replace')
        for line in cache.splitlines():
            match = re.match(r'([^:#]+)_DIR:[^=]+=(.*)', line)
            if match and str(args.host_tools).replace('\\', '/').lower() in match[2].replace('\\', '/').lower():
                key = match[1]
                if not (key.startswith('ament_') or key in PURE_TOOLS):
                    raise RuntimeError('Host runtime package import rejected: ' + key)
        rc, _ = command(name + '-build', [args.cmake, '--build', folder, '--parallel', '4', '--verbose'])
        if rc:
            raise RuntimeError(name + ': actual build failure, not a device result')
        if name in ('libyaml_vendor', 'spdlog_vendor', 'console_bridge_vendor'):
            # Upstream ExternalProject does not inherit the parent's project
            # include. Apply the same ELF configuration to its actual existing
            # target cache; do not patch vendor or ROS source implementations.
            child = folder / (name + '-prefix/src/' + name + '-build')
            child_cache = (child / 'CMakeCache.txt').read_text()
            homes = re.findall(r'^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$', child_cache, re.M)
            toolchains = re.findall(r'^CMAKE_TOOLCHAIN_FILE:[^=]+=(.+)$', child_cache, re.M)
            if (len(homes) != 1 or len(toolchains) != 1 or
                    not Path(homes[0]).resolve().is_relative_to(folder.resolve()) or
                    Path(toolchains[0]).resolve() != args.sdk / 'build/cmake/ohos.toolchain.cmake'):
                raise RuntimeError('Unbound target vendor cache: ' + name)
            commands = [
                [args.cmake, '-S', homes[0], '-B', child,
                 '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(),
                 '-DCMAKE_PROJECT_INCLUDE=' + str(repo / 'integrations/ros2/target/RosTargetBuildOptions.cmake')],
                [args.cmake, '--build', child, '--parallel', '4', '--verbose'],
                [args.cmake, '--install', child]]
            for phase, argv in zip(('configure', 'build', 'install'), commands):
                rc, _ = command(name + '-elf-' + phase, argv)
                if rc:
                    raise RuntimeError(name + ': target vendor ELF ' + phase + ' failed')
        rc, _ = command(name + '-install', [args.cmake, '--install', folder])
        if rc:
            raise RuntimeError(name + ': install failed')
        if name in ('libyaml_vendor','spdlog_vendor','console_bridge_vendor'):
            rows=synchronize_vendor_libraries(name,folder,install)
            count=len(list(args.output.glob(name+'-install-sync-*.json')))+1
            (args.output/f'{name}-install-sync-{count}.json').write_text(json.dumps(rows,indent=2)+'\n')
        (args.output / (name + '-source.json')).write_text(json.dumps({
            'source': str(source), 'package_xml_sha256': hashlib.sha256((source / 'package.xml').read_bytes()).hexdigest(),
            'version': ET.parse(source / 'package.xml').getroot().findtext('version'),
            'build': str(folder), 'install': str(install), 'builder_applies_source_patch': False}, indent=2) + '\n')
        complete.add(name)
        active.remove(name)

    for name in args.package:
        build(name)
    print('TARGET_ROS_SELECTED_PACKAGES_BUILT', sorted(complete))


if __name__ == '__main__':
    main()
