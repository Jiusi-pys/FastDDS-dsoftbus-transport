"""R35 stock record/default-play/no-loan-play; fresh independent one-shot gates."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import runpy
import secrets
import shlex
import shutil
import subprocess
import time

import run_actionbag_bag_recordplay as R
import run_action_natural_restart as Q
import serialized_bag_oracle as O

BAG, NATIVE, ORDINARY, BASE = R.BAG, R.NATIVE, R.ORDINARY, R.BASE
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ROOT = REPO.parents[2]
EVIDENCE = ROOT / 'build/r35-serialized-bag'
SCOPE, CASES, LANES = O.SCOPE, O.CASES, O.LANES
PROBE = REPO / 'integrations/ros2/target/serialized_bag_probe.py'
MEMBER = 'scripts/serialized_bag_probe.py'
SOURCES = [Path(__file__), PROBE, HERE / 'serialized_bag_oracle.py', HERE / 'test_serialized_bag.py']
LIMITS = dict(query_fresh_seconds=300, query_dispatch_seconds=240, listener_seconds=60,
              business_seconds=35, probe_seconds=40, close_release_seconds=50, exit_seconds=20, analysis_seconds=60)
PARENT = ROOT / 'build/r34-serialized-impact/package-6afcb66e9b25f91c/DERIVATION.json'
PARENT_SHA = '525783854268c9c85960f23ffa7b061e863be7ec17d6a07a47d006cf76eac25f'
ADMISSION_SHA = '96b5a2e6042ceddd958d0eb9908694eacd7769abe06625824cba1e03964cfcd0'
require, read = O.require, O.read


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def descriptor(path):
    return {'path': str(Path(path).resolve(strict=True)), 'sha256': sha(path)}


def checked_descriptor(value, bindings, root=ROOT):
    require(type(value) is dict and set(value) == {'path', 'sha256'} and
            type(value['path']) is str and re.fullmatch('[0-9a-f]{64}', value['sha256']), 'Exact descriptor required')
    path = BASE.runtime_path(value['path'], root)
    require(sha(path) == value['sha256'], 'Descriptor bytes changed: ' + str(path))
    bindings[str(path)] = value['sha256']
    return path


def exclusive(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2, ensure_ascii=False)
        stream.write('\n')


def fresh(path, kind):
    path = Path(path).absolute()
    require(path.parent == EVIDENCE and re.fullmatch(kind + '-[0-9a-f]{16}', path.name) and not path.exists(), 'Fresh named E35 direct child required')
    return BASE.runtime_path(path, EVIDENCE, exists=False)


def source_inventory():
    baseline = read(EVIDENCE / 'BASELINE.json')
    require(sha(EVIDENCE / 'MAIN_DESIGN_ADMISSION.json') == ADMISSION_SHA, 'Main design admission changed')
    sources = dict(baseline['source_inventory'])
    require(len(sources) == 286 and all(sha(p) == h for p, h in sources.items()), 'Protected286 sources changed')
    sources.update({str(p.resolve(strict=True)): sha(p) for p in SOURCES})
    return sources


def parent_package():
    require(sha(PARENT) == PARENT_SHA, 'Selected parent derivation changed')
    data = read(PARENT)
    package, files = Path(data['package']), data['files']
    require(len(files) == 3239 and BASE.runtime_inventory(package) == files, 'Exact3239 parent package changed')
    return package, files


def compose(output):
    output = fresh(output, 'package')
    parent, original = parent_package()
    require(MEMBER not in original, 'Probe already present in parent')
    output.mkdir()
    package = output / 'package'
    shutil.copytree(parent, package)
    shutil.copyfile(PROBE, package / MEMBER)
    manifest = read(package / 'LAUNCHER_PACKAGE.json')
    manifest['files'][MEMBER] = sha(PROBE)
    # A new manifest replaces only the copied manifest, not any parent bytes.
    (package / 'LAUNCHER_PACKAGE.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    value = dict(schema=1, scope=SCOPE, package=str(package), files=BASE.runtime_inventory(package),
                 parent=descriptor(PARENT), probe_sha256=sha(PROBE), native_build='REUSED_HASH_VERIFIED_NOT_REBUILT', device_tested=False)
    exclusive(output / 'DERIVATION.json', value)
    checked_package(output / 'DERIVATION.json', {})
    return value


def checked_package(path, bindings):
    path = checked_descriptor(descriptor(path), bindings, EVIDENCE)
    value = read(path)
    require(set(value) == {'schema', 'scope', 'package', 'files', 'parent', 'probe_sha256', 'native_build', 'device_tested'} and
            value['schema'] == 1 and value['scope'] == SCOPE and value['parent'] == descriptor(PARENT) and
            value['native_build'] == 'REUSED_HASH_VERIFIED_NOT_REBUILT' and value['device_tested'] is False, 'R35 derivation schema')
    package = BASE.runtime_path(value['package'], EVIDENCE)
    require(path.name == 'DERIVATION.json' and path.parent.parent == EVIDENCE and
            re.fullmatch('package-[0-9a-f]{16}', path.parent.name) and package == path.parent / 'package', 'Derived package location')
    _, original = parent_package()
    files = BASE.runtime_inventory(package)
    require(value['files'] == files and len(files) == 3240 and set(files) == set(original) | {MEMBER} and
            all(files[n] == h for n, h in original.items() if n != 'LAUNCHER_PACKAGE.json') and
            files[MEMBER] == value['probe_sha256'] == sha(PROBE), 'Only new script and manifest delta permitted')
    expected = read(Path(read(PARENT)['package']) / 'LAUNCHER_PACKAGE.json')
    expected['files'][MEMBER] = sha(PROBE)
    require(read(package / 'LAUNCHER_PACKAGE.json') == expected and expected['files'] ==
            {n: h for n, h in files.items() if n != 'LAUNCHER_PACKAGE.json'}, 'Exact launcher manifest delta')
    return package, files


def checked_qualification(value, derivation, bindings):
    path = checked_descriptor(value, bindings, EVIDENCE)
    result = read(path)
    require(result.get('schema') == 1 and result.get('scope') == SCOPE and result.get('status') == 'PASS_FINAL_HOST' and
            result.get('device_tested') is False and result.get('package') == descriptor(derivation), 'Independent current final host qualification required')
    inventory = checked_descriptor(result['source_inventory'], bindings, EVIDENCE)
    require(read(inventory) == source_inventory(), 'Actual286 plus new source qualification differs')


def checked_sealed_audit(value, scope, bindings):
    require(type(value) is dict and set(value) == {'result', 'seal'}, 'Independent result/seal pair required')
    result_path = checked_descriptor(value['result'], bindings, EVIDENCE)
    seal_path = checked_descriptor(value['seal'], bindings, EVIDENCE)
    seal = read(seal_path)
    members = seal.get('files', seal)
    require(type(members) is dict and bool(members), 'Nonempty audit seal required')
    checked = {}
    for name, digest in members.items():
        relative = Path(name)
        require('..' not in relative.parts, 'Unsafe audit seal member')
        path = relative if relative.is_absolute() else seal_path.parent / relative
        actual = checked_descriptor({'path': str(path), 'sha256': digest}, bindings, EVIDENCE)
        checked[str(actual)] = digest
    require(checked.get(str(result_path)) == value['result']['sha256'], 'Audit result not sealed')
    result = read(result_path)
    require(result.get('schema') == 1 and result.get('scope') == scope, 'Independent audit scope')
    return result


def checked_recording(value, derivation, bindings):
    audit = checked_sealed_audit(value, 'R35_INDEPENDENT_CLOSED_BAG_ADMISSION', bindings)
    require(audit.get('status') == 'PLAY_INPUT_ADMITTED' and audit.get('package') == descriptor(derivation), 'Closed-bag admission missing/wrong package')
    result_path = checked_descriptor(audit['record_result'], bindings, EVIDENCE)
    result = read(result_path)
    require(result.get('scope') == SCOPE and result.get('case') == CASES[0] and result.get('result') == 'PASS' and
            result.get('normal_process_exit') is True and result.get('recording_completed') is True and
            result.get('persistence_fidelity') == 'PASS' and result.get('failures') == [] and result.get('blocked') == [] and
            result.get('source_sha256') == sha(__file__) and result.get('package') == descriptor(derivation), 'Actual normal faithful record required')
    for path, digest in result['evidence'].items():
        checked_descriptor(dict(path=path, sha256=digest), bindings, EVIDENCE)
    record_root = result_path.parent
    for name in ('CLEANUP.json', 'NORMAL_STOP_INTENT.json', 'NORMAL_STOP_DISPATCH.json', 'STOP_STATE_COMPLETED.json',
                 'NATIVE.json', 'CHILD_EXITS.json', 'MAIN_RELEASE.json', 'PLAN.json'):
        require(str(record_root / name) in result['evidence'], 'Missing record lifecycle evidence: ' + name)
    require(normal_cleanup(read(record_root / 'CLEANUP.json')), 'Record cleanup ownership/forced-signal failure')
    stop = read(record_root / 'STOP_STATE_COMPLETED.json')
    require(all(stop.get(n) is True for n in ('attempt_latched', 'intent_persisted', 'dispatch_entered',
            'dispatch_confirmed', 'dispatch_receipt_persisted', 'recording_completed')), 'Record stop completion facts missing')
    collection = read(checked_descriptor(audit['collection'], bindings, EVIDENCE))
    expected = read(checked_descriptor(audit['expected'], bindings, EVIDENCE))
    recorded = read(checked_descriptor(audit['bag_content'], bindings, EVIDENCE))
    require(collection['normal_closed_before_collection'] is True and collection['run'] == result['run'] == expected['record_run'] and
            collection['files'] == expected['files'], 'Record collection provenance')
    checked_root = BASE.runtime_path(collection['local_directory'], EVIDENCE)
    actual = O.verify_recorded_bag(checked_root, expected)
    require(actual == recorded and actual['play_eligible'] is True and actual['persistence_fidelity'] == 'PASS', 'Serialized-source/storage mismatch bars both play modes')
    return dict(run=result['run'], collection=collection, expected=expected, content=actual,
                inventory=local_bag_inventory(checked_root, collection['files']),
                audit=value, seal_sha256=value['seal']['sha256'], record_result=audit['record_result'])


def local_bag_inventory(root, files):
    """Complete immutable input names, lengths and hashes, never raw contents."""
    root = Path(root)
    for parent in (root, *root.parents):
        O.SQL._plain_path(parent, directory=True)
    require(O.SQL._snapshot(root, files) == files, 'Sealed local input filenames/hash changed')
    inventory = {}
    for name, expected in files.items():
        data = (root / name).read_bytes()
        require(O.digest(data) == expected, 'Sealed local input changed during complete read')
        inventory[name] = dict(bytes=len(data), sha256=expected)
    require(O.SQL._snapshot(root, files) == files, 'Sealed local input changed across inventory')
    return inventory


def normal_cleanup(cleanup):
    return (set(cleanup.get('sides', {})) == {'A', 'B'} and all(row.get('absent') is True and
            row.get('forced_signals') == [] and 'error' not in row for row in cleanup['sides'].values()))


def prepare(selection_path, run=None):
    selection_path = BASE.runtime_path(selection_path, EVIDENCE)
    selection = read(selection_path)
    require(set(selection) == {'schema', 'case', 'package', 'qualification', 'routes', 'identity', 'external',
                             'recording', 'previous_play_audit'} and selection['schema'] == 1 and selection['case'] in CASES, 'Exact R35 selection schema')
    bindings = {str(selection_path): sha(selection_path)}
    derivation = checked_descriptor(selection['package'], bindings, EVIDENCE)
    package, files = checked_package(derivation, bindings)
    checked_qualification(selection['qualification'], derivation, bindings)
    require(selection['external']['sha256'] == BAG.EXTERNAL_SHA and selection['identity']['sha256'] == BAG.IDENTITY_SHA, 'Reuse exact admitted external providers/identity')
    identity = read(checked_descriptor(selection['identity'], bindings))
    external = read(checked_descriptor(selection['external'], bindings))
    routes = read(checked_descriptor(selection['routes'], bindings))
    require(identity['main_execution_result'] == 'PASS_IDENTITY_READINESS_ONLY' and
            {r['side']: r['new_token'] for r in identity['boards']} == {'A': 672006291, 'B': 672042405}, 'Existing admitted identity only')
    observed = datetime.fromisoformat(routes['observed_utc'])
    require(observed.utcoffset() is not None and 0 <= (datetime.now(timezone.utc) - observed).total_seconds() <= 300 and
            routes.get('cross_confirmed') is True and bool(routes.get('bindings')), 'Fresh paired query required')
    for path, digest in routes['bindings'].items():
        # Preserve the existing exact SDK product path/hash provenance exception.
        # It is read only and cannot become a deployment or output directory.
        bindings[str(Q.route_binding(path, digest))] = digest
    peers, udids = {}, {}
    for side in 'AB':
        row = routes['sides'][side]
        require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == row['remote_exit'] == 0, 'Actual query target/exit')
        for kind in ('stdout', 'udid'):
            path = checked_descriptor(dict(path=row[kind + '_file'], sha256=row[kind + '_sha256']), bindings)
            data = path.read_bytes()
            if kind == 'stdout':
                peers[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
            else:
                require(1 <= len(data) <= 64 and not any(x in data for x in (b'\0', b'\r', b'\n')), 'UDID bytes')
                udids[side] = data.decode('utf-8')
        require(len(udids[side].encode()) == peers[side]['udid_bytes'], 'Query UDID length')
    require(udids['A'] != udids['B'] and all(peers[p]['local'] in peers[s]['remotes'] for s, p in [('A', 'B'), ('B', 'A')]), 'Cross-confirmed opposite pair only')
    recording = None
    if selection['case'] == CASES[0]:
        require(selection['recording'] is None and selection['previous_play_audit'] is None, 'Record cannot consume old playback')
    else:
        recording = checked_recording(selection['recording'], derivation, bindings)
        if selection['case'] == CASES[1]:
            require(selection['previous_play_audit'] is None, 'Default play is its own initial mode')
        else:
            audit = checked_sealed_audit(selection['previous_play_audit'], 'R35_INDEPENDENT_PLAY_AUDIT', bindings)
            prior_path = checked_descriptor(audit['play_result'], bindings, EVIDENCE)
            prior = read(prior_path)
            require(audit.get('allows_no_loan') is True and audit.get('status') in ('PASS', 'FAIL_AUDITED') and
                    prior.get('scope') == SCOPE and prior.get('case') == CASES[1] and prior.get('record_run') == recording['run'] and
                    prior.get('normal_process_exit') is True and prior.get('experiment_validity') == 'PASS' and
                    prior.get('functionality') == 'PASS' and prior.get('blocked') == [] and prior.get('source_sha256') == sha(__file__) and
                    prior.get('package') == descriptor(derivation) and prior.get('bag_seal_sha256') == recording['seal_sha256'],
                    'Default case needs independent audit, complete cleanup and unchanged input before control')
            for path, digest in prior['evidence'].items():
                checked_descriptor(dict(path=path, sha256=digest), bindings, EVIDENCE)
            require(str(prior_path.parent / 'CLEANUP.json') in prior['evidence'] and
                    normal_cleanup(read(prior_path.parent / 'CLEANUP.json')), 'Default failure cleanup/ownership is unresolved')
    current = source_inventory()
    bindings.update(current)
    run = run or secrets.token_hex(8)
    require(re.fullmatch('[0-9a-f]{16}', run) and (recording is None or run != recording['run']), 'Fresh operation run')
    plan = dict(schema=1, scope=SCOPE, case=selection['case'], run=run,
        record_run=run if recording is None else recording['run'], package=str(package), files=files,
        inputs=selection, bindings=bindings, recording=recording, selection_file=descriptor(selection_path), limits=LIMITS)
    return plan, peers, udids, observed, external


def checked_plan(path):
    plan = read(path)
    checked, peers, udids, observed, external = prepare(plan['selection_file']['path'], plan['run'])
    require(plan == checked, 'Prepared R35 plan changed')
    return plan, peers, udids, observed, external


def release_for(plan, prepared, operations_review):
    return dict(schema=1, scope='MAIN_ONE_SHOT_R35_BAG_RELEASE', case=plan['case'], run=plan['run'],
                record_run=plan['record_run'], prepared_sha256=sha(prepared), runner_sha256=sha(__file__),
                operations_review=operations_review)


def checked_release(args, plan):
    release = read(args.release)
    require(release == release_for(plan, args.prepared, release.get('operations_review')), 'Exact mode/run/prepared-bound Main release')
    review = read(checked_descriptor(release['operations_review'], {}, EVIDENCE))
    require(review.get('scope') == 'R35_OPERATIONAL_REVIEW' and review.get('status') == 'PASS' and
            review.get('case') == plan['case'] and review.get('prepared_sha256') == sha(args.prepared) and
            review.get('limits') == LIMITS, 'Exact operational review required')
    return release


def operation_argv(case, run, record_run, side, root, contract_sha, bag_seal_sha=None):
    require(case in CASES and side in 'AB' and re.fullmatch('[0-9a-f]{16}', run) and re.fullmatch('[0-9a-f]{16}', record_run), 'Argv identity')
    argv = [root + '/package/run-token-first']
    if side == 'A':
        argv += ['--script', root + '/package/' + MEMBER, '--case', case, '--run', run, '--record-run', record_run,
                 '--side', 'A', '--role', 'bag-publisher' if case == CASES[0] else 'bag-subscriber',
                 '--finish-control', root + '/control.PROBE_FINISH', '--contract', root + '/r35-contract.json', '--contract-sha256', contract_sha]
        if case != CASES[0]:
            argv += ['--resume-control', root + '/control.R35_RESUME', '--bag-seal-sha256', bag_seal_sha]
    elif case == CASES[0]:
        argv += ['ros2', 'bag', 'record', '--output', root + '/bag', '--storage', 'sqlite3', '--storage-preset-profile', 'none',
                 '--compression-mode', 'none', '--max-cache-size', '0', '--disable-keyboard-controls', '--node-name', 'r35bagrec_' + run,
                 '--qos-profile-overrides-path', root + '/qos.yaml', '--topics', *[O.topic(record_run, l) for l in LANES]]
    else:
        argv += ['ros2', 'bag', 'play', '--input', root + '/input/bag', 'sqlite3', '--topics',
                 *[O.topic(record_run, l) for l in LANES], '--remap',
                 *[O.topic(record_run, l) + ':=' + O.topic(run, l, True) for l in LANES], '__node:=r35bagplayer_' + run,
                 '--start-paused', '--disable-keyboard-controls', '--wait-for-all-acked', '5000',
                 '--qos-profile-overrides-path', root + '/qos.yaml']
        if case == CASES[2]:
            argv += ['--disable-loan-message']
    return argv


class SerializedBagRunner(R.BagRecordPlayRunner):
    """New complete constructor; no fictitious String10 case or base constructor."""
    def __init__(self, args):
        self.args, self.output, self.owns_output = args, fresh(args.output, 'execution'), False
        self.plan, peers, udids, self.observed, self.external = checked_plan(args.prepared)
        checked_release(args, self.plan)
        BAG.bag_selected_hdc(args.hdc)  # validates selected executable; does not invoke HDC
        self.run, self.case, self.record_run = self.plan['run'], self.plan['case'], self.plan['record_run']
        self.package, self.files, self.recording = Path(self.plan['package']), self.plan['files'], self.plan['recording']
        self.routes, self.udids = peers, udids
        identity = read(self.plan['inputs']['identity']['path'])
        self.tokens = {r['side']: r['new_token'] for r in identity['boards']}
        self.readiness = {'owned_libpython': {'member': 'lib/libpython3.12.so.1.0'}}
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.roles = {'A': 'bag-publisher' if self.case == CASES[0] else 'bag-subscriber', 'B': None}
        self.contract = O.graph_contract(self.case, self.run, self.record_run)
        self.contract_bytes = (json.dumps(self.contract, sort_keys=True, separators=(',', ':')) + '\n').encode('utf-8')
        self.contract_sha = O.digest(self.contract_bytes)
        self.expected_argv = {s: [self.prefix[s] + '/package/actionbag_python', *self.argv(s)[1:]] for s in 'AB'}
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.finished_fixtures, self.closed, self.maps_observed = set(), set(), {}
        self.normal_stop_attempted = self.normal_stop_confirmed = self.resume_attempted = False
        self.stop_state = dict(attempt_latched=False, intent_persisted=False, dispatch_entered=False,
                               dispatch_confirmed=False, dispatch_receipt_persisted=False, recording_completed=False)
        self.business_evidence = self.bag_receipt = None
        self.normal_process_exit, self.fidelity = False, 'NOT_RUN'
        self.output.mkdir()
        self.owns_output = True
        self.write('PLAN.json', self.plan)

    def argv(self, side):
        return operation_argv(self.case, self.run, self.record_run, side, self.prefix[side], self.contract_sha,
                              None if self.recording is None else self.recording['seal_sha256'])

    def configure(self, side):
        root = self.prefix[side]
        qos = self.output / (side + '.qos.yaml')
        qos.write_text(''.join(O.topic(self.record_run, l) + ':\n  history: keep_last\n  depth: 10\n  reliability: reliable\n  durability: volatile\n' for l in LANES), encoding='utf-8', newline='\n')
        self.send(side, qos, root + '/qos.yaml')
        if side == 'A':
            contract = self.output / 'r35-contract.json'
            contract.write_bytes(self.contract_bytes)
            self.send(side, contract, root + '/r35-contract.json')
        if side == 'B' and self.recording is not None:
            self.root_guard(side)
            self.shell(side, 'umask 077; mkdir -m 700 ' + root + '/input && mkdir -m 700 ' + root + '/input/bag')
            for name, digest in self.recording['collection']['files'].items():
                source = Path(self.recording['collection']['local_directory']) / name
                require(sha(source) == digest, 'Sealed bag changed before upload')
                self.send(side, source, root + '/input/bag/' + name)
            self.check_play_input('PREFLIGHT')
        BAG.BagCliRunner.configure(self, side)

    def check_play_input(self, phase):
        require(self.recording is not None and phase in ('PREFLIGHT', 'PRERESUME', 'POSTFLIGHT'), 'Owned play input phase')
        root, files = self.recording['collection']['local_directory'], self.recording['collection']['files']
        before = local_bag_inventory(root, files)
        remote = self.remote_bag_inventory(self.prefix['B'] + '/input/bag')
        self.write('PLAY_INPUT_' + phase + '.json', remote)
        after = local_bag_inventory(root, files)
        self.write('PLAY_LOCAL_' + phase + '.json', dict(before=before, after=after))
        require(before == after == remote == self.recording['inventory'], 'Complete sealed play input names/size/hash mismatch: ' + phase)
        if phase != 'PREFLIGHT':
            require(remote == read(self.output / 'PLAY_INPUT_PREFLIGHT.json'), 'Sealed play input differs from admitted preflight')
        return remote

    def poll(self, side):
        root = self.prefix[side]
        raw = self.shell(side, 'test -f ' + root + '/stdout && test -f ' + root + '/stderr && test "$(stat -c %s ' + root +
            '/stdout)" -le 4194304 && test "$(stat -c %s ' + root + '/stderr)" -le 4194304 && cat ' + root +
            '/stdout && printf "\\nR25_STDERR\\n" && cat ' + root + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        require(not re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE', text), 'Native operation failure')
        if side == 'A':
            O.rows(text, self.case, self.run, self.record_run, self.owned[side]['child']['pid'])
        status = self.shell(side, 'if test -f ' + root + '/exit; then cat ' + root + '/exit; fi').decode().strip()
        return text, status

    def maps(self, side, label):
        ORDINARY.OrdinaryCliRunner.maps(self, side, label)
        if side == 'B' and label == 'business':
            actual = read(self.output / (side + '.' + label + '.providers.json'))['sha256']
            needed = ('librosbag2_storage_sqlite3.so', 'libsqlite3.so', 'librcl_interfaces__rosidl_typesupport_cpp.so',
                      'librcl_interfaces__rosidl_typesupport_fastrtps_cpp.so', 'libstd_msgs__rosidl_typesupport_cpp.so')
            require(all(self.prefix[side] + '/package/lib/' + n in actual for n in needed), 'Actual selected C++ type/SQLite providers not mapped')

    def records(self, text):
        return O.rows(text, self.case, self.run, self.record_run, self.owned['A']['child']['pid'])

    def business(self, text, final=False):
        return O.probe_business(self.records(text), self.case, self.run, self.record_run, final)

    def normal_recorder_stop(self, texts, business):
        require(self.case == CASES[0] and not self.normal_stop_attempted and business['complete'] and
                'Recording...' in texts['B'] and texts['B'].count('Recording stopped') == 0 and
                all("Subscribed to topic '" + O.topic(self.run, l) + "'" in texts['B'] for l in LANES), 'Exact normal stop prerequisites')
        require(all((self.output / (s + '.business.providers.json')).is_file() for s in 'AB'), 'Both live map receipts required')
        self.normal_stop_attempted = self.stop_state['attempt_latched'] = True
        intent = dict(schema=1, scope=SCOPE, run=self.run, side='B', signal='TERM', owned=self.owned['B'],
                      case=self.case, source_contract='STOCK_RECORD_VERB_SIGTERM_EVENT_NORMAL_STOP', no_repeat=True,
                      maps={s: sha(self.output / (s + '.business.providers.json')) for s in 'AB'})
        try:
            exclusive(self.output / 'NORMAL_STOP_INTENT.json', intent)
            self.stop_state['intent_persisted'] = True
            self.guard('B')
            self.root_guard('B')
            self.stop_state['dispatch_entered'] = True
            self.shell('B', 'kill -TERM ' + str(self.owned['B']['child']['pid']))
            self.stop_state['dispatch_confirmed'] = True
            exclusive(self.output / 'NORMAL_STOP_DISPATCH.json', dict(run=self.run, command_sequence=self.sequence,
                signal='TERM', intent_sha256=sha(self.output / 'NORMAL_STOP_INTENT.json')))
            self.stop_state['dispatch_receipt_persisted'] = True
            self.normal_stop_confirmed = True
        finally:
            self.write('STOP_STATE.json', dict(self.stop_state, classification=self.stop_classification()))

    def stop_classification(self):
        if not self.stop_state['attempt_latched']:
            return 'NOT_ATTEMPTED'
        if not self.stop_state['dispatch_entered']:
            return 'NOT_DISPATCHED'
        if not self.normal_stop_confirmed:
            return 'DISPATCH_UNCONFIRMED_OR_RECEIPT_UNPERSISTED_NO_REPEAT'
        return 'RECORDING_COMPLETED' if self.stop_state['recording_completed'] else 'DISPATCH_CONFIRMED_CLOSE_UNPROVEN'

    def resume_playback(self, business):
        require(self.case != CASES[0] and not self.resume_attempted and business['ready'] is not None and
                not business['received'] and (self.output / 'B.business.providers.json').is_file(), 'One Resume after exact readiness')
        require(self.stage == 'BUSINESS' and self.stage_deadline is not None and time.monotonic() < self.stage_deadline,
                'Pre-Resume checks require the remaining original business budget')
        self.guard('A')
        self.root_guard('A')
        self.guard('B')
        self.check_play_input('PRERESUME')
        require(time.monotonic() < self.stage_deadline, 'Pre-Resume input checks exceeded original business budget')
        self.resume_attempted = True
        data = dict(schema=1, scope='R35_RESUME', case=self.case, run=self.run, record_run=self.record_run,
                    pid=self.owned['A']['child']['pid'], bag_seal_sha256=self.recording['seal_sha256'])
        exclusive(self.output / 'RESUME_INTENT.json', data)
        target = self.prefix['A'] + '/control.R35_RESUME'
        self.shell('A', 'umask 077; set -C; test ! -e ' + target + ' && test ! -L ' + target +
            ' && test ! -e ' + target + '.tmp && test ! -L ' + target + '.tmp && printf %s ' +
            shlex.quote(json.dumps(data, sort_keys=True, separators=(',', ':'))) + ' > ' + target +
            '.tmp && chmod 600 ' + target + '.tmp && mv ' + target + '.tmp ' + target)
        self.write('RESUME_DISPATCH.json', dict(run=self.run, command_sequence=self.sequence, contents=data))

    def workflow(self):
        first = time.monotonic()
        self.stage, self.stage_deadline = 'LISTENER', first + 60
        for side in 'AB':
            self.launch(side)
        while True:
            texts, statuses = self.poll_pair()
            if all(self.ready(s, texts[s], 'LISTENER') for s in 'AB'):
                break
        for side in 'AB':
            self.maps(side, 'listener')
        require(time.monotonic() < first + 60, 'Initial listener/maps deadline')
        self.origin = time.monotonic()
        self.write('CLOCK.json', dict(origin='BEFORE_FIRST_LISTENER_RELEASE', business_origin=self.origin,
            business_deadline=self.origin + 35, normal_close_release_deadline=self.origin + 50,
            probe_origin='ACTUAL_A_NODE_CREATE_RETURN', limits=LIMITS))
        self.stage, self.stage_deadline = 'BUSINESS', self.origin + 35
        for side in 'AB':
            self.publish(side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + self.run + ' side=' + side + ' stage=LISTENER_READY\n')
        while True:
            texts, statuses = self.poll_pair()
            business = self.business(texts['A'])
            if self.case == CASES[0]:
                require(self.normal_stop_attempted or not self.ready('B', texts['B'], 'CLOSE'), 'Recorder closed before normal TERM')
                if business['complete'] and not self.normal_stop_attempted:
                    for side in 'AB':
                        self.maps(side, 'business')
                    self.normal_recorder_stop(texts, business)
            elif business['ready'] is not None and not self.resume_attempted:
                for side in 'AB':
                    self.maps(side, 'business')
                self.resume_playback(business)
            if business['complete'] and self.ready('B', texts['B'], 'CLOSE'):
                if self.case == CASES[0]:
                    require(self.normal_stop_confirmed and texts['B'].count('Recording stopped') == 1, 'Stock stop completion observation missing')
                break
        self.stage, self.stage_deadline = 'CLOSE', self.origin + 50
        self.finish_fixture('A', 'NORMAL_COMPLETION')
        require(time.monotonic() < self.origin + 40, 'A finish release crossed probe lifetime envelope')
        while True:
            texts, statuses = self.poll_pair()
            if all(self.ready(s, texts[s], 'CLOSE') for s in 'AB'):
                break
        for side in 'AB':
            self.maps(side, 'close')
        for side in 'AB':
            self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run +
                ' side=' + side + ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
            self.closed.add(side)
        require(time.monotonic() < self.origin + 50, 'Original50s paired close release deadline')
        self.stage, self.stage_deadline = 'EXIT', time.monotonic() + 20
        self.wait_exit(20)
        require(time.monotonic() < self.stage_deadline, 'Original20s exit deadline')

    def poll_pair(self):
        texts, statuses = {}, {}
        for side in 'AB':
            texts[side], statuses[side] = self.poll(side)
        require(time.monotonic() < self.stage_deadline and not any(statuses.values()), 'Stage deadline/early child exit')
        time.sleep(0.01)
        return texts, statuses

    def analyze(self, texts):
        records = self.records(texts['A'])
        business = self.business(texts['A'], True)
        support = O.one(records, 'TYPE_SUPPORT')
        require(support['capsules'] == {'event': 'PyCapsule', 'string': 'PyCapsule'} and
                set(support['origins']) == {'rclpy', 'rclpy._rclpy_pybind11', 'rclpy.serialization', 'rclpy.executors',
                    'rclpy.publisher', 'rclpy.subscription', 'rcl_interfaces.msg', 'std_msgs.msg', 'rosbag2_py'}, 'Actual selected native type/module evidence')
        require(all(self.files.get(v['member']) == v['sha256'] for v in support['origins'].values()), 'Actual native module origin/hash differs')
        for side in 'AB':
            require(NATIVE.actionbag_fields(texts[side], 'ROS_PYTHON_INTERPRETER_RETURN') == [{'execution_rc': '0', 'finalize_rc': '0'}] and
                    NATIVE.actionbag_fields(texts[side], 'ROS_PYTHON_FINAL') == [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1', 'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}] and
                    NATIVE.actionbag_fields(texts[side], 'ROS_IDENTITY_FINAL') == [{'restored': '1', 'business_rc': '0'}], 'Native/interpreter/identity finalization differs')
            R.recordplay_errors((self.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8'),
                (self.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8'), self.run, side, self.owned[side]['child']['pid'], self.files)
        self.write('NATIVE.json', NATIVE.actionbag_native_capture(texts, self.run, {s: self.owned[s]['child']['pid'] for s in 'AB'}))
        locator = REPO / 'scripts/public_socket/analyze_v1_locators.py'
        require(sha(locator) == '582971f7d631ddffde9ec59c2e677c0ad756d2271d3ce1df6204e35ed0da93ff', 'Frozen V1 locator analysis changed')
        locators = runpy.run_path(str(locator))
        self.write('window.json', {'run': self.run})
        derived = {s: locators['derive']({'namespace': 'mdds.r25.action', 'domain': 201, 'udid': self.udids[s]}, 'AB'.index(s) + 1) for s in 'AB'}
        self.write('V1_LOCATORS.json', locators['analyze'](self.output, derived))
        self.business_evidence = business
        if self.case == CASES[0]:
            defs = O.one(records, 'DEFINITIONS')['definitions']
            static = O.definitions(self.package)
            wanted_defs, topics, published = {}, {}, {}
            sources = business['ready']['source_endpoints']
            for lane in LANES:
                endpoint = sources[lane]
                require(endpoint['owner'] == '/r35bagpub_' + self.run and endpoint['type'] == O.TYPES[lane] and endpoint['kind'] == 'publisher', 'Actual source endpoint differs')
                thash = endpoint['type_hash']
                generated = read(self.package / 'share' / O.TYPES[lane].split('/')[0] / 'msg' / (O.TYPES[lane].split('/')[-1] + '.json'))
                selected = next(x['hash_string'] for x in generated['type_hashes'] if x['type_name'] == O.TYPES[lane])
                require(thash == '' or thash == selected, 'Actual graph type hash conflicts with selected definition')
                native = defs[O.TYPES[lane]]
                require(native['encoding'] == static[O.TYPES[lane]]['encoding'] and native['encoded_message_definition'] == static[O.TYPES[lane]]['encoded_message_definition'] and
                        native['native_type_hash'] in ('', thash), 'Actual full native definition differs from selected dependencies')
                topics[lane] = dict(type_description_hash=thash, offered_qos_profiles=[endpoint['qos']])
                candidate = dict(static[O.TYPES[lane]], type_description_hash=thash)
                require(O.TYPES[lane] not in wanted_defs or candidate == wanted_defs[O.TYPES[lane]], 'Same-type source hash differs between lanes')
                wanted_defs[O.TYPES[lane]] = candidate
                for index in (1, 2):
                    raw = business['raw_data'][(lane, index)]
                    published[lane + ':' + str(index)] = dict(cdr_base64=base64.b64encode(raw).decode(), cdr_bytes=len(raw), cdr_sha256=O.digest(raw))
            self.expected = dict(schema=1, record_run=self.run, topics=topics, definitions=wanted_defs, published=published)
        else:
            stored = O.stored_buffers(self.recording['collection']['local_directory'], self.recording['expected'])
            comparisons = []
            for key, raw in business['raw_data'].items():
                source = stored[key]
                comparisons.append(dict(lane=key[0], index=key[1], storage_bytes=len(source),
                    storage_sha256=O.digest(source), raw_bytes=len(raw), raw_sha256=O.digest(raw), equal=raw == source))
            self.fidelity = 'PASS' if all(x['equal'] for x in comparisons) else 'FAIL'
            self.write('REPLAY_CONTENT.json', dict(functionality='PASS', persistence_fidelity=self.fidelity, comparisons=comparisons,
                buffers={l + ':' + str(i): v for (l, i), v in business['metrics'].items()}, effective_can_loan_messages='UNOBSERVABLE_WITH_STOCK_CLI'))

    def cleanup(self, failed):
        # Same conservative no-B-TERM/KILL-after-attempt rule as the frozen owner.
        result = dict(directories_retained=True, identity_mutations=0, sides={}, pending=self.launch_pending)
        for side in self.owned:
            status = dict(forced_signals=[], absent=False)
            attempted = side == 'B' and self.normal_stop_attempted
            ambiguous = attempted and self.stop_state['dispatch_entered'] and not self.normal_stop_confirmed
            try:
                self.stage_deadline = time.monotonic() + 10
                require('child' in self.owned[side], 'Partial launch child unknown; no signal permitted')
                if attempted:
                    status['normal_stop_dispatch'] = self.stop_classification()
                if ambiguous:
                    self.blocked.append('NORMAL_STOP: uncertain dispatch/receipt; B read-only; no repeat/control')
                if failed and not ambiguous and self.cleanup_observe(side):
                    for name in ('control.PROBE_FINISH', 'control.RUNTIME_CLOSE'):
                        if name == 'control.PROBE_FINISH' and (not self.roles[side] or side in self.finished_fixtures):
                            continue
                        exists = self.shell(side, 'if test -e ' + self.prefix[side] + '/' + name + ' || test -L ' + self.prefix[side] + '/' + name + '; then printf 1; else printf 0; fi').decode().strip()
                        if exists == '0':
                            data = ('R25_PROBE_FINISH schema=1 run=' + self.run + ' side=' + side + ' pid=' + str(self.owned[side]['child']['pid']) + ' reason=WINDOW_FAILED\n') if name == 'control.PROBE_FINISH' else (
                                'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side + ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=WINDOW_FAILED\n')
                            if not self.publish(side, name, data, cleanup_absent_ok=True):
                                break
                            if name == 'control.PROBE_FINISH':
                                self.finished_fixtures.add(side)
                for signal, budget in ([(None, 10)] if attempted else [(None, 10), ('TERM', 5), ('KILL', 2)]):
                    self.stage_deadline = time.monotonic() + budget
                    if not self.cleanup_observe(side):
                        break
                    if signal:
                        self.guard(side)
                        self.shell(side, 'kill -' + signal + ' ' + str(self.owned[side]['child']['pid']))
                        status['forced_signals'].append(signal)
                    while self.stage_deadline - time.monotonic() >= 1 and self.cleanup_observe(side):
                        time.sleep(0.1)
                self.stage_deadline = time.monotonic() + 3
                child_absent = not self.cleanup_observe(side)
                parent_present = self.cleanup_observe(side, role='supervisor')
                while parent_present and self.stage_deadline - time.monotonic() >= 1:
                    time.sleep(0.1)
                    parent_present = self.cleanup_observe(side, role='supervisor')
                status['absent'] = child_absent and not parent_present
                require(status['absent'], 'Owned process absence unconfirmed; retained without normal-stop retry')
                self.stage_deadline = None
                self.verify_files(side)
                providers = self.maps_observed.get(side, {})
                for start in range(0, len(providers), 20):
                    names = sorted(providers)[start:start + 20]
                    raw = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(p) for p in names)).decode()
                    require({line.split(maxsplit=1)[1].strip(): line.split(maxsplit=1)[0] for line in raw.splitlines()} ==
                            {n: providers[n] for n in names}, 'Mapped provider postflight drift')
            except (ValueError, OSError) as error:
                status['error'] = str(error)
                self.blocked.append('CLEANUP ' + side + ': ' + str(error))
            finally:
                self.stage_deadline = None
            if status['forced_signals']:
                self.errors.append('Forced owned termination: ' + side)
            result['sides'][side] = status
        for side in self.launch_pending:
            if side not in self.owned or 'child' not in self.owned[side]:
                self.blocked.append('LAUNCH ' + side + ': unknown retained; no redispatch/signal')
        self.write('CLEANUP.json', result)

    def closed_bag(self):
        cleanup = read(self.output / 'CLEANUP.json')
        require(set(cleanup['sides']) == {'A', 'B'} and all(r.get('absent') is True and r.get('forced_signals') == [] and
                'error' not in r for r in cleanup['sides'].values()), 'Normal four-PID absence/zero forced signals required')
        self.stage, self.stage_deadline = 'POSTCLOSE_BAG', time.monotonic() + 60
        root = self.prefix['B'] + ('/bag' if self.recording is None else '/input/bag')
        before = self.remote_bag_inventory(root) if self.recording is None else self.check_play_input('POSTFLIGHT')
        if self.recording is not None:
            require(before == read(self.output / 'PLAY_INPUT_PREFLIGHT.json') and
                    {n: x['sha256'] for n, x in before.items()} == self.recording['collection']['files'] and
                    all(sha(Path(self.recording['collection']['local_directory']) / n) == h for n, h in self.recording['collection']['files'].items()), 'Immutable playback input drift')
            self.write('PLAY_INPUT_POSTFLIGHT.json', before)
        else:
            self.write('BAG_REMOTE_BEFORE.json', before)
            local = self.output / 'closed-bag'
            local.mkdir()
            for name, info in before.items():
                raw = self.command('B', ['file', 'recv', root + '/' + name, str(local / name)], 30)
                require(b'transfer finish' in raw.lower() and (local / name).stat().st_size == info['bytes'] and sha(local / name) == info['sha256'], 'Complete bag transfer mismatch')
            after = self.remote_bag_inventory(root)
            require(after == before, 'Remote bag changed during collection')
            self.write('BAG_REMOTE_AFTER.json', after)
            self.expected['files'] = {n: x['sha256'] for n, x in before.items()}
            self.write('SOURCE_EXPECTATIONS.json', self.expected)
            self.write('COLLECTED_BAG.json', dict(run=self.run, local_directory=str(local), files=self.expected['files'], normal_closed_before_collection=True))
            self.bag_receipt = O.verify_recorded_bag(local, self.expected)
            self.fidelity = self.bag_receipt['persistence_fidelity']
            self.write('BAG_CONTENT.json', self.bag_receipt)
            self.stop_state['recording_completed'] = True
            self.write('STOP_STATE_COMPLETED.json', dict(self.stop_state, classification=self.stop_classification()))
        require(time.monotonic() < self.stage_deadline, 'Original60s post-exit collection/oracle deadline')

    def execute(self):
        consumed, workflow_normal = False, False
        try:
            release = checked_release(self.args, self.plan)
            checked_plan(self.args.prepared)
            require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Query dispatch freshness expired')
            exclusive(self.args.prepared.parent / (self.run + '.CONSUMED.json'), dict(release_sha256=sha(self.args.release), output=str(self.output), utc=datetime.now(timezone.utc).isoformat()))
            consumed = True
            self.write('MAIN_RELEASE.json', release)
            self.deploy()
            self.workflow()
            self.stage, self.stage_deadline = 'ANALYZE', time.monotonic() + 60
            self.analyze(self.collect())
            require(time.monotonic() < self.stage_deadline, 'Original60s analysis deadline')
            workflow_normal = True
        except ValueError as error:
            self.errors.append(self.stage + ': ' + str(error))
        except (Exception, KeyboardInterrupt) as error:
            self.blocked.append(self.stage + ': ' + repr(error))
        finally:
            if consumed:
                self.cleanup(bool(self.errors or self.blocked))
                if workflow_normal and not self.errors and not self.blocked:
                    try:
                        self.closed_bag()
                        self.normal_process_exit = True
                    except ValueError as error:
                        self.errors.append('POSTCLOSE_BAG: ' + str(error))
                    except (Exception, KeyboardInterrupt) as error:
                        self.blocked.append('POSTCLOSE_BAG: ' + repr(error))
                elif self.owned:
                    try:
                        self.collect()
                    except (Exception, KeyboardInterrupt) as error:
                        self.blocked.append('FAILURE_CAPTURE: ' + repr(error))
            if self.fidelity == 'FAIL':
                self.errors.append('Complete-buffer persistence fidelity FAIL; no normalization or implicit next mode')
            self.stage_deadline = None
            passed = not self.errors and not self.blocked and self.normal_process_exit
            buffers = [] if self.business_evidence is None else list(self.business_evidence['metrics'].values())
            anomaly = ('OBSERVED_NONZERO_SUFFIX' if any(x['suffix_nonzero_bytes'] for x in buffers) else
                       'OBSERVED_SUFFIX' if any(x['suffix_bytes'] for x in buffers) else 'NOT_OBSERVED_IN_THIS_RUN')
            evidence = {str(p): sha(p) for p in self.output.rglob('*') if p.is_file()}
            self.write('RESULT.json', dict(schema=1, scope=SCOPE, case=self.case, run=self.run, record_run=self.record_run,
                result='PASS' if passed else 'FAIL' if self.errors else 'BLOCKED', failures=self.errors, blocked=self.blocked,
                experiment_validity='PASS' if self.normal_process_exit else 'NOT_VERIFIED', functionality='PASS' if workflow_normal else 'NOT_VERIFIED',
                persistence_fidelity=self.fidelity, normal_process_exit=self.normal_process_exit, recording_completed=self.stop_state['recording_completed'],
                anomaly_reproduction=anomaly, compatibility=('INCONCLUSIVE_ANOMALY_NOT_REPRODUCED' if anomaly == 'NOT_OBSERVED_IN_THIS_RUN' else
                    'BOUNDED_MODE_TYPES_SAMPLES_PASS' if passed else 'OPEN_FAILED_COMPARISON_OR_VALIDITY'),
                bag_seal_sha256=None if self.recording is None else self.recording['seal_sha256'], source_sha256=sha(__file__),
                package=self.plan['inputs']['package'], evidence=evidence, sdk_epoch='NOT_PROVEN', recorder_internal_take='UNOBSERVABLE',
                strong_dds='BLOCKED_PROVENANCE_GAP', general_compatibility='OPEN_NOT_VERIFIED', utc=datetime.now(timezone.utc).isoformat()))
        return 0 if passed else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('compose', 'prepare', 'run'))
    parser.add_argument('--output', type=Path, required=True)
    for name in ('inputs', 'prepared', 'release', 'hdc'):
        parser.add_argument('--' + name, type=Path)
    args = parser.parse_args(argv)
    if args.mode == 'compose':
        compose(args.output)
        return 0
    if args.mode == 'prepare':
        require(args.inputs is not None, 'Selection required')
        output = fresh(args.output, 'prepared')
        plan, *_ = prepare(args.inputs)
        output.mkdir()
        exclusive(output / 'PLAN.json', plan)
        return 0
    require(all(getattr(args, n) is not None for n in ('prepared', 'release', 'hdc')), 'Run requires exact prepared/release/HDC')
    return SerializedBagRunner(args).execute()


if __name__ == '__main__':
    raise SystemExit(main())
