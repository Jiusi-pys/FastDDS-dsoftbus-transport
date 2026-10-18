"""One owned A/B0/B1 normal restart scene. No provisioning or network changes."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import tarfile
import tempfile
import time


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(name, path, digest=None):
    if digest is not None and sha(path) != digest:
        raise ValueError('Frozen helper changed: ' + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ROOT = REPO.parents[2]
EVIDENCE = ROOT / 'build/r27-process-restart'
S = load('r27_frozen_sustained', HERE / 'run_sustained_roundtrip.py',
         'c3d4096efb1fc17357fb144794c72bf1a410e5a831c9102173da7b7a483a838b')
O = load('r27_oracle', HERE / 'process_restart_oracle.py')
R, BAG, ORDINARY, NATIVE, BASE = S.R, S.BAG, S.ORDINARY, S.NATIVE, S.BASE
N = load('r27_native', HERE / 'process_restart_native_oracle.py')
require, read, write_json = O.require, S.read, S.write_json
PROBE = REPO / 'integrations/ros2/target/process_restart_probe.py'
MEMBER = 'scripts/process_restart_probe.py'
BRIDGE = 'lib/libfastdds_dsoftbus_ros2_target_bridge.so'
SOURCES = [Path(__file__), PROBE, HERE / 'process_restart_oracle.py', HERE / 'process_restart_native_oracle.py',
           HERE / 'test_process_restart.py', HERE / 'process_restart_observation_tests.cpp',
           REPO / 'integrations/ros2/BoundedNativeControl.hpp', REPO / 'integrations/ros2/ProcessRestartObservation.hpp',
           REPO / 'integrations/ros2/target/TargetBridge.cpp', REPO / 'CMakeLists.txt']


def fresh(path, kind):
    path = Path(path).absolute()
    require(path.parent == EVIDENCE and re.fullmatch(kind + '-[0-9a-f]{16}', path.name), 'Fresh R27 child required')
    path = BASE.runtime_path(path, EVIDENCE, exists=False)
    require(not path.exists(), 'Output already exists')
    return path


def checked_bridge(receipt):
    receipt = BASE.runtime_path(receipt, EVIDENCE)
    result = read(receipt)
    require(result.get('status') in ('PASS_BUILD_AND_HOST_BASELINE', 'PASS_INITIAL_HOST', 'PASS_FINAL_HOST'),
            'Independent successful build/host receipt required')
    mode = result['target_build']['modes']['on']
    bridge = BASE.runtime_path(mode['bridge'], EVIDENCE)
    require(sha(bridge) == mode['bridge_sha256'], 'Experimental bridge changed')
    source = result['source_inventory']
    require(sha(source['path']) == source['sha256'], 'Compiler source inventory changed')
    inventory = read(source['path'])
    # Tester records exact absolute source paths, not file names or timestamps.
    require(isinstance(inventory, dict) and inventory, 'Empty build source inventory')
    for path, digest in inventory.items():
        require(sha(path) == digest, 'Built source changed: ' + path)
    require(all(str(p) in inventory for p in SOURCES[-4:]), 'Build omitted current observer/target sources')
    return bridge


def compose(receipt, output):
    output = fresh(output, 'package')
    bridge = checked_bridge(receipt)
    require(sha(S.PARENT / 'RUNTIME_PACKAGE.json') == S.PARENT_SHA, 'Original package receipt changed')
    parent = read(S.PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    require(BASE.runtime_inventory(S.PARENT / 'package') == parent, 'Original package changed')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(S.PARENT / 'package', package)
    shutil.copyfile(bridge, package / BRIDGE)
    shutil.copyfile(PROBE, package / MEMBER)
    manifest = read(package / 'LAUNCHER_PACKAGE.json')
    manifest['files'].update({BRIDGE: sha(bridge), MEMBER: sha(PROBE)})
    write_json(package / 'LAUNCHER_PACKAGE.json', manifest)
    result = {'schema': 1, 'scope': 'SEPARATE_R27_EXPERIMENTAL_BRIDGE_AND_PROBE',
              'package': str(package), 'files': BASE.runtime_inventory(package),
              'parent_sha256': S.PARENT_SHA, 'build_receipt': str(Path(receipt).resolve()),
              'build_receipt_sha256': sha(receipt), 'bridge_sha256': sha(bridge), 'probe_sha256': sha(PROBE),
              'device_tested': False}
    write_json(output / 'DERIVATION.json', result)
    checked_package(output / 'DERIVATION.json')
    return result


def checked_package(path):
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    require(result.get('schema') == 1 and result.get('scope') == 'SEPARATE_R27_EXPERIMENTAL_BRIDGE_AND_PROBE' and
            result.get('device_tested') is False and result.get('parent_sha256') == S.PARENT_SHA and
            sha(result['build_receipt']) == result['build_receipt_sha256'], 'Derivation identity changed')
    bridge = checked_bridge(result['build_receipt'])
    package = BASE.runtime_path(result['package'], EVIDENCE)
    files = BASE.runtime_inventory(package)
    require(sha(S.PARENT / 'RUNTIME_PACKAGE.json') == S.PARENT_SHA, 'Parent receipt drift')
    parent = read(S.PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    require(files == result['files'] and set(files) == set(parent) | {MEMBER} and
            all(files[k] == v for k, v in parent.items() if k not in (BRIDGE, 'LAUNCHER_PACKAGE.json')) and
            files[BRIDGE] == result['bridge_sha256'] == sha(bridge) and
            files[MEMBER] == result['probe_sha256'] == sha(PROBE), 'Unexpected package change')
    require(read(package / 'LAUNCHER_PACKAGE.json')['files'] ==
            {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'}, 'Manifest content drift')
    return package, files


def prepare(args):
    output = fresh(args.output, 'prepared')
    package, files = checked_package(args.derivation)
    require(sha(args.external) == BAG.EXTERNAL_SHA and sha(args.identity) == BAG.IDENTITY_SHA, 'Identity/provider expansion forbidden')
    routes = read(args.routes)
    bindings = {str(p.resolve()): sha(p) for p in [args.derivation, args.routes, args.external, args.identity, *SOURCES]}
    require(routes.get('cross_confirmed') is True and routes.get('bindings'), 'Paired routes required')
    for path, digest in routes['bindings'].items():
        require(sha(path) == digest, 'Original route query changed')
        bindings[path] = digest
    scenario = secrets.token_hex(8)
    runs = {key: secrets.token_hex(8) for key in ('A', 'B0', 'B1')}
    require(len(set(runs.values()) | {scenario}) == 4, 'Unique scenario/process identities required')
    result = {'schema': 1, 'scope': 'R27_PROCESS_RESTART_PREPARED_NOT_DISPATCHED', 'scenario': scenario, 'runs': runs,
              'derivation': str(args.derivation.resolve()), 'package': str(package), 'files': files, 'bindings': bindings,
              'routes': str(args.routes.resolve()), 'external': str(args.external.resolve()), 'identity': str(args.identity.resolve())}
    output.mkdir()
    write_json(output / 'PLAN.json', result)
    return result


def checked_plan(path):
    plan = read(path)
    require(set(plan) == {'schema', 'scope', 'scenario', 'runs', 'derivation', 'package', 'files', 'bindings',
                         'routes', 'external', 'identity'} and type(plan['schema']) is int and plan['schema'] == 1 and
            plan['scope'] == 'R27_PROCESS_RESTART_PREPARED_NOT_DISPATCHED' and set(plan['runs']) == {'A', 'B0', 'B1'} and
            all(isinstance(x, str) and re.fullmatch('[0-9a-f]{16}', x) for x in [plan['scenario'], *plan['runs'].values()]) and
            len(set(plan['runs'].values()) | {plan['scenario']}) == 4, 'Prepared identity/schema invalid')
    package, files = checked_package(plan['derivation'])
    require(str(package) == plan['package'] and files == plan['files'], 'Prepared package changed')
    require(sha(plan['external']) == BAG.EXTERNAL_SHA and sha(plan['identity']) == BAG.IDENTITY_SHA, 'Identity/provider receipt drift')
    routes = read(plan['routes'])
    expected = {str(Path(plan[k]).resolve()): sha(plan[k]) for k in ('derivation', 'routes', 'external', 'identity')}
    expected.update({str(p.resolve()): sha(p) for p in SOURCES})
    require(routes.get('bindings') and routes.get('cross_confirmed') is True, 'Original paired query binding required')
    expected.update(routes['bindings'])
    require(plan['bindings'] == expected, 'Incomplete/expanded source bindings')
    for name, digest in expected.items():
        require(sha(name) == digest, 'Input source changed')
    observed = datetime.fromisoformat(routes['observed_utc'])
    require(observed.utcoffset() is not None and 0 <= (datetime.now(timezone.utc) - observed).total_seconds() <= 300,
            'Preparation freshness expired; runtime budget is separate')
    peers, udids = {}, {}
    for side in 'AB':
        row = routes['sides'][side]
        require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == row['remote_exit'] == 0, 'Peer query target/exit mismatch')
        for kind in ('stdout', 'udid'):
            data = Path(row[kind + '_file']).read_bytes()
            require(hashlib.sha256(data).hexdigest() == row[kind + '_sha256'], 'Query export changed')
            if kind == 'stdout':
                peers[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
            else:
                require(1 <= len(data) <= 64 and not any(c in data for c in (b'\0', b'\r', b'\n')), 'Invalid UDID')
                udids[side] = data.decode()
        require(len(udids[side].encode()) == peers[side]['udid_bytes'], 'UDID length differs')
    require(udids['A'] != udids['B'] and all(peers[p]['local'] in peers[s]['remotes'] for s, p in [('A', 'B'), ('B', 'A')]),
            'Pair not cross-confirmed')
    identity = read(plan['identity'])
    tokens = {r['side']: r['new_token'] for r in identity['boards']}
    require(identity['main_execution_result'] == 'PASS_IDENTITY_READINESS_ONLY' and tokens == {'A': 672006291, 'B': 672042405},
            'Only admitted existing identity permitted')
    return plan, peers, udids, tokens, observed


class RestartProcess(S.SustainedRunner):
    """One instance uses the frozen per-PID guards; no AB dictionary swapping."""
    def __init__(self, args, plan, instance, peers, udids, tokens, observed, output):
        self.args, self.output, self.owns_output = args, output, True
        require(not output.exists(), 'Fresh instance output required')
        output.mkdir()
        self.plan, self.instance, self.side = plan, instance, instance[0]
        self.run, self.scenario, self.case = plan['runs'][instance], plan['scenario'], 'process_restart'
        self.package, self.files = Path(plan['package']), plan['files']
        self.prefix = {self.side: '/data/local/tmp/mdds_dds_' + self.run + '_' + self.side}
        self.roles = {self.side: 'requester' if self.side == 'A' else 'responder'}
        self.expected_argv = {self.side: [self.prefix[self.side] + '/package/actionbag_python', *self.argv(self.side)[1:]]}
        self.routes, self.udids, self.tokens, self.observed = peers, udids, tokens, observed
        self.external = read(plan['external'])
        self.readiness = {'owned_libpython': {'member': 'lib/libpython3.12.so.1.0'}}
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.finished_fixtures, self.closed, self.maps_observed = set(), set(), {}
        self.normal_stop_attempted = self.normal_stop_confirmed = False

    def argv(self, side):
        root = self.prefix[side]
        return [root + '/package/run-token-first', '--script', root + '/package/' + MEMBER,
                '--role', self.roles[side], '--run', self.run, '--side', side, '--scenario', self.scenario,
                '--instance', self.instance, '--finish-control', root + '/control.PROBE_FINISH']

    def poll(self, side):
        root = self.prefix[side]
        script = ('test -f ' + root + '/stdout && test -f ' + root + '/stderr && test "$(stat -c %s ' +
            root + '/stdout)" -le 4194304 && test "$(stat -c %s ' + root + '/stderr)" -le 4194304 && cat ' +
            root + '/stdout && printf "\nR27_STDERR\n" && cat ' + root + '/stderr && printf "\nR27_EXIT\n" && '
            'if test -f ' + root + '/exit; then cat ' + root + '/exit; fi')
        before = self.sequence
        try:
            raw = self.shell(side, script)
        except OSError as error:
            # Explicit capture workaround, once across the whole scene. Only this
            # fixed read-only snapshot may be sampled again; never replay controls.
            # The original remote result stays UNKNOWN, even if this sample works.
            if str(error) != 'Remote exit marker missing' or self.poll_resample_budget['used']:
                raise
            base = self.output / 'commands' / ('%04d-%s' % (self.sequence, side))
            record = read(base.with_suffix('.json'))
            if (self.sequence != before + 1 or record.get('host_exit') != 0 or record.get('timed_out') or
                    base.with_suffix('.stdout').read_bytes() or base.with_suffix('.stderr').read_bytes() or
                    self.stage_deadline is None or time.monotonic() >= self.stage_deadline):
                raise
            self.poll_resample_budget['used'] = True
            self.write('POLL_RESAMPLE_REQUEST.json', {
                'scope': 'ONE_READ_ONLY_EMPTY_CAPTURE_RESAMPLE', 'instance': self.instance,
                'failed_sequence': self.sequence, 'original_remote_exit': 'UNKNOWN',
                'original_evidence': {str(base.with_suffix(s)): sha(base.with_suffix(s))
                                      for s in ('.json', '.stdout', '.stderr')},
                'unchanged_stage_deadline': self.stage_deadline})
            raw = self.shell(side, script)  # Existing command clamps to the same deadline.
            self.write('POLL_RESAMPLE_CAPTURE.json', {'sequence': self.sequence,
                'remote_exit': 0, 'semantic_acceptance': 'NOT_ASSERTED'})
        parts = re.split(r'\r?\nR27_EXIT\r?\n', raw.decode('utf-8'))
        require(len(parts) == 2, 'Exactly one exit boundary required')
        text, status = parts  # Keep original log bytes/line endings in command captures.
        require(not re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE|NATIVE_REACCEPT_REFUSAL', text),
                'Native process failure')
        O.rows(text, self.scenario, self.instance, self.run, self.owned[side]['child']['pid'])
        return text, status.strip()

    def extra_control(self, name):
        require(self.instance == 'A' and name in ('ARM', 'REPLACE', 'POST'), 'Only finite A control permitted')
        self.guard('A')
        self.root_guard('A')
        if name == 'POST':
            member = 'control.PROCESS_POST'
            data = 'R27_POST schema=1 scenario=' + self.scenario + ' run=' + self.run + ' side=A pid=' + str(self.owned['A']['child']['pid']) + '\n'
        else:
            member = 'control.NATIVE_REACCEPT.' + name
            data = 'NATIVE_CONTROL run=' + self.run + ' side=A phase=' + name + '\n'
        target = self.prefix['A'] + '/' + member
        self.shell('A', 'umask 077; set -C; test ! -e ' + target + ' && test ! -L ' + target +
            ' && test ! -e ' + target + '.tmp && test ! -L ' + target + '.tmp && printf %s ' + shlex.quote(data) +
            ' > ' + target + '.tmp && chmod 600 ' + target + '.tmp && mv ' + target + '.tmp ' + target)
        self.write('releases/' + name + '.json', {'contents': data, 'owned': self.owned['A'], 'monotonic': time.monotonic()})

    def close_release(self):
        side = self.side
        self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side +
                     ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
        self.closed.add(side)

    def confirm_absent(self):
        side = self.side
        status = self.shell(side, 'cat ' + self.prefix[side] + '/exit').decode().strip()
        require(status == 'R25_CHILD_EXIT run=' + self.run + ' side=' + side + ' pid=' +
                str(self.owned[side]['child']['pid']) + ' rc=0', 'Normal zero exit missing')
        require(not self.cleanup_observe(side) and not self.cleanup_observe(side, role='supervisor'), 'Owned PIDs still present')
        text = self.shell(side, 'cat ' + self.prefix[side] + '/stdout && cat ' + self.prefix[side] + '/stderr').decode('utf-8')
        require(O.fields(text, 'ROS_IDENTITY_FINAL') == [{'restored': '1', 'business_rc': '0'}], 'Token restoration not proven')
        self.write('NORMAL_ABSENCE.json', {'monotonic': time.monotonic(), 'exit': status, 'owned': self.owned[side], 'token_restored': True})


    # Narrow copies of the frozen paired primitives above: only one owned side.
    def deploy(self):
        self.stage = 'DEPLOY'
        approved = Path(os.environ['LOCALAPPDATA']) / 'Temp'
        temp = Path(tempfile.mkdtemp(prefix='r25-cli-', dir=approved)).resolve()
        if temp.parent != approved.resolve() or not temp.name.startswith('r25-cli-'): raise OSError('Owned Temp escaped approved parent')
        archive = temp / 'p.tar'
        self.write('LOCAL_TEMP.json', {'directory': str(temp), 'archive': str(archive), 'platform': 'Windows native HDC local paths required'})
        try:
            with tarfile.open(archive, 'w', format=tarfile.USTAR_FORMAT) as tar:
                for name, sha in sorted(self.files.items()):
                    data = (self.package / name).read_bytes()
                    if hashlib.sha256(data).hexdigest() != sha: raise ValueError('Package changed before archive')
                    entry = tarfile.TarInfo(name)
                    entry.size, entry.uid, entry.gid, entry.mtime = len(data), 0, 0, 0
                    entry.mode = 0o755 if name in ('actionbag_python', 'run-token-first') else 0o600
                    entry.tobuf(format=tarfile.USTAR_FORMAT)
                    tar.addfile(entry, io.BytesIO(data))
            self.write('ARCHIVE.json', {'sha256': hashlib.sha256(archive.read_bytes()).hexdigest(), 'files': self.files, 'format': 'USTAR_REGULAR_ONLY'})
            for side in (self.side,):
                p, upload = self.prefix[side], self.prefix[side] + '.upload.tar'
                raw = self.shell(side, 'umask 077; test ! -e ' + p + ' && test ! -L ' + p + ' && test ! -e ' + upload +
                    ' && test ! -L ' + upload + ' && mkdir -m 700 ' + p + ' && mkdir -m 700 ' + p + '/package && stat -c "%d:%i:%u:%a" ' + p)
                self.roots[side] = raw.decode().strip()
                if not re.fullmatch(r'[0-9]+:[0-9]+:[0-9]+:700', self.roots[side]): raise OSError('Fresh private root not confirmed')
                self.send(side, archive, upload)
                self.root_guard(side)
                self.shell(side, 'tar -xf ' + upload + ' -C ' + p + '/package && rm -- ' + upload, 30)
                self.verify_files(side)
                providers = {r['canonical_absolute_path']: r['sha256'] for r in self.external['devices'][side]['providers']}
                for start in range(0, len(providers), 20):
                    names = sorted(providers)[start:start + 20]
                    text = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(n) for n in names)).decode()
                    got = {line.split(maxsplit=1)[1].strip(): line.split(maxsplit=1)[0] for line in text.splitlines()}
                    if got != {n: providers[n] for n in names}: raise OSError('Explicit external provider preflight mismatch')
                self.configure(side)
        finally:
            if temp.parent == approved.resolve() and temp.is_dir() and not temp.is_symlink():
                if archive.is_file() and not archive.is_symlink(): archive.unlink()
                temp.rmdir()

    def configure(self, side):
        peer = 'B' if side == 'A' else 'A'
        root, package = self.prefix[side], self.prefix[side] + '/package'
        service = 'com.kaihong.mdds.r27.' + self.scenario
        fields = {'bridge_library': package + '/lib/libfastdds_dsoftbus_ros2_target_bridge.so',
            'fastdds_module': package + '/lib/libfastrtps.so.2.14', 'fastcdr_module': package + '/lib/libfastcdr.so.2',
            'softbus_module': '/system/lib64/platformsdk/libsoftbus_client.z.so', 'backend': 'OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            'run': self.run, 'side': str('AB'.index(side)), 'domain': '201', 'authorized_package': 'com.kaihong.mdds',
            'peer_network_id': self.routes[peer]['local'], 'service_prefix': service, 'connect_release': root + '/control.LISTENER_READY',
            'locator_version': 'V1', 'namespace': 'mdds.r25.action', 'local_udid': self.udids[side], 'peer_udid': self.udids[peer],
            'service_slot': str('AB'.index(side) + 1), 'peer_slot': str('AB'.index(peer) + 1), 'profile': 'BYTES_NATIVE',
            'native_name_contract': 'PACKAGE_SCOPED_V1', 'local_initiator_name': service + '.' + side + '.tx',
            'peer_initiator_name': service + '.' + peer + '.tx', 'runtime_close_release': root + '/control.RUNTIME_CLOSE'}
        if self.instance == 'A':
            fields.update(backend='OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL',
                          native_reaccept_order='outgoing-first', native_reaccept_control=root + '/control.NATIVE_REACCEPT',
                          process_restart_observation='1')
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
            'supervisor_sha256': sha(supervisor), 'config_sha256': sha(config)})

    def collect(self):
        texts = {}
        for side in (self.side,):
            p = self.prefix[side]
            for name in ('stdout', 'stderr', 'supervisor.stdout', 'supervisor.stderr', 'exit'):
                size = int(self.shell(side, 'stat -c %s ' + p + '/' + name).decode().strip())
                if not 0 <= size <= 16 * 1024 * 1024:
                    raise ValueError('Complete evidence exceeds declared size bound')
                target = self.output / (side + '.raw.' + name + '.log')
                raw = self.command(side, ['file', 'recv', p + '/' + name, str(target)], 30)
                if b'transfer finish' not in raw.lower() or not target.is_file() or target.stat().st_size != size:
                    raise OSError('Raw log transfer incomplete')
                digest = self.shell(side, 'sha256sum ' + p + '/' + name).decode().split()[0]
                if hashlib.sha256(target.read_bytes()).hexdigest() != digest:
                    raise OSError('Raw log transfer hash mismatch')
            texts[side] = (self.output / (side + '.raw.stdout.log')).read_bytes().decode('utf-8') + '\n' + (self.output / (side + '.raw.stderr.log')).read_bytes().decode('utf-8')
        return texts


class RestartScene:
    def __init__(self, args):
        BAG.bag_selected_hdc(args.hdc)
        self.args = args
        self.output = fresh(args.output, 'execution')
        self.plan, peers, udids, tokens, self.observed = checked_plan(args.prepared)
        self.output.mkdir()
        self.errors, self.blocked, self.processes = [], [], {}
        self.deadline, self.origin = None, None
        poll_resample_budget = {'used': False}
        for key in ('A', 'B0', 'B1'):
            self.processes[key] = RestartProcess(args, self.plan, key, peers, udids, tokens, self.observed, self.output / key)
            self.processes[key].poll_resample_budget = poll_resample_budget
        self.write('PLAN.json', self.plan)

    def write(self, name, data):
        write_json(self.output / name, data)

    def budget(self, deadline, stage):
        require(time.monotonic() < deadline, 'Deadline exceeded: ' + stage)
        self.deadline = deadline
        for process in self.processes.values():
            process.stage, process.stage_deadline = stage, deadline

    def wait(self, keys, predicate, *, exits=()):
        while time.monotonic() < self.deadline:
            texts, statuses = {}, {}
            for key in keys:
                process = self.processes[key]
                texts[key], statuses[key] = process.poll(process.side)
                require(key in exits or not statuses[key], 'Unexpected process exit: ' + key)
            if predicate(texts, statuses):
                require(time.monotonic() < self.deadline, 'Predicate crossed absolute deadline')
                return texts
            time.sleep(0.1)
        raise ValueError('Fixed stage deadline exceeded')

    def records(self, text, key):
        p = self.processes[key]
        return O.rows(text, p.scenario, key, p.run, p.owned[p.side]['child']['pid'])

    def phase(self, texts, key, phase):
        return any(r['event'] == 'PHASE_COMPLETE' and r.get('phase') == phase for r in self.records(texts[key], key))

    def checkpoint(self, texts, key, stage):
        p = self.processes[key]
        return p.ready(p.side, texts[key], stage)

    def workflow(self):
        a, b0, b1 = (self.processes[k] for k in ('A', 'B0', 'B1'))
        for process in (a, b0, b1):
            process.deploy()  # All three fresh package roots precede runtime business.
        self.budget(time.monotonic() + 60, 'INITIAL_LISTENERS')
        a.launch('A')
        b0.launch('B')
        self.wait(('A', 'B0'), lambda texts, status: all(self.checkpoint(texts, k, 'LISTENER') for k in ('A', 'B0')))
        a.maps('A', 'listener')
        b0.maps('B', 'listener')
        require(time.monotonic() < self.deadline, 'Initial maps exceeded deadline')
        self.origin = time.monotonic()
        self.write('CLOCK.json', {'origin': 'BEFORE_FIRST_LISTENER_RELEASE', 'business_origin': self.origin,
            'business_deadline': self.origin + 35, 'normal_close_release_deadline': self.origin + 50,
            'probe_lifetime_seconds': 40, 'preparation_query_utc': self.observed.isoformat()})
        self.budget(self.origin + 35, 'PRE')
        for p in (a, b0):
            p.publish(p.side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + p.run + ' side=' + p.side + ' stage=LISTENER_READY\n')
        texts = self.wait(('A', 'B0'), lambda t, s: self.phase(t, 'A', 'PRE') and self.phase(t, 'B0', 'PRE'))
        pre = O.pair_content(self.records(texts['A'], 'A'), self.records(texts['B0'], 'B0'), self.plan['scenario'], 'PRE')
        require(time.monotonic() < self.deadline, 'PRE oracle exceeded35s')
        self.write('PRE.json', pre)
        a.extra_control('ARM')
        self.wait(('A', 'B0'), lambda t, s: any(r.get('stage') == 'ARMED' and r.get('run') == a.run and
            r.get('side') == 'A' and r.get('source') == 'SDK_PUBLIC_SOCKET' for r in O.fields(t['A'], 'NATIVE_REACCEPT_EVENT')))
        self.write('B0_NORMAL_STOP_INTENT.json', {'monotonic': time.monotonic(), 'owned': b0.owned['B'], 'method': 'PUBLIC_PROBE_FINISH'})
        b0.finish_fixture('B', 'NORMAL_COMPLETION')
        self.wait(('A', 'B0'), lambda t, s: self.checkpoint(t, 'B0', 'CLOSE'))
        b0.maps('B', 'close')
        b0.close_release()
        self.wait(('A', 'B0'), lambda t, s: bool(s['B0']), exits=('B0',))
        b0.confirm_absent()
        require(time.monotonic() < self.deadline, 'B0 normal exit exceeded35s')
        old = self.wait(('A',), lambda t, s: O.owners(t['A'], a.run, 'OLD_BOTH_READY') is not None)
        self.write('OLD_BOTH_READY.json', {'monotonic': time.monotonic(), 'observation': O.owners(old['A'], a.run, 'OLD_BOTH_READY')})
        # Fresh dispatch marker/control root, after B0's exit/token/absence proof.
        b1.launch('B')
        require(b0.owned['B']['child']['pid'] != b1.owned['B']['child']['pid'] and
                b0.owned['B']['child']['starttime'] != b1.owned['B']['child']['starttime'], 'B process identity reused')
        self.wait(('A', 'B1'), lambda t, s: self.checkpoint(t, 'B1', 'LISTENER'))
        b1.maps('B', 'listener')
        a.extra_control('REPLACE')
        b1.publish('B', 'control.LISTENER_READY', 'ROS_RELEASE run=' + b1.run + ' side=B stage=LISTENER_READY\n')
        active = self.wait(('A', 'B1'), lambda t, s: O.owners(t['A'], a.run, 'NEW_BOTH_ACTIVE') is not None)
        self.write('NEW_BOTH_ACTIVE.json', {'monotonic': time.monotonic(), 'observation': O.owners(active['A'], a.run, 'NEW_BOTH_ACTIVE'),
                                          'continuity': O.a_continuity(active['A'], a.run)})
        a.extra_control('POST')
        texts = self.wait(('A', 'B1'), lambda t, s: self.phase(t, 'A', 'POST') and self.phase(t, 'B1', 'POST'))
        post = O.pair_content(self.records(texts['A'], 'A'), self.records(texts['B1'], 'B1'), self.plan['scenario'], 'POST')
        require(time.monotonic() < self.deadline, 'POST oracle exceeded35s')
        self.write('BUSINESS.json', {'pre': pre, 'post': post, 'accepted_monotonic': time.monotonic()})
        require(time.monotonic() < self.deadline, 'Business acceptance crossed35s')
        self.budget(self.origin + 50, 'FINAL_CLOSE')
        for p in (a, b1):
            p.maps(p.side, 'business')
            p.finish_fixture(p.side, 'NORMAL_COMPLETION')
        self.wait(('A', 'B1'), lambda t, s: all(self.checkpoint(t, k, 'CLOSE') for k in ('A', 'B1')))
        for p in (a, b1):
            p.maps(p.side, 'close')
            p.close_release()
        require(time.monotonic() < self.deadline, 'Normal close release crossed50s')
        self.budget(time.monotonic() + 20, 'FINAL_EXIT')
        self.wait(('A', 'B1'), lambda t, s: all(s.values()), exits=('A', 'B1'))
        for p in (a, b1):
            p.confirm_absent()
        self.budget(time.monotonic() + 60, 'ANALYZE')
        texts = {key: p.collect()[p.side] for key, p in self.processes.items()}
        self.analyze(texts)

    def analyze(self, texts):
        identities, diagnostics = {}, {}
        for key, p in self.processes.items():
            side, pid = p.side, p.owned[p.side]['child']['pid']
            identities[key] = {'run': p.run, 'pid': pid}
            O.content(self.records(texts[key], key), self.plan['scenario'], key, 'ALL', final=True)
            require(O.fields(texts[key], 'ROS_PYTHON_INTERPRETER_RETURN') == [{'execution_rc': '0', 'finalize_rc': '0'}] and
                    O.fields(texts[key], 'ROS_PYTHON_FINAL') == [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1',
                    'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}], 'Python/native finalization failed')
            diagnostics[key] = R.recordplay_errors((p.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8'),
                (p.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8'), p.run, side, pid, p.files)
        a = self.processes['A']
        require(O.owners(texts['A'], a.run, 'NEW_BOTH_ACTIVE'), 'Final dual replacement observation missing')
        continuity = O.a_continuity(texts['A'], a.run)
        native = N.native_capture(texts, identities)
        require(continuity['participant'][:24] == native['participant_prefixes']['A'], 'A wire evidence disagrees')
        self.write('ANALYSIS.json', {'scope': 'ONE_NORMAL_B_PROCESS_RESTART', 'native': native, 'A_continuity': continuity,
            'diagnostics': diagnostics, 'sdk_epoch': 'NOT_PROVEN', 'service_action_recovery': 'NOT_TESTED',
            'repeated_reconnect': 'NOT_TESTED', 'long_duration': 'NOT_TESTED'})

    def execute(self):
        consumed = False
        try:
            release = read(self.args.release)
            require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_R27_PROCESS_RESTART_RELEASE',
                'scenario': self.plan['scenario'], 'prepared_sha256': sha(self.args.prepared), 'runner_sha256': sha(__file__)},
                'Exact Main one-shot release required')
            checked_plan(self.args.prepared)
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Route freshness expired before dispatch')
            with (self.args.prepared.parent / (self.plan['scenario'] + '.CONSUMED.json')).open('x', encoding='utf-8') as stream:
                json.dump({'release_sha256': sha(self.args.release), 'output': str(self.output), 'utc': datetime.now(timezone.utc).isoformat()}, stream)
            consumed = True
            self.write('MAIN_RELEASE.json', release)
            self.workflow()
        except ValueError as error:
            self.errors.append(str(error))
        except (Exception, KeyboardInterrupt) as error:
            self.blocked.append(repr(error))
        finally:
            for key, p in self.processes.items():
                p.cleanup(bool(self.errors or self.blocked))
                self.errors.extend(key + ': ' + x for x in p.errors)
                self.blocked.extend(key + ': ' + x for x in p.blocked)
                if p.owned and (self.errors or self.blocked):
                    try:
                        p.stage_deadline = None
                        p.collect()
                    except (Exception, KeyboardInterrupt) as error:
                        self.blocked.append(key + ' FAILURE_CAPTURE: ' + repr(error))
            passed = not self.errors and not self.blocked
            self.write('RESULT.json', {'scope': 'ONE_NORMAL_B_PROCESS_RESTART', 'scenario': self.plan['scenario'],
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors,
                'blocked': self.blocked, 'release_consumed': consumed, 'process_restart_verified': passed,
                'sdk_epoch': 'NOT_PROVEN', 'default_automatic_reconnect': 'NOT_CLAIMED',
                'evidence': {str(p): sha(p) for p in self.output.rglob('*') if p.is_file()}})
        return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('compose')
    c.add_argument('--build-receipt', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('prepare')
    for name in ('derivation', 'routes', 'external', 'identity', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    r = sub.add_parser('run')
    for name in ('prepared', 'release', 'hdc', 'output'):
        r.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'compose':
        compose(args.build_receipt, args.output)
    elif args.command == 'prepare':
        prepare(args)
    else:
        return RestartScene(args).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
