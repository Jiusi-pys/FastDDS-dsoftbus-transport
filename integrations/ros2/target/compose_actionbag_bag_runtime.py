"""Add the reviewed native SQLite rosbag overlay to an immutable CLI runtime.

Local composition only. No target execution, provisioning or deployment grant.
The two original packages remain unchanged; only genuine installed RECORD paths
are relocated. The prior CLI qualification does not qualify the combined bag.
"""
import argparse
import base64
import csv
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[3]
BASE_HELPER = Path(__file__).with_name('build_actionbag_runtime.py')
BASE_HELPER_SHA = 'b677052c5aa061c300a78e9c4f0e9277b60becd1a6a0a2ec78aec401c205e8d2'
if hashlib.sha256(BASE_HELPER.read_bytes()).hexdigest() != BASE_HELPER_SHA:
    raise ValueError('Selected immutable composition helper changed')
_spec = importlib.util.spec_from_file_location('bag_composition_base', BASE_HELPER)
BASE = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(BASE)
SHA = BASE.runtime_sha
LOAD = BASE.runtime_json
PINNED = {
    'base_result': ('runtime-qualified-cli-types-modules/RUNTIME_PACKAGE.json', 'c46747034682072be50e8a7ad4ca6bd4129e8846dafd7b7cc4305ff3309bc299'),
    'base_closure': ('runtime-qualified-cli-types-modules/ELF_CLOSURE.json', 'de184dc31070776e029f0453f7d05711655a358b53a24d981c46086c2dbe1941'),
    'overlay': ('bag-fix1-independent-build/OVERLAY.json', 'fa3e4aaab603b641752f10cec7ad499f321ce1d4fc3e8cf5326ed9f274869d09'),
    'bag_build': ('bag-fix1-independent-build/BUILD_RESULT.json', 'b08ff96c47804a42101a1d1bddcddb50fbc0eed58c8db34d9de7e63cd48d6be4'),
}
ROSBAG_RECORD = 'python/ros2bag-0.26.11.dist-info/RECORD'
RELOCATIONS = {
    '../../../share/ament_index/resource_index/packages/ros2bag': '../share/ament_index/resource_index/packages/ros2bag',
    '../../../share/ros2bag/package.xml': '../share/ros2bag/package.xml',
}


def relocate_bag_record(raw, payload):
    """Only the two source-bound wheel data paths change; retain every hash."""
    rows = list(csv.reader(io.StringIO(raw.decode('utf-8'), newline=''), strict=True))
    if len(rows) != 20 or any(len(row) != 3 for row in rows):
        raise ValueError('Selected ros2bag RECORD shape differs')
    seen, changed, output = set(), [], []
    for row in rows:
        name, digest, size = row
        if name in seen:
            raise ValueError('Duplicate original RECORD row')
        seen.add(name)
        new = RELOCATIONS.get(name, name)
        full = 'share/' + new[len('../share/'):] if new.startswith('../share/') else 'python/' + new
        BASE.runtime_name(full)
        if full not in payload:
            raise ValueError('Original RECORD member missing: ' + full)
        if full == ROSBAG_RECORD:
            if digest or size:
                raise ValueError('Unexpected hashed RECORD self row')
        else:
            data = payload[full].read_bytes()
            if digest != 'sha256=' + base64.urlsafe_b64encode(hashlib.sha256(data).digest()).decode().rstrip('=') or size != str(len(data)):
                raise ValueError('Original RECORD content changed: ' + full)
        output.append([new, digest, size])
        if new != name:
            changed.append({'before': row, 'after': output[-1]})
    if {row['before'][0] for row in changed} != set(RELOCATIONS):
        raise ValueError('Missing exact RECORD data-path transitions')
    encoded = io.StringIO(newline='')
    csv.writer(encoded, lineterminator='\n').writerows(output)
    return encoded.getvalue().encode('utf-8'), changed


