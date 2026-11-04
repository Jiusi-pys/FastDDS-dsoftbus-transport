"""Build only the fixed ActionBag peer-directory query; no device operations."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]


def actionbag_peer_build_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', required=True, type=Path)
    parser.add_argument('--identity-build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root, identity_build, output = args.inputs_root.resolve(), args.identity_build.resolve(), args.output.resolve()
    if not output.is_relative_to(REPO / 'build') or output == REPO / 'build' or output.exists():
        raise ValueError('Require a fresh contained W3 build directory')
    if not identity_build.is_relative_to(REPO / 'build'):
        raise ValueError('Require the separately qualified W3 identity build')
    spec = importlib.util.spec_from_file_location('owned_build_recorder', Path(__file__).with_name('project_python.py'))
    recorder = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recorder)
    digest, record = recorder.project_python_digest, recorder.project_python_record
    evidence = root / 'build/r25-native-actionbag'
    freeze = evidence / 'implementation/FREEZE.json'
    for row in json.loads(freeze.read_text())['files']:
        if digest(REPO / row['path']) != row['sha256']:
            raise ValueError('Qualified identity source changed: ' + row['path'])
    initial = evidence / 'initial-test/RESULT.json'
    result = json.loads(initial.read_text())
    if result['initial_test_gate'] != 'PASS':
        raise ValueError('Identity independent initial test has not passed')
    bridge = identity_build / 'libmdds_actionbag_token_bridge.so'
    if digest(bridge) != 'd7e503a01c8d5d86fd49858bb7e375f49e71585081d87f450cf0521f7611ee57' or result['artifacts'].get(str(bridge)) != digest(bridge):
        raise ValueError('Use the exact independently qualified fixed-name bridge')
    platform_proof = evidence / 'SOFTBUS_PLATFORM_PREFLIGHT.json'
    # Preserve the current target proof verbatim as an input; device execution
    # must recheck the actual loaded provider, not treat a local build as proof.
    selection_file = root / 'build/ros2-target/r2/BUILD_SELECTION.json'
    selection = json.loads(selection_file.read_text())
    sdk = Path(selection['sdk'])
    compiler = sdk / 'llvm/bin/clang++.exe'
    app_cxx = sdk / 'llvm/lib/aarch64-linux-ohos/libc++_shared.so'
    if digest(compiler) != selection['tool_sha256'][str(compiler)]:
        raise ValueError('Selected SDK compiler changed')
    public = root / 'communication_dsoftbus/interfaces/kits'
    sources = [Path(__file__).with_name('actionbag_peer_query.cpp'),
        Path(__file__).with_name('identity') / 'token_first_entry.cpp',
        REPO / 'probes/softbus_record_contract.cpp', REPO / 'probes/softbus_probe_profile.cpp']
    inputs = sources + [Path(__file__), Path(__file__).with_name('project_python.py'), bridge,
        compiler, app_cxx, freeze, initial, platform_proof, selection_file,
        REPO / 'probes/softbus_record_contract.hpp', REPO / 'probes/softbus_probe_profile.hpp']
    inputs += list(Path(__file__).with_name('identity').glob('*.h')) + list(Path(__file__).with_name('identity').glob('*.hpp'))
    for folder in ('bus_center', 'common'):
        inputs += [path for path in (public / folder).rglob('*.h') if path.is_file()]
    hashes = {str(path): digest(path) for path in inputs}
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    temporary = output / 'tmp'
    temporary.mkdir()
    environment = os.environ.copy()
    environment.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary), PYTHONDONTWRITEBYTECODE='1')
    (output / 'INPUTS.json').write_text(json.dumps({'scope': 'LOCAL_QUERY_BUILD_NO_DEVICE', 'sha256': hashes}, indent=2) + '\n')
    common = [compiler, '--target=aarch64-linux-ohos',
        '--sysroot=' + str(sdk / 'sysroot'), '-std=c++17', '-O2', '-fPIE', '-Wall', '-Wextra', '-Werror',
        '-I' + str(REPO / 'probes'), '-I' + str(public / 'bus_center'), '-I' + str(public / 'common')]
    objects = []
    for number, source in enumerate(sources):
        obj = output / (str(number) + '.o')
        record(output, 'compile-' + source.stem, common + ['-MD', '-MF', str(obj) + '.d', '-c', source, '-o', obj], environment)
        objects.append(obj)
    artifact = output / 'actionbag_peer_query'
    record(output, 'link-query', common + ['-nostdlib++', *objects, bridge, app_cxx, '-ldl', '-pthread', '-pie',
        '-Wl,--allow-shlib-undefined', '-Wl,-rpath,$ORIGIN', '-Wl,-Map,' + str(output / 'query.map'), '-o', artifact], environment)
    record(output, 'query-elf', [sdk / 'llvm/bin/llvm-readelf.exe', '-h', '-d', '--dyn-syms', artifact], environment)
    if any(digest(path) != expected for path, expected in hashes.items()):
        raise ValueError('Input changed during query build')
    (output / 'BUILD_RESULT.json').write_text(json.dumps({'status': 'BUILDS', 'device_tested': False,
        'artifacts': {str(artifact): digest(artifact), str(bridge): digest(bridge)},
        'external_softbus': '/system/lib64/platformsdk/libsoftbus_client.z.so',
        'external_softbus_sha256': '9b431d2976616becbbe5b3b18f3c9507ca42f7a15c965c9aa86457f4ff0e6087',
        'app_cxx_candidate': {'path': str(app_cxx), 'sha256': digest(app_cxx)},
        'input_manifest_sha256': digest(output / 'INPUTS.json'),
        'socket_calls': 0, 'provisioning_calls': 0}, indent=2) + '\n')


if __name__ == '__main__':
    actionbag_peer_build_main()
