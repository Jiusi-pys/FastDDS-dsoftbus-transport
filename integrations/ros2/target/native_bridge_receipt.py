"""Readonly R37 native build binding and explicit one-member package derivation.

This does not build, admit a device run, or turn a build into an integration PASS.
Historical package manifests remain historical; NATIVE_REBUILD.json records the
new bridge, exact compiler dependencies and source binding explicitly.
"""
import hashlib
import json
from pathlib import Path
import re
import shutil

REPO = Path(__file__).resolve().parents[3]
ROOT = REPO.parents[2]
EVIDENCE = ROOT / 'build/r37-native-close'
PARENT = ROOT / 'build/r36-bounded-endurance/package-527b209a8be429f1'
PARENT_SHA = '9bb64dacaa3c9f5703a6c54d0d75d5217b0961df589f1c5ab56b598aaedc43fa'
BRIDGE = 'lib/libfastdds_dsoftbus_ros2_target_bridge.so'
BASELINE_NATIVE_SHA = '6378aeaec87bb59e230da105d38c4d199f345f6c3078834361bbf898d55be7ef'
PROOF = 'NATIVE_REBUILD.json'


def need(value, reason):
    if not value:
        raise ValueError(reason)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')


def checked_build(path, repo=REPO):
    path, repo = Path(path).resolve(), Path(repo).resolve()
    need(path.is_relative_to(EVIDENCE) and path.is_file(), 'R37 native build receipt location')
    value = read(path)
    need(set(value) == {'schema', 'scope', 'status', 'source_root', 'toolchain', 'artifact',
        'input_sha256', 'repo_sources', 'compile_units', 'receipts', 'returncodes'}, 'Exact native binding fields')
    need(type(value['schema']) is int and value['schema'] == 1 and
        value['scope'] == 'R37_SOURCE_BOUND_NATIVE_BRIDGE_BUILD' and value['status'] == 'PASS_BUILD_ONLY' and
        value['source_root'] == str(repo), 'Native build identity/scope')
    inputs = value['input_sha256']
    need(isinstance(inputs, dict) and len(inputs) >= 895, 'Complete native compiler/link dependency binding')
    for name, digest in inputs.items():
        need(Path(name).is_absolute() and re.fullmatch('[0-9a-f]{64}', digest) and sha(name) == digest,
             'Native input drift: ' + name)
    expected_sources = {Path(name).resolve().relative_to(repo).as_posix(): digest
                        for name, digest in inputs.items() if Path(name).resolve().is_relative_to(repo)}
    need(value['repo_sources'] == expected_sources, 'Repository compiler input membership mismatch')
    for name in ('integrations/ros2/target/NativeObservation.cpp', 'integrations/ros2/target/NativeObservation.hpp',
                 'integrations/ros2/target/TargetBridge.cpp', 'integrations/ros2/BridgeOwner.hpp'):
        need(value['repo_sources'].get(name) == sha(repo / name), 'Current native source not bound: ' + name)
    toolchain = value['toolchain']
    need(set(toolchain) == {'api_version', 'target', 'files'} and type(toolchain['api_version']) is int and
         toolchain['api_version'] == 23 and toolchain['target'] == 'aarch64-linux-ohos' and
         len(toolchain['files']) >= 4 and all(inputs.get(k) == v for k, v in toolchain['files'].items()),
         'SDK23 compiler/toolchain identity binding')
    codes = value['returncodes']
    need(set(codes) == {'configure', 'build', 'dependencies', 'readelf'} and
         all(type(v) is int and v == 0 for v in codes.values()), 'Native build command failed')
    need(set(value['receipts']) == set(codes), 'Native command receipt membership')
    for label, receipt in value['receipts'].items():
        need(set(receipt) == {'path', 'sha256'} and sha(receipt['path']) == receipt['sha256'],
             'Native command receipt drift: ' + label)
        command = read(receipt['path'])
        need(type(command.get('exit_code')) is int and command['exit_code'] == codes[label] and
             isinstance(command.get('argv'), list) and command['argv'], 'Native command receipt rejected: ' + label)
        for stream in ('stdout', 'stderr'):
            need(sha(command[stream]['path']) == command[stream]['sha256'], 'Native raw command output drift')
    units = value['compile_units']
    need(isinstance(units, list) and len(units) == 23 and
         len({u['source'] for u in units}) == len({u['object'] for u in units}) == 23,
         'All 23 native compilation units required')
    for unit in units:
        need(set(unit) == {'source', 'object', 'object_sha256', 'dependencies'} and
             unit['source'] in inputs and Path(unit['source']).resolve().is_relative_to(repo) and
             unit['dependencies'] and set(unit['dependencies']).issubset(inputs) and
             sha(unit['object']) == unit['object_sha256'], 'Native object/dependency drift')
    artifact = value['artifact']
    need(set(artifact) == {'path', 'sha256'} and sha(artifact['path']) == artifact['sha256'] and
         artifact['sha256'] != BASELINE_NATIVE_SHA, 'Fresh repaired native artifact required')
    header = Path(artifact['path']).read_bytes()[:64]
    need(len(header) == 64 and header[:6] == b'\x7fELF\x02\x01' and
         int.from_bytes(header[16:18], 'little') == 3 and int.from_bytes(header[18:20], 'little') == 183,
         'Actual ELF64 AArch64 shared object required')
    return value


