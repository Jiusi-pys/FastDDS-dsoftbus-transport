"""One continuous A with exactly B0->B1->B2 normal process replacements."""
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
import repeated_restart_oracle as O

HERE, REPO, ROOT = P.HERE, P.REPO, P.ROOT
EVIDENCE = ROOT / 'build/r28-repeated-restart'
BASE, BAG, NATIVE, S, R = P.BASE, P.BAG, P.NATIVE, P.S, P.R
sha, read, require, write_json = P.sha, P.read, O.require, P.write_json
PROBE = REPO / 'integrations/ros2/target/repeated_restart_probe.py'
MEMBER = 'scripts/repeated_restart_probe.py'
LIMITS = {'business_seconds': 35, 'probe_seconds': 40, 'normal_close_release_seconds': 50,
          'final_exit_seconds': 20, 'analysis_seconds': 60, 'query_construct_seconds': 300,
          'query_dispatch_seconds': 240, 'replacements': 2, 'roundtrips_per_phase': 5}
SOURCES = [Path(__file__), PROBE, HERE / 'repeated_restart_oracle.py', HERE / 'test_repeated_restart.py',
           HERE / 'repeated_process_rounds_tests.cpp', *P.SOURCES,
           REPO / 'integrations/ros2/RepeatedProcessRounds.hpp',
           REPO / 'include/fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp',
           REPO / 'src/PublicSocketBackend.cpp', REPO / 'tests/bounded_native_mode_tests.cpp']


def fresh(path, kind):
    path = Path(path).absolute()
    require(path.parent == EVIDENCE and re.fullmatch(kind + '-[0-9a-f]{16}', path.name), 'Fresh R28 child required')
    return BASE.runtime_path(path, EVIDENCE, exists=False)


def checked_bridge(receipt):
    receipt = BASE.runtime_path(receipt, EVIDENCE)
    result = read(receipt)
    require(result.get('status') in ('PASS_INITIAL_HOST', 'PASS_FINAL_HOST'), 'Independent build/host receipt required')
    mode = result['target_build']['modes']['on']
    bridge = BASE.runtime_path(mode['bridge'], EVIDENCE)
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
    result = {'schema': 1, 'scope': 'R28_FIXED_TWO_REPLACEMENTS', 'package': str(package),
              'files': BASE.runtime_inventory(package), 'parent_sha256': S.PARENT_SHA,
              'build_receipt': str(Path(receipt).resolve()), 'build_receipt_sha256': sha(receipt),
              'bridge_sha256': sha(bridge), 'probe_sha256': sha(PROBE), 'device_tested': False}
    write_json(output / 'DERIVATION.json', result)
    checked_package(output / 'DERIVATION.json')
    return result


def checked_package(path):
    result = read(BASE.runtime_path(path, EVIDENCE))
    require(result.get('schema') == 1 and result.get('scope') == 'R28_FIXED_TWO_REPLACEMENTS' and
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
            files[MEMBER] == result['probe_sha256'] == sha(PROBE), 'Unexpected repeated package change')
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
    require(len(set(runs.values()) | {scenario}) == 5, 'Unique process/scenario identities required')
    result = {'schema': 1, 'scope': 'R28_TWO_REPLACEMENTS_PREPARED_NOT_DISPATCHED', 'scenario': scenario, 'runs': runs,
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
            plan['scope'] == 'R28_TWO_REPLACEMENTS_PREPARED_NOT_DISPATCHED' and plan['limits'] == LIMITS and
            all(type(v) is int for v in plan['limits'].values()) and set(plan['runs']) == set(O.INSTANCES) and
            all(isinstance(x, str) and re.fullmatch('[0-9a-f]{16}', x) for x in [plan['scenario'], *plan['runs'].values()]) and
            len(set(plan['runs'].values()) | {plan['scenario']}) == 5, 'Prepared schema/identity/limits invalid')
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


