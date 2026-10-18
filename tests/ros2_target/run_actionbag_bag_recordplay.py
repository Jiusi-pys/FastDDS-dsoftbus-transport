"""Finite cross-board stock rosbag record/play windows with owned process guards.

Prepare is local. A run needs fresh paired routes and an exact Main release.
Successful recording includes normal close and immutable collection; playback
uses that sealed recording in a different run and one A-side Resume request.
"""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import runpy
import secrets
import shlex
import subprocess
import time

REPO = Path(__file__).resolve().parents[2]
BAG_PATH = Path(__file__).with_name('run_actionbag_bag.py')
BAG_SHA = '7825bcb5cb39ff916d5350f3fce2a1b91390fd965f02828b2cd62c8744f98937'
if hashlib.sha256(BAG_PATH.read_bytes()).hexdigest() != BAG_SHA:
    raise ValueError('Frozen ABI ownership module changed')
_spec = importlib.util.spec_from_file_location('recordplay_frozen_bag', BAG_PATH)
BAG = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(BAG)
ORDINARY, NATIVE, SHA, LOAD = BAG.ORDINARY, BAG.NATIVE, BAG.bag_sha, BAG.bag_json
COMPOSER_PATH = REPO / 'integrations/ros2/target/compose_bag_recordplay_runtime.py'
COMPOSER_SHA = 'fbeab1dac6d2ffd9f57a63d4fcbfc99045543de1b222584cc6b9a73963dfcb8d'
ORACLE_PATH = Path(__file__).with_name('bag_sqlite_oracle.py')
ORACLE_SHA = '88ab482bee3acfca529b1b61d21605c078b3a50fa42444ab7ee3c98b857b5c8b'
if SHA(COMPOSER_PATH) != COMPOSER_SHA or SHA(ORACLE_PATH) != ORACLE_SHA:
    raise ValueError('Selected composition/content implementation changed')
_spec = importlib.util.spec_from_file_location('recordplay_composer', COMPOSER_PATH)
COMPOSER = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(COMPOSER)
ORACLE = runpy.run_path(str(ORACLE_PATH))
BASE = COMPOSER.BASE
OUTPUT_ROOT = COMPOSER.ROOT / 'build/r25-native-actionbag'
CASES = ('bag_record_A_to_B', 'bag_play_B_to_A')
EVENTS = {'NODE_READY', 'ENTITY_READY', 'BAG_DEFINITION', 'BAG_PUBLISHER_INFO', 'BAG_PUBLISH',
          'BAG_ACKED', 'BAG_REPLAY_READY', 'BAG_RESUME_ACCEPTED', 'BAG_RESUME_CALLED',
          'BAG_RESUME_RETURNED', 'BAG_RECEIVE', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN',
          'SHUTDOWN_RETURN', 'FINAL', 'FAIL'}
SOURCE_PATHS = [Path(__file__), Path(__file__).with_name('test_actionbag_bag_recordplay.py'),
    COMPOSER_PATH, REPO / 'tests/ros2_target/test_bag_recordplay_composition.py',
    COMPOSER.PROBE, REPO / 'tests/ros2_target/test_bag_recordplay_probe.py', BAG_PATH,
    BAG.ORDINARY_PATH, ORDINARY.NATIVE_PATH, ORACLE_PATH, COMPOSER.HELPER, COMPOSER.HELPERS.BASE_HELPER]


def recordplay_require(condition, message):
    if not condition:
        raise ValueError(message)


def recordplay_output(output, mode):
    # Reject lexical aliases and immutable input descendants before reading inputs.
    path = Path(output).absolute()
    recordplay_require(mode in ('prepared', 'execution') and path.parent == OUTPUT_ROOT and
        re.fullmatch('bag-recordplay-run-[0-9a-f]{16}-' + mode, path.name),
        'Output must be a fresh named direct child of the recordplay evidence root')
    checked = BASE.runtime_path(path, OUTPUT_ROOT, exists=False)
    recordplay_require(not checked.exists(), 'Fresh recordplay output required')
    return checked


def recordplay_descriptor(value, bindings):
    recordplay_require(isinstance(value, dict) and isinstance(value.get('path'), str) and
        isinstance(value.get('sha256'), str) and re.fullmatch('[0-9a-f]{64}', value['sha256']),
        'Exact evidence descriptor required')
    path = BASE.runtime_path(value['path'], COMPOSER.ROOT)
    recordplay_require(SHA(path) == value['sha256'], 'Selected evidence changed: ' + str(path))
    bindings[str(path)] = value['sha256']
    return {'path': str(path), 'sha256': value['sha256']}


def recordplay_records(text, run, pid, role):
    recordplay_require(role in ('bag-publisher', 'bag-subscriber'), 'Unknown bag probe role')
    rows = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=ORDINARY.ordinary_unique)
        recordplay_require(isinstance(row, dict) and all(type(row.get(k)) is int for k in ('schema', 'seq', 'pid')) and
            all(row.get(k) == v for k, v in {'schema': 1, 'run': run, 'side': 'A', 'pid': pid,
                'role': role, 'seq': len(rows) + 1}.items()), 'Probe identity/sequence differs')
        recordplay_require(row.get('event') in EVENTS and row['event'] != 'FAIL', 'Unknown/failing bag probe event')
        rows.append(row)
    return rows


