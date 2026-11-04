"""Build the complete archived example_interfaces package with target Python support. No devices."""
import argparse
import ast
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]


def actionbag_types_build_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build' or output.exists():
        raise ValueError('Require a fresh contained W3 build directory')
    here = Path(__file__).parent
    spec = importlib.util.spec_from_file_location('action_types_recorder', here / 'project_python.py')
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    digest, record = recorder.project_python_digest, recorder.project_python_record
    spec = importlib.util.spec_from_file_location('action_types_packages', here / 'build_ros_slice.py')
    packages = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(packages)
    selection_file = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_file.read_text())
    sdk, cmake, python, ninja, host = [Path(selection[k]) for k in ('sdk', 'cmake', 'python', 'ninja', 'host_tools')]
    canonical = REPO / 'build/r24-project-python/final-runtime'
    source = root / 'build/ros2-target/r2/src/ros/example_interfaces'
    source_receipt = root / 'build/ros2-target/r2/src/ros_example_interfaces.source.json'
    dev = root / 'build/r24-project-python/python-dev'
    dev_receipt = dev / 'MANIFEST.json'
    original = root / 'build/ros2-target/r2/ros/install/share/example_interfaces'
    native_cache = root / 'build/r18ready1/native/CMakeCache.txt'
    pins = {
        source / 'CMakeLists.txt': '4142b79135177d54d29053c6e4e048f0a684a80f62c1484924d373759ba1505e',
        source / 'action/Fibonacci.action': 'c4c59f0bba6fb0f8989582f492001cd65987b6bd34ddd00dd40f72136ba751d4',
        source_receipt: '041fac3f1573e3d884ea2915a9ff57bdf522f6a99d503dd026c394796f1c596b',
        canonical / 'BUILD.json': '04ff19bf38a052d13004cd80e7c2a32169c7ac8011d4d5cecc9119d21afd2e7e',
        canonical / 'package/PACKAGE.json': 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f',
        here / 'ProjectPython.cmake': '8ab09a4229974dadde3d4eedd334b297cf771c5294e1520c388c25e800f91311',
        dev / 'usr/lib/libpython3.12.so.1.0': '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a',
    }
    pins.update({Path(p): sha for p, sha in selection['tool_sha256'].items()})
    for tool in ('clang.exe', 'clang++.exe', 'ld.lld.exe', 'llvm-ar.exe', 'llvm-ranlib.exe', 'llvm-readelf.exe'):
        file = sdk / 'llvm/bin' / tool
        pins[file] = digest(file)
    shim = ninja.with_suffix('.shim')
    if shim.is_file():
        pins[shim] = digest(shim)
        match = re.fullmatch(r'path = "([^\"]+)"\s*', shim.read_text())
        if not match: raise ValueError('Unrecognized declared Ninja shim')
        real_ninja = Path(match[1])
        pins[real_ninja] = digest(real_ninja)
    for file, expected in pins.items():
        if digest(file) != expected:
            raise ValueError('Pinned input changed: ' + str(file))
    built = json.loads((canonical / 'BUILD.json').read_text())
    for name, expected in built['artifacts'].items():
        if digest(canonical / 'install' / name) != expected:
            raise ValueError('Qualified project Python build input changed: ' + name)
    development = json.loads(dev_receipt.read_text())
    headers = {dev / n: r['sha256'] for n, r in development.items()
               if n.startswith(('usr/include/python3.12/', 'numpy-include/'))}
    if sum('usr/include/python3.12/' in str(p).replace('\\', '/') for p in headers) != 214 or len(headers) != 245:
        raise ValueError('Require the exact 214 Python and31 NumPy target headers')
    for file, expected in headers.items():
        if digest(file) != expected:
            raise ValueError('Target development header changed: ' + str(file))
    declared = re.findall(r'"((?:action|msg|srv)/[^\"]+)"', (source / 'CMakeLists.txt').read_text())
    actual = sorted(p.relative_to(source).as_posix() for kind, suffix in [('action', 'action'), ('msg', 'msg'), ('srv', 'srv')]
                    for p in (source / kind).glob('*.' + suffix))
    if len(declared) != len(set(declared)) or sorted(declared) != actual or 'action/Fibonacci.action' not in actual:
        raise ValueError('Build must retain the complete unchanged interface membership')
    if digest(original / 'action/Fibonacci.action') != pins[source / 'action/Fibonacci.action']:
        raise ValueError('Archived Fibonacci does not match the existing native Action resource')
    cache_text = native_cache.read_text()
    match = re.search(r'^example_interfaces_DIR:[^=]+=(.+)$', cache_text, re.M)
    if not match or Path(match[1]).resolve() != (original / 'cmake').resolve():
        raise ValueError('Existing native Action cache is not bound to this example_interfaces install')
    install = output / 'install'
    deps = [Path(r['path']) for r in selection['dependency_prefixes']]
    fast = root / 'build/g2a1/target214-install'
    prefixes = [install, canonical / 'install', root / 'build/r18-action-native/dependency-build/install',
                root / 'build/ros2-target/r2/ros/install', fast] + deps
    current = {k: v for k, v in packages.package_dirs(host).items()
               if k.startswith('ament_') or k in packages.PURE_TOOLS or k == 'python_cmake_module'}
    for prefix in reversed(prefixes):
        current.update(packages.package_dirs(prefix))
    current.pop('example_interfaces', None)
    if current.get('rosidl_generator_py') != canonical / 'install/share/rosidl_generator_py/cmake':
        raise ValueError('Must use the existing project target Python generator')
    inputs = {**pins, **headers}
    for file in [Path(__file__), here / 'project_python.py', here / 'build_ros_slice.py', here / 'RosTargetBuildOptions.cmake',
                 selection_file, dev_receipt, native_cache, original / 'action/Fibonacci.idl', original / 'action/Fibonacci.json']:
        inputs[file] = digest(file)
    # Bind all selected target dependency files before execution, including the
    # existing generator and templates. Host-only discovery is limited to the
    # declared pure build packages, not a host ROS binary-library search path.
    for prefix in prefixes[1:]:
        for file in sorted(prefix.rglob('*')):
            if file.is_file() and '__pycache__' not in file.parts and file.suffix != '.pyc':
                if file.is_symlink():
                    raise ValueError('Unreviewed dependency symlink: ' + str(file))
                inputs.setdefault(file, digest(file))
    for folder in {v.parent for k, v in current.items() if v.is_relative_to(host)}:
        for file in folder.rglob('*'):
            if file.is_file(): inputs[file] = digest(file)
    for name in ('ament_package', 'rosidl_adapter', 'rosidl_parser', 'rosidl_cmake', 'rosidl_generator_c',
                 'rosidl_generator_cpp', 'rosidl_pycommon', 'rosidl_cli', 'em.py', 'lark', 'yaml'):
        folder = host / 'Lib/site-packages' / name
        for file in ([folder] if folder.is_file() else folder.rglob('*')):
            if file.is_file() and '__pycache__' not in file.parts and file.suffix != '.pyc': inputs[file] = digest(file)
    source_files = {p.relative_to(source).as_posix(): digest(p) for p in source.rglob('*') if p.is_file()}
    inputs.update({source / name: sha for name, sha in source_files.items()})
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    (output / 'tmp').mkdir()
    install.mkdir()
    snapshot = output / 'src/example_interfaces'
    shutil.copytree(source, snapshot)
    if any(digest(snapshot / n) != sha for n, sha in source_files.items()):
        raise ValueError('Source snapshot differs')
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'LOCAL_TARGET_TYPES_BUILD_NO_DEVICE',
        'sha256': {str(p): sha for p, sha in inputs.items()}, 'source_files': source_files,
        'interfaces': actual, 'package_dirs': {k: str(v) for k, v in current.items()}}, indent=2) + '\n')
    environment = os.environ.copy()
    environment.update(TEMP=str(output / 'tmp'), TMP=str(output / 'tmp'), TMPDIR=str(output / 'tmp'),
        PYTHONDONTWRITEBYTECODE='1', AMENT_PREFIX_PATH=os.pathsep.join(str(p) for p in prefixes),
        PYTHONPATH=os.pathsep.join([str(canonical / 'install/lib/python3.12/site-packages'), str(host / 'Lib/site-packages')]),
        PATH=os.pathsep.join([str(python.parent), str(python.parent / 'Library/bin'), environment.get('PATH', '')]))
    (output / 'ENVIRONMENT.json').write_text(json.dumps({k: environment[k] for k in
        ('TEMP', 'TMP', 'TMPDIR', 'PYTHONDONTWRITEBYTECODE', 'AMENT_PREFIX_PATH', 'PYTHONPATH', 'PATH')}, indent=2) + '\n')
    folder = output / 'target-build'
    libraries = [p / leaf for p in prefixes for leaf in ('lib', 'Lib') if (p / leaf).is_dir()]
    argv = [cmake, '-S', snapshot, '-B', folder, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(),
        '-DCMAKE_TOOLCHAIN_FILE=' + str(sdk / 'build/cmake/ohos.toolchain.cmake'),
        '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release',
        '-DBUILD_TESTING=OFF', '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
        '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
        '-DCMAKE_INSTALL_PREFIX=' + str(install), '-DPython3_EXECUTABLE=' + str(python),
        '-DCMAKE_PROJECT_INCLUDE=' + str(here / 'ProjectPython.cmake'),
        '-DPROJECT_PYTHON_INCLUDE=' + str(dev / 'usr/include/python3.12'),
        '-DPROJECT_PYTHON_LIBRARY=' + str(dev / 'usr/lib/libpython3.12.so.1.0'),
        '-DPROJECT_NUMPY_INCLUDE=' + str(dev / 'numpy-include'), '-DCMAKE_PREFIX_PATH=' + ';'.join(str(p) for p in prefixes),
        '-DCMAKE_SHARED_LINKER_FLAGS=' + ' '.join('-L"' + p.as_posix() + '"' for p in libraries),
        '-DFastRTPS_INCLUDE_DIR:PATH=' + (fast / 'include').as_posix(),
        '-DFastRTPS_LIBRARY_RELEASE:FILEPATH=' + (fast / 'lib/libfastrtps.so').as_posix(),
        '-DFastCDR_LIBRARY_RELEASE:FILEPATH=' + (deps[0] / 'lib/libfastcdr.so').as_posix()]
    argv += ['-D' + k + '_DIR:PATH=' + v.as_posix() for k, v in sorted(current.items())]
    record(output, 'configure', argv, environment)
    record(output, 'build', [cmake, '--build', folder, '--parallel', '4', '--verbose', '--', '-d', 'keeprsp'], environment)
    record(output, 'install', [cmake, '--install', folder], environment)
    record(output, 'ninja-commands', [ninja, '-C', folder, '-t', 'commands'], environment)
    record(output, 'ninja-deps', [ninja, '-C', folder, '-t', 'deps'], environment)
    dep_log = next((output / 'commands').glob('*-ninja-deps.stdout')).read_text()
    used_headers = {}
    # Ninja repeats shared SDK headers for every object. Bind each actual path
    # once; repeatedly resolving/hashing it is not additional evidence.
    for name in sorted({line.strip() for line in dep_log.splitlines() if line.startswith('    ')}):
        file = Path(name)
        if not file.is_absolute(): file = folder / file
        if not file.is_file(): raise ValueError('Compiler dependency missing: ' + str(file))
        used_headers[str(file.resolve())] = digest(file)
    (output / 'COMPILER_INPUTS.json').write_text(json.dumps(used_headers, indent=2) + '\n')
    response_files = {p.relative_to(folder).as_posix(): digest(p) for p in folder.rglob('*.rsp')}
    if not response_files: raise ValueError('Actual Ninja compile/link response files were not retained')
    (output / 'BUILD_COMMAND_INPUTS.json').write_text(json.dumps({'response_files': response_files,
        'compile_commands_sha256': digest(folder / 'compile_commands.json'),
        'build_ninja_sha256': digest(folder / 'build.ninja'),
        'rules_ninja_sha256': digest(folder / 'CMakeFiles/rules.ninja')}, indent=2) + '\n')
    wire = {}
    for leaf in ('Fibonacci.action', 'Fibonacci.idl', 'Fibonacci.json'):
        left, right = original / 'action' / leaf, install / 'share/example_interfaces/action' / leaf
        equal = json.loads(left.read_text()) == json.loads(right.read_text()) if leaf.endswith('.json') else (
            left.read_bytes().replace(b'\r\n', b'\n') == right.read_bytes().replace(b'\r\n', b'\n'))
        wire[leaf] = {'native_path': str(left), 'native_sha256': digest(left), 'new_sha256': digest(right),
                      'equal_json_or_LF_text': equal}
        if not equal: raise ValueError('Native Action wire resource changed: ' + leaf)
    generated = install / 'lib/python3.12/site-packages/example_interfaces'
    python_files = list(generated.rglob('*.py'))
    if not python_files: raise ValueError('Python generation produced no files')
    for file in python_files: ast.parse(file.read_text(), filename=str(file))
    for name in actual:
        parent, filename = name.split('/')
        stem = filename.rsplit('.', 1)[0]
        snake = re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', re.sub(r'(.)([A-Z][a-z]+)', r'\1_\2', stem)).lower()
        if not (generated / parent / ('_' + snake + '.py')).is_file():
            raise ValueError('Missing generated Python interface: ' + name)
    natives = [p for p in install.rglob('*') if p.is_file() and p.read_bytes()[:4] == b'\x7fELF']
    readelf = sdk / 'llvm/bin/llvm-readelf.exe'
    native_exports = {}
    for i, file in enumerate(natives):
        record(output, f'elf-{i:02d}', [readelf, '-h', '-d', '--dyn-syms', '--wide', file], environment)
        elf_text = next((output / 'commands').glob(f'*-elf-{i:02d}.stdout')).read_text()
        if not re.search(r'Machine:\s+AArch64\b', elf_text): raise ValueError('Non-target ELF: ' + str(file))
        needed = re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', elf_text)
        if any('/' in name or '\\' in name or ':' in name for name in needed):
            raise ValueError('Non-SONAME native dependency: ' + str(file))
        if re.search(r'\((?:RPATH|RUNPATH)\)', elf_text): raise ValueError('Unreviewed ELF runtime search path: ' + str(file))
        symbols = set()
        for line in elf_text.splitlines():
            fields = line.split()
            if len(fields) >= 8 and fields[0].rstrip(':').isdigit() and fields[6] != 'UND': symbols.add(fields[7])
        native_exports[file] = symbols
    capsule = generated / 'example_interfaces_s__rosidl_typesupport_c.cpython-312-aarch64-linux-ohos.so'
    if capsule not in natives or not (install / 'lib/libexample_interfaces__rosidl_generator_py.so').is_file():
        raise ValueError('Required Python C typesupport capsule/generator library missing')
    if 'PyInit_example_interfaces_s__rosidl_typesupport_c' not in native_exports[capsule]:
        raise ValueError('Missing native Python module initialization export')
    if 'rosidl_typesupport_c__get_action_type_support_handle__example_interfaces__action__Fibonacci' not in native_exports[install / 'lib/libexample_interfaces__rosidl_typesupport_c.so']:
        raise ValueError('Missing actual Fibonacci Action C typesupport export')
    if any(digest(file) != expected for file, expected in inputs.items()):
        raise ValueError('Build input changed during execution')
    artifacts = {p.relative_to(install).as_posix(): digest(p) for p in sorted(install.rglob('*')) if p.is_file()}
    overlay = {}
    omitted_host_bytecode = []
    for name, sha in artifacts.items():
        if '__pycache__' in Path(name).parts or name.endswith('.pyc'):
            omitted_host_bytecode.append(name)
            continue
        if name.startswith('lib/python3.12/site-packages/'):
            relative = 'python/' + name[len('lib/python3.12/site-packages/'):]
        elif name.startswith(('lib/', 'share/')):
            relative = name
        else: continue
        overlay[relative] = {'source': str(install / name), 'sha256': sha}
    (output / 'OVERLAY.json').write_text(json.dumps({'schema': 1, 'scope': 'COMPLETE_EXAMPLE_INTERFACES_TARGET_BUILD_NOT_RUNTIME_TESTED',
        'files': overlay, 'interfaces': actual, 'omitted_host_bytecode': omitted_host_bytecode,
        'base_python_package_sha256': pins[canonical / 'package/PACKAGE.json']}, indent=2) + '\n')
    (output / 'WIRE_BINDING.json').write_text(json.dumps({'native_action_cache': str(native_cache),
        'native_action_cache_sha256': inputs[native_cache], 'resources': wire}, indent=2) + '\n')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS', 'device_tested': False,
        'python_action_runtime_tested': False, 'communication_verified': False,
        'interfaces': actual, 'python_ast_parsed': len(python_files), 'native_members': len(natives),
        'artifacts': artifacts, 'input_manifest_sha256': digest(output / 'INPUTS.json'),
        'overlay_manifest_sha256': digest(output / 'OVERLAY.json'), 'wire_binding_sha256': digest(output / 'WIRE_BINDING.json')}, indent=2) + '\n')


if __name__ == '__main__':
    actionbag_types_build_main()
