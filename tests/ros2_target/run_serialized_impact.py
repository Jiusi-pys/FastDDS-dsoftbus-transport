"""One bounded typed/raw serialization experiment; no native runtime changes."""
import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import secrets
import shlex
import shutil
import time
import run_action_natural_restart as Q
import serialized_impact_oracle as O
import run_parameter_native as T

N, P = Q.N, Q.P
HERE, REPO, ROOT = Q.HERE, Q.REPO, Q.ROOT
BASE, BAG, NATIVE, R = Q.BASE, Q.BAG, Q.NATIVE, Q.R
sha, read, require, write_json = Q.sha, Q.read, O.require, Q.write_json
EVIDENCE = ROOT / 'build/r34-serialized-impact'
SCOPE = 'R34_SERIALIZED_IMPACT_TYPED_AND_RAW'
PROBE = REPO / 'integrations/ros2/target/serialized_impact_probe.py'
MEMBER = 'scripts/serialized_impact_probe.py'
LIMITS = {k: v for k, v in Q.LIMITS.items() if k.endswith('_seconds')}
SOURCES = [Path(__file__), PROBE, HERE / 'serialized_impact_oracle.py', HERE / 'test_serialized_impact.py']


@dataclass(frozen=True)
class Acceptance:
    scope: str
    release_scope: str
    oracle: object


V1 = Acceptance(SCOPE, 'MAIN_ONE_SHOT_R34_SERIALIZED_IMPACT_RELEASE', O)


def checked_acceptance(acceptance):
    require(acceptance is V1, 'Only fixed internal acceptance profiles permitted')
    return acceptance


def checked_release(args, plan, acceptance):
    checked_acceptance(acceptance)
    release = read(args.release)
    require(type(release.get('schema')) is int and release == {'schema': 1, 'scope': acceptance.release_scope,
        'scenario': plan['scenario'], 'prepared_sha256': sha(args.prepared), 'runner_sha256': sha(__file__)},
        'Exact Main one-shot parameter release required')
    return release


PINS = {
    'derivation': ('build/r33-capability-gap/package-9e8884606c97acee/DERIVATION.json', '5c4cf15d2d0ea7df51ce9f1658a86a640a5f50213875b78575d2e851c3e44f85'),
    'publication': ('build/r33-capability-gap/main-functional-v2-publication/POST_PUBLISH.json', '08526697ff84741f6321348dc01adc8b01f0c2724bd5a8181fb7b1779e2c3ead'),
    'device': ('build/r33-capability-gap/functional-v2-device3-final-audit/RESULT.json', 'cfc8ca5f62841a9d314e6a7cafa5c1f210ea55a9fdb80fad8edb141919f24d18'),
    'sources': ('build/r33-capability-gap/functional-v2-final/SOURCE_INVENTORY.json', '0b4ca39f579a3b74701234f1b0ce87aaaa9b616ee99be846009a465c34bda504'),
}


def fresh(path, kind):
    output = BASE.runtime_path(path, EVIDENCE, exists=False)
    require(output.parent == EVIDENCE.resolve() and re.fullmatch(kind + '-[0-9a-f]{16}', output.name) and
            not output.exists(), 'Fresh dedicated R34 immediate child required')
    return output


def current(bindings):
    receipts = {}
    for name, (relative, digest) in PINS.items():
        path = N.checked_descriptor({'path': str(ROOT / relative), 'sha256': digest}, bindings)
        receipts[name] = read(path)
    require(receipts['publication']['status'] == 'PASS' and receipts['publication']['head'] ==
            'bb007f39d3d5224d44ed5bcfa8294a68b337b32e' and receipts['device']['status'] == 'PASS_DEVICE_FINAL_AUDIT' and
            receipts['device']['commit_gate'] == 'PASS' and receipts['device']['device_tested'] is True and
            receipts['device']['cross_device_tested'] is True, 'Current published R33 evidence differs')
    sources = receipts['sources']
    require(len(sources) == 282 and all(sha(p) == h for p, h in sources.items()), 'Published282 source baseline drift')
    bindings.update(sources)
    bindings.update({str(p.resolve(strict=True)): sha(p) for p in SOURCES})
    return T.checked_package(ROOT / PINS['derivation'][0], bindings, T.V2)


