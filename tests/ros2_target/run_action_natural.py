"""Natural stock Action windows on the immutable published R30 runtime package.

This is a separate test admission path. It does not alter old qualification
receipts, the runtime package, public Action semantics, or Transport code.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import secrets
import shlex
import shutil
import subprocess
import time
from types import SimpleNamespace

import run_action_restart as R30

P = R30.P
ORDINARY, NATIVE, BASE, BAG = P.ORDINARY, P.NATIVE, P.BASE, P.BAG
ROOT, REPO = P.ROOT, P.REPO
EVIDENCE = ROOT / 'build/r31-action-natural'
OUTPUT_ROOT = EVIDENCE / 'current-stack-windows'
SCOPE = 'R31_CURRENT_R30_PACKAGE_NATURAL_ACTION_AB_BA'
CASES = ('action_send_goal_AB', 'action_send_goal_BA')
BRIDGE_SHA = '9db2a35c7da2cb98afa2c255ab122179e5a959a9d4a39515b11881ffe98039c1'
PUBLISHED = 'a8945d4520af23f476ac93f9c376668b4d397481'
LIMITS = {'business_seconds': 35, 'probe_seconds': 40, 'normal_close_release_seconds': 50,
          'final_exit_seconds': 20, 'analysis_seconds': 60,
          'query_construct_seconds': 300, 'query_dispatch_seconds': 240}
PINS = {
    'final_device': ('build/r30-action-restart/device1-final-audit/RESULT.json', 'c3e725e6ca3de342fbeea1fced2dd55c2a46dd50ff6826d097e716152fa81202'),
    'device_seal': ('build/r30-action-restart/device1-final-audit/SEAL.json', '60ccea1ef8984476ee46fa17dfc10b3f87dbc361eb0c97b142bf17e2d92f2b27'),
    'final_host': ('build/r30-action-restart/final-tester/RESULT.json', '6a6710ed313c06c01ac93fec87f92747baf060109a50bccac62a945276206a6f'),
    'build': ('build/r30-action-restart/final-tester/BUILD_RECEIPT.json', '4333cde06477f75d44092f7e201617750c1dbd4968dc826bdcf9601b8e84375e'),
    'native_reuse': ('build/r30-action-restart/final-tester/NATIVE_REUSE.json', 'caa68158cee27917846b4852b771a667083eb0cab1791d51387b409c209538f9'),
    'derivation': ('build/r30-action-restart/package-1eee6bd720e559ee/DERIVATION.json', 'ac22627dd7c1597c2e3d52c35e6b85506be83ed2188bd8f86dcc48de201833ea'),
    'publication': ('build/r30-action-restart/main-action-restart-publication/POST_PUBLISH.json', '44422a33e86ef2d7670aec539d0c7e74a32b5e265b0ef4ddc0316754cfd999f5'),
}
SOURCES = (Path(__file__), Path(__file__).with_name('test_action_natural.py'))
require, sha = R30.require, P.sha


def read_json_text(text):
    return json.loads(text, object_pairs_hook=ORDINARY.ordinary_unique)


def read(path):
    return read_json_text(Path(path).read_text(encoding='utf-8-sig'))


def descriptor(path):
    return {'path': str(Path(path).resolve(strict=True)), 'sha256': sha(path)}


def checked_descriptor(row, bindings):
    require(isinstance(row, dict) and set(row) == {'path', 'sha256'} and
            isinstance(row['sha256'], str) and re.fullmatch('[0-9a-f]{64}', row['sha256']),
            'Exact file descriptor required')
    path = BASE.runtime_path(row['path'], ROOT / 'build')
    require(path.is_file() and sha(path) == row['sha256'], 'Selected descriptor changed')
    bindings[str(path)] = row['sha256']
    return path


def validate_selection_shape(value):
    require(isinstance(value, dict) and set(value) == {'schema', 'case', 'ordinary_inputs', 'routes'} and
            type(value['schema']) is int and value['schema'] == 1 and value['case'] in CASES,
            'Only exact R31 natural Action selections are supported')


def checked_output(path, mode, prepared=None):
    """Reject writes outside fresh, dedicated evidence before any admission."""
    output = BASE.runtime_path(path, EVIDENCE, exists=False)
    root = OUTPUT_ROOT.resolve()
    prefix = {'prepare': 'prepared', 'run': 'execute', 'analyze': 'analyze'}.get(mode)
    if prefix:
        require(output.parent == root and re.fullmatch(prefix + '-[0-9a-f]{16}', output.name),
                'Dedicated current-stack output required')
    else:
        require(mode == 'validation' and output.name == 'validation' and
                output.parent.parent == root and re.fullmatch('prepared-[0-9a-f]{16}', output.parent.name) and
                prepared is not None and Path(prepared).resolve(strict=True) == output.parent / 'PLAN.json',
                'Exact prepared validation output required')
    require(not output.exists(), 'Fresh current-stack output required')
    return output


def current_package(bindings):
    receipts = {}
    for name, (relative, digest) in PINS.items():
        path = checked_descriptor({'path': str(ROOT / relative), 'sha256': digest}, bindings)
        receipts[name] = read(path)
    host, reused, publication = (receipts[k] for k in ('final_host', 'native_reuse', 'publication'))
    require(host['status'] == 'PASS_FINAL_HOST' and host['package_reusable_for_device_plan'] is True and
            reused['status'] == 'PASS' and publication['status'] == 'PASS' and publication['head'] == PUBLISHED,
            'Current published R30 qualification missing')
    device = receipts['final_device']
    require(device['status'] == device['commit_gate'] == 'PASS' and
            device['device_tested'] is True and device['cross_device_tested'] is True and
            device['package'] == {'path': str(ROOT / PINS['derivation'][0]), 'sha256': PINS['derivation'][1]} and
            device['final_host']['sha256'] == PINS['final_host'][1], 'Canonical R30 device qualification differs')
    seal_root = (ROOT / PINS['device_seal'][0]).parent
    members = receipts['device_seal']['files']
    require(isinstance(members, dict) and len(members) == 213, 'Complete canonical device seal required')
    for name, digest in members.items():
        member = BASE.runtime_path(seal_root / name, seal_root)
        checked_descriptor({'path': str(member), 'sha256': digest}, bindings)
    deps = reused['compiler_dependencies']
    dep_path = checked_descriptor({'path': deps['path'], 'sha256': deps['sha256']}, bindings)
    inventory = read(dep_path)
    require(deps['count'] == len(inventory) == 897 and all(sha(p) == h for p, h in inventory.items()),
            'Current native compiler dependency bytes differ')
    source_path = checked_descriptor(host['source_inventory'], bindings)
    sources = read(source_path)
    require(len(sources) == 270 and all(sha(p) == h for p, h in sources.items()), 'Current R30 source bytes differ')
    bindings.update(sources)
    derivation = ROOT / PINS['derivation'][0]
    package, files = R30.checked_package(derivation)
    require(len(files) == 3236 and str(package) == host['package'] and files[P.BRIDGE] == BRIDGE_SHA and
            receipts['derivation']['build_receipt_sha256'] == PINS['build'][1], 'Exact R30 package required')
    require(sha(P.S.PARENT / 'RUNTIME_PACKAGE.json') == P.S.PARENT_SHA, 'R30 parent composition changed')
    bindings[str(P.S.PARENT / 'RUNTIME_PACKAGE.json')] = P.S.PARENT_SHA
    return package, files


def current_elf_closure(package, files, bindings, external):
    parent = read(P.S.PARENT / 'RUNTIME_PACKAGE.json')
    closure_path = checked_descriptor({'path': str(P.S.PARENT / 'ELF_CLOSURE.json'),
                                       'sha256': parent['report_sha256']['ELF_CLOSURE.json']}, bindings)
    closure = read(closure_path)
    elf_names = set()
    for name in files:
        with (package / name).open('rb') as stream:
            if stream.read(4) == b'\x7fELF':
                elf_names.add(name)
    require(elf_names == set(closure['objects']), 'Current package ELF set differs from qualified parent')
    for name, row in closure['objects'].items():
        require(files[name] == (BRIDGE_SHA if name == P.BRIDGE else row['sha256']), 'Unqualified ELF payload delta')
        require(set(closure['routes'][name]) == set(row['needed']), 'Incomplete parent ELF dependency routes')
        for edge in closure['routes'][name].values():
            if 'package' in edge:
                expected = BRIDGE_SHA if edge['package'] == P.BRIDGE else edge['sha256']
                require(files.get(edge['package']) == expected, 'Package dependency target changed')
            else:
                provider = edge['external']
                for side in 'AB':
                    rows = {r['canonical_absolute_path']: r for r in external['devices'][side]['providers']}
                    require(rows.get(provider['target_path'], {}).get('sha256') == provider['sha256'],
                            'Unqualified external dependency target')
    tool = Path(NATIVE.HDCPATH).parent.parent / 'native/llvm/bin/llvm-readelf.exe'
    require(tool.is_file(), 'Selected SDK23 readelf unavailable')
    bindings[str(tool.resolve())] = sha(tool)
    argv = [str(tool), '-h', '-d', '--wide', str(package / P.BRIDGE)]
    result = subprocess.run(argv, capture_output=True, timeout=30, check=False)
    require(result.returncode == 0 and not result.stderr, 'Current bridge ELF inspection failed')
    text = result.stdout.decode('utf-8')
    require(re.search(r'Machine:\s+AArch64\b', text) and re.search(r'Class:\s+ELF64\b', text),
            'Current bridge target architecture differs')
    tags = {kind: re.findall(r'\(' + kind + r'\)[^\n]*\[([^\]]+)\]', text)
            for kind in ('NEEDED', 'SONAME', 'RPATH', 'RUNPATH')}
    previous = closure['objects'][P.BRIDGE]
    require(tags['NEEDED'] == previous['needed'] and tags['SONAME'] == previous['soname'] and
            tags['RPATH'] + tags['RUNPATH'] == previous['search_paths'], 'Current bridge loader graph differs')
    return {'scope': 'HASH_INHERITED_PARENT_ELF_GRAPH_WITH_CURRENT_BRIDGE_REINSPECTION',
            'parent_closure': descriptor(closure_path), 'objects': len(elf_names), 'bridge': tags,
            'readelf_argv': argv, 'readelf_exit': result.returncode,
            'readelf_stdout_sha256': hashlib.sha256(result.stdout).hexdigest(),
            'target_loaded_maps': 'REQUIRED_AT_EXECUTION'}


def prepare(args):
    """Requalify original public API inputs and explicitly map the R30 derivative."""
    output = None if getattr(args, 'validate_only', False) else checked_output(args.output, 'prepare')
    bindings = {}
    selection_path = BASE.runtime_path(args.inputs, ROOT / 'build')
    selection = read(selection_path)
    validate_selection_shape(selection)
    ordinary_input = checked_descriptor(selection['ordinary_inputs'], bindings)
    comparison = ORDINARY.ordinary_prepare(SimpleNamespace(inputs=ordinary_input, output=args.output, validate_only=True))
    require(comparison['case'] == selection['case'], 'Comparison direction differs')
    bindings.update(comparison['bindings'])
    package, files = current_package(bindings)
    old_package = Path(comparison['package'])
    # The full old prepare verifies readiness, owned-Python/ELF provenance and
    # recursively closed providers. Only the following explicitly qualified
    # R30 package mapping replaces old whole-inventory equality.
    api_members = {'actionbag_python', 'run-token-first', 'scripts/actionbag_runtime_probe.py',
                   'lib/libpython3.12.so.1.0', 'lib/libfastrtps.so.2.14', 'lib/libfastcdr.so.2',
                   'lib/librmw_fastrtps_cpp.so', 'lib/librmw_fastrtps_shared_cpp.so',
                   'python/ros2action/verb/send_goal.py'}
    rclpy = {name for name in comparison['files'] if name.startswith('python/rclpy/')}
    require(len(rclpy) == 64 and all(files.get(name) == comparison['files'][name] for name in api_members | rclpy),
            'Current package changed qualified public API/fixture/provider bytes')
    require(comparison['inputs']['external']['sha256'] == BAG.EXTERNAL_SHA and
            comparison['inputs']['identity']['sha256'] == BAG.IDENTITY_SHA, 'R30 provider/identity selection differs')
    external = checked_descriptor(comparison['inputs']['external'], bindings)
    identity = checked_descriptor(comparison['inputs']['identity'], bindings)
    old_external = read(comparison['inputs']['external']['path'])
    current_external = read(external)
    # Every previously admitted external object must retain its exact ELF
    # identity. Extra parent-package candidates come only from the pinned R30
    # closure; actual loaded maps/hashes are still checked on both boards.
    for side in 'AB':
        old_rows = {row['canonical_absolute_path']: row for row in old_external['devices'][side]['providers']}
        new_rows = {row['canonical_absolute_path']: row for row in current_external['devices'][side]['providers']}
        require(all(new_rows.get(name) == row for name, row in old_rows.items()), 'External provider qualification drift')
    elf_closure = current_elf_closure(package, files, bindings, current_external)
    routes = checked_descriptor(selection['routes'], bindings)
    route_data = read(routes)
    require(route_data.get('cross_confirmed') is True and route_data.get('bindings'), 'Paired query required')
    for path, digest in route_data['bindings'].items():
        require(sha(path) == digest, 'Original route evidence changed')
        bindings[path] = digest
    for path in SOURCES:
        bindings[str(path.resolve())] = sha(path)
    selected_file = descriptor(selection_path)
    bindings[selected_file['path']] = selected_file['sha256']
    result = {'schema': 1, 'scope': 'FIXED_ORDINARY_CASE_PREPARED_NO_DEVICE_DISPATCH',
              'qualification_scope': SCOPE, 'run': secrets.token_hex(8), 'case': selection['case'],
              'package': str(package), 'files': files, 'bindings': bindings,
              'inputs': {**{k: comparison['inputs'][k] for k in ('design_plan', 'readiness')},
                         'external': descriptor(external), 'identity': descriptor(identity), 'routes': descriptor(routes)},
              'case_definition': comparison['case_definition'], 'selection_file': selected_file,
              'deadlines_seconds': comparison['deadlines_seconds'], 'limits': LIMITS,
              'current_stack': {'published': PUBLISHED, 'derivation': descriptor(ROOT / PINS['derivation'][0]),
                                'bridge_sha256': BRIDGE_SHA, 'package_members': 3236,
                                'A_backend': backend_fields('A', '/data/local/tmp/placeholder_A')['backend'],
                                'B_backend': backend_fields('B', '/data/local/tmp/placeholder_B')['backend'],
                                'native_build': 'REUSED_HASH_VERIFIED_897_DEPENDENCIES',
                                'elf_closure': elf_closure,
                                'comparison_package': str(old_package)}, 'sdk_epoch': 'NOT_PROVEN'}
    if getattr(args, 'validate_only', False):
        return result
    output.mkdir(parents=True)
    R30.write_json(output / 'PLAN.json', result)
    NaturalActionRunner(SimpleNamespace(mode='validation', prepared=output / 'PLAN.json', output=output / 'validation'))
    print('R31_CURRENT_ACTION_PREPARED_NO_DEVICE ' + str(output / 'PLAN.json'))
    return 0


def require_prepared_equal(prepared, checked):
    require(isinstance(prepared, dict) and isinstance(prepared.get('run'), str) and
            re.fullmatch('[0-9a-f]{16}', prepared['run']) and set(prepared) == set(checked) and
            all(prepared[k] == checked[k] for k in checked if k != 'run'), 'Prepared current-stack plan differs')


def backend_fields(side, root):
    require(side in ('A', 'B'), 'Physical side required')
    if side == 'A':
        return {'backend': 'OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL',
                'native_reaccept_order': 'outgoing-first',
                'native_reaccept_control': root + '/control.NATIVE_REACCEPT', 'process_restart_observation': '1'}
    return {'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL'}


def check_no_replacement(texts):
    for text in texts.values():
        require(not re.search(r'NATIVE_(?:REACCEPT_(?:REFUSAL|EVENT)|PROCESS_OWNERS|REPEAT_|ROUTE_EVENT)', text),
                'Unrequested replacement/control observation or refusal')


class NaturalActionRunner(ORDINARY.OrdinaryCliRunner):
    def __init__(self, args):
        require(not hasattr(args, 'validation_probe'), 'Caller cannot select legacy validation bypass')
        output = checked_output(args.output, getattr(args, 'mode', 'run'), getattr(args, 'prepared', None))
        prepared = read(args.prepared)
        selected = checked_descriptor(prepared['selection_file'], {})
        checked = prepare(SimpleNamespace(inputs=selected, output=output, validate_only=True))
        require_prepared_equal(prepared, checked)
        # Full new admission above replaces only old whole-package admission.
        # The frozen constructor still validates every prepared binding,
        # inventory, identity, route byte and original query timestamp.
        internal = SimpleNamespace(**vars(args), validation_probe=True)
        super().__init__(internal)

    def analyze(self, texts):
        check_no_replacement(texts)
        super().analyze(texts)
        self.write('CURRENT_STACK.json', {'scope': SCOPE, 'published': PUBLISHED,
                   'package': self.plan['current_stack'], 'no_replacement_requested': True,
                   'natural_restart': 'NOT_VERIFIED', 'compatibility_status': 'OPEN_UNCOORDINATED_NOT_FIXED'})

    def execute(self):
        # Keep inherited deployment/business/cleanup. Bound the otherwise
        # unbounded legacy analysis phase without changing business deadlines.
        try:
            self.deploy()
            self.workflow()
            self.stage, self.stage_deadline = 'ANALYZE', time.monotonic() + LIMITS['analysis_seconds']
            self.analyze(self.collect())
            if time.monotonic() >= self.stage_deadline:
                raise ValueError('Current-stack analysis exceeded60s')
        except ValueError as error:
            self.errors.append(self.stage + ': ' + str(error))
        except (Exception, KeyboardInterrupt) as error:
            self.blocked.append(self.stage + ': ' + repr(error))
        finally:
            self.cleanup(bool(self.errors or self.blocked))
            if self.errors or self.blocked:
                try:
                    if self.owned:
                        self.collect()
                except (OSError, ValueError) as error:
                    self.blocked.append('FAILURE_CAPTURE: ' + str(error))
            result = {'run': self.run, 'case': self.case,
                      'result': 'FAIL' if self.errors else 'BLOCKED' if self.blocked else 'PASS',
                      'failures': self.errors, 'blocked': self.blocked, 'identity_mutations': 0,
                      'device_window': self.prefix, 'directories_retained': True, 'sdk_epoch': 'NOT_PROVEN',
                      'source_sha256': sha(__file__), 'qualification_scope': SCOPE,
                      'current_stack': self.plan['current_stack'], 'natural_restart': 'NOT_VERIFIED'}
            self.write('RESULT.json', result)
        return 0 if result['result'] == 'PASS' else 1

    def configure(self, side):
        peer = 'B' if side == 'A' else 'A'
        root, package = self.prefix[side], self.prefix[side] + '/package'
        service = 'com.kaihong.mdds.r31.' + self.run
        fields = {'bridge_library': package + '/lib/libfastdds_dsoftbus_ros2_target_bridge.so',
            'fastdds_module': package + '/lib/libfastrtps.so.2.14', 'fastcdr_module': package + '/lib/libfastcdr.so.2',
            'softbus_module': '/system/lib64/platformsdk/libsoftbus_client.z.so', 'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            'run': self.run, 'side': str('AB'.index(side)), 'domain': '201', 'authorized_package': 'com.kaihong.mdds',
            'peer_network_id': self.routes[peer]['local'], 'service_prefix': service, 'connect_release': root + '/control.LISTENER_READY',
            'locator_version': 'V1', 'namespace': 'mdds.r25.action', 'local_udid': self.udids[side], 'peer_udid': self.udids[peer],
            'service_slot': str('AB'.index(side) + 1), 'peer_slot': str('AB'.index(peer) + 1), 'profile': 'BYTES_NATIVE',
            'native_name_contract': 'PACKAGE_SCOPED_V1', 'local_initiator_name': service + '.' + side + '.tx',
            'peer_initiator_name': service + '.' + peer + '.tx', 'runtime_close_release': root + '/control.RUNTIME_CLOSE'}
        fields.update(backend_fields(side, root))
        config = self.output / (side + '.private.conf')
        config.write_text(''.join(k + '=' + v + '\n' for k, v in fields.items()), encoding='utf-8', newline='\n')
        env = {'PATH': '/system/bin:/system/xbin', 'LD_LIBRARY_PATH': package + '/lib:' + ORDINARY.RUNTIME + '/lib',
            'AMENT_PREFIX_PATH': package, 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': '201',
            'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET', 'ROS_LOG_DIR': root + '/logs', 'RCUTILS_COLORIZED_OUTPUT': '0',
            'RCUTILS_LOGGING_USE_STDOUT': '0', 'FASTDDS_DSOFTBUS_REQUIRED_CONFIG': root + '/ros.conf',
            'FASTDDS_DSOFTBUS_NATIVE_TOKEN': str(self.tokens[side]), 'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED': '1',
            'FASTDDS_DSOFTBUS_NATIVE_IDENTITY': NATIVE.IDENTITY, 'FASTDDS_ENVIRONMENT_FILE': '', 'SKIP_DEFAULT_XML': '1'}
        argv, unused = ORDINARY.ordinary_cli_argv(self.case, self.run, side, root, self.design)
        command = 'env -i ' + ' '.join(shlex.quote(k + '=' + v) for k, v in env.items()) + ' ' + shlex.join(argv)
        script = ('umask 077\nset -C\ncd ' + root + ' || exit 91\nmkdir logs || exit 92\n' + command +
            ' >stdout 2>stderr &\nchild=$!\nprintf "%s\\n" "$child" >child.pid.tmp\nmv child.pid.tmp child.pid\n' +
            'wait "$child"\nrc=$?\nprintf "R25_CHILD_EXIT run=' + self.run + ' side=' + side +
            ' pid=%s rc=%s\\n" "$child" "$rc" >exit.tmp\nmv exit.tmp exit\nexit "$rc"\n')
        supervisor = self.output / (side + '.supervisor.sh')
        supervisor.write_text(script, encoding='utf-8', newline='\n')
        self.send(side, config, root + '/ros.conf')
        self.send(side, supervisor, root + '/supervisor.sh')
        self.shell(side, 'chmod 600 ' + root + '/ros.conf ' + root + '/supervisor.sh')
        remote = self.shell(side, 'sha256sum ' + shlex.quote(root + '/ros.conf')).decode().strip().split()
        require(remote == [sha(config), root + '/ros.conf'], 'Deployed current-stack configuration differs')
        self.write(side + '.current_config.json', {'fields': fields, 'remote_path': root + '/ros.conf',
                   'sha256': sha(config), 'remote_sha256_verified': True, 'replacement_permission': 'NEVER_GRANTED'})
        self.write(side + '.environment.json', env)
        self.write(side + '.dispatch.json', {'argv': argv, 'expected_exec_argv': self.expected_argv[side],
            'supervisor_sha256': hashlib.sha256(supervisor.read_bytes()).hexdigest(), 'config_sha256': hashlib.sha256(config.read_bytes()).hexdigest()})


def natural_cli_main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    preparation = sub.add_parser('prepare', help='Validate exact selected readiness/package/source inputs; no devices')
    preparation.add_argument('--inputs', type=Path, required=True)
    preparation.add_argument('--output', type=Path, required=True)
    for name in ('run', 'analyze'):
        command = sub.add_parser(name)
        command.add_argument('--prepared', type=Path, required=True)
        command.add_argument('--output', type=Path, required=True)
        if name == 'run': command.add_argument('--hdc', type=Path, default=Path(NATIVE.HDCPATH))
        else: command.add_argument('--run-evidence', type=Path, required=True)
    args = parser.parse_args(argv)
    if args.mode == 'prepare': return prepare(args)
    if args.mode == 'run' and str(args.hdc.resolve()).lower() != str(Path(NATIVE.HDCPATH).resolve()).lower():
        parser.error('Only the selected SDK23 HDC is permitted')
    runner = NaturalActionRunner.__new__(NaturalActionRunner)
    try:
        runner.__init__(args)
        if args.mode == 'run': return runner.execute()
        old = args.run_evidence.resolve()
        if json.loads((old / 'PLAN.json').read_text()) != runner.plan: raise ValueError('Run evidence/plan mismatch')
        runner.owned = {s: json.loads((old / (s + '.ownership.json')).read_text()) for s in 'AB'}
        execution = ORDINARY.ordinary_execution_verdict(old, runner.plan, runner.owned)
        runner.write('RECORDED_EXECUTION.json', execution)
        texts = {}
        for side in 'AB':
            for kind in ('stdout', 'stderr'):
                src, dst = old / (side + '.raw.' + kind + '.log'), runner.output / (side + '.raw.' + kind + '.log')
                shutil.copyfile(src, dst)
            texts[side] = (runner.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8') + '\n' + (runner.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8')
            external = {r['canonical_absolute_path']: r['sha256'] for r in runner.external['devices'][side]['providers']}
            for label in (('imports',) if runner.case == 'imports_cdr_pair' else ('listener', 'close')):
                map_file, hashes_file = side + '.' + label + '.maps', side + '.' + label + '.providers.json'
                raw = (old / map_file).read_text()
                observed = json.loads((old / hashes_file).read_text(), object_pairs_hook=ORDINARY.ordinary_unique)
                package = runner.prefix[side] + '/package'
                paths, anonymous = ORDINARY.ordinary_map_paths(raw, package, runner.files, external,
                    runner.case != 'imports_cdr_pair', runner.readiness['owned_libpython']['member'])
                expected = {p: runner.files[p[len(package) + 1:]] if p.startswith(package + '/') else external[p] for p in paths}
                if observed != {'sha256': expected, 'anonymous_mappings': anonymous}:
                    raise ValueError('Recorded actual maps/provider hash proof incomplete or changed')
                shutil.copyfile(old / map_file, runner.output / map_file)
                shutil.copyfile(old / hashes_file, runner.output / hashes_file)
        runner.analyze(texts)
        runner.write('RESULT.json', {**execution, 'content_analysis': 'PASS', 'device_execution': 'NONE',
            'recorded_evidence': str(old), 'case': runner.case, 'run': runner.run, 'qualification_scope': SCOPE, 'current_stack': runner.plan['current_stack']})
        return 0 if execution['result'] == 'SKIPPED' else 1
    except (ValueError, OSError, KeyError, TypeError) as error:
        if getattr(runner, 'owns_output', False) and not (runner.output / 'INPUT_FAILURE.json').exists():
            runner.write('INPUT_FAILURE.json', {'result': 'BLOCKED', 'reason': str(error), 'device_execution': 'NOT_DISPATCHED_BY_INPUT_VALIDATION'})
        raise


if __name__ == '__main__':
    raise SystemExit(natural_cli_main())
