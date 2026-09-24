"""Restore a sealed checkpoint and build it; never deploy, install, delete or mutate Git."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

TOOL_HASH = '33fc9f7e841efb2cb1c959307bb8e1a6a260f745352a3ea55107f62a399fbc3c'
sys.dont_write_bytecode = True
_tool = Path(__file__).with_name('checkpoint_tool.py')
if hashlib.sha256(_tool.read_bytes()).hexdigest() != TOOL_HASH:
    raise ValueError('original checkpoint restorer hash mismatch')
_spec = importlib.util.spec_from_file_location('sealed_checkpoint_tool', _tool)
sealed = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(sealed)
SHA = re.compile(r'^[0-9a-f]{64}$')
HEAD = re.compile(r'^[0-9a-f]{40}$')
MARKER = 'delivery-owner.json'
SOURCE_RECEIPT = 'source-receipt.json'
TEST_CREATORS = {
    'build-input': ('tests/experimental_build_inputs_tests.ps1', 'c888a6d54a75fbb8c837fcc3e021ea5d18a6c4b76b4594c119d0aeec8ff65415'),
    'permission-fixture-log': ('tests/native_permission_guard_tests.ps1', '6996d6b7bd041bd30fc08edecb43c9df713aabae876ce74951c85dfc617f9d42'),
}


def hash_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, value):
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False, sort_keys=True)
        stream.write('\n')


def canonical(path):
    return sealed.canonical_path(path)


def overlaps(left, right):
    return sealed.is_within(left, right) or sealed.is_within(right, left)


def validate_output(output, protected, allow_owned=False):
    output = canonical(output)
    for root in protected:
        if overlaps(output, Path(root).resolve()):
            raise ValueError('output overlap with protected source/dependency: ' + str(root))
    if output.exists() and not (allow_owned and (output / MARKER).is_file()):
        raise ValueError('existing output is not an owned delivery directory')
    return output


def relative_paths(records):
    return sealed.path_set([item['path'] for item in records])


def read_overlay(root, manifest):
    if manifest.get('schema') != 1:
        raise ValueError('overlay schema')
    root = canonical(root)
    relative_paths(manifest['files'])
    payloads = []
    for item in manifest['files']:
        rel = item['path']
        # Runtime C++ cannot be repaired through this delivery-only overlay.
        if not (rel.startswith(('scripts/', 'cmake/', 'tests/')) or rel == 'CMakeLists.txt'):
            raise ValueError('overlay outside delivery scope: ' + rel)
        if Path(rel).suffix.lower() in ('.cpp', '.cxx', '.h', '.hpp', '.ipp'):
            raise ValueError('runtime source overlay outside delivery scope: ' + rel)
        path = root / rel
        sealed.no_links(path)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('overlay hash mismatch: ' + rel)
        payloads.append((item, data))
    return payloads


def validate_archive_members(members):
    files = []
    total = 0
    for member in members:
        sealed.safe_relative(member.name.rstrip('/'))
        if not (member.isfile() or member.isdir()):
            raise ValueError('archive links/special entries rejected')
        if member.isfile():
            files.append(member.name)
            total += member.size
    sealed.path_set(files)
    if len(files) > 5000 or total > 128 * 1024 * 1024:
        raise ValueError('baseline archive exceeds selected-source bound')
    return files


def test_generated_kind(rel):
    if re.fullmatch(r'\.artifacts/build-input-tests/[0-9a-f]{32}/space name\.h', rel):
        return 'build-input'
    if rel == '.artifacts/tdd/permissions-guard/ctest.log':
        return 'permission-fixture-log'
    return None


def capture_generated_test_files(receipt, source):
    """Account only the two outputs made by hash-identified sealed CTest fixtures."""
    source = canonical(source)
    declared = {record['path']: record['sha256'] for record in receipt['files']}
    generated = []
    for relroot in ('.artifacts/build-input-tests', '.artifacts/tdd/permissions-guard'):
        root = source / relroot
        if not root.exists():
            continue
        for suffix in sealed.payload_paths(root):
            rel = relroot + '/' + suffix
            kind = test_generated_kind(rel)
            if kind is None:
                raise ValueError('undeclared source in generated fixture root: ' + rel)
            creator, required_hash = TEST_CREATORS[kind]
            if declared.get(creator) != required_hash or hash_file(source / creator) != required_hash:
                raise ValueError('generated fixture creator is not the sealed test: ' + creator)
            path = source / rel
            data = path.read_bytes()
            if kind == 'build-input' and data != b'owned fixture':
                raise ValueError('generated build fixture content mismatch')
            if kind == 'permission-fixture-log':
                if len(data) > 32768 or b'scope=pure snapshot/ownership only; device/service/scheduler=NOT_TESTED' not in data:
                    raise ValueError('generated permission fixture log invalid')
            generated.append({'path': rel, 'sha256': hashlib.sha256(data).hexdigest(),
                              'classification': 'NEW_TEST_GENERATED', 'creator': creator,
                              'creator_sha256': required_hash, 'kind': kind})
    if len(generated) > 64:
        raise ValueError('generated fixture record bound exceeded')
    return generated


def prior_generated_test_files(output):
    merged = {}
    for mode in ('off', 'on'):
        path = output / ('host-' + mode + '-logs') / 'generated-test-files.json'
        if path.exists():
            for record in read_json(path)['files']:
                merged[record['path']] = record
    return list(merged.values())


def validate_source(receipt, source, generated_roots=(), generated_files=()):
    source = canonical(source)
    if receipt.get('schema') != 1 or receipt.get('kind') != 'RESTORED_PROJECT_SOURCE':
        raise ValueError('source receipt schema/kind')
    if Path(receipt['source_root']).resolve() != source:
        raise ValueError('source receipt root mismatch')
    if not HEAD.fullmatch(receipt['baseline_head']) or not SHA.fullmatch(receipt['checkpoint_inventory_sha256']) or not SHA.fullmatch(receipt['overlay_manifest_sha256']):
        raise ValueError('source identity hash invalid')
    relative_paths(receipt['files'])
    expected = {item['path']: item for item in receipt['files']}
    for rel, item in expected.items():
        path = source / rel
        sealed.no_links(path)
        if not path.is_file() or hash_file(path) != item['sha256']:
            raise ValueError('source hash mismatch: ' + rel)
        if item['origin'] not in ('CHECKPOINT', 'GIT_BASELINE', 'DELIVERY_OVERLAY'):
            raise ValueError('source origin invalid')
    generated_roots = [Path(p).resolve() for p in generated_roots]
    generated_paths = {}
    for record in generated_files:
        rel = record['path']
        sealed.safe_relative(rel)
        if test_generated_kind(rel) is None or rel in expected:
            raise ValueError('undeclared generated output path: ' + rel)
        path = source / rel
        sealed.no_links(path)
        if not path.is_file() or hash_file(path) != record['sha256']:
            raise ValueError('generated output hash mismatch: ' + rel)
        generated_paths[rel] = record['sha256']
    for directory, dirs, names in os.walk(source):
        current = Path(directory)
        dirs[:] = [d for d in dirs if not any(sealed.is_within(current / d, p) for p in generated_roots)]
        for name in dirs + names:
            sealed.no_links(current / name)
        for name in names:
            rel = (current / name).relative_to(source).as_posix()
            if rel not in expected and rel not in generated_paths:
                raise ValueError('undeclared source file: ' + rel)
    return len(expected)


def validate_review(receipt, source_hash):
    if receipt.get('schema') != 1 or receipt.get('status') != 'CLOSED' or receipt.get('blocking_findings') != 0:
        raise ValueError('native requires a closed independent review')
    if receipt.get('source_receipt_sha256') != source_hash:
        raise ValueError('native review source identity mismatch')
    if any(receipt.get(key, {}).get('status') != 'PASS' for key in ('host_off', 'host_on')):
        raise ValueError('native requires both restored host suites PASS')


def _declared_path(value):
    path = Path(value)
    if not path.is_absolute():
        raise ValueError('external dependency must use absolute path: ' + str(value))
    return path.resolve()


def selected_dependency_anchors(config, declarations):
    """Bind the effective selected commands/import entrypoints, not merely an unrelated file inventory."""
    host, native = config['host'], config['native']
    host_keys = {'generator', 'architecture', 'configuration', 'configuration_types', 'prefix',
                 'runtime', 'native', 'drop_modes', 'child_environment', 'psmodulepath',
                 'fastdds_dir', 'fastcdr_dir', 'foonathan_dir'}
    native_keys = {'sdk_root', 'prefix', 'platform_compiler', 'AccessTokenSource_policy',
                   'token_setproc_library', 'access_token_library', 'platform_cxx_library',
                   'ninja', 'runtime', 'drop_modes', 'restored_token_headers_relative'}
    if set(host) - host_keys or set(native) - native_keys:
        raise ValueError('inconsistent/unknown Host or native selection key')
    if host.get('configuration_types') != 'Release' or host.get('runtime') != 'MultiThreadedDLL /MD' or host.get('native') != 'OFF':
        raise ValueError('inconsistent Host ABI/configuration')
    if native.get('runtime') != 'NOT_RUN' or native.get('AccessTokenSource_policy') != 'RESTORED_SOURCE/.artifacts/reference/security_access_token':
        raise ValueError('inconsistent native execution/source configuration')
    if host.get('drop_modes') != ['OFF', 'ON'] or native.get('drop_modes') != ['OFF', 'ON']:
        raise ValueError('inconsistent supported experiment configurations')
    anchors = []

    def require(path, purpose):
        path = _declared_path(path)
        expected = declarations.get(str(path).casefold())
        if expected is None:
            raise ValueError('selected dependency is not hash-declared: ' + purpose + ': ' + str(path))
        anchors.append({'purpose': purpose, 'path': str(path), 'sha256': expected})

    def consistent(configured, effective, purpose):
        if _declared_path(configured) != _declared_path(effective):
            raise ValueError('inconsistent configured/effective dependency: ' + purpose)

    for key in ('python', 'node', 'git', 'cmake', 'ctest', 'powershell'):
        require(config[key], 'tool.' + key)
    for key in ('platform_compiler', 'token_setproc_library', 'access_token_library', 'platform_cxx_library'):
        require(native[key], 'native.' + key)
    sdk = _declared_path(native['sdk_root'])
    for rel in ('llvm/bin/clang.exe', 'llvm/bin/clang++.exe', 'llvm/bin/ld.lld.exe',
                'llvm/bin/llvm-ar.exe', 'llvm/bin/llvm-ranlib.exe',
                'build-tools/cmake/bin/ninja.exe', 'build/cmake/ohos.toolchain.cmake',
                'llvm/lib/aarch64-linux-ohos/libc++_shared.so'):
        require(sdk / rel, 'native.sdk.' + rel)
    consistent(native['ninja'], sdk / 'build-tools/cmake/bin/ninja.exe', 'native.ninja')
    require(Path(native['platform_compiler']).parent / 'ld.lld.exe', 'native.platform_linker')
    host_prefix, native_prefix = _declared_path(host['prefix']), _declared_path(native['prefix'])
    host_dirs = {'fastdds_dir': host_prefix / 'share/fastdds/cmake',
                 'fastcdr_dir': host_prefix / 'lib/cmake/fastcdr',
                 'foonathan_dir': host_prefix / 'share/foonathan_memory/cmake'}
    for key, effective in host_dirs.items():
        consistent(host[key], effective, 'host.' + key)
    consistent(host['child_environment']['PATH_prefix'], host_prefix / 'bin', 'host.child.PATH_prefix')
    if host['child_environment'].get('PSModulePath_policy') != 'Machine environment value; child process only':
        raise ValueError('inconsistent PSModulePath policy')
    if os.name == 'nt':
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\Session Manager\Environment') as key:
            machine_modules = os.path.expandvars(winreg.QueryValueEx(key, 'PSModulePath')[0])
        normalize = lambda value: [str(_declared_path(item)).casefold() for item in value.split(';') if item]
        if normalize(host['psmodulepath']) != normalize(machine_modules):
            raise ValueError('inconsistent configured/effective PSModulePath')
    package_dirs = [('host.fastdds', host_dirs['fastdds_dir'], 'fastdds-config.cmake'),
                    ('host.fastcdr', host_dirs['fastcdr_dir'], 'fastcdr-config.cmake'),
                    ('host.foonathan', host_dirs['foonathan_dir'], 'foonathan_memory-config.cmake'),
                    ('native.fastdds', native_prefix / 'share/fastdds/cmake', 'fastdds-config.cmake'),
                    ('native.fastcdr', native_prefix / 'lib/cmake/fastcdr', 'fastcdr-config.cmake'),
                    ('native.foonathan', native_prefix / 'lib/foonathan_memory/cmake', 'foonathan_memory-config.cmake')]
    for purpose, directory, entrypoint in package_dirs:
        require(directory / entrypoint, purpose + '.entrypoint')
        for path in sorted(directory.glob('*.cmake')):
            require(path, purpose + '.import')
    for stem in ('fastdds-3.6', 'fastcdr-2.3', 'foonathan_memory-0.7.4'):
        require(host_prefix / 'bin' / (stem + '.dll'), 'host.package.runtime')
        require(host_prefix / 'lib' / (stem + '.lib'), 'host.package.import_library')
    for name in ('libfastdds.so.3.6.2.0', 'libfastcdr.so.2.3.5', 'libfoonathan_memory-0.7.4.so'):
        require(native_prefix / 'lib' / name, 'native.package.runtime')
    return anchors


def preflight(config, checkpoint, output, allow_owned=False):
    if config.get('schema') != 1:
        raise ValueError('dependency config schema')
    checkpoint = canonical(checkpoint)
    verified = sealed.verify_checkpoint(checkpoint)
    inventory = read_json(checkpoint / 'inventory.json')
    if config.get('expected_inventory_sha256') != verified['inventory_sha256']:
        raise ValueError('configured checkpoint inventory hash mismatch')
    baseline = config['baseline']
    for record in config.get('hash_manifests', []):
        if hash_file(record['path']) != record['sha256']:
            raise ValueError('dependency declaration manifest hash mismatch')
    if config.get('checkpoint_tool', {}).get('sha256') != TOOL_HASH or hash_file(config['checkpoint_tool']['path']) != TOOL_HASH:
        raise ValueError('declared sealed restore tool identity mismatch')
    if not HEAD.fullmatch(baseline['head']):
        raise ValueError('baseline requires full pinned HEAD')
    for rel in baseline['export_paths'] + baseline.get('deleted_paths', []):
        sealed.safe_relative(rel)
    host = config['host']
    if host['generator'] != 'Visual Studio 17 2022' or host['architecture'] != 'x64' or host['configuration'] != 'Release':
        raise ValueError('unsupported Host ABI/configuration')
    native = config['native']
    if native.get('restored_token_headers_relative', '.artifacts/reference/security_access_token') != '.artifacts/reference/security_access_token':
        raise ValueError('token source must be restored checkpoint headers')
    protected = [baseline['repository'], inventory['source_root'], checkpoint,
                 host['prefix'], native['sdk_root'], native['prefix'], Path(native['platform_compiler']).parent]
    output = validate_output(output, protected, allow_owned)
    declarations = {}
    for item in config['files']:
        path = _declared_path(item['path'])
        if item.get('classification') != 'DECLARED_EXTERNAL_DEPENDENCY':
            raise ValueError('forbidden or unknown external dependency class: ' + str(path))
        key = str(path).casefold()
        if key in declarations and declarations[key] != item['sha256']:
            raise ValueError('conflicting external hash: ' + str(path))
        if not SHA.fullmatch(item['sha256']) or not path.is_file() or hash_file(path) != item['sha256']:
            raise ValueError('external file missing/hash mismatch: ' + str(path))
        declarations[key] = item['sha256']
    anchors = selected_dependency_anchors(config, declarations)
    overlay = config['overlay']
    manifest_path = canonical(overlay['manifest'])
    if hash_file(manifest_path) != overlay['sha256']:
        raise ValueError('overlay manifest hash mismatch')
    manifest = read_json(manifest_path)
    read_overlay(overlay['root'], manifest)
    if output.exists():
        owner = read_json(output / MARKER)
        if owner.get('checkpoint_inventory_sha256') != verified['inventory_sha256'] or owner.get('dependencies_sha256') != config['_config_sha256']:
            raise ValueError('existing delivery ownership/config mismatch')
    return {'schema': 1, 'status': 'PREFLIGHT_PASS', 'checkpoint': verified,
            'declared_unique_files': len(declarations), 'baseline_head': baseline['head'],
            'selected_dependency_anchors': anchors,
            'output_root': str(output), 'overlay_manifest_sha256': overlay['sha256'],
            'not_hermetic': True, 'runtime_device_execution': 'NOT_RUN'}


def invoke(argv, cwd, logbase, env=None):
    logbase = Path(logbase)
    logbase.parent.mkdir(parents=True, exist_ok=True)
    write_json(logbase.with_suffix('.command.json'), {'argv': [str(arg) for arg in argv], 'cwd': str(cwd),
        'started_utc': datetime.now(timezone.utc).isoformat(), 'shell': False})
    with logbase.with_suffix('.stdout.log').open('xb') as stdout, logbase.with_suffix('.stderr.log').open('xb') as stderr:
        result = subprocess.run([str(arg) for arg in argv], cwd=str(cwd), env=env, stdout=stdout, stderr=stderr, shell=False)
    write_json(logbase.with_suffix('.result.json'), {'exit_code': result.returncode})
    if result.returncode:
        raise RuntimeError(f'command failed exit={result.returncode}: {logbase}')
    return logbase.with_suffix('.stdout.log')


def restore(config, checkpoint, output):
    checked = preflight(config, checkpoint, output)
    checkpoint, output = canonical(checkpoint), canonical(output)
    baseline = config['baseline']
    overlay = config['overlay']
    output.mkdir(parents=True, exist_ok=False)
    write_json(output / MARKER, {'schema': 1, 'checkpoint_inventory_sha256': checked['checkpoint']['inventory_sha256'],
        'dependencies_sha256': config['_config_sha256']})
    write_json(output / 'preflight.json', checked)
    logs = output / 'restore-logs'
    observed = invoke([config['git'], '-C', baseline['repository'], 'rev-parse', '--verify', baseline['head'] + '^{commit}'], output, logs / 'baseline-commit')
    if observed.read_text(encoding='ascii').strip() != baseline['head']:
        raise ValueError('baseline commit resolution mismatch')
    archive = output / 'baseline.tar'
    invoke([config['git'], '-C', baseline['repository'], 'archive', '--format=tar', '--output=' + str(archive), baseline['head'], '--', *baseline['export_paths']], output, logs / 'baseline-export')
    baseline_dir = output / 'baseline'
    baseline_dir.mkdir()
    with tarfile.open(archive, 'r:') as tar:
        members = tar.getmembers()
        validate_archive_members(members)
        for member in members:
            target = baseline_dir / member.name
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as stream:
                    stream.write(tar.extractfile(member).read())
    source = output / 'src'
    restoration = sealed.restore_checkpoint(checkpoint, source)
    write_json(output / 'checkpoint-file-restore.json', restoration)
    inventory = read_json(checkpoint / 'inventory.json')
    origins = {item['path']: {'path': item['path'], 'sha256': item['sha256'], 'origin': 'CHECKPOINT'} for item in inventory['files']}
    deleted = set(baseline.get('deleted_paths', []))
    overlap = []
    for rel in sealed.payload_paths(baseline_dir):
        if rel in deleted:
            continue
        prior = baseline_dir / rel
        target = source / rel
        if target.exists():
            overlap.append({'path': rel, 'baseline_sha256': hash_file(prior), 'checkpoint_sha256': hash_file(target), 'selected': 'CHECKPOINT'})
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                stream.write(prior.read_bytes())
            origins[rel] = {'path': rel, 'sha256': hash_file(target), 'origin': 'GIT_BASELINE'}
    overlay_events = []
    for item, data in read_overlay(overlay['root'], read_json(overlay['manifest'])):
        target = source / item['path']
        previous = hash_file(target) if target.exists() else None
        if item.get('origin_sha256') is not None and item['origin_sha256'] != previous:
            raise ValueError('overlay expected previous source hash mismatch: ' + item['path'])
        target.parent.mkdir(parents=True, exist_ok=True)
        # This replacement is only within the newly owned isolated composition.
        target.write_bytes(data)
        origins[item['path']] = {'path': item['path'], 'sha256': item['sha256'], 'origin': 'DELIVERY_OVERLAY'}
        overlay_events.append({'path': item['path'], 'previous_sha256': previous, 'new_sha256': item['sha256']})
    receipt = {'schema': 1, 'kind': 'RESTORED_PROJECT_SOURCE', 'source_root': str(source),
        'baseline_head': baseline['head'], 'checkpoint_inventory_sha256': checked['checkpoint']['inventory_sha256'],
        'overlay_manifest_sha256': overlay['sha256'], 'files': sorted(origins.values(), key=lambda v: v['path'].casefold())}
    validate_source(receipt, source)
    write_json(output / SOURCE_RECEIPT, receipt)
    write_json(output / 'source-composition.json', {'baseline_overlap': overlap, 'skipped_baseline_deleted': sorted(deleted),
        'overlay': overlay_events, 'source_receipt_sha256': hash_file(output / SOURCE_RECEIPT), 'source_count': len(origins)})
    return {'status': 'RESTORED_PROJECT_SOURCE_VERIFIED', 'source': str(source), 'files': len(origins),
            'source_receipt_sha256': hash_file(output / SOURCE_RECEIPT)}


def child_environment(config):
    env = os.environ.copy()
    prepend = [str(Path(config['host']['prefix']) / 'bin')]
    prepend += [str(Path(config[name]).parent) for name in ('python', 'node', 'cmake', 'powershell')]
    env['PATH'] = os.pathsep.join(prepend + [env.get('PATH', '')])
    if os.name == 'nt':
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r'SYSTEM\CurrentControlSet\Control\Session Manager\Environment') as key:
            env['PSModulePath'] = os.path.expandvars(winreg.QueryValueEx(key, 'PSModulePath')[0])
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    return env


def host_commands(config, source, build, mode):
    prefix = Path(config['host']['prefix'])
    configure = [config['cmake'], '-S', str(source), '-B', str(build), '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_CONFIGURATION_TYPES=Release', '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL',
        '-DMDDS_FASTDDS_MODE=CONFIG', '-DBUILD_TESTING=ON', '-DBUILD_FASTDDS_DSOFTBUS_EXPERIMENT=ON',
        '-DMDDS_EXPERIMENT_NATIVE=OFF', '-DMDDS_EXPERIMENT_RX_DROP=' + mode.upper(),
        '-DBUILD_SOFTBUS_CAPABILITY_PROBE=ON', '-DBUILD_NATIVE_PERMISSION_PROBE=OFF',
        '-Dfastdds_DIR=' + str(prefix / 'share/fastdds/cmake'), '-Dfastcdr_DIR=' + str(prefix / 'lib/cmake/fastcdr'),
        '-Dfoonathan_memory_DIR=' + str(prefix / 'share/foonathan_memory/cmake'),
        '-DPython3_EXECUTABLE=' + config['python'], '-DRECORD_HARNESS_NODE=' + config['node'],
        '-DPERMISSION_POWERSHELL=' + config['powershell']]
    build_cmd = [config['cmake'], '--build', str(build), '--config', 'Release', '--parallel', '3', '--', '/v:diag']
    return configure, build_cmd


def native_command(config, source, source_receipt, mode):
    native = config['native']
    args = [config['powershell'], '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(source / 'scripts/BuildFastDdsSocketExperiment.ps1'),
        '-OutputDirectory', '.artifacts/native-' + mode, '-RestoredSourceReceipt', str(source_receipt),
        '-SdkRoot', native['sdk_root'], '-Prefix', native['prefix'], '-PlatformCompiler', native['platform_compiler'],
        '-AccessTokenSource', str(source / '.artifacts/reference/security_access_token'),
        '-TokenSetprocLibrary', native['token_setproc_library'], '-AccessTokenLibrary', native['access_token_library'],
        '-PlatformCxxLibrary', native['platform_cxx_library']]
    if mode == 'on':
        args.append('-EnableRxDrop')
    return args


def build_run(config, checkpoint, output, kind, mode, review=None, dry_run=False):
    checked = preflight(config, checkpoint, output, allow_owned=True)
    output = canonical(output)
    source, receipt_path = output / 'src', output / SOURCE_RECEIPT
    receipt = read_json(receipt_path)
    generated = [source / '.artifacts' / ('native-' + each) for each in ('off', 'on')]
    validate_source(receipt, source, generated, prior_generated_test_files(output))
    build = output / ('host-' + mode) if kind == 'host' else source / '.artifacts' / ('native-' + mode)
    if build.exists():
        raise ValueError('existing build target; choose a fresh restore output root')
    logs = output / (kind + '-' + mode + '-logs')
    if logs.exists():
        raise ValueError('existing build receipt; failed evidence preserved')
    if kind == 'native':
        if review is None:
            raise ValueError('native requires --review-receipt')
        validate_review(read_json(review), hash_file(receipt_path))
        for each in ('off', 'on'):
            host_receipt = read_json(output / ('host-' + each + '-logs') / 'summary.json')
            if host_receipt.get('status') != 'PASS' or host_receipt.get('source_receipt_sha256') != hash_file(receipt_path):
                raise ValueError('restored host receipt does not establish this source')
        commands = [native_command(config, source, receipt_path, mode)]
    else:
        commands = list(host_commands(config, source, build, mode))
        commands += [[config['ctest'], '--test-dir', str(build), '-C', 'Release', '--show-only=json-v1'],
                     [config['ctest'], '--test-dir', str(build), '-C', 'Release', '--output-on-failure', '--output-junit', str(logs / 'ctest.xml')]]
    if dry_run:
        return {'status': 'DRY_RUN', 'commands': commands, 'preflight': checked}
    logs.mkdir(parents=True)
    source_hash = hash_file(receipt_path)
    write_json(logs / 'preflight.json', checked)
    result = {'schema': 1, 'status': 'FAIL', 'kind': kind, 'mode': mode, 'source_receipt_sha256': source_hash,
              'first_failure': None, 'device_runtime': 'NOT_RUN', 'binary_reproducibility': 'NOT_ESTABLISHED'}
    try:
        env = child_environment(config)
        if kind == 'host':
            query = build / '.cmake/api/v1/query/client-restored-delivery'
            query.mkdir(parents=True)
            for name in ('cmakeFiles-v1', 'codemodel-v2', 'cache-v2', 'toolchains-v1'):
                (query / name).write_bytes(b'')
            phases = ('configure', 'build', 'discovered', 'tests')
        else:
            phases = ('native-build',)
        for phase, command in zip(phases, commands):
            result['current_phase'] = phase
            invoke(command, source, logs / phase, env)
        if kind == 'host':
            generated_files = capture_generated_test_files(receipt, source)
            write_json(logs / 'generated-test-files.json', {'schema': 1, 'files': generated_files,
                'meaning': 'Exact outputs of sealed local CTest fixtures; not project/build source inputs'})
        else:
            generated_files = prior_generated_test_files(output)
        validate_source(receipt, source, generated, generated_files)
        if kind == 'host':
            discovered = read_json(logs / 'discovered.stdout.log')
            import xml.etree.ElementTree as ET
            suites = ET.parse(logs / 'ctest.xml').getroot()
            cases = list(suites.iter('testcase'))
            counts = {'discovered': len(discovered['tests']), 'executed': len(cases),
                      'failed': sum(case.find('failure') is not None for case in cases),
                      'skipped': sum(case.find('skipped') is not None for case in cases)}
            counts['passed'] = counts['executed'] - counts['failed'] - counts['skipped']
            counts['not_run'] = counts['discovered'] - counts['executed']
            result['tests'] = counts
            if counts['failed'] or counts['skipped'] or counts['not_run'] or not counts['discovered']:
                raise ValueError('full restored suite contains failed/skipped/not-run tests')
        else:
            manifest = build / 'build-manifest.json'
            if not manifest.is_file():
                raise ValueError('native build manifest missing')
            result['build_manifest'] = str(manifest)
            result['build_manifest_sha256'] = hash_file(manifest)
        result['status'] = 'PASS'
    except Exception as error:
        result['first_failure'] = {'stage': result.get('current_phase', 'prepare'), 'error': str(error)}
        raise
    finally:
        write_json(logs / 'summary.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('preflight', 'restore', 'host', 'native'))
    parser.add_argument('--checkpoint', type=Path, required=True)
    parser.add_argument('--dependencies', type=Path, required=True)
    parser.add_argument('--output-root', type=Path, required=True,
        help='Fresh absent directory for restore; later builds require its existing delivery-owner.json. Put CLI capture logs elsewhere.')
    parser.add_argument('--mode', choices=('off', 'on'))
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--review-receipt', type=Path)
    args = parser.parse_args()
    try:
        config = read_json(args.dependencies)
        config['_config_sha256'] = hash_file(args.dependencies)
        if args.command == 'preflight' or (args.command == 'restore' and args.dry_run):
            result = preflight(config, args.checkpoint, args.output_root, allow_owned=args.command == 'preflight')
            result['dry_run'] = args.dry_run
        elif args.command == 'restore':
            result = restore(config, args.checkpoint, args.output_root)
        else:
            if args.mode is None:
                raise ValueError('--mode is required for build stages')
            result = build_run(config, args.checkpoint, args.output_root, args.command, args.mode, args.review_receipt, args.dry_run)
        print(json.dumps(result, indent=2))
        return 0
    except Exception as error:
        print(json.dumps({'status': 'FAIL', 'command': args.command, 'error': str(error)}, indent=2), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