def recordplay_business(text, run, record_run, pid, role, *, final=False):
    rows = recordplay_records(text, run, pid, role)
    single = {}
    payloads = []
    payload_event = 'BAG_PUBLISH' if role == 'bag-publisher' else 'BAG_RECEIVE'
    for row in rows:
        event = row['event']
        if event == payload_event:
            sequence = len(payloads)
            recordplay_require(sequence < 10 and type(row.get('sequence')) is int and row['sequence'] == sequence,
                               'Bag payload count/order differs')
            expected = 'r25bag_' + record_run + '_' + str(sequence).zfill(3)
            data = base64.b64decode(row['cdr_base64'], validate=True)
            recordplay_require(row.get('text') == expected and ORACLE['decode_string_cdr'](data) == expected and
                row.get('cdr_sha256') == hashlib.sha256(data).hexdigest() and type(row.get('cdr_bytes')) is int and
                row['cdr_bytes'] == len(data), 'Actual bag CDR differs')
            if role == 'bag-publisher':
                recordplay_require(type(row.get('matched')) is int and row['matched'] == 1 and
                                   'BAG_PUBLISHER_INFO' in single, 'Publish without one actual recorder/endpoint')
            else:
                recordplay_require('BAG_RESUME_CALLED' in single, 'Replay delivered before Resume')
            payloads.append(row)
        else:
            recordplay_require(event not in single, 'Duplicate single-use bag event')
            single[event] = row
    if not rows:
        recordplay_require(not final, 'Final bag probe records missing')
        return None
    expected_node = '/' + ('bagpub_' if role == 'bag-publisher' else 'bagsub_') + run
    recordplay_require(rows[0]['event'] == 'NODE_READY' and rows[0].get('name') == expected_node,
                       'Unexpected bag Node identity/order')
    if 'ENTITY_READY' in single:
        recordplay_require(single['ENTITY_READY'].get('entity') == role and
            single['ENTITY_READY'].get('name') == ('/bag_' if role == 'bag-publisher' else '/bagplay_') + run,
            'Bag entity identity differs')
    complete = False
    if role == 'bag-publisher':
        forbidden = {'BAG_REPLAY_READY', 'BAG_RESUME_ACCEPTED', 'BAG_RESUME_CALLED', 'BAG_RESUME_RETURNED', 'BAG_RECEIVE'}
        recordplay_require(not forbidden.intersection(single), 'Subscriber events in record window')
        complete = len(payloads) == 10 and 'BAG_ACKED' in single
        if 'BAG_ACKED' in single:
            ack = single['BAG_ACKED']
            recordplay_require(len(payloads) == 10 and ack['seq'] > payloads[-1]['seq'] and
                type(ack.get('published')) is int and ack['published'] == 10 and
                type(ack.get('timeout_seconds')) in (int, float) and 0 < ack['timeout_seconds'] <= 5,
                'Bounded acknowledgement evidence differs')
        if complete:
            recordplay_require(all(k in single for k in ('BAG_DEFINITION', 'ENTITY_READY', 'BAG_PUBLISHER_INFO')),
                               'Actual source definition/graph evidence missing')
    else:
        forbidden = {'BAG_DEFINITION', 'BAG_PUBLISHER_INFO', 'BAG_PUBLISH', 'BAG_ACKED'}
        recordplay_require(not forbidden.intersection(single), 'Publisher events in playback window')
        resume = ['BAG_REPLAY_READY', 'BAG_RESUME_ACCEPTED', 'BAG_RESUME_CALLED', 'BAG_RESUME_RETURNED']
        present = [name for name in resume if name in single]
        recordplay_require(present == resume[:len(present)] and
            [single[name]['seq'] for name in present] == sorted(single[name]['seq'] for name in present),
            'Resume control/service order differs')
        for name in ('BAG_REPLAY_READY', 'BAG_RESUME_CALLED'):
            if name in single:
                recordplay_require(single[name].get('service') == '/bagplayer_' + run + '/resume', 'Wrong Resume service')
        if 'BAG_REPLAY_READY' in single:
            recordplay_require(type(single['BAG_REPLAY_READY'].get('publisher_count')) is int and
                single['BAG_REPLAY_READY']['publisher_count'] == 1, 'Playback publisher count differs')
        if 'BAG_RESUME_ACCEPTED' in single:
            recordplay_require(single['BAG_RESUME_ACCEPTED'].get('record_run') == record_run, 'Wrong Resume record run')
        if 'BAG_RESUME_RETURNED' in single:
            recordplay_require(single['BAG_RESUME_RETURNED'].get('success') is True, 'Resume response failed')
        complete = len(payloads) == 10 and 'BAG_RESUME_RETURNED' in single
    if complete:
        recordplay_require('ENTITY_READY' in single, 'Bag entity creation evidence missing')
    if final or 'FINISH_ACCEPTED' in single:
        recordplay_require(complete, 'Normal finish with incomplete bag business')
    if final:
        tail = ['FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL']
        recordplay_require([r['event'] for r in rows[-4:]] == tail and single['FINISH_ACCEPTED'].get('reason') == 'NORMAL_COMPLETION'
            and all(single[k].get('returned') is True for k in tail[1:3]) and
            single['FINAL'].get('success') is True and single['FINAL'].get('node_created') is True,
            'Bag probe normal teardown/final evidence differs')
    return {'complete': complete, 'single': single, 'payloads': payloads}


def recordplay_argv(case, run, record_run, side, root):
    recordplay_require(case in CASES and side in ('A', 'B') and re.fullmatch('[0-9a-f]{16}', run) and
        re.fullmatch('[0-9a-f]{16}', record_run), 'Unknown bag case/run/side')
    recordplay_require((run == record_run) == (case == CASES[0]), 'Playback requires a distinct record run')
    argv = [root + '/package/run-token-first']
    if side == 'A':
        role = 'bag-publisher' if case == CASES[0] else 'bag-subscriber'
        argv += ['--script', root + '/package/scripts/bag_recordplay_probe.py', '--role', role,
                 '--run', run, '--side', side, '--record-run', record_run,
                 '--finish-control', root + '/control.PROBE_FINISH']
        if role == 'bag-subscriber':
            argv += ['--resume-control', root + '/control.BAG_RESUME']
    elif case == CASES[0]:
        argv += ['ros2', 'bag', 'record', '--output', root + '/bag', '--storage', 'sqlite3',
            '--storage-preset-profile', 'none', '--compression-mode', 'none', '--max-cache-size', '0',
            '--disable-keyboard-controls', '--node-name', 'bagrec_' + run,
            '--qos-profile-overrides-path', root + '/qos.yaml', '--topics', '/bag_' + run]
    else:
        argv += ['ros2', 'bag', 'play', '--input', root + '/input/bag', 'sqlite3', '--topics', '/bag_' + record_run,
            '--remap', '/bag_' + record_run + ':=/bagplay_' + run, '__node:=bagplayer_' + run,
            '--start-paused', '--disable-keyboard-controls', '--wait-for-all-acked', '5000',
            '--qos-profile-overrides-path', root + '/qos.yaml']
    return argv


def recordplay_expectations(business, run, package, files):
    recordplay_require(business is not None and business['complete'], 'Publisher incomplete')
    endpoint, definition = (business['single'][key] for key in ('BAG_PUBLISHER_INFO', 'BAG_DEFINITION'))
    recordplay_require(endpoint.get('node_name') == 'bagpub_' + run and endpoint.get('node_namespace') == '/' and
        endpoint.get('topic_type') == 'std_msgs/msg/String' and isinstance(endpoint.get('gid'), str) and
        re.fullmatch('[0-9a-f]{32}', endpoint['gid']) and endpoint['gid'] != '0' * 32, 'Source endpoint identity differs')
    type_hash = endpoint.get('type_description_hash')
    recordplay_require(isinstance(type_hash, str) and (type_hash == '' or re.fullmatch('RIHS01_[0-9a-f]{64}', type_hash)),
                       'Source endpoint type hash differs')
    profiles = endpoint.get('offered_qos_profiles')
    recordplay_require(isinstance(profiles, list) and len(profiles) == 1 and isinstance(profiles[0], dict),
                       'One actual offered QoS profile required')
    qos = profiles[0]
    durations = ('deadline', 'lifespan', 'liveliness_lease_duration')
    recordplay_require(set(qos) == {'history', 'depth', 'reliability', 'durability', 'liveliness',
        'avoid_ros_namespace_conventions', *durations}, 'Complete actual offered QoS required')
    recordplay_require(qos['history'] == 'unknown' and type(qos['depth']) is int and qos['depth'] == 0 and
        qos['reliability'] == 'reliable' and qos['durability'] == 'volatile' and qos['liveliness'] == 'automatic' and
        qos['avoid_ros_namespace_conventions'] is False, 'Selected RMW graph QoS conversion differs')
    for name in durations:
        value = qos[name]
        recordplay_require(isinstance(value, dict) and set(value) == {'sec', 'nsec'} and
            all(type(value[k]) is int for k in value) and value['sec'] >= 0 and 0 <= value['nsec'] < 1000000000,
            'Source graph duration differs')
    member = 'share/std_msgs/msg/String.msg'
    raw = (package / member).read_bytes()
    recordplay_require(hashlib.sha256(raw).hexdigest() == files[member] == definition.get('source_sha256') and
        definition.get('encoding') == 'ros2msg' and definition.get('encoded_message_definition') == raw.decode('utf-8') and
        isinstance(definition.get('type_hash'), str), 'Actual definition differs from selected source')
    # Selected SequentialWriter substitutes topic.type_description_hash for an empty definition hash.
    definition_hash = definition['type_hash'] or type_hash
    recordplay_require(definition_hash == type_hash, 'Native definition/endpoint type hash differs')
    return {'schema': 1, 'record_run': run, 'topic': '/bag_' + run, 'ros_distro': '',
            'offered_qos_profiles': profiles, 'type_description_hash': type_hash,
            'message_definition': {'encoding': 'ros2msg', 'encoded_message_definition': raw.decode('utf-8'),
                                   'type_description_hash': definition_hash}, 'published': business['payloads']}


