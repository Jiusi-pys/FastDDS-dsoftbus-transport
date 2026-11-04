"""Build/package the same-process token-first Python launcher. Never runs a device."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
BASE_SHA = 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f'
BRIDGE_SHA = 'd7e503a01c8d5d86fd49858bb7e375f49e71585081d87f450cf0521f7611ee57'
PYTHON_SHA = '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a'
OWNED_PYTHON_SHA = '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25'
NAME = 'mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16'


def actionbag_python_visibility_derivative(source):
    """Exactly one approved ELF flag byte; no layout/code/symbol/ABI rewrite."""
    original = source.read_bytes()
    if hashlib.sha256(original).hexdigest() != PYTHON_SHA or len(original) < 64:
        raise ValueError('Exact original CPython provider required')
    if original[:7] != b'\x7fELF\x02\x01\x01' or struct.unpack_from('<HH', original, 16) != (3, 183):
        raise ValueError('Require ELF64LE ET_DYN AArch64 provider')
    phoff = struct.unpack_from('<Q', original, 32)[0]
    phsize, phnum = struct.unpack_from('<HH', original, 54)
    if phsize != 56 or not phnum or phoff < 64 or phoff + phsize * phnum > len(original):
        raise ValueError('Invalid bounded ELF program header table')
    dynamic = []
    for index in range(phnum):
        entry = struct.unpack_from('<IIQQQQQQ', original, phoff + index * phsize)
        if entry[0] == 2:
            dynamic.append(entry)
    if len(dynamic) != 1:
        raise ValueError('Require unique PT_DYNAMIC')
    entry = dynamic[0]
    start, size = entry[2], entry[5]
    if not size or size % 16 or start + size > len(original):
        raise ValueError('Unbounded PT_DYNAMIC')
    flags, terminated = [], False
    for offset in range(start, start + size, 16):
        tag, value = struct.unpack_from('<qQ', original, offset)
        if tag == 0:
            terminated = True
            break
        if tag == 0x6ffffffb:
            flags.append((offset + 8, value))
    if not terminated or flags != [(4594312, 1)]:
        raise ValueError('Require exact existing unique DT_FLAGS_1 NOW preimage')
    changed = bytearray(original)
    struct.pack_into('<Q', changed, flags[0][0], 3)  # Existing NOW | DF_1_GLOBAL only.
    differences = [n for n, (before, after) in enumerate(zip(original, changed)) if before != after]
    if differences != [4594312] or changed[4594312] != 3 or hashlib.sha256(changed).hexdigest() != OWNED_PYTHON_SHA:
        raise ValueError('Unapproved CPython derivative byte change')
    return bytes(changed), {'kind': 'OWNED_ELF_FLAGS_DERIVATIVE', 'baseline_sha256': PYTHON_SHA,
        'owned_sha256': OWNED_PYTHON_SHA, 'flags_1': 3, 'old_flags_1': 1,
        'changed_byte_offsets': differences, 'source': str(source),
        'layout_code_exports_soname_needed_abi': 'UNCHANGED',
        'visibility': 'Explicit process-wide CPython symbol visibility for extension relocations',
        'canonical_runtime_modified': False}


def actionbag_python_build_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--token-bridge', type=Path)
    parser.add_argument('--script', type=Path, action='append', default=[],
                        help='Explicit Python script to seal under package/scripts; no execution')
    args = parser.parse_args()
    root, output = args.inputs_root.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build' or output.exists():
        raise ValueError('Require fresh contained W3 build directory')
    spec = importlib.util.spec_from_file_location('r25_owned_python_build', Path(__file__).with_name('project_python.py'))
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    digest, record = recorder.project_python_digest, recorder.project_python_record
    base = REPO / 'build/r24-project-python/final-runtime/package'
    base_manifest = base / 'PACKAGE.json'
    if digest(base_manifest) != BASE_SHA:
        raise ValueError('Exact qualified project Python package required')
    package = json.loads(base_manifest.read_text())
    if package['runtime_prefix'] != '/data/python312-rk3588a-verify-c646982b346f':
        raise ValueError('Unselected Python runtime prefix')
    base_files = {}
    for name, row in package['files'].items():
        file = base / name
        if file.is_symlink() or not file.resolve().is_relative_to(base) or digest(file) != row['sha256']:
            raise ValueError('Project Python input changed: ' + name)
        base_files[file] = row['sha256']
    selection_path = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_path.read_text())
    sdk = Path(selection['sdk'])
    compiler, readelf = sdk / 'llvm/bin/clang++.exe', sdk / 'llvm/bin/llvm-readelf.exe'
    for file, expected in selection['tool_sha256'].items():
        if digest(file) != expected:
            raise ValueError('Selected target build tool changed: ' + file)
    dev = root / 'build/r24-project-python/python-dev'
    dev_manifest = dev / 'MANIFEST.json'
    dev_files = json.loads(dev_manifest.read_text())
    headers = {dev / name: row['sha256'] for name, row in dev_files.items()
               if name.startswith('usr/include/python3.12/')}
    if len(headers) != 214:
        raise ValueError('Expected exact 214 target Python headers')
    libpython = dev / 'usr/lib/libpython3.12.so.1.0'
    for file, expected in {**headers, libpython: PYTHON_SHA}.items():
        if digest(file) != expected:
            raise ValueError('Target Python development input changed: ' + str(file))
    owned_python_bytes, python_provider = actionbag_python_visibility_derivative(libpython)
    bridge = args.token_bridge.resolve() if args.token_bridge else REPO / 'build/r25-native-actionbag/initial-test-build/libmdds_actionbag_token_bridge.so'
    if bridge.name != 'libmdds_actionbag_token_bridge.so' or digest(bridge) != BRIDGE_SHA:
        raise ValueError('Exact new identity bridge required; legacy bridge is forbidden')
    here = Path(__file__).parent
    frozen_shared = {
        here / 'IdentityStartup.cpp': '0c8f5f8ce17277238550d641f7528c96b97e0306a79a8427223e7c353fb82fb1',
        here / 'identity/token_first_entry.hpp': '216516c75df7b76c017b9e3c95f575822db9ba14efd499d22fa788f3140285bf',
        here / 'identity/token_first_entry.cpp': '682c100f9ffa7635e39fc566fbccb372b3d74112f3308e0f52be3b47b282e7c8',
        here / 'identity/actionbag_identity.hpp': '06cddc6832d9b5076e28d0fe8675a01cc747884ebb93d3a57dea0ee6fa5b7217',
        here / 'identity/actionbag_token_bridge.h': 'f0471e01fb840d386728d6f953194b3f8c13017b15f35861092848c98d3633f2',
    }
    for file, expected in frozen_shared.items():
        if digest(file) != expected:
            raise ValueError('Frozen reviewed identity source changed: ' + str(file))
    sources = [here / 'actionbag_python_entry.cpp', here / 'IdentityStartup.cpp',
               here / 'identity/token_first_entry.cpp', here.parent / 'FactoryAbiProbe.cpp']
    # Frozen shared sources are consumed without copying or editing their tree.
    inputs = {**base_files, **headers, libpython: PYTHON_SHA, bridge: BRIDGE_SHA}
    for file in sources + [Path(__file__), here / 'project_python.py', selection_path, dev_manifest, base_manifest,
            here / 'identity/token_first_entry.hpp', here / 'identity/actionbag_identity.hpp',
            here / 'identity/token_bridge.h', here / 'identity/actionbag_token_bridge.h',
            here.parent / 'StartupDiagnostics.hpp', here.parent / 'TeardownDiagnostics.hpp',
            here.parent / 'NativePrefix.hpp', here.parent / 'ResidualDiagnostics.hpp',
            REPO / 'examples/ros2_transport/FailureProtocol.hpp', readelf]:
        inputs[file] = digest(file)
    inputs.update({Path(file): expected for file, expected in selection['tool_sha256'].items()})
    scripts = {}
    for source in args.script:
        source = source.resolve(strict=True)
        if source.suffix != '.py' or not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.py', source.name) or source.name in scripts:
            raise ValueError('Require unique simple .py script names')
        scripts[source.name] = source
        inputs[source] = digest(source)
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    (output / 'tmp').mkdir()
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'LOCAL_TARGET_BUILD_NO_DEVICE',
        'sha256': {str(p): sha for p, sha in inputs.items()}}, indent=2) + '\n')
    environment = os.environ.copy()
    environment.update(TEMP=str(output / 'tmp'), TMP=str(output / 'tmp'), TMPDIR=str(output / 'tmp'),
                       PYTHONDONTWRITEBYTECODE='1')
    (output / 'ENVIRONMENT.json').write_text(json.dumps({key: environment[key] for key in
        ('TEMP', 'TMP', 'TMPDIR', 'PYTHONDONTWRITEBYTECODE')}, indent=2) + '\n')
    bundle = output / 'package'
    bundle.mkdir()
    for file in list(base_files) + [base_manifest]:
        destination = bundle / file.relative_to(base)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(file, destination)
    (bundle / 'scripts').mkdir()
    for name, source in scripts.items():
        shutil.copyfile(source, bundle / 'scripts' / name)
    shutil.copyfile(bridge, bundle / 'lib' / bridge.name)
    owned_python = bundle / 'lib/libpython3.12.so.1.0'
    owned_python.write_bytes(owned_python_bytes)
    executable = bundle / 'actionbag_python'
    common = [compiler, '--target=aarch64-linux-ohos', '--sysroot=' + str(sdk / 'sysroot'),
              '-std=c++17', '-O2', '-fPIE', '-pie', '-stdlib=libc++', '-nostdlib++',
              '-Wall', '-Wextra', '-Werror', '-DFASTDDS_DSOFTBUS_ACTIONBAG_IDENTITY=1',
              '-I' + str(dev / 'usr/include/python3.12'), '-I' + str(here.parent),
              '-I' + str(REPO / 'examples/ros2_transport')]
    libraries = [owned_python, bundle / 'lib' / bridge.name, bundle / 'lib/libfastrtps.so.2.14',
                 bundle / 'lib/libfastcdr.so.2', bundle / 'lib/libc++_shared.so']
    record(output, 'launcher-link', common + sources + libraries + ['-ldl', '-pthread',
        '-Wl,--allow-shlib-undefined', '-Wl,--export-dynamic',
        '-Wl,-rpath-link,' + str(bundle / 'lib'), '-Wl,-Map,' + str(output / 'launcher.map'),
        '-o', executable], environment)
    for label, file in [('launcher', executable), ('token-bridge', bridge), ('selected-fastdds', bundle / 'lib/libfastrtps.so.2.14'),
                        ('canonical-python', libpython), ('owned-python', owned_python)]:
        record(output, label + '-elf', [readelf, '-h', '-d', '--dyn-syms', '--wide', file], environment)
    def python_elf(label):
        text = next((output / 'commands').glob('*-' + label + '-elf.stdout')).read_text(encoding='utf-8')
        # The dynamic-tag label text alone differs; every remaining readelf line must match.
        return '\n'.join(line for line in text.splitlines() if '(FLAGS_1)' not in line)
    if python_elf('canonical-python') != python_elf('owned-python') or digest(owned_python) != OWNED_PYTHON_SHA:
        raise ValueError('Python provider ABI/exports/dynamic/layout inspection differs')
    (output / 'PYTHON_PROVIDER.json').write_text(json.dumps(python_provider, indent=2) + '\n')
    run = '''#!/bin/sh
set -eu
HERE=$(CDPATH= cd -P "${0%/*}" && pwd)
unset PYTHONHOME PYTHONPATH PYTHONUSERBASE LD_PRELOAD
unset FASTRTPS_DEFAULT_PROFILES_FILE FASTDDS_DEFAULT_PROFILES_FILE CYCLONEDDS_URI
export AMENT_PREFIX_PATH="$HERE"
export LD_LIBRARY_PATH="$HERE/lib:/data/python312-rk3588a-verify-c646982b346f/usr/lib"
export RMW_IMPLEMENTATION=rmw_fastrtps_cpp
export RCUTILS_COLORIZED_OUTPUT=0
exec "$HERE/actionbag_python" "$@"
'''
    (bundle / 'run-token-first').write_bytes(run.encode())
    # The inherited R24 run remains an ABI-only diagnostic, not this entrypoint.
    files = {p.relative_to(bundle).as_posix(): digest(p) for p in sorted(bundle.rglob('*')) if p.is_file()}
    (bundle / 'LAUNCHER_PACKAGE.json').write_text(json.dumps({'schema': 1,
        'scope': 'TOKEN_FIRST_PYTHON_BUILDS_NOT_DEVICE_TESTED', 'identity': NAME,
        'base_package_sha256': BASE_SHA, 'bridge_sha256': BRIDGE_SHA,
        'python_provider': python_provider,
        'participant_lifecycles_per_process': 1, 'daemon_supported': False,
        'files': files}, indent=2) + '\n')
    if any(digest(file) != expected for file, expected in inputs.items()):
        raise ValueError('Build input changed during execution')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS', 'device_tested': False,
        'communication_verified': False, 'identity': NAME,
        'artifacts': {str(executable): digest(executable),
                      str(owned_python): digest(owned_python),
                      str(output / 'PYTHON_PROVIDER.json'): digest(output / 'PYTHON_PROVIDER.json'),
                      str(bundle / 'LAUNCHER_PACKAGE.json'): digest(bundle / 'LAUNCHER_PACKAGE.json')},
        'input_manifest_sha256': digest(output / 'INPUTS.json'),
        'entry': 'run-token-first ros2 [args] | --script /owned/package/scripts/name.py [args]',
        'provision_or_reload': 'NEVER', 'ros2_action_and_bag_plugins': 'NOT_ADDED'}, indent=2) + '\n')


if __name__ == '__main__':
    actionbag_python_build_main()