def build_bindings(derivation):
    reference = read(derivation)['native_build']
    need(sha(reference['path']) == reference['sha256'], 'Native binding reference changed')
    build = checked_build(reference['path'])
    result = dict(build['input_sha256'])
    result[reference['path']] = reference['sha256']
    result[build['artifact']['path']] = build['artifact']['sha256']
    for unit in build['compile_units']:
        result[unit['object']] = unit['object_sha256']
    for receipt in build['receipts'].values():
        result[receipt['path']] = receipt['sha256']
        for stream in ('stdout', 'stderr'):
            value = read(receipt['path'])[stream]
            result[value['path']] = value['sha256']
    return result


def verify_locator_aggregation(text, run, side):
    rows = {}
    for prefix in ('ROS_V1_SPDP', 'ROS_V1_OBSERVATION', 'ROS_V1_AGGREGATION', 'ROS_CLOSE_COMPONENTS'):
        values = []
        for line in text.splitlines():
            if not line.startswith(prefix + ' '):
                continue
            pairs = [x.split('=', 1) for x in line[len(prefix) + 1:].split()]
            need(all(len(p) == 2 for p in pairs) and len({p[0] for p in pairs}) == len(pairs),
                 'Malformed native observation fields')
            row = dict(pairs)
            need(row.get('run') == run and row.get('side') == side, 'Native observation run/side mismatch')
            values.append(row)
        rows[prefix] = values
    need(rows['ROS_CLOSE_COMPONENTS'] == [{'schema': '1', 'run': run, 'side': side,
         'runtime_result': '0', 'observation_complete': '1'}], 'Runtime and observer close must both succeed')
    need(len(rows['ROS_V1_AGGREGATION']) == len(rows['ROS_V1_OBSERVATION']) == 1,
         'Exactly one locator aggregation/fault summary required')
    ledger, summary = rows['ROS_V1_AGGREGATION'][0], rows['ROS_V1_OBSERVATION'][0]
    need(ledger.get('schema') == '1' and ledger.get('mode') == 'EXACT_ORDERED_BATCH_IDENTITY' and
         ledger.get('slots') == '256' and summary.get('captured') == '1' and summary.get('faults') == '0',
         'Fixed-capacity locator observation incomplete/faulted')
    for field in ('invalid_frame', 'unsupported_frame', 'parser_capacity', 'advert_capacity',
                  'invalid_selection', 'selection_capacity', 'counter_capacity'):
        need(ledger.get(field) == '0', 'Locator failure: ' + field)
    adverts = rows['ROS_V1_SPDP']
    need(0 < len(adverts) == int(summary['adverts']) <= 256, 'Locator retained entry count mismatch')
    groups, total = {}, 0
    for item in adverts:
        for field in ('occurrences', 'observation', 'last_observation', 'sequence_high', 'sequence_low',
                      'last_sequence_high', 'last_sequence_low', 'batch_size'):
            need(re.fullmatch('[0-9]+', item.get(field, '')) is not None, 'Missing locator first/last/count evidence')
        count, first, last = (int(item[k]) for k in ('occurrences', 'observation', 'last_observation'))
        need(0 < count <= 0xffffffffffffffff and 0 < first <= last <= 0xffffffffffffffff,
             'Locator occurrence/observation bounds')
        need(all(int(item[k]) <= 0xffffffff for k in ('sequence_high', 'sequence_low',
             'last_sequence_high', 'last_sequence_low')), 'RTPS sequence word bounds')
        groups.setdefault(first, []).append(item)
        total += count
    repeated_batches, identities = 0, set()
    for values in groups.values():
        size = len(values)
        need(all(int(v['batch_size']) == size and v['occurrences'] == values[0]['occurrences'] and
                 v['last_observation'] == values[0]['last_observation'] for v in values),
             'Incomplete or incoherent retained locator batch')
        identity = tuple(tuple(v[k] for k in ('direction', 'socket', 'writer', 'pid', 'bytes',
                         'kind', 'address', 'port', 'data_offset')) for v in values)
        need(identity not in identities, 'Duplicate retained batch identity')
        identities.add(identity)
        repeated_batches += int(values[0]['occurrences']) - 1
    need(int(ledger['observed']) == total and int(ledger['repeated']) == total - len(adverts) and
         int(ledger['repeated_batches']) == repeated_batches, 'Locator aggregation conservation mismatch')
    return {'stored': len(adverts), 'observed': total, 'repeated': total - len(adverts),
            'distinct_batches': len(groups), 'repeated_batches': repeated_batches, 'normal_close': 'PASS'}


