"""Build the fixed R25 identity helper/bridge and optionally the existing native Action consumer. No devices."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
NAME = 'mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16'


def actionbag_build_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-action', action='store_true')
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build' or output.exists():
        raise ValueError('Require a fresh contained W3 build directory')
    spec = importlib.util.spec_from_file_location('r24_owned_build', Path(__file__).with_name('project_python.py'))
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    digest, record = recorder.project_python_digest, recorder.project_python_record
    evidence = root / 'build/r25-native-actionbag'
    authorization = json.loads((evidence / 'AUTHORIZATION.json').read_text())
    if (authorization['new_process_name'], authorization['apl'], authorization['permissions'], authorization['dcaps'], authorization['acls']) != (
            NAME, 'normal', ['ohos.permission.DISTRIBUTED_DATASYNC'], [], []):
        raise ValueError('Identity authorization schema mismatch')
    platform_proof = evidence / 'platform-check-fix1/RESULT.json'
    if json.loads(platform_proof.read_text())['status'] != 'PASS':
        raise ValueError('Current selected platform DSO binding required')
    selection_path = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_path.read_text())
    compiler = root / '.artifacts/toolchains/clang-windows-bb5cdf-20250726/clang_windows-x86_64-bb5cdf-20250726/bin/clang++.exe'
    sdk = Path(selection['sdk'])
    official = root / '.artifacts/reference/security_access_token/interfaces/innerkits'
    if digest(official / 'nativetoken/include/nativetoken_kit.h') != 'add868c861d75550ee3ad376f30680a8986e07463986d357c6131f7bae482e93':
        raise ValueError('Exact no-uid native provisioning header changed')
    native = root / '.artifacts/permissions/20260913T175519Z-preflight/A-libnativetoken_shared.z.so'
    setproc = root / '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libtokensetproc_shared.z.so'
    access = root / '.artifacts/permissions/20260913T180236Z-corrected-preflight/A-libaccesstoken_sdk.z.so'
    platform_cxx = root / '.artifacts/permissions/20260913T181632Z-abi-preflight/A-chipset-sdk-sp-libc++.so'
    pinned = {compiler: '70f8ecd180d0ba95763c8a3023622799434ab499f8752bb064edd7ba6824bd9a',
              native: 'b539ec42801e036db4b66f42af7a3e2e97610a13942d5e5b67c2c4572b9d7e81',
              setproc: '80b49bfb66f5ccff8a92bcf9c9759c1537409ae0119629b4b62162a3bd163e4d',
              access: '646e02a51b8e6f38c16ae194d46a7973d59b788e02ddc3414fadbeaef0edec4e',
              platform_cxx: '9466fb0d933689533bdf4b4962907f3e0c0970be278bd6ccf703ffa2aea838a6'}
    for path, expected in pinned.items():
        if digest(path) != expected:
            raise ValueError('Pinned platform input changed: ' + str(path))
    current = json.loads(platform_proof.read_text())['results']
    if [row['side'] for row in current] != ['A', 'B'] or any(
            list(row['observed'].values()) != [pinned[native], pinned[setproc], pinned[access], pinned[platform_cxx]]
            for row in current):
        raise ValueError('A/B current platform proof does not match selected native ABI')
    for path, expected in selection['tool_sha256'].items():
        if digest(path) != expected:
            raise ValueError('Selected SDK/build tool changed: ' + path)
    identity = Path(__file__).with_name('identity')
    source_files = list(identity.glob('*.hpp')) + list(identity.glob('*.h')) + list(identity.glob('*.cpp'))
    source_files += [Path(__file__), Path(__file__).with_name('IdentityStartup.cpp'), Path(__file__).with_name('project_python.py')]
    input_files = source_files + list(pinned) + [selection_path, platform_proof, evidence / 'AUTHORIZATION.json']
    for part in ('nativetoken', 'token_setproc', 'accesstoken'):
        input_files += [p for p in (official / part / 'include').rglob('*') if p.is_file()]
    hashes = {str(p): digest(p) for p in input_files}
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'LOCAL_BUILD_NO_DEVICE_NO_TOKEN_CALLS', 'sha256': hashes}, indent=2) + '\n')
    environment = os.environ.copy()
    temporary = output / 'tmp'
    temporary.mkdir()
    environment.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary),
                       PYTHONDONTWRITEBYTECODE='1', PYTHONPATH=str(Path(selection['host_tools']) / 'Lib/site-packages'))
    (output / 'ENVIRONMENT.json').write_text(json.dumps({key: environment[key] for key in
        ('TEMP', 'TMP', 'TMPDIR', 'PYTHONDONTWRITEBYTECODE', 'PYTHONPATH')}, indent=2) + '\n')
    common = [compiler, '--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
              '-std=c++17', '-O2', '-stdlib=libc++', '-nostdlib++', '-Wall', '-Wextra', '-Werror']
    for part in ('nativetoken', 'token_setproc', 'accesstoken'):
        common += ['-I' + str(official / part / 'include')]
    libraries = [setproc, access, platform_cxx, '-Wl,--allow-shlib-undefined']
    bridge = output / 'libmdds_actionbag_token_bridge.so'
    helper = output / 'actionbag_identity'
    record(output, 'fixed-readonly-bridge', common + ['-fPIC', '-shared', identity / 'actionbag_token_bridge.cpp',
        *libraries, '-Wl,-soname,' + bridge.name, '-MD', '-MF', output / 'bridge.d', '-o', bridge], environment)
    record(output, 'fixed-create-once-helper', common + ['-fPIE', '-pie', identity / 'actionbag_identity.cpp',
        identity / 'actionbag_token_bridge.cpp', native, *libraries, '-MD', '-MF', output / 'helper.d', '-o', helper], environment)
    readelf = sdk / 'llvm/bin/llvm-readelf.exe'
    for path in (helper, bridge):
        record(output, path.name + '-elf', [readelf, '-h', '-d', '--dyn-syms', path], environment)
    artifacts = {str(p): digest(p) for p in (helper, bridge)}
    if args.build_action:
        cache_path = root / 'build/r18ready1/native/CMakeCache.txt'
        cache = {}
        for line in cache_path.read_text().splitlines():
            if line and not line.startswith(('#', '//')) and '=' in line and ':' in line.split('=', 1)[0]:
                key, value = line.split('=', 1)
                cache[key.split(':', 1)[0]] = value
        old_prefix = root / 'build/r18-action-native/target-bridge-install'
        prefix = output / 'target-prefix'
        old_inventory = {}
        for path in old_prefix.rglob('*'):
            if path.is_symlink():
                raise ValueError('Unreviewed package symlink: ' + str(path))
            if path.is_file():
                relative = path.relative_to(old_prefix)
                old_inventory[relative.as_posix()] = digest(path)
                destination = prefix / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
        share = prefix / 'share/FastDDSDSoftBusTransportRos2Target'
        shutil.copyfile(Path(__file__).with_name('IdentityStartup.cpp'), share / 'IdentityStartup.cpp')
        for path in source_files:
            if path.parent == identity:
                shutil.copyfile(path, share / 'identity' / path.name)
        config = prefix / 'lib/cmake/FastDDSDSoftBusTransportRos2Target/FastDDSDSoftBusTransportRos2TargetConfig.cmake'
        config.write_text(config.read_text() + '\nset_property(TARGET FastDDSDSoftBusTransportRos2Target::factory_configuration APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS FASTDDS_DSOFTBUS_ACTIONBAG_IDENTITY=1)\n')
        (output / 'PREFIX_INPUTS.json').write_text(json.dumps({'source': str(old_prefix), 'sha256': old_inventory,
            'cache': str(cache_path), 'cache_sha256': digest(cache_path)}, indent=2) + '\n')
        prefixes = cache['CMAKE_PREFIX_PATH'].split(';')
        prefixes = [prefix.as_posix() if Path(p).resolve() == old_prefix.resolve() else Path(p).as_posix() for p in prefixes]
        environment['AMENT_PREFIX_PATH'] = os.pathsep.join(prefixes)
        (output / 'ACTION_ENVIRONMENT.json').write_text(json.dumps({'AMENT_PREFIX_PATH': environment['AMENT_PREFIX_PATH']}, indent=2) + '\n')
        flags, count = re.subn(r'-Wl,-Map,[^ ]+', '-Wl,-Map,' + (output / 'action.map').as_posix(), cache['CMAKE_EXE_LINKER_FLAGS'])
        if count != 1:
            raise ValueError('Expected exactly one declared Action linker map')
        action_source = REPO / 'examples/ros2_action_acceptance'
        action_build = output / 'action'
        argv = [selection['cmake'], '-S', action_source, '-B', action_build, '-G', 'Ninja',
                '-DCMAKE_MAKE_PROGRAM=' + cache['CMAKE_MAKE_PROGRAM'],
                '-DCMAKE_TOOLCHAIN_FILE=' + cache['CMAKE_TOOLCHAIN_FILE'], '-DOHOS_ARCH=arm64-v8a', '-DOHOS_STL=c++_shared',
                '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH', '-DCMAKE_PREFIX_PATH=' + ';'.join(prefixes),
                '-DFastDDSDSoftBusTransportRos2Target_DIR=' + (prefix / 'lib/cmake/FastDDSDSoftBusTransportRos2Target').as_posix(),
                '-DCMAKE_PROJECT_INCLUDE=' + str(Path(__file__).with_name('RosTargetBuildOptions.cmake')),
                '-DFASTDDS_DSOFTBUS_TOKEN_BRIDGE_LIBRARY=' + str(bridge), '-DCMAKE_EXE_LINKER_FLAGS=' + flags,
                '-DPython3_EXECUTABLE=' + selection['python'],
                '-DFastRTPS_INCLUDE_DIR=' + cache['FastRTPS_INCLUDE_DIR'],
                '-DFastRTPS_LIBRARY_RELEASE=' + cache['FastRTPS_LIBRARY_RELEASE']]
        for key, value in cache.items():
            package_dir = Path(value)
            if key == 'FastDDSDSoftBusTransportRos2Target_DIR':
                continue
            if key.endswith('_DIR') and package_dir.is_dir() and list(package_dir.glob('*[Cc]onfig.cmake')) and (
                    package_dir.is_relative_to(root) or
                    ((key.startswith('ament_') or key[:-4] in {'rosidl_adapter', 'rosidl_cmake', 'rosidl_parser',
                       'rosidl_generator_c', 'rosidl_generator_cpp', 'fastrtps_cmake_module'}) and
                     package_dir.is_relative_to(Path(selection['host_tools'])))):
                argv += ['-D' + key + '=' + value]
        record(output, 'action-configure', argv, environment)
        record(output, 'action-build', [selection['cmake'], '--build', action_build, '--parallel', '4'], environment)
        artifact = action_build / 'ros2_action_acceptance'
        artifacts[str(artifact)] = digest(artifact)
        record(output, 'action-elf', [readelf, '-h', '-d', '--dyn-syms', artifact], environment)
    if any(digest(p) != expected for p, expected in hashes.items()):
        raise ValueError('Build input changed during execution')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS', 'device_tested': False,
        'identity': NAME, 'artifacts': artifacts, 'input_manifest_sha256': digest(output / 'INPUTS.json'),
        'business_provisioning': 'NEVER', 'persistent_record_removal': 'NOT_PROMISED'}, indent=2) + '\n')


if __name__ == '__main__':
    actionbag_build_main()