def recordplay_errors(stdout, stderr, run, side, pid, files):
    """Source-bound optional unique-flow pair within one original stdout stream."""
    lines = stdout.splitlines()
    errors = [(index, raw, re.sub(r'\x1b\[[0-9;]*m', '', raw)) for index, raw in enumerate(lines)
              if re.search(r'\b(?:error|exception|failed)\b', re.sub(r'\x1b\[[0-9;]*m', '', raw), re.I)]
    recordplay_require(not any(re.search(r'\b(?:error|exception|failed)\b', re.sub(r'\x1b\[[0-9;]*m', '', line), re.I)
                               for line in stderr.splitlines()), 'Unexpected bag stderr diagnostic')
    if (files.get('lib/librmw_fastrtps_shared_cpp.so') != '0fc8f4a214849f4a5abe15f66baa23404448d0cebaf972607d40578200dcf369' or
            files.get('lib/librmw_fastrtps_cpp.so') != '6b637fe1292e664507811041e5576bad9291c9bae60b87237f7b92fa88476681'):
        raise OSError('BLOCKED_TRIAGE: optional-flow binary binding changed')
    if not errors:
        raise OSError('OBSERVATION_DRIFT: zero optional-flow pair requires triage')
    recordplay_require(len(errors) == 2 and all(re.fullmatch(
        r'(?:[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} )?' + re.escape(signature), errors[i][2])
        for i, signature in enumerate(ORDINARY.ERROR_PAIR)), 'Unknown/additional/reordered bag error')
    starts = [i for i, line in enumerate(lines) if line.startswith('ROS_CONNECT_ALLOWED ')]
    ends = [i for i, line in enumerate(lines) if line.startswith('ROS_RUNTIME_CLOSE_READY ')]
    if len(starts) != 1 or len(ends) != 1 or not starts[0] < errors[0][0] < errors[1][0] < ends[0]:
        raise OSError('BLOCKED_LOG_PHASE_TRIAGE: optional pair outside observed connect/close interval')
    close = NATIVE.actionbag_fields(stdout, 'ROS_RUNTIME_CLOSE_READY')
    recordplay_require(len(close) == 1 and all(close[0].get(k) == v for k, v in
        {'run': run, 'side': side, 'pid': str(pid)}.items()), 'Diagnostic close anchor identity differs')
    graph = [r for r in NATIVE.actionbag_fields(stderr, 'FDDST_ENDPOINT') if r.get('topic') == 'ros_discovery_info']
    recordplay_require([r.get('role') for r in graph] == ['writer', 'reader', 'reader'] and
        all(r.get('data_sharing') == 'OFF' for r in graph), 'Optional-flow writer1/reader2 context differs')
    return {'actual_pairs': 1, 'raw_lines': [r[1] for r in errors], 'unique_flow': 'NOT_PROVIDED',
            'phase': 'CONSERVATIVE_OBSERVED_STDOUT_ORDER_NOT_CAUSAL_LOG_ORDER', 'strict': 'UNTESTED'}


def recordplay_gates(gates, sources, runtime, freeze, bindings):
    recordplay_require(isinstance(gates, dict) and set(gates) == {'composition', 'harness'}, 'Complete independent gates required')
    for family, scope in [('composition', 'TWO_FILE_BAG_RECORDPLAY_STATIC_DERIVATION'),
                          ('harness', 'BAG_RECORDPLAY_HARNESS_HOST_ONLY')]:
        family_gates = gates[family]
        recordplay_require(isinstance(family_gates, dict) and set(family_gates) == {'initial', 'review', 'final'},
                           'Initial/review/final producers required')
        expected_sources = sources if family == 'harness' else {name: sources[name] for name in (
            'integrations/ros2/target/compose_bag_recordplay_runtime.py', 'tests/ros2_target/test_bag_recordplay_composition.py',
            'integrations/ros2/target/bag_recordplay_probe.py', 'tests/ros2_target/test_bag_recordplay_probe.py')}
        results, descriptors, dates = {}, {}, {}
        for stage, gate in family_gates.items():
            recordplay_require(isinstance(gate, dict) and set(gate) == {'result', 'seal'}, 'Producer result/seal required')
            descriptor = recordplay_descriptor(gate['result'], bindings)
            seal_path = Path(recordplay_descriptor(gate['seal'], bindings)['path'])
            seal = LOAD(seal_path)
            members = seal.get('files')
            recordplay_require(isinstance(members, (list, dict)) and bool(members), 'Empty producer seal')
            if isinstance(members, dict):
                expanded = []
                for name, digest in members.items():
                    relative = Path(name)
                    recordplay_require(isinstance(name, str) and name and not relative.is_absolute() and
                        not relative.drive and '..' not in relative.parts and relative != Path('.'),
                        'Unsafe relative producer seal member')
                    member_path = BASE.runtime_path(seal_path.parent / relative, seal_path.parent)
                    expanded.append({'path': str(member_path), 'sha256': digest})
                members = expanded
            sealed = {}
            for member in members:
                checked = recordplay_descriptor(member, bindings)
                recordplay_require(checked['path'] not in sealed, 'Duplicate producer seal member')
                sealed[checked['path']] = checked['sha256']
            recordplay_require(sealed.get(descriptor['path']) == descriptor['sha256'], 'Producer result not sealed')
            result = LOAD(descriptor['path'])
            recordplay_require(result.get('status') == 'PASS' and result.get('scope') == scope and
                result.get('sources') == expected_sources, 'Producer scope/status/source differs: ' + family + '/' + stage)
            if stage in ('initial', 'final'):
                recordplay_require(result.get('initial_gate' if stage == 'initial' else 'host_final') == 'PASS',
                                   'Producer execution stage did not pass')
            source = recordplay_descriptor(result.get('source_freeze'), bindings)
            recordplay_require(LOAD(source['path']).get('sources') == expected_sources, 'Producer source freeze differs')
            if family == 'harness':
                recordplay_require(source == freeze, 'Harness producer used a different freeze')
            recordplay_require(recordplay_descriptor(result.get('runtime_receipt'), bindings) == runtime,
                               'Producer tested a different runtime')
            if stage == 'review':
                recordplay_require(result.get('blocking_findings') == [] and result.get('assessment') == 'NO_ACTIONABLE_FINDINGS_FOUND',
                                   'Unresolved code review findings')
            date = datetime.fromisoformat(result['utc'])
            recordplay_require(date.utcoffset() is not None, 'Producer timestamp lacks timezone')
            dates[stage], results[stage], descriptors[stage] = date, result, descriptor
        recordplay_require(dates['initial'] <= dates['review'] <= dates['final'], 'Producer order differs')
        recordplay_require(len({d['path'] for d in descriptors.values()}) == 3, 'Distinct producer results required')
        for stage, name in [('review', 'initial'), ('final', 'initial'), ('final', 'review')]:
            recordplay_require(recordplay_descriptor(results[stage].get(name + '_result'), bindings) == descriptors[name],
                               'Producer initial/review/final links differ')