def bag_elf_inventory(package, files, base_closure, external, readelf, output):
    """Inspect the combined package and require deterministic existing providers."""
    objects, routes, commands = {}, {}, []
    for name, digest in sorted(files.items()):
        path = package / name
        with path.open('rb') as stream:
            if stream.read(4) != b'\x7fELF':
                continue
        stem = output / 'commands' / ('%04d-readelf' % (len(commands) + 1))
        argv = [str(readelf), '-h', '-d', '--dyn-syms', '--wide', str(path)]
        started = datetime.now(timezone.utc).isoformat()
        with stem.with_suffix('.stdout').open('wb') as stdout, stem.with_suffix('.stderr').open('wb') as stderr:
            try:
                run = subprocess.run(argv, stdout=stdout, stderr=stderr, timeout=30, check=False)
                rc, error = run.returncode, None
            except (OSError, subprocess.TimeoutExpired) as exc:
                rc, error = None, repr(exc)
        receipt = {'argv': argv, 'cwd': os.getcwd(), 'started_utc': started,
                   'finished_utc': datetime.now(timezone.utc).isoformat(), 'exit_code': rc, 'error': error,
                   'stdout_sha256': SHA(stem.with_suffix('.stdout')), 'stderr_sha256': SHA(stem.with_suffix('.stderr'))}
        stem.with_suffix('.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        commands.append(receipt)
        if rc != 0 or error or stem.with_suffix('.stderr').stat().st_size:
            raise ValueError('ELF inspection failed: ' + name)
        text = stem.with_suffix('.stdout').read_text(encoding='utf-8')
        if not re.search(r'Machine:\s+AArch64', text) or not re.search(r'Class:\s+ELF64', text):
            raise ValueError('Unexpected native architecture: ' + name)
        tags = lambda kind: re.findall(r'\(' + kind + r'\)[^\n]*\[([^\]]+)\]', text)
        row = {'sha256': digest, 'needed': tags('NEEDED'), 'soname': tags('SONAME'),
               'search_paths': tags('RPATH') + tags('RUNPATH')}
        if name in base_closure['objects']:
            if row != base_closure['objects'][name]:
                raise ValueError('Inherited ELF changed: ' + name)
        elif row['search_paths']:
            raise ValueError('New bag search path is not allowed: ' + name)
        if '.cpython-' in name and not name.endswith('.cpython-312-aarch64-linux-ohos.so'):
            raise ValueError('Wrong target SOABI')
        if name.startswith('python/rosbag2_py/') and name.endswith('.so'):
            symbol = 'PyInit_' + Path(name).name.split('.')[0]
            if not re.search(r'GLOBAL\s+DEFAULT\s+(?!UND\b)\S+\s+' + re.escape(symbol) + r'\s*$', text, re.M):
                raise ValueError('Missing native initializer: ' + name)
        objects[name] = row
    sonames = {}
    for name, row in objects.items():
        for soname in row['soname']:
            if soname in sonames and sonames[soname] != row['sha256']:
                raise ValueError('Conflicting ELF SONAME: ' + soname)
            sonames[soname] = row['sha256']
        search = ['python/numpy.libs', 'lib'] if row['search_paths'] or name.startswith('python/numpy.libs/') else ['lib']
        routes[name] = {}
        for needed in row['needed']:
            BASE.runtime_name(needed)
            if '/' in needed:
                raise ValueError('Path in DT_NEEDED')
            candidates = [directory + '/' + needed for directory in search if directory + '/' + needed in objects]
            if candidates:
                provider = objects[candidates[0]]
                if provider['soname'] and needed not in provider['soname']:
                    raise ValueError('Provider filename/SONAME differs')
                routes[name][needed] = {'package': candidates[0], 'sha256': provider['sha256']}
            elif needed in external['providers']:
                routes[name][needed] = {'external': external['providers'][needed]}
            else:
                raise ValueError('Unqualified bag native dependency: ' + needed)
    for name, expected in base_closure['routes'].items():
        if routes.get(name) != expected:
            raise ValueError('Bag overlay changes an inherited loader route: ' + name)
    return {**base_closure, 'objects': objects, 'routes': routes,
            'target_loaded_maps_rechecked': False, 'runtime_symbol_resolution': 'NOT_EXECUTED',
            'scope': 'COMBINED_PACKAGE_STATIC_CLOSURE_ONLY', 'readelf_commands': len(commands)}


def compose_bag_runtime(output):
    root = BASE.runtime_path(REPO.parents[2])
    output = BASE.runtime_path(output, REPO / 'build', exists=False)
    if output.exists():
        raise ValueError('Require fresh output; never overwrite an existing package')
    artifacts = REPO / 'build/r25-native-actionbag'
    inputs, reports = {str(BASE_HELPER): BASE_HELPER_SHA, str(Path(__file__).resolve()): SHA(__file__)}, {}
    for name, (relative, digest) in PINNED.items():
        path = BASE.runtime_path(artifacts / relative, artifacts)
        if SHA(path) != digest:
            raise ValueError('Selected input changed: ' + name)
        inputs[str(path)], reports[name] = digest, LOAD(path)
    proof = BASE.runtime_path(root / 'build/r25-native-actionbag/bag-fix1-final-host-test/RESULT.json', root)
    if SHA(proof) != '81ac95e661c3cc50b7ba491b7d6707a92b4662b7412d91f9d60e1498c2dc5cb3':
        raise ValueError('Bag post-review host proof changed')
    inputs[str(proof)] = SHA(proof)
    package = artifacts / 'runtime-qualified-cli-types-modules/package'
    base_files = reports['base_result']['package_inventory']
    if BASE.runtime_inventory(package) != base_files:
        raise ValueError('Qualified base package inventory changed')
    manifest = LOAD(package / 'LAUNCHER_PACKAGE.json')
    composition = LOAD(package / 'COMPOSITION.json')
    if composition['bag'] != 'NOT_INCLUDED' or reports['bag_build']['overlay_sha256'] != PINNED['overlay'][1]:
        raise ValueError('Unexpected composition or overlay state')
    payload = {name: BASE.runtime_path(package / name, package) for name in base_files
               if name not in ('COMPOSITION.json', 'LAUNCHER_PACKAGE.json')}
    shared, added = [], []
    overlay = reports['overlay']
    if overlay['scope'] != 'SQLITE_TARGET_BUILD_NOT_RUNTIME_VERIFIED' or len(overlay['files']) != 491:
        raise ValueError('Unexpected selected overlay')
    for name, row in overlay['files'].items():
        BASE.runtime_name(name)
        if name.endswith(('.pyc', '.pyo')) or '__pycache__' in name.split('/'):
            raise ValueError('Bytecode is not an overlay input')
        path = BASE.runtime_path(row['source'], root)
        if SHA(path) != row['sha256']:
            raise ValueError('Overlay member changed: ' + name)
        inputs[str(path)] = row['sha256']
        if name in payload:
            if SHA(payload[name]) != row['sha256']:
                raise ValueError('Conflicting bag overlay member: ' + name)
            shared.append(name)
        else:
            payload[name] = path
            added.append(name)
    if len(shared) != 73 or len(added) != 418:
        raise ValueError('Reviewed overlap set changed')
    probe = BASE.runtime_path(Path(__file__).with_name('bag_runtime_probe.py'), REPO)
    probe_member = 'scripts/bag_runtime_probe.py'
    if probe_member in payload:
        raise ValueError('Unexpected existing bag probe')
    payload[probe_member] = probe
    inputs[str(probe)] = SHA(probe)
    BASE.runtime_namespace([*payload, 'COMPOSITION.json', 'LAUNCHER_PACKAGE.json'])
    record, changes = relocate_bag_record(payload[ROSBAG_RECORD].read_bytes(), payload)
    readelf = BASE.runtime_path(Path(os.environ['LOCALAPPDATA']) / 'OpenHarmony/Sdk/23/native/llvm/bin/llvm-readelf.exe')
    if SHA(readelf) != '7a9045f22b922c3a97f97e35d95956ba993e8abeda615a2e752182c3401f7ef0':
        raise ValueError('Selected readelf changed')
    inputs[str(readelf)] = SHA(readelf)
    output.mkdir(parents=True)
    (output / 'commands').mkdir()
    bundle = output / 'package'
    save = lambda name, value: (output / name).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    try:
        for name, source in payload.items():
            target = bundle / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            if SHA(target) != SHA(source):
                raise ValueError('Copied member changed')
        (output / 'ros2bag.original.RECORD').write_bytes(payload[ROSBAG_RECORD].read_bytes())
        (bundle / ROSBAG_RECORD).write_bytes(record)
        helper = BASE.ActionbagRuntimeBuilder.__new__(BASE.ActionbagRuntimeBuilder)
        helper.bundle = bundle
        records = helper.validate_installed_records()
        save('ALL_RECORDS.json', records)
        save('RECORD_DERIVATION.json', {'scope': 'TWO_GENUINE_ROS2BAG_DATA_PATHS_ONLY',
             'original_sha256': SHA(payload[ROSBAG_RECORD]), 'installed_sha256': SHA(bundle / ROSBAG_RECORD), 'changes': changes})
        files = BASE.runtime_inventory(bundle)
        closure = bag_elf_inventory(bundle, files, reports['base_closure'], composition['external_runtime'], readelf, output)
        save('ELF_CLOSURE.json', closure)
        save('INPUTS.json', inputs)
        save('OVERLAY_COMPARISON.json', {'shared_identical': shared, 'added': added, 'conflicts': []})
        composition = {**composition, 'scope': 'CLI_AND_SQLITE_BAG_STATIC_COMPOSITION_ONLY',
            'bag': {'overlay_sha256': PINNED['overlay'][1], 'native_modules': overlay['modules'],
                    'record_derivation_sha256': SHA(output / 'RECORD_DERIVATION.json'),
                    'probe': {'member': probe_member, 'sha256': SHA(probe)}},
            'parent_composition_sha256': base_files['COMPOSITION.json'],
            'bag_composition_inputs': inputs, 'qualification': None,
            'deployment_eligibility': 'NOT_GRANTED_COMBINED_PACKAGE_REQUIRES_NEW_GATES', 'target_execution': 'NOT_RUN'}
        (bundle / 'COMPOSITION.json').write_text(json.dumps(composition, indent=2) + '\n', encoding='utf-8')
        files = BASE.runtime_inventory(bundle)
        manifest = {**manifest, 'scope': 'COMPOSED_CLI_SQLITE_BAG_NOT_TARGET_VERIFIED', 'files': files}
        (bundle / 'LAUNCHER_PACKAGE.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        unchanged = BASE.runtime_inventory(package) == base_files and all(SHA(path) == digest for path, digest in inputs.items())
        save('NO_MUTATION.json', {'status': 'PASS' if unchanged else 'FAIL', 'base_package_members': len(base_files), 'input_files': len(inputs)})
        if not unchanged:
            raise ValueError('Input changed during composition; no automatic restoration')
        save('RUNTIME_PACKAGE.json', {'schema': 4, 'status': 'STRUCTURAL_PASS_NOT_RUNTIME_VERIFIED',
            'package_inventory': BASE.runtime_inventory(bundle), 'launcher_manifest_sha256': SHA(bundle / 'LAUNCHER_PACKAGE.json'),
            'report_sha256': {name: SHA(output / name) for name in ('INPUTS.json', 'OVERLAY_COMPARISON.json',
                'ALL_RECORDS.json', 'RECORD_DERIVATION.json', 'ELF_CLOSURE.json', 'NO_MUTATION.json')},
            'eligible_for_deployment': False, 'target_imported': False, 'device_tested': False,
            'communication_verified': False, 'production_verified': False,
            'outstanding': ['Independent composition/probe tests/review and source freeze',
                'Exact external provider/readiness binding', 'Target no-Node ABI/parser gate', 'Real cross-board record/play'],
            'source_sha256': SHA(__file__)})
    except BaseException as error:
        save('FAILURE.json', {'status': 'FAIL', 'error': repr(error), 'target_executed': False})
        raise
    return 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    raise SystemExit(compose_bag_runtime(parser.parse_args().output))
