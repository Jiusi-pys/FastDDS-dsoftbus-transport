"""Derive a record/play candidate with pinned String C++ typesupport.

All selected native files and wheel RECORDs stay byte-identical. Historical
no-Node ABI evidence is a prerequisite, never qualification of this candidate.
Reinspect the combined ELF closure; no native build or device operation occurs.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
ROOT = REPO.parents[2]
HELPER = Path(__file__).with_name('compose_actionbag_bag_runtime.py')
HELPER_SHA = '2a984e347094db03f80dbd8c20c695d5bf937f922a24fd894b54b5578132e72a'
if hashlib.sha256(HELPER.read_bytes()).hexdigest() != HELPER_SHA:
    raise ValueError('Selected composition helper changed')
_spec = importlib.util.spec_from_file_location('recordplay_frozen_composer', HELPER)
HELPERS = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(HELPERS)
BASE, SHA, LOAD = HELPERS.BASE, HELPERS.SHA, HELPERS.LOAD
PARENT = REPO / 'build/r25-native-actionbag/bag-runtime-abi-initial-test-build'
EVIDENCE = ROOT / 'build/r25-native-actionbag'
PROBE = Path(__file__).with_name('bag_recordplay_probe.py')
PROBE_SHA = '436bd0692773360fb9f69370a3aa46388e30403a8221237786c1010b413ab678'
MEMBER = 'scripts/bag_recordplay_probe.py'
MANIFESTS = {'COMPOSITION.json', 'LAUNCHER_PACKAGE.json'}
TYPESUPPORT = {
    'lib/libstd_msgs__rosidl_typesupport_cpp.so': 'aa2bd3ada365934933317f8be6bb255b20433bcde888ea8bbff3e0b6ab2e63c3',
    'lib/libstd_msgs__rosidl_typesupport_fastrtps_cpp.so': '790e5094dc127d9bf479f170e39c265b7fc8aeaf10ad754666c9cfbdad0c2671',
    'lib/libstd_msgs__rosidl_typesupport_introspection_cpp.so': 'abb1c320c0b06d2c87074e9b90d06390f8f53c718b3197e0712d58346957c78c',
}
READELF_SHA = '7a9045f22b922c3a97f97e35d95956ba993e8abeda615a2e752182c3401f7ef0'
INDEX_PREFIX = 'share/ament_index/resource_index/rosidl_interfaces/'
INDEX_PINS = {
    'action_msgs': ('c185eb6bea98d502726b82b8b1dea8ed4428b6565b67da278beebcdea3fcf266',
        '7674db7af2fbe648e14cdc7b03be2cf205ac0c0017d82e03ca07b9b7e21e2bb5'),
    'builtin_interfaces': ('e7bb57f969e0c5865b7067d7efc7b1140d82616bffda34a484dd5bb8933439e4',
        '757000cba4c9cd227b89d6a6db2a475dbbb5147048cdd934ed873608bc38f9db'),
    'composition_interfaces': ('80efd6c17f4b3406097dd4026c14360c6c76b27f20b402d3849b731a8465326d',
        'e8525c28241bc8e479ddbb7610997694c0bab187718d6a7e4ea376d752bf7a61'),
    'example_interfaces': ('f00589227d537a013dc941416a220e70ae918f9956f21579462a329f15d46549',
        '74ef1006b73087c0dca6b0be95801e1955672837af7ae76671a6599140b4c3b0'),
    'lifecycle_msgs': ('1151752ae9436824c656ab4c97bf3c0b0165f9eadca6e57ff0fc8b05b3631e63',
        '2b0adde352a52b6a039b215f207f2122190fe3db1765a0a975e2af508e6f9a3c'),
    'rcl_interfaces': ('7d6bbca4386ce5eeed417bae98a3f17b232c4138f948b4f7d090d794b94c9e7e',
        'd40af2692e9a158122a5aa8ca792519750927499e6e849897e6359466772918a'),
    'rosbag2_interfaces': ('b9d12669ccc66f3f47f9cdb4189a0b37d3c8f31d7b25dddde3407ea6f0ec94ad',
        'f9c6ff6eedc0afaf8575248bc38096f4dd6a56debc470db1930695fed9def01e'),
    'rosgraph_msgs': ('510e9f965982668d2e34ef3e622d7e163ac168dcb6ead27d0fee22bdae6027e5',
        '708a145c53f72a3ff35031987760749e7ae8de215b7b73cf14b13af55df2075a'),
    'service_msgs': ('e0258bc318e50ce20243aa11373430d9aefd5c0cb00111f1fe9b34f2c5c6c1ab',
        'f3e321280709bc64aa64a71fa6d5d2218b1b1e73a690686905123d7e786722e8'),
    'std_msgs': ('f5a583299fcb26a806db4ffceaadec7cdc3889eacaf9e1eecf3a0728a52cc070',
        'fd01977513492eafd6c057b934dc90a2e9b6aa31e28e265cf66159ad1d274560'),
    'type_description_interfaces': ('66031ed97635217ef3fae76ede78c168660d1713e0c7ff0a194c6d6630820868',
        '833fcbd5ee7ad006d8478e11c1d6806e6fdc667fd2bd3a0977992f1e18fd1cc5'),
    'unique_identifier_msgs': ('c13b5697d24017eef6d9abcd509f9fb63ed2d05a6e266aaecf989f93c2559d54',
        '11361ea56679f0dfef70ae114b2d0d555c6d56c63ccc64a7ba23a272a63c3964'),
}
PINS = {
    'runtime': (PARENT / 'RUNTIME_PACKAGE.json', '461f56442cd3a0024fd0248b240ed44aa18d53235fd524ffdae70be1c5a2860d'),
    'abi_result': (EVIDENCE / 'bag-abi4-window/FINAL_TESTER_RESULT.json', '7b190dc63b2d708f0860629c57cd9fef7176a454a9cc84530a55499af1234f97'),
    'abi_seal': (EVIDENCE / 'bag-abi4-window/FINAL_TESTER_SEAL.json', 'f84893715e6dca0a5c2e1232b84a99ce17b4b873e076109ea7127ea56586679d'),
    'probe_final': (EVIDENCE / 'bag-recordplay-probe-final-test/RESULT.json', '89e5a8645c3ccd516acf49807a4f1de6abe7efdce8e8e39b28c40819643aa507'),
}


def recordplay_parent():
    """Read actual immutable parent inputs before creating an output directory."""
    if REPO != ROOT / 'build/r15-lifecycle/publish-worktree':
        raise ValueError('Require the selected project and W3 checkout')
    inputs, reports = {str(HELPER): HELPER_SHA, str(HELPERS.BASE_HELPER): HELPERS.BASE_HELPER_SHA,
                       str(PROBE): PROBE_SHA, str(Path(__file__)): SHA(__file__)}, {}
    for name, (source, digest) in PINS.items():
        path = BASE.runtime_path(source, ROOT)
        if SHA(path) != digest:
            raise ValueError('Selected prerequisite changed: ' + name)
        reports[name], inputs[str(path)] = LOAD(path), digest
    if any(SHA(path) != digest for path, digest in inputs.items()):
        raise ValueError('Selected source changed')
    runtime = reports['runtime']
    parent = BASE.runtime_path(PARENT / 'package', ROOT)
    before = BASE.runtime_inventory(parent)
    if before != runtime['package_inventory'] or len(before) != 3230:
        raise ValueError('Selected ABI package inventory changed')
    manifest, composition = LOAD(parent / 'LAUNCHER_PACKAGE.json'), LOAD(parent / 'COMPOSITION.json')
    if (manifest['files'] != {k: v for k, v in before.items() if k != 'LAUNCHER_PACKAGE.json'} or
            before['LAUNCHER_PACKAGE.json'] != runtime['launcher_manifest_sha256']):
        raise ValueError('Parent manifest differs from the actual package')
    for name, digest in runtime['report_sha256'].items():
        BASE.runtime_name(name)
        path = BASE.runtime_path(PARENT / name, PARENT)
        if SHA(path) != digest:
            raise ValueError('Parent report changed: ' + name)
        inputs[str(path)] = digest
    abi = reports['abi_result']
    if (abi.get('status') != 'PASS' or abi.get('scope') != 'TARGET_NO_NODE_BAG_ABI_ONLY' or
            abi.get('run') != 'daf356bea873e640' or abi.get('communication_verified') is not False or
            abi.get('record_play') != 'NOT_RUN'):
        raise ValueError('Historical ABI prerequisite has the wrong scope')
    members = reports['abi_seal']['files']
    seen = set()
    if len(members) != 753:
        raise ValueError('Historical ABI seal shape differs')
    for row in members:
        path = BASE.runtime_path(row['path'], ROOT)
        if path in seen or path.stat().st_size != row['bytes'] or SHA(path) != row['sha256']:
            raise ValueError('Historical ABI sealed evidence changed')
        seen.add(path)
        inputs[str(path)] = row['sha256']
    closure = LOAD(PARENT / 'ELF_CLOSURE.json')
    records = LOAD(PARENT / 'ALL_RECORDS.json')
    if (len(closure['objects']) != 217 or records['validated_RECORDs'] != 21 or
            records['rows'] != 1440 or MEMBER in before):
        raise ValueError('Selected native/RECORD/application member shape differs')
    return inputs, reports, parent, before, manifest, composition, closure, records


def recordplay_rosidl_indexes(parent):
    """Normalize only pinned CRLF indexes; never rewrite message definitions."""
    directory = BASE.runtime_path(parent / INDEX_PREFIX, parent)
    if {path.name for path in directory.iterdir()} != set(INDEX_PINS):
        raise ValueError('Selected rosidl index member set changed')
    payloads, rows = {}, {}
    for package, (old_sha, new_sha) in INDEX_PINS.items():
        name = INDEX_PREFIX + package
        raw = BASE.runtime_path(parent / name, parent).read_bytes()
        normalized = raw.replace(b'\r\n', b'\n')
        if (hashlib.sha256(raw).hexdigest() != old_sha or
                hashlib.sha256(normalized).hexdigest() != new_sha or
                raw == normalized or b'\r' in normalized):
            raise ValueError('Selected rosidl index bytes changed: ' + name)
        references = normalized.decode('utf-8').split('\n')
        for reference in references:
            BASE.runtime_name(reference)
            path = BASE.runtime_path(parent / 'share' / package / reference, parent / 'share' / package)
            if not path.is_file():
                raise ValueError('Rosidl index reference is not a file: ' + reference)
        payloads[name] = normalized
        rows[name] = {'before_sha256': old_sha, 'after_sha256': new_sha,
                      'crlf_replacements': raw.count(b'\r\n'), 'references': len(references)}
    return payloads, {'status': 'PASS', 'operation': 'EXACT_PINNED_CRLF_TO_LF', 'indexes': rows}


def recordplay_typesupport():
    """Bind the three existing R24 dynamic roots and the inspection tool."""
    sources = {name: BASE.runtime_path(ROOT / 'build/ros2-target/r2/ros/install' / name, ROOT)
               for name in TYPESUPPORT}
    if any(SHA(path) != TYPESUPPORT[name] for name, path in sources.items()):
        raise ValueError('Selected String C++ typesupport changed')
    readelf = BASE.runtime_path(Path(os.environ['LOCALAPPDATA']) /
        'OpenHarmony/Sdk/23/native/llvm/bin/llvm-readelf.exe')
    if SHA(readelf) != READELF_SHA:
        raise ValueError('Selected readelf changed')
    return sources, readelf


def validate_recordplay_derivation(before, after, closure):
    """Allow only pinned additions, manifests and twelve exact index changes."""
    BASE.runtime_namespace(before)
    BASE.runtime_namespace(after)
    if (len(before) != 3230 or len(after) != 3234 or MEMBER in before or set(before) & TYPESUPPORT.keys() or
            set(after) != set(before) | {MEMBER} | TYPESUPPORT.keys() or after.get(MEMBER) != PROBE_SHA):
        raise ValueError('Record/play package member set differs')
    if any(after.get(name) != digest for name, digest in TYPESUPPORT.items()):
        raise ValueError('Selected String C++ typesupport derivation differs')
    indexes = {INDEX_PREFIX + name: pins for name, pins in INDEX_PINS.items()}
    if any(before.get(name) != pins[0] or after.get(name) != pins[1] for name, pins in indexes.items()):
        raise ValueError('Selected rosidl index derivation differs')
    if any(after[name] != digest for name, digest in before.items() if name not in MANIFESTS | indexes.keys()):
        raise ValueError('Inherited payload changed')
    if any(after[name] == before[name] for name in MANIFESTS):
        raise ValueError('Both manifests must describe the new derivation')
    if len(closure['objects']) != 217 or any(
            before.get(name) != row['sha256'] or after.get(name) != row['sha256']
            for name, row in closure['objects'].items()):
        raise ValueError('Inherited native ELF bytes changed')
    return {'status': 'PASS', 'inherited_payload_files': 3216, 'added': [MEMBER, *sorted(TYPESUPPORT)],
            'normalized_rosidl_indexes': sorted(indexes),
            'regenerated': sorted(MANIFESTS), 'native_elf_files_unchanged': 217,
            'new_native_build_commands': 0, 'new_readelf_commands': 220}


def compose_bag_recordplay_runtime(output):
    # A fresh direct child cannot be nested in any existing immutable artifact.
    # Check the lexical boundary before reading selected inputs or creating paths.
    output = Path(output).absolute()
    destination = REPO / 'build/r25-native-actionbag'
    if output.parent != destination or not re.fullmatch('bag-recordplay-[A-Za-z0-9_-]+', output.name):
        raise ValueError('Require a fresh direct bag-recordplay child of the selected artifact directory')
    output = BASE.runtime_path(output, destination, exists=False)
    if output.exists():
        raise ValueError('Require a fresh output; no replacement is permitted')
    inputs, reports, parent, before, manifest, composition, closure, records = recordplay_parent()
    index_payloads, index_report = recordplay_rosidl_indexes(parent)
    typesupport, readelf = recordplay_typesupport()
    inputs.update({str(path): TYPESUPPORT[name] for name, path in typesupport.items()})
    # Deployment input descriptors require project-owned files. SDK tooling is
    # build provenance, checked here and after inspection, not a deployed input.
    inspection_tool = {'path': str(readelf), 'sha256': READELF_SHA}
    for path in inputs:
        BASE.runtime_path(path, ROOT)
    output.mkdir(parents=True)
    bundle = output / 'package'
    save = lambda name, value: (output / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    try:
        for name, digest in before.items():
            if name in MANIFESTS:
                continue
            source = BASE.runtime_path(parent / name, parent)
            target = bundle / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            if SHA(source) != digest or SHA(target) != digest:
                raise ValueError('Inherited payload changed during copy')
        shutil.copyfile(PROBE, bundle / MEMBER)
        for name, payload in index_payloads.items():
            (bundle / name).write_bytes(payload)
        for name, source in typesupport.items():
            shutil.copyfile(source, bundle / name)
        helper = BASE.ActionbagRuntimeBuilder.__new__(BASE.ActionbagRuntimeBuilder)
        helper.bundle = bundle
        actual_records = helper.validate_installed_records()
        if actual_records != records:
            raise ValueError('Inherited RECORD coverage changed')
        save('ALL_RECORDS.json', actual_records)
        (output / 'commands').mkdir()
        combined = HELPERS.bag_elf_inventory(bundle, BASE.runtime_inventory(bundle), closure,
            composition['external_runtime'], readelf, output)
        if set(combined['objects']) != set(closure['objects']) | TYPESUPPORT.keys():
            raise ValueError('Combined ELF member set differs')
        save('ELF_CLOSURE.json', combined)
        parent_binding = {name: {'path': str(path), 'sha256': digest} for name, (path, digest) in PINS.items()}
        derived = {**composition, 'scope': 'BAG_RECORDPLAY_SCRIPT_AND_ROSIDL_INDEX_STATIC_DERIVATION',
            'parent_composition_sha256': before['COMPOSITION.json'],
            'recordplay': {'probe': {'member': MEMBER, 'sha256': PROBE_SHA},
                          'rosidl_index_normalization': index_report,
                          'dynamic_typesupport_roots': TYPESUPPORT,
                          'inspection_tool': inspection_tool,
                          'prerequisites': parent_binding, 'historical_abi_is_new_qualification': False},
            'qualification': None, 'target_execution': 'NOT_RUN',
            'deployment_eligibility': 'NOT_GRANTED_RECORDPLAY_REQUIRES_NEW_GATES'}
        (bundle / 'COMPOSITION.json').write_text(json.dumps(derived, indent=2) + '\n', encoding='utf-8')
        new_manifest = {**manifest, 'scope': 'BAG_RECORDPLAY_CANDIDATE_NOT_TARGET_VERIFIED',
                        'files': BASE.runtime_inventory(bundle)}
        (bundle / 'LAUNCHER_PACKAGE.json').write_text(json.dumps(new_manifest, indent=2) + '\n', encoding='utf-8')
        after = BASE.runtime_inventory(bundle)
        delta = validate_recordplay_derivation(before, after, closure)
        delta['rosidl_index_normalization'] = index_report
        delta['dynamic_typesupport_roots'] = TYPESUPPORT
        delta['inspection_tool'] = inspection_tool
        if (SHA(readelf) != READELF_SHA or BASE.runtime_inventory(parent) != before or
                any(SHA(path) != digest for path, digest in inputs.items())):
            raise ValueError('Selected input changed during composition; no automatic restoration')
        save('DERIVATION.json', delta)
        save('INPUTS.json', inputs)
        save('NO_MUTATION.json', {'status': 'PASS', 'parent_members': len(before), 'inputs': len(inputs)})
        save('RUNTIME_PACKAGE.json', {'schema': 4, 'utc': datetime.now(timezone.utc).isoformat(),
            'status': 'STRUCTURAL_PASS_NOT_RUNTIME_VERIFIED', 'scope': 'BAG_RECORDPLAY_STATIC_DERIVATION_ONLY',
            'package_inventory': after, 'launcher_manifest_sha256': after['LAUNCHER_PACKAGE.json'],
            'report_sha256': {name: SHA(output / name) for name in (
                'ALL_RECORDS.json', 'ELF_CLOSURE.json', 'DERIVATION.json', 'INPUTS.json', 'NO_MUTATION.json')},
            'source_sha256': SHA(__file__), 'probe_sha256': PROBE_SHA, 'prerequisites': parent_binding,
            'eligible_for_deployment': False, 'target_imported': False, 'device_tested': False,
            'communication_verified': False, 'production_verified': False, 'record_play': 'NOT_RUN',
            'native_elf_inspection': 'COMBINED_STATIC_CLOSURE_REINSPECTED',
            'new_native_build_commands': 0, 'new_readelf_commands': combined['readelf_commands'],
            'outstanding': ['Independent composition tests/review/final tests',
                'Record/play runner tests/review/final tests and fresh operational admission',
                'Real cross-board recording, normal close, sealed collection and replay']})
    except BaseException as error:
        save('FAILURE.json', {'status': 'FAIL', 'error': repr(error), 'device_executed': False})
        raise
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    raise SystemExit(compose_bag_recordplay_runtime(parser.parse_args().output))