def compose(output, acceptance=V1):
    checked_acceptance(acceptance)
    output = fresh(output, 'package')
    parent, files = current({})
    require(MEMBER not in files, 'New parameter script already in parent')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(parent, package)
    shutil.copyfile(PROBE, package / MEMBER)
    manifest = read(package / 'LAUNCHER_PACKAGE.json')
    manifest['files'][MEMBER] = sha(PROBE)
    write_json(package / 'LAUNCHER_PACKAGE.json', manifest)
    result = dict(schema=1, scope=acceptance.scope, package=str(package), files=BASE.runtime_inventory(package),
                  parent=N.descriptor(ROOT / PINS['derivation'][0]), probe_sha256=sha(PROBE),
                  native_build='REUSED_HASH_VERIFIED_NOT_REBUILT', device_tested=False)
    write_json(output / 'DERIVATION.json', result)
    checked_package(output / 'DERIVATION.json', {}, acceptance)
    return result


def checked_package(path, bindings, acceptance=V1):
    checked_acceptance(acceptance)
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    parent, original = current(bindings)
    require(set(result) == {'schema', 'scope', 'package', 'files', 'parent', 'probe_sha256', 'native_build', 'device_tested'} and
            type(result['schema']) is int and result['schema'] == 1 and result['scope'] == acceptance.scope and
            result['device_tested'] is False and result['native_build'] == 'REUSED_HASH_VERIFIED_NOT_REBUILT' and
            result['parent'] == N.descriptor(ROOT / PINS['derivation'][0]), 'Exact R34 package schema required')
    package = BASE.runtime_path(result['package'], EVIDENCE)
    require(path.name == 'DERIVATION.json' and package == path.parent / 'package' and
            path.parent.parent == EVIDENCE.resolve() and re.fullmatch('package-[0-9a-f]{16}', path.parent.name), 'Exact derived package location')
    files = BASE.runtime_inventory(package)
    require(files == result['files'] and set(files) == set(original) | {MEMBER} and len(files) == 3239 and
            all(files[k] == v for k, v in original.items() if k != 'LAUNCHER_PACKAGE.json') and
            files[MEMBER] == result['probe_sha256'] == sha(PROBE), 'Only new script/manifest may differ')
    expected = read(parent / 'LAUNCHER_PACKAGE.json')
    expected['files'][MEMBER] = sha(PROBE)
    require(read(package / 'LAUNCHER_PACKAGE.json') == expected and
            expected['files'] == {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'}, 'Exact manifest delta required')
    bindings[str(path)] = sha(path)
    return package, files


def qualification(path, derivation, bindings, acceptance=V1):
    checked_acceptance(acceptance)
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    require(type(result.get('schema')) is int and result['schema'] == 1 and result.get('status') == 'PASS_FINAL_HOST' and result.get('scope') == acceptance.scope and
            result.get('device_tested') is False and result.get('package') == N.descriptor(derivation), 'Independent final host receipt required')
    inventory = N.checked_descriptor(result['source_inventory'], bindings)
    expected = read(ROOT / PINS['sources'][0])
    expected.update({str(p.resolve(strict=True)): sha(p) for p in SOURCES})
    require(read(inventory) == expected and len(expected) == 286 and all(sha(p) == h for p, h in expected.items()),
            'Independent286 source qualification differs')
    bindings[str(path)] = sha(path)


def native_provenance(path, bindings):
    """The selected stripped extension has a recorded, unresolved provenance gap."""
    path = BASE.runtime_path(path, EVIDENCE)
    result = read(path)
    require(type(result.get('schema')) is int and result['schema'] == 1 and
            result.get('scope') == 'R34_NATIVE_RAW_SOURCE_PROVENANCE' and
            result.get('status') == 'PARTIAL_PROVENANCE_RAW_ONLY' and
            result.get('dds_payload_boundary') == 'BLOCKED' and
            result.get('package') == N.descriptor(ROOT / PINS['derivation'][0]) and
            type(result.get('bindings')) is dict and result['bindings'], 'Independent raw-only provenance receipt required')
    for selected, digest in result['bindings'].items():
        require(sha(selected) == digest, 'Native provenance evidence drift')
        bindings[selected] = digest
    bindings[str(path)] = sha(path)


def admission(plan, *, complete, acceptance=V1):
    checked_acceptance(acceptance)
    bindings = {}
    package, files = checked_package(plan['derivation'], bindings, acceptance)
    qualification(plan['qualification'], plan['derivation'], bindings, acceptance)
    native_provenance(plan['native_provenance'], bindings)
    require(sha(plan['external']) == BAG.EXTERNAL_SHA and sha(plan['identity']) == BAG.IDENTITY_SHA, 'Identity/provider expansion forbidden')
    N.current_elf_closure(package, files, bindings, read(plan['external']))
    for key in ('derivation', 'qualification', 'native_provenance', 'routes', 'external', 'identity'):
        selected = BASE.runtime_path(plan[key], ROOT / 'build')
        bindings[str(selected)] = sha(selected)
    routes = read(plan['routes'])
    require(routes.get('cross_confirmed') is True and routes.get('bindings'), 'Original paired routes required')
    for path, digest in routes['bindings'].items():
        bindings[str(Q.route_binding(path, digest))] = digest
    if complete:
        require(plan['package'] == str(package) and plan['files'] == files and plan['bindings'] == bindings,
                'Prepared package/source binding drift')
    return package, files, bindings, routes


def prepare(args, acceptance=V1):
    checked_acceptance(acceptance)
    output = fresh(args.output, 'prepared')
    plan = {k: str(Path(getattr(args, k)).resolve(strict=True)) for k in ('derivation', 'qualification', 'native_provenance', 'routes', 'external', 'identity')}
    package, files, bindings, unused = admission(plan, complete=False, acceptance=acceptance)
    scenario, run = secrets.token_hex(8), secrets.token_hex(8)
    require(scenario != run, 'Distinct scenario and paired run required')
    plan.update(schema=1, scope=acceptance.scope, scenario=scenario, runs={s: run for s in O.INSTANCES}, limits=LIMITS,
                package=str(package), files=files, bindings=bindings)
    validate_plan(plan, acceptance)
    output.mkdir()
    write_json(output / 'PLAN.json', plan)
    return plan


def checked_plan(path, acceptance=V1):
    checked_acceptance(acceptance)
    path = BASE.runtime_path(path, EVIDENCE)
    require(path.name == 'PLAN.json' and path.parent.parent == EVIDENCE.resolve() and
            re.fullmatch('prepared-[0-9a-f]{16}', path.parent.name), 'Exact R34 prepared location')
    return validate_plan(read(path), acceptance)


def validate_plan(plan, acceptance=V1):
    checked_acceptance(acceptance)
    require(set(plan) == {'schema', 'scope', 'scenario', 'runs', 'limits', 'derivation', 'qualification', 'package', 'files',
                         'bindings', 'routes', 'external', 'identity', 'native_provenance'} and type(plan['schema']) is int and plan['schema'] == 1 and
            plan['scope'] == acceptance.scope and plan['limits'] == LIMITS and all(type(v) is int for v in plan['limits'].values()) and
            set(plan['runs']) == set(O.INSTANCES) and all(isinstance(x, str) and re.fullmatch('[0-9a-f]{16}', x)
            for x in [plan['scenario'], *plan['runs'].values()]) and plan['runs']['A'] == plan['runs']['B'] != plan['scenario'],
            'Exact paired identity/schema/limits required')
    unused, files, bindings, routes = admission(plan, complete=True, acceptance=acceptance)
    observed = datetime.fromisoformat(routes['observed_utc'])
    require(observed.utcoffset() is not None and 0 <= (datetime.now(timezone.utc) - observed).total_seconds() <= 300,
            'Original query freshness expired; business budget separate')
    peers, udids = {}, {}
    for side in O.INSTANCES:
        row = routes['sides'][side]
        require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == row['remote_exit'] == 0, 'Peer target/exit mismatch')
        for kind in ('stdout', 'udid'):
            data = Path(row[kind + '_file']).read_bytes()
            require(hashlib.sha256(data).hexdigest() == row[kind + '_sha256'], 'Query export drift')
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
            'Only existing admitted identity permitted')
    return plan, peers, udids, tokens, observed


