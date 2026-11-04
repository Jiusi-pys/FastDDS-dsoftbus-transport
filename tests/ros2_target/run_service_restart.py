"""Finite AddTwoInts service recovery using the unchanged R27 native control path."""
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
import run_process_restart as P
import service_restart_oracle as O

HERE, REPO, ROOT = P.HERE, P.REPO, P.ROOT
EVIDENCE = ROOT / 'build/r29-service-restart'
BASE, BAG, NATIVE, S, R = P.BASE, P.BAG, P.NATIVE, P.S, P.R
sha, read, require, write_json = P.sha, P.read, O.require, P.write_json
PROBE = REPO / 'integrations/ros2/target/service_restart_probe.py'
MEMBER = 'scripts/service_restart_probe.py'
LIMITS = {'business_seconds': 35, 'probe_seconds': 40, 'normal_close_release_seconds': 50,
          'final_exit_seconds': 20, 'analysis_seconds': 60, 'query_construct_seconds': 300,
          'query_dispatch_seconds': 240, 'replacements': 1, 'application_requests': 3}
SOURCES = [Path(__file__), PROBE, HERE / 'service_restart_oracle.py', HERE / 'test_service_restart.py', *P.SOURCES]



def fresh(path, kind):
    path = Path(path).absolute()
    require(path.parent == EVIDENCE and re.fullmatch(kind + '-[0-9a-f]{16}', path.name), 'Fresh R29 child required')
    return BASE.runtime_path(path, EVIDENCE, exists=False)


def checked_bridge(receipt):
    receipt = BASE.runtime_path(receipt, EVIDENCE)
    result = read(receipt)
    require(result.get('status') in ('PASS_INITIAL_HOST', 'PASS_FINAL_HOST'), 'Independent build/host receipt required')
    mode = result['target_build']['modes']['on']
    bridge = Path(mode['bridge']).resolve()
    allowed = ROOT / 'build/r28-repeated-restart/device-fix1-initial-tester/target-on-fix1'
    require(bridge.is_relative_to(EVIDENCE) or bridge.parent == allowed, 'Only R29 or exact unchanged R28 bridge')
    bridge = BASE.runtime_path(bridge, ROOT / 'build')
    require(sha(bridge) == mode['bridge_sha256'], 'Experimental bridge changed')
    inventory = result['source_inventory']
    require(sha(inventory['path']) == inventory['sha256'], 'Source inventory changed')
    sources = read(inventory['path'])
    require(sources and all(sha(p) == h for p, h in sources.items()) and
            all(str(p.resolve()) in sources for p in SOURCES), 'Build/host source binding incomplete or changed')
    return bridge