class RepeatedProcess(P.RestartProcess):
    def argv(self, side):
        result = super().argv(side)
        result[2] = self.prefix[side] + '/package/' + MEMBER
        return result

    def validate_poll(self, text, side):
        O.rows(text, self.scenario, self.instance, self.run, self.owned[side]['child']['pid'])

    def configuration_fields(self, side, fields):
        service = 'com.kaihong.mdds.r28.' + self.scenario
        fields.update(service_prefix=service, local_initiator_name=service + '.' + side + '.tx',
                      peer_initiator_name=service + '.' + ('B' if side == 'A' else 'A') + '.tx')
        if side == 'A':
            fields['process_restart_cycles'] = '2'
        return fields

    def extra_control(self, name):
        require(self.instance == 'A' and name in ('SAMPLE0', 'ARM1', 'REPLACE1', 'PHASE1', 'SAMPLE1',
                                                 'ARM2', 'REPLACE2', 'PHASE2', 'SAMPLE2', 'COMPLETE'), 'Finite A control required')
        self.guard('A')
        self.root_guard('A')
        if name.startswith('PHASE'):
            phase = int(name[-1])
            member = 'control.REPEAT_PHASE' + str(phase)
            data = 'R28_PHASE schema=1 scenario=' + self.scenario + ' run=' + self.run + ' side=A pid=' + str(
                self.owned['A']['child']['pid']) + ' phase=' + str(phase) + '\n'
        else:
            member = 'control.NATIVE_REACCEPT.' + name
            data = 'NATIVE_CONTROL run=' + self.run + ' side=A phase=' + name + '\n'
        target = self.prefix['A'] + '/' + member
        self.shell('A', 'umask 077; set -C; test ! -e ' + target + ' && test ! -L ' + target +
                   ' && test ! -e ' + target + '.tmp && test ! -L ' + target + '.tmp && printf %s ' + shlex.quote(data) +
                   ' > ' + target + '.tmp && chmod 600 ' + target + '.tmp && mv ' + target + '.tmp ' + target)
        self.write('releases/' + name + '.json', {'contents': data, 'owned': self.owned['A'], 'monotonic': time.monotonic()})

    def resource_sample(self, point):
        require(self.instance == 'A' and point in ('PHASE0', 'AFTER_B0_EXIT', 'PHASE1', 'AFTER_B1_EXIT', 'PHASE2'),
                'Fixed A resource observation point required')
        expected = self.owned['A']['child']
        pid = expected['pid']
        require(type(pid) is int and pid > 1, 'Invalid owned resource PID')
        prefix = '/proc/' + str(pid)
        # One read-only RPC retains both complete identity observations. These
        # sequential /proc reads are not an atomic snapshot; identity drift fails.
        identity = ('printf "STAT\\n" && cat ' + prefix + '/stat && printf "\\nEXE\\n" && readlink ' +
                    prefix + '/exe && printf "CMD\\n" && od -An -tx1 ' + prefix + '/cmdline')
        resource = ('cat ' + prefix + '/stat && printf "\nR28_STATUS\n" && cat ' + prefix +
                    '/status && printf "\nR28_FD\n" && ls -1 ' + prefix + '/fd && printf "\nR28_STAT_END\n" && cat ' + prefix + '/stat')
        capture = self.shell('A', identity + ' && printf "\\nR28_RESOURCE_BEGIN\\n" && ' + resource +
                             ' && printf "\\nR28_RESOURCE_END\\n" && ' + identity)
        require(time.monotonic() < self.stage_deadline, 'Resource capture crossed stage deadline')
        boundaries = re.fullmatch(rb'(.*?)\r?\nR28_RESOURCE_BEGIN\r?\n(.*?)\r?\nR28_RESOURCE_END\r?\n(.*)', capture, re.S)
        require(boundaries is not None and capture.count(b'R28_RESOURCE_BEGIN') == capture.count(b'R28_RESOURCE_END') == 1,
                'Resource capture boundaries incomplete')
        parts = boundaries.groups()
        for observed in (parts[0], parts[2]):
            match = re.fullmatch(r'STAT\r?\n([^\r\n]+)\r?\n\r?\nEXE\r?\n([^\r\n]+)\r?\nCMD\r?\n([0-9a-fA-F \r\n]+)',
                                 observed.decode('utf-8'))
            require(match is not None, 'Resource identity capture incomplete')
            actual = NATIVE.actionbag_proc_stat(match[1])
            actual.update(exe=match[2], argv=bytes.fromhex(match[3]).decode('utf-8').rstrip('\x00').split('\x00'))
            require(all(actual[k] == expected[k] for k in ('pid', 'starttime', 'ppid', 'exe', 'argv')),
                    'Owned resource PID identity changed')
        raw = parts[1]
        sample = parse_resource(raw.decode('utf-8'), pid, expected['starttime'])
        sample.update(point=point, monotonic=time.monotonic(), instance=self.instance, run=self.run,
                      raw_sha256=hashlib.sha256(raw).hexdigest(), capture_sha256=hashlib.sha256(capture).hexdigest())
        self.write('resources/' + point + '.json', sample)
        (self.output / 'resources' / (point + '.raw')).write_bytes(raw)
        (self.output / 'resources' / (point + '.capture.raw')).write_bytes(capture)
        require(time.monotonic() < self.stage_deadline, 'Resource evidence crossed stage deadline')