class ImpactProcess(P.RestartProcess):
    def argv(self, side):
        result = super().argv(side)
        result[2] = self.prefix[side] + '/package/' + MEMBER
        return result

    def validate_poll(self, text, side):
        O.rows(text, self.scenario, self.instance, self.run, self.owned[side]['child']['pid'])

    def configuration_fields(self, side, fields):
        prefix = 'com.kaihong.mdds.r34.' + self.scenario
        fields.update(service_prefix=prefix, local_initiator_name=prefix + '.' + side + '.tx',
                      peer_initiator_name=prefix + '.' + ('B' if side == 'A' else 'A') + '.tx')
        return fields

    def configure(self, side):
        super().configure(side)
        local, remote = self.output / (side + '.private.conf'), self.prefix[side] + '/ros.conf'
        require(self.shell(side, 'sha256sum ' + shlex.quote(remote)).decode().strip().split() == [sha(local), remote],
                'Remote parameter configuration differs')

    def extra_control(self, name):
        raise ValueError('Parameter scene cannot authorize replacement or business controls')


class ImpactScene(P.RestartScene):
    acceptance = V1

    def __init__(self, args, acceptance=V1):
        self.acceptance = checked_acceptance(acceptance)
        self.args, self.output = args, fresh(args.output, 'execution')
        self.plan, peers, udids, tokens, self.observed = checked_plan(args.prepared, self.acceptance)
        checked_release(args, self.plan, self.acceptance)
        BAG.bag_selected_hdc(args.hdc)
        self.output.mkdir()
        self.errors, self.blocked, self.processes = [], [], {}
        self.deadline, self.origin = None, None
        resample = {'used': False}
        for side in O.INSTANCES:
            process = ImpactProcess(args, self.plan, side, peers, udids, tokens, self.observed, self.output / side)
            process.poll_resample_budget = resample
            self.processes[side] = process
        self.write('PLAN.json', self.plan)

    def records(self, text, key):
        p = self.processes[key]
        return O.rows(text, p.scenario, key, p.run, p.owned[key]['child']['pid'])

    def workflow(self):
        for p in self.processes.values():
            p.deploy()
        self.budget(time.monotonic() + 60, 'INITIAL_LISTENERS')
        for p in self.processes.values():
            p.launch(p.side)
        self.wait(O.INSTANCES, lambda t, s: all(self.checkpoint(t, k, 'LISTENER') for k in O.INSTANCES))
        for p in self.processes.values():
            p.maps(p.side, 'listener')
        require(time.monotonic() < self.deadline, 'Initial maps deadline')
        self.origin = time.monotonic()
        self.write('CLOCK.json', {'origin': 'BEFORE_FIRST_LISTENER_RELEASE', 'business_origin': self.origin,
            'business_deadline': self.origin + 35, 'normal_close_release_deadline': self.origin + 50,
            'probe_lifetime_seconds': 40, 'probe_origin': 'EACH_PROCESS_CREATE_NODE_RETURN',
            'preparation_query_utc': self.observed.isoformat(), 'limits': LIMITS})
        self.budget(self.origin + 35, 'PARAMETER_BUSINESS')
        for p in self.processes.values():
            p.publish(p.side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + p.run + ' side=' + p.side + ' stage=LISTENER_READY\n')
        texts = self.wait(O.INSTANCES, lambda t, s: bool(O.select(self.records(t['A'], 'A'), 'CLIENT_COMPLETE')) and
                          bool(O.select(self.records(t['B'], 'B'), 'SERVER_COMPLETE')))
        result = self.acceptance.oracle.business(self.records(texts['A'], 'A'), self.records(texts['B'], 'B'), self.plan['scenario'])
        self.write('BUSINESS.json', dict(result, monotonic=time.monotonic()))
        require(time.monotonic() < self.deadline, 'Business oracle deadline')
        self.budget(self.origin + 50, 'FINAL_CLOSE')
        for p in self.processes.values():
            p.maps(p.side, 'business')
            p.finish_fixture(p.side, 'NORMAL_COMPLETION')
        self.wait(O.INSTANCES, lambda t, s: all(self.checkpoint(t, k, 'CLOSE') for k in O.INSTANCES))
        for p in self.processes.values():
            p.maps(p.side, 'close')
            p.close_release()
        require(time.monotonic() < self.deadline, 'Normal close release deadline')
        self.write('NORMAL_CLOSE_RELEASED.json', {'monotonic': time.monotonic()})
        require(time.monotonic() < self.deadline, 'Close receipt crossed original50s deadline')
        self.budget(time.monotonic() + 20, 'FINAL_EXIT')
        self.wait(O.INSTANCES, lambda t, s: all(s.values()), exits=O.INSTANCES)
        for p in self.processes.values():
            p.confirm_absent()
        require(time.monotonic() < self.deadline, 'Absence proof crossed original20s deadline')
        self.write('FINAL_EXIT_CONFIRMED.json', {'monotonic': time.monotonic()})
        require(time.monotonic() < self.deadline, 'Exit receipt crossed original20s deadline')
        self.budget(time.monotonic() + 60, 'ANALYZE')
        self.analyze({key: p.collect()[p.side] for key, p in self.processes.items()})
        require(time.monotonic() < self.deadline, 'Analysis deadline')

    def analyze(self, texts):
        diagnostics, records, pids = {}, {}, {}
        for side, p in self.processes.items():
            pids[side] = p.owned[side]['child']['pid']
            records[side] = self.records(texts[side], side)
            O.finalized(records[side], self.plan['files'])
            require(O.fields(texts[side], 'ROS_PYTHON_INTERPRETER_RETURN') == [{'execution_rc': '0', 'finalize_rc': '0'}] and
                    O.fields(texts[side], 'ROS_PYTHON_FINAL') == [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1',
                    'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}], 'Native/interpreter finalization failed')
            diagnostics[side] = R.recordplay_errors((p.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8'),
                (p.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8'), p.run, side, pids[side], p.files)
        content = self.acceptance.oracle.business(records['A'], records['B'], self.plan['scenario'], final=True)
        endpoints = O.endpoints(texts, self.plan['scenario'])
        native = NATIVE.actionbag_native_capture(texts, self.plan['runs']['A'], pids)
        self.write('ANALYSIS.json', {'scope': self.acceptance.scope, 'content': content, 'native': native, 'endpoints': endpoints, 'diagnostics': diagnostics,
            'dds_payload_boundary': 'BLOCKED_PROVENANCE_GAP', 'raw_observation': 'NATIVE_RAW_CALLBACK_BUFFER',
            'sdk_epoch': 'NOT_PROVEN', 'default_automatic_reconnect': 'NOT_CLAIMED', 'long_duration': 'NOT_TESTED'})

    def execute(self):
        consumed = False
        try:
            release = checked_release(self.args, self.plan, self.acceptance)
            checked_plan(self.args.prepared, self.acceptance)
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Original query dispatch freshness expired')
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
            self.write('RESULT.json', {'scope': self.acceptance.scope, 'scenario': self.plan['scenario'],
                'status': 'PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', 'failures': self.errors,
                'blocked': self.blocked, 'release_consumed': consumed, 'bounded_raw_experiment_valid': passed,
                'business_permissions': 'FORBIDDEN', 'sdk_epoch': 'NOT_PROVEN', 'default_automatic_reconnect': 'NOT_CLAIMED',
                'general_serialized_compatibility': 'OPEN', 'dds_payload_boundary': 'BLOCKED_PROVENANCE_GAP',
                'raw_observation': 'NATIVE_RAW_CALLBACK_BUFFER', 'bag_recordplay': 'NOT_TESTED',
                'evidence': {str(p): sha(p) for p in self.output.rglob('*') if p.is_file()}})
        return 0 if passed else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for suffix, acceptance in (('', V1),):
        for operation, fields in (
                ('compose', ('output',)),
                ('prepare', ('derivation', 'qualification', 'native_provenance', 'routes', 'external', 'identity', 'output')),
                ('run', ('prepared', 'release', 'hdc', 'output'))):
            command = sub.add_parser(operation + suffix)
            command.set_defaults(operation=operation, acceptance=acceptance)
            for name in fields:
                command.add_argument('--' + name.replace('_', '-'), type=Path, required=True)
    args = parser.parse_args(argv)
    if args.operation == 'compose':
        compose(args.output, args.acceptance)
    elif args.operation == 'prepare':
        prepare(args, args.acceptance)
    else:
        return ImpactScene(args, args.acceptance).execute()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
