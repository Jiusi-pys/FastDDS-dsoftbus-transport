"""Compose an exact frozen launcher and reviewed CLI/types in a fresh runtime.

Local copies and ELF inspection only. Never deploys or executes target code.
Schema2 is structural only. Schema3 can admit separately qualified inputs to
bounded ordinary acceptance; it never grants a runtime or communication claim.
"""
import argparse
import ast
import base64
import csv
from datetime import datetime
import hashlib
import importlib.metadata
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import posixpath
import re
import shutil
import stat
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
BASE = 'b8cd4ff42c347544a6e7994a78b4c4983024ddf27cd6302a2ee1c35db281762f'
IDENTITY = 'mdds_actionbag_2e107f51cd7b40218b01437d1e5c4e16'
HELPER = 'f4c0891a2ec77e05ae71e170c4ec0e0e5f29c3c8e272025d1a8006dc1b30cbd8'
TARGET_BRIDGE_MEMBER = 'lib/libfastdds_dsoftbus_ros2_target_bridge.so'
TARGET_BRIDGE_SHA = '6378aeaec87bb59e230da105d38c4d199f345f6c3078834361bbf898d55be7ef'
TARGET_BRIDGE_NEEDED = ['libsoftbus_client.z.so', 'libfastrtps.so.2.14', 'libtinyxml2.so.6',
    'libfoonathan_memory-0.7.4.so', 'libfastcdr.so.2', 'libc++_shared.so', 'libc.so']
TARGET_BRIDGE_EXPORTS = ['FastddsDsoftbusHookVersion', 'FastddsDsoftbusCreate', 'FastddsDsoftbusClose',
    'FastddsDsoftbusAfterPublicDelete', 'FastddsDsoftbusCaptureStartup', 'FastddsDsoftbusCaptureNativePrefix']
REVIEWS = {
    'cli': '365ae63dea0b951f7a2d4f1733e4b7f911dbebfca68ed55386964530f67a5473',
    'types': 'a0b2390d49f84986049f6b60f1bb3ea36fc40bcbcc1dd69065077a3dfdb77567',
    'design': 'b12304671b58b716a8f22d33a7985e3a67f5e0e3971363383189345c7a33f2a0',
}
CORE = {
    'actionbag_python': '7301537db0022cb986ce5da3b2361e0a8d7e537b812d152dad2db1204c6802d6',
    'lib/libpython3.12.so.1.0': '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25',
    'lib/libmdds_actionbag_token_bridge.so': 'd7e503a01c8d5d86fd49858bb7e375f49e71585081d87f450cf0521f7611ee57',
    'lib/libfastrtps.so.2.14': '40261a300a35f1974381faeacf56988f3131540b18e06522e602dd5a610975fd',
    'lib/libfastcdr.so.2': '113f8ef51c667c29c729dafc5fa20d0038688c217cd5bea99d4f13e2661d6b00',
}
RPATH_NAMES = {
    'python/numpy/core/_multiarray_tests.cpython-312-aarch64-linux-ohos.so',
    'python/numpy/core/_multiarray_umath.cpython-312-aarch64-linux-ohos.so',
    'python/numpy/linalg/_umath_linalg.cpython-312-aarch64-linux-ohos.so',
}
PYTHON_BASELINE = '47193e17cf471888d1ec325447f8b89f16c022d310a477db9d3d4744eaef191a'
LAUNCHER_FREEZE = '189a1a336567b2a081c7f41350dd4f958951d4e6ea8d2f52100f0090da55af1b'
LAUNCHER_SOURCES = {'actionbag_python_entry.cpp': '77b8807d4e587592ba6942b20a21034c2cfb8b1f166aaa2bce2fbfb5520ee597',
                    'build_actionbag_python.py': 'd6574ad1f930e1469f6ba3a3d5dae0e302df1ae3ac1bc9fc0bd4a47adeaf27bc'}
SYSTEM_EVIDENCE = {
    'system_initial': ('4c321055d9f159a0eaf98bd9990a9518425a49724f941c9e7c8b16c12675fa50', 'a00c3d02f1e45ff259cff9516dca20a762e01dfaca09856033be308d00025fdc'),
    'system_addendum': ('7e6a378a5db316eddd6435a1cc6a7fe9c20bd2d6adc450f7e3d67f03b03e935d', '362914bbf716ee89385c7a6f83fcd931a0a49349fece7ef11f6075dd37490f9b'),
    'system_roots': ('39915e3b6461609aff376a46871e07e2685428114ea09843b92abd2f135bb6fa', '4957e3cb6659a7c5a405a1b64e09bbaa7a178a333daba3bb0993ab9b961e4f2d'),
}
RECORD_NAMES = ('python/argcomplete-3.1.4.dist-info/RECORD',
                'python/lark-1.1.9.dist-info/RECORD', 'python/packaging-24.0.dist-info/RECORD')
QUALIFICATION_KEYS = frozenset('''qualification_design qualification_design_result package_provider_addendum
launcher_initial launcher_initial_seal launcher_review launcher_post launcher_post_seal
launcher_target launcher_target_seal launcher_target_plan launcher_target_review
ordinary_freeze ordinary_initial ordinary_initial_seal ordinary_review ordinary_final ordinary_final_seal
cli_initial_seal cli_final cli_final_seal types_initial_seal types_final types_final_seal
composer_freeze composer_initial composer_initial_seal composer_review composer_final composer_final_seal
python_initial python_initial_seal python_addendum python_addendum_seal external_edges'''.split())
OBSERVED_PROOFS = {
    'observed_contract': '542db8f9ef5b0109359836b8afc7594a18da960ec6a8dbee9cade0ba651334af',
    'observed_review': '783c51c3f801cd27ac75ec9989b9e3ad337d98bd9129ccc54df3fb332367825a',
    'observed_audit': 'e59c34736fbc511054f5778f66e0983dede07a0437d2e5a4fc33d6b0de8aeaf5',
    'observed_audit_seal': '563f59552b48fafab2407ed3c2a79dbfa22c587b6fc22813f0befa07f0ac74c6',
    'observed_capture_seal': '230aed7237f9e7b493ce5bf3bee73ec113f9101cafe4e3439ec808ff6a9a05a5',
}
OBSERVED_ROOTS = {
    **{'/data/python312-rk3588a-verify-c646982b346f/usr/lib/python3.12/lib-dynload/' + n + '.cpython-312-aarch64-linux-ohos.so':
       ('OBSERVED_PYTHON_EXTENSION_ROOT', 'PyInit_' + n) for n in ('_csv', '_heapq', '_queue')},
    '/system/lib64/platformsdk/libdfx_signalhandler.z.so': ('OBSERVED_SYSTEM_ROOT', None),
    '/system/lib64/platformsdk/libfwmark_client.z.so': ('OBSERVED_SYSTEM_ROOT', None),
}
HITRACE_PATH = '/system/lib64/chipset-sdk-sp/libhitrace_meter.so'
HITRACE_PROOFS = {
    'hitrace_capture_result': '9a50e10576303f470b234eac0492bdcfcc383ba6c4e1bfa0d729587508611b70',
    'hitrace_capture_seal': 'c87a253147596088cbb42a521691444f272471e74899b506d9461e1bc7706933',
    'hitrace_contract': '203cdcbc5f1029a2b297829a5409d3b1222648f1a984cd0ceaaa9f571c3771dd',
    'hitrace_review': '7865c8f8968a9d517478d509255420755e8cb50fdd8220ccc4fc18d4ef236803',
}
CLI_MODULE_PROOFS = {
    'cli_modules_contract': 'c6fa8032eab2a514877cda6f416b29c0a522bcc20b316381efee8dd698916ec7',
    'cli_modules_review': 'cfbbf719097451e2ef1b2f11f9032460013441e74a52e4d230d134bb89c7ec37',
    'cli_modules_audit': 'd72b4f5bcce76403f62ccba29219940a93da975c45b95c2312455c6395b4761c',
    'cli_modules_audit_seal': '394040acd6e424c637e31518f636ee8e566a9091b50e900ea44801970b32ae63',
    'cli_modules_capture_seal': '4ff75ca91e6b7ee0bb245b62864dbe8d04a810dce5ad0b16438c1b6f1d3a7a13',
}
CLI_MODULE_SYMBOLS = {'/data/python312-rk3588a-verify-c646982b346f/usr/lib/python3.12/lib-dynload/' +
    n + '.cpython-312-aarch64-linux-ohos.so': 'PyInit_' + n for n in ('pyexpat', '_decimal', 'resource')}
ARTIFACT_KEYS = frozenset('''cli_review cli_report cli_binding types_review types_report types_binding
design_review design_report design_binding cli_test types_test cli_source types_source cli_freeze types_freeze
cli_build types_build cli_overlay types_overlay cli_composition cli_dependencies cli_no_mutation types_wire
design_inventory native_baseline launcher_build launcher_manifest launcher_provider launcher_freeze provider_decision
canonical_python system_initial system_initial_seal system_addendum system_addendum_seal system_roots system_roots_seal
external_alias_A external_alias_B'''.split())
ROSGRAPH_ARTIFACT_KEYS = frozenset('rosgraph_source rosgraph_build rosgraph_overlay rosgraph_native rosgraph_wire'.split())
DEVICES = {'A': '3e01ff55454d202020104033bf453b00', 'B': '3e01ff55454d202020104433991c3b00'}
QUALIFICATION_CONTRACTS = {
    'qualification_design': '173911172ddf24fe8fa50a1f3be26aca0be8b7d08bddc3f53433db800389a8b6',
    'qualification_design_result': '01131441cd78e34ec116358234287b073b5fab26b35c0fe0f64db3e7bdaa5205',
    'package_provider_addendum': 'ddaaa95b902a322439af7a1e51552bd27ba5fda6efa9df405faf250d4a446b32',
    'python_initial': '4189255ac9a195538c7d4526567739729dc56e3beef7ee675a1d03ded5361258',
    'python_initial_seal': '1481574f7ce7dc4ec1604204139746d9484bfe799715ca6e45ad5a5a863c49f2',
    'python_addendum': 'a0060f65108744b25914d2e7f220fb86b5de8d4653f32fe1414d8cea0efde519',
    'python_addendum_seal': 'e48763ebac76070089515fa2e9673f63207efb91730bba8178158dbe2f943cbe',
    'cli_final': '48a11cdc76ebcc6bd396c47bd0bc217099095a3ad4c90dbc1a6671c7c5aa7c65',
    'cli_final_seal': '73d4b137d7cf8aab62f43222b3db8494040b2fd563070e8d43c8395c70a9edca',
    'types_final': 'ff1cc4e3832fe22e3570b2f8d5609fe6ad9ed360aef3c6ff74f754c7a36e0ffa',
    'types_final_seal': '67fc0becfc86d42ab4c36f60d4004e0c293dfb40f2677b22beadb698eeb2c812',
}

def runtime_sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def runtime_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate JSON key: ' + key)
            result[key] = value
        return result
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique)


def runtime_name(name):
    if not isinstance(name, str) or not name or '\\' in name or ':' in name or any(ord(c) < 32 or ord(c) == 127 for c in name):
        raise ValueError('Unsafe runtime name: ' + repr(name))
    parts = name.split('/')
    for part in parts:
        if part in ('', '.', '..') or part.endswith((' ', '.')) or re.search(r'[<>"|?*]', part):
            raise ValueError('Noncanonical runtime name: ' + name)
        if re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])', part.split('.')[0], re.I):
            raise ValueError('Windows reserved component: ' + name)
    if PurePosixPath(name).is_absolute():
        raise ValueError('Absolute runtime name')
    return name


def runtime_namespace(names):
    seen = {}
    for name in names:
        folded = runtime_name(name).casefold()
        if folded in seen:
            raise ValueError('Runtime case alias: ' + name)
        seen[folded] = name
    for folded, name in seen.items():
        if any('/'.join(folded.split('/')[:i]) in seen for i in range(1, len(folded.split('/')))):
            raise ValueError('File/directory collision: ' + name)


def runtime_path(path, root=None, exists=True):
    path = Path(os.path.abspath(path))
    for part in [*reversed(path.parents), path]:
        if part.exists() or part.is_symlink():
            info = part.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise ValueError('Symlink/reparse input or ancestor: ' + str(part))
    if exists and not path.exists():
        raise ValueError('Missing selected input: ' + str(path))
    path = path.resolve(strict=exists)
    if root is not None and (path == root or not path.is_relative_to(root)):
        raise ValueError('Selected source escapes root: ' + str(path))
    return path


def runtime_inventory(root):
    result = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        for path in sorted(directory.iterdir()):
            runtime_path(path, root)  # Reject reparse directories before traversal.
            if path.is_dir():
                pending.append(path)
            elif path.is_file():
                name = runtime_name(path.relative_to(root).as_posix())
                result[name] = runtime_sha(path)
            else:
                raise ValueError('Nonregular selected input: ' + str(path))
    runtime_namespace(result)
    return result