def parse_resource(text, pid, starttime):
    parts = re.split(r'\r?\nR28_(?:STATUS|FD|STAT_END)\r?\n', text)
    require(len(parts) == 4, 'Resource boundaries incomplete')
    for stat in (parts[0].strip(), parts[3].strip()):
        before, marker, after = stat.rpartition(') ')
        require(marker and before.startswith(str(pid) + ' (') and len(after.split()) >= 20 and
                after.split()[19] == str(starttime), 'Resource PID/starttime changed')
    metrics = {}
    for line in parts[1].splitlines():
        key, separator, value = line.partition(':')
        if key in ('VmRSS', 'VmHWM', 'Threads'):
            require(separator and key not in metrics, 'Duplicate resource metric')
            pattern = r'\s*([0-9]+)\s+kB\s*' if key != 'Threads' else r'\s*([0-9]+)\s*'
            match = re.fullmatch(pattern, value)
            require(match is not None, 'Resource metric shape')
            metrics[key] = int(match[1])
    fds = parts[2].splitlines()
    require(fds and len(fds) == len(set(fds)) and all(re.fullmatch('[0-9]+', fd) for fd in fds), 'FD list incomplete')
    return {'pid': pid, 'starttime': str(starttime), 'fd_count': len(fds),
            'metrics': {k: metrics.get(k, 'NOT_OBSERVED') for k in ('VmRSS', 'VmHWM', 'Threads')},
            'memory_unit': 'kB', 'leak_freedom': 'NOT_PROVEN'}


