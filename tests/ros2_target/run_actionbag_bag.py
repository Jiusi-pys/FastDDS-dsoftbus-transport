"""Finite native rosbag no-Node gate using frozen ordinary ownership primitives.

Prepare is local. Run requires an exact Main release after independent gates.
Record/play are deliberately not executable cases until separately implemented.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import secrets
import shlex
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]
ORDINARY_PATH = Path(__file__).with_name('run_actionbag_cli.py')
ORDINARY_SHA = '3568820a923ee3aebec056aece1f53c2582ed235837e13904c838c7fc35282b3'
if hashlib.sha256(ORDINARY_PATH.read_bytes()).hexdigest() != ORDINARY_SHA:
    raise ValueError('Frozen ordinary ownership implementation changed')
_spec = importlib.util.spec_from_file_location('bag_frozen_ordinary', ORDINARY_PATH)
ORDINARY = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ORDINARY)
NATIVE = ORDINARY.NATIVE
CASE = 'bag_imports_qos_pair'
EXTERNAL_SHA = 'd7299f754fd31eb3de9b9bd4de7814eba2789c0adc3dfb3751934dc669788018'
IDENTITY_SHA = '2846e008219116ef8edc00c14ade315955d0df12b609c8460cd200bf39b707df'
MODULES = ('_compression_options', '_message_definitions', '_reader', '_storage', '_writer', '_info', '_transport', '_reindexer')
SOURCE_PATHS = [Path(__file__), Path(__file__).with_name('test_actionbag_bag.py'), ORDINARY_PATH,
    ORDINARY.NATIVE_PATH, REPO / 'integrations/ros2/target/compose_actionbag_bag_runtime.py',
    REPO / 'integrations/ros2/target/bag_runtime_probe.py', ORDINARY.PROBE]
GATE_ROLES = {
    'composition_initial': ('initial', 'NEW_BAG_COMPOSITION_STATIC_AND_PROBE_PURE_ENVELOPE_STOCK_PARSER_ONLY', 'PASS_INITIAL_HOST_SCOPE'),
    'composition_review': ('review', 'BAG_STATIC_COMPOSITION_AND_ABI_PROBE_CODE_REVIEW', 'NO_ACTIONABLE_FINDINGS_FOUND'),
    'composition_final': ('postreview_host', 'BAG_STATIC_COMPOSITION_AND_PROBE_HOST_CONTROLS_ONLY', 'PASS_POSTREVIEW_HOST_SCOPE'),
    'harness_initial': ('initial', 'TWO_NEW_BAG_ABI_HARNESS_FILES_HOST_ONLY', 'PASS_INITIAL_HOST_SCOPE'),
    'harness_review': ('review', 'BAG_ABI_HARNESS_CODE_REVIEW', 'NO_ACTIONABLE_FINDINGS_FOUND'),
    'harness_final': ('postreview_host', 'BAG_ABI_HARNESS_POSTREVIEW_HOST_ONLY', 'PASS_POSTREVIEW_HOST_SCOPE'),
}


def bag_sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def bag_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=ORDINARY.ordinary_unique)


def bag_gate_results(gates, source_map, runtime_descriptor, freeze_descriptor, bindings):
    """Read the six producers themselves; a PASS wrapper is not a test result."""
    if not isinstance(gates, dict) or set(gates) != set(GATE_ROLES):
        raise ValueError('Independent composition and harness gate set incomplete')
    current = {Path(path).relative_to(REPO).as_posix(): digest for path, digest in source_map.items()}
    composition = {name: current[name] for name in (
        'integrations/ros2/target/compose_actionbag_bag_runtime.py', 'integrations/ros2/target/bag_runtime_probe.py')}

    def descriptor(value):
        if not isinstance(value, dict) or not isinstance(value.get('path'), str) or not re.fullmatch('[0-9a-f]{64}', value.get('sha256', '')):
            raise ValueError('Producer descriptor missing or malformed')
        path = Path(value['path']).resolve(strict=True)
        if bag_sha(path) != value['sha256']:
            raise ValueError('Producer descriptor bytes changed')
        bindings[str(path)] = value['sha256']
        return {'path': str(path), 'sha256': value['sha256']}

    runtime_binding, freeze_binding = descriptor(runtime_descriptor), descriptor(freeze_descriptor)
    results, selected, times = {}, {}, {}
    for name, expected in GATE_ROLES.items():
        gate = gates[name]
        if not isinstance(gate, dict) or set(gate) != {'status', 'result', 'bindings'} or gate['status'] != 'PASS' or not gate['bindings']:
            raise ValueError('Unclosed ABI gate: ' + name)
        for path, digest in gate['bindings'].items():
            descriptor({'path': path, 'sha256': digest})
        selected[name] = descriptor(gate['result'])
        result = bag_json(selected[name]['path'])
        if not isinstance(result, dict) or tuple(result.get(key) for key in ('stage', 'scope', 'status')) != expected:
            raise ValueError('Producer stage/scope/status differs: ' + name)
        expected_sources = composition if name.startswith('composition_') else current
        if result.get('source_freeze') != expected_sources:
            raise ValueError('Producer tested a different source set: ' + name)
        source_descriptor = descriptor(result.get('source_freeze_descriptor'))
        if name.startswith('composition_'):
            if bag_json(source_descriptor['path']).get('files') != composition:
                raise ValueError('Composition source freeze content differs')
        elif source_descriptor != freeze_binding:
            raise ValueError('Harness producer did not bind the current full freeze')
        if name.endswith('_review') and (result.get('blocking_findings') != [] or
                result.get('new_findings') != [] or result.get('source_fixes_required') is not False):
            raise ValueError('Producer review still has unresolved findings')
        if name == 'composition_initial':
            artifact = result.get('build_result')
        elif name != 'composition_review':
            artifact = result.get('runtime_receipt')
        else:
            artifact = runtime_descriptor  # Its initial_result below binds the selected build.
        if descriptor(artifact) != runtime_binding:
            raise ValueError('Producer verified a different runtime: ' + name)
        try:
            timestamp = datetime.fromisoformat(result['utc'])
            if timestamp.utcoffset() is None:
                raise ValueError('Naive producer timestamp')
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError('Producer UTC evidence missing') from error
        times[name], results[name] = timestamp, result
    for family in ('composition', 'harness'):
        initial, review, final = (family + '_' + stage for stage in ('initial', 'review', 'final'))
        if (descriptor(results[review].get('initial_result')) != selected[initial] or
                descriptor(results[final].get('initial_result')) != selected[initial] or
                descriptor(results[final].get('review_result')) != selected[review] or
                not times[initial] <= times[review] <= times[final]):
            raise ValueError('Producer initial/review/final chain differs: ' + family)


def bag_selected_hdc(path):
    if path is None or str(Path(path).resolve()).lower() != str(Path(NATIVE.HDCPATH).resolve()).lower():
        raise ValueError('Only the selected SDK HDC is admitted')


def bag_records(text, run, side, pid):
    rows = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=ORDINARY.ordinary_unique)
        if not isinstance(row, dict) or any(type(row.get(key)) is not int for key in ('schema', 'pid', 'seq')) or any(
                row.get(key) != value for key, value in {'schema': 1, 'run': run, 'side': side,
                'pid': pid, 'role': 'bag-abi', 'seq': len(rows) + 1}.items()):
            raise ValueError('Bag probe identity/schema/order differs')
        if row.get('event') not in ('BAG_ABI_READY', 'FINISH_ACCEPTED', 'FINAL', 'FAIL') or row['event'] == 'FAIL':
            raise ValueError('Unexpected/failed bag probe record')
        rows.append(row)
    return rows


def bag_abi_business(text, run, side, pid, files, package, final=False):
    rows = bag_records(text, run, side, pid)
    ready = [row for row in rows if row['event'] == 'BAG_ABI_READY']
    if not ready:
        if final:
            raise ValueError('Final ABI readiness missing')
        return False
    if len(ready) != 1:
        raise ValueError('Duplicate ABI readiness')
    row = ready[0]
    expected_modules = {'rosbag2_py.' + name for name in MODULES} | {
        'rclpy._rclpy_pybind11', 'ros2bag.verb.record', 'ros2bag.verb.play', 'ros2bag_sqlite3_cli'}
    if set(row.get('origins', {})) != expected_modules or row.get('node_created') is not False or row.get('soabi') != 'cpython-312-aarch64-linux-ohos':
        raise ValueError('Incomplete native ABI/origin evidence')
    for origin in row['origins'].values():
        path = origin['path']
        if not path.startswith(package + '/python/') or files.get(path[len(package) + 1:]) != origin['sha256']:
            raise ValueError('Unselected ABI module origin')
    capsules = row.get('capsules', {})
    message_fields = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
    if capsules != {**{name: message_fields for name in ('String', 'Resume.Request', 'Resume.Response')}, 'Resume': ['_TYPE_SUPPORT']}:
        raise ValueError('Actual generated message/service capsules incomplete')
    cdr_rows = row.get('cdr', [])
    if len(cdr_rows) != 3 or [item.get('type') for item in cdr_rows] != ['String', 'Resume.Request', 'Resume.Response']:
        raise ValueError('Incomplete no-Node CDR evidence')
    for item in cdr_rows:
        data = base64.b64decode(item['base64'], validate=True)
        if len(data) <= 4 or len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256'] or data[:4] not in (b'\x00\x00\x00\x00', b'\x00\x01\x00\x00'):
            raise ValueError('Native serialized message evidence differs')
        if item['type'] == 'String':
            value = ('r25bag_' + run).encode() + b'\0'
            endian = 'little' if data[1] == 1 else 'big'
            if int.from_bytes(data[4:8], endian) != len(value) or data[8:] != value:
                raise ValueError('Actual ABI String CDR payload differs')
    expected_qos = [dict(history=1, depth=depth, reliability=reliability, durability=durability,
        liveliness=liveliness, deadline={'sec': 2, 'nsec': 13}, lifespan={'sec': 3, 'nsec': 17},
        liveliness_lease_duration={'sec': 4, 'nsec': 19}, avoid_ros_namespace_conventions=avoid)
        for depth, reliability, durability, liveliness, avoid in [(7, 1, 2, 1, False), (11, 2, 1, 3, True)]]
    if row.get('qos_setters') != 'NATIVE_CAST_COMPLETED_NOT_MAP_READBACK' or row.get('qos_expected') != expected_qos or row.get('qos_native_roundtrip') != expected_qos:
        raise ValueError('Required public native QoS conversion differs')
    if [preset[0] for preset in row.get('presets', [])] != ['none', 'resilient'] or row.get('preset_default') != 'none' or row.get('invalid_preset_exit') != 2 or row.get('playback_paused') is not True or row.get('playback_ack_timeout_ms') != 5000:
        raise ValueError('Actual stock parser/plugin evidence differs')
    if final and ([r['event'] for r in rows] != ['BAG_ABI_READY', 'FINISH_ACCEPTED', 'FINAL'] or
                  rows[1].get('reason') != 'NORMAL_COMPLETION' or rows[2].get('success') is not True or rows[2].get('node_created') is not False):
        raise ValueError('ABI normal finish/final sequence incomplete')
    return True


def bag_prepare(selection_path):
    selection = bag_json(selection_path)
    if set(selection) != {'schema', 'case', 'package', 'runtime', 'readiness', 'external', 'identity', 'routes', 'freeze'} or selection['schema'] != 1 or selection['case'] != CASE:
        raise ValueError('Only the fixed no-Node bag ABI selection is implemented')
    bindings, receipts = {str(Path(selection_path).resolve()): bag_sha(selection_path)}, {}
    for name in ('runtime', 'readiness', 'external', 'identity', 'routes', 'freeze'):
        descriptor = selection[name]
        if set(descriptor) != {'path', 'sha256'} or not re.fullmatch('[0-9a-f]{64}', descriptor['sha256']):
            raise ValueError('Require exact selected receipt: ' + name)
        path = Path(descriptor['path']).resolve(strict=True)
        if bag_sha(path) != descriptor['sha256']:
            raise ValueError('Selected receipt changed: ' + name)
        bindings[str(path)], receipts[name] = descriptor['sha256'], bag_json(path)
    if selection['external']['sha256'] != EXTERNAL_SHA or selection['identity']['sha256'] != IDENTITY_SHA:
        raise ValueError('Exact previously qualified providers and identity are required')
    package = Path(selection['package']).resolve(strict=True)
    runtime, ready, freeze = receipts['runtime'], receipts['readiness'], receipts['freeze']
    manifest_path = package / 'LAUNCHER_PACKAGE.json'
    manifest = bag_json(manifest_path)
    files = {**manifest['files'], 'LAUNCHER_PACKAGE.json': bag_sha(manifest_path)}
    if runtime.get('schema') != 4 or runtime.get('status') != 'STRUCTURAL_PASS_NOT_RUNTIME_VERIFIED' or runtime.get('eligible_for_deployment') is not False or runtime.get('package_inventory') != files:
        raise ValueError('Combined package structural seal differs')
    if (runtime.get('launcher_manifest_sha256') != files['LAUNCHER_PACKAGE.json'] or
            runtime.get('source_sha256') != bag_sha(REPO / 'integrations/ros2/target/compose_actionbag_bag_runtime.py') or
            set(runtime.get('report_sha256', {})) != {'INPUTS.json', 'OVERLAY_COMPARISON.json', 'ALL_RECORDS.json',
                'RECORD_DERIVATION.json', 'ELF_CLOSURE.json', 'NO_MUTATION.json'}):
        raise ValueError('Combined package producer/manifest/report contract differs')
    if any(runtime.get(name) is not False for name in ('target_imported', 'device_tested', 'communication_verified', 'production_verified')):
        raise ValueError('Static package must not claim runtime qualification')
    if {p.relative_to(package).as_posix() for p in package.rglob('*') if p.is_file()} != set(files):
        raise ValueError('Combined package member set differs')
    for name, digest in files.items():
        if not name or '\\' in name or ':' in name or any(part in ('', '.', '..') or part.endswith((' ', '.')) for part in name.split('/')):
            raise ValueError('Unsafe package name')
        path = package / name
        if path.is_symlink() or path.resolve() != path or not path.is_file() or bag_sha(path) != digest:
            raise ValueError('Combined package file changed')
    if any(path.is_symlink() for path in package.rglob('*')):
        raise ValueError('Symlink in package')
    source_map = {str(path.resolve()): bag_sha(path) for path in SOURCE_PATHS}
    if freeze.get('schema') != 1 or freeze.get('scope') != 'BAG_ABI_SOURCE_FREEZE' or freeze.get('sources') != source_map:
        raise ValueError('Exact current bag and inherited source freeze required')
    for member, source in [('scripts/bag_runtime_probe.py', REPO / 'integrations/ros2/target/bag_runtime_probe.py'),
                           ('scripts/actionbag_runtime_probe.py', ORDINARY.PROBE)]:
        if files.get(member) != bag_sha(source):
            raise ValueError('Frozen target probe differs from combined package')
    if (ready.get('schema') != 1 or ready.get('scope') != 'BAG_ABI_ONLY_POSTREVIEW_HOST_READY_TARGET_PENDING' or
            ready.get('source_freeze_sha256') != selection['freeze']['sha256'] or
            ready.get('runtime_sha256') != selection['runtime']['sha256'] or
            ready.get('launcher_manifest_sha256') != files['LAUNCHER_PACKAGE.json'] or
            ready.get('external_sha256') != EXTERNAL_SHA or ready.get('identity_sha256') != IDENTITY_SHA or
            ready.get('record_play_authorized') is not False or ready.get('external_provider_expansion') is not False):
        raise ValueError('New exact ABI-only readiness is required; ordinary READY is not reusable')
    bag_gate_results(ready.get('gates'), source_map, selection['runtime'], selection['freeze'], bindings)
    for name, digest in runtime['report_sha256'].items():
        path = package.parent / name
        if bag_sha(path) != digest:
            raise ValueError('Combined composition report changed')
        bindings[str(path)] = digest
    bindings.update(source_map)
    return {'schema': 1, 'scope': 'BAG_ABI_PREPARED_NO_DEVICE_DISPATCH', 'case': CASE,
        'run': secrets.token_hex(8), 'package': str(package), 'files': files, 'inputs': selection,
        'bindings': bindings, 'selection_file': {'path': str(Path(selection_path).resolve()), 'sha256': bag_sha(selection_path)}}


class BagCliRunner(ORDINARY.OrdinaryCliRunner):
    def __init__(self, args):
        bag_selected_hdc(args.hdc)
        self.args, self.output = args, args.output.resolve()
        if self.output.exists():
            raise ValueError('Fresh evidence output required')
        self.plan = bag_json(args.prepared)
        checked = bag_prepare(self.plan['selection_file']['path'])
        if any(self.plan.get(key) != value for key, value in checked.items() if key != 'run') or not re.fullmatch('[0-9a-f]{16}', self.plan.get('run', '')):
            raise ValueError('Prepared bag selection changed')
        self.run, self.case = self.plan['run'], CASE
        self.package, self.files = Path(self.plan['package']), self.plan['files']
        self.external = bag_json(self.plan['inputs']['external']['path'])
        self.readiness = bag_json(self.plan['inputs']['readiness']['path'])
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.roles = {s: 'bag-abi' for s in 'AB'}
        self.expected_argv = {s: [self.prefix[s] + '/package/actionbag_python', *self.argv(s)[1:]] for s in 'AB'}
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.finished_fixtures, self.closed, self.maps_observed = set(), set(), {}
        identity = bag_json(self.plan['inputs']['identity']['path'])
        if identity['main_execution_result'] != 'PASS_IDENTITY_READINESS_ONLY':
            raise ValueError('Existing identity readiness required')
        self.tokens = {row['side']: row['new_token'] for row in identity['boards']}
        if self.tokens != {'A': 672006291, 'B': 672042405}:
            raise ValueError('Unexpected identity tokens')
        routes = bag_json(self.plan['inputs']['routes']['path'])
        self.observed = datetime.fromisoformat(routes['observed_utc'])
        age = (datetime.now(timezone.utc) - self.observed).total_seconds()
        if self.observed.utcoffset() is None or not 0 <= age <= 300 or routes.get('cross_confirmed') is not True or not routes.get('bindings'):
            raise ValueError('Fresh cross-confirmed paired route receipts required')
        for path, digest in routes['bindings'].items():
            if bag_sha(path) != digest:
                raise ValueError('Fresh paired route input changed')
        self.routes, self.udids = {}, {}
        for side in 'AB':
            row = routes['sides'][side]
            if row['target'] != NATIVE.TARGETS[side] or row['host_exit'] != 0 or row['remote_exit'] != 0:
                raise ValueError('Paired query target or exit differs')
            for kind in ('stdout', 'udid'):
                data = Path(row[kind + '_file']).read_bytes()
                if hashlib.sha256(data).hexdigest() != row[kind + '_sha256']:
                    raise ValueError('Paired query export changed')
                if kind == 'stdout':
                    self.routes[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
                else:
                    if not 1 <= len(data) <= 64 or any(char in data for char in (b'\0', b'\r', b'\n')):
                        raise ValueError('Invalid private UDID bytes')
                    self.udids[side] = data.decode()
            if len(self.udids[side].encode()) != self.routes[side]['udid_bytes']:
                raise ValueError('Private UDID length differs')
        if self.udids['A'] == self.udids['B'] or any(self.routes[peer]['local'] not in self.routes[side]['remotes'] for side, peer in [('A', 'B'), ('B', 'A')]):
            raise ValueError('Real pair is not cross-confirmed')
        self.output.mkdir(parents=True)
        self.owns_output = True
        self.write('PLAN.json', self.plan)

    def argv(self, side):
        root = self.prefix[side]
        return [root + '/package/run-token-first', '--script', root + '/package/scripts/bag_runtime_probe.py',
                '--role', 'bag-abi', '--run', self.run, '--side', side, '--finish-control', root + '/control.PROBE_FINISH']

    def configure(self, side):
        peer = 'B' if side == 'A' else 'A'
        root, package = self.prefix[side], self.prefix[side] + '/package'
        service = 'com.kaihong.mdds.r25.' + self.run
        fields = {'bridge_library': package + '/lib/libfastdds_dsoftbus_ros2_target_bridge.so',
            'fastdds_module': package + '/lib/libfastrtps.so.2.14', 'fastcdr_module': package + '/lib/libfastcdr.so.2',
            'softbus_module': '/system/lib64/platformsdk/libsoftbus_client.z.so', 'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            'run': self.run, 'side': str('AB'.index(side)), 'domain': '201', 'authorized_package': 'com.kaihong.mdds',
            'peer_network_id': self.routes[peer]['local'], 'service_prefix': service, 'connect_release': root + '/control.LISTENER_READY',
            'locator_version': 'V1', 'namespace': 'mdds.r25.action', 'local_udid': self.udids[side], 'peer_udid': self.udids[peer],
            'service_slot': str('AB'.index(side) + 1), 'peer_slot': str('AB'.index(peer) + 1), 'profile': 'BYTES_NATIVE',
            'native_name_contract': 'PACKAGE_SCOPED_V1', 'local_initiator_name': service + '.' + side + '.tx',
            'peer_initiator_name': service + '.' + peer + '.tx', 'runtime_close_release': root + '/control.RUNTIME_CLOSE'}
        config = self.output / (side + '.private.conf')
        config.write_text(''.join(k + '=' + v + '\n' for k, v in fields.items()), encoding='utf-8', newline='\n')
        env = {'PATH': '/system/bin:/system/xbin', 'LD_LIBRARY_PATH': package + '/lib:' + ORDINARY.RUNTIME + '/lib',
            'AMENT_PREFIX_PATH': package, 'RMW_IMPLEMENTATION': 'rmw_fastrtps_cpp', 'ROS_DOMAIN_ID': '201',
            'ROS_AUTOMATIC_DISCOVERY_RANGE': 'SUBNET', 'ROS_LOG_DIR': root + '/logs', 'RCUTILS_COLORIZED_OUTPUT': '0',
            'RCUTILS_LOGGING_USE_STDOUT': '0', 'FASTDDS_DSOFTBUS_REQUIRED_CONFIG': root + '/ros.conf',
            'FASTDDS_DSOFTBUS_NATIVE_TOKEN': str(self.tokens[side]), 'FASTDDS_DSOFTBUS_IDENTITY_ADMITTED': '1',
            'FASTDDS_DSOFTBUS_NATIVE_IDENTITY': NATIVE.IDENTITY, 'FASTDDS_ENVIRONMENT_FILE': '', 'SKIP_DEFAULT_XML': '1'}
        argv = self.argv(side)
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
        self.write(side + '.environment.json', env)
        self.write(side + '.dispatch.json', {'argv': argv, 'expected_exec_argv': self.expected_argv[side],
            'supervisor_sha256': bag_sha(supervisor), 'config_sha256': bag_sha(config)})

    def poll(self, side):
        root = self.prefix[side]
        raw = self.shell(side, 'test -f ' + root + '/stdout && test -f ' + root + '/stderr && test "$(stat -c %s ' +
            root + '/stdout)" -le 4194304 && test "$(stat -c %s ' + root + '/stderr)" -le 4194304 && cat ' +
            root + '/stdout && printf "\\nR25_STDERR\\n" && cat ' + root + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        if re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE', text):
            raise ValueError('Executed ABI window reported failure')
        bag_records(text, self.run, side, self.owned[side]['child']['pid'])
        status = self.shell(side, 'if test -f ' + root + '/exit; then cat ' + root + '/exit; fi').decode().strip()
        return text, status

    def maps(self, side, label):
        self.guard(side)
        pid = self.owned[side]['child']['pid']
        raw = self.shell(side, 'cat /proc/' + str(pid) + '/maps').decode()
        (self.output / (side + '.' + label + '.maps')).write_text(raw, encoding='utf-8')
        package = self.prefix[side] + '/package'
        external = {row['canonical_absolute_path']: row['sha256'] for row in self.external['devices'][side]['providers']}
        paths, anonymous = ORDINARY.ordinary_map_paths(raw, package, self.files, external, False, 'lib/libpython3.12.so.1.0')
        for name in MODULES:
            if package + '/python/rosbag2_py/' + name + '.cpython-312-aarch64-linux-ohos.so' not in paths:
                raise ValueError('Selected native bag module not mapped: ' + name)
        hashes = {}
        for start in range(0, len(paths), 20):
            text = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(path) for path in paths[start:start + 20])).decode()
            for line in text.splitlines():
                match = re.fullmatch('([0-9a-f]{64})  (/.+)', line)
                if not match or match[2] in hashes:
                    raise ValueError('Mapped hash inventory is malformed')
                hashes[match[2]] = match[1]
        if set(hashes) != set(paths):
            raise ValueError('Incomplete mapped hash inventory')
        for path, digest in hashes.items():
            expected = self.files[path[len(package) + 1:]] if path.startswith(package + '/') else external[path]
            if digest != expected:
                raise ValueError('Mapped provider bytes changed')
        self.maps_observed[side] = hashes
        self.write(side + '.' + label + '.providers.json', {'sha256': hashes, 'anonymous_mappings': anonymous})

    def workflow(self):
        self.stage, first_dispatch = 'LAUNCH', time.monotonic()
        self.stage_deadline = first_dispatch + 60
        for side in 'AB':
            self.launch(side)
        self.stage = 'IMPORTS'
        while True:
            ready = {}
            for side in 'AB':
                text, status = self.poll(side)
                if status:
                    raise ValueError('ABI child exited before paired hold')
                ready[side] = bag_abi_business(text, self.run, side, self.owned[side]['child']['pid'],
                    self.files, self.prefix[side] + '/package')
            if time.monotonic() >= self.stage_deadline:
                raise ValueError('Absolute ABI listener/import deadline exceeded')
            if all(ready.values()):
                break
            time.sleep(0.1)
        for side in 'AB':
            self.maps(side, 'imports')
        for side in 'AB':
            self.finish_fixture(side, 'NORMAL_COMPLETION')
        self.stage, self.stage_deadline = 'EXIT', min(first_dispatch + 60, time.monotonic() + 20)
        self.wait_exit(min(20, self.stage_deadline - time.monotonic()))

    def analyze(self, texts):
        for side in 'AB':
            text = texts[side]
            bag_abi_business(text, self.run, side, self.owned[side]['child']['pid'], self.files, self.prefix[side] + '/package', final=True)
            if NATIVE.actionbag_fields(text, 'ROS_PYTHON_INTERPRETER_RETURN') != [{'execution_rc': '0', 'finalize_rc': '0'}] or NATIVE.actionbag_fields(text, 'ROS_PYTHON_FINAL') != [{
                    'finalized': '1', 'python_rc': '0', 'teardown_observed': '0', 'teardown_rc': '-1', 'communication': 'NOT_ASSERTED'}] or NATIVE.actionbag_fields(text, 'ROS_IDENTITY_FINAL') != [{'restored': '1', 'business_rc': '0'}]:
                raise ValueError('No-Node finalize/restore evidence missing')
            if re.search(r'FDDST_ROS_PRE_PARTICIPANT|ROS_LISTENER_READY|ROS_RUNTIME_CLOSE_READY|ROS_NATIVE_DATA |ROS_STARTUP_OPERATION |Factory_(?:create|delete)_participant|FDDST_ROS_CLOSED|FDDST_ENDPOINT', text):
                raise ValueError('Unexpected no-Node runtime/Participant/Socket activity')
            # Expected negative-parser text is structured evidence, not a native diagnostic.
            stdout = (self.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8')
            native_stdout = '\n'.join(line for line in stdout.splitlines() if not line.startswith('R25_PROBE '))
            stderr = (self.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8')
            ORDINARY.ordinary_error_observations(native_stdout, stderr, self.run, side, self.owned[side]['child']['pid'], None, False, self.files)
        self.write('ANALYSIS.json', {'scope': 'TARGET_NO_NODE_BAG_ABI_ONLY', 'content': 'PASS',
            'communication_verified': False, 'record_play': 'NOT_RUN', 'execution_qualification': 'PENDING_CLEANUP'})

    def execute(self):
        try:
            release = bag_json(self.args.release)
            if release != {'schema': 1, 'scope': 'MAIN_ONE_SHOT_BAG_ABI_RELEASE', 'run': self.run,
                    'prepared_sha256': bag_sha(self.args.prepared), 'readiness_sha256': self.plan['inputs']['readiness']['sha256'],
                    'runner_sha256': bag_sha(__file__), 'record_play_authorized': False}:
                raise ValueError('Exact Main ABI-only release required')
            if not 0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240:
                raise ValueError('Main release route age must be under240s')
            consumed = self.args.prepared.parent / (self.run + '.CONSUMED.json')
            with consumed.open('x', encoding='utf-8') as stream:
                json.dump({'release_sha256': bag_sha(self.args.release), 'output': str(self.output),
                           'utc': datetime.now(timezone.utc).isoformat()}, stream)
            self.write('MAIN_RELEASE.json', release)
            self.deploy()
            self.workflow()
            self.stage_deadline = None
            self.analyze(self.collect())
        except ValueError as error:
            self.errors.append(self.stage + ': ' + str(error))
        except (OSError, subprocess.SubprocessError) as error:
            self.blocked.append(self.stage + ': ' + str(error))
        except (Exception, KeyboardInterrupt) as error:
            self.blocked.append(self.stage + ': unexpected ' + repr(error))
        finally:
            self.cleanup(bool(self.errors or self.blocked))
            if self.errors or self.blocked:
                try:
                    if self.owned:
                        self.collect()
                except (OSError, ValueError) as error:
                    self.blocked.append('FAILURE_CAPTURE: ' + str(error))
            result = {'run': self.run, 'case': self.case, 'result': 'FAIL' if self.errors else 'BLOCKED' if self.blocked else 'PASS',
                'failures': self.errors, 'blocked': self.blocked, 'identity_mutations': 0, 'device_window': self.prefix,
                'directories_retained': True, 'source_sha256': bag_sha(__file__), 'inherited_runner_sha256': ORDINARY_SHA,
                'scope': 'TARGET_NO_NODE_BAG_ABI_ONLY', 'communication_verified': False, 'record_play': 'NOT_RUN'}
            self.write('RESULT.json', result)
        return 0 if result['result'] == 'PASS' else 1


def bag_runner_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('prepare', 'run'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--prepared', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--hdc', type=Path)
    args = parser.parse_args()
    if args.mode == 'prepare':
        if args.inputs is None or args.output.exists():
            parser.error('Prepare requires inputs and a fresh output')
        plan = bag_prepare(args.inputs)
        args.output.mkdir(parents=True)
        (args.output / 'PLAN.json').write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
        return 0
    if args.prepared is None or args.release is None or args.hdc is None:
        parser.error('Run requires prepared, release and selected HDC')
    bag_selected_hdc(args.hdc)
    return BagCliRunner(args).execute()


if __name__ == '__main__':
    raise SystemExit(bag_runner_main())