def recordplay_source_record(descriptor, bindings):
    result_descriptor = recordplay_descriptor(descriptor, bindings)
    result = LOAD(result_descriptor['path'])
    recordplay_require(result.get('result') == 'PASS' and result.get('scope') == 'TARGET_BAG_RECORDPLAY_BOUNDED' and
        result.get('case') == CASES[0] and result.get('source_sha256') == SHA(__file__) and
        result.get('failures') == [] and result.get('blocked') == [] and result.get('communication_verified') is True and
        result.get('record_play') == 'RECORD_ONLY_PASS', 'Successful actual recording required before playback')
    evidence = result.get('evidence')
    recordplay_require(isinstance(evidence, dict) and bool(evidence), 'Recording execution evidence missing')
    for path, digest in evidence.items():
        recordplay_descriptor({'path': path, 'sha256': digest}, bindings)
    root = Path(result_descriptor['path']).parent
    for name in ('CLEANUP.json', 'NORMAL_STOP_INTENT.json', 'NORMAL_STOP_DISPATCH.json',
                 'COLLECTED_BAG.json', 'BAG_CONTENT.json', 'SOURCE_EXPECTATIONS.json', 'CHILD_EXITS.json'):
        recordplay_require(str(root / name) in evidence, 'Recording evidence member missing: ' + name)
    cleanup = LOAD(root / 'CLEANUP.json')
    recordplay_require(set(cleanup['sides']) == {'A', 'B'} and all(row.get('absent') is True and
        row.get('forced_signals') == [] and 'error' not in row for row in cleanup['sides'].values()),
        'Recording cleanup was not normal')
    collection, expected = LOAD(root / 'COLLECTED_BAG.json'), LOAD(root / 'SOURCE_EXPECTATIONS.json')
    recordplay_require(collection.get('run') == result['run'] == expected['record_run'] and
        collection.get('normal_closed_before_collection') is True and collection.get('files') == expected['files'],
        'Closed recording provenance differs')
    verified = ORACLE['verify_recorded_bag'](Path(collection['local_directory']), expected)
    recordplay_require(verified == LOAD(root / 'BAG_CONTENT.json'), 'Sealed recording content changed')
    return {'result': result_descriptor, 'run': result['run'], 'runtime_sha256': result['runtime_sha256'],
            'collection': collection, 'expected': expected}


def recordplay_prepare(selection_path):
    selection = LOAD(selection_path)
    recordplay_require(set(selection) == {'schema', 'case', 'package', 'runtime', 'readiness', 'external', 'identity',
        'routes', 'freeze', 'recording'} and type(selection['schema']) is int and selection['schema'] == 1 and
        selection['case'] in CASES, 'Only the two fixed communicating bag cases are implemented')
    bindings = {}
    selected = recordplay_descriptor({'path': str(Path(selection_path).resolve()), 'sha256': SHA(selection_path)}, bindings)
    receipts, descriptors = {}, {}
    for name in ('runtime', 'readiness', 'external', 'identity', 'routes', 'freeze'):
        descriptors[name] = recordplay_descriptor(selection[name], bindings)
        receipts[name] = LOAD(descriptors[name]['path'])
    recordplay_require(selection['external']['sha256'] == BAG.EXTERNAL_SHA and selection['identity']['sha256'] == BAG.IDENTITY_SHA,
                       'Exact admitted identity and external providers required')
    package = BASE.runtime_path(selection['package'], REPO / 'build')
    files = BASE.runtime_inventory(package)
    runtime = receipts['runtime']
    recordplay_require(runtime.get('scope') == 'BAG_RECORDPLAY_STATIC_DERIVATION_ONLY' and
        runtime.get('schema') == 4 and runtime.get('status') == 'STRUCTURAL_PASS_NOT_RUNTIME_VERIFIED' and
        runtime.get('package_inventory') == files and runtime.get('source_sha256') == COMPOSER_SHA and
        runtime.get('probe_sha256') == COMPOSER.PROBE_SHA and runtime.get('record_play') == 'NOT_RUN' and
        all(runtime.get(k) is False for k in ('eligible_for_deployment', 'target_imported', 'device_tested',
                                             'communication_verified', 'production_verified')), 'Static package provenance differs')
    manifest = LOAD(package / 'LAUNCHER_PACKAGE.json')
    recordplay_require(len(files) == 3234 and manifest['files'] == {k: v for k, v in files.items() if k != 'LAUNCHER_PACKAGE.json'} and
        runtime['launcher_manifest_sha256'] == files['LAUNCHER_PACKAGE.json'] and
        files.get(COMPOSER.MEMBER) == COMPOSER.PROBE_SHA, 'Package manifest/probe mismatch')
    recordplay_require(set(runtime['report_sha256']) == {'ALL_RECORDS.json', 'ELF_CLOSURE.json', 'DERIVATION.json',
                                                       'INPUTS.json', 'NO_MUTATION.json'}, 'Composition report set differs')
    for name, digest in runtime['report_sha256'].items():
        recordplay_descriptor({'path': str(package.parent / name), 'sha256': digest}, bindings)
    for path, digest in LOAD(package.parent / 'INPUTS.json').items():
        recordplay_descriptor({'path': path, 'sha256': digest}, bindings)
    sources = {path.relative_to(REPO).as_posix(): SHA(path) for path in SOURCE_PATHS}
    freeze = receipts['freeze']
    recordplay_require(freeze.get('schema') == 1 and freeze.get('scope') == 'BAG_RECORDPLAY_SOURCE_FREEZE' and
                       freeze.get('sources') == sources, 'Current full source freeze required')
    ready = receipts['readiness']
    recordplay_require(ready.get('schema') == 1 and ready.get('scope') == 'BAG_RECORDPLAY_POSTREVIEW_HOST_READY_TARGET_PENDING' and
        ready.get('source_freeze_sha256') == selection['freeze']['sha256'] and ready.get('runtime_sha256') == selection['runtime']['sha256'] and
        ready.get('external_sha256') == BAG.EXTERNAL_SHA and ready.get('identity_sha256') == BAG.IDENTITY_SHA and
        ready.get('owned_libpython') == {'member': 'lib/libpython3.12.so.1.0', 'sha256': '7b6e1fe170b5bcc8d5fa13edd0706cc6b351a6ec7e56d30b758e4471fef24b25'} and
        ready.get('external_provider_expansion') is False, 'Exact new operational readiness required')
    recordplay_gates(ready.get('gates'), sources, descriptors['runtime'], descriptors['freeze'], bindings)
    recording = None
    if selection['case'] == CASES[0]:
        recordplay_require(selection['recording'] is None, 'Record case cannot consume another recording')
    else:
        recording = recordplay_source_record(selection['recording'], bindings)
        recordplay_require(recording['runtime_sha256'] == selection['runtime']['sha256'], 'Replay runtime differs from record runtime')
    bindings.update({str(REPO / name): digest for name, digest in sources.items()})
    return {'schema': 1, 'scope': 'BAG_RECORDPLAY_PREPARED_NO_DEVICE_DISPATCH', 'case': selection['case'],
            'run': secrets.token_hex(8), 'package': str(package), 'files': files, 'inputs': selection,
            'bindings': bindings, 'recording': recording, 'selection_file': selected}