def compose_native_package(output, binding, inventory):
    output = Path(output).resolve()
    need(output.parent == EVIDENCE.resolve() and re.fullmatch('package-[0-9a-f]{16}', output.name) and
         not output.exists(), 'Fresh R37 package output required')
    build = checked_build(binding)
    need(sha(PARENT / 'DERIVATION.json') == PARENT_SHA, 'Frozen failed R36 package derivation changed')
    parent = read(PARENT / 'DERIVATION.json')['files']
    need(inventory(PARENT / 'package') == parent and parent[BRIDGE] == BASELINE_NATIVE_SHA,
         'Frozen parent package changed')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(PARENT / 'package', package)
    shutil.copyfile(build['artifact']['path'], package / BRIDGE)
    proof = {'schema': 1, 'scope': 'R37_EXPLICIT_NATIVE_REBUILD_NOT_DEVICE_ADMISSION',
             'historical_manifests': 'PACKAGE_AND_COMPOSITION_UNCHANGED_HISTORICAL_R25_INPUTS',
             'parent_sha256': PARENT_SHA, 'before_native_sha256': BASELINE_NATIVE_SHA,
             'build_binding': {'path': str(Path(binding).resolve()), 'sha256': sha(binding)}, 'build': build}
    write(package / PROOF, proof)
    launcher = read(package / 'LAUNCHER_PACKAGE.json')
    launcher['files'][BRIDGE] = sha(package / BRIDGE)
    launcher['files'][PROOF] = sha(package / PROOF)
    write(package / 'LAUNCHER_PACKAGE.json', launcher)
    result = {'scope': 'R37_NATIVE_BRIDGE_SOURCE_BOUND_DERIVATION', 'parent_sha256': PARENT_SHA,
              'native_build': proof['build_binding'], 'files': inventory(package), 'native_files_changed': 1,
              'device_tested': False, 'package': str(package)}
    write(output / 'DERIVATION.json', result)
    return result


def checked_native_package(derivation, inventory):
    derivation = Path(derivation).resolve()
    need(derivation.is_relative_to(EVIDENCE.resolve()), 'R37 native derivation location')
    report = read(derivation)
    package = Path(report['package']).resolve()
    need(package.is_relative_to(EVIDENCE.resolve()) and package == derivation.parent / 'package',
         'R37 package path binding')
    build_path = Path(report['native_build']['path'])
    need(report['native_build'] == {'path': str(build_path.resolve()), 'sha256': sha(build_path)},
         'Native build receipt changed')
    build = checked_build(build_path)
    files = inventory(package)
    need(report == {'scope': 'R37_NATIVE_BRIDGE_SOURCE_BOUND_DERIVATION', 'parent_sha256': PARENT_SHA,
        'native_build': {'path': str(build_path.resolve()), 'sha256': sha(build_path)}, 'files': files,
        'native_files_changed': 1, 'device_tested': False, 'package': str(package)}, 'Exact native derivation required')
    need(sha(PARENT / 'DERIVATION.json') == PARENT_SHA, 'Original R36 receipt changed')
    parent = read(PARENT / 'DERIVATION.json')['files']
    need(set(files) == set(parent) | {PROOF} and all(files[k] == v for k, v in parent.items()
        if k not in (BRIDGE, 'LAUNCHER_PACKAGE.json')) and files[BRIDGE] == build['artifact']['sha256'],
        'Only the rebuilt bridge and explicit inventories may change')
    expected_proof = {'schema': 1, 'scope': 'R37_EXPLICIT_NATIVE_REBUILD_NOT_DEVICE_ADMISSION',
        'historical_manifests': 'PACKAGE_AND_COMPOSITION_UNCHANGED_HISTORICAL_R25_INPUTS',
        'parent_sha256': PARENT_SHA, 'before_native_sha256': BASELINE_NATIVE_SHA,
        'build_binding': report['native_build'], 'build': build}
    need(read(package / PROOF) == expected_proof and read(package / 'LAUNCHER_PACKAGE.json')['files'] ==
         {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'}, 'Native proof/inventory mismatch')
    return package, files