def compose(receipt, output):
    output = fresh(output, 'package')
    require(not output.exists(), 'Package already exists')
    bridge = checked_bridge(receipt)
    require(sha(S.PARENT / 'RUNTIME_PACKAGE.json') == S.PARENT_SHA, 'Parent receipt changed')
    parent = read(S.PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    require(BASE.runtime_inventory(S.PARENT / 'package') == parent, 'Original package changed')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(S.PARENT / 'package', package)
    shutil.copyfile(bridge, package / P.BRIDGE)
    for source, member in ((P.PROBE, P.MEMBER), (PROBE, MEMBER)):
        shutil.copyfile(source, package / member)
    manifest = read(package / 'LAUNCHER_PACKAGE.json')
    manifest['files'].update({m: sha(package / m) for m in (P.BRIDGE, P.MEMBER, MEMBER)})
    write_json(package / 'LAUNCHER_PACKAGE.json', manifest)
    result = {'schema': 1, 'scope': 'R29_ONE_SERVICE_SERVER_REPLACEMENT', 'package': str(package),
              'files': BASE.runtime_inventory(package), 'parent_sha256': S.PARENT_SHA,
              'build_receipt': str(Path(receipt).resolve()), 'build_receipt_sha256': sha(receipt),
              'bridge_sha256': sha(bridge), 'probe_sha256': sha(PROBE), 'device_tested': False}
    write_json(output / 'DERIVATION.json', result)
    checked_package(output / 'DERIVATION.json')
    return result


def checked_package(path):
    result = read(BASE.runtime_path(path, EVIDENCE))
    require(result.get('schema') == 1 and result.get('scope') == 'R29_ONE_SERVICE_SERVER_REPLACEMENT' and
            result.get('device_tested') is False and result.get('parent_sha256') == S.PARENT_SHA and
            sha(result['build_receipt']) == result['build_receipt_sha256'], 'Derivation identity changed')
    bridge = checked_bridge(result['build_receipt'])
    package = BASE.runtime_path(result['package'], EVIDENCE)
    files = BASE.runtime_inventory(package)
    require(sha(S.PARENT / 'RUNTIME_PACKAGE.json') == S.PARENT_SHA, 'Parent receipt changed')
    parent = read(S.PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    require(files == result['files'] and set(files) == set(parent) | {P.MEMBER, MEMBER} and
            all(files[k] == v for k, v in parent.items() if k not in (P.BRIDGE, 'LAUNCHER_PACKAGE.json')) and
            files[P.BRIDGE] == result['bridge_sha256'] == sha(bridge) and files[P.MEMBER] == sha(P.PROBE) and
            files[MEMBER] == result['probe_sha256'] == sha(PROBE), 'Unexpected service package change')
    require(read(package / 'LAUNCHER_PACKAGE.json')['files'] == {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'},
            'Manifest content drift')
    return package, files


def prepare(args):
    output = fresh(args.output, 'prepared')
    require(not output.exists(), 'Prepared output already exists')
    package, files = checked_package(args.derivation)
    require(sha(args.external) == BAG.EXTERNAL_SHA and sha(args.identity) == BAG.IDENTITY_SHA, 'Identity/provider expansion forbidden')
    routes = read(args.routes)
    bindings = {str(p.resolve()): sha(p) for p in [args.derivation, args.routes, args.external, args.identity, *SOURCES]}
    require(routes.get('cross_confirmed') is True and routes.get('bindings'), 'Paired routes required')
    for path, digest in routes['bindings'].items():
        require(sha(path) == digest, 'Original route query changed')
        bindings[path] = digest
    scenario = secrets.token_hex(8)
    runs = {key: secrets.token_hex(8) for key in O.INSTANCES}
    require(len(set(runs.values()) | {scenario}) == 4, 'Unique process/scenario identities required')
    result = {'schema': 1, 'scope': 'R29_SERVICE_RESTART_PREPARED_NOT_DISPATCHED', 'scenario': scenario, 'runs': runs,
              'limits': LIMITS, 'derivation': str(args.derivation.resolve()), 'package': str(package), 'files': files,
              'bindings': bindings, 'routes': str(args.routes.resolve()), 'external': str(args.external.resolve()),
              'identity': str(args.identity.resolve())}
    output.mkdir()
    write_json(output / 'PLAN.json', result)
    return result


def checked_plan(path):
    plan = read(path)
    require(set(plan) == {'schema', 'scope', 'scenario', 'runs', 'limits', 'derivation', 'package', 'files', 'bindings',
                         'routes', 'external', 'identity'} and type(plan['schema']) is int and plan['schema'] == 1 and
            plan['scope'] == 'R29_SERVICE_RESTART_PREPARED_NOT_DISPATCHED' and plan['limits'] == LIMITS and
            all(type(v) is int for v in plan['limits'].values()) and set(plan['runs']) == set(O.INSTANCES) and
            all(isinstance(x, str) and re.fullmatch('[0-9a-f]{16}', x) for x in [plan['scenario'], *plan['runs'].values()]) and
            len(set(plan['runs'].values()) | {plan['scenario']}) == 4, 'Prepared schema/identity/limits invalid')
    package, files = checked_package(plan['derivation'])
    require(str(package) == plan['package'] and files == plan['files'], 'Prepared package changed')
    require(sha(plan['external']) == BAG.EXTERNAL_SHA and sha(plan['identity']) == BAG.IDENTITY_SHA, 'Identity/provider receipt drift')
    routes = read(plan['routes'])
    expected = {str(Path(plan[k]).resolve()): sha(plan[k]) for k in ('derivation', 'routes', 'external', 'identity')}
    expected.update({str(p.resolve()): sha(p) for p in SOURCES})
    require(routes.get('bindings') and routes.get('cross_confirmed') is True, 'Original paired query binding required')
    expected.update(routes['bindings'])
    require(plan['bindings'] == expected and all(sha(p) == h for p, h in expected.items()), 'Incomplete/changed input bindings')
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


class ServiceProcess(P.RestartProcess):
    def argv(self, side):
        result = super().argv(side)
        result[2] = self.prefix[side] + '/package/' + MEMBER
        return result

    def validate_poll(self, text, side):
        O.rows(text, self.scenario, self.instance, self.run, self.owned[side]['child']['pid'])

    def configuration_fields(self, side, fields):
        prefix = 'com.kaihong.mdds.r29.' + self.scenario
        peer = 'B' if side == 'A' else 'A'
        fields.update(service_prefix=prefix, local_initiator_name=prefix + '.' + side + '.tx',
                      peer_initiator_name=prefix + '.' + peer + '.tx')
        return fields

    def extra_control(self, name):
        if name in ('ARM', 'REPLACE'):
            return super().extra_control(name)
        require(self.instance == 'A' and name in ('OLD', 'POST'), 'Only finite service client controls')
        self.guard('A')
        self.root_guard('A')
        data = ('R29_CONTROL schema=1 scenario=' + self.scenario + ' run=' + self.run + ' side=A pid=' +
                str(self.owned['A']['child']['pid']) + ' phase=' + name + '\n')
        target = self.prefix['A'] + '/control.SERVICE_' + name
        self.shell('A', 'umask 077; set -C; test ! -e ' + target + ' && test ! -L ' + target +
            ' && test ! -e ' + target + '.tmp && test ! -L ' + target + '.tmp && printf %s ' + shlex.quote(data) +
            ' > ' + target + '.tmp && chmod 600 ' + target + '.tmp && mv ' + target + '.tmp ' + target)
        self.write('releases/' + name + '.json', {'contents': data, 'owned': self.owned['A'], 'monotonic': time.monotonic()})


class ServiceScene(P.RestartScene):
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
            self.processes[key] = ServiceProcess(args, self.plan, key, peers, udids, tokens, self.observed, self.output / key)
            self.processes[key].poll_resample_budget = poll_resample_budget
        self.write('PLAN.json', self.plan)

    def records(self, text, key):
        p = self.processes[key]
        return O.rows(text, p.scenario, key, p.run, p.owned[p.side]['child']['pid'])

    def has(self, texts, key, event, phase=None):
        return bool(O.select(self.records(texts[key], key), event, phase))

    def stamp(self, name, **fields):
        require(time.monotonic() < self.deadline, 'Service stage deadline before evidence write')
        self.write(name + '.json', {'monotonic': time.monotonic(), 'stage_deadline': self.deadline, **fields})
        require(time.monotonic() < self.deadline, 'Service stage deadline after evidence write')

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
        self.budget(self.origin + LIMITS['business_seconds'], 'SERVICE_BUSINESS')
        for p in (a, b0):
            p.publish(p.side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + p.run + ' side=' + p.side + ' stage=LISTENER_READY\n')
        texts = self.wait(('A', 'B0'), lambda t, s: self.has(t, 'A', 'CLIENT_RESPONSE', 'PRE') and
                          self.has(t, 'B0', 'SERVER_PAUSED'))
        pre = O.pair(self.records(texts['A'], 'A'), self.records(texts['B0'], 'B0'), self.plan['scenario'], 'PRE')
        self.stamp('PRE', result=pre)
        a.extra_control('ARM')
        self.wait(('A', 'B0'), lambda t, s: any(r.get('stage') == 'ARMED' and r.get('run') == a.run and
            r.get('side') == 'A' and r.get('source') == 'SDK_PUBLIC_SOCKET' for r in O.fields(t['A'], 'NATIVE_REACCEPT_EVENT')))
        a.extra_control('OLD')
        old = self.wait(('A', 'B0'), lambda t, s: self.has(t, 'A', 'OLD_PENDING'))
        old_records = self.records(old['A'], 'A')
        require(not O.select(old_records, 'CLIENT_RESPONSE', 'OLD'), 'OLD unexpectedly completed before B0 stop')
        require(not O.select(old_records, 'SERVICE_UNAVAILABLE'), 'Service disappeared before controlled B0 stop')
        self.stamp('OLD_PENDING', result=O.pending(old_records))
        self.stamp('B0_NORMAL_STOP_INTENT', owned=b0.owned['B'], method='PUBLIC_PROBE_FINISH')
        b0.finish_fixture('B', 'NORMAL_COMPLETION')
        self.wait(('A', 'B0'), lambda t, s: self.checkpoint(t, 'B0', 'CLOSE'))
        b0.maps('B', 'close')
        b0.close_release()
        self.wait(('A', 'B0'), lambda t, s: bool(s['B0']), exits=('B0',))
        b0.confirm_absent()
        self.stamp('B0_NORMAL_EXIT_CONFIRMED', owned=b0.owned['B'])
        old = self.wait(('A',), lambda t, s: O.owners(t['A'], a.run, 'OLD_BOTH_READY') is not None and
                        self.has(t, 'A', 'SERVICE_UNAVAILABLE'))
        require(not O.select(self.records(old['A'], 'A'), 'CLIENT_RESPONSE', 'OLD'), 'OLD response before B1 launch')
        self.stamp('SERVICE_UNAVAILABLE', observation=O.one(self.records(old['A'], 'A'), 'SERVICE_UNAVAILABLE'),
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
        a.extra_control('POST')
        texts = self.wait(('A', 'B1'), lambda t, s: self.has(t, 'A', 'PHASE_COMPLETE', 'NEW') and
                          self.has(t, 'B1', 'PHASE_COMPLETE', 'NEW'))
        post = O.pair(self.records(texts['A'], 'A'), self.records(texts['B1'], 'B1'), self.plan['scenario'], 'NEW')
        self.stamp('BUSINESS', pre=pre, new=post, application_requests=3,
                   old_response_observed=bool(O.select(self.records(texts['A'], 'A'), 'CLIENT_RESPONSE', 'OLD')))
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
        endpoints = O.service_endpoints(texts, self.plan['scenario'], a.run)
        require(continuity['participant'][:24] == native['participant_prefixes']['A'], 'A wire identity disagrees')
        self.stamp('ANALYSIS', scope='ONE_NORMAL_SERVICE_SERVER_REPLACEMENT', content=content,
                   native=native, A_continuity=continuity, service_endpoints=endpoints, diagnostics=diagnostics, sdk_epoch='NOT_PROVEN',
                   action_recovery='NOT_TESTED', long_duration='NOT_TESTED')

    def execute(self):
        consumed = False
        try:
            release = read(self.args.release)
            require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_R29_SERVICE_RESTART_RELEASE',
                'scenario': self.plan['scenario'], 'prepared_sha256': sha(self.args.prepared), 'runner_sha256': sha(__file__)},
                'Exact Main one-shot service release required')
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
            self.write('RESULT.json', {'scope': 'ONE_NORMAL_SERVICE_SERVER_REPLACEMENT', 'scenario': self.plan['scenario'],
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors,
                'blocked': self.blocked, 'release_consumed': consumed, 'service_restart_verified': passed,
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
        return ServiceScene(args).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
