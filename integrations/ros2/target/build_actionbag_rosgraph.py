"""Build the one missing official rosgraph_msgs Python dependency. No devices.

Host Python only generates code. Target compilation uses the existing project
CPython/ROS providers; no installed R24 payload or transport code is changed.
"""
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


def actionbag_rosgraph_build_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if REPO != root / 'build/r15-lifecycle/publish-worktree' or output == REPO / 'build' or not output.is_relative_to(REPO / 'build') or output.exists():
        raise ValueError('Require selected ROOT/W3 and fresh contained output')
    here = Path(__file__).parent
    def load(name, path):
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    recorder = load('rosgraph_recorder', here / 'project_python.py')
    packages = load('rosgraph_packages', here / 'build_ros_slice.py')
    digest, record = recorder.project_python_digest, recorder.project_python_record
    selection_file = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_file.read_text())
    sdk, cmake, python, ninja, host = [Path(selection[k]) for k in ('sdk', 'cmake', 'python', 'ninja', 'host_tools')]
    canonical = REPO / 'build/r24-project-python/final-runtime'
    source = root / 'build/ros2-target/r2/src/ros/rcl_interfaces/rosgraph_msgs'
    receipt = root / 'build/ros2-target/r2/src/ros_rcl_interfaces.source.json'
    original = root / 'build/ros2-target/r2/ros/install/share/rosgraph_msgs'
    dev = root / 'build/r24-project-python/python-dev'
    baseline = REPO / 'build/r25-native-actionbag/runtime-qualified-cli-types-providerfix2'
    pins = {
        source / 'CMakeLists.txt': 'f17554c8d1b6ea63a2c943a9653545f6d12498114722433c4b3fe745500a04c0',
        source / 'package.xml': 'fded8f7b97d39d3aeb1d1b55104cbd1f0ad5057d6f5dfb0c0a9c95f0b35827e1',
        source / 'msg/Clock.msg': 'a8d1d14b0057bea4040f2e9662279e6ee270bd9fd6d00e1b2bcfb449bf4d521c',
        receipt: 'e826d45c9e36acd7541bac77bbc35ece2518d9f9ac2e0444d9f53473ae7a62e5',
        canonical / 'BUILD.json': '04ff19bf38a052d13004cd80e7c2a32169c7ac8011d4d5cecc9119d21afd2e7e',
        canonical / 'package/PACKAGE.json': 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f',
        here / 'ProjectPython.cmake': '8ab09a4229974dadde3d4eedd334b297cf771c5294e1520c388c25e800f91311',
        dev / 'usr/lib/libpython3.12.so.1.0': '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a',
        baseline / 'RUNTIME_PACKAGE.json': 'e5ca85f5c36569cc1e6eb1d5eb046ec8c6ab6a8b8b1f775c58acb250944ba648',
        baseline / 'ELF_CLOSURE.json': 'ac0656eb7714324947a56542073d852096bc31659d0163577416374ad4cac7ee',
    }
    pins.update({Path(p): h for p, h in selection['tool_sha256'].items()})
    for name in ('clang.exe', 'clang++.exe', 'ld.lld.exe', 'llvm-ar.exe', 'llvm-ranlib.exe', 'llvm-readelf.exe'):
        tool = sdk / 'llvm/bin' / name
        pins[tool] = digest(tool)
    tool_aliases = []
    shim = ninja.with_suffix('.shim')
    if shim.is_file():
        pins[shim] = digest(shim)
        match = re.fullmatch(r'path = "([^\"]+)"\s*', shim.read_text())
        if not match: raise ValueError('Unrecognized declared Ninja shim')
        declared = Path(match[1])
        resolved = declared.resolve(strict=True)
        if digest(declared) != digest(resolved): raise ValueError('Declared Ninja alias differs from canonical tool bytes')
        tool_aliases.append({'selected_shim_executable': str(ninja), 'shim': str(shim), 'shim_sha256': pins[shim],
            'declared_target': str(declared), 'canonical_target': str(resolved), 'sha256': digest(resolved)})
        pins[resolved] = digest(resolved)
        ninja = resolved
    for path, expected in pins.items():
        if digest(path) != expected: raise ValueError('Pinned input changed: ' + str(path))
    for name, expected in json.loads((canonical / 'BUILD.json').read_text())['artifacts'].items():
        if digest(canonical / 'install' / name) != expected: raise ValueError('Qualified project provider changed')
    development = json.loads((dev / 'MANIFEST.json').read_text())
    headers = {dev / n: r['sha256'] for n, r in development.items() if n.startswith(('usr/include/python3.12/', 'numpy-include/'))}
    if len(headers) != 245 or sum('usr/include/python3.12/' in p.as_posix() for p in headers) != 214:
        raise ValueError('Require exact214 CPython and31 NumPy headers')
    for path, expected in headers.items():
        if digest(path) != expected: raise ValueError('Target header changed')
    interfaces = sorted(re.findall(r'\bmsg/[A-Za-z]+\.msg\b', (source / 'CMakeLists.txt').read_text()))
    actual = sorted('msg/' + p.name for p in (source / 'msg').glob('*.msg'))
    if len(interfaces) != 9 or interfaces != actual or len(set(interfaces)) != 9:
        raise ValueError('Require complete official nine-message rosgraph package')
    install = output / 'install'
    deps = [Path(r['path']) for r in selection['dependency_prefixes']]
    fast = root / 'build/g2a1/target214-install'
    prefixes = [install, canonical / 'install', root / 'build/r18-action-native/dependency-build/install', root / 'build/ros2-target/r2/ros/install', fast] + deps
    current = {k: v for k, v in packages.package_dirs(host).items() if k.startswith('ament_') or k in packages.PURE_TOOLS or k == 'python_cmake_module'}
    for prefix in reversed(prefixes): current.update(packages.package_dirs(prefix))
    current.pop('rosgraph_msgs', None)
    if current.get('rosidl_generator_py') != canonical / 'install/share/rosidl_generator_py/cmake':
        raise ValueError('Require the reviewed project Python generator')
    inputs = {**pins, **headers}
    for path in (Path(__file__), here / 'project_python.py', here / 'build_ros_slice.py', here / 'RosTargetBuildOptions.cmake', selection_file, dev / 'MANIFEST.json'):
        inputs[path] = digest(path)
    for prefix in prefixes[1:]:
        for path in sorted(prefix.rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc':
                if path.is_symlink(): raise ValueError('Unreviewed dependency symlink')
                inputs.setdefault(path, digest(path))
    for folder in {p.parent for p in current.values() if p.is_relative_to(host)}:
        for path in folder.rglob('*'):
            if path.is_file(): inputs[path] = digest(path)
    for name in ('ament_package', 'rosidl_adapter', 'rosidl_parser', 'rosidl_cmake', 'rosidl_generator_c', 'rosidl_generator_cpp', 'rosidl_pycommon', 'rosidl_cli', 'em.py', 'lark', 'yaml'):
        folder = host / 'Lib/site-packages' / name
        for path in ([folder] if folder.is_file() else folder.rglob('*')):
            if path.is_file() and '__pycache__' not in path.parts and path.suffix != '.pyc': inputs[path] = digest(path)
    source_files = {p.relative_to(source).as_posix(): digest(p) for p in source.rglob('*') if p.is_file()}
    inputs.update({source / n: h for n, h in source_files.items()})
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    (output / 'tmp').mkdir()
    install.mkdir()
    snapshot = output / 'src/rosgraph_msgs'
    shutil.copytree(source, snapshot)
    if any(digest(snapshot / n) != h for n, h in source_files.items()): raise ValueError('Source snapshot changed')
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'ROS_GRAPH_NODE_DEPENDENCY_TARGET_BUILD_NO_DEVICE', 'sha256': {str(p): h for p, h in inputs.items()}, 'source_files': source_files, 'interfaces': interfaces, 'package_dirs': {k: str(v) for k, v in current.items()}, 'tool_aliases': tool_aliases}, indent=2) + '\n')
    env = os.environ.copy()
    env.update(TEMP=str(output / 'tmp'), TMP=str(output / 'tmp'), TMPDIR=str(output / 'tmp'), PYTHONDONTWRITEBYTECODE='1', AMENT_PREFIX_PATH=os.pathsep.join(str(p) for p in prefixes), PYTHONPATH=os.pathsep.join([str(canonical / 'install/lib/python3.12/site-packages'), str(host / 'Lib/site-packages')]), PATH=os.pathsep.join([str(python.parent), str(python.parent / 'Library/bin'), env.get('PATH', '')]))
    (output / 'ENVIRONMENT.json').write_text(json.dumps({k: env[k] for k in ('TEMP','TMP','TMPDIR','PYTHONDONTWRITEBYTECODE','AMENT_PREFIX_PATH','PYTHONPATH','PATH')}, indent=2) + '\n')
    folder = output / 'target-build'
    libraries = [p / leaf for p in prefixes for leaf in ('lib', 'Lib') if (p / leaf).is_dir()]
    argv = [cmake, '-S', snapshot, '-B', folder, '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM=' + ninja.as_posix(), '-DCMAKE_TOOLCHAIN_FILE=' + str(sdk / 'build/cmake/ohos.toolchain.cmake'), '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared', '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_TESTING=OFF', '-DBUILD_SHARED_LIBS=ON', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF', '-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF', '-DCMAKE_INSTALL_PREFIX=' + str(install), '-DPython3_EXECUTABLE=' + str(python), '-DCMAKE_PROJECT_INCLUDE=' + str(here / 'ProjectPython.cmake'), '-DPROJECT_PYTHON_INCLUDE=' + str(dev / 'usr/include/python3.12'), '-DPROJECT_PYTHON_LIBRARY=' + str(dev / 'usr/lib/libpython3.12.so.1.0'), '-DPROJECT_NUMPY_INCLUDE=' + str(dev / 'numpy-include'), '-DCMAKE_PREFIX_PATH=' + ';'.join(str(p) for p in prefixes), '-DCMAKE_SHARED_LINKER_FLAGS=' + ' '.join('-L"' + p.as_posix() + '"' for p in libraries), '-DFastRTPS_INCLUDE_DIR:PATH=' + (fast / 'include').as_posix(), '-DFastRTPS_LIBRARY_RELEASE:FILEPATH=' + (fast / 'lib/libfastrtps.so').as_posix(), '-DFastCDR_LIBRARY_RELEASE:FILEPATH=' + (deps[0] / 'lib/libfastcdr.so').as_posix()]
    argv += ['-D' + k + '_DIR:PATH=' + v.as_posix() for k, v in sorted(current.items())]
    record(output, 'configure', argv, env)
    record(output, 'build', [cmake, '--build', folder, '--parallel', '4', '--verbose', '--', '-d', 'keeprsp'], env)
    record(output, 'install', [cmake, '--install', folder], env)
    record(output, 'ninja-commands', [ninja, '-C', folder, '-t', 'commands'], env)
    record(output, 'ninja-deps', [ninja, '-C', folder, '-t', 'deps'], env)
    dependencies = next((output / 'commands').glob('*-ninja-deps.stdout')).read_text()
    compiler_inputs = {}
    for name in sorted({line.strip() for line in dependencies.splitlines() if line.startswith('    ')}):
        path = Path(name)
        if not path.is_absolute(): path = folder / path
        if not path.is_file(): raise ValueError('Compiler dependency missing')
        compiler_inputs[str(path.resolve())] = digest(path)
    responses = {p.relative_to(folder).as_posix(): digest(p) for p in folder.rglob('*.rsp')}
    if not responses: raise ValueError('Compile/link response files not retained')
    (output / 'COMPILER_INPUTS.json').write_text(json.dumps(compiler_inputs, indent=2) + '\n')
    (output / 'BUILD_COMMAND_INPUTS.json').write_text(json.dumps({'response_files': responses, 'compile_commands_sha256': digest(folder / 'compile_commands.json'), 'build_ninja_sha256': digest(folder / 'build.ninja'), 'rules_ninja_sha256': digest(folder / 'CMakeFiles/rules.ninja')}, indent=2) + '\n')
    wire = {}
    for name in interfaces:
        for suffix in ('.msg', '.idl', '.json'):
            leaf = str(Path(name).with_suffix(suffix))
            left, right = original / leaf, install / 'share/rosgraph_msgs' / leaf
            inputs[left] = digest(left)
            equal = json.loads(left.read_text()) == json.loads(right.read_text()) if suffix == '.json' else left.read_bytes().replace(b'\r\n', b'\n') == right.read_bytes().replace(b'\r\n', b'\n')
            if not equal: raise ValueError('Existing official wire resource differs: ' + leaf)
            wire[leaf] = {'original_sha256': digest(left), 'generated_sha256': digest(right), 'equal_json_or_LF_text': True}
    generated = install / 'lib/python3.12/site-packages/rosgraph_msgs'
    python_files = list(generated.rglob('*.py'))
    for path in python_files: ast.parse(path.read_text(), filename=str(path))
    if not (generated / 'msg/_clock.py').is_file() or len(list((generated / 'msg').glob('_*.py'))) != 10:
        raise ValueError('Complete nine-message Python generation missing')
    native = {}
    for path in sorted(install.rglob('*')):
        if not path.is_file() or path.read_bytes()[:4] != b'\x7fELF': continue
        label = 'elf-' + str(len(native))
        record(output, label, [sdk / 'llvm/bin/llvm-readelf.exe', '-h', '-d', '--dyn-syms', '--wide', path], env)
        text = next((output / 'commands').glob('*-' + label + '.stdout')).read_text()
        if not re.search(r'Machine:\s+AArch64\b', text) or not re.search(r'Class:\s+ELF64\b', text) or re.search(r'\((?:RPATH|RUNPATH)\)', text): raise ValueError('Unexpected target ELF architecture/search path')
        needed = re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', text)
        if any('/' in n or '\\' in n or ':' in n for n in needed): raise ValueError('Non-SONAME dependency')
        symbols = {f[7] for line in text.splitlines() if len(f := line.split()) >= 8 and f[0].rstrip(':').isdigit() and f[6] != 'UND'}
        native[path.relative_to(install).as_posix()] = {'sha256': digest(path), 'needed': needed, 'soname': re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', text), 'exports': sorted(symbols)}
    capsule = 'lib/python3.12/site-packages/rosgraph_msgs/rosgraph_msgs_s__rosidl_typesupport_c.cpython-312-aarch64-linux-ohos.so'
    if capsule not in native or 'PyInit_rosgraph_msgs_s__rosidl_typesupport_c' not in native[capsule]['exports'] or 'rosidl_typesupport_c__get_message_type_support_handle__rosgraph_msgs__msg__Clock' not in native['lib/librosgraph_msgs__rosidl_typesupport_c.so']['exports']:
        raise ValueError('Real Clock Python/native typesupport exports missing')
    existing = json.loads((baseline / 'ELF_CLOSURE.json').read_text())
    names = {n for row in native.values() for n in row['soname']}
    names.update(n for row in existing['objects'].values() for n in row['soname'])
    names.update(existing['actual_external_objects'])
    unresolved = {n for row in native.values() for n in row['needed']} - names
    if unresolved: raise ValueError('New rosgraph ELF has unresolved existing-provider dependency: ' + ','.join(sorted(unresolved)))
    if any(digest(p) != h for p, h in inputs.items()): raise ValueError('Selected input changed during build')
    artifacts = {p.relative_to(install).as_posix(): digest(p) for p in sorted(install.rglob('*')) if p.is_file()}
    overlay, omitted = {}, []
    for name, h in artifacts.items():
        if '__pycache__' in Path(name).parts or name.endswith('.pyc'): omitted.append(name); continue
        if name.startswith('lib/python3.12/site-packages/'): member = 'python/' + name[len('lib/python3.12/site-packages/'):]
        elif name.startswith(('lib/', 'share/')): member = name
        else: continue
        overlay[member] = {'source': str(install / name), 'sha256': h}
    (output / 'OVERLAY.json').write_text(json.dumps({'schema': 1, 'scope': 'COMPLETE_OFFICIAL_ROSGRAPH_MSGS_NODE_DEPENDENCY_NOT_RUNTIME_TESTED', 'files': overlay, 'interfaces': interfaces, 'omitted_host_bytecode': omitted, 'base_python_package_sha256': pins[canonical / 'package/PACKAGE.json']}, indent=2) + '\n')
    (output / 'WIRE_BINDING.json').write_text(json.dumps(wire, indent=2) + '\n')
    (output / 'NATIVE.json').write_text(json.dumps(native, indent=2) + '\n')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS_NOT_RUNTIME_TESTED', 'interfaces': interfaces, 'python_ast_parsed': len(python_files), 'native_members': len(native), 'artifacts': artifacts, 'input_manifest_sha256': digest(output / 'INPUTS.json'), 'overlay_sha256': digest(output / 'OVERLAY.json'), 'device_tested': False, 'node_import_verified': False, 'communication_verified': False}, indent=2) + '\n')


if __name__ == '__main__':
    actionbag_rosgraph_build_main()
