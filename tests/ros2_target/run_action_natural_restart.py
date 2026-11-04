"""Autonomous Action business on the current stack across one guarded replacement.

No host Action permission files are admitted. Native/ROS2/rclpy bytes are inherited
from the complete R30/R31 qualification; only a new test script is packaged.
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
import time
import run_action_natural as N
import action_natural_restart_oracle as O

P, R30 = N.P, N.R30
HERE, REPO, ROOT = P.HERE, P.REPO, P.ROOT
BASE, BAG, NATIVE, R = P.BASE, P.BAG, P.NATIVE, P.R
sha, read, require, write_json = P.sha, N.read, O.require, P.write_json
EVIDENCE = ROOT / 'build/r32-action-natural-restart'
SCOPE = 'R32_AUTONOMOUS_ACTION_ONE_GUARDED_SERVER_REPLACEMENT'
PROBE = REPO / 'integrations/ros2/target/action_natural_restart_probe.py'
MEMBER = 'scripts/action_natural_restart_probe.py'
LIMITS = dict(R30.LIMITS)
SOURCES = [Path(__file__), PROBE, HERE / 'action_natural_restart_oracle.py', HERE / 'test_action_natural_restart.py']
# Existing paired-query product, pinned by peer_evidence_view.PRODUCTS.
# This is a read-only provenance exception, never an output/deployment root.
SDK_LIBCXX = Path(NATIVE.HDCPATH).parent.parent / 'native/llvm/lib/aarch64-linux-ohos/libc++_shared.so'
SDK_LIBCXX_SHA = 'd1a368b58e7cb80a7422b8bd344ec9328a43884d62f409af6e9ec161c73a2f4d'
PINS = {
    'publication': ('build/r31-action-natural/main-current-action-publication/POST_PUBLISH.json', '3e9dcce55df3979932d3f07bc5e7a4576f524d48a23cf780db4b955a753d3985'),
    'combined': ('build/r31-action-natural/current-stack-combined-final/RESULT.json', '58c0242b95a8c027f6ca9439d342548ab79d1e6e71f7633bcc5dba51ddb27b5f'),
    'sources': ('build/r31-action-natural/current-stack-combined-final/SOURCE_INVENTORY_CURRENT.json', 'f17ef9224b5cbc50708b4fc6471c9fdf4b2112b1944a75d8d8b61601e96ab988'),
}


def fresh(path, kind):
    output = BASE.runtime_path(path, EVIDENCE, exists=False)
    require(output.parent == EVIDENCE.resolve() and re.fullmatch(kind + '-[0-9a-f]{16}', output.name),
            'Dedicated R32 immediate child required')
    require(not output.exists(), 'Fresh R32 output required')
    return output


def current(bindings):
    package, files = N.current_package(bindings)
    receipts = {}
    for name, (relative, digest) in PINS.items():
        path = N.checked_descriptor({'path': str(ROOT / relative), 'sha256': digest}, bindings)
        receipts[name] = read(path)
    require(receipts['publication']['status'] == 'PASS' and
            receipts['publication']['head'] == 'f14e57c9e5e94558a66a0f028cf99801f37ccd84', 'R31 publication differs')
    sources = receipts['sources']
    require(len(sources) == 272 and all(sha(p) == h for p, h in sources.items()), 'R31 current source baseline changed')
    bindings.update(sources)
    bindings.update({str(p.resolve(strict=True)): sha(p) for p in SOURCES})
    return package, files


def compose(output):
    output = fresh(output, 'package')
    bindings = {}
    parent, files = current(bindings)
    require(MEMBER not in files, 'New fixture already exists in parent')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(parent, package)
    shutil.copyfile(PROBE, package / MEMBER)
    manifest = read(package / 'LAUNCHER_PACKAGE.json')
    manifest['files'][MEMBER] = sha(PROBE)
    write_json(package / 'LAUNCHER_PACKAGE.json', manifest)
    result = {'schema': 1, 'scope': SCOPE, 'package': str(package), 'files': BASE.runtime_inventory(package),
              'parent': N.descriptor(ROOT / N.PINS['derivation'][0]), 'probe_sha256': sha(PROBE),
              'native_build': 'REUSED_HASH_VERIFIED_NOT_REBUILT', 'device_tested': False}
    write_json(output / 'DERIVATION.json', result)
    checked_package(output / 'DERIVATION.json', {})
    return result


def checked_package(path, bindings):
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    parent, original = current(bindings)
    require(set(result) == {'schema', 'scope', 'package', 'files', 'parent', 'probe_sha256', 'native_build', 'device_tested'} and
            type(result['schema']) is int and result['schema'] == 1 and result['scope'] == SCOPE and
            result['device_tested'] is False and result['native_build'] == 'REUSED_HASH_VERIFIED_NOT_REBUILT' and
            result['parent'] == N.descriptor(ROOT / N.PINS['derivation'][0]), 'R32 package derivation differs')
    package = BASE.runtime_path(result['package'], EVIDENCE)
    require(path.name == 'DERIVATION.json' and package == path.parent / 'package' and
            path.parent.parent == EVIDENCE.resolve() and re.fullmatch('package-[0-9a-f]{16}', path.parent.name),
            'Exact derived package location required')
    files = BASE.runtime_inventory(package)
    require(files == result['files'] and set(files) == set(original) | {MEMBER} and len(files) == 3237 and
            all(files[k] == v for k, v in original.items() if k != 'LAUNCHER_PACKAGE.json') and
            files[MEMBER] == result['probe_sha256'] == sha(PROBE), 'Only new fixture and manifest delta allowed')
    expected = read(parent / 'LAUNCHER_PACKAGE.json')
    expected['files'][MEMBER] = sha(PROBE)
    require(read(package / 'LAUNCHER_PACKAGE.json') == expected and
            expected['files'] == {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'}, 'Exact manifest delta required')
    bindings[str(path)] = sha(path)
    return package, files


def qualification(path, derivation, bindings):
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    require(result.get('schema') == 1 and result.get('status') == 'PASS_FINAL_HOST' and result.get('scope') == SCOPE and
            result.get('device_tested') is False and result.get('package') == N.descriptor(derivation),
            'Independent final host qualification for this package required')
    inventory = N.checked_descriptor(result['source_inventory'], bindings)
    sources = read(inventory)
    expected_sources = read(ROOT / PINS['sources'][0])
    expected_sources.update({str(p.resolve(strict=True)): sha(p) for p in SOURCES})
    require(sources == expected_sources and len(sources) == 276 and all(sha(p) == h for p, h in sources.items()),
            'Independent current 276-source qualification differs')
    bindings[str(path)] = sha(path)


def route_binding(path, digest):
    selected = Path(path).resolve(strict=True)
    if selected == SDK_LIBCXX.resolve(strict=True):
        require(digest == SDK_LIBCXX_SHA and sha(selected) == SDK_LIBCXX_SHA, 'Pinned query SDK product changed')
    else:
        selected = BASE.runtime_path(selected, ROOT / 'build')
        require(sha(selected) == digest, 'Original route query changed')
    return selected


def prepare(args):
    output = fresh(args.output, 'prepared')
    bindings = {}
    package, files = checked_package(args.derivation, bindings)
    qualification(args.qualification, args.derivation, bindings)
    require(sha(args.external) == BAG.EXTERNAL_SHA and sha(args.identity) == BAG.IDENTITY_SHA, 'Identity/provider expansion forbidden')
    N.current_elf_closure(package, files, bindings, read(args.external))
    routes = read(args.routes)
    for path in (args.derivation, args.qualification, args.routes, args.external, args.identity):
        selected = BASE.runtime_path(path, ROOT / 'build')
        bindings[str(selected)] = sha(selected)
    require(routes.get('cross_confirmed') is True and routes.get('bindings'), 'Paired routes required')
    for path, digest in routes['bindings'].items():
        selected = route_binding(path, digest)
        bindings[str(selected)] = digest
    scenario = secrets.token_hex(8)
    runs = {key: secrets.token_hex(8) for key in O.INSTANCES}
    require(len(set(runs.values()) | {scenario}) == 4, 'Unique process/scenario identities required')
    result = {'schema': 1, 'scope': SCOPE, 'scenario': scenario, 'runs': runs, 'limits': LIMITS,
              'derivation': str(args.derivation.resolve()), 'qualification': str(args.qualification.resolve()),
              'package': str(package), 'files': files, 'bindings': bindings, 'routes': str(args.routes.resolve()),
              'external': str(args.external.resolve()), 'identity': str(args.identity.resolve())}
    # Pure validation must complete before any prepared artifact is created.
    validate_plan(result)
    output.mkdir()
    write_json(output / 'PLAN.json', result)
    return result


def checked_plan(path):
    path = BASE.runtime_path(path, EVIDENCE)
    require(path.name == 'PLAN.json' and path.parent.parent == EVIDENCE.resolve() and
            re.fullmatch('prepared-[0-9a-f]{16}', path.parent.name), 'Exact R32 prepared location required')
    return validate_plan(read(path))


def validate_plan(plan):
    require(set(plan) == {'schema', 'scope', 'scenario', 'runs', 'limits', 'derivation', 'qualification', 'package', 'files',
                         'bindings', 'routes', 'external', 'identity'} and type(plan['schema']) is int and plan['schema'] == 1 and
            plan['scope'] == SCOPE and plan['limits'] == LIMITS and all(type(v) is int for v in plan['limits'].values()) and
            set(plan['runs']) == set(O.INSTANCES) and all(isinstance(x, str) and re.fullmatch('[0-9a-f]{16}', x)
            for x in [plan['scenario'], *plan['runs'].values()]) and len(set(plan['runs'].values()) | {plan['scenario']}) == 4,
            'Prepared identity/schema/limits invalid')
    expected = {}
    package, files = checked_package(plan['derivation'], expected)
    qualification(plan['qualification'], plan['derivation'], expected)
    require(str(package) == plan['package'] and files == plan['files'], 'Prepared package changed')
    require(sha(plan['external']) == BAG.EXTERNAL_SHA and sha(plan['identity']) == BAG.IDENTITY_SHA, 'Identity/provider drift')
    N.current_elf_closure(package, files, expected, read(plan['external']))
    for key in ('derivation', 'qualification', 'routes', 'external', 'identity'):
        selected = BASE.runtime_path(plan[key], ROOT / 'build')
        expected[str(selected)] = sha(selected)
    routes = read(plan['routes'])
    require(routes.get('cross_confirmed') is True and routes.get('bindings'), 'Original paired query required')
    for path, digest in routes['bindings'].items():
        selected = route_binding(path, digest)
        expected[str(selected)] = digest
    require(plan['bindings'] == expected and all(sha(p) == h for p, h in expected.items()), 'Prepared bindings drift')
    observed = datetime.fromisoformat(routes['observed_utc'])
    require(observed.utcoffset() is not None and 0 <= (datetime.now(timezone.utc) - observed).total_seconds() <= 300,
            'Query preparation freshness expired; business budget is separate')
    peers, udids = {}, {}
    for side in 'AB':
        row = routes['sides'][side]
        require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == row['remote_exit'] == 0, 'Peer target/exit mismatch')
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


class NaturalActionProcess(P.RestartProcess):
    def argv(self, side):
        result = super().argv(side)
        result[2] = self.prefix[side] + '/package/' + MEMBER
        return result

    def validate_poll(self, text, side):
        O.rows(text, self.scenario, self.instance, self.run, self.owned[side]['child']['pid'])

    def configuration_fields(self, side, fields):
        prefix = 'com.kaihong.mdds.r32.' + self.scenario
        peer = 'B' if side == 'A' else 'A'
        fields.update(service_prefix=prefix, local_initiator_name=prefix + '.' + side + '.tx',
                      peer_initiator_name=prefix + '.' + peer + '.tx')
        return fields

    def configure(self, side):
        super().configure(side)
        config, root = self.output / (side + '.private.conf'), self.prefix[side]
        remote = self.shell(side, 'sha256sum ' + shlex.quote(root + '/ros.conf')).decode().strip().split()
        require(remote == [sha(config), root + '/ros.conf'], 'Deployed R32 configuration differs')
        self.write(side + '.config_verified.json', {'sha256': sha(config), 'remote_path': root + '/ros.conf'})

    def extra_control(self, name):
        require(self.instance == 'A' and name in ('ARM', 'REPLACE'), 'Only existing A lifecycle controls permitted')
        return super().extra_control(name)


class NaturalActionScene(P.RestartScene):
    def __init__(self, args):
        BAG.bag_selected_hdc(args.hdc)
        self.args = args
        self.output = fresh(args.output, 'execution')
        require(not self.output.exists(), 'Fresh execution required')
        self.plan, peers, udids, tokens, self.observed = checked_plan(args.prepared)
        self.output.mkdir()
        self.errors, self.blocked, self.processes = [], [], {}
        self.deadline, self.origin = None, None
        poll_resample_budget = {'used': False}
        for key in O.INSTANCES:
            self.processes[key] = NaturalActionProcess(args, self.plan, key, peers, udids, tokens, self.observed, self.output / key)
            self.processes[key].poll_resample_budget = poll_resample_budget
        self.write('PLAN.json', self.plan)

    def records(self, text, key):
        p = self.processes[key]
        return O.rows(text, p.scenario, key, p.run, p.owned[p.side]['child']['pid'])

    def has(self, texts, key, event, phase=None):
        return bool(O.select(self.records(texts[key], key), event, phase))

    def stamp(self, name, **fields):
        require(time.monotonic() < self.deadline, 'Action stage deadline before evidence write')
        self.write(name + '.json', {'monotonic': time.monotonic(), 'stage_deadline': self.deadline, **fields})
        require(time.monotonic() < self.deadline, 'Action stage deadline after evidence write')

    def workflow(self):
        a, b0, b1 = (self.processes[k] for k in O.INSTANCES)
        for process in (a, b0, b1):
            process.deploy()
        self.budget(time.monotonic() + 60, 'INITIAL_LISTENERS')
        a.launch('A')
        b0.launch('B')
        self.wait(('A', 'B0'), lambda t, s: all(self.checkpoint(t, k, 'LISTENER') for k in ('A', 'B0')))
        a.maps('A', 'listener')
        b0.maps('B', 'listener')
        require(time.monotonic() < self.deadline, 'Initial maps exceeded deadline')
        self.origin = time.monotonic()
        self.write('CLOCK.json', {'origin': 'BEFORE_FIRST_LISTENER_RELEASE', 'business_origin': self.origin,
            'business_deadline': self.origin + LIMITS['business_seconds'],
            'normal_close_release_deadline': self.origin + LIMITS['normal_close_release_seconds'],
            'probe_lifetime_seconds': LIMITS['probe_seconds'], 'probe_origin': 'EACH_PROCESS_CREATE_NODE_RETURN',
            'preparation_query_utc': self.observed.isoformat(), 'limits': LIMITS,
            'measurement': 'HOST_CONTROL_AND_CAPTURE_INCLUDED_NOT_NETWORK_LATENCY'})
        self.budget(self.origin + LIMITS['business_seconds'], 'ACTION_BUSINESS')
        for p in (a, b0):
            p.publish(p.side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + p.run + ' side=' + p.side + ' stage=LISTENER_READY\n')
        texts = self.wait(('A', 'B0'), lambda t, s: self.has(t, 'A', 'CLIENT_RESULT', 'PRE') and
                          self.has(t, 'B0', 'SERVER_RESULT', 'PRE'))
        pre = O.pair(self.records(texts['A'], 'A'), self.records(texts['B0'], 'B0'), self.plan['scenario'], 'PRE')
        self.stamp('PRE', result=pre)
        a.extra_control('ARM')
        self.wait(('A', 'B0'), lambda t, s: any(r.get('stage') == 'ARMED' and r.get('run') == a.run and
            r.get('side') == 'A' and r.get('source') == 'SDK_PUBLIC_SOCKET' for r in O.fields(t['A'], 'NATIVE_REACCEPT_EVENT')))
        accepted = self.wait(('A', 'B0'), lambda t, s: self.has(t, 'A', 'CLIENT_ACCEPTED', 'OLD') and
                             self.has(t, 'A', 'CLIENT_RESULT_REQUEST', 'OLD') and self.has(t, 'B0', 'SERVER_GOAL', 'OLD'))
        self.stamp('OLD_ACCEPTED', result=O.accepted_pair(self.records(accepted['A'], 'A'),
                                                      self.records(accepted['B0'], 'B0'), self.plan['scenario'], 'OLD'))
        self.stamp('B0_NORMAL_STOP_INTENT', owned=b0.owned['B'], method='PUBLIC_PROBE_FINISH')
        b0.finish_fixture('B', 'NORMAL_COMPLETION')
        self.wait(('A', 'B0'), lambda t, s: self.checkpoint(t, 'B0', 'CLOSE'))
        b0.maps('B', 'close')
        b0.close_release()
        self.wait(('A', 'B0'), lambda t, s: bool(s['B0']), exits=('B0',))
        b0.confirm_absent()
        self.stamp('B0_NORMAL_EXIT_CONFIRMED', owned=b0.owned['B'])
        old = self.wait(('A',), lambda t, s: O.owners(t['A'], a.run, 'OLD_BOTH_READY') is not None and
                        self.has(t, 'A', 'ACTION_UNAVAILABLE'))
        self.stamp('ACTION_UNAVAILABLE', observation=O.one(self.records(old['A'], 'A'), 'ACTION_UNAVAILABLE'),
                   owners=O.owners(old['A'], a.run, 'OLD_BOTH_READY'))
        b1.launch('B')
        require(b0.owned['B']['child']['pid'] != b1.owned['B']['child']['pid'] and
                b0.owned['B']['child']['starttime'] != b1.owned['B']['child']['starttime'], 'B process identity reused')
        self.stamp('B1_LAUNCHED', owned=b1.owned['B'])
        self.wait(('A', 'B1'), lambda t, s: self.checkpoint(t, 'B1', 'LISTENER'))
        b1.maps('B', 'listener')
        a.extra_control('REPLACE')
        b1.publish('B', 'control.LISTENER_READY', 'ROS_RELEASE run=' + b1.run + ' side=B stage=LISTENER_READY\n')
        active = self.wait(('A', 'B1'), lambda t, s: O.owners(t['A'], a.run, 'NEW_BOTH_ACTIVE') is not None)
        self.stamp('NEW_BOTH_ACTIVE', observation=O.owners(active['A'], a.run, 'NEW_BOTH_ACTIVE'),
                   continuity=O.a_continuity(active['A'], a.run))
        texts = self.wait(('A', 'B1'), lambda t, s: self.has(t, 'A', 'PHASE_COMPLETE', 'NEW') and
                          self.has(t, 'B1', 'PHASE_COMPLETE', 'NEW'))
        post = O.pair(self.records(texts['A'], 'A'), self.records(texts['B1'], 'B1'), self.plan['scenario'], 'NEW')
        self.stamp('BUSINESS', pre=pre, new=post, application_goals=3,
                   old_response_observed=bool(O.select(self.records(texts['A'], 'A'), 'CLIENT_RESULT', 'OLD')))
        self.budget(self.origin + LIMITS['normal_close_release_seconds'], 'FINAL_CLOSE')
        for p in (a, b1):
            p.maps(p.side, 'business')
            p.finish_fixture(p.side, 'NORMAL_COMPLETION')
        self.wait(('A', 'B1'), lambda t, s: all(self.checkpoint(t, k, 'CLOSE') for k in ('A', 'B1')))
        for p in (a, b1):
            p.maps(p.side, 'close')
            p.close_release()
        self.stamp('NORMAL_CLOSE_RELEASED')
        self.budget(time.monotonic() + LIMITS['final_exit_seconds'], 'FINAL_EXIT')
        self.wait(('A', 'B1'), lambda t, s: all(s.values()), exits=('A', 'B1'))
        for p in (a, b1):
            p.confirm_absent()
        self.stamp('FINAL_EXIT_CONFIRMED')
        self.budget(time.monotonic() + LIMITS['analysis_seconds'], 'ANALYZE')
        texts = {key: p.collect()[p.side] for key, p in self.processes.items()}
        self.analyze(texts)
        require(time.monotonic() < self.deadline, 'Final analysis crossed deadline')

    def analyze(self, texts):
        identities, diagnostics, records = {}, {}, {}
        for key, p in self.processes.items():
            side, pid = p.side, p.owned[p.side]['child']['pid']
            identities[key] = {'run': p.run, 'pid': pid}
            records[key] = self.records(texts[key], key)
            require(O.fields(texts[key], 'ROS_PYTHON_INTERPRETER_RETURN') == [{'execution_rc': '0', 'finalize_rc': '0'}] and
                    O.fields(texts[key], 'ROS_PYTHON_FINAL') == [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1',
                    'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}], 'Python/native finalization failed')
            diagnostics[key] = R.recordplay_errors((p.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8'),
                (p.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8'), p.run, side, pid, p.files)
        content = O.all_content(records, self.plan['scenario'], self.plan['files'])
        a = self.processes['A']
        require(O.owners(texts['A'], a.run, 'NEW_BOTH_ACTIVE'), 'Final dual replacement missing')
        continuity = O.a_continuity(texts['A'], a.run)
        native = P.N.native_capture(texts, identities)
        endpoints = O.action_endpoints(texts, self.plan['scenario'], a.run)
        require(continuity['participant'][:24] == native['participant_prefixes']['A'], 'A wire identity disagrees')
        self.stamp('ANALYSIS', scope=SCOPE, content=content,
                   native=native, A_continuity=continuity, action_endpoints=endpoints, diagnostics=diagnostics, sdk_epoch='NOT_PROVEN',
                   old_goal_recovery='NOT_CLAIMED', cancel_behavior='NOT_TESTED', long_duration='NOT_TESTED')

    def execute(self):
        consumed = False
        try:
            release = read(self.args.release)
            require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_R32_NATURAL_ACTION_RESTART_RELEASE',
                'scenario': self.plan['scenario'], 'prepared_sha256': sha(self.args.prepared), 'runner_sha256': sha(__file__)},
                'Exact Main one-shot action release required')
            checked_plan(self.args.prepared)
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < LIMITS['query_dispatch_seconds'],
                    'Route freshness expired before dispatch')
            with (self.args.prepared.parent / (self.plan['scenario'] + '.CONSUMED.json')).open('x', encoding='utf-8') as stream:
                json.dump({'release_sha256': sha(self.args.release), 'output': str(self.output),
                           'utc': datetime.now(timezone.utc).isoformat()}, stream)
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
            self.write('RESULT.json', {'scope': SCOPE, 'scenario': self.plan['scenario'],
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors,
                'blocked': self.blocked, 'release_consumed': consumed, 'autonomous_new_goal_after_guarded_replacement': passed, 'old_goal_recovery': 'NOT_CLAIMED', 'business_permissions': 'FORBIDDEN',
                'sdk_epoch': 'NOT_PROVEN', 'default_automatic_reconnect': 'NOT_CLAIMED',
                'evidence': {str(p): sha(p) for p in self.output.rglob('*') if p.is_file()}})
        return 0 if passed else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    c = sub.add_parser('compose')
    c.add_argument('--output', type=Path, required=True)
    p = sub.add_parser('prepare')
    for name in ('derivation', 'qualification', 'routes', 'external', 'identity', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    r = sub.add_parser('run')
    for name in ('prepared', 'release', 'hdc', 'output'):
        r.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args(argv)
    if args.command == 'compose':
        compose(args.output)
    elif args.command == 'prepare':
        prepare(args)
    else:
        return NaturalActionScene(args).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