class ActionbagRuntimeBuilder:
    def __init__(self, args):
        self.root = runtime_path(args.inputs_root)
        if REPO != self.root / 'build/r15-lifecycle/publish-worktree':
            raise ValueError('Require the selected project inputs root and W3')
        self.output = runtime_path(args.output, exists=False)
        owned = runtime_path(REPO / 'build')
        if self.output == owned or not self.output.is_relative_to(owned) or self.output.exists():
            raise ValueError('Require fresh strict descendant of W3/build')
        runtime_name(self.output.relative_to(owned).as_posix())
        self.inputs = {}
        self.protected = {}
        self.selection_path = runtime_path(args.selection)
        self.selection = runtime_json(self.selection_path)
        self.qualified = self.selection.get('schema') == 3
        keys = {'schema', 'builds', 'artifacts', 'fixture', 'readelf'}
        if self.qualified:
            keys.add('qualification_inputs')
        if self.selection.get('schema') not in (2, 3) or set(self.selection) != keys:
            raise ValueError('Unknown composition selection schema')
        if self.qualified and set(self.selection['qualification_inputs']) not in (
                QUALIFICATION_KEYS, QUALIFICATION_KEYS | OBSERVED_PROOFS.keys(),
                QUALIFICATION_KEYS | OBSERVED_PROOFS.keys() | HITRACE_PROOFS.keys(),
                QUALIFICATION_KEYS | OBSERVED_PROOFS.keys() | HITRACE_PROOFS.keys() | CLI_MODULE_PROOFS.keys()):
            raise ValueError('Require the complete fixed qualification input set')
        fixture = self.selection['fixture']
        if fixture not in (None, 'actionbag_runtime_probe') or (self.qualified and fixture is None):
            raise ValueError('Qualified mode requires the explicitly selected fixture')
        expected_artifacts = ARTIFACT_KEYS | ({'fixture_source', 'fixture_freeze'} if fixture is not None else set())
        self.rosgraph_selected = bool(ROSGRAPH_ARTIFACT_KEYS & self.selection['artifacts'].keys())
        if self.rosgraph_selected:
            expected_artifacts |= ROSGRAPH_ARTIFACT_KEYS
        if not isinstance(self.selection['artifacts'], dict) or set(self.selection['artifacts']) != expected_artifacts:
            raise ValueError('Unknown/missing/inconsistent fixed artifact selection')
        tool = self.selection['readelf']
        for label, row in [*self.selection['artifacts'].items(), ('readelf', tool)]:
            if (not isinstance(row, dict) or set(row) != {'path', 'sha256'} or not isinstance(row['path'], str) or
                    not isinstance(row['sha256'], str) or not re.fullmatch('[0-9a-f]{64}', row['sha256'])):
                raise ValueError('Require exact descriptor fields: ' + label)
        self.inputs[str(self.selection_path)] = runtime_sha(self.selection_path)
        self.bundle = self.output / 'package'
        self.plan = {}
        self.origins = {}
        self.transitions = []

    def selected(self, label):
        row = self.selection['artifacts'][label]
        if set(row) != {'path', 'sha256'} or not re.fullmatch('[0-9a-f]{64}', row['sha256']):
            raise ValueError('Invalid selected artifact: ' + label)
        path = runtime_path(row['path'])
        if runtime_sha(path) != row['sha256']:
            raise ValueError('Selected artifact changed: ' + label)
        self.inputs[str(path)] = row['sha256']
        return path

    def bind(self, label):
        return runtime_json(self.selected(label))

    def proof(self, label, json=True):
        row = self.selection['qualification_inputs'][label]
        if set(row) != {'path', 'sha256'} or not re.fullmatch('[0-9a-f]{64}', row['sha256']):
            raise ValueError('Invalid qualification descriptor: ' + label)
        path = runtime_path(row['path'])
        if not path.is_relative_to(self.root / 'build/r25-native-actionbag'):
            raise ValueError('Qualification descriptor outside the explicit R25 evidence root')
        if runtime_sha(path) != row['sha256'] or (label in QUALIFICATION_CONTRACTS and row['sha256'] != QUALIFICATION_CONTRACTS[label]):
            raise ValueError('Qualification proof hash differs: ' + label)
        if path == self.output or path.is_relative_to(self.output):
            raise ValueError('Qualification proof cannot depend on this output')
        self.inputs[str(path)] = row['sha256']
        if not json:
            return path
        value = runtime_json(path)
        if not isinstance(value, dict) or value.get('synthetic') or value.get('private_host_control'):
            raise ValueError('Real qualified build rejects synthetic/private proof: ' + label)
        return value

    def proof_seal(self, label, members):
        seal = self.proof(label)
        rows = seal if label in ('types_initial_seal', 'cli_final_seal', 'types_final_seal') else seal['files']
        if isinstance(rows, dict):
            rows = [{'path': p, 'sha256': r if isinstance(r, str) else r['sha256'],
                     **({} if isinstance(r, str) or 'bytes' not in r else {'bytes': r['bytes']})} for p, r in rows.items()]
        if not isinstance(rows, list) or not rows or len(rows) > 20000:
            raise ValueError('Invalid bounded qualification seal')
        sealed = {}
        for row in rows:
            path = runtime_path(row['path'])
            if not any(path.is_relative_to(root) for root in (self.root / 'build/r25-native-actionbag', REPO / 'build/r25-native-actionbag')):
                raise ValueError('Qualification seal outside exact evidence/build roots')
            if str(path) in sealed or runtime_sha(path) != row['sha256'] or ('bytes' in row and path.stat().st_size != row['bytes']):
                raise ValueError('Duplicate/changed qualification seal member')
            sealed[str(path)] = row['sha256']
            self.inputs[str(path)] = row['sha256']
        for name in members:
            path = self.proof(name, json=False)
            if sealed.get(str(path)) != runtime_sha(path):
                raise ValueError('Qualification result missing from its seal: ' + name)
        return sealed

    def protect(self, path):
        if self.output.is_relative_to(path) or path.is_relative_to(self.output):
            raise ValueError('Output overlaps protected input')
        self.protected[str(path)] = runtime_inventory(path)

    def add(self, name, path, digest, origin):
        runtime_name(name)
        path = runtime_path(path)
        if runtime_sha(path) != digest:
            raise ValueError('Payload digest mismatch: ' + name)
        self.inputs[str(path)] = digest
        if name in self.plan and self.plan[name]['sha256'] != digest:
            if name not in RECORD_NAMES or origin != 'cli':
                raise ValueError('Unapproved changed collision: ' + name)
            candidate = self.design_inventory['record_relocation_candidates'][name]
            if (self.plan[name]['sha256'], digest) != (candidate['collision']['base'], candidate['collision']['overlay']):
                raise ValueError('Unapproved RECORD transition')
            if self.cli['source_map'][name].get('kind') != 'GENERATED_INSTALLED_RECORD':
                raise ValueError('Missing relocated RECORD provenance')
            self.transitions.append({'path': name, 'old_sha256': self.plan[name]['sha256'], 'new_sha256': digest, 'reason': 'EXACT_RELOCATED_INSTALLED_RECORD'})
        elif name in self.plan:
            self.origins[name].append({'origin': origin, 'source': str(path), 'sha256': digest, 'deduplicated': True})
            return
        self.plan[name] = {'source': path, 'sha256': digest}
        self.origins.setdefault(name, []).append({'origin': origin, 'source': str(path), 'sha256': digest})

    def collect_system(self):
        # These exact sealed receipts, not a prefix allowance, define all legal
        # system providers. They include the initial blockers and their addendum.
        sealed = {}
        results = {}
        roots = [self.selected(label).parent for label in SYSTEM_EVIDENCE]
        for label, (result_sha, seal_sha) in SYSTEM_EVIDENCE.items():
            result_path, seal_path = self.selected(label), self.selected(label + '_seal')
            if runtime_sha(result_path) != result_sha or runtime_sha(seal_path) != seal_sha:
                raise ValueError('Unselected system closure receipt')
            results[label] = runtime_json(result_path)
            for row in runtime_json(seal_path)['files']:
                path = runtime_path(row['path'])
                if not any(path.is_relative_to(root) and path != root for root in roots):
                    raise ValueError('System seal member outside exact evidence roots')
                if runtime_sha(path) != row['sha256'] or path.stat().st_size != row['bytes']:
                    raise ValueError('Sealed system evidence changed')
                if str(path) in sealed and sealed[str(path)] != row['sha256']:
                    raise ValueError('Conflicting system evidence seals')
                sealed[str(path)] = row['sha256']
                self.inputs[str(path)] = row['sha256']
            if sealed.get(str(result_path)) != result_sha:
                raise ValueError('System result absent from its sealed evidence')
        final = results['system_roots']
        if final['status'] != 'PASS_READONLY_ROOTS_CLOSURE' or final['blockers'] or final['unresolved_edges']:
            raise ValueError('System closure is incomplete')
        addendum = results['system_addendum']
        if addendum['status'] != 'PASS_READONLY_SYSTEM_CLOSURE' or addendum['blockers'] or addendum['unresolved_edges']:
            raise ValueError('System closure addendum is incomplete')
        mapping = final['mapping']
        if len(mapping) != 49 or set(final['root_reachable_names']) != set(mapping):
            raise ValueError('Exact system root reachability differs')
        providers = {}
        self.provider_origin_rows = {side: {} for side in DEVICES}
        for name, boards in mapping.items():
            runtime_name(name)
            if '/' in name or set(boards) != {'A', 'B'}:
                raise ValueError('Invalid exact system provider entry')
            a, b = boards['A'], boards['B']
            for field in ('sha256', 'actual_path', 'needed', 'soname'):
                if a[field] != b[field]:
                    raise ValueError('System boards differ: ' + name + ' ' + field)
            for side, row in boards.items():
                file = runtime_path(row['local_file'])
                if sealed.get(str(file)) != row['sha256'] or runtime_sha(file) != row['sha256'] or row['local_sha256'] != row['sha256']:
                    raise ValueError('Unsealed/mismatched actual system ELF: ' + name)
                for key, receipt in row.items():
                    if key.endswith('_receipt') and str(runtime_path(receipt)) not in sealed:
                        raise ValueError('Unsealed system transfer/resolve/ELF receipt')
                self.provider_origin_rows[side].setdefault(row['actual_path'], row)
            providers[name] = {'target_path': a['actual_path'], 'sha256': a['sha256'],
                'local_elf': a['local_file'], 'board_B_local_elf': b['local_file'],
                'needed': a['needed'], 'soname': a['soname'],
                'aliases_A': a.get('aliases', []), 'aliases_B': b.get('aliases', [])}
        for name, (path, digest) in final['roots'].items():
            if (providers[name]['target_path'], providers[name]['sha256']) != (path, digest):
                raise ValueError('Selected dynamic root differs')
        pending = {dep for row in providers.values() for dep in row['needed']} - set(providers)
        if pending or 'libpython3.12.so.1.0' in providers:
            raise ValueError('System closure has unknown edges or external Python provider')
        # The only extra name is the previously proved libc alias. Its exact
        # two-board receipts are checked separately; it never selects Python.
        providers['libc.musl-aarch64.so.1'] = dict(providers['libc.so'], alias_of='libc.so')
        self.external = {'providers': providers, 'transitive_status': 'BOUND_STATIC_DT_NEEDED_COMPLETE',
            'root_names': sorted(final['roots']), 'canonical_python': {
                'sha256': PYTHON_BASELINE, 'selected_for_load': False,
                'purpose': 'Immutable source/stdlib-prefix provenance only'},
            'runtime_namespace_and_all_dlopen': 'NOT_PROVEN'}
        self.system_sealed = sealed

    def receipt_index(self, descriptor):
        if (not isinstance(descriptor, dict) or set(descriptor) != {'path', 'sha256'} or
                not isinstance(descriptor['path'], str) or not isinstance(descriptor['sha256'], str) or
                not re.fullmatch('[0-9a-f]{64}', descriptor['sha256'])):
            raise ValueError('Invalid typed role descriptor')
        path = runtime_path(descriptor['path'], self.root / 'build/r25-native-actionbag')
        if path.is_relative_to(self.output) or runtime_sha(path) != descriptor['sha256']:
            raise ValueError('Changed or output-dependent typed role index')
        self.inputs[str(path)] = descriptor['sha256']
        value = runtime_json(path)
        if not isinstance(value, dict) or value.get('synthetic') or value.get('private_host_control'):
            raise ValueError('Real receipt role cannot use synthetic evidence')
        return value

    def receipt_command(self, descriptor, sealed):
        # Five existing immutable collectors use this one concrete command
        # format. Raw siblings are mandatory; an ELF/RESULT hash is no receipt.
        path = runtime_path(descriptor['path'], self.root / 'build/r25-native-actionbag')
        if set(descriptor) != {'path', 'sha256'} or sealed.get(str(path)) != descriptor['sha256']:
            raise ValueError('Command role requires an exact sealed receipt descriptor')
        data = self.receipt_index(descriptor)
        fields = {'argv', 'cwd', 'utc', 'elapsed', 'timeout_seconds', 'timed_out', 'host_exit', 'stdout_sha256', 'stderr_sha256'}
        if (set(data) != fields or type(data['host_exit']) is not int or data['host_exit'] != 0 or data['timed_out'] is not False or
                type(data['timeout_seconds']) is not int or not 0 < data['timeout_seconds'] <= 60 or
                type(data['elapsed']) not in (int, float) or not 0 <= data['elapsed'] <= data['timeout_seconds'] or
                datetime.fromisoformat(data['utc']).utcoffset() is None or not isinstance(data['argv'], list) or
                not data['argv'] or not all(isinstance(a, str) for a in data['argv'])):
            raise ValueError('Wrong type, failed or unbounded command receipt')
        raw = []
        for kind in ('stdout', 'stderr'):
            file = path.with_suffix('.' + kind)
            if sealed.get(str(file)) != data[kind + '_sha256'] or runtime_sha(file) != data[kind + '_sha256']:
                raise ValueError('Role raw output hash/seal mismatch')
            self.inputs[str(file)] = data[kind + '_sha256']
            raw.append(file.read_text(encoding='utf-8'))
        if raw[1] or re.search(r'^(?:Fail\b|Error\b|\[Fail\])', raw[0], re.I | re.M):
            raise ValueError('Successful role receipt has unexpected stderr')
        return data['argv'], raw[0]

    def connectivity_role(self, descriptor, side, sealed):
        role = self.receipt_index(descriptor)
        if (set(role) != {'schema', 'role', 'scope', 'side', 'device_alias', 'receipt'} or type(role['schema']) is not int or
                (role['schema'], role['role'], role['scope'], role['side'], role['device_alias']) !=
                (1, 'CONNECTED_TARGET_IDENTIFIER', 'CONNECTIVITY_ONLY_NOT_ATTESTATION', side, DEVICES[side])):
            raise ValueError('Identity role is not the selected physical connection identifier')
        argv, text = self.receipt_command(role['receipt'], sealed)
        hdc = self.readelf.parents[3] / 'toolchains/hdc.exe'
        if argv != [str(hdc), 'list', 'targets', '-v']:
            raise ValueError('Connectivity role is not an actual list-targets observation')
        rows = [line.split() for line in text.splitlines() if line.strip()]
        matches = [row for row in rows if row[0] == DEVICES[side]]
        if len(matches) != 1 or len(matches[0]) != 5 or matches[0][1:] != ['USB', 'Connected', 'localhost', 'hdc']:
            raise ValueError('Selected device has no unique actual Connected row')

    def provider_origin_role(self, descriptor, side, path, row, sealed):
        role = self.receipt_index(descriptor)
        if (row['side'], row['target'], row['actual_path']) != (side, DEVICES[side], path):
            raise ValueError('Selected raw provider belongs to another board or path')
        expected = {'schema': 1, 'role': 'PROVIDER_ORIGIN', 'scope': 'DISK_FILE_OBSERVATION_ONLY',
            'side': side, 'device_alias': DEVICES[side], 'canonical_absolute_path': path,
            'sha256': row['sha256'], 'size': row['size'], 'needed': row['needed'], 'soname': row['soname'],
            'local_elf': row['local_file']}
        if set(role) != set(expected) | {'receipts'} or type(role['schema']) is not int or any(role[k] != v for k, v in expected.items()):
            raise ValueError('Provider origin role has wrong board/path/hash/ELF identity')
        if path in getattr(self, 'hitrace_rows', {}).get(side, {}):
            if role != self.hitrace_contract['exact_origin_roles_to_derive'][side]:
                raise ValueError('HiTrace origin must bind the exact same-board four captured roles')
            return  # The complete capture/raw/attempt chain was checked before registration.
        if path in getattr(self, 'observed_rows', {}).get(side, {}):
            self.observed_origin(role, side, row, sealed)
            return
        source_receipts = {'resolve': row.get('resolution_receipt', row.get('stat_hash_receipt')),
            'receive': row['receive_receipt'], 'posthash': row['post_receive_hash_receipt'], 'readelf': row['readelf_receipt']}
        if set(role['receipts']) != set(source_receipts):
            raise ValueError('Provider requires the exact four actual observation roles')
        hdc = str(self.readelf.parents[3] / 'toolchains/hdc.exe')
        hash_line = row['sha256'] + '  ' + path
        for kind, source in source_receipts.items():
            descriptor = role['receipts'][kind]
            if descriptor != {'path': source, 'sha256': sealed.get(source)}:
                raise ValueError('Provider role references an unrelated or wrong-board receipt')
            argv, text = self.receipt_command(descriptor, sealed)
            if kind == 'readelf':
                if (argv != [str(self.readelf), '--file-header', '--dynamic', row['local_file']] or
                        not re.search(r'Machine:\s+AArch64\b', text) or not re.search(r'Class:\s+ELF64\b', text) or
                        re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', text) != row['needed'] or
                        re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', text) != row['soname'] or
                        runtime_sha(row['local_file']) != row['sha256']):
                    raise ValueError('Actual provider ELF receipt does not describe selected bytes')
            elif kind == 'receive':
                if argv != [hdc, '-t', DEVICES[side], 'file', 'recv', path, row['local_file']] or 'FileTransfer finish' not in text:
                    raise ValueError('Transfer role has wrong target/remote/local file')
            else:
                if (len(argv) != 5 or argv[:4] != [hdc, '-t', DEVICES[side], 'shell'] or
                        text.splitlines().count('R25_ELF_REMOTE_EXIT=0') != 1 or text.splitlines().count(hash_line) < 1 or
                        'sha256sum' not in argv[4]):
                    raise ValueError('Hash/resolve role has wrong target, remote exit or canonical hash')
                if kind == 'resolve':
                    candidate = row.get('candidate', path)
                    stat_line = 'STAT ' + ' '.join(str(row[k]) for k in ('mode', 'uid', 'gid', 'size', 'inode'))
                    blocks = [header + '\n' + stat_line + '\n' + hash_line for header in ('REAL ' + path, 'CANDIDATE ' + candidate + ' ' + path)]
                    if (sum(text.count(block) for block in blocks) != 1 or
                            'readlink -f' not in argv[4] or 'stat -Lc' not in argv[4] or candidate not in argv[4]):
                        raise ValueError('Resolve/stat role does not bind exact canonical path and file identity')
                elif path not in argv[4] or text.splitlines().count(hash_line) != 1:
                    raise ValueError('Posthash role does not name selected canonical file')

    def loader_launch_runner(self, descriptor):
        current = REPO / 'tests/ros2_target/run_actionbag_cli.py'
        if runtime_sha(current) == descriptor['sha256']:
            return self.loader_file(descriptor)
        baseline = '7bd0ea7fb6dd574c68966e4120dd0ade85fe782e2082bda5be69364773ba98ef'
        qualified = '3d348d26157311d5fd5d1f05248d4454ca238296ae8f0154936fac061a03c40e'
        original = self.root / 'build/r25-native-actionbag/ordinary-cli-cleanup-fix1-implementation/ORIGINAL_run_actionbag_cli.py'
        if (descriptor != {'path': str(current), 'sha256': baseline} or
                not getattr(self, 'cleanup_source_qualified', False) or runtime_sha(current) != qualified or runtime_sha(original) != baseline):
            raise ValueError('Loader source continuation is limited to the exact qualified cleanup repair')
        trees = [ast.parse(p.read_bytes()) for p in (original, current)]
        for tree in trees:
            classes = [n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'OrdinaryCliRunner']
            if len(classes) != 1 or {n.name for n in classes[0].body if isinstance(n, ast.FunctionDef) and n.name in ('publish', 'cleanup')} != {'publish', 'cleanup'}:
                raise ValueError('Loader source continuation requires the two exact cleanup methods')
            classes[0].body = [n for n in classes[0].body if not (isinstance(n, ast.FunctionDef) and n.name in ('publish', 'cleanup'))]
        if ast.dump(trees[0], include_attributes=False) != ast.dump(trees[1], include_attributes=False):
            raise ValueError('Cleanup continuation changed module/class/launch/environment AST')
        self.inputs[str(original)], self.inputs[str(current)] = baseline, qualified
        self.external['loader_source_continuation'] = {'scope': 'REVIEWED_CLEANUP_ONLY_LAUNCH_ENV_UNCHANGED',
            'original_contract_runner': descriptor, 'original_snapshot': {'path': str(original), 'sha256': baseline},
            'qualified_runner': {'path': str(current), 'sha256': qualified},
            'unchanged_ast_except': ['OrdinaryCliRunner.publish', 'OrdinaryCliRunner.cleanup'],
            'qualification': {k: self.selection['qualification_inputs']['ordinary_' + k] for k in ('freeze', 'initial', 'review', 'final')}}
        return current

    def loader_file(self, descriptor):
        # Only called with descriptors from the exact pinned interpretation or
        # its sealed capture. This is not an extensible proof/artifact channel.
        if (not isinstance(descriptor, dict) or set(descriptor) != {'path', 'sha256'} or
                not isinstance(descriptor['path'], str) or not isinstance(descriptor['sha256'], str) or
                not re.fullmatch('[0-9a-f]{64}', descriptor['sha256'])):
            raise ValueError('Invalid loader binding descriptor')
        path = runtime_path(descriptor['path'])
        if (not any(path.is_relative_to(root) for root in (self.root / 'build/r25-native-actionbag', REPO)) or
                path.is_relative_to(self.output) or runtime_sha(path) != descriptor['sha256']):
            raise ValueError('Loader input escaped, changed or depends on output')
        self.inputs[str(path)] = descriptor['sha256']
        return path

    def loader_capture_command(self, descriptor, side, sealed):
        path = self.loader_file(descriptor)
        if sealed.get(str(path)) != descriptor['sha256']:
            raise ValueError('Loader command is not in the exact capture seal')
        row = runtime_json(path)
        fields = {'argv', 'cwd', 'utc_end', 'elapsed_seconds', 'timeout_seconds', 'timed_out',
            'host_rc', 'remote_exit', 'marker', 'stdout', 'stderr'}
        if (set(row) != fields or row['cwd'] != str(self.root) or type(row['host_rc']) is not int or row['host_rc'] != 0 or
                row['timed_out'] is not False or type(row['timeout_seconds']) is not int or not 0 < row['timeout_seconds'] <= 30 or
                type(row['elapsed_seconds']) not in (int, float) or not 0 <= row['elapsed_seconds'] <= row['timeout_seconds'] or
                not isinstance(row['utc_end'], str) or datetime.fromisoformat(row['utc_end']).utcoffset() is None or
                datetime.fromisoformat(row['utc_end']).utcoffset().total_seconds() != 0):
            raise ValueError('Loader command failed or lacks the bounded actual capture format')
        raw = []
        for kind in ('stdout', 'stderr'):
            file = self.loader_file(row[kind])
            if file != path.with_suffix('.' + kind) or sealed.get(str(file)) != row[kind]['sha256']:
                raise ValueError('Loader command raw role/path/hash mismatch')
            raw.append(file.read_text(encoding='utf-8'))
        argv = row['argv']
        hdc = str(self.readelf.parents[3] / 'toolchains/hdc.exe')
        if (raw[1] or not isinstance(argv, list) or not all(isinstance(a, str) for a in argv) or
                re.search(r'^(?:Fail\b|Error\b|\[Fail\])', raw[0], re.I | re.M)):
            raise ValueError('Loader capture has unexpected error output or argv')
        if side is None:
            if argv != [hdc, 'list', 'targets', '-v'] or row['remote_exit'] is not None or row['marker'] is not None:
                raise ValueError('Loader connectivity role has wrong command')
        elif len(argv) == 5 and argv[:4] == [hdc, '-t', DEVICES[side], 'shell']:
            if (type(row['remote_exit']) is not int or row['remote_exit'] != 0 or not isinstance(row['marker'], str) or
                    not re.fullmatch('LOADER_CFG_' + side + '_[A-Z0-9]+_REMOTE_EXIT', row['marker']) or
                    re.findall('^' + re.escape(row['marker']) + '=(.*)$', raw[0], re.M) != ['0'] or
                    '/system/bin/env -i PATH=/system/bin:/system/xbin LC_ALL=C /system/bin/sh -c ' not in argv[4]):
                raise ValueError('Loader shell role has wrong target, environment or remote outcome')
        elif (len(argv) != 7 or argv[:5] != [hdc, '-t', DEVICES[side], 'file', 'recv'] or
                row['remote_exit'] is not None or row['marker'] is not None or
                not raw[0].startswith('FileTransfer finish, Size:3574, File count = 1, ')):
            raise ValueError('Loader receive role has wrong target or transfer outcome')
        return row, raw[0]

    def loader_configuration_role(self, descriptor, side, sealed):
        role = self.receipt_index(descriptor)
        fields = {'schema', 'kind', 'side', 'target', 'loader_sha256', 'disk_capture', 'capture_seal',
            'selection_contract', 'namespace_contract', 'namespace_review', 'scope'}
        loader_sha = 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86'
        if (set(role) != fields or type(role['schema']) is not int or
                (role['schema'], role['kind'], role['side'], role['target'], role['loader_sha256'], role['scope']) !=
                (1, 'LOADER_SEARCH_CONFIGURATION', side, DEVICES[side], loader_sha, 'STATIC_CONFIGURATION_ROLE_ONLY')):
            raise ValueError('Loader configuration requires its own exact typed board/loader receipt')
        evidence = self.root / 'build/r25-native-actionbag'
        analysis = evidence / 'loader-namespace-analysis'
        fixed = {'namespace_contract': (analysis / 'ADAPTER_CONTRACT.json', '17a729c77bf0864f49b624b841547be8a84c448b290201d2ba293e184a8cf310'),
            'namespace_review': (analysis / 'RESULT.json', '49ca156e7a547c3e6b8b8a2bb89d44f06da11a2fc02ec50c0d042702b073135e')}
        for key, (path, digest) in fixed.items():
            if role[key] != {'path': str(path), 'sha256': digest}:
                raise ValueError('Loader interpretation is not the exact independent contract/review')
        contract = runtime_json(self.loader_file(role['namespace_contract']))
        review = runtime_json(self.loader_file(role['namespace_review']))
        if (set(role) != set(contract['strict_receipt_keys']) or review['status'] != 'PASS' or review['blocking_findings'] or
                review['contract'] != role['namespace_contract'] or review['runtime_resolution'] != 'NOT_VERIFIED' or
                review['ordinary_acceptance'] != 'NOT_GRANTED' or review['device_release'] != 'NOT_GRANTED'):
            raise ValueError('Loader review grants a different role or runtime scope')
        self.loader_file(review['report'])
        integrity = runtime_json(self.loader_file({'path': str(analysis / 'INTEGRITY.json'),
            'sha256': 'cd5d8ef309f1f947124a79d4369221ec279b7473cb436297bc9596be4e7b91da'}))
        for row in integrity['files'] + integrity['inputs']:
            file = self.loader_file({k: row[k] for k in ('path', 'sha256')})
            if 'bytes' in row and file.stat().st_size != row['bytes']:
                raise ValueError('Loader analysis artifact size changed')
        # Rebind only these finite reviewed descriptor-bearing records. There
        # is no generic recursive JSON input walker or unreviewed path grammar.
        for key in ('selection_contract', 'prior_receipt_contract', 'interpretation', 'launch_source_contract', 'binary_literals', 'capture_result'):
            self.loader_file(contract[key])
        launch = runtime_json(self.loader_file(contract['launch_source_contract']))
        for key in ('runner', 'wrapper_builder'):
            if key == 'runner': self.loader_launch_runner(launch[key])
            else: self.loader_file(launch[key])
        literals = runtime_json(self.loader_file(contract['binary_literals']))
        self.loader_file(literals['elf'])
        selection = runtime_json(self.loader_file(contract['selection_contract']))
        self.loader_file(selection['loader'])
        for row in selection['disassembly_ranges']:
            self.loader_file({k: row[k] for k in ('path', 'sha256')})
        interpretation = runtime_json(self.loader_file(contract['interpretation']))
        self.loader_file(interpretation['raw'])
        if (role['disk_capture'] != contract['boards'][side] or role['capture_seal'] != contract['capture_seal'] or
                role['selection_contract'] != contract['selection_contract']):
            raise ValueError('Loader role substitutes another board, capture seal or selection contract')
        capture_root = evidence / 'loader-config-fix1-device-test'
        seal_rows = runtime_json(self.loader_file(role['capture_seal']))['files']
        captured = {}
        for row in seal_rows:
            file = self.loader_file({k: row[k] for k in ('path', 'sha256')})
            if not file.is_relative_to(capture_root) or str(file) in captured or file.stat().st_size != row['bytes']:
                raise ValueError('Loader capture inventory changed or escaped')
            captured[str(file)] = row['sha256']
        if len(captured) != 365:
            raise ValueError('Require exact365 captured loader files')
        disk = runtime_json(self.loader_file(role['disk_capture']))
        disk_fields = {'side', 'target', 'kind', 'status', 'connectivity', 'loader_revalidation', 'loader_sha256',
            'selectors_before', 'selectors_after', 'selected_requested_path', 'selected_canonical_path',
            'config_before', 'config_after', 'transfer', 'local_file', 'namespace_qualification', 'runtime_resolution'}
        if (set(disk) != disk_fields or (disk['side'], disk['target'], disk['kind'], disk['status'], disk['loader_sha256']) !=
                (side, DEVICES[side], 'LOADER_CONFIGURATION_DISK_CAPTURE', 'PASS_DISK_CAPTURE_ONLY', loader_sha)):
            raise ValueError('Loader disk role does not describe the selected board')
        _, text = self.loader_capture_command(disk['connectivity'], None, captured)
        if [line.split() for line in text.splitlines() if line.startswith(DEVICES[side])] != [[DEVICES[side], 'USB', 'Connected', 'localhost', 'hdc']]:
            raise ValueError('Loader capture has no same-board connectivity observation')
        _, text = self.loader_capture_command(disk['loader_revalidation'], side, captured)
        if text.splitlines().count(loader_sha + '  /system/lib/ld-musl-aarch64.so.1') != 1:
            raise ValueError('Loader binary revalidation differs')
        for phase in ('before', 'after'):
            rows = disk['selectors_' + phase]
            if len(rows) != 4:
                raise ValueError('Missing actual loader selector phase')
            for path, row in zip(selection['selector_paths_in_order'], rows):
                command, text = self.loader_capture_command(row['command_receipt'], side, captured)
                expected = ('REQUESTED=' + path + '\n' + "stat: '" + path + "': No such file or directory\nLSTAT_RC=1\n" +
                    "stat: '" + path + "': No such file or directory\nSTAT_RC=1\nLINK_RC=1\n" + path + '\nCANONICAL_RC=0\n\n' + command['marker'] + '=0\n')
                if (row['path'] != path or row['state'] != 'STAT_ENOENT' or row['stat_rc'] != 1 or
                        row['codes'] != {'LSTAT': 1, 'STAT': 1, 'LINK': 1, 'CANONICAL': 0} or text != expected):
                    raise ValueError('Loader selector is not the captured stable ENOENT branch')
        canonical = '/system/etc/ld-musl-namespace-aarch64.ini'
        before, after = disk['config_before'], disk['config_after']
        if (disk['selected_requested_path'] != '/etc/ld-musl-namespace-aarch64.ini' or disk['selected_canonical_path'] != canonical or
                {k: v for k, v in before.items() if k != 'command_receipt'} != {k: v for k, v in after.items() if k != 'command_receipt'}):
            raise ValueError('Loader selected file or pre/post identity differs')
        for row in (before, after):
            _, text = self.loader_capture_command(row['command_receipt'], side, captured)
            if row['canonical_path'] != canonical or row['sha256'] != contract['configuration_sha256'] or row['bytes'] != 3574 or len(row['stat']) != 9 or row['stat'][0] != 'regular file':
                raise ValueError('Loader configuration is not the captured regular file')
            for kind in ('LSTAT', 'STAT'):
                line = kind + ' ' + ' '.join(k + '=' + v for k, v in zip(('type', 'mode', 'uid', 'gid', 'dev', 'inode', 'size', 'mtime', 'ctime'), row['stat']))
                if text.splitlines().count(line) != 1:
                    raise ValueError('Loader raw stat is not the typed configuration identity')
            if text.splitlines().count('CANONICAL=' + canonical) != 1 or text.splitlines().count(row['sha256'] + '  ' + canonical) != 1:
                raise ValueError('Loader raw canonical hash differs')
        local = disk['local_file']
        file = self.loader_file({k: local[k] for k in ('path', 'sha256')})
        command, _ = self.loader_capture_command(disk['transfer'], side, captured)
        if (local['sha256'] != contract['configuration_sha256'] or local['bytes'] != 3574 or file.stat().st_size != 3574 or
                captured.get(str(file)) != local['sha256'] or command['argv'][5:] != [canonical, str(file)]):
            raise ValueError('Loader transfer does not bind the same-board captured bytes')
        self.external.setdefault('loader_search_configuration', {})[side] = {
            'receipt': descriptor, 'scope': 'STATIC_CONFIGURATION_ROLE_ONLY', 'selected_section': 'acquiescence',
            'secure_auxv_observed': False, 'environment_search': 'CONDITIONAL_NONSECURE_BRANCH',
            'runtime_maps': 'NOT_VERIFIED', 'exact_generated_run_binding': 'LATER_EXECUTION_PLAN'}

    def ordinary_cleanup_gate(self, first, audit, final, descriptor, source_map):
        if (first.get('status') != 'PASS_INITIAL_HOST_SCOPE' or first.get('stage') != 'initial_cleanup_fix1' or
                first.get('scope') != 'ORDINARY_CLEANUP_ABSENT_CONTROL_RACE_ONLY' or
                audit.get('status') != 'NO_ACTIONABLE_FINDINGS_FOUND' or audit.get('scope') != 'FIXED_ORDINARY_CLI_CLEANUP_REVIEW' or
                audit.get('stage') != 'review_cleanup_fix1' or audit.get('findings') != [] or
                audit.get('blocking_findings') != [] or audit.get('new_findings') != [] or
                final.get('status') != 'PASS_POSTREVIEW_HOST_SCOPE' or final.get('stage') != 'postreview_host' or
                final.get('scope') != 'ORDINARY_CLEANUP_ABSENT_CONTROL_RACE_ONLY' or
                any(row.get('source_freeze') != source_map or row.get('source_freeze_descriptor') != descriptor for row in (first, audit, final)) or
                audit.get('initial_result_sha256') != runtime_sha(self.proof('ordinary_initial', False)) or
                final.get('initial_result_sha256') != runtime_sha(self.proof('ordinary_initial', False)) or
                final.get('review_result_sha256') != runtime_sha(self.proof('ordinary_review', False)) or
                audit.get('initial_seal_sha256') != runtime_sha(self.proof('ordinary_initial_seal', False))):
            raise ValueError('Cleanup-specific exact source/stage/hash qualification differs')
        for row in (first, final):
            if not row.get('commands') or any(c.get('exit_code') != 0 or c.get('timed_out') is not False or c.get('tree_empty_confirmed') is not True for c in row['commands']):
                raise ValueError('Cleanup host command failed, timed out or left owned processes')
        self.cleanup_source_qualified = True

    def qualify(self):
        # No output, future ordinary run, final external selection or Main READY
        # is an input here. All proofs describe earlier immutable source/input gates.
        for label in QUALIFICATION_KEYS:
            self.proof(label, json=False)
        for label in ('qualification_design', 'package_provider_addendum'):
            self.proof(label, json=False)
        design = self.proof('qualification_design_result')
        if design['design_sha256'] != QUALIFICATION_CONTRACTS['qualification_design']:
            raise ValueError('Qualification design/result differs')
        initial, review, post = (self.proof('launcher_' + n) for n in ('initial', 'review', 'post'))
        sources = {'integrations/ros2/target/' + n: sha for n, sha in LAUNCHER_SOURCES.items()}
        artifacts = {str(self.builds['launcher'] / n): runtime_sha(self.builds['launcher'] / n) for n in (
            'package/actionbag_python', 'package/lib/libpython3.12.so.1.0', 'package/LAUNCHER_PACKAGE.json', 'PYTHON_PROVIDER.json')}
        if (initial['status'] != 'PASS_INITIAL_HOST_BUILD_SCOPE' or review['status'] != 'NO_ACTIONABLE_FINDINGS_FOUND' or
                post['status'] != 'PASS_POSTREVIEW_HOST_SCOPE' or review['findings'] or
                any(row['source_freeze'] != sources or row['artifacts'] != artifacts for row in (initial, review, post))):
            raise ValueError('Launcher source/artifact qualification differs')
        if (review['initial_test_result_sha256'] != runtime_sha(self.proof('launcher_initial', False)) or
                review['initial_seal_sha256'] != runtime_sha(self.proof('launcher_initial_seal', False)) or
                post['review_result_sha256'] != runtime_sha(self.proof('launcher_review', False)) or
                post['initial_seal_sha256'] != runtime_sha(self.proof('launcher_initial_seal', False))):
            raise ValueError('Launcher source gate ordering/binding differs')
        self.proof_seal('launcher_initial_seal', ['launcher_initial'])
        self.proof_seal('launcher_post_seal', ['launcher_post'])
        target, plan = self.proof('launcher_target'), self.proof('launcher_target_plan')
        target_seal = self.proof_seal('launcher_target_seal', ['launcher_target', 'launcher_target_plan'])
        target_review = self.proof('launcher_target_review')
        if (target_review['status'] != 'PASS_SCOPED_POSTRUN_EVIDENCE_REVIEW' or target_review['assessment'] != 'NO_ACTIONABLE_FINDINGS_FOUND' or
                target_review['source_freeze'] != sources or target_review['findings'] or
                target_review['actual_target_result_sha256'] != runtime_sha(self.proof('launcher_target', False)) or
                target_review['actual_target_seal_sha256'] != runtime_sha(self.proof('launcher_target_seal', False))):
            raise ValueError('Target post-run review is not bound to these six cases')
        manifest_sha = runtime_sha(self.selected('launcher_manifest'))
        if (target['status'] != 'PASS_TARGET_ENTRY_ONLY' or target['stage_specific_final_tester_gate'] != 'PASS' or
                target['source_freeze'] != sources or target['package_manifest_sha256'] != manifest_sha or
                target['owned_provider_sha256'] != CORE['lib/libpython3.12.so.1.0'] or target['canonical_source_sha256'] != PYTHON_BASELINE or
                target['host_postreview_result_sha256'] != runtime_sha(self.proof('launcher_post', False)) or
                target['plan_sha256'] != runtime_sha(self.proof('launcher_target_plan', False)) or
                plan['package_manifest_sha256'] != manifest_sha or runtime_path(plan['package']) != self.builds['launcher'] / 'package' or
                target['session_exit_code'] != 0 or target['all_six_original_process_pairs_exited'] is not True):
            raise ValueError('Actual launcher target qualification binding differs')
        boards = {row['side']: row for row in plan['boards']}
        if len(plan['boards']) != 2 or set(boards) != set(DEVICES) or any(boards[s]['target'] != d for s, d in DEVICES.items()):
            raise ValueError('Actual target physical boards differ')
        cases = {(r['side'], r['case']): r for r in target['cases']}
        expected = {(s, n): rc for s in DEVICES for n, rc in [('normal', 0), ('help', 0), ('invalid', 2)]}
        if len(target['cases']) != 6 or set(cases) != set(expected):
            raise ValueError('Exactly six actual no-Node cases required')
        for (side, name), rc in expected.items():
            row = cases[side, name]
            if (row['status'] != 'PASS_ENTRY_ONLY' or (row['expected_rc'], row['child_rc']) != (rc, rc) or
                    any(row[k] is not True for k in ('owned_provider_before_and_after', 'math_hashlib_imports', 'both_exited', 'finalized', 'token_restored', 'actual_exit_collected_before_acceptance')) or
                    row['teardown_observed'] is not False or row['communication'] != 'NOT_RUN' or
                    type(row['child_pid']) is not int or row['child_pid'] <= 0 or row['child_pid'] == row['supervisor_pid']):
                raise ValueError('Actual no-Node case did not pass lifecycle/exit contract')
            folder = self.proof('launcher_target', False).parent / side / name
            out, err = folder / 'raw-stdout', folder / 'raw-stderr'
            for path, digest in ((out, row['stdout_sha256']), (err, row['stderr_sha256'])):
                if target_seal.get(str(path)) != digest:
                    raise ValueError('Case raw log is not sealed to the actual result')
            text = out.read_text(encoding='utf-8')
            provider_path = boards[side]['remote'] + '/package/lib/libpython3.12.so.1.0'
            if row['owned_provider_path'] != provider_path:
                raise ValueError('Actual owned provider path differs')
            ordered = [f"ROS_IDENTITY_TASKS pid={row['child_pid']} count=1 complete=1",
                'ROS_IDENTITY stage=select_token matches=1 rc=0',
                'ROS_IDENTITY phase=after_select bridge=0 called=15 info_rc=0 permission_rc=0 granted=1',
                'ROS_PYTHON_PROVIDER phase=BEFORE_INITIALIZE owned=1 unique=1 path=' + provider_path,
                'ROS_PYTHON_INITIALIZE after_token_admission=1 isolated=1 site=0',
                'ROS_PYTHON_RUNTIME imports=math,hashlib owned_provider=1 original_stdlib=1',
                'ROS_PYTHON_ENTRY mode=ros2 participant_lifecycles_max=1 daemon=REFUSED',
                f'ROS_PYTHON_INTERPRETER_RETURN execution_rc={rc} finalize_rc=0',
                'ROS_PYTHON_PROVIDER phase=AFTER_FINALIZE owned=1 unique=1 path=' + provider_path,
                f'ROS_PYTHON_FINAL finalized=1 python_rc={rc} teardown_observed=0 teardown_rc=-1 communication=NOT_ASSERTED',
                'ROS_IDENTITY stage=restore_token matches=1 rc=0', f'ROS_IDENTITY_FINAL restored=1 business_rc={rc}']
            if (any(text.count(marker) != 1 for marker in ordered) or
                    [text.index(marker) for marker in ordered] != sorted(text.index(marker) for marker in ordered) or
                    'ROS_PYTHON_DEFAULT_CONTEXT shutdown_returned=1' not in text or
                    any(marker in text + err.read_text() for marker in ('ROS_LISTENER_READY', 'FDDST_ROS_PRE_PARTICIPANT', 'ROS_PYTHON_PROVIDER_REJECTED', 'Error relocating', 'Traceback (most recent call last)'))):
                raise ValueError('Actual raw no-Node lifecycle markers differ')
        for kind in ('cli', 'types'):
            final = self.proof(kind + '_final')
            component_source = 'integrations/ros2/target/build_actionbag_' + kind + '.py'
            if (final['status'] != ('PASS_POSTREVIEW_HOST_SCOPE' if kind == 'cli' else 'PASS') or
                    final['source_freeze'] != {component_source: runtime_sha(self.selected(kind + '_source')), 'integrations/ros2/target/project_python.py': HELPER} or
                    final['review_result_sha256'] != runtime_sha(self.selected(kind + '_review')) or
                    final['initial_result_sha256'] != runtime_sha(self.selected(kind + '_test')) or
                    final['initial_seal_sha256'] != runtime_sha(self.proof(kind + '_initial_seal', False))):
                raise ValueError('Overlay post-review final gate differs: ' + kind)
            first_seal = self.proof_seal(kind + '_initial_seal', [])
            if first_seal.get(str(self.selected(kind + '_test'))) != runtime_sha(self.selected(kind + '_test')):
                raise ValueError('Overlay initial result absent from its seal')
            final_seal = self.proof_seal(kind + '_final_seal', [kind + '_final'])
            if kind == 'cli':
                # This exact hash-bound final receipt retains two raw global
                # assertions. Only the separately reviewed launcher2 changes
                # below are dispositioned; no arbitrary false check is waived.
                if {k for k, v in final['checks'].items() if v is not True} != {'all_verification_assertions'} or final['checks']['all_verification_assertions'] is not False:
                    raise ValueError('CLI final has undispositioned false checks')
                disposition = self.proof('cli_final', False).parent / 'GLOBAL_PRESERVATION_DRIFT.json'
                if str(disposition) not in final_seal:
                    raise ValueError('CLI exact drift disposition is not sealed')
                drift = runtime_json(disposition)
                if (set(drift['sources']) != set(sources) or drift['fix2_review_result_sha256'] != runtime_sha(self.proof('launcher_review', False)) or
                        drift['fix2_postreview_result_sha256'] != runtime_sha(self.proof('launcher_post', False)) or drift['initial_cli_seal_mismatches'] != 0):
                    raise ValueError('CLI drift is unrelated to reviewed launcher2')
                for name, row in drift['sources'].items():
                    if row != {'current_sha256': sources[name], 'reviewed_fix2_sha256': sources[name],
                               'matches_reviewed_fix2': True, 'in_actual_cli_scalar_inputs': False,
                               'in_actual_cli_protected_trees': False, 'literal_referenced_by_cli_builder': False}:
                        raise ValueError('CLI drift exceeds the two reviewed source changes')
            elif any(v is not True for v in final['checks'].values()):
                raise ValueError('Types final checks did not all pass')
        for kind in ('ordinary', 'composer'):
            freeze = self.proof(kind + '_freeze')
            names = (['integrations/ros2/target/actionbag_runtime_probe.py', 'tests/ros2_target/run_actionbag_cli.py',
                      'tests/ros2_target/test_actionbag_cli.py'] if kind == 'ordinary' else ['integrations/ros2/target/build_actionbag_runtime.py'])
            if kind == 'composer' and self.rosgraph_selected:
                names.append('integrations/ros2/target/build_actionbag_rosgraph.py')
            source_map = {name: runtime_sha(runtime_path(REPO / name, REPO)) for name in names}
            cleanup_freeze = kind == 'ordinary' and freeze.get('scope') == 'ORDINARY_CLEANUP_ABSENT_CONTROL_RACE_ONLY'
            frozen_sources = {r['path']: r['sha256'] for r in freeze['files']}
            if len(freeze['files']) != len(names) or frozen_sources != source_map:
                raise ValueError('Exact source freeze differs: ' + kind)
            for name, digest in source_map.items():
                self.inputs[str(REPO / name)] = digest
            first, audit, final = (self.proof(kind + '_' + stage) for stage in ('initial', 'review', 'final'))
            if kind == 'ordinary':
                # This existing producer uses three distinct PASS receipts.
                # Their roles are established by stages, source identity and
                # the exact initial -> review -> final chain, not PASS alone.
                freeze_descriptor = self.selection['qualification_inputs']['ordinary_freeze']
                initial_path = str(self.proof('ordinary_initial', False))
                review_path = str(self.proof('ordinary_review', False))
                resolution = audit.get('finding_resolution')
                if cleanup_freeze:
                    self.ordinary_cleanup_gate(first, audit, final, freeze_descriptor, source_map)
                if not cleanup_freeze and (first.get('status') != 'PASS' or first.get('stage') != 'initial_fix2' or
                        first.get('scope') != 'HOST_CONTROLS_AND_HISTORICAL_NATIVE_PARSER_REPLAY_ONLY' or
                        audit.get('status') != 'PASS' or audit.get('scope') != 'INDEPENDENT_FIX_REVIEW_OF_FROZEN_ORDINARY_CLI_FIX2' or
                        audit.get('blocking_findings') != [] or audit.get('new_findings') != [] or
                        audit.get('ready_for_postreview_final_testing') is not True or
                        not isinstance(resolution, dict) or set(resolution) != {'ORDINARY-REVIEW-01', 'ORDINARY-REVIEW-02'} or
                        any(not isinstance(row, dict) or row.get('status') != 'RESOLVED' for row in resolution.values()) or
                        final.get('status') != 'PASS' or final.get('stage') != 'postreview_host' or
                        final.get('scope') != 'HOST_CONTROLS_AND_HISTORICAL_PARSER_REPLAY_ONLY' or
                        final.get('host_gate') != 'PASS_FOR_POSTREVIEW_HOST_STAGE' or
                        final.get('all_owned_trees_empty') is not True or
                        final.get('postreview_tests', {}).get('all_pass') is not True or
                        type(final.get('postreview_tests', {}).get('skipped')) is not int or final['postreview_tests']['skipped'] != 0 or
                        any(type(final.get(key)) is not int or final[key] != 0 for key in ('new_behavioral_test_failures', 'new_test_retries'))):
                    raise ValueError('Ordinary stage-specific host/review receipt did not pass')
                if not cleanup_freeze and (any(row.get('source_freeze_descriptor') != freeze_descriptor for row in (first, audit, final)) or
                        any(row.get('source_freeze_sha256') != freeze_descriptor['sha256'] for row in (first, final)) or
                        audit.get('initial_result_path') != initial_path or final.get('initial_result_path') != initial_path or
                        final.get('initial_result_sha256') != runtime_sha(self.proof('ordinary_initial', False)) or
                        final.get('review_result_path') != review_path):
                    raise ValueError('Ordinary freeze descriptor or exact stage links differ')
            elif (first['status'] not in ('PASS_INITIAL_HOST_SCOPE', 'PASS_INITIAL_HOST_BUILD_SCOPE') or
                    audit['status'] != 'NO_ACTIONABLE_FINDINGS_FOUND' or audit['findings'] or
                    final['status'] != 'PASS_POSTREVIEW_HOST_SCOPE'):
                raise ValueError('Composer stage-specific source qualification did not pass')
            if (any(row['source_freeze'] != source_map for row in (first, audit, final)) or
                    audit['initial_result_sha256'] != runtime_sha(self.proof(kind + '_initial', False)) or
                    final['review_result_sha256'] != runtime_sha(self.proof(kind + '_review', False))):
                raise ValueError('Component qualification does not bind this source: ' + kind)
            if kind == 'composer' and self.rosgraph_selected and any(
                    row.get('rosgraph_build') != self.rosgraph_binding or row.get('source_freeze_descriptor') != self.selection['qualification_inputs']['composer_freeze']
                    for row in (first, audit, final)):
                raise ValueError('Same-stage rosgraph build/source qualification differs from selected artifact')
            self.proof_seal(kind + '_initial_seal', [kind + '_initial'])
            self.proof_seal(kind + '_final_seal', [kind + '_final'])
            if kind == 'ordinary':
                fixture_path = self.selected('fixture_source')
                fixture_binding = (freeze.get('schema') == 1 and
                    frozen_sources.get('integrations/ros2/target/actionbag_runtime_probe.py') == self.fixture['sha256'] and
                    freeze.get('source') == str(fixture_path) and freeze.get('source_sha256') == self.fixture['sha256'] and
                    freeze.get('fixture_unchanged') is True and self.fixture['sha256'] == '6402cbbc836ebb5f2f8fc8b7dd9f4df10da429c142c442208df410748a0d06df') if cleanup_freeze else (
                    runtime_path(freeze['source']) == fixture_path and freeze['source_sha256'] == self.fixture['sha256'])
                if self.fixture is None or self.selected('fixture_freeze') != self.proof('ordinary_freeze', False) or not fixture_binding:
                    raise ValueError('Qualified ordinary source is not the sealed package fixture')
        self.collect_python()
        self.qualification = {'scope': 'ORDINARY_FIXED_CASE_QUALIFICATION_ONLY',
            'inputs': self.selection['qualification_inputs'], 'launcher_review_raw_status': review['status'],
            'launcher_review_accepted_meaning': 'PASS_SOURCE_REVIEW_ONLY',
            'input_launcher_noNode_qualified': True, 'disk_closure': 'DISK_VERIFIED_EXPECTED_BINDING',
            'runtime_namespace_maps_imports': 'NOT_YET_VERIFIED'}

    def observed_command(self, descriptor, sealed):
        path = runtime_path(descriptor['path'])
        roots = [self.root / 'build/r25-native-actionbag/ordinary-imports-provider-device-test/commands']
        if getattr(self, 'cli_modules_commands', None) is not None:
            roots.append(self.cli_modules_commands)
        if path.parent not in roots:
            raise ValueError('Observed command is outside its verified exact capture directory')
        if set(descriptor) != {'path', 'sha256'} or sealed.get(str(path)) != descriptor['sha256']:
            raise ValueError('Observed command is not in the exact capture seal')
        data = self.receipt_index(descriptor)
        fields = {'argv', 'cwd', 'started_utc', 'completed_utc', 'host_rc', 'timed_out', 'timeout_seconds', 'stdout_sha256', 'stderr_sha256'}
        if (set(data) != fields or data['cwd'] != str(self.root) or type(data['host_rc']) is not int or data['host_rc'] != 0 or
                data['timed_out'] is not False or data['timeout_seconds'] != 30 or
                not isinstance(data['argv'], list) or not all(isinstance(a, str) for a in data['argv'])):
            raise ValueError('Observed command has wrong schema or failed outcome')
        start, end = (datetime.fromisoformat(data[k]) for k in ('started_utc', 'completed_utc'))
        if start.utcoffset() is None or end.utcoffset() is None or not 0 <= (end - start).total_seconds() <= 30:
            raise ValueError('Observed command time bounds differ')
        raw = []
        for kind in ('stdout', 'stderr'):
            file = path.with_suffix('.' + kind)
            if sealed.get(str(file)) != data[kind + '_sha256'] or runtime_sha(file) != data[kind + '_sha256']:
                raise ValueError('Observed raw output differs from sealed command')
            self.inputs[str(file)] = data[kind + '_sha256']
            raw.append(file.read_text(encoding='utf-8'))
        if raw[1] or re.search(r'^(?:Fail\b|Error\b|\[Fail\])', raw[0], re.I | re.M):
            raise ValueError('Observed command has error output')
        return data['argv'], raw[0]

    def observed_origin(self, role, side, row, sealed):
        sources = {'resolve': row['resolution_receipt'], 'receive': row['receive_receipt'],
            'posthash': row['post_receive_hash_receipt'], 'readelf': row['readelf_receipt']}
        if set(role['receipts']) != set(sources):
            raise ValueError('Observed provider requires four exact raw observation roles')
        path, local = row['actual_path'], row['local_file']
        cli_module = getattr(self, 'cli_module_rows', {}).get(side, {}).get(path) == row
        hdc = str(self.readelf.parents[3] / 'toolchains/hdc.exe')
        for kind, source in sources.items():
            descriptor = role['receipts'][kind]
            if descriptor != {'path': source, 'sha256': sealed.get(source)}:
                raise ValueError('Observed origin references another board/path/receipt')
            argv, text = self.observed_command(descriptor, sealed)
            if kind == 'readelf':
                if (argv != [str(self.readelf), '-h', '-d', *(['--dyn-syms'] if cli_module else []), local] or
                        not re.search(r'Machine:\s+AArch64\b', text) or not re.search(r'Class:\s+ELF64\b', text) or
                        not re.search(r'Data:\s+2.s complement, little endian', text) or
                        re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', text) != row['needed'] or
                        re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', text) != row['soname']):
                    raise ValueError('Observed readelf does not bind actual AArch64 ELF')
                if cli_module and (not re.search(r'Type:\s+DYN\b', text) or re.search(r'\((?:RPATH|RUNPATH)\)', text) or
                        not re.search(r'\bFUNC\s+GLOBAL\s+DEFAULT\s+\d+\s+' + re.escape(CLI_MODULE_SYMBOLS[path]) + r'\b', text)):
                    raise ValueError('CLI module lacks the exact defined PyInit/ELF contract')
            elif kind == 'receive':
                if argv != [hdc, '-t', DEVICES[side], 'file', 'recv', path, local] or not re.fullmatch(r'FileTransfer finish, Size:' + str(row['size']) + r', File count = 1, time:[^\n]+\n?', text):
                    raise ValueError('Observed receive target/path/size differs')
            else:
                phase = 'PRE' if kind == 'resolve' else 'POST'
                marker = re.findall(r'^R25_PROVIDER_' + side + r'_([0-9]{2})_' + phase + r'_RC=(.*)$', text, re.M)
                lines = ['REQUESTED=' + path, 'CANONICAL=' + path, 'LSTAT=' + row['lstat'], 'STAT=' + row['stat'], row['sha256'] + '  ' + path, '']
                if (len(argv) != 5 or argv[:4] != [hdc, '-t', DEVICES[side], 'shell'] or
                        len(marker) != 1 or marker[0][1] != '0' or
                        text.splitlines() != lines + ['R25_PROVIDER_' + side + '_' + marker[0][0] + '_' + phase + '_RC=0'] or
                        not argv[4].startswith('sh -c ') or any(s not in argv[4] for s in ('readlink -f', 'stat -c', 'stat -Lc', 'sha256sum', path))):
                    raise ValueError('Observed pre/post file identity or remote exit differs')

    def collect_observed_roots(self, sealed):
        self.observed_rows = {side: {} for side in DEVICES}
        if not OBSERVED_PROOFS.keys() <= self.selection['qualification_inputs'].keys():
            return
        for label, digest in OBSERVED_PROOFS.items():
            if runtime_sha(self.proof(label, False)) != digest:
                raise ValueError('Unselected finite observed-root proof: ' + label)
        contract, review, audit = (self.proof(k) for k in ('observed_contract', 'observed_review', 'observed_audit'))
        if (review['status'] != 'PASS_DESIGN_ONLY' or review['contract_sha256'] != OBSERVED_PROOFS['observed_contract'] or
                audit['status'] != 'PASS_LOCAL_DISK_CAPTURE_AND_CLOSURE_AUDIT' or audit['disk10capture'] != 'PASS' or
                audit['closure_audit'] != 'PASS' or audit['blockers'] or audit['additional_device_calls'] != 0 or
                audit['runtime_qualification'] != 'NOT_GRANTED' or audit['ordinary_result'] != 'UNCHANGED_BLOCKED' or
                audit['source_capture']['original_exit'] != 1 or audit['source_capture']['session'] != 84207 or
                audit['source_capture']['seal_sha256'] != OBSERVED_PROOFS['observed_capture_seal']):
            raise ValueError('Observed proof must preserve original failure and local audit roles')
        sealed.update(self.proof_seal('observed_audit_seal', ['observed_audit']))
        sealed.update(self.proof_seal('observed_capture_seal', []))
        capture = self.proof('observed_capture_seal', False).parent
        failed = capture / 'FIRST_FAILURE.json'
        if (sealed.get(str(failed)) != runtime_sha(failed) or runtime_json(failed)['status'] != 'BLOCKED_STOPPED_NO_RETRY' or
                runtime_json(failed)['exception'] != "KeyError('resolution_receipt')"):
            raise ValueError('Original failed capture must remain sealed and failed')
        if {r['path']: (r['category'], r['causal_loader_caller']) for r in contract['roots']} != {p: (c, 'UNPROVEN') for p, (c, _) in OBSERVED_ROOTS.items()}:
            raise ValueError('Observed root contract exceeds the five approved roots')
        rows = {(r['side'], r['actual_path']): r for r in audit['artifacts']}
        if len(audit['artifacts']) != 10 or set(rows) != {(s, p) for s in DEVICES for p in OBSERVED_ROOTS}:
            raise ValueError('Observed root inventory must be exactly five per board')
        providers = self.external['providers']
        for path in OBSERVED_ROOTS:
            a, b = (rows[s, path] for s in DEVICES)
            if any(a[k] != b[k] for k in ('actual_path', 'sha256', 'needed', 'soname', 'size')):
                raise ValueError('Observed provider differs across physical boards')
            for side in DEVICES:
                row = rows[side, path]
                local = runtime_path(row['local_file'], capture / 'elf' / side)
                metadata = row['stat'].split(':')
                original = capture / (side + '-' + local.name + '.json')
                if (row['side'] != side or row['target'] != DEVICES[side] or row['candidate'] != path or
                        sealed.get(str(original)) != runtime_sha(original) or runtime_json(original) != row or
                        sealed.get(str(local)) != row['sha256'] or runtime_sha(local) != row['sha256'] or
                        row['local_sha256'] != row['sha256'] or local.stat().st_size != row['size'] or
                        len(metadata) != 8 or row['lstat'] != row['stat'] or int(metadata[0], 16) & 0xf000 != 0x8000 or
                        int(metadata[4]) != row['size']):
                    raise ValueError('Observed origin/local ELF/regular-file metadata drift')
                self.observed_rows[side][path] = row
                self.provider_origin_rows[side][path] = row
                self.inputs[str(local)] = row['sha256']
            name = PurePosixPath(path).name
            if name in providers:
                raise ValueError('Observed roots may only add new exact providers')
            providers[name] = {'target_path': path, 'sha256': a['sha256'], 'local_elf': a['local_file'],
                'board_B_local_elf': b['local_file'], 'needed': a['needed'], 'soname': a['soname']}
        edges, visited = set(), set()
        for side in DEVICES:
            pending = list(OBSERVED_ROOTS)
            while pending:
                path = pending.pop()
                if (side, path) in visited: continue
                visited.add((side, path))
                row = self.provider_origin_rows[side][path]
                checked = audit['checked_providers'].get(side + ':' + path)
                if checked is None or any(checked[k] != row[v] for k, v in [('side', 'side'), ('path', 'actual_path'), ('sha256', 'sha256'), ('local_file', 'local_file'), ('needed', 'needed'), ('soname', 'soname')]):
                    raise ValueError('Observed recursive provider is unrelated to same-board origin')
                for receipt in checked['origin_receipts']:
                    if (sealed.get(receipt['path']) != receipt['sha256'] or receipt['path'] != row.get(receipt['original_field'])):
                        raise ValueError('Observed recursive closure uses unrelated origin receipt')
                for needed in row['needed']:
                    provider = providers.get(needed)
                    if provider is None: raise ValueError('Unresolved observed-root NEEDED edge')
                    target = provider['target_path']
                    edges.add((side, path, needed, target, provider['sha256']))
                    pending.append(target)
        if (set(audit['checked_providers']) != {s + ':' + p for s, p in visited} or
                len(audit['needed_edges']) != len(edges) or
                {(r['side'], r['consumer'], r['needed'], r['provider'], r['provider_sha256']) for r in audit['needed_edges']} != edges):
            raise ValueError('Observed-root recursive union has missing/extra NEEDED edges')
        self.external['observed_addendum'] = {'scope': 'IMPORTS_CDR_NO_NODE_CANDIDATE_ONLY',
            'roots': contract['roots'], 'causal_loader_caller': 'UNPROVEN', 'original_capture_exit': 1,
            'original_ordinary_result': 'BLOCKED_UNCHANGED', 'runtime_retest': 'NOT_RUN',
            'proofs': {k: self.selection['qualification_inputs'][k] for k in OBSERVED_PROOFS}}

    def collect_hitrace_root(self, sealed, selected=None):
        self.hitrace_rows = {side: {} for side in DEVICES}
        if not HITRACE_PROOFS.keys() <= self.selection['qualification_inputs'].keys():
            return
        for label, digest in HITRACE_PROOFS.items():
            if runtime_sha(self.proof(label, False)) != digest:
                raise ValueError('Unselected exact HiTrace proof: ' + label)
        contract, review, result = (self.proof(k) for k in ('hitrace_contract', 'hitrace_review', 'hitrace_capture_result'))
        if (review['status'] != 'PASS_DESIGN_ONLY' or review['findings'] or review['blocking_findings'] or
                any(review[k] != HITRACE_PROOFS[v] for k, v in (
                    ('contract_sha256', 'hitrace_contract'), ('capture_result_sha256', 'hitrace_capture_result'),
                    ('capture_seal_sha256', 'hitrace_capture_seal'))) or
                result['status'] != contract['capture']['expected_status'] or set(result['boards']) != set(DEVICES) or
                result['device_calls'] != 6 or result['sha_A_equals_B'] is not True or
                result['runtime_qualification'] != 'NOT_GRANTED' or result['original_window'] != 'FAIL_UNCHANGED' or
                result['no_runtime_execution'] is not True):
            raise ValueError('HiTrace evidence must retain exact design/disk-only/failure lineage')
        captured = self.proof_seal('hitrace_capture_seal', ['hitrace_capture_result'])
        capture = self.proof('hitrace_capture_result', False).parent
        if (len(captured) != 37 or set(captured) != {str(runtime_path(p)) for p in capture.rglob('*')
                if p.is_file() and p.name != 'SEALED_MANIFEST.json'} or
                any(not runtime_path(p).is_relative_to(capture) for p in captured)):
            raise ValueError('HiTrace capture must contain exactly its complete 37 sealed members')
        sealed.update(captured)
        for descriptor in (contract['capture']['plan'], contract['capture']['collector'],
                contract['lineage']['analysis_result'], contract['lineage']['analysis_evidence'], contract['baseline_external_edges']):
            file = runtime_path(descriptor['path'])
            if runtime_sha(file) != descriptor['sha256']:
                raise ValueError('HiTrace fixed source/lineage input changed')
            self.inputs[str(file)] = descriptor['sha256']
        plan = runtime_json(contract['capture']['plan']['path'])
        if plan['readelf'] != self.selection['readelf'] or len(plan['commands']) != 6:
            raise ValueError('HiTrace plan requires the selected exact ELF tool and six actual calls')
        baseline = runtime_json(contract['baseline_external_edges']['path'])
        selected = self.proof('external_edges') if selected is None else selected
        root = contract['root']
        if root['path'] != HITRACE_PATH or PurePosixPath(HITRACE_PATH).name in self.external['providers']:
            raise ValueError('HiTrace must be one new exact observed root')
        for side, target in DEVICES.items():
            row = result['boards'][side]
            directory = capture / side
            local = runtime_path(directory / 'libhitrace_meter.so', directory)
            if (runtime_json(directory / 'RESULT.json') != row or row['target'] != target or row['actual_path'] != HITRACE_PATH or
                    row['metadata_before'] != row['metadata_after'] or row['metadata_before'] != {
                        'canonical_path': HITRACE_PATH, 'stat': 'regular file|644|65096|2897|1776669000',
                        'size': root['size'], 'sha256': root['sha256']} or
                    row['received'] != {'path': str(local), 'sha256': root['sha256']} or
                    runtime_sha(local) != root['sha256'] or local.stat().st_size != root['size'] or
                    row['soname'] != root['soname'] or row['needed'] != root['needed'] or row['unresolved']):
                raise ValueError('HiTrace same-board ELF/metadata/result identity differs')
            role = contract['exact_origin_roles_to_derive'][side]
            times = []
            for step, kind in (('pre', 'resolve'), ('recv', 'receive'), ('post', 'posthash'), ('readelf', 'readelf')):
                command_path = directory / (step + '.COMMAND.json')
                if role['receipts'][kind] != {'path': str(command_path), 'sha256': captured.get(str(command_path))}:
                    raise ValueError('HiTrace command belongs to another board or role')
                command = runtime_json(command_path)
                text = []
                for stream in ('stdout', 'stderr'):
                    file = directory / (step + '.' + stream)
                    if captured.get(str(file)) != command[stream + '_sha256'] or runtime_sha(file) != command[stream + '_sha256']:
                        raise ValueError('HiTrace raw output hash differs')
                    text.append(file.read_text(encoding='utf-8'))
                if command['cwd'] != str(self.root) or type(command['host_exit']) is not int or command['host_exit'] != 0 or text[1]:
                    raise ValueError('HiTrace command failed or has unexpected stderr/cwd')
                if step == 'readelf':
                    if command['argv'] != [str(self.readelf), '-h', '-d', str(local)]:
                        raise ValueError('HiTrace readelf target/tool differs')
                    if (any(not re.search(pattern, text[0]) for pattern in (
                            r'Class:\s+ELF64\b', r'Machine:\s+AArch64\b', r'Type:\s+DYN\b',
                            r'Data:\s+2.s complement, little endian')) or
                            re.search(r'\((?:RPATH|RUNPATH)\)', text[0]) or
                            re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', text[0]) != root['soname'] or
                            re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', text[0]) != root['needed']):
                        raise ValueError('HiTrace actual ELF header/SONAME/NEEDED differs')
                    continue
                attempt = runtime_json(directory / (step + '.ATTEMPT.json'))
                expected = [r['argv'] for r in plan['commands'] if (r['side'], r['step']) == (side, step)]
                start, end, before = (datetime.fromisoformat(v) for v in (command['start_utc'], command['end_utc'], attempt['utc']))
                if (expected != [command['argv']] or attempt['argv'] != command['argv'] or
                        (command['side'], command['step'], attempt['side'], attempt['step']) != (side, step, side, step) or
                        attempt['cwd'] != str(self.root) or command['timeout_seconds'] != 20 or attempt['timeout_seconds'] != 20 or
                        command['timed_out'] is not False or any(t.utcoffset() is None for t in (start, end, before)) or
                        not before <= start <= end or not 0 <= (end - start).total_seconds() <= 20):
                    raise ValueError('HiTrace full argv/attempt/time/outcome binding differs')
                times.extend((start, end))
                if step == 'recv':
                    if (command['remote_exit'] is not None or command['argv'] != [str(self.readelf.parents[3] / 'toolchains/hdc.exe'),
                            '-t', target, 'file', 'recv', HITRACE_PATH, str(local)] or
                            not re.fullmatch(r'FileTransfer finish, Size:65096, File count = 1, time:[0-9]+ms rate:[0-9.]+kB/s\s*', text[0])):
                        raise ValueError('HiTrace transfer path/target/size/outcome differs')
                elif (type(command['remote_exit']) is not int or command['remote_exit'] != 0 or
                        text[0].splitlines() != ['CANONICAL', HITRACE_PATH, 'STAT', row['metadata_before']['stat'],
                            'SHA256', root['sha256'] + '  ' + HITRACE_PATH, 'R25_HITRACE_REMOTE_EXIT=0']):
                    raise ValueError('HiTrace pre/post exact raw identity or marker differs')
            if times != sorted(times):
                raise ValueError('HiTrace pre/receive/post times overlap or reorder')
            original, device = baseline['devices'][side], selected['devices'][side]
            descriptor = device['receipt_roles']['provider_origins'].get(HITRACE_PATH)
            if descriptor is None or self.receipt_index(descriptor) != role:
                raise ValueError('HiTrace selected origin is not the exact same-board typed role')
            expected = {'canonical_absolute_path': HITRACE_PATH, 'sha256': root['sha256'], 'size': root['size'],
                'elf_machine': 'AArch64', 'soname': root['soname'][0], 'needed': root['needed'],
                'origin_package_or_platform_receipt_sha256': descriptor['sha256']}
            preserved = {**device, 'providers': [r for r in device['providers'] if r['canonical_absolute_path'] != HITRACE_PATH],
                'needed_edges': [r for r in device['needed_edges'] if r['consumer_canonical_path'] != HITRACE_PATH],
                'receipt_roles': {**device['receipt_roles'], 'provider_origins': {p: r for p, r in device['receipt_roles']['provider_origins'].items() if p != HITRACE_PATH}}}
            if (preserved != original or len(device['providers']) != 86 or
                    [r for r in device['providers'] if r['canonical_absolute_path'] == HITRACE_PATH] != [expected] or
                    [r for r in device['needed_edges'] if r['consumer_canonical_path'] == HITRACE_PATH] != contract['external_delta']['edges'][side]):
                raise ValueError('HiTrace selection must preserve original85 and append only one root/six outgoing edges')
            self.hitrace_rows[side][HITRACE_PATH] = self.provider_origin_rows[side][HITRACE_PATH] = {
                'side': side, 'target': target, 'actual_path': HITRACE_PATH, 'sha256': root['sha256'],
                'size': root['size'], 'local_file': str(local), 'needed': root['needed'], 'soname': root['soname']}
        self.hitrace_contract = contract
        self.external['providers']['libhitrace_meter.so'] = {'target_path': HITRACE_PATH, 'sha256': root['sha256'],
            'local_elf': result['boards']['A']['received']['path'], 'board_B_local_elf': result['boards']['B']['received']['path'],
            'needed': root['needed'], 'soname': root['soname']}
        self.external['hitrace_observed_addendum'] = {'scope': contract['scope'], 'root': root,
            'actual_first_runtime_caller': 'UNKNOWN', 'incoming_needed_edge': 'NONE', 'original_window': 'FAIL_UNCHANGED',
            'runtime_retest': 'NOT_RUN', 'proofs': {k: self.selection['qualification_inputs'][k] for k in HITRACE_PROOFS}}

    def collect_cli_modules(self, sealed):
        self.cli_module_rows = {side: {} for side in DEVICES}
        if not CLI_MODULE_PROOFS.keys() <= self.selection['qualification_inputs'].keys():
            return None
        for label, digest in CLI_MODULE_PROOFS.items():
            if runtime_sha(self.proof(label, False)) != digest:
                raise ValueError('Unselected exact CLI module proof: ' + label)
        contract, review, audit = (self.proof(k) for k in ('cli_modules_contract', 'cli_modules_review', 'cli_modules_audit'))
        if (review['status'] != 'PASS_DESIGN_ONLY' or review['findings'] or review['blocking_findings'] or
                review['contract_sha256'] != CLI_MODULE_PROOFS['cli_modules_contract'] or
                review['audit_result_sha256'] != CLI_MODULE_PROOFS['cli_modules_audit'] or
                review['capture_seal_sha256'] != CLI_MODULE_PROOFS['cli_modules_capture_seal'] or
                audit['status'] != contract['audit']['expected_status'] or audit['blockers'] or audit['new_device_calls'] != 0 or
                audit['runtime_qualification'] != 'NOT_GRANTED' or audit['original_graph'] != 'BLOCKED_UNCHANGED'):
            raise ValueError('CLI module audit/design must retain disk-only and original failure scope')
        sealed.update(self.proof_seal('cli_modules_audit_seal', ['cli_modules_audit']))
        captured = self.proof_seal('cli_modules_capture_seal', [])
        capture = self.proof('cli_modules_capture_seal', False).parent
        if (len(captured) != 117 or set(captured) != {str(runtime_path(p)) for p in capture.rglob('*')
                if p.is_file() and p.name != 'SEAL.json'}):
            raise ValueError('CLI module capture requires its exact complete117 members')
        sealed.update(captured)
        for descriptor in (contract['capture']['result'], contract['capture']['plan'],
                contract['baseline_external_edges'], contract['lineage']['analysis_result']):
            file = runtime_path(descriptor['path'])
            if runtime_sha(file) != descriptor['sha256']:
                raise ValueError('CLI module fixed source/baseline/lineage changed')
            self.inputs[str(file)] = descriptor['sha256']
        result = runtime_json(contract['capture']['result']['path'])
        rows = {(r['side'], r['actual_path']): r for r in result['rows']}
        roots = {r['path']: r for r in contract['roots']}
        if (set(roots) != set(CLI_MODULE_SYMBOLS) or len(result['rows']) != 6 or
                set(rows) != {(s, p) for s in DEVICES for p in roots} or
                result['status'] != contract['capture']['expected_status'] or result['device_calls'] != 18 or result['readelf_calls'] != 6 or
                result['runtime_qualification'] != 'NOT_GRANTED' or result['original_graph'] != 'BLOCKED_UNCHANGED'):
            raise ValueError('CLI module capture must be exactly the six reviewed rows')
        self.cli_modules_commands = runtime_path(capture / 'commands', capture)
        selected = self.proof('external_edges')
        baseline = runtime_json(contract['baseline_external_edges']['path'])
        projection = {**selected, 'devices': {}}
        for side in DEVICES:
            device = selected['devices'][side]
            for path, root in roots.items():
                row = rows[side, path]
                local = runtime_path(row['local_file'], capture / 'elf' / side)
                metadata = row['stat'].split(':')
                if (row['target'] != DEVICES[side] or row['candidate'] != path or row['sha256'] != root['sha256'] or
                        runtime_json(capture / (side + '-' + local.name + '.json')) != row or
                        row['local_sha256'] != root['sha256'] or captured.get(str(local)) != root['sha256'] or
                        local.stat().st_size != root['size'] or row['size'] != root['size'] or
                        row['needed'] != ['libc.so'] or row['soname'] or row['lstat'] != row['stat'] or
                        len(metadata) != 8 or int(metadata[0], 16) & 0xf000 != 0x8000 or int(metadata[4]) != row['size']):
                    raise ValueError('CLI module exact board/path/hash/regular-file row differs')
                role = contract['exact_origin_roles_to_derive'][side][path]
                descriptor = device['receipt_roles']['provider_origins'].get(path)
                if descriptor is None or self.receipt_index(descriptor) != role:
                    raise ValueError('CLI module origin must be its exact same-board four roles')
                self.cli_module_rows[side][path] = self.observed_rows[side][path] = self.provider_origin_rows[side][path] = row
                self.provider_origin_role(descriptor, side, path, row, sealed)
                commands = [runtime_json(role['receipts'][k]['path']) for k in ('resolve', 'receive', 'posthash', 'readelf')]
                times = [datetime.fromisoformat(c[k]) for c in commands for k in ('started_utc', 'completed_utc')]
                if times != sorted(times):
                    raise ValueError('CLI module capture commands overlap or reorder')
                export = runtime_json(capture / (side + '-' + local.name + '.EXPORT.json'))
                raw = Path(row['readelf_receipt']).with_suffix('.stdout').read_text(encoding='utf-8')
                if (export['status'] != 'DEFINED_GLOBAL_FUNCTION' or export['symbol'] != CLI_MODULE_SYMBOLS[path] or
                        export['readelf_receipt'] != role['receipts']['readelf'] or
                        export['elf'] != {'path': str(local), 'sha256': root['sha256']} or
                        sum(line.split() == export['raw_symbol_fields'] for line in raw.splitlines()) != 1):
                    raise ValueError('CLI module EXPORT is not bound to actual symbol output')
                expected = {'canonical_absolute_path': path, 'sha256': root['sha256'], 'size': root['size'],
                    'elf_machine': 'AArch64', 'soname': None, 'needed': ['libc.so'],
                    'origin_package_or_platform_receipt_sha256': descriptor['sha256']}
                if [r for r in device['providers'] if r['canonical_absolute_path'] == path] != [expected]:
                    raise ValueError('CLI module selected row is absent, duplicated or changed')
            projection['devices'][side] = {**device,
                'providers': [r for r in device['providers'] if r['canonical_absolute_path'] not in roots],
                'needed_edges': [r for r in device['needed_edges'] if r['consumer_canonical_path'] not in roots],
                'receipt_roles': {**device['receipt_roles'], 'provider_origins': {p: r for p, r in device['receipt_roles']['provider_origins'].items() if p not in roots}}}
            if (len(device['providers']) != 89 or len(device['needed_edges']) != 299 or
                    [r for r in device['needed_edges'] if r['consumer_canonical_path'] in roots] != contract['external_delta']['new_edges'][side]):
                raise ValueError('CLI module delta must be exactly three roots and three libc edges')
        if projection != baseline:
            raise ValueError('CLI module89-to86 projection changed the original exact baseline')
        for path, root in roots.items():
            name = PurePosixPath(path).name
            if name in self.external['providers']:
                raise ValueError('CLI module root collides with an existing provider')
            self.external['providers'][name] = {'target_path': path, 'sha256': root['sha256'],
                'local_elf': rows['A', path]['local_file'], 'board_B_local_elf': rows['B', path]['local_file'],
                'needed': ['libc.so'], 'soname': []}
        self.external['cli_modules_addendum'] = {'scope': contract['scope'], 'roots': contract['roots'],
            'projection': 'EXACT89_TO86_BASELINE', 'runtime_retest': 'NOT_RUN', 'original_graph': 'BLOCKED_UNCHANGED',
            'proofs': {k: self.selection['qualification_inputs'][k] for k in CLI_MODULE_PROOFS}}
        return projection

    def collect_python(self):
        raw = self.proof('python_addendum')
        sealed = dict(self.system_sealed)
        sealed.update(self.proof_seal('python_initial_seal', ['python_initial']))
        sealed.update(self.proof_seal('python_addendum_seal', ['python_addendum']))
        if (raw['status'] != 'PASS_READONLY_PYTHON_NATIVE_CLOSURE' or raw['blockers'] or raw['unresolved_edges'] or
                len(raw['roots']) != 30 or raw['original_blocked_result_sha256'] != QUALIFICATION_CONTRACTS['python_initial'] or
                raw['original_seal_sha256'] != QUALIFICATION_CONTRACTS['python_initial_seal'] or
                raw['canonical47193_mapping_allowed'] is not False or raw['payload_launches'] != 0):
            raise ValueError('Exact Python/libz additive disk closure differs')
        providers = self.external['providers']
        aliases = {}
        all_rows = list(raw['roots'].items()) + list(raw['dependencies'].items()) + [('libz.so', raw['libz'])]
        for hint, boards in all_rows:
            if set(boards) != set(DEVICES):
                raise ValueError('Python provider requires both physical boards')
            a, b = boards['A'], boards['B']
            if any(a[k] != b[k] for k in ('actual_path', 'sha256', 'needed', 'soname', 'size')):
                raise ValueError('Python provider differs across boards')
            path = a['actual_path']
            if (not path.startswith('/') or posixpath.normpath(path) != path or '\\' in path or
                    'libpython3.12' in PurePosixPath(path).name or a['sha256'] == PYTHON_BASELINE):
                raise ValueError('Unapproved external provider path or canonical Python')
            for side, row in boards.items():
                local = runtime_path(row['local_file'])
                if (row['target'] != DEVICES[side] or row['side'] != side or row['local_sha256'] != row['sha256'] or
                        sealed.get(str(local)) != row['sha256'] or local.stat().st_size != row['size']):
                    raise ValueError('Unsealed actual Python provider')
                for name, receipt in row.items():
                    if name.endswith('_receipt') and str(runtime_path(receipt)) not in sealed:
                        raise ValueError('Unsealed Python ELF/alias/transfer receipt')
                if not hint.startswith('/') and 'resolution_receipt' in row:
                    aliases[side, hint] = runtime_sha(row['resolution_receipt'])
                self.provider_origin_rows[side].setdefault(path, row)
            name = hint if not hint.startswith('/') else PurePosixPath(path).name
            runtime_name(name)
            item = {'target_path': path, 'sha256': a['sha256'], 'local_elf': a['local_file'],
                'board_B_local_elf': b['local_file'], 'needed': a['needed'], 'soname': a['soname']}
            if name in providers:
                if any(providers[name][k] != item[k] for k in ('target_path', 'sha256', 'needed', 'soname')):
                    raise ValueError('Conflicting exact external provider name')
                continue
            canonical_name = next((n for n, r in providers.items() if r['target_path'] == path), None)
            if canonical_name is not None:
                if any(providers[canonical_name][k] != item[k] for k in ('sha256', 'needed', 'soname')):
                    raise ValueError('Conflicting canonical external provider')
                item['alias_of'] = canonical_name
            providers[name] = item
        self.collect_observed_roots(sealed)
        projection = self.collect_cli_modules(sealed)
        self.collect_hitrace_root(sealed, projection)
        expected = {r['target_path']: r for r in providers.values()}
        selected = self.proof('external_edges')
        if set(selected) != {'schema', 'scope', 'devices'} or selected['schema'] != 1 or selected['scope'] != 'DISK_VERIFIED_EXPECTED_BINDING' or set(selected['devices']) != set(DEVICES):
            raise ValueError('Pre-build external selection must not depend on final package/readiness')
        owned = {'member': 'lib/libpython3.12.so.1.0', 'sha256': CORE['lib/libpython3.12.so.1.0'], 'soname': 'libpython3.12.so.1.0'}
        for side, device in selected['devices'].items():
            if set(device) != {'physical_side', 'device_alias', 'device_identity_receipt_sha256', 'loader_search_configuration_sha256', 'captured_at_utc', 'providers', 'needed_edges', 'receipt_roles'}:
                raise ValueError('Unknown pre-build device fields')
            if device['physical_side'] != side or device['device_alias'] != DEVICES[side] or datetime.fromisoformat(device['captured_at_utc']).utcoffset() is None:
                raise ValueError('Pre-build physical identity/capture differs')
            roles = device['receipt_roles']
            if not isinstance(roles, dict) or set(roles) != {'identity', 'loader_configuration', 'provider_origins'}:
                raise ValueError('Require fixed typed identity/configuration/provider roles')
            if (device['device_identity_receipt_sha256'] != roles['identity']['sha256'] or
                    device['loader_search_configuration_sha256'] != roles['loader_configuration']['sha256']):
                raise ValueError('Device role hash does not identify its typed index')
            self.connectivity_role(roles['identity'], side, sealed)
            if set(roles['provider_origins']) != set(expected):
                raise ValueError('Missing or unknown canonical provider origin role')
            actual = {r['canonical_absolute_path']: r for r in device['providers']}
            if len(actual) != len(device['providers']) or set(actual) != set(expected):
                raise ValueError('Exact System/Python/libz canonical provider inventory differs')
            for path, row in actual.items():
                bound = expected[path]
                if (set(row) != {'canonical_absolute_path', 'sha256', 'size', 'elf_machine', 'soname', 'needed', 'origin_package_or_platform_receipt_sha256'} or
                        row['sha256'] != bound['sha256'] or row['needed'] != bound['needed'] or row['elf_machine'] != 'AArch64' or
                        row['size'] != Path(bound['local_elf']).stat().st_size or
                        row['soname'] != (bound['soname'][0] if bound['soname'] else None) or len(bound['soname']) > 1 or
                        row['origin_package_or_platform_receipt_sha256'] != roles['provider_origins'][path]['sha256']):
                    raise ValueError('External selected row differs from real sealed ELF')
                self.provider_origin_role(roles['provider_origins'][path], side, path, self.provider_origin_rows[side][path], sealed)
            self.loader_configuration_role(roles['loader_configuration'], side, sealed)
            edges = {}
            for edge in device['needed_edges']:
                key = (edge['consumer_canonical_path'], edge['needed_name'])
                if key in edges or key[0] not in expected or key[1] not in expected[key[0]]['needed']:
                    raise ValueError('Duplicate/unrecorded actual external NEEDED edge')
                edges[key] = edge
            if set(edges) != {(p, n) for p, row in expected.items() for n in row['needed']}:
                raise ValueError('Incomplete recursive actual external edge set')
            for (consumer, needed), edge in edges.items():
                if 'package_provider' in edge:
                    if set(edge) != {'consumer_canonical_path', 'needed_name', 'package_provider'} or needed != owned['soname'] or edge['package_provider'] != owned:
                        raise ValueError('Only the explicit exact owned-Python package edge is allowed')
                    continue
                if needed == owned['soname'] or set(edge) not in (
                        {'consumer_canonical_path', 'needed_name', 'provider_canonical_path', 'provider_sha256'},
                        {'consumer_canonical_path', 'needed_name', 'provider_canonical_path', 'provider_sha256', 'alias_evidence_sha256'}):
                    raise ValueError('Missing package edge, mixed variant or unknown external edge')
                provider = providers.get(needed)
                if provider is None or (edge['provider_canonical_path'], edge['provider_sha256']) != (provider['target_path'], provider['sha256']):
                    raise ValueError('External edge differs from exact inspected resolution')
                if needed not in provider['soname'] and PurePosixPath(provider['target_path']).name != needed:
                    alias = aliases.get((side, needed))
                    if needed == 'libc.musl-aarch64.so.1':
                        alias = runtime_sha(self.selected('external_alias_' + side))
                    if alias is None or edge.get('alias_evidence_sha256') != alias:
                        raise ValueError('Missing exact sealed alias resolution receipt')
                elif 'alias_evidence_sha256' in edge:
                    raise ValueError('Unnecessary unselected alias evidence')
        self.external.update(disk_devices=selected['devices'],
            transitive_status='DISK_VERIFIED_EXPECTED_BINDING_SYSTEM_PYTHON_LIBZ',
            runtime_namespace_and_all_dlopen='NOT_YET_VERIFIED')

    def collect_target_bridge(self):
        # The hook's configured dlopen root has no incoming DT_NEEDED edge.
        # Reuse the exact already tested native Action package, not an SDK stub.
        root = REPO / 'build/r25-native-actionbag/native-action-initial-package1/package'
        receipts = {'PACKAGE.json': '968911a55a4cf408166578d47360d6c0ca0364fcb5b4870a992bab3086b2dd39',
            'PACKAGE_INPUTS.json': '5012d8fecfc9fee19d015f999dd5270cebe9ece48f15c91c35776a8abcd5c2c5'}
        for leaf, digest in receipts.items():
            path = runtime_path(root / leaf)
            if runtime_sha(path) != digest: raise ValueError('Exact native bridge source package changed')
            self.inputs[str(path)] = digest
        manifest, inputs = (runtime_json(root / n) for n in receipts)
        leaf = PurePosixPath(TARGET_BRIDGE_MEMBER).name
        rows = [r for r in inputs['members'] if r['name'] == leaf]
        if (manifest['identity'] != IDENTITY or manifest['files'].get(leaf) != TARGET_BRIDGE_SHA or
                len(rows) != 1 or rows[0]['sha256'] != TARGET_BRIDGE_SHA or rows[0]['soname'] != leaf or
                rows[0]['needed'] != TARGET_BRIDGE_NEEDED or
                {'role': 'bridge', 'source': rows[0]['source']} not in inputs['roots']):
            raise ValueError('Native package does not bind the exact configured dynamic bridge')
        for name, digest in manifest['input_receipts'].items():
            path = runtime_path(name)
            if runtime_sha(path) != digest: raise ValueError('Native bridge original build receipt changed')
            self.inputs[str(path)] = digest
        original = runtime_path(rows[0]['source'])
        bridge = runtime_path(root / leaf, root)
        if runtime_sha(original) != TARGET_BRIDGE_SHA or runtime_sha(bridge) != TARGET_BRIDGE_SHA or TARGET_BRIDGE_MEMBER in self.plan:
            raise ValueError('Missing/changed/colliding configured dynamic bridge')
        self.inputs[str(original)] = TARGET_BRIDGE_SHA
        for dep in TARGET_BRIDGE_NEEDED[1:-1]:
            member = 'lib/' + dep
            if self.plan.get(member, {}).get('sha256') != manifest['files'].get(dep):
                raise ValueError('Native bridge internal provider differs: ' + dep)
        self.add(TARGET_BRIDGE_MEMBER, bridge, TARGET_BRIDGE_SHA, 'EXACT_CONFIGURED_TRANSPORT_DYNAMIC_ROOT')
        self.target_bridge = {'member': TARGET_BRIDGE_MEMBER, 'sha256': TARGET_BRIDGE_SHA,
            'source': str(bridge), 'original_build_product': str(original), 'factory_abi': 21405,
            'required_exports': TARGET_BRIDGE_EXPORTS, 'needed': TARGET_BRIDGE_NEEDED,
            'native_package_receipts': {str(root / n): h for n, h in receipts.items()},
            'selection_reason': 'Fixed ordinary bridge_library configuration; explicit dlopen root, no invented incoming NEEDED edge',
            'runtime_loaded': False}

    def check_target_bridge_elf(self, text):
        if (not re.search(r'Class:\s+ELF64\b', text) or not re.search(r'Machine:\s+AArch64\b', text) or
                not re.search(r'Type:\s+DYN\b', text) or re.search(r'\((?:RPATH|RUNPATH)\)', text) or
                re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', text) != [PurePosixPath(TARGET_BRIDGE_MEMBER).name] or
                re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', text) != TARGET_BRIDGE_NEEDED or
                any(not re.search(r'\bFUNC\s+GLOBAL\s+DEFAULT\s+(?!UND\b)\S+\s+' + re.escape(n) + r'\s*$', text, re.M) for n in TARGET_BRIDGE_EXPORTS)):
            raise ValueError('Configured transport bridge ELF/SONAME/NEEDED/factory ABI exports differ')

    def collect_rosgraph(self):
        root = self.builds['rosgraph']
        install = runtime_path(root / 'install', root)
        self.protect(install)
        labels = {'rosgraph_build': 'BUILD_RESULT.json', 'rosgraph_overlay': 'OVERLAY.json', 'rosgraph_native': 'NATIVE.json', 'rosgraph_wire': 'WIRE_BINDING.json'}
        for label, leaf in labels.items():
            if self.selected(label) != root / leaf:
                raise ValueError('Rosgraph artifact belongs to another selected build')
        source = self.selected('rosgraph_source')
        if source != REPO / 'integrations/ros2/target/build_actionbag_rosgraph.py':
            raise ValueError('Require the dedicated official rosgraph dependency builder')
        result, overlay, native, wire = (self.bind(k) for k in labels)
        expected_interfaces = ['msg/' + n + '.msg' for n in ('Action', 'Clock', 'Graph', 'InterfaceType', 'Node', 'QoSProfile', 'Service', 'Topic', 'TypeHash')]
        if (result['status'] != 'BUILDS_NOT_RUNTIME_TESTED' or result['interfaces'] != expected_interfaces or
                result['native_members'] != 11 or result['artifacts'] != self.protected[str(install)] or
                any(result.get(k) is not False for k in ('device_tested', 'node_import_verified', 'communication_verified')) or
                overlay['scope'] != 'COMPLETE_OFFICIAL_ROSGRAPH_MSGS_NODE_DEPENDENCY_NOT_RUNTIME_TESTED' or
                overlay['base_python_package_sha256'] != BASE or overlay['interfaces'] != expected_interfaces or
                result['overlay_sha256'] != runtime_sha(self.selected('rosgraph_overlay')) or len(overlay['files']) != 105):
            raise ValueError('Rosgraph complete official package/build/overlay differs')
        input_path = root / 'INPUTS.json'
        if runtime_sha(input_path) != result['input_manifest_sha256']:
            raise ValueError('Rosgraph build provenance differs')
        inputs = runtime_json(input_path)['sha256']
        if inputs.get(str(source)) != runtime_sha(source) or not 0 < len(inputs) <= 50000:
            raise ValueError('Rosgraph source is not bound to selected build')
        self.inputs[str(input_path)] = runtime_sha(input_path)
        for name, digest in inputs.items():
            path = runtime_path(name)
            if path.is_relative_to(self.output) or runtime_sha(path) != digest:
                raise ValueError('Rosgraph selected build input changed or depends on output')
            self.inputs[str(path)] = digest
        if len(native) != 11 or any(result['artifacts'].get(n) != r['sha256'] for n, r in native.items()):
            raise ValueError('Rosgraph native provenance differs')
        expected_wire = {str(Path(n).with_suffix(s)) for n in expected_interfaces for s in ('.msg', '.idl', '.json')}
        if set(wire) != expected_wire or any(r['equal_json_or_LF_text'] is not True for r in wire.values()):
            raise ValueError('Rosgraph source/wire resources differ from official baseline')
        before = {n: r['sha256'] for n, r in self.plan.items()}
        for name, row in overlay['files'].items():
            runtime_name(name)
            allowed = name.startswith(('lib/librosgraph_msgs__', 'python/rosgraph_msgs/', 'python/rosgraph_msgs-2.0.4-py3.12.egg-info/', 'share/rosgraph_msgs/')) or (
                name.startswith('share/ament_index/resource_index/') and PurePosixPath(name).name == 'rosgraph_msgs')
            installed = name if not name.startswith('python/') else 'lib/python3.12/site-packages/' + name[7:]
            path = runtime_path(row['source'], install)
            if (not allowed or name in self.plan or '__pycache__' in PurePosixPath(name).parts or name.endswith('.pyc') or
                    path != install / installed or result['artifacts'].get(installed) != row['sha256']):
                raise ValueError('Rosgraph destination/source/collision differs')
            self.add(name, path, row['sha256'], 'rosgraph_node_dependency')
        if any(self.plan[n]['sha256'] != digest for n, digest in before.items()):
            raise ValueError('Rosgraph overlay changed a previous native/RECORD/payload member')
        self.rosgraph_binding = {'build_output': str(root), 'build_result_sha256': runtime_sha(self.selected('rosgraph_build')),
            'overlay_sha256': runtime_sha(self.selected('rosgraph_overlay')), 'native_sha256': runtime_sha(self.selected('rosgraph_native')),
            'wire_binding_sha256': runtime_sha(self.selected('rosgraph_wire'))}

    def collect(self):
        for kind, expected in REVIEWS.items():
            path = self.selected(kind + '_review')
            if runtime_sha(path) != expected:
                raise ValueError('Unselected formal review: ' + kind)
            review = runtime_json(path)
            if review['review_gate'] != ('PASS_DESIGN_ONLY' if kind == 'design' else 'PASS'):
                raise ValueError('Review gate not passed')
            for suffix, key in (('_report', 'report_sha256'), ('_binding', 'binding_sha256')):
                if runtime_sha(self.selected(kind + suffix)) != review[key]:
                    raise ValueError('Review evidence binding differs: ' + kind + suffix)
            if kind != 'design':
                test_path = self.selected(kind + '_test')
                if runtime_sha(test_path) != review['tester_result_sha256']:
                    raise ValueError('Review/test binding mismatch')
                self.tests = getattr(self, 'tests', {})
                self.tests[kind] = runtime_json(test_path)
                source = self.selected(kind + '_source')
                if runtime_sha(source) != review['source_sha256']:
                    raise ValueError('Reviewed source hash mismatch: ' + kind)
                freeze = self.selected(kind + '_freeze')
                if runtime_sha(freeze) != review['freeze_sha256']:
                    raise ValueError('Reviewed freeze hash mismatch: ' + kind)
        if runtime_sha(self.selected('launcher_freeze')) != LAUNCHER_FREEZE:
            raise ValueError('Require exact fix2 launcher freeze')
        self.launcher_freeze = self.bind('launcher_freeze')
        for name, expected in LAUNCHER_SOURCES.items():
            source = runtime_path(REPO / 'integrations/ros2/target' / name)
            if runtime_sha(source) != expected or expected not in self.launcher_freeze['source_files'].values():
                raise ValueError('Frozen fix2 launcher source changed')
            self.inputs[str(source)] = expected
        if runtime_sha(self.selected('provider_decision')) != '2e00218c6f2f91421f662ab5dec193c0a70928f31c742720f9783032c2b22505':
            raise ValueError('Unselected owned-provider contract')
        self.design_inventory = self.bind('design_inventory')
        design_review = self.bind('design_review')
        if runtime_sha(self.selected('design_inventory')) != design_review['inventory_sha256']:
            raise ValueError('Unreviewed transition inventory')
        builds = self.selection['builds']
        if set(builds) != {'launcher', 'cli', 'types'} | ({'rosgraph'} if self.rosgraph_selected else set()):
            raise ValueError('Only selected CLI+types composition is implemented')
        self.builds = {k: runtime_path(v, REPO / 'build') for k, v in builds.items()}
        if self.builds['launcher'] != REPO / 'build/r25-native-actionbag/python-launcher-fix2-independent-build':
            raise ValueError('Require selected independent fix2 launcher build')
        for kind in ('cli', 'types'):
            if self.builds[kind] != Path(self.tests[kind]['build_output']).resolve():
                raise ValueError('Test selected different build: ' + kind)
        base_root = runtime_path(self.builds['launcher'] / 'package')
        cli_root = runtime_path(self.builds['cli'] / 'overlay')
        type_root = runtime_path(self.builds['types'] / 'install')
        for root in (base_root, cli_root, type_root):
            self.protect(root)
        self.launcher = self.bind('launcher_manifest')
        if self.selected('launcher_manifest') != base_root / 'LAUNCHER_PACKAGE.json':
            raise ValueError('Launcher manifest not at selected root')
        launcher_build = self.bind('launcher_build')
        if self.selected('launcher_build') != self.builds['launcher'] / 'BUILD_RESULT.json' or launcher_build['artifacts'].get(str(base_root / 'LAUNCHER_PACKAGE.json')) != runtime_sha(self.selected('launcher_manifest')):
            raise ValueError('Launcher build binding differs')
        if self.launcher['base_package_sha256'] != BASE or self.launcher['identity'] != IDENTITY or self.launcher['participant_lifecycles_per_process'] != 1 or self.launcher['daemon_supported']:
            raise ValueError('Unselected launcher contract')
        base_files = self.launcher['files']
        if self.protected[str(base_root)] != {**base_files, 'LAUNCHER_PACKAGE.json': runtime_sha(base_root / 'LAUNCHER_PACKAGE.json')}:
            raise ValueError('Launcher missing or unsealed extra files')
        if base_files['PACKAGE.json'] != BASE:
            raise ValueError('R24 source seal differs')
        for name, expected in CORE.items():
            if base_files.get(name) != expected:
                raise ValueError('Selected core provider differs: ' + name)
        self.python_provider = self.launcher['python_provider']
        provider_report = self.bind('launcher_provider')
        if self.selected('launcher_provider') != self.builds['launcher'] / 'PYTHON_PROVIDER.json' or provider_report != self.python_provider:
            raise ValueError('Owned Python provider provenance differs')
        if (provider_report['baseline_sha256'], provider_report['owned_sha256'], provider_report['old_flags_1'],
                provider_report['flags_1'], provider_report['changed_byte_offsets']) != (
                PYTHON_BASELINE, CORE['lib/libpython3.12.so.1.0'], 1, 3, [4594312]):
            raise ValueError('Unselected Python visibility derivative')
        original = self.selected('canonical_python')
        if runtime_sha(original) != PYTHON_BASELINE or original != self.root / 'build/r24-project-python/python-dev/usr/lib/libpython3.12.so.1.0':
            raise ValueError('Require unchanged original Python source artifact')
        original_bytes = original.read_bytes()
        owned_bytes = (base_root / 'lib/libpython3.12.so.1.0').read_bytes()
        if len(original_bytes) != len(owned_bytes) or [n for n, (a, b) in enumerate(zip(original_bytes, owned_bytes)) if a != b] != [4594312] or original_bytes[4594312] != 1 or owned_bytes[4594312] != 3:
            raise ValueError('Owned Python differs beyond approved visibility flag')
        for name, digest in base_files.items():
            self.add(name, runtime_path(base_root / name, base_root), digest, 'base')
        self.cli = self.bind('cli_overlay')
        self.types = self.bind('types_overlay')
        cli_review = self.bind('cli_review')
        if runtime_sha(self.selected('cli_overlay')) != cli_review['overlay_manifest_sha256']:
            raise ValueError('CLI review/overlay mismatch')
        if runtime_sha(self.selected('types_overlay')) != self.tests['types']['overlay_sha256']:
            raise ValueError('Types test/overlay mismatch')
        for kind, filename in (('cli', 'OVERLAY.json'), ('types', 'OVERLAY.json')):
            if self.selected(kind + '_overlay') != self.builds[kind] / filename:
                raise ValueError('Overlay root mismatch')
            result_path = self.selected(kind + '_build')
            if result_path != self.builds[kind] / 'BUILD_RESULT.json' or runtime_sha(result_path) != (cli_review['build_result_sha256'] if kind == 'cli' else self.tests['types']['build_result_sha256']):
                raise ValueError('Build result mismatch')
        composition = self.bind('cli_composition')
        self.dependencies = self.bind('cli_dependencies')
        if composition['base_manifest_sha256'] != BASE or self.cli['native_files'] != 0:
            raise ValueError('CLI base/native contract differs')
        if self.bind('cli_no_mutation')['status'] != 'PASS':
            raise ValueError('CLI input mutation reported')
        for label, filename in (('cli_composition', 'COMPOSITION.json'), ('cli_dependencies', 'DEPENDENCIES.json'), ('cli_no_mutation', 'NO_MUTATION.json')):
            if self.selected(label) != self.builds['cli'] / filename:
                raise ValueError('CLI provenance root differs')
        if self.protected[str(cli_root)] != {n: r['sha256'] for n, r in self.cli['files'].items()}:
            raise ValueError('CLI missing or unsealed extra files')
        removals = composition['remove_from_future_private_copy_only']
        if len(removals) != 96:
            raise ValueError('Require exact reviewed96 old CLI removals')
        # Bind entire removal map to the design-reviewed COMPOSITION bytes.
        expected_comp = next(r['sha256'] for n, r in self.design_inventory['inputs'].items() if n.endswith('developer-cli-overlay6\\COMPOSITION.json') or n.endswith('developer-cli-overlay6/COMPOSITION.json'))
        if runtime_sha(self.selected('cli_composition')) != expected_comp:
            raise ValueError('CLI removal plan changed from reviewed design')
        expected_deps = next(r['sha256'] for n, r in self.design_inventory['inputs'].items() if n.endswith('developer-cli-overlay6\\DEPENDENCIES.json') or n.endswith('developer-cli-overlay6/DEPENDENCIES.json'))
        if runtime_sha(self.selected('cli_dependencies')) != expected_deps:
            raise ValueError('CLI dependency plan changed from reviewed design')
        for name, digest in removals.items():
            runtime_name(name)
            if not name.startswith('python/ros2') or self.plan.get(name, {}).get('sha256') != digest:
                raise ValueError('Old CLI predecessor mismatch: ' + name)
            self.transitions.append({'path': name, 'old_sha256': digest, 'reason': 'EXACT_OLD_CLI_REMOVAL'})
            del self.plan[name]
        for name, row in self.cli['files'].items():
            if not name.startswith(('python/', 'share/')):
                raise ValueError('Unexpected pure CLI destination')
            self.add(name, runtime_path(cli_root / runtime_name(name), cli_root), row['sha256'], 'cli')
        if self.types['base_python_package_sha256'] != BASE:
            raise ValueError('Types Python origin mismatch')
        for name, row in self.types['files'].items():
            runtime_name(name)
            if not (name.startswith(('lib/libexample_interfaces', 'python/example_interfaces/',
                    'python/example_interfaces-0.12.1-py3.12.egg-info/', 'share/example_interfaces/', 'share/ament_index/'))):
                raise ValueError('Unexpected types destination: ' + name)
            source = runtime_path(row['source'], type_root)
            installed_name = name if not name.startswith('python/') else 'lib/python3.12/site-packages/' + name[7:]
            if source != type_root / installed_name or name in self.plan:
                raise ValueError('Types mapping/collision differs: ' + name)
            self.add(name, source, row['sha256'], 'types')
        self.bind('types_wire')
        if runtime_sha(self.selected('types_wire')) != self.tests['types']['wire_binding_sha256']:
            raise ValueError('Unselected Action wire binding')
        if self.selected('types_wire') != self.builds['types'] / 'WIRE_BINDING.json':
            raise ValueError('Types wire root mismatch')
        if self.rosgraph_selected:
            self.collect_rosgraph()
        self.collect_target_bridge()
        # All protected original core libraries/extensions must survive unchanged.
        for name, digest in base_files.items():
            if (name.startswith('lib/') or name.endswith('.so')) and self.plan.get(name, {}).get('sha256') != digest:
                raise ValueError('Core native member replaced: ' + name)
        if [n for n in self.plan if 'token_bridge' in n] != ['lib/libmdds_actionbag_token_bridge.so']:
            raise ValueError('Unexpected identity bridge')
        self.fixture = None
        if self.selection['fixture'] is not None:
            if self.selection['fixture'] != 'actionbag_runtime_probe':
                raise ValueError('Unknown fixture selection')
            source = self.selected('fixture_source')
            freeze = self.bind('fixture_freeze')
            if source != REPO / 'integrations/ros2/target/actionbag_runtime_probe.py' or Path(freeze['source']).resolve() != source or freeze['source_sha256'] != runtime_sha(source):
                raise ValueError('Fixture source/freeze binding differs')
            ast.parse(source.read_text(encoding='utf-8'))
            self.add('scripts/actionbag_runtime_probe.py', source, runtime_sha(source), 'explicit_frozen_fixture')
            self.fixture = {'source': str(source), 'sha256': runtime_sha(source),
                'freeze_sha256': runtime_sha(self.selected('fixture_freeze')), 'review_and_runtime': 'NOT_QUALIFIED_BY_COMPOSER'}
        runtime_namespace([*self.plan, 'COMPOSITION.json', 'LAUNCHER_PACKAGE.json'])
        self.collect_system()
        for name, digest in {'libsoftbus_client.z.so': '9b431d2976616becbbe5b3b18f3c9507ca42f7a15c965c9aa86457f4ff0e6087',
                'libc.so': 'd3c3b126a2715ba7d3b6f312776a4b6a55177367ba859970b14033d177eafc86'}.items():
            if self.external['providers'][name]['sha256'] != digest:
                raise ValueError('Configured bridge external provider differs: ' + name)
        for board in ('A', 'B'):
            text = self.selected('external_alias_' + board).read_text(encoding='utf-8')
            alias = json.loads(text.splitlines()[0])
            if not alias['same_handle'] or not alias['same_malloc'] or '/system/lib/ld-musl-aarch64.so.1' not in alias['maps'] or 'ALIAS_REMOTE_EXIT=0' not in text:
                raise ValueError('Missing actual libc alias receipt')
        baseline = self.bind('native_baseline')
        for name, expected in baseline['files'].items():
            if name in ('README.md', 'PLAN.md') or name.startswith('docs/'):
                continue  # Main owns independent documentation/Git transitions.
            source = runtime_path(REPO / runtime_name(name), REPO)
            if runtime_sha(source) != expected:
                raise ValueError('Frozen native baseline changed: ' + name)
            self.inputs[str(source)] = expected
        helper = runtime_path(Path(__file__).with_name('project_python.py'))
        if runtime_sha(helper) != HELPER:
            raise ValueError('Owned recorder helper changed')
        self.inputs[str(helper)] = HELPER
        spec = importlib.util.spec_from_file_location('actionbag_runtime_owned_recorder', helper)
        self.recorder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.recorder)
        self.readelf = runtime_path(self.selection['readelf']['path'])
        if runtime_sha(self.readelf) != self.selection['readelf']['sha256']:
            raise ValueError('Selected readelf changed')
        self.inputs[str(self.readelf)] = runtime_sha(self.readelf)
        self.inputs[str(Path(__file__).resolve())] = runtime_sha(__file__)
        for label in self.selection['artifacts']:
            self.selected(label)
        if self.qualified:
            self.qualify()
        self.prepare_installed_records()
        for path in self.inputs:
            p = Path(path)
            if p == self.output or p.is_relative_to(self.output):
                raise ValueError('Output overlaps selected artifact')

    def prepare_installed_records(self):
        # Three reviewed metadata-only derivations. Never repair arbitrary
        # missing members, rewrite SOABI by pattern, or generate another RECORD.
        evidence = self.root / 'build/r25-native-actionbag'
        contracts = {}
        for directory, filename, digest in (
            ('package-numpy-record-scope-review', 'EXACT_METADATA_TRANSITIONS.json', 'f957f671a990784a6ec36f8c37d10d1296bb7b7676d8708aa2e69d22beb3adbf'),
            ('package-numpy-record-scope-review', 'RESULT.json', 'dee16777b4290dd6c210930ce14dae6f223a0d5fbddac79ef1aa4c554e1158aa'),
            ('package-numpy-record-scope-review-supplement', 'PSUTIL_EXACT_CONTRACT.json', 'ff6d582f4639581d9a682753215cb323ee145b0082aaee142cf7109aaddae25c'),
            ('package-numpy-record-scope-review-supplement', 'RESULT.json', 'b5589215b9ab96c71727d41e9f69a63346b041399fa5d1d36578bb70f62bb830')):
            path = runtime_path(evidence / directory / filename, self.root)
            if runtime_sha(path) != digest:
                raise ValueError('Exact installed metadata contract differs')
            self.inputs[str(path)] = digest
            contracts[(directory, filename)] = runtime_json(path)
        self.record_data, self.record_originals = {}, {}
        audit = []
        contract = contracts[('package-numpy-record-scope-review', 'EXACT_METADATA_TRANSITIONS.json')]
        expected = {'numpy-1.26.4.dist-info': (13, 6, 770), 'PyYAML-6.0.2.dist-info': (0, 2, 22)}
        if len(contract['records']) != 2:
            raise ValueError('Require the two exact inherited RECORD contracts')
        for record in contract['records']:
            dist = Path(record['record']['path']).parent.name
            if dist not in expected:
                raise ValueError('Unknown inherited metadata derivation')
            counts = expected.pop(dist)
            name = 'python/' + dist + '/RECORD'
            source = self.plan[name]
            raw = source['source'].read_bytes()
            if (source['sha256'] != record['record']['sha256'] or hashlib.sha256(raw).hexdigest() != source['sha256'] or
                    len(raw) != record['record']['bytes'] or
                    (len(record['relocations']), len(record['omissions']), record['unchanged_existing_rows_checked']) != counts):
                raise ValueError('Exact inherited RECORD preimage differs')
            lines = raw.decode('utf-8').splitlines(keepends=True)
            replacements = {tuple(r['original_row']): r for r in record['relocations']}
            omissions = {tuple(r['original_row']): r for r in record['omissions']}
            if len(replacements) != counts[0] or len(omissions) != counts[1] or replacements.keys() & omissions.keys():
                raise ValueError('Duplicate metadata transition contract')
            output, changes, unchanged = [], [], 0
            for line in lines:
                rows = list(csv.reader([line], strict=True))
                if len(rows) != 1 or len(rows[0]) != 3:
                    raise ValueError('Noncanonical original RECORD row')
                row = tuple(rows[0])
                if row in replacements:
                    transition = replacements.pop(row)
                    member = 'python/' + runtime_name(transition['candidate_ohos_member'])
                    target = self.plan[member]
                    data = target['source'].read_bytes()
                    digest = hashlib.sha256(data).hexdigest()
                    if digest != target['sha256'] or digest != transition['selected_runtime']['sha256'] or len(data) != transition['selected_runtime']['bytes']:
                        raise ValueError('Selected OHOS metadata target differs')
                    new_row = [member[7:], 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).decode().rstrip('='), str(len(data))]
                    if new_row != transition['replacement_row'] or 'python/' + row[0] in self.plan:
                        raise ValueError('Unapproved metadata relocation')
                    encoded = io.StringIO(newline='')
                    csv.writer(encoded, lineterminator='\r\n' if line.endswith('\r\n') else '\n').writerow(new_row)
                    output.append(encoded.getvalue())
                    changes.append({'original_row': list(row), 'replacement_row': new_row, 'disposition': 'EXACT_EXISTING_OHOS_MEMBER_RELOCATION'})
                elif row in omissions:
                    transition = omissions.pop(row)
                    if ('python/' + row[0] in self.plan or 'python/' + transition['candidate_ohos_member'] in self.plan or
                            transition['runtime_member_exists'] is not False or transition['disposition'] !=
                            'NOT_PACKAGED_IN_APPROVED_R24_RUNTIME_SUBSET; not a claim of intrinsic unsupported functionality'):
                        raise ValueError('Unapproved installed metadata omission')
                    changes.append({'original_row': list(row), 'replacement_row': None, 'disposition': transition['disposition']})
                else:
                    output.append(line)  # Preserve all other original bytes.
                    unchanged += 1
            if replacements or omissions or unchanged != counts[2]:
                raise ValueError('Missing/repeated exact metadata transition')
            data = ''.join(output).encode('utf-8')
            self.record_data[name], self.record_originals[name] = data, raw
            audit.append({'path': name, 'kind': 'FINITE_INHERITED_INSTALLED_RECORD_DERIVATION',
                'original_source': str(source['source']), 'original_sha256': source['sha256'],
                'original_copy': str(self.output / 'metadata-derivation/originals' / name),
                'original_bytes': len(raw), 'unchanged_rows_and_raw_lines': unchanged,
                'transitions': changes, 'new_sha256': hashlib.sha256(data).hexdigest()})
        if expected:
            raise ValueError('Missing exact inherited metadata contract')
        psutil = contracts[('package-numpy-record-scope-review-supplement', 'PSUTIL_EXACT_CONTRACT.json')]
        name = psutil['new_member']
        if name != 'python/psutil-5.9.8.dist-info/RECORD' or name in self.plan or len(psutil['members']) != 15 or psutil['expected_rows'] != 16:
            raise ValueError('Unexpected psutil installed subset RECORD preimage')
        members, rows = set(), []
        for item in psutil['members']:
            member = 'python/' + runtime_name(item['installed_relative_name'])
            if member in members:
                raise ValueError('Duplicate psutil subset member')
            members.add(member)
            target = self.plan[member]
            data = target['source'].read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            if digest != target['sha256'] or digest != item['sealed_member']['sha256'] or len(data) != item['sealed_member']['bytes']:
                raise ValueError('Exact psutil installed subset member differs')
            row = [member[7:], 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).decode().rstrip('='), str(len(data))]
            if row != item['proposed_record_row']:
                raise ValueError('Exact psutil installed subset row differs')
            rows.append(row)
        if members != {n for n in self.plan if n.startswith(('python/psutil/', 'python/psutil-5.9.8.dist-info/'))}:
            raise ValueError('Unexpected psutil installed subset payload')
        if psutil['self_row'] != [name[7:], '', '']:
            raise ValueError('Unexpected psutil self row')
        encoded = io.StringIO(newline='')
        writer = csv.writer(encoded, lineterminator='\n')
        writer.writerows(sorted(rows))
        writer.writerow(psutil['self_row'])
        data = encoded.getvalue().encode('utf-8')
        self.record_data[name] = data
        audit.append({'path': name, 'kind': 'GENERATED_INSTALLED_SUBSET_RECORD',
            'original_RECORD': None, 'original_RECORD_provenance': 'ABSENT_IN_CANDIDATE_R24_LAUNCHER_AND_OLD_QUALIFIED_PACKAGE',
            'upstream_wheel_RECORD': False, 'new_sha256': hashlib.sha256(data).hexdigest(),
            'rows': sorted(rows) + [psutil['self_row']], 'exact_source_contract_sha256': 'ff6d582f4639581d9a682753215cb323ee145b0082aaee142cf7109aaddae25c'})
        self.record_derivation = {'scope': 'THREE_EXACT_METADATA_ONLY_DERIVATIONS', 'records': audit,
            'full_upstream_wheels_certified': False,
            'capability_limits': ['NumPy and PyYAML retain the approved partial R24 runtime subset; eight exact unselected members NOT_PACKAGED',
                'psutil RECORD describes only the fifteen selected immutable members; adds no native capability',
                'WHEEL tags retained as historical source metadata; target compatibility requires separate ELF/runtime evidence'],
            'native_and_python_payload_changed': False}
        for name, data in self.record_data.items():
            digest = hashlib.sha256(data).hexdigest()
            self.plan[name] = {'source': self.output / 'metadata-derivation/installed' / name, 'sha256': digest}
            self.origins.setdefault(name, []).append({'origin': 'EXACT_REVIEWED_INSTALLED_METADATA_DERIVATION',
                'source': str(self.plan[name]['source']), 'sha256': digest})
        runtime_namespace([*self.plan, 'COMPOSITION.json', 'LAUNCHER_PACKAGE.json'])
        self.validate_installed_records(planned=True)

    def validate_installed_records(self, planned=False):
        # Validate every installed distribution inventory, not just relocated CLI
        # metadata. Only RECORD's own row may omit hash and size.
        files = self.plan if planned else runtime_inventory(self.bundle)
        records = sorted(n for n in files if n.startswith('python/') and n.endswith('.dist-info/RECORD'))
        directories = {n.split('/')[1] for n in files if n.startswith('python/') and len(n.split('/')) > 2 and n.split('/')[1].endswith('.dist-info')}
        if {n.split('/')[1] for n in records} != directories:
            raise ValueError('Installed distribution missing RECORD')
        def content(name):
            if name not in files:
                raise ValueError('Installed RECORD member missing: ' + name)
            if planned:
                return self.record_data[name] if name in self.record_data else self.plan[name]['source'].read_bytes()
            return runtime_path(self.bundle / name, self.bundle).read_bytes()
        report = {}
        for name in records:
            rows = list(csv.reader(io.StringIO(content(name).decode('utf-8'), newline=''), strict=True))
            seen, self_rows = set(), 0
            for row in rows:
                if len(row) != 3:
                    raise ValueError('Installed RECORD must have exactly three fields')
                member, encoded, length = row
                # Installed RECORD paths are relative to package/python; some
                # authentic records name ../share assets in the same package.
                # Require their canonical spelling and an exact package member.
                if not member or PurePosixPath(member).is_absolute():
                    raise ValueError('Absolute/empty installed RECORD path')
                full = runtime_name(posixpath.normpath('python/' + member))
                if posixpath.relpath(full, 'python') != member:
                    raise ValueError('Noncanonical installed RECORD path')
                if full.casefold() in seen:
                    raise ValueError('Duplicate installed RECORD member')
                seen.add(full.casefold())
                data = content(full)
                if full == name:
                    if encoded or length:
                        raise ValueError('Installed RECORD self row must be unhashed')
                    self_rows += 1
                else:
                    expected = 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).decode().rstrip('=')
                    if encoded != expected or length != str(len(data)):
                        raise ValueError('Installed RECORD member hash/size differs: ' + full)
            if self_rows != 1:
                raise ValueError('Installed RECORD requires exactly one self row')
            report[name] = {'sha256': hashlib.sha256(content(name)).hexdigest(), 'rows': len(rows), 'self_rows': self_rows, 'all_rows_valid': True}
        if not records:
            raise ValueError('No installed RECORD coverage')
        return {'status': 'PASS', 'dist_info_directories': len(directories), 'validated_RECORDs': len(records),
            'rows': sum(r['rows'] for r in report.values()), 'records': report,
            'scope': 'ALL_INSTALLED_DIST_INFO_RECORDS_HASH_SIZE_PATH_SELF_COVERAGE; NOT_FULL_UPSTREAM_WHEEL_CERTIFICATION'}

    def save(self, name, value):
        (self.output / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    def metadata(self):
        distributions = {}
        for distribution in importlib.metadata.distributions(path=[str(self.bundle / 'python')]):
            name = re.sub('[-_.]+', '-', distribution.metadata['Name']).lower()
            if name in distributions:
                raise ValueError('Duplicate installed distribution: ' + name)
            distributions[name] = distribution
        for name in ('ros2cli', 'ros2node', 'ros2topic', 'ros2action', 'ros2pkg', 'ros2interface'):
            if distributions[name].version != '0.32.12' or not (self.bundle / 'share/ament_index/resource_index/packages' / name).is_file():
                raise ValueError('CLI metadata/ament mismatch: ' + name)
        self.save('ALL_RECORDS.json', self.validate_installed_records())
        entries = {n: sorted((e.group, e.name, e.value) for e in d.entry_points) for n, d in distributions.items()}
        expected_entries = self.cli['entry_points']
        for name, expected in expected_entries.items():
            if entries.get(name) != sorted((r['group'], r['name'], r['value']) for r in expected):
                raise ValueError('Installed entrypoints differ: ' + name)
            for row in expected:
                module = row['value'].split(':')[0].replace('.', '/')
                if not any((self.bundle / 'python' / (module + tail)).is_file() for tail in ('.py', '/__init__.py')):
                    raise ValueError('Advertised entrypoint module absent: ' + module)
        for name in ('share/ament_index/resource_index/packages/example_interfaces',
                     'share/ament_index/resource_index/rosidl_interfaces/example_interfaces',
                     'share/example_interfaces/action/Fibonacci.idl'):
            if not (self.bundle / name).is_file():
                raise ValueError('Required Action ament/interface resource absent')
        self.save('METADATA.json', {'distributions': {n: d.version for n, d in distributions.items()},
            'entry_points': entries, 'selected_overlay_entry_points': expected_entries,
            'target_modules_imported': False, 'full_pkg_create_lint': 'UNVERIFIED_DEPENDENCIES_REMAIN'})

    def elf_closure(self):
        objects = {}
        environment = os.environ.copy()
        environment.update(TEMP=str(self.output / 'tmp'), TMP=str(self.output / 'tmp'), PYTHONDONTWRITEBYTECODE='1')
        for name, row in self.plan.items():
            file = self.bundle / name
            with file.open('rb') as stream:
                magic = stream.read(4)
            if magic != b'\x7fELF':
                continue
            label = 'elf-' + str(len(objects) + 1)
            self.recorder.project_python_record(self.output, label,
                [self.readelf, '-h', '-d', '--dyn-syms', '--wide', file], environment, timeout_seconds=30)
            log = next((self.output / 'commands').glob('*-' + label + '.stdout')).read_text(encoding='utf-8')
            if not re.search(r'Machine:\s+AArch64', log) or not re.search(r'Class:\s+ELF64', log):
                raise ValueError('Unexpected native architecture: ' + name)
            def tags(kind):
                return re.findall(r'\(' + kind + r'\)[^\n]*\[([^\]]+)\]', log)
            paths = tags('RPATH') + tags('RUNPATH')
            if paths:
                if name not in RPATH_NAMES or row['sha256'] != self.launcher['files'][name] or paths != ['$ORIGIN/../../numpy.libs']:
                    raise ValueError('Unapproved ELF search path: ' + name)
                expanded = posixpath.normpath(posixpath.join(posixpath.dirname(name), paths[0].replace('$ORIGIN/', '')))
                if expanded != 'python/numpy.libs':
                    raise ValueError('Escaping baseline RPATH')
            if '.cpython-' in name and not name.endswith('.cpython-312-aarch64-linux-ohos.so'):
                raise ValueError('Unselected target SOABI')
            if name.startswith('python/example_interfaces/') and name.endswith('.so'):
                module = Path(name).name.split('.')[0]
                if not re.search(r'GLOBAL\s+DEFAULT\s+\d+\s+PyInit_' + re.escape(module) + r'\b', log):
                    raise ValueError('Missing types Python capsule initializer')
            if name == 'lib/libexample_interfaces__rosidl_typesupport_c.so' and not re.search(
                    r'GLOBAL\s+DEFAULT\s+\d+\s+rosidl_typesupport_c__get_action_type_support_handle__example_interfaces__action__Fibonacci\b', log):
                raise ValueError('Missing defined Fibonacci C Action typesupport export')
            if name == 'lib/libpython3.12.so.1.0' and (tags('SONAME') != ['libpython3.12.so.1.0'] or not re.search(r'\(FLAGS_1\)[^\n]*NOW[^\n]*GLOBAL', log)):
                raise ValueError('Owned Python visibility/SONAME differs')
            if name == TARGET_BRIDGE_MEMBER:
                self.check_target_bridge_elf(log)
            objects[name] = {'sha256': row['sha256'], 'needed': tags('NEEDED'), 'soname': tags('SONAME'), 'search_paths': paths}
        if objects.get(TARGET_BRIDGE_MEMBER, {}).get('sha256') != TARGET_BRIDGE_SHA:
            raise ValueError('Configured transport dynamic root is absent from final ELF inventory')
        if {n for n, r in objects.items() if r['search_paths']} != RPATH_NAMES:
            raise ValueError('Baseline NumPy RPATH set differs')
        if self.qualified and (objects['lib/libpython3.12.so.1.0']['needed'] != ['libc.so'] or
                objects['lib/libpython3.12.so.1.0']['sha256'] != CORE['lib/libpython3.12.so.1.0']):
            raise ValueError('Owned Python recursive libc contract differs')
        sonames = {}
        for name, row in objects.items():
            for soname in row['soname']:
                if soname in sonames and sonames[soname]['sha256'] != row['sha256']:
                    raise ValueError('Conflicting SONAME provider: ' + soname)
                sonames[soname] = {'path': name, 'sha256': row['sha256']}
        routes, missing = {}, set()
        for name, row in objects.items():
            search = []
            if row['search_paths'] or name.startswith('python/numpy.libs/'):
                # These unchanged baseline children are loaded through the three
                # selected modules' inherited DT_RPATH route. No new path added.
                search.append('python/numpy.libs')
            search += ['lib']
            routes[name] = {}
            for needed in row['needed']:
                if '/' in needed or '\\' in needed:
                    raise ValueError('Path in DT_NEEDED')
                candidates = [directory + '/' + needed for directory in search if directory + '/' + needed in objects]
                if candidates:
                    provider = objects[candidates[0]]
                    if provider['soname'] and needed not in provider['soname']:
                        raise ValueError('Provider filename/SONAME differs')
                    routes[name][needed] = {'package': candidates[0], 'sha256': provider['sha256']}
                elif needed in self.external['providers']:
                    routes[name][needed] = {'external': self.external['providers'][needed]}
                else:
                    missing.add(needed)
        if missing:
            raise ValueError('Unknown native dependency: ' + ','.join(sorted(missing)))
        external_objects = {}
        for name, row in self.external['providers'].items():
            if 'alias_of' in row:
                continue  # libc aliases share the exact actual loader file.
            label = 'system-elf-' + str(len(external_objects) + 1)
            file = runtime_path(row['local_elf'])
            symbol = OBSERVED_ROOTS.get(row['target_path'], (None, None))[1] or CLI_MODULE_SYMBOLS.get(row['target_path'])
            self.recorder.project_python_record(self.output, label,
                [self.readelf, '-h', '-d', '--wide', *(['--dyn-syms'] if symbol else []), file], environment, timeout_seconds=30)
            log = next((self.output / 'commands').glob('*-' + label + '.stdout')).read_text(encoding='utf-8')
            if not re.search(r'Machine:\s+AArch64', log):
                raise ValueError('External provider architecture differs')
            needed = re.findall(r'\(NEEDED\)[^\n]*\[([^\]]+)\]', log)
            soname = re.findall(r'\(SONAME\)[^\n]*\[([^\]]+)\]', log)
            if needed != row['needed'] or soname != row['soname']:
                raise ValueError('Actual system ELF differs from exact two-board receipts: ' + name)
            if symbol and not re.search(r'\bFUNC\s+GLOBAL\s+DEFAULT\s+(?!UND\b)\S+\s+' + re.escape(symbol) + r'\s*$', log, re.M):
                raise ValueError('Observed CPython module lacks its actual PyInit export')
            external_objects[name] = {'path': str(file), 'sha256': row['sha256'],
                'target_path': row['target_path'], 'needed': needed,
                'soname': soname, 'board_B_same_bytes': True}
        pending = sorted({n for r in external_objects.values() for n in r['needed'] if n not in self.external['providers']
                          and not (self.qualified and n == 'libpython3.12.so.1.0')})
        if pending:
            raise ValueError('Actual system closure has unresolved dependencies')
        if self.qualified:
            # An explicit pre-build package edge is required; no missing-name
            # fallback. Its owned provider's own libc edge was checked above.
            edges = self.external['disk_devices']['A']['needed_edges']
            external_routes = {name: {e['needed_name']: e for e in edges if e['consumer_canonical_path'] == row['target_path']}
                               for name, row in external_objects.items()}
        else:
            external_routes = {name: {dep: {'target_path': self.external['providers'][dep]['target_path'],
                                'sha256': self.external['providers'][dep]['sha256']} for dep in row['needed']}
                               for name, row in external_objects.items()}
        self.save('ELF_CLOSURE.json', {'objects': objects, 'routes': routes, 'internal_dependency_routes': 'PASS',
            'explicit_dynamic_roots': [self.target_bridge],
            'actual_external_objects': external_objects,
            'exact_system_dependency_routes': external_routes,
            'external_dependency_names_requiring_exact_target_receipts_and_ELF': pending,
            'external_transitive_closure': self.external['transitive_status'], 'target_loaded_maps_rechecked': False,
            'external_python_stdlib_native_closure': 'DISK_VERIFIED_EXPECTED_BINDING' if self.qualified else 'BLOCKED_PENDING_EXACT_MODULE_PROVIDER_RECEIPTS',
            'dynamic_providers_require_target_check': ['rmw_fastrtps_cpp', 'project patched FastDDS factory hook', 'DSoftBus Socket provider', 'rosidl_typesupport_c/fastrtps/introspection Python capsule selection'],
            'runtime_symbol_resolution': 'NOT_EXECUTED'})

    def unchanged(self):
        changed = [p for p, digest in self.inputs.items() if runtime_sha(p) != digest]
        changed += [p for p, files in self.protected.items() if runtime_inventory(Path(p)) != files]
        self.save('NO_MUTATION.json', {'status': 'FAIL' if changed else 'PASS', 'changed': changed,
            'scope': 'Selected payload roots/artifacts/source/helper/tool only; unrelated Main Git/docs transitions are not policed'})
        if changed:
            raise ValueError('Protected input changed; no automatic restoration')

    def build(self):
        self.collect()  # All path, source and collision decisions precede output creation.
        self.output.mkdir(parents=True)
        (self.output / 'commands').mkdir()
        (self.output / 'tmp').mkdir()
        try:
            for name, data in self.record_data.items():
                target = self.plan[name]['source']
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            for name, data in self.record_originals.items():
                target = self.output / 'metadata-derivation/originals' / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            self.save('RECORD_DERIVATION.json', self.record_derivation)
            self.save('INPUTS.json', self.inputs)
            self.save('PLAN.json', {'members': {n: {'source': str(r['source']), 'sha256': r['sha256']} for n, r in self.plan.items()}, 'transitions': self.transitions})
            for name, row in self.plan.items():
                target = self.bundle / name
                target.parent.mkdir(parents=True, exist_ok=True)
                runtime_path(target, self.bundle, exists=False)
                shutil.copyfile(row['source'], target)
                if runtime_sha(target) != row['sha256']:
                    raise ValueError('Copied member changed: ' + name)
            self.metadata()
            self.elf_closure()
            composition = {'schema': 1, 'scope': 'CLI_TYPES_STATIC_COMPOSITION_NO_TARGET_EXECUTION',
                'identity': IDENTITY, 'base_package_sha256': BASE,
                'historical_PACKAGE_json': 'UNCHANGED_SOURCE_R24_INVENTORY_NOT_CURRENT_RUNTIME_INVENTORY',
                'selected_inputs': self.inputs, 'member_provenance': {n: self.origins[n] for n in self.plan},
                'transitions': self.transitions, 'fixture': self.fixture,
                'installed_metadata_derivation': self.record_derivation,
                'python_provider': self.python_provider,
                'external_runtime': self.external, 'bag': 'NOT_INCLUDED',
                'explicit_dynamic_roots': [self.target_bridge],
                'rosgraph_node_dependency': self.rosgraph_binding if self.rosgraph_selected else 'NOT_INCLUDED',
                'external_python_stdlib_native_closure': 'DISK_VERIFIED_EXPECTED_BINDING' if self.qualified else 'BLOCKED_PENDING_EXACT_MODULE_PROVIDER_RECEIPTS_NO_PREFIX_ALLOWANCE',
                'participant_lifecycles_per_process': 1, 'spawn_refused_existing_daemon_not_blocked': True,
                'communicating_graph_commands_require': 'Only commands whose stock parser supports --no-daemon; action list BLOCKED ORD-01',
                'target_execution': 'NOT_RUN',
                'qualification': self.qualification if self.qualified else None,
                'deployment_eligibility': 'BOUNDED_ORDINARY_ACCEPTANCE_ONLY' if self.qualified else 'NOT_GRANTED_BY_STATIC_COMPOSITION'}
            (self.bundle / 'COMPOSITION.json').write_text(json.dumps(composition, indent=2) + '\n', encoding='utf-8')
            files = runtime_inventory(self.bundle)
            expected = {**{n: r['sha256'] for n, r in self.plan.items()},
                'COMPOSITION.json': runtime_sha(self.bundle / 'COMPOSITION.json')}
            if files != expected:
                raise ValueError('Unexpected final package membership')
            seal = {k: v for k, v in self.launcher.items() if k != 'files'}
            seal.update(scope='COMPOSED_CLI_TYPES_NOT_TARGET_VERIFIED', files=files)
            (self.bundle / 'LAUNCHER_PACKAGE.json').write_text(json.dumps(seal, indent=2) + '\n', encoding='utf-8')
            self.unchanged()
            self.save('RUNTIME_PACKAGE.json', {'schema': 3 if self.qualified else 2,
                'status': 'ELIGIBLE_FOR_BOUNDED_ACCEPTANCE_NOT_RUNTIME_VERIFIED' if self.qualified else 'STRUCTURAL_PASS_NOT_ELIGIBLE',
                'structural_qualification': 'EXACT_CLI_TYPES_OWNED_PYTHON_AND_STATIC_SYSTEM_CLOSURE_PASS',
                'readiness_gate': 'FINAL_EXACT_PACKAGE_VALIDATION_EXTERNAL_CLOSURE_AND_MAIN_RELEASE_REQUIRED' if self.qualified else 'BLOCKED_PENDING_INDEPENDENT_LAUNCHER_FIX2_REVIEW_TARGET_CASES_FIXTURE_AND_COMPOSER_GATES',
                'external_python_stdlib_native_closure': 'DISK_VERIFIED_EXPECTED_BINDING' if self.qualified else 'BLOCKED_PENDING_EXACT_MODULE_PROVIDER_RECEIPTS',
                'fixture': 'SEALED_EXPLICIT_SOURCE_NOT_RUNTIME_QUALIFIED' if self.fixture else 'BLOCKED_NOT_SELECTED',
                'launcher_manifest_sha256': runtime_sha(self.bundle / 'LAUNCHER_PACKAGE.json'), 'files': len(files),
                'package_inventory': {**files, 'LAUNCHER_PACKAGE.json': runtime_sha(self.bundle / 'LAUNCHER_PACKAGE.json')},
                'report_sha256': {n: runtime_sha(self.output / n) for n in ('INPUTS.json', 'PLAN.json', 'METADATA.json', 'ELF_CLOSURE.json', 'NO_MUTATION.json', 'ALL_RECORDS.json', 'RECORD_DERIVATION.json')},
                'target_imported': False, 'target_runtime_provider_rechecked': False, 'device_tested': False,
                'communication_verified': False, 'production_verified': False,
                'eligible_for_deployment': self.qualified, 'eligible_for_bounded_acceptance': self.qualified,
                'eligibility_scope': 'ORDINARY_FIXED_CASE_QUALIFICATION_ONLY' if self.qualified else 'NONE',
                'qualification_inputs': self.selection['qualification_inputs'] if self.qualified else {},
                'input_launcher_noNode_qualified': self.qualified,
                'outstanding': ['Final exact package validation and external Main readiness', 'First composed-package actual namespace/maps/import/node/CLI acceptance'],
                'source_sha256': runtime_sha(__file__)})
            return 0  # No mode emits Main's external device-run release.
        except BaseException as error:
            self.save('FAILURE.json', {'status': 'FAIL', 'error': repr(error), 'target_executed': False})
            self.unchanged()
            raise


def actionbag_runtime_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs-root', type=Path, required=True)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    return ActionbagRuntimeBuilder(parser.parse_args()).build()


if __name__ == '__main__':
    raise SystemExit(actionbag_runtime_main())