class RepeatedScene(P.RestartScene):
    def __init__(self, args):
        BAG.bag_selected_hdc(args.hdc)
        self.args = args
        self.output = fresh(args.output, 'execution')
        require(not self.output.exists(), 'Execution output exists')
        self.plan, peers, udids, tokens, self.observed = checked_plan(args.prepared)
        self.output.mkdir()
        self.errors, self.blocked, self.processes = [], [], {}
        self.deadline = self.origin = None
        resample = {'used': False}
        for key in O.INSTANCES:
            process = RepeatedProcess(args, self.plan, key, peers, udids, tokens, self.observed, self.output / key)
            process.poll_resample_budget = resample
            self.processes[key] = process
        self.write('PLAN.json', self.plan)

    def records(self, text, key):
        p = self.processes[key]
        return O.rows(text, p.scenario, key, p.run, p.owned[p.side]['child']['pid'])

    def sample_phase(self, phase):
        a = self.processes['A']
        a.extra_control('SAMPLE' + str(phase))
        self.wait(('A',), lambda t, s: len(O.resources(t['A'], a.run)['samples']) == phase + 1)
        a.resource_sample('PHASE' + str(phase))

    def workflow(self):
        a, b0 = self.processes['A'], self.processes['B0']
        for process in self.processes.values():
            process.deploy()
        self.budget(time.monotonic() + 60, 'INITIAL_LISTENERS')
        a.launch('A')
        b0.launch('B')
        self.wait(('A', 'B0'), lambda t, s: all(self.checkpoint(t, k, 'LISTENER') for k in ('A', 'B0')))
        for p in (a, b0):
            p.maps(p.side, 'listener')
        require(time.monotonic() < self.deadline, 'Initial maps exceeded deadline')
        self.origin = time.monotonic()
        self.write('CLOCK.json', {'origin': 'BEFORE_FIRST_LISTENER_RELEASE', 'business_origin': self.origin,
            'business_deadline': self.origin + 35, 'normal_close_release_deadline': self.origin + 50,
            'probe_lifetime_seconds': 40, 'preparation_query_utc': self.observed.isoformat(), 'limits': LIMITS})
        self.budget(self.origin + 35, 'PHASE0')
        for p in (a, b0):
            p.publish(p.side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + p.run + ' side=' + p.side + ' stage=LISTENER_READY\n')
        phases = []
        for phase in range(3):
            key = 'B' + str(phase)
            texts = self.wait(('A', key), lambda t, s: self.phase(t, 'A', phase) and self.phase(t, key, phase))
            phases.append(O.pair_content(self.records(texts['A'], 'A'), self.records(texts[key], key), self.plan['scenario'], phase))
            self.write('PHASE' + str(phase) + '.json', phases[-1])
            self.sample_phase(phase)
            require(time.monotonic() < self.deadline, 'Phase evidence exceeded35s')
            if phase == 2:
                break
            round = phase + 1
            old, new_key = self.processes[key], 'B' + str(round)
            new = self.processes[new_key]
            a.extra_control('ARM' + str(round))
            self.wait(('A', key), lambda t, s: O.owners(t['A'], a.run, round, 'ARMED') is not None)
            self.write(key + '_NORMAL_STOP_INTENT.json', {'monotonic': time.monotonic(), 'owned': old.owned['B'], 'method': 'PUBLIC_PROBE_FINISH'})
            old.finish_fixture('B', 'NORMAL_COMPLETION')
            self.wait(('A', key), lambda t, s: self.checkpoint(t, key, 'CLOSE'))
            old.maps('B', 'close')
            old.close_release()
            self.wait(('A', key), lambda t, s: bool(s[key]), exits=(key,))
            old.confirm_absent()
            a.resource_sample('AFTER_' + key + '_EXIT')
            ready = self.wait(('A',), lambda t, s: O.owners(t['A'], a.run, round, 'OLD_BOTH_READY') is not None)
            self.write('ROUND' + str(round) + '_OLD_READY.json', {'monotonic': time.monotonic(), 'observation': O.owners(ready['A'], a.run, round, 'OLD_BOTH_READY')})
            new.launch('B')
            identities = [(p.owned['B']['child']['pid'], p.owned['B']['child']['starttime'])
                          for k, p in self.processes.items() if k.startswith('B') and p.owned]
            require(len(identities) == len(set(identities)), 'B process identity reused')
            self.wait(('A', new_key), lambda t, s: self.checkpoint(t, new_key, 'LISTENER'))
            new.maps('B', 'listener')
            a.extra_control('REPLACE' + str(round))
            new.publish('B', 'control.LISTENER_READY', 'ROS_RELEASE run=' + new.run + ' side=B stage=LISTENER_READY\n')
            active = self.wait(('A', new_key), lambda t, s: O.owners(t['A'], a.run, round, 'NEW_BOTH_ACTIVE') is not None)
            self.write('ROUND' + str(round) + '_NEW_ACTIVE.json', {'monotonic': time.monotonic(),
                'observation': O.owners(active['A'], a.run, round, 'NEW_BOTH_ACTIVE'), 'continuity': O.continuity(active['A'], a.run)})
            a.extra_control('PHASE' + str(round))
        require(time.monotonic() < self.deadline, 'Business acceptance crossed35s')
        self.write('BUSINESS.json', {'phases': phases, 'accepted_monotonic': time.monotonic(), 'roundtrips': 15})
        require(time.monotonic() < self.deadline, 'Business evidence crossed35s')
        self.budget(self.origin + 50, 'FINAL_CLOSE')
        a.extra_control('COMPLETE')
        self.wait(('A', 'B2'), lambda t, s: O.completion(t['A'], a.run) is not None)
        b2 = self.processes['B2']
        for p in (a, b2):
            p.maps(p.side, 'business')
            p.finish_fixture(p.side, 'NORMAL_COMPLETION')
        self.wait(('A', 'B2'), lambda t, s: all(self.checkpoint(t, k, 'CLOSE') for k in ('A', 'B2')))
        for p in (a, b2):
            p.maps(p.side, 'close')
            p.close_release()
        require(time.monotonic() < self.deadline, 'Normal close release crossed50s')
        self.budget(time.monotonic() + 20, 'FINAL_EXIT')
        self.wait(('A', 'B2'), lambda t, s: all(s.values()), exits=('A', 'B2'))
        for p in (a, b2):
            p.confirm_absent()
        require(time.monotonic() < self.deadline, 'Final exit evidence crossed20s')
        self.budget(time.monotonic() + 60, 'ANALYZE')
        texts = {key: p.collect()[p.side] for key, p in self.processes.items()}
        self.analyze(texts)
        require(time.monotonic() < self.deadline, 'Analysis evidence crossed60s')

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
        require(O.owners(texts['A'], a.run, 2, 'NEW_BOTH_ACTIVE') is not None, 'Both replacements not observed')
        continuity = O.continuity(texts['A'], a.run, final=True)
        resources = O.resources(texts['A'], a.run, final=True)
        complete = O.completion(texts['A'], a.run)
        require(complete is not None, 'Explicit observation completion before public shutdown missing')
        native = P.N.native_capture(texts, identities, instances=O.INSTANCES)
        require(continuity['participant'][:24] == native['participant_prefixes']['A'], 'A wire identities disagree')
        os_samples = [read(a.output / 'resources' / (p + '.json')) for p in
                      ('PHASE0', 'AFTER_B0_EXIT', 'PHASE1', 'AFTER_B1_EXIT', 'PHASE2')]
        self.write('ANALYSIS.json', {'scope': 'TWO_CONTINUOUS_NORMAL_B_REPLACEMENTS', 'native': native,
            'A_continuity': continuity, 'native_resources': resources, 'A_process_resources': os_samples,
            'observation_completion': complete,
            'diagnostics': diagnostics, 'sdk_epoch': 'NOT_PROVEN', 'leak_freedom': 'NOT_PROVEN',
            'service_action_recovery': 'NOT_TESTED', 'long_duration': 'NOT_TESTED'})

    def execute(self):
        consumed = False
        try:
            release = read(self.args.release)
            require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_R28_REPEATED_RESTART_RELEASE', 'scenario': self.plan['scenario'],
                'prepared_sha256': sha(self.args.prepared), 'runner_sha256': sha(__file__), 'limits': LIMITS}, 'Exact Main one-shot release required')
            checked_plan(self.args.prepared)
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Query freshness expired before dispatch')
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
            self.write('RESULT.json', {'scope': 'TWO_CONTINUOUS_NORMAL_B_REPLACEMENTS', 'scenario': self.plan['scenario'],
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors, 'blocked': self.blocked,
                'release_consumed': consumed, 'repeated_restart_verified': passed, 'limits': LIMITS, 'sdk_epoch': 'NOT_PROVEN',
                'leak_freedom': 'NOT_PROVEN', 'default_automatic_reconnect': 'NOT_CLAIMED',
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
        return RepeatedScene(args).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
