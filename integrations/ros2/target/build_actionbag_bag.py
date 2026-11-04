"""Offline OHOS rosbag2 transport/Python build; emit a separate SQLite overlay. No devices."""
import argparse
import ast
import base64
import csv
import hashlib
import importlib.util
import importlib.metadata
import io
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shlex
import shutil
import sys
import zipfile

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]


def actionbag_bag_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build' or output.exists():
        raise ValueError('Require a fresh contained W3 build directory')
    here = Path(__file__).parent
    spec = importlib.util.spec_from_file_location('actionbag_bag_recorder', here / 'project_python.py')
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    digest, record = recorder.project_python_digest, recorder.project_python_record
    spec = importlib.util.spec_from_file_location('actionbag_bag_packages', here / 'build_ros_slice.py')
    packages = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packages)
    selection_file = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_file.read_text())
    sdk, cmake, python, ninja, host = [Path(selection[k]) for k in ('sdk', 'cmake', 'python', 'ninja', 'host_tools')]
    if Path(sys.executable).resolve() != python.resolve():
        raise ValueError('Invoke the bound host Python so metadata tools match build commands')
    readiness_file = root / 'build/r25-native-actionbag/cli-bag-readiness/INVENTORY.json'
    readiness = json.loads(readiness_file.read_text())
    canonical = REPO / 'build/r24-project-python/final-runtime'
    dev = root / 'build/r24-project-python/python-dev'
    old_bundle = root / 'build/r19baglocal5'
    fast = root / 'build/g2a1/target214-install'
    names = ['composition_interfaces', 'keyboard_handler', 'rosbag2_interfaces', 'rclcpp_components',
             'rosbag2_compression', 'pybind11_vendor', 'rosbag2_transport', 'rosbag2_py', 'ros2bag']
    versions = ['2.0.4', '0.3.2', '0.26.11', '28.1.22', '0.26.11', '3.1.3', '0.26.11', '0.26.11', '0.26.11']
    sources = {name: Path(readiness['sources'][name]['path']) for name in names}
    sqlite_source = root / 'ros2/src/ros2/rosbag2/rosbag2_storage_sqlite3'
    sqlite_setup = root / 'build/r19sqlite2/build/rosbag2_storage_sqlite3/ament_cmake_python/ros2bag_sqlite3_cli/setup.py'
    # Reuse the authentic ament-generated setup with the same upstream version,
    # setup.cfg and module. Build a new pure wheel in our owned tree; never edit
    # the native SQLite prefix or patch ros2bag's missing-extension fallback.
    sources['rosbag2_storage_sqlite3'] = sqlite_source
    pins = {
        canonical / 'BUILD.json': '04ff19bf38a052d13004cd80e7c2a32169c7ac8011d4d5cecc9119d21afd2e7e',
        canonical / 'package/PACKAGE.json': 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f',
        here / 'project_python.py': 'f4c0891a2ec77e05ae71e170c4ec0e0e5f29c3c8e272025d1a8006dc1b30cbd8',
        here / 'ProjectPython.cmake': '8ab09a4229974dadde3d4eedd334b297cf771c5294e1520c388c25e800f91311',
        dev / 'usr/lib/libpython3.12.so.1.0': '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a',
        fast / 'lib/libfastrtps.so.2.14': '40261a300a35f1974381faeacf56988f3131540b18e06522e602dd5a610975fd',
        old_bundle / 'BUNDLE_MANIFEST.json': 'ec4847feb168412e796127f4ac5a294bb2ce921240213213f4799e9556c2668c',
        root / 'build/r24-project-python/platform-inputs/ld-musl-aarch64.so.1': 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86',
    }
    pins.update({Path(p): sha for p, sha in selection['tool_sha256'].items()})
    pins.update({sqlite_source / name: sha for name, sha in {
        'CMakeLists.txt': 'd2daa45c2565e2558a08534bd262ee75b79ad8ca2f037b3980acc1c8bf7b5f4e',
        'setup.cfg': '0e955cb020efc9eb76bf44ceb9408f1f8e1109559deddb7e53f4d903c252c95f',
        'package.xml': '472025fede55dee595b0e2f0ac745b6641dfedde244e69cf3cf0f52d06058401',
        'ros2bag_sqlite3_cli/__init__.py': 'f30096231088d7f48d2475201e868d3aa7d118dcf77d009fe2a6addc95f86246',
    }.items()})
    pins[sqlite_setup] = '4a025b95bc5f9bb1f86b04d9a70e3f058bc178e57f01f6ccd1eafabffea6291e'
    sqlite_ament_macro = host / 'share/ament_cmake_python/cmake/ament_python_install_package.cmake'
    pins[sqlite_ament_macro] = '3ad8bbba249578010284e9c0899e036a2575920ff4f06cb15f0d1405f075e38c'
    pins.update({Path(p): sha for p, sha in readiness['pybind11_header_cmake_inputs'].items()})
    for name, version in zip(names, versions):
        source = sources[name]
        if readiness['sources'][name]['version'] != version:
            raise ValueError('Unexpected source version: ' + name)
        for rel, sha in readiness['sources'][name]['files'].items():
            pins[source / rel] = sha
    development = json.loads((dev / 'MANIFEST.json').read_text())
    headers = {dev / n: row['sha256'] for n, row in development.items()
               if n.startswith(('usr/include/python3.12/', 'numpy-include/'))}
    if len(headers) != 245 or sum('usr/include/python3.12/' in p.as_posix() for p in headers) != 214:
        raise ValueError('Require the bound 214 CPython and31 NumPy target headers')
    pins.update(headers)
    base = json.loads((canonical / 'package/PACKAGE.json').read_text())
    for name, row in base['files'].items():
        pins[Path(row['source']) if 'source' in row else canonical / 'package' / name] = row['sha256']
    for row in json.loads((old_bundle / 'BUNDLE_MANIFEST.json').read_text()):
        pins[Path(row['path'])] = row['sha256']
    for name, sha in json.loads((canonical / 'BUILD.json').read_text())['artifacts'].items():
        pins[canonical / 'install' / name] = sha
    for p, expected in pins.items():
        if not p.is_file() or digest(p) != expected:
            raise ValueError('Bound input changed: ' + str(p))
    # Target package exports precede host-only CMake/generator tools. Every binary
    # package gets an explicit target *_DIR; no host ROS binary prefix is searched.
    install = output / 'install'
    deps = [Path(r['path']) for r in selection['dependency_prefixes']]
    prefixes = [install, canonical / 'install', fast, root / 'build/r18-action-native/dependency-build/install',
        root / 'build/ros2-target/r2/ros/install', root / 'build/r19cpp1/install/rosbag2_cpp',
        root / 'build/r19storage3/install/rosbag2_storage', root / 'build/r19sqlite2/install/rosbag2_storage_sqlite3',
        root / 'build/r19next1/install', root / 'build/r19-rosbag-preparation/vendor-wrapper-execution/install'] + deps
    current = {k: v for k, v in packages.package_dirs(host).items()
               if k.startswith('ament_') or k in packages.PURE_TOOLS or k == 'python_cmake_module'}
    for prefix in reversed(prefixes[1:]):
        current.update(packages.package_dirs(prefix))
    pybind = python.parent / 'Lib/site-packages/pybind11'
    current['pybind11'] = pybind / 'share/cmake/pybind11'
    current['ament_cmake_vendor_package'] = host / 'share/ament_cmake_vendor_package/cmake'
    current['TinyXML2'] = root / 'build/r19-rosbag-preparation/next-dependency-implementation/adapter'
    for name in names:
        current.pop(name, None)
    if current['rosidl_generator_py'] != canonical / 'install/share/rosidl_generator_py/cmake':
        raise ValueError('Project target Python generator must be selected')
    inputs = dict(pins)
    for p in [Path(__file__), selection_file, readiness_file, dev / 'MANIFEST.json', here / 'build_ros_slice.py',
              here / 'RosTargetBuildOptions.cmake', old_bundle / 'RESOURCES.json']:
        inputs[p] = digest(p)
    for tool in ('clang.exe', 'clang++.exe', 'ld.lld.exe', 'llvm-ar.exe', 'llvm-ranlib.exe', 'llvm-readelf.exe', 'llvm-strip.exe'):
        p = sdk / 'llvm/bin' / tool
        inputs[p] = digest(p)
    shim = ninja.with_suffix('.shim')
    if shim.is_file():
        inputs[shim] = digest(shim)
        match = re.fullmatch(r'path = "([^\"]+)"\s*', shim.read_text())
        if not match: raise ValueError('Unknown Ninja shim')
        inputs[Path(match[1])] = digest(Path(match[1]))
    for prefix in prefixes[1:] + [pybind]:
        for p in prefix.rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc':
                if p.is_symlink(): raise ValueError('Unreviewed input symlink: ' + str(p))
                inputs.setdefault(p, digest(p))
    for folder in {v.parent for v in current.values() if v.is_relative_to(host)}:
        for p in folder.rglob('*'):
            if p.is_file(): inputs[p] = digest(p)
    for p in current['TinyXML2'].glob('*.cmake'):
        inputs[p] = digest(p)
    host_distributions = {}
    for name in ('setuptools', 'wheel', 'packaging'):
        distribution = importlib.metadata.distribution(name)
        host_distributions[name] = {'version': distribution.version, 'metadata': str(distribution._path)}
        for item in distribution.files:
            p = Path(distribution.locate_file(item)).resolve()
            if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc': inputs[p] = digest(p)
    for name in ('ament_package', 'rosidl_adapter', 'rosidl_parser', 'rosidl_cmake', 'rosidl_generator_c',
                 'rosidl_generator_cpp', 'rosidl_pycommon', 'rosidl_cli', 'em.py', 'lark', 'yaml'):
        folder = host / 'Lib/site-packages' / name
        for p in ([folder] if folder.is_file() else folder.rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc': inputs[p] = digest(p)
    source_files = {}
    for name, source in sources.items():
        source_files[name] = {}
        for p in source.rglob('*'):
            if p.is_file() and '__pycache__' not in p.parts and p.suffix != '.pyc':
                if p.is_symlink(): raise ValueError('Source symlink: ' + str(p))
                source_files[name][p.relative_to(source).as_posix()] = digest(p)
                inputs[p] = digest(p)
    output.mkdir(parents=True)
    for leaf in ('commands', 'tmp', 'install', 'src'):
        (output / leaf).mkdir()
    for name, source in sources.items():
        snapshot = output / 'src' / name
        for rel, sha in source_files[name].items():
            p = snapshot / rel
            p.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / rel, p)
            if digest(p) != sha: raise ValueError('Source snapshot mismatch')
    environment = os.environ.copy()
    environment.update(TEMP=str(output / 'tmp'), TMP=str(output / 'tmp'), TMPDIR=str(output / 'tmp'),
        PYTHONDONTWRITEBYTECODE='1', PIP_NO_INDEX='1', AMENT_PREFIX_PATH=os.pathsep.join(map(str, prefixes)),
        PYTHONPATH=os.pathsep.join([str(canonical / 'install/lib/python3.12/site-packages'), str(host / 'Lib/site-packages')]),
        PATH=os.pathsep.join([str(python.parent), str(python.parent / 'Library/bin'), environment.get('PATH', '')]))
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'OFFLINE_TARGET_BAG_BUILD_NO_DEVICE',
        'sha256': {str(p): sha for p, sha in inputs.items()}, 'source_files': source_files,
        'versions': dict(zip(names, versions)), 'host_distributions': host_distributions,
        'package_dirs': {k: str(v) for k, v in current.items()}}, indent=2) + '\n')
    (output / 'ENVIRONMENT.json').write_text(json.dumps({k: environment[k] for k in
        ('TEMP', 'TMP', 'TMPDIR', 'PYTHONDONTWRITEBYTECODE', 'PIP_NO_INDEX', 'AMENT_PREFIX_PATH', 'PYTHONPATH', 'PATH')}, indent=2) + '\n')
    # The upstream pybind NewTools clears a supplied extension suffix when its
    # interpreter-cache identity changes. Bind that identity to the host codegen
    # interpreter while binding headers/library/suffix to the measured target.
    hook = output / 'TargetPython.cmake'
    hook.write_text('include("' + (here / 'ProjectPython.cmake').as_posix() + '")\n' +
        'set(PYBIND11_PYTHON_EXECUTABLE_LAST "' + python.as_posix() + '" CACHE INTERNAL "" FORCE)\n' +
        'set(PYTHON_MODULE_EXTENSION ".cpython-312-aarch64-linux-ohos.so" CACHE INTERNAL "" FORCE)\n' +
        'if(PROJECT_NAME STREQUAL "rosbag2_py")\n'
        '  if(NOT TARGET Python3::Module)\n'
        '    add_library(Python3::Module INTERFACE IMPORTED GLOBAL)\n'
        '  endif()\n'
        '  set_property(TARGET Python3::Module PROPERTY INTERFACE_LINK_LIBRARIES Python3::Python)\n'
        'endif()\n')
    libraries = [p / leaf for p in prefixes for leaf in ('lib', 'Lib') if (p / leaf).is_dir()]
    linked_files = {}
    for name in names[:-1]:
        current.update(packages.package_dirs(install))
        folder = output / ('build-' + name)
        argv = [cmake, '-S', output / 'src' / name, '-B', folder, '-G', 'Ninja',
            '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(), '-DCMAKE_TOOLCHAIN_FILE=' + (sdk / 'build/cmake/ohos.toolchain.cmake').as_posix(),
            '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release',
            '-DBUILD_TESTING=OFF', '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
            '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
            '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH', '-DCMAKE_INSTALL_PREFIX=' + install.as_posix(),
            '-DCMAKE_PROJECT_INCLUDE=' + hook.as_posix(), '-DPython3_EXECUTABLE=' + python.as_posix(),
            '-DPython3_INCLUDE_DIR=' + (dev / 'usr/include/python3.12').as_posix(),
            '-DPython3_LIBRARY=' + (dev / 'usr/lib/libpython3.12.so.1.0').as_posix(),
            '-DPROJECT_PYTHON_INCLUDE=' + (dev / 'usr/include/python3.12').as_posix(),
            '-DPROJECT_PYTHON_LIBRARY=' + (dev / 'usr/lib/libpython3.12.so.1.0').as_posix(),
            '-DPROJECT_NUMPY_INCLUDE=' + (dev / 'numpy-include').as_posix(),
            '-DPYBIND11_FINDPYTHON=ON', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
            '-DCMAKE_PREFIX_PATH=' + ';'.join(p.as_posix() for p in prefixes),
            '-DCMAKE_SHARED_LINKER_FLAGS=' + ' '.join('-L"' + p.as_posix() + '"' for p in libraries),
            '-DFastRTPS_INCLUDE_DIR:PATH=' + (fast / 'include').as_posix(),
            '-DFastRTPS_LIBRARY_RELEASE:FILEPATH=' + (fast / 'lib/libfastrtps.so.2.14').as_posix(),
            '-DFastCDR_LIBRARY_RELEASE:FILEPATH=' + (deps[0] / 'lib/libfastcdr.so').as_posix()]
        argv += ['-D' + k + '_DIR:PATH=' + v.as_posix() for k, v in sorted(current.items())]
        record(output, name + '-configure', argv, environment)
        record(output, name + '-build', [cmake, '--build', folder, '--parallel', '4', '--verbose', '--', '-d', 'keeprsp'], environment)
        record(output, name + '-install', [cmake, '--install', folder], environment)
        record(output, name + '-commands', [ninja, '-C', folder, '-t', 'commands'], environment)
        record(output, name + '-deps', [ninja, '-C', folder, '-t', 'deps'], environment)
        used = {}
        log = next((output / 'commands').glob('*-' + name + '-deps.stdout')).read_text()
        for path in sorted({line.strip() for line in log.splitlines() if line.startswith('    ')}):
            p = Path(path)
            if not p.is_absolute(): p = folder / p
            if not p.is_file(): raise ValueError('Actual compiler input missing: ' + str(p))
            used[str(p.resolve())] = digest(p)
        (output / (name + '-COMPILER_INPUTS.json')).write_text(json.dumps(used, indent=2) + '\n')
        commands = {p.relative_to(folder).as_posix(): digest(p) for p in folder.rglob('*.rsp')}
        for response in folder.rglob('*.rsp'):
            for token in shlex.split(response.read_text(), posix=False):
                p = Path(token.strip('"'))
                if p.is_absolute() and p.is_file() and p.read_bytes()[:4] == b'\x7fELF':
                    linked_files[p] = digest(p)
        for rel in ('compile_commands.json', 'build.ninja', 'CMakeFiles/rules.ninja'):
            if (folder / rel).is_file(): commands[rel] = digest(folder / rel)
        (output / (name + '-BUILD_INPUTS.json')).write_text(json.dumps(commands, indent=2) + '\n')
    # Wheel build is offline and dependency-free. Stock setup metadata/resources
    # are retained; no setup.py install, network resolver or host native wheel.
    sqlite_snapshot = output / 'src/rosbag2_storage_sqlite3'
    sqlite_wheel_source = output / 'sqlite-cli-wheel-source'
    (sqlite_wheel_source / 'ros2bag_sqlite3_cli').mkdir(parents=True)
    shutil.copyfile(sqlite_setup, sqlite_wheel_source / 'setup.py')
    shutil.copyfile(sqlite_snapshot / 'setup.cfg', sqlite_wheel_source / 'setup.cfg')
    shutil.copyfile(sqlite_snapshot / 'ros2bag_sqlite3_cli/__init__.py',
                    sqlite_wheel_source / 'ros2bag_sqlite3_cli/__init__.py')
    wheel_reports = {}
    for distribution_name, snapshot in [('ros2bag', output / 'src/ros2bag'),
                                         ('ros2bag_sqlite3_cli', sqlite_wheel_source)]:
        record(output, distribution_name + '-wheel', [python, '-X', 'utf8', '-B', '-c',
            'import os,runpy,sys;os.chdir(sys.argv.pop(1));sys.argv=["setup.py","bdist_wheel","--dist-dir",sys.argv[1]];runpy.run_path("setup.py",run_name="__main__")',
            snapshot, output / 'wheels' / distribution_name], environment, timeout_seconds=120)
        wheels = list((output / 'wheels' / distribution_name).glob('*.whl'))
        if len(wheels) != 1: raise ValueError('Expected exactly one pure wheel: ' + distribution_name)
        wheel_files = {}
        with zipfile.ZipFile(wheels[0]) as archive:
            record_members = [n for n in archive.namelist() if n.endswith('.dist-info/RECORD')]
            if len(record_members) != 1:
                raise ValueError('Require exactly one wheel RECORD')
            rows = list(csv.reader(io.StringIO(archive.read(record_members[0]).decode('utf-8'))))
            members = {m.filename for m in archive.infolist() if not m.is_dir()}
            if any(len(row) != 3 for row in rows) or len(rows) != len({row[0] for row in rows}):
                raise ValueError('Ambiguous wheel RECORD rows')
            if {row[0] for row in rows} != members:
                raise ValueError('Wheel RECORD membership differs from output')
            for member_name, checksum, size in rows:
                if member_name == record_members[0]:
                    if checksum or size: raise ValueError('Wheel RECORD self entry')
                    continue
                data = archive.read(member_name)
                expected = 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode()
                if checksum != expected or size != str(len(data)):
                    raise ValueError('Wheel RECORD digest mismatch: ' + member_name)
            metadata = [n for n in archive.namelist() if n.endswith('.dist-info/WHEEL')]
            if len(metadata) != 1 or len(archive.namelist()) != len(set(archive.namelist())):
                raise ValueError('Ambiguous wheel metadata/members')
            text = archive.read(metadata[0]).decode()
            if 'Root-Is-Purelib: true\n' not in text or 'Tag: py3-none-any\n' not in text:
                raise ValueError('Unexpected pure wheel tag: ' + distribution_name)
            for member in archive.infolist():
                if member.is_dir(): continue
                name = member.filename
                relative = PurePosixPath(name)
                if (relative.is_absolute() or relative.as_posix() != name or '\\' in name or ':' in name or
                    any(ord(c) < 32 or ord(c) == 127 for c in name) or '..' in relative.parts or
                    any(p.endswith((' ', '.')) or PureWindowsPath(p).is_reserved() for p in relative.parts) or
                    (member.external_attr >> 16) & 0o170000 == 0o120000):
                    raise ValueError('Noncanonical wheel member: ' + name)
                data = archive.read(member)
                if data[:4] == b'\x7fELF' or data[:2] == b'MZ' or name.endswith(('.pyd', '.so', '.dll')):
                    raise ValueError('Native content in pure wheel: ' + distribution_name)
                if '.data/data/' in name:
                    rel = name.split('.data/data/', 1)[1]
                    if not rel.startswith('share/'): raise ValueError('Unexpected wheel data destination')
                elif '.data/' in name:
                    raise ValueError('Unexpected wheel relocation')
                else: rel = 'lib/python3.12/site-packages/' + name
                target = install / rel
                if not target.resolve().is_relative_to(install.resolve()) or target.exists():
                    raise ValueError('Wheel installation collision: ' + rel)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
                wheel_files[rel] = digest(target)
        records = [n for n in wheel_files if n.endswith('.dist-info/RECORD')]
        if len(records) != 1: raise ValueError('Require one installed distribution RECORD')
        installed_record = records[0]
        stream = io.StringIO(newline='')
        writer = csv.writer(stream, lineterminator='\n')
        for name in sorted(wheel_files):
            p = install / name
            relative = os.path.relpath(p, install / 'lib/python3.12/site-packages').replace('\\', '/')
            data = p.read_bytes()
            encoded = base64.urlsafe_b64encode(hashlib.sha256(data).digest()).rstrip(b'=').decode()
            writer.writerow([relative, '' if name == installed_record else 'sha256=' + encoded,
                             '' if name == installed_record else len(data)])
        (install / installed_record).write_text(stream.getvalue(), encoding='utf-8', newline='')
        wheel_files[installed_record] = digest(install / installed_record)
        if not any(n.endswith('.dist-info/entry_points.txt') for n in wheel_files):
            raise ValueError('Missing actual ros2bag entry points')
        wheel_reports[distribution_name] = {'path': str(wheels[0]), 'sha256': digest(wheels[0]), 'files': wheel_files}
    plugin_metadata = [n for n in wheel_reports['ros2bag_sqlite3_cli']['files']
                       if n.endswith('.dist-info/entry_points.txt')]
    plugin_dist = 'lib/python3.12/site-packages/ros2bag_sqlite3_cli-0.26.11.dist-info/'
    plugin_expected = {plugin_dist + n for n in ('METADATA', 'WHEEL', 'entry_points.txt', 'top_level.txt', 'RECORD')}
    plugin_expected.add('lib/python3.12/site-packages/ros2bag_sqlite3_cli/__init__.py')
    if set(wheel_reports['ros2bag_sqlite3_cli']['files']) != plugin_expected:
        raise ValueError('Unexpected SQLite CLI output membership')
    plugin_distribution = importlib.metadata.Distribution.at(install / plugin_dist)
    if (plugin_distribution.metadata['Name'] != 'ros2bag-sqlite3-cli' or
            plugin_distribution.version != '0.26.11'):
        raise ValueError('Unexpected SQLite CLI distribution identity')
    if len(plugin_metadata) != 1 or (install / plugin_metadata[0]).read_text().strip() != (
            '[ros2bag.storage_plugin_cli_extension]\nsqlite3 = ros2bag_sqlite3_cli'):
        raise ValueError('SQLite CLI wheel must register exactly the authentic sqlite3 entry point')
    if digest(install / 'lib/python3.12/site-packages/ros2bag_sqlite3_cli/__init__.py') != pins[
            sqlite_source / 'ros2bag_sqlite3_cli/__init__.py']:
        raise ValueError('Installed SQLite CLI module differs from upstream')
    (output / 'SQLITE_CLI_PROVENANCE.json').write_text(json.dumps({
        'version': '0.26.11', 'source': str(sqlite_source),
        'source_files': source_files['rosbag2_storage_sqlite3'],
        'ament_generated_setup': {'path': str(sqlite_setup), 'sha256': pins[sqlite_setup]},
        'ament_setup_generator': {'path': str(sqlite_ament_macro), 'sha256': pins[sqlite_ament_macro]},
        'method': 'offline pure wheel from hash-bound upstream module/setup.cfg and authentic ament-generated setup.py',
        'stock_ros2bag_unchanged': True,
        'wheel': wheel_reports['ros2bag_sqlite3_cli']}, indent=2) + '\n')
    generated = install / 'lib/python3.12/site-packages'
    modules = ['_compression_options', '_message_definitions', '_reader', '_storage', '_writer', '_info', '_transport', '_reindexer']
    for name in modules:
        p = generated / 'rosbag2_py' / (name + '.cpython-312-aarch64-linux-ohos.so')
        if not p.is_file(): raise ValueError('Missing required bag extension: ' + name)
        key = b'__pybind11_internals_v5_clang_libcpp_cxxabi1002__'
        if key not in p.read_bytes(): raise ValueError('Different actual pybind internals key: ' + name)
    for p in generated.rglob('*.py'):
        ast.parse(p.read_text(encoding='utf-8'), filename=str(p))
    # Build an explicit SONAME catalogue. R24 project providers outrank old R19
    # candidates; every conflicting candidate and selection remains in evidence.
    readelf = sdk / 'llvm/bin/llvm-readelf.exe'
    candidates = [(p, 'NEW_BUILD') for p in sorted(install.rglob('*')) if p.is_file() and p.read_bytes()[:4] == b'\x7fELF']
    candidates += [(Path(row['source']), 'QUALIFIED_R24') for name, row in base['files'].items() if name.startswith('lib/')]
    candidates += [(p, 'ACTUAL_LINK_INPUT') for p in sorted(linked_files)]
    candidates += [(Path(row['path']), 'R19_CANDIDATE') for row in json.loads((old_bundle / 'BUNDLE_MANIFEST.json').read_text())
                   if Path(row['path']).parent.name == 'lib']
    catalogue, elf, conflicts = {}, {}, []
    for index, (p, origin) in enumerate(candidates):
        if p in elf: continue
        if p.read_bytes()[:4] != b'\x7fELF': continue
        record(output, f'elf-{index:03d}', [readelf, '-h', '-d', '--dyn-syms', '--wide', p], environment, timeout_seconds=60)
        text = next((output / 'commands').glob(f'*-elf-{index:03d}.stdout')).read_text()
        if not re.search(r'Machine:\s+AArch64\b', text): raise ValueError('Non-target native input')
        needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', text)
        sonames = re.findall(r'\(SONAME\).*?\[([^\]]+)\]', text)
        if len(sonames) > 1 or any('/' in n or '\\' in n or ':' in n for n in needed + sonames):
            raise ValueError('Non-SONAME native reference: ' + str(p))
        row = {'source': str(p), 'sha256': digest(p), 'origin': origin, 'needed': needed,
               'soname': sonames[0] if sonames else None, 'search_paths': re.findall(r'\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]', text)}
        exports, requirements = set(), set()
        for line in text.splitlines():
            parts = line.split()
            if len(parts) < 8 or not parts[0].rstrip(':').isdigit(): continue
            symbol = parts[7].split('@')[0]
            if parts[6] != 'UND': exports.add(symbol)
            elif parts[4] == 'GLOBAL': requirements.add(symbol)
        row['exports'], row['required_symbols'] = sorted(exports), sorted(requirements)
        elf[p] = row
        if row['soname']:
            name = row['soname']
            if name in catalogue and catalogue[name]['sha256'] != row['sha256']:
                conflicts.append({'soname': name, 'selected': catalogue[name], 'rejected': row})
            else: catalogue.setdefault(name, row)
    external = {'libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so'}
    pending = [row for row in elf.values() if row['origin'] == 'NEW_BUILD' and
               (row['soname'] or Path(row['source']).name.endswith('.so'))]
    for name in ('librosbag2_storage_sqlite3.so',):
        if name not in catalogue: raise ValueError('Missing explicit SQLite dynamic root')
        pending.append(catalogue[name])
    chosen = {}
    while pending:
        row = pending.pop()
        name = row['soname'] or Path(row['source']).name
        if name in chosen:
            if chosen[name]['sha256'] != row['sha256']: raise ValueError('Conflicting root native file')
            continue
        chosen[name] = row
        if row['search_paths']: raise ValueError('Unreviewed selected ELF search path: ' + row['source'])
        for needed in row['needed']:
            if needed in external: continue
            if needed == 'libpython3.12.so.1.0':
                continue
            if needed not in catalogue: raise ValueError('Unresolved exact native dependency: ' + needed)
            pending.append(catalogue[needed])
    for name, expected in [('libfastrtps.so.2.14', pins[fast / 'lib/libfastrtps.so.2.14'])]:
        if name in chosen and chosen[name]['sha256'] != expected:
            raise ValueError('Old FastDDS provider selected')
    for p, sha in linked_files.items():
        if digest(p) != sha: raise ValueError('Actual link input changed')
        soname = elf[p]['soname']
        if soname and soname in chosen and chosen[soname]['sha256'] != sha:
            raise ValueError('Actual link provider differs from selected SONAME: ' + str(p))
    (output / 'LINK_INPUTS.json').write_text(json.dumps({str(p): sha for p, sha in linked_files.items()}, indent=2) + '\n')
    # Necessary symbol-name closure only; real C++/pybind ABI remains a target gate.
    providers = [Path(row['source']) for row in chosen.values()] + [dev / 'usr/lib/libpython3.12.so.1.0',
        root / 'build/r24-project-python/platform-inputs/ld-musl-aarch64.so.1']
    exports, required = set(), {}
    for index, p in enumerate(providers):
        if p in elf:
            exports.update(elf[p]['exports'])
            required[str(p)] = set(elf[p]['required_symbols'])
            continue
        record(output, f'provider-{index:03d}', [readelf, '--dyn-syms', '--wide', p], environment, timeout_seconds=60)
        text = next((output / 'commands').glob(f'*-provider-{index:03d}.stdout')).read_text()
        symbols = set()
        for line in text.splitlines():
            parts = line.split()
            if len(parts) < 8 or not parts[0].rstrip(':').isdigit(): continue
            symbol = parts[7].split('@')[0]
            if parts[6] != 'UND': exports.add(symbol)
            elif parts[4] == 'GLOBAL': symbols.add(symbol)
        required[str(p)] = symbols
    missing = {name: sorted(symbols - exports) for name, symbols in required.items() if symbols - exports}
    (output / 'SYMBOL_CLOSURE.json').write_text(json.dumps({'scope': 'NECESSARY_SYMBOL_NAMES_NOT_ABI_PROOF',
        'missing': missing, 'platform_loader_never_bundled': str(providers[-1])}, indent=2) + '\n')
    if missing: raise ValueError('Required symbol closure incomplete')
    overlay = {}
    for p in sorted(install.rglob('*')):
        if not p.is_file() or '__pycache__' in p.parts or p.suffix == '.pyc': continue
        name = p.relative_to(install).as_posix()
        if name.startswith('lib/python3.12/site-packages/'):
            rel = 'python/' + name[len('lib/python3.12/site-packages/'):]
        elif name.startswith('share/'): rel = name
        elif p in elf and elf[p]['soname']: continue
        else: continue
        overlay[rel] = {'source': str(p), 'sha256': digest(p)}
    for name, row in chosen.items():
        p = Path(row['source'])
        if row['soname']:
            overlay['lib/' + name] = {'source': str(p), 'sha256': row['sha256']}
    resources = []
    for row in json.loads((old_bundle / 'RESOURCES.json').read_text()):
        name = row['destination']
        if (name.startswith(('share/rosbag2_cpp/', 'share/rosbag2_storage/', 'share/rosbag2_storage_sqlite3/')) or
            name.rsplit('/', 1)[-1] in ('rosbag2_cpp', 'rosbag2_storage', 'rosbag2_storage_sqlite3')):
            p = Path(row['copied']['path'])
            if digest(p) != row['copied']['sha256']: raise ValueError('R19 resource changed')
            if name in overlay: raise ValueError('Resource collision')
            overlay[name] = {'source': str(p), 'sha256': digest(p)}
            resources.append(row)
    (output / 'SONAME_CLOSURE.json').write_text(json.dumps({'chosen': chosen, 'conflicts': conflicts,
        'external': sorted(external), 'target_python_external': {'path': str(dev / 'usr/lib/libpython3.12.so.1.0'),
        'sha256': pins[dev / 'usr/lib/libpython3.12.so.1.0']}, 'all_candidates': list(elf.values())}, indent=2) + '\n')
    (output / 'RESOURCES.json').write_text(json.dumps(resources, indent=2) + '\n')
    (output / 'OVERLAY.json').write_text(json.dumps({'schema': 1, 'scope': 'SQLITE_TARGET_BUILD_NOT_RUNTIME_VERIFIED',
        'files': overlay, 'versions': dict(zip(names, versions)), 'modules': modules,
        'wheel': wheel_reports['ros2bag'], 'sqlite_cli_wheel': wheel_reports['ros2bag_sqlite3_cli'],
        'base_python_package_sha256': pins[canonical / 'package/PACKAGE.json'],
        'unverified': ['target imports and QoS cross-extension cast', 'record/play and network communication',
                       'MCAP and compression plugins', 'full rosbag2_storage_default_plugins meta package']}, indent=2) + '\n')
    changed = [str(p) for p, sha in inputs.items() if not p.is_file() or digest(p) != sha]
    (output / 'NO_MUTATION.json').write_text(json.dumps({'status': 'FAIL' if changed else 'PASS', 'changed': changed}, indent=2) + '\n')
    if changed: raise ValueError('Build input mutation')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS', 'device_tested': False,
        'overlay_sha256': digest(output / 'OVERLAY.json'), 'native_modules': len(modules),
        'native_closure_files': len(chosen), 'payload_files': len(overlay)}, indent=2) + '\n')
    print('BAG_TARGET_OVERLAY_BUILT_NO_RUNTIME_TEST ' + str(output))


if __name__ == '__main__':
    actionbag_bag_main()