class BagRecordPlayRunner(BAG.BagCliRunner):
    def __init__(self, args):
        # Do not call the ABI constructor with an invented ABI case or plan.
        self.args, self.output, self.owns_output = args, recordplay_output(args.output, 'execution'), False
        self.sequence, self.owned, self.launch_pending, self.roots = 0, {}, {}, {}
        self.errors, self.blocked, self.stage, self.stage_deadline = [], [], 'INPUTS', None
        self.finished_fixtures, self.closed, self.maps_observed = set(), set(), {}
        self.normal_stop_attempted = self.normal_stop_confirmed = self.resume_attempted = False
        self.business_evidence = self.bag_receipt = None
        BAG.bag_selected_hdc(args.hdc)
        self.plan = LOAD(args.prepared)
        checked = recordplay_prepare(self.plan['selection_file']['path'])
        recordplay_require(set(self.plan) == set(checked) and
            all(self.plan[k] == value for k, value in checked.items() if k != 'run') and
            re.fullmatch('[0-9a-f]{16}', self.plan.get('run', '')), 'Prepared record/play selection changed')
        self.run, self.case = self.plan['run'], self.plan['case']
        self.recording = self.plan['recording']
        self.record_run = self.run if self.recording is None else self.recording['run']
        recordplay_require((self.run == self.record_run) == (self.case == CASES[0]), 'Fresh playback run required')
        self.package, self.files = Path(self.plan['package']), self.plan['files']
        self.external = LOAD(self.plan['inputs']['external']['path'])
        self.readiness = LOAD(self.plan['inputs']['readiness']['path'])
        self.prefix = {s: '/data/local/tmp/mdds_dds_' + self.run + '_' + s for s in 'AB'}
        self.roles = {'A': 'bag-publisher' if self.case == CASES[0] else 'bag-subscriber', 'B': None}
        self.expected_argv = {s: [self.prefix[s] + '/package/actionbag_python', *self.argv(s)[1:]] for s in 'AB'}
        identity = LOAD(self.plan['inputs']['identity']['path'])
        recordplay_require(identity['main_execution_result'] == 'PASS_IDENTITY_READINESS_ONLY', 'Existing identity not admitted')
        self.tokens = {r['side']: r['new_token'] for r in identity['boards']}
        recordplay_require(self.tokens == {'A': 672006291, 'B': 672042405}, 'Unexpected identity tokens')
        routes = LOAD(self.plan['inputs']['routes']['path'])
        self.observed = datetime.fromisoformat(routes['observed_utc'])
        recordplay_require(self.observed.utcoffset() is not None and
            0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() <= 300 and
            routes.get('cross_confirmed') is True and bool(routes.get('bindings')), 'Fresh paired routes required')
        for path, digest in routes['bindings'].items():
            recordplay_require(SHA(path) == digest, 'Paired route input changed')
        self.routes, self.udids = {}, {}
        for side in 'AB':
            row = routes['sides'][side]
            recordplay_require(row['target'] == NATIVE.TARGETS[side] and row['host_exit'] == 0 and row['remote_exit'] == 0,
                               'Paired query target/exit differs')
            for kind in ('stdout', 'udid'):
                data = Path(row[kind + '_file']).read_bytes()
                recordplay_require(hashlib.sha256(data).hexdigest() == row[kind + '_sha256'], 'Paired query export changed')
                if kind == 'stdout':
                    self.routes[side] = NATIVE.actionbag_parse_peer(data.decode('utf-8-sig'))
                else:
                    recordplay_require(1 <= len(data) <= 64 and not any(c in data for c in (b'\0', b'\r', b'\n')),
                                       'Invalid private UDID bytes')
                    self.udids[side] = data.decode()
            recordplay_require(len(self.udids[side].encode()) == self.routes[side]['udid_bytes'], 'UDID length differs')
        recordplay_require(self.udids['A'] != self.udids['B'] and all(self.routes[p]['local'] in self.routes[s]['remotes']
            for s, p in [('A', 'B'), ('B', 'A')]), 'Actual pair is not cross-confirmed')
        self.output.mkdir()
        self.owns_output = True
        self.write('PLAN.json', self.plan)

    def argv(self, side):
        return recordplay_argv(self.case, self.run, self.record_run, side, self.prefix[side])

    def configure(self, side):
        root = self.prefix[side]
        qos = self.output / (side + '.qos.yaml')
        qos.write_text('/bag_' + self.record_run + ':\n  history: keep_last\n  depth: 10\n  reliability: reliable\n  durability: volatile\n',
                       encoding='utf-8', newline='\n')
        self.send(side, qos, root + '/qos.yaml')
        if side == 'B' and self.case == CASES[1]:
            self.root_guard(side)
            self.shell(side, 'umask 077; mkdir -m 700 ' + root + '/input && mkdir -m 700 ' + root + '/input/bag')
            source = Path(self.recording['collection']['local_directory'])
            for name, digest in self.recording['collection']['files'].items():
                recordplay_require(SHA(source / name) == digest, 'Sealed replay input changed before upload')
                self.send(side, source / name, root + '/input/bag/' + name)
            self.write('PLAY_INPUT_PREFLIGHT.json', self.remote_bag_inventory(root + '/input/bag'))
        # Frozen implementation uses virtual self.argv and exact env-i; ROS_DISTRO stays absent.
        BAG.BagCliRunner.configure(self, side)

    def poll(self, side):
        root = self.prefix[side]
        raw = self.shell(side, 'test -f ' + root + '/stdout && test -f ' + root + '/stderr && test "$(stat -c %s ' +
            root + '/stdout)" -le 4194304 && test "$(stat -c %s ' + root + '/stderr)" -le 4194304 && cat ' +
            root + '/stdout && printf "\\nR25_STDERR\\n" && cat ' + root + '/stderr')
        text = raw.decode('utf-8', errors='strict')
        recordplay_require(not re.search(r'ROS_FAILURE|ROS_PYTHON_EXCEPTION|ROS_PYTHON_INITIALIZE_ERROR|INIT_FAILED|RUNTIME_CLOSE_FAILURE', text),
                           'Executed bag window reported failure')
        if side == 'A':
            recordplay_records(text, self.run, self.owned[side]['child']['pid'], self.roles[side])
        status = self.shell(side, 'if test -f ' + root + '/exit; then cat ' + root + '/exit; fi').decode().strip()
        return text, status

    def maps(self, side, label):
        recordplay_require(self.case in CASES, 'Only communicating bag maps are supported')
        ORDINARY.OrdinaryCliRunner.maps(self, side, label)
        if side == 'B' and label == 'business':
            paths = LOAD(self.output / (side + '.' + label + '.providers.json'))['sha256']
            recordplay_require(all(self.prefix[side] + '/package/lib/' + name in paths for name in
                ('librosbag2_storage_sqlite3.so', 'libsqlite3.so')), 'Live SQLite plugin/providers not mapped')

    def normal_recorder_stop(self, texts, business):
        recordplay_require(self.case == CASES[0] and not self.normal_stop_attempted and business is not None and
            business['complete'] and 'Recording...' in texts['B'] and "Subscribed to topic '/bag_" + self.run + "'" in texts['B'],
            'Recorder normal stop prerequisites incomplete or repeated')
        recordplay_require(all((self.output / (side + '.business.providers.json')).is_file() for side in 'AB'),
                           'Both live business map receipts required before normal TERM')
        self.guard('B')
        self.root_guard('B')
        intent = {'schema': 1, 'run': self.run, 'side': 'B', 'signal': 'TERM', 'owned': self.owned['B'],
            'utc': datetime.now(timezone.utc).isoformat(), 'publisher': business,
            'maps': {s: SHA(self.output / (s + '.business.providers.json')) for s in 'AB'},
            'source_contract': 'STOCK_RECORD_VERB_SIGTERM_EVENT_NORMAL_STOP', 'no_repeat': True}
        # Latch before persistence/dispatch: even an uncertain dispatch must never be retried.
        self.normal_stop_attempted = True
        with (self.output / 'NORMAL_STOP_INTENT.json').open('x', encoding='utf-8') as stream:
            json.dump(intent, stream, indent=2)
        self.guard('B')
        self.root_guard('B')
        self.shell('B', 'kill -TERM ' + str(self.owned['B']['child']['pid']))
        self.normal_stop_confirmed = True
        self.write('NORMAL_STOP_DISPATCH.json', {'run': self.run, 'side': 'B', 'signal': 'TERM',
            'dispatch_confirmed': True, 'command_sequence': self.sequence, 'intent_sha256': SHA(self.output / 'NORMAL_STOP_INTENT.json')})

    def resume_playback(self, business):
        recordplay_require(self.case == CASES[1] and not self.resume_attempted and business is not None and
            'BAG_REPLAY_READY' in business['single'] and not business['payloads'] and
            (self.output / 'B.business.providers.json').is_file(), 'Playback not ready or Resume repeated')
        self.guard('A')
        self.root_guard('A')
        self.resume_attempted = True
        data = ('R25_BAG_RESUME schema=1 run=' + self.run + ' side=A pid=' + str(self.owned['A']['child']['pid']) +
                ' record_run=' + self.record_run + '\n')
        self.write('RESUME_INTENT.json', {'run': self.run, 'contents': data, 'owned': self.owned['A']})
        target = self.prefix['A'] + '/control.BAG_RESUME'
        self.shell('A', 'umask 077; set -C; test ! -e ' + target + ' && test ! -L ' + target +
            ' && test ! -e ' + target + '.tmp && test ! -L ' + target + '.tmp && printf %s ' + shlex.quote(data) +
            ' > ' + target + '.tmp && chmod 600 ' + target + '.tmp && mv ' + target + '.tmp ' + target)
        self.write('releases/A-control.BAG_RESUME.json', {'run': self.run, 'contents': data,
            'owned': self.owned['A'], 'utc': datetime.now(timezone.utc).isoformat(), 'monotonic': time.monotonic()})

    def workflow(self):
        first = time.monotonic()
        self.stage, self.stage_deadline = 'LAUNCH', first + 60
        for side in 'AB':
            self.launch(side)
        self.stage = 'LISTENER'
        while True:
            texts, statuses = {}, {}
            for side in 'AB':
                texts[side], statuses[side] = self.poll(side)
            recordplay_require(time.monotonic() < self.stage_deadline and not any(statuses.values()),
                               'Listener deadline/early child exit')
            if all(self.ready(side, texts[side], 'LISTENER') for side in 'AB'):
                break
            time.sleep(0.1)
        for side in 'AB':
            self.maps(side, 'listener')
        origin = time.monotonic()
        recordplay_require(origin < first + 60, 'Listener maps exceeded deadline')
        self.stage_deadline = min(first + 60, origin + 35)
        self.write('CLOCK.json', {'first_dispatch': first, 'business_origin': origin, 'business_deadline': origin + 35,
            'normal_close_release_deadline': origin + 50, 'origin': 'BEFORE_FIRST_LISTENER_RELEASE'})
        for side in 'AB':
            self.publish(side, 'control.LISTENER_READY', 'ROS_RELEASE run=' + self.run + ' side=' + side + ' stage=LISTENER_READY\n')
        self.stage, self.stage_deadline = 'BUSINESS', origin + 35
        while True:
            texts, statuses = {}, {}
            for side in 'AB':
                texts[side], statuses[side] = self.poll(side)
            recordplay_require(time.monotonic() < self.stage_deadline and not any(statuses.values()), 'Business deadline/early child exit')
            business = recordplay_business(texts['A'], self.run, self.record_run, self.owned['A']['child']['pid'], self.roles['A'])
            if self.case == CASES[0]:
                recordplay_require(self.normal_stop_attempted or not self.ready('B', texts['B'], 'CLOSE'),
                                   'Recorder closed before owned normal TERM')
                if (business and business['complete'] and not self.normal_stop_attempted and 'Recording...' in texts['B'] and
                        "Subscribed to topic '/bag_" + self.run + "'" in texts['B']):
                    for side in 'AB':
                        self.maps(side, 'business')
                    self.normal_recorder_stop(texts, business)
            elif business and 'BAG_REPLAY_READY' in business['single'] and not self.resume_attempted:
                for side in 'AB':
                    self.maps(side, 'business')
                self.resume_playback(business)
            if business and business['complete'] and self.ready('B', texts['B'], 'CLOSE'):
                self.business_evidence = business
                break
            time.sleep(0.1)
        self.stage, self.stage_deadline = 'CLOSE', origin + 50
        self.finish_fixture('A', 'NORMAL_COMPLETION')
        while True:
            ready = {}
            for side in 'AB':
                text, status = self.poll(side)
                recordplay_require(not status, 'Child exited before paired normal close')
                ready[side] = self.ready(side, text, 'CLOSE')
            recordplay_require(time.monotonic() < self.stage_deadline, 'Absolute50s close deadline exceeded')
            if all(ready.values()):
                break
            time.sleep(0.1)
        for side in 'AB':
            self.maps(side, 'close')
        for side in 'AB':
            self.publish(side, 'control.RUNTIME_CLOSE', 'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side +
                ' pid=' + str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=NORMAL_COMPLETION\n')
            self.closed.add(side)
        recordplay_require(time.monotonic() < self.stage_deadline, 'Close release exceeded absolute50s deadline')
        self.stage, self.stage_deadline = 'EXIT', time.monotonic() + 20
        self.wait_exit(20)

    def analyze(self, texts):
        pids = {side: self.owned[side]['child']['pid'] for side in 'AB'}
        business = recordplay_business(texts['A'], self.run, self.record_run, pids['A'], self.roles['A'], final=True)
        diagnostics = {}
        for side in 'AB':
            text = texts[side]
            recordplay_require(NATIVE.actionbag_fields(text, 'ROS_PYTHON_INTERPRETER_RETURN') ==
                [{'execution_rc': '0', 'finalize_rc': '0'}] and NATIVE.actionbag_fields(text, 'ROS_PYTHON_FINAL') ==
                [{'finalized': '1', 'python_rc': '0', 'teardown_observed': '1', 'teardown_rc': '0', 'communication': 'NOT_ASSERTED'}] and
                NATIVE.actionbag_fields(text, 'ROS_IDENTITY_FINAL') == [{'restored': '1', 'business_rc': '0'}],
                'Native/interpreter/identity finalization differs')
            stdout = (self.output / (side + '.raw.stdout.log')).read_text(encoding='utf-8')
            stderr = (self.output / (side + '.raw.stderr.log')).read_text(encoding='utf-8')
            diagnostics[side] = recordplay_errors(stdout, stderr, self.run, side, pids[side], self.files)
        native = NATIVE.actionbag_native_capture(texts, self.run, pids)
        locator = REPO / 'scripts/public_socket/analyze_v1_locators.py'
        recordplay_require(SHA(locator) == '582971f7d631ddffde9ec59c2e677c0ad756d2271d3ce1df6204e35ed0da93ff',
                           'Selected V1 oracle changed')
        module = runpy.run_path(str(locator))
        self.write('window.json', {'run': self.run})
        expected = {s: module['derive']({'namespace': 'mdds.r25.action', 'domain': 201, 'udid': self.udids[s]},
                    'AB'.index(s) + 1) for s in 'AB'}
        locators = module['analyze'](self.output, expected)
        if self.case == CASES[0]:
            recordplay_require(self.normal_stop_attempted and self.normal_stop_confirmed and not self.resume_attempted,
                               'Recorder normal TERM outcome unconfirmed')
            expectations = recordplay_expectations(business, self.run, self.package, self.files)
            self.write('SOURCE_EXPECTATIONS.json', expectations)
        else:
            recordplay_require(self.resume_attempted and not self.normal_stop_attempted, 'Playback control history differs')
            actual = [{k: row[k] for k in ('sequence', 'text', 'cdr_base64', 'cdr_sha256', 'cdr_bytes')}
                      for row in business['payloads']]
            expected_cdr = [{k: row[k] for k in ('sequence', 'text', 'cdr_base64', 'cdr_sha256', 'cdr_bytes')}
                            for row in self.recording['expected']['published']]
            recordplay_require(actual == expected_cdr, 'Replayed CDR differs from actual original publications')
            self.write('REPLAY_CONTENT.json', {'status': 'PASS_TEN_ORIGINAL_CDR_IN_ORDER', 'received': actual,
                'source_record': self.recording['result'], 'natural_cli_return': True, 'signals': 0})
        self.write('ANALYSIS.json', {'scope': 'BAG_RECORDPLAY_BOUNDED_CONTENT_AND_NATIVE_PATH', 'business': 'PASS',
            'native': native, 'v1_spdp': locators, 'diagnostics': diagnostics, 'sdk_epoch': 'NOT_PROVEN',
            'dedicated_SEDP': 'NOT_QUALIFIED', 'execution_qualification': 'PENDING_CLEANUP_AND_CLOSED_INPUT_CHECK'})

    def remote_bag_inventory(self, root):
        recordplay_require(root in (self.prefix['B'] + '/bag', self.prefix['B'] + '/input/bag'), 'Unowned bag directory')
        self.root_guard('B')
        raw = self.shell('B', 'test -d ' + root + ' && test ! -L ' + root + ' && cd ' + root +
            ' && for f in * .[!.]* ..?*; do if test -e "$f" || test -L "$f"; then '
            'test -f "$f" && test ! -L "$f" || exit 92; printf "FILE "; stat -c "%s:%h" "$f" || exit 93; '
            'sha256sum "$f" || exit 94; fi; done')
        lines = raw.decode('utf-8').splitlines()
        recordplay_require(len(lines) == 4, 'Bag must contain exactly two regular files and no sidecars/directories')
        inventory = {}
        for index in (0, 2):
            size = re.fullmatch(r'FILE ([0-9]+):1', lines[index])
            row = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9_][A-Za-z0-9_.-]*)', lines[index + 1])
            recordplay_require(size and row and row[2] not in inventory, 'Bag inventory kind/link/hash/name differs')
            inventory[row[2]] = {'bytes': int(size[1]), 'sha256': row[1]}
        recordplay_require('metadata.yaml' in inventory and len([n for n in inventory if re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.db3', n)]) == 1 and
            all(0 < row['bytes'] <= (1048576 if name == 'metadata.yaml' else 67108864) for name, row in inventory.items()),
            'Bag filenames/size outside bounded SQLite contract')
        return inventory

    def closed_bag(self):
        cleanup = LOAD(self.output / 'CLEANUP.json')
        recordplay_require(not self.errors and not self.blocked and set(cleanup['sides']) == {'A', 'B'} and
            all(r.get('absent') is True and r.get('forced_signals') == [] and 'error' not in r for r in cleanup['sides'].values()),
            'Proven normal process absence required before bag collection/check')
        self.stage, self.stage_deadline = 'POSTCLOSE_BAG', time.monotonic() + 60
        root = self.prefix['B'] + ('/bag' if self.case == CASES[0] else '/input/bag')
        before = self.remote_bag_inventory(root)
        if self.case == CASES[1]:
            expected = self.recording['collection']['files']
            recordplay_require(before == LOAD(self.output / 'PLAY_INPUT_PREFLIGHT.json') and
                {name: row['sha256'] for name, row in before.items()} == expected, 'Replay input changed on B')
            source = Path(self.recording['collection']['local_directory'])
            recordplay_require({name: SHA(source / name) for name in expected} == expected, 'Original sealed input changed')
            self.write('PLAY_INPUT_POSTFLIGHT.json', before)
            return
        local = self.output / 'closed-bag'
        local.mkdir()
        self.write('BAG_REMOTE_BEFORE.json', before)
        for name, row in before.items():
            out = self.command('B', ['file', 'recv', root + '/' + name, str(local / name)], 30)
            recordplay_require(b'transfer finish' in out.lower(), 'Bag receive completion missing')
            recordplay_require((local / name).stat().st_size == row['bytes'] and SHA(local / name) == row['sha256'],
                               'Received bag size/hash differs')
        after = self.remote_bag_inventory(root)
        self.write('BAG_REMOTE_AFTER.json', after)
        recordplay_require(after == before, 'Closed remote bag changed across collection')
        expected = LOAD(self.output / 'SOURCE_EXPECTATIONS.json')
        expected['files'] = {name: row['sha256'] for name, row in before.items()}
        self.write('SOURCE_EXPECTATIONS.json', expected)
        content = ORACLE['verify_recorded_bag'](local, expected)
        self.write('BAG_CONTENT.json', content)
        self.bag_receipt = {'run': self.run, 'local_directory': str(local), 'remote_directory': root,
            'files': expected['files'], 'normal_closed_before_collection': True,
            'normal_stop_intent_sha256': SHA(self.output / 'NORMAL_STOP_INTENT.json'),
            'normal_stop_dispatch_sha256': SHA(self.output / 'NORMAL_STOP_DISPATCH.json'),
            'cleanup_sha256': SHA(self.output / 'CLEANUP.json'), 'content_oracle_sha256': ORACLE_SHA}
        self.write('COLLECTED_BAG.json', self.bag_receipt)

    def cleanup_observe(self, side, role='child'):
        """An exit during proc capture requires a separate absence observation."""
        if self.stage_deadline is not None and self.stage_deadline - time.monotonic() < 1:
            raise OSError('Cleanup observation budget exhausted; no new RPC')
        try:
            return self.guard(side, role=role, absent_ok=True)
        except OSError as error:
            if str(error) not in ('Remote setup/capture command exit 1',
                                  'Owned process identity capture incomplete'):
                raise
            pid = self.owned[side][role]['pid']
            if type(pid) is not int or pid <= 1:
                raise ValueError('Invalid owned PID')
            if self.stage_deadline is not None and self.stage_deadline - time.monotonic() < 1:
                raise OSError('Cleanup absence confirmation budget exhausted; no new RPC') from error
            raw = self.shell(side, 'if test -d /proc/' + str(pid) +
                '; then printf PRESENT; else printf ABSENT; fi').strip()
            if raw != b'ABSENT':
                raise
            self.write('cleanup-exit-race-%s-%s.json' % (side, role),
                {'pid': pid, 'capture_error': str(error), 'separate_absence_confirmed': True})
            return False

    def cleanup(self, failed):
        result = {'directories_retained': True, 'identity_mutations': 0, 'sides': {}, 'pending': self.launch_pending}
        for side in self.owned:
            status = {'forced_signals': [], 'absent': False}
            attempted_stop = side == 'B' and self.normal_stop_attempted
            ambiguous_stop = attempted_stop and not self.normal_stop_confirmed
            try:
                self.stage_deadline = time.monotonic() + 10
                if 'child' not in self.owned[side]:
                    raise OSError('Partial launch child identity unknown; no signal permitted')
                if ambiguous_stop:
                    status['normal_stop_dispatch'] = 'AMBIGUOUS_NO_REPEAT_NO_CONTROL'
                    self.blocked.append('NORMAL_STOP: dispatch ambiguous; B retained/read-only; playback prohibited')
                if failed and not ambiguous_stop and self.cleanup_observe(side):
                    for name in ('control.PROBE_FINISH', 'control.RUNTIME_CLOSE'):
                        if name == 'control.PROBE_FINISH' and (not self.roles[side] or side in self.finished_fixtures):
                            continue
                        exists = self.shell(side, 'if test -e ' + self.prefix[side] + '/' + name + ' || test -L ' +
                            self.prefix[side] + '/' + name + '; then printf 1; else printf 0; fi').decode().strip()
                        if exists == '0':
                            data = ('R25_PROBE_FINISH schema=1 run=' + self.run + ' side=' + side + ' pid=' +
                                str(self.owned[side]['child']['pid']) + ' reason=WINDOW_FAILED\n') if name == 'control.PROBE_FINISH' else (
                                'ROS_RUNTIME_CLOSE_RELEASE schema=1 run=' + self.run + ' side=' + side + ' pid=' +
                                str(self.owned[side]['child']['pid']) + ' stage=RUNTIME_CLOSE reason=WINDOW_FAILED\n')
                            published = self.publish(side, name, data, cleanup_absent_ok=True)
                            if name == 'control.PROBE_FINISH' and published:
                                self.finished_fixtures.add(side)
                            if not published:
                                break
                # A normal-stop attempt permanently removes the TERM/KILL retry path for B.
                actions = [(None, 10)] if attempted_stop else [(None, 10), ('TERM', 5), ('KILL', 2)]
                for signal, budget in actions:
                    self.stage_deadline = time.monotonic() + budget
                    if not self.cleanup_observe(side):
                        break
                    if signal:
                        self.guard(side)
                        self.shell(side, 'kill -' + signal + ' ' + str(self.owned[side]['child']['pid']))
                        status['forced_signals'].append(signal)
                    # Do not dispatch a new proc RPC into the subsecond tail.
                    # Actual RPC/identity failures still block; no timeout retry.
                    while self.stage_deadline - time.monotonic() >= 1 and self.cleanup_observe(side):
                        time.sleep(0.1)
                self.stage_deadline = time.monotonic() + 3
                child_absent = not self.cleanup_observe(side)
                parent_present = self.cleanup_observe(side, role='supervisor')
                while parent_present and self.stage_deadline - time.monotonic() >= 1:
                    time.sleep(0.1)
                    if self.stage_deadline - time.monotonic() < 1:
                        break
                    parent_present = self.cleanup_observe(side, role='supervisor')
                status['absent'] = child_absent and not parent_present
                if not status['absent']:
                    raise OSError('Owned process absence unconfirmed; no normal-stop retry permitted' if attempted_stop else
                                  'Owned process absence unconfirmed')
                self.stage_deadline = None
                self.verify_files(side)
                providers = self.maps_observed.get(side, {})
                for start in range(0, len(providers), 20):
                    paths = sorted(providers)[start:start + 20]
                    raw = self.shell(side, 'sha256sum ' + ' '.join(shlex.quote(p) for p in paths)).decode()
                    got = {line.split(maxsplit=1)[1].strip(): line.split(maxsplit=1)[0] for line in raw.splitlines()}
                    if got != {p: providers[p] for p in paths}:
                        raise OSError('Observed provider postflight drift')
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
                self.blocked.append('LAUNCH ' + side + ': unknown outcome retained; no redispatch/signal')
        self.write('CLEANUP.json', result)

    def execute(self):
        try:
            release = LOAD(self.args.release)
            recordplay_require(release == {'schema': 1, 'scope': 'MAIN_ONE_SHOT_BAG_RECORDPLAY_RELEASE', 'run': self.run,
                'case': self.case, 'prepared_sha256': SHA(self.args.prepared), 'readiness_sha256': self.plan['inputs']['readiness']['sha256'],
                'runner_sha256': SHA(__file__), 'record_run': self.record_run}, 'Exact Main one-shot release required')
            recordplay_require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240, 'Main release route age must be under240s')
            for path, digest in self.plan['bindings'].items():
                recordplay_require(SHA(path) == digest, 'Prepared input changed before dispatch')
            recordplay_require(0 <= (datetime.now(timezone.utc) - self.observed).total_seconds() < 240,
                               'Route admission expired while checking input bindings')
            with (self.args.prepared.parent / (self.run + '.CONSUMED.json')).open('x', encoding='utf-8') as stream:
                json.dump({'release_sha256': SHA(self.args.release), 'output': str(self.output), 'utc': datetime.now(timezone.utc).isoformat()}, stream)
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
            if not self.errors and not self.blocked:
                try:
                    self.closed_bag()
                except ValueError as error:
                    self.errors.append('POSTCLOSE_BAG: ' + str(error))
                except (Exception, KeyboardInterrupt) as error:
                    self.blocked.append('POSTCLOSE_BAG: ' + repr(error))
            elif self.owned:
                try:
                    self.collect()
                except (OSError, ValueError) as error:
                    self.blocked.append('FAILURE_CAPTURE: ' + str(error))
            self.stage_deadline = None
            passed = not self.errors and not self.blocked
            evidence = {str(path): SHA(path) for path in sorted(self.output.rglob('*')) if path.is_file()}
            self.write('RESULT.json', {'run': self.run, 'case': self.case, 'record_run': self.record_run,
                'result': 'FAIL' if self.errors else 'BLOCKED' if self.blocked else 'PASS', 'failures': self.errors, 'blocked': self.blocked,
                'scope': 'TARGET_BAG_RECORDPLAY_BOUNDED', 'communication_verified': passed,
                'record_play': ('RECORD_ONLY_PASS' if self.case == CASES[0] else 'PLAYBACK_PASS') if passed else 'NOT_VERIFIED',
                'source_sha256': SHA(__file__), 'runtime_sha256': self.plan['inputs']['runtime']['sha256'],
                'inherited_runner_sha256': BAG_SHA, 'oracle_sha256': ORACLE_SHA, 'evidence': evidence,
                'identity_mutations': 0, 'directories_retained': True, 'sdk_epoch': 'NOT_PROVEN',
                'dedicated_SEDP': 'NOT_QUALIFIED', 'utc': datetime.now(timezone.utc).isoformat()})
        return 0 if passed else 1


def recordplay_main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('prepare', 'run'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--prepared', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--hdc', type=Path)
    args = parser.parse_args()
    if args.mode == 'prepare':
        if args.inputs is None:
            parser.error('Prepare requires inputs and a fresh output')
        output = recordplay_output(args.output, 'prepared')
        plan = recordplay_prepare(args.inputs)
        output.mkdir()
        with (output / 'PLAN.json').open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(plan, indent=2) + '\n')
        return 0
    if args.prepared is None or args.release is None or args.hdc is None:
        parser.error('Run requires prepared, release and selected HDC')
    return BagRecordPlayRunner(args).execute()


if __name__ == '__main__':
    raise SystemExit(recordplay_main())
