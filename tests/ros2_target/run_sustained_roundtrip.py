"""One finite paired String roundtrip window; no restart/reaccept qualification.

Uses the accepted native package and frozen ownership/cleanup primitives.
Main must finish independent host testing/review before issuing a one-shot
release bound to the prepared inputs. No provisioning or permission changes.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO.parents[2]
EVIDENCE = ROOT / 'build/r26-sustained-restart'
PARENT = REPO / 'build/r25-native-actionbag/bag-recordplay-typesupport-cleanup-fix2-initial-test-build'
PARENT_SHA = '17bc0f79af03c3d8a3b9dab89412091d344cf949aa8ccb2da91df56e3ec9724c'
PROBE = REPO / 'integrations/ros2/target/sustained_roundtrip_probe.py'
MEMBER = 'scripts/sustained_roundtrip_probe.py'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_module(name, path, digest=None):
    if digest is not None and sha(path) != digest:
        raise ValueError('Frozen helper changed: ' + str(path))
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


R = load_module('roundtrip_ownership', Path(__file__).with_name('run_actionbag_bag_recordplay.py'),
                'e885ef483318c4b2f85e402b6032a539c6b273b5fa8dc915f5223265f21820fc')
O = load_module('roundtrip_oracle', Path(__file__).with_name('sustained_roundtrip_oracle.py'))
BAG, ORDINARY, NATIVE, BASE = R.BAG, R.ORDINARY, R.NATIVE, R.BASE
require, read = R.recordplay_require, R.LOAD
SOURCES = [Path(__file__), PROBE, Path(__file__).with_name('sustained_roundtrip_oracle.py'),
           Path(__file__).with_name('test_sustained_roundtrip.py')]


def fresh_output(path, kind):
    path = Path(path).absolute()
    require(path.parent == EVIDENCE and re.fullmatch(kind + '-[0-9a-f]{16}', path.name),
            'Output must be a fresh direct child of stage evidence root')
    checked = BASE.runtime_path(path, EVIDENCE, exists=False)
    require(not checked.exists(), 'Output already exists')
    return checked


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')


def compose_sustained(output):
    output = fresh_output(output, 'package')
    require(sha(PARENT / 'RUNTIME_PACKAGE.json') == PARENT_SHA, 'Parent runtime receipt changed')
    expected = read(PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    source = PARENT / 'package'
    require(BASE.runtime_inventory(source) == expected, 'Accepted package bytes changed')
    output.mkdir()
    destination = output / 'package'
    shutil.copytree(source, destination)
    shutil.copyfile(PROBE, destination / MEMBER)
    manifest = read(destination / 'LAUNCHER_PACKAGE.json')
    manifest['files'][MEMBER] = sha(PROBE)
    write_json(destination / 'LAUNCHER_PACKAGE.json', manifest)
    files = BASE.runtime_inventory(destination)
    require(set(files) == set(expected) | {MEMBER} and all(files[k] == v for k, v in expected.items()
            if k != 'LAUNCHER_PACKAGE.json'), 'Only new probe and inventory may differ')
    require(manifest['files'] == {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'},
            'Derived inventory mismatch')
    result = {'scope': 'ROUNDTRIP_SCRIPT_DERIVATION_ONLY', 'parent_sha256': PARENT_SHA,
              'probe_sha256': sha(PROBE), 'files': files, 'native_files_changed': 0,
              'device_tested': False, 'package': str(destination)}
    write_json(output / 'DERIVATION.json', result)
    return result


def checked_package(derivation):
    derivation = BASE.runtime_path(derivation, EVIDENCE)
    package = BASE.runtime_path(read(derivation)['package'], EVIDENCE)
    files = BASE.runtime_inventory(package)
    require(sha(PARENT / 'RUNTIME_PACKAGE.json') == PARENT_SHA, 'Original package receipt changed')
    parent = read(PARENT / 'RUNTIME_PACKAGE.json')['package_inventory']
    report = read(derivation)
    require(report == {'scope': 'ROUNDTRIP_SCRIPT_DERIVATION_ONLY', 'parent_sha256': PARENT_SHA,
        'probe_sha256': sha(PROBE), 'files': files, 'native_files_changed': 0, 'device_tested': False,
        'package': str(package)}, 'Exact static derivation required')
    require(set(files) == set(parent) | {MEMBER} and all(files[k] == v for k, v in parent.items()
            if k != 'LAUNCHER_PACKAGE.json') and files[MEMBER] == sha(PROBE), 'Native/parent drift')
    require(read(package / 'LAUNCHER_PACKAGE.json')['files'] ==
            {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'}, 'Manifest drift')
    return package, files


def prepare_sustained(derivation, routes, external, identity, output):
    output = fresh_output(output, 'prepared')
    derivation = BASE.runtime_path(derivation, EVIDENCE)
    package, files = checked_package(derivation)
    require(sha(external) == BAG.EXTERNAL_SHA and sha(identity) == BAG.IDENTITY_SHA, 'Identity/provider expansion forbidden')
    bindings = {str(p.resolve()): sha(p) for p in [derivation, Path(routes), Path(external), Path(identity), *SOURCES]}
    route_data = read(routes)
    require(route_data.get('cross_confirmed') is True and bool(route_data.get('bindings')), 'Paired routes required')
    for path, digest in route_data['bindings'].items():
        require(sha(path) == digest, 'Route binding changed')
        bindings[path] = digest
    output.mkdir()
    result = {'schema': 1, 'scope': 'ROUNDTRIP_PREPARED_NOT_DISPATCHED', 'run': secrets.token_hex(8),
        'case': 'sustained_200_ABA', 'package': str(package), 'files': files, 'bindings': bindings,
        'routes': str(Path(routes).resolve()), 'external': str(Path(external).resolve()),
        'identity': str(Path(identity).resolve()), 'derivation': str(derivation)}
    write_json(output / 'PLAN.json', result)
    return result


class SustainedRunner(R.BagRecordPlayRunner):
    def __init__(self, args):
        self.args, self.output, self.owns_output = args, fresh_output(args.output, 'execution'), False
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.finished_fixtures, self.closed, self.maps_observed = set(), set(), {}
        self.normal_stop_attempted = self.normal_stop_confirmed = False
        BAG.bag_selected_hdc(args.hdc)
        self.plan = read(args.prepared)
        require(set(self.plan) == {'schema', 'scope', 'run', 'case', 'package', 'files', 'bindings',
                'routes', 'external', 'identity', 'derivation'} and type(self.plan.get('schema')) is int and
                self.plan.get('scope') == 'ROUNDTRIP_PREPARED_NOT_DISPATCHED' and self.plan.get('schema') == 1 and
                self.plan.get('case') == 'sustained_200_ABA' and re.fullmatch('[0-9a-f]{16}', self.plan.get('run', '')),
                'Exact roundtrip prepared plan required')
        self.run, self.case = self.plan['run'], self.plan['case']
        self.package, self.files = checked_package(self.plan['derivation'])
        require(str(self.package) == self.plan['package'] and self.files == self.plan['files'], 'Prepared package changed')
        for path, digest in self.plan['bindings'].items():
            require(sha(path) == digest, 'Prepared input changed')
        require(all(self.plan['bindings'].get(str(p.resolve())) == sha(p) for p in SOURCES), 'Current sources not bound')
        require(sha(self.plan['external']) == BAG.EXTERNAL_SHA and sha(self.plan['identity']) == BAG.IDENTITY_SHA,
                'Identity/provider receipt changed')
        self.external = read(self.plan['external'])
        self.readiness = {'owned_libpython': {'member': 'lib/libpython3.12.so.1.0'}}
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.roles = {'A': 'requester', 'B': 'responder'}
        self.expected_argv = {s: [self.prefix[s] + '/package/actionbag_python', *self.argv(s)[1:]] for s in 'AB'}
        identity = read(self.plan['identity'])
        require(identity['main_execution_result'] == 'PASS_IDENTITY_READINESS_ONLY', 'Identity not admitted')
        self.tokens = {r['side']: r['new_token'] for r in identity['boards']}
        require(self.tokens == {'A': 672006291, 'B': 672042405}, 'Identity token drift')
        routes = read(self.plan['routes'])
        required_bindings = {str(Path(self.plan[k]).resolve()): sha(self.plan[k])
                             for k in ('derivation', 'routes', 'external', 'identity')}
        required_bindings.update({str(p.resolve()): sha(p) for p in SOURCES})
        require(routes.get('bindings'), 'Nonempty original query bindings required')
        required_bindings.update(routes['bindings'])
        require(self.plan['bindings'] == required_bindings, 'Incomplete or expanded prepared binding set')
        self.observed = datetime.fromisoformat(routes['observed_utc'])
        require(self.observed.utcoffset() is not None and routes.get('cross_confirmed') is True and
                0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() <= 300, 'Fresh paired routes required')
        self.routes, self.udids = {}, {}
        for side in 'AB':
            row = routes['sides'][side]
            require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == 0 and row['remote_exit'] == 0,
                    'Route target/exit mismatch')
            for kind in ('stdout', 'udid'):
                data = Path(row[kind + '_file']).read_bytes()
                require(hashlib.sha256(data).hexdigest() == row[kind + '_sha256'], 'Route export changed')
                if kind == 'stdout':
                    self.routes[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
                else:
                    require(1 <= len(data) <= 64 and not any(c in data for c in (b'\0', b'\r', b'\n')), 'Invalid UDID')
                    self.udids[side] = data.decode()
            require(len(self.udids[side].encode()) == self.routes[side]['udid_bytes'], 'UDID length mismatch')
        require(self.udids['A'] != self.udids['B'] and all(self.routes[p]['local'] in self.routes[s]['remotes']
                for s, p in [('A', 'B'), ('B', 'A')]), 'Pair is not cross-confirmed')
        self.output.mkdir()
        self.owns_output = True
        self.write('PLAN.json', self.plan)

    def argv(self, side):
        root = self.prefix[side]
        return [root + '/package/run-token-first', '--script', root + '/package/' + MEMBER,
                '--role', self.roles[side], '--run', self.run, '--side', side,
                '--finish-control', root + '/control.PROBE_FINISH']

    configure = BAG.BagCliRunner.configure
    maps = ORDINARY.OrdinaryCliRunner.maps

    def poll(self, side):
        root = self.prefix[side]
        raw = self.shell(side, 'test -f ' + root + '/stdout && test -f ' + root + '/stderr && test "$(stat -c %s ' +
            root + '/stdout)" -le 4194304 && test "$(stat -c %s ' + root + '/stderr)" -le 4194304 && cat ' +
            root + '/stdout && printf "\\nR25_STDERR\\n" && cat ' + root + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        require(not re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE', text),
                'Native window failed')
        O.roundtrip_rows(text, self.run, side, self.owned[side]['child']['pid'])
        status = self.shell(side, 'if test -f ' + root + '/exit; then cat ' + root + '/exit; fi').decode().strip()
        return text, status

    def paired_checkpoint(self, stage):
        while time.monotonic() < self.stage_deadline:
            texts = {}
            for side in 'AB':
                texts[side], status = self.poll(side)
                require(not status, 'Early child exit at ' + stage)
            if all(self.ready(s, texts[s], stage) for s in 'AB'):
                require(time.monotonic() < self.stage_deadline, 'Checkpoint deadline exceeded: ' + stage)
                return texts
            time.sleep(0.1)
        raise ValueError('Checkpoint deadline exceeded: ' + stage)

    def workflow(self):
        self.stage, self.stage_deadline = 'LAUNCH', time.monotonic() + 60
        for side in 'AB':
            self.launch(side)
        self.paired_checkpoint('LISTENER')
        for side in 'AB':
            self.maps(side, 'listener')
        origin = time.monotonic()
        require(origin < self.stage_deadline, 'Listener maps exceeded deadline')
        self.write('CLOCK.json', {'origin': 'BEFORE_FIRST_LISTENER_RELEASE', 'business_origin': origin,
            'business_deadline': origin + 35, 'normal_close_release_deadline': origin + 50})
        self.stage, self.stage_deadline = 'BUSINESS', origin + 35
        for side in 'AB':
            self.publish(side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + self.run + ' side=' + side + ' stage=LISTENER_READY\n')
        pids = {s: self.owned[s]['child']['pid'] for s in 'AB'}
        while time.monotonic() < self.stage_deadline:
            texts, done = {}, {}
            for side in 'AB':
                texts[side], status = self.poll(side)
                require(not status, 'Early child exit during business')
                done[side] = any(r['event'] == 'ROUNDTRIP_COMPLETE' for r in O.roundtrip_rows(texts[side], self.run, side, pids[side]))
            if all(done.values()):
                business = O.verify_roundtrip_logs(texts, self.run, pids, final=False)
                require(time.monotonic() < self.stage_deadline, 'Fixed35s business deadline exceeded')
                self.write('BUSINESS.json', business)
                break
            time.sleep(0.1)
        else:
            raise ValueError('Fixed35s business deadline exceeded')
        require(time.monotonic() < self.stage_deadline, 'Fixed35s business deadline exceeded')
        self.stage, self.stage_deadline = 'CLOSE', origin + 50
        for side in 'AB':
            self.maps(side, 'business')
            self.finish_fixture(side, 'NORMAL_COMPLETION')
        self.paired_checkpoint('CLOSE')
        for side in 'AB':
            self.maps(side, 'close')
        for side in 'AB':
            self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run +
                ' side=' + side + ' pid=' + str(pids[side]) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
            self.closed.add(side)
        require(time.monotonic() < self.stage_deadline, 'Close release exceeded absolute50s deadline')
        self.stage, self.stage_deadline = 'EXIT', time.monotonic() + 20
        self.wait_exit(20)

    def analyze(self, texts):
        pids = {s: self.owned[s]['child']['pid'] for s in 'AB'}
        content = O.verify_roundtrip_logs(texts, self.run, pids)
        diagnostics = {}
        for side in 'AB':
            require(NATIVE.actionbag_fields(texts[side], 'ROS_PYTHON_INTERPRETER_RETURN') ==
                [{'execution_rc': '0', 'finalize_rc': '0'}] and NATIVE.actionbag_fields(texts[side], 'ROS_PYTHON_FINAL') ==
                [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1', 'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}] and
                NATIVE.actionbag_fields(texts[side], 'ROS_IDENTITY_FINAL') == [{'restored': '1', 'business_rc': '0'}],
                'Native/interpreter/identity finalization failed')
            diagnostics[side] = R.recordplay_errors((self.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8'),
                (self.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8'), self.run, side, pids[side], self.files)
        self.write('ANALYSIS.json', {'content': content, 'native': NATIVE.actionbag_native_capture(texts, self.run, pids),
            'diagnostics': diagnostics, 'sdk_epoch': 'NOT_PROVEN', 'restart': 'NOT_TESTED'})

    def execute(self):
        try:
            release = read(self.args.release)
            require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_SUSTAINED_ROUNDTRIP_RELEASE',
                'run': self.run, 'prepared_sha256': sha(self.args.prepared), 'runner_sha256': sha(__file__)},
                'Exact Main one-shot release required')
            for path, digest in self.plan['bindings'].items():
                require(sha(path) == digest, 'Input changed before dispatch')
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Route freshness expired')
            with (self.args.prepared.parent / (self.run + '.CONSUMED.json')).open('x', encoding='utf-8') as stream:
                json.dump({'release_sha256': sha(self.args.release), 'output': str(self.output),
                           'utc': datetime.now(timezone.utc).isoformat()}, stream)
            self.write('MAIN_RELEASE.json', release)
            self.deploy()
            self.workflow()
            self.stage_deadline = None
            self.analyze(self.collect())
        except ValueError as error:
            self.errors.append(self.stage + ': ' + str(error))
        except (Exception, KeyboardInterrupt) as error:
            self.blocked.append(self.stage + ': ' + repr(error))
        finally:
            self.cleanup(bool(self.errors or self.blocked))
            if self.owned and (self.errors or self.blocked):
                try:
                    self.collect()
                except (Exception, KeyboardInterrupt) as error:
                    self.blocked.append('FAILURE_CAPTURE: ' + repr(error))
            passed = not self.errors and not self.blocked
            self.write('RESULT.json', {'run': self.run, 'scope': 'BOUNDED_200_STRING_ROUNDTRIPS',
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors,
                'blocked': self.blocked, 'communication_verified': passed, 'process_restart': 'NOT_TESTED',
                'participant_recreation': 'NOT_TESTED', 'sdk_epoch': 'NOT_PROVEN',
                'evidence': {str(p): sha(p) for p in self.output.rglob('*') if p.is_file()}})
        return 0 if passed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    compose = commands.add_parser('compose')
    compose.add_argument('--output', type=Path, required=True)
    prepare = commands.add_parser('prepare')
    for name in ('derivation', 'routes', 'external', 'identity', 'output'):
        prepare.add_argument('--' + name, type=Path, required=True)
    run = commands.add_parser('run')
    for name in ('prepared', 'release', 'hdc', 'output'):
        run.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'compose':
        compose_sustained(args.output)
    elif args.command == 'prepare':
        prepare_sustained(args.derivation, args.routes, args.external, args.identity, args.output)
    else:
        return SustainedRunner(args).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
