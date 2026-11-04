"""Synthetic host controls only; no HDC, native loading or device qualification."""
import argparse
import ast
import base64
from contextlib import ExitStack, redirect_stdout
import copy
from datetime import datetime, timezone, timedelta
import importlib.util
import io
import json
import os
from pathlib import Path
import secrets
import shlex
import sqlite3
import stat
import sys
import time
from types import SimpleNamespace as NS, ModuleType
import unittest
from unittest.mock import Mock, patch

import yaml
import run_serialized_bag as H
import serialized_bag_oracle as O
from test_parameter_native import encode

RECORD, PLAY = '0123456789abcdef', 'fedcba9876543210'
QOS = dict(history='unknown', depth=0, reliability='reliable', durability='volatile', liveliness='automatic',
           avoid_ros_namespace_conventions=False, deadline=dict(sec=0, nsec=0), lifespan=dict(sec=0, nsec=0),
           liveliness_lease_duration=dict(sec=0, nsec=0))


class NoAliasDumper(yaml.SafeDumper):
    def ignore_aliases(self, data):
        return True


def dump(value):
    return yaml.dump(value, Dumper=NoAliasDumper)


def raw(lane, index, suffix=b'fixture!', run=RECORD):
    value = O.expected_value(lane, index, run)
    return encode('s', value['data']) if lane.endswith('string') else encode('ParameterEvent', value) + suffix


def graph(case, run, record_run):
    contract = O.graph_contract(case, run, record_run)
    owner = '/r35bagrec_' + run if case == H.CASES[0] else '/r35bagplayer_' + run
    return dict(nodes=sorted(contract), entities=contract, data={l: [dict(owner=owner, type=O.TYPES[l],
        kind='subscription' if case == H.CASES[0] else 'publisher', gid='1' * 32, qos=copy.deepcopy(QOS), type_hash='')] for l in H.LANES})


def fixture_records(case=H.CASES[0], run=RECORD, record_run=RECORD, changed=False):
    records = []

    def add(event, **fields):
        records.append(dict(schema=1, scope=H.SCOPE, case=case, run=run, record_run=record_run, side='A', pid=22,
            role='bag-publisher' if case == H.CASES[0] else 'bag-subscriber', seq=len(records) + 1,
            monotonic_ns=len(records) + 1, event=event, **fields))
        return len(records)

    add('TYPE_SUPPORT', origins={}, capsules={'event': 'PyCapsule', 'string': 'PyCapsule'})
    add('DEFINITIONS', definitions={})
    add('NODE_READY', name=('/r35bagpub_' if case == H.CASES[0] else '/r35bagsub_') + run)
    good = graph(case, run, record_run)
    add('DISCOVERY', snapshot=good, changed_snapshots=1)
    add('READY', graph=good, counts={l: [1] if case == H.CASES[0] else [0, 0] for l in H.LANES},
        slots_empty=True, requested_qos=dict(history=1, depth=10, reliability=1, durability=2), source_endpoints=None)
    if case != H.CASES[0]:
        add('RESUME_ACCEPTED', bag_seal_sha256='2' * 64)
        add('RESUME_CALLED', service='/r35bagplayer_' + run + '/resume')
        add('RESUME_RETURNED', response_type='rosbag2_interfaces/srv/Resume_Response', fields={},
            future_done=True, future_cancelled=False, future_raised=False)
    for index in (1, 2):
        for lane in H.LANES:
            data = raw(lane, index, b'changed!' if changed else b'fixture!', record_run)
            ref = add('BUFFER', lane=lane, index=index if case == H.CASES[0] else None,
                origin='LOCAL_SERIALIZE_MESSAGE' if case == H.CASES[0] else 'RAW_SUBSCRIPTION',
                cdr_base64=base64.b64encode(data).decode(), cdr_bytes=len(data), cdr_sha256=O.digest(data))
            add('NATIVE_DECODE', buffer_seq=ref, lane=lane, index=index, value=O.expected_value(lane, index, record_run))
            if case == H.CASES[0]:
                add('PUBLISH_RETURN', lane=lane, index=index, buffer_seq=ref,
                    input_kind='bytes' if lane.startswith('serialized_') else 'message')
            else:
                add('RAW_RECEIVE', lane=lane, index=index, buffer_seq=ref, value=O.expected_value(lane, index, record_run), metadata={n: None for n in O.META})
                add('TYPED_RECEIVE', lane=lane, index=index, value=O.expected_value(lane, index, record_run), metadata={n: None for n in O.META})
    if case == H.CASES[0]:
        for lane in H.LANES:
            add('ACKED', lane=lane, returned=True, timeout_ns=1000000)
        add('RECORD_COMPLETE', published=8, acknowledged=4, settle_seconds=0.25)
    else:
        add('PLAY_COMPLETE', callbacks=16, counters={l: [2, 2] for l in H.LANES}, quiet_seconds=0.25)
    add('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
    add('OBSERVATION_END', callbacks=0 if case == H.CASES[0] else 16, published=8 if case == H.CASES[0] else 0,
        last_spin_ns=len(records), cutoff_ns=len(records) + 1)
    add('NODE_DESTROY_RETURN', returned=True)
    add('SHUTDOWN_RETURN', returned=True)
    add('FINAL', success=True, node_created=True)
    return records


def bag_fixture(root, package, changed=False, typed_diff=False, timestamps=None):
    root.mkdir()
    expected = dict(schema=1, record_run=RECORD, topics={l: dict(type_description_hash='', offered_qos_profiles=[QOS]) for l in H.LANES},
        definitions={typ: dict(value, type_description_hash='') for typ, value in O.definitions(package).items()}, published={})
    connection = sqlite3.connect(root / 'fixture.db3')
    for table, shape in O.SQL._SCHEMA.items():
        connection.execute('CREATE TABLE ' + table + '(' + ','.join(n + ' ' + t + (' PRIMARY KEY' if pk else ' NOT NULL' if nn else '') for n, t, nn, pk in shape) + ')')
    connection.execute('CREATE INDEX timestamp_idx ON messages(timestamp ASC)')
    connection.execute('INSERT INTO schema VALUES(4,?)', ('',))
    for tid, lane in enumerate(H.LANES, 1):
        connection.execute('INSERT INTO topics VALUES(?,?,?,?,?,?)', (tid, O.topic(RECORD, lane), O.TYPES[lane], 'cdr', dump([QOS]), ''))
    for did, (typ, info) in enumerate(expected['definitions'].items(), 1):
        connection.execute('INSERT INTO message_definitions VALUES(?,?,?,?,?)', (did, typ, info['encoding'], info['encoded_message_definition'], ''))
    timestamps = timestamps or [100, 200, 50, 50, 50, 50, 50, 50]
    pos = 0
    for index in (1, 2):
        for tid, lane in enumerate(H.LANES, 1):
            data = raw(lane, index)
            expected['published'][lane + ':' + str(index)] = dict(cdr_bytes=len(data), cdr_sha256=O.digest(data), cdr_base64=base64.b64encode(data).decode())
            stored = raw(lane, index, b'changed!' if changed and lane.startswith('serialized_') else b'fixture!')
            if typed_diff and lane == 'typed_event':
                stored = raw(lane, index, b'')
            connection.execute('INSERT INTO messages(topic_id,timestamp,data) VALUES(?,?,?)', (tid, timestamps[pos], stored))
            pos += 1
    start, duration = O.writer_timing(timestamps)
    metadata = dict(version=9, storage_identifier='sqlite3', relative_file_paths=['fixture.db3'],
        files=[dict(path='fixture.db3', starting_time={'nanoseconds_since_epoch': start}, duration={'nanoseconds': duration}, message_count=8)],
        starting_time={'nanoseconds_since_epoch': start}, duration={'nanoseconds': duration}, message_count=8,
        topics_with_message_count=[dict(topic_metadata=dict(name=O.topic(RECORD, l), type=O.TYPES[l], serialization_format='cdr',
            offered_qos_profiles=[QOS], type_description_hash=''), message_count=2) for l in reversed(H.LANES)],
        compression_format='', compression_mode='', custom_data=None, ros_distro='')
    connection.execute('INSERT INTO metadata VALUES(1,9,?)', (dump(metadata),))
    connection.commit()
    connection.close()
    (root / 'metadata.yaml').write_text(dump({'rosbag2_bagfile_information': metadata}), encoding='utf-8')
    expected['files'] = {p.name: H.sha(p) for p in root.iterdir()}
    return expected


class PublicGraphLeaves:
    """Synthetic public ROS leaves; graph/wait/tick/endpoint bodies stay real."""
    class NodeNameNonExistentError(RuntimeError):
        pass

    methods = {'publishers': 'get_publisher_names_and_types_by_node',
               'subscriptions': 'get_subscriber_names_and_types_by_node',
               'services': 'get_service_names_and_types_by_node', 'clients': 'get_client_names_and_types_by_node'}

    def __init__(self, probe, node, states, step=0.01):
        self.probe, self.states, self.spins, self.step = probe, states, 0, step
        self.clock, self.calls, self.current = [100.0], [], states[0]
        self.good = graph(probe.args.case, probe.args.run, probe.args.record_run)
        for kind, method in self.methods.items():
            setattr(node, method, lambda name, namespace, kind=kind: self.entities(kind, name, namespace))
        node.get_node_names_and_namespaces = lambda: [(n.rsplit('/', 1)[1], n.rsplit('/', 1)[0] or '/') for n in self.snapshot()['nodes']]
        node.get_subscriptions_info_by_topic = lambda topic: self.infos(topic, False)
        node.get_publishers_info_by_topic = lambda topic: self.infos(topic, True)
        self.ros = ModuleType('rclpy'); self.ros.ok = lambda: not self.current.get('context_lost', False)
        self.ros.spin_once = self.spin
        self.node_module = ModuleType('rclpy.node'); self.node_module.NodeNameNonExistentError = self.NodeNameNonExistentError
        contract = json.dumps(O.graph_contract(probe.args.case, probe.args.run, probe.args.record_run)).encode()
        probe.contract_path = NS(read_bytes=lambda: contract)
        args = copy.copy(probe.args); args.contract_sha256 = O.digest(contract); probe.args = args

    def snapshot(self):
        return self.current.get('snapshot', self.good)

    def spin(self, node, timeout_sec):
        if timeout_sec != 0.01:
            raise AssertionError('Original bounded spin required')
        self.current = self.states[min(self.spins, len(self.states) - 1)]
        self.spins += 1; self.clock[0] += self.step

    def entities(self, kind, name, namespace):
        self.calls.append((self.spins, kind, name, namespace))
        if namespace != '/':
            raise AssertionError('Exact expected node namespace required')
        if self.current.get('missing') == kind and name == self.current.get('missing_name', self.probe.remote_name):
            raise self.current.get('raised', self.NodeNameNonExistentError('synthetic public missing node'))
        return [(topic, [typ]) for topic, typ in self.snapshot()['entities']['/' + name][kind].items()]

    def match_count(self, lane):
        return self.current.get('counts', {}).get(lane, 1)

    def infos(self, topic, publishers):
        lane = topic.rsplit('/', 1)[1]
        if self.probe.recording and publishers:
            values = [dict(self.good['data'][lane][0], owner='/' + self.probe.node_name, kind='publisher')]
            values = self.current.get('sources', {}).get(lane, values)
        else:
            values = self.snapshot()['data'][lane]
        return [self.info(item) for item in values]

    @staticmethod
    def info(item):
        value = item['qos']; qos = NS(depth=value['depth'], avoid_ros_namespace_conventions=value['avoid_ros_namespace_conventions'])
        for name in ('history', 'reliability', 'durability', 'liveliness'):
            setattr(qos, name, NS(name=value[name].upper()))
        for name in ('deadline', 'lifespan', 'liveliness_lease_duration'):
            setattr(qos, name, NS(nanoseconds=value[name]['sec'] * 1000000000 + value[name]['nsec']))
        namespace, name = item['owner'].rsplit('/', 1)
        return NS(node_name=name, node_namespace=namespace or '/', topic_type=item['type'],
                  endpoint_type=1 if item['kind'] == 'publisher' else 2, endpoint_gid=bytes.fromhex(item['gid']),
                  topic_type_hash=NS(version=0, value=bytes(32)), qos_profile=qos)


class Fixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.no_run = patch.object(H.subprocess, 'run', side_effect=AssertionError('Host fixture forbids every subprocess/device command'))
        cls.no_popen = patch.object(H.subprocess, 'Popen', side_effect=AssertionError('Host fixture forbids every subprocess/device command'))
        cls.no_run.start(); cls.addClassCleanup(cls.no_run.stop)
        cls.no_popen.start(); cls.addClassCleanup(cls.no_popen.stop)
        cls.root = H.EVIDENCE / ('host-fixture-' + secrets.token_hex(8))
        cls.root.mkdir()
        (cls.root / 'HOST_ONLY.txt').write_text('Synthetic qualification/release/process fixtures. No device admission or independent testing evidence. Retained for reproducibility.', encoding='utf-8')
        selected = os.environ.get('R35_HOST_TEST_DERIVATION')
        cls.derivation = Path(selected) if selected else H.EVIDENCE / ('package-' + secrets.token_hex(8)) / 'DERIVATION.json'
        if not selected:
            H.compose(cls.derivation.parent)
        # Sharing immutable package bytes saves disk, never admission/source hashes.
        H.checked_package(cls.derivation, {})
        cls.package = cls.derivation.parent / 'package'
        spec = importlib.util.spec_from_file_location('r35_probe_' + cls.root.name.replace('-', '_'), cls.package / H.MEMBER)
        cls.probe_module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.probe_module)
        previous = H.read(H.ROOT / 'build/r34-serialized-impact/prepared-ebb84d81828531e0/PLAN.json')
        cls.original = previous

    def directory(self, label):
        root = self.root / (label + '-' + secrets.token_hex(4))
        root.mkdir()
        return root

    def construct(self, case=H.CASES[0], recording=None, previous=None):
        root = self.directory('constructor')
        routes = H.read(self.original['routes'])
        routes['observed_utc'] = datetime.now(timezone.utc).isoformat()
        H.exclusive(root / 'ROUTES.json', routes)
        H.exclusive(root / 'SOURCES.json', H.source_inventory())
        H.exclusive(root / 'QUALIFICATION.json', dict(schema=1, scope=H.SCOPE, status='PASS_FINAL_HOST', device_tested=False,
            package=H.descriptor(self.derivation), source_inventory=H.descriptor(root / 'SOURCES.json')))
        selection = dict(schema=1, case=case, package=H.descriptor(self.derivation), qualification=H.descriptor(root / 'QUALIFICATION.json'),
            routes=H.descriptor(root / 'ROUTES.json'), identity=H.descriptor(self.original['identity']), external=H.descriptor(self.original['external']),
            recording=recording, previous_play_audit=previous)
        H.exclusive(root / 'SELECTION.json', selection)
        plan, *_ = H.prepare(root / 'SELECTION.json', RECORD if case == H.CASES[0] else PLAY)
        H.exclusive(root / 'PLAN.json', plan)
        H.exclusive(root / 'OPS.json', dict(scope='R35_OPERATIONAL_REVIEW', status='PASS', case=case,
            prepared_sha256=H.sha(root / 'PLAN.json'), limits=H.LIMITS))
        H.exclusive(root / 'RELEASE.json', H.release_for(plan, root / 'PLAN.json', H.descriptor(root / 'OPS.json')))
        args = NS(prepared=root / 'PLAN.json', release=root / 'RELEASE.json', output=H.EVIDENCE / ('execution-' + secrets.token_hex(8)), hdc=Path(H.NATIVE.HDCPATH))
        return H.SerializedBagRunner(args), args, selection

    def probe(self, case=H.CASES[0]):
        run = RECORD if case == H.CASES[0] else PLAY
        root = Path('/data/local/tmp/mdds_dds_' + run + '_A')
        args = NS(run=run, record_run=RECORD, side='A', case=case, role='bag-publisher' if case == H.CASES[0] else 'bag-subscriber',
                  finish_control=str(root / 'control.PROBE_FINISH'), contract=str(root / 'r35-contract.json'), contract_sha256='1' * 64,
                  resume_control=None if case == H.CASES[0] else str(root / 'control.R35_RESUME'), bag_seal_sha256=None if case == H.CASES[0] else '2' * 64)
        return self.probe_module.SerializedBagProbe(args)

    def sealed_recording(self, changed=False):
        runner, _, _ = self.construct()
        root = runner.output
        expected = bag_fixture(root / 'closed-bag', self.package, changed=changed)
        H.exclusive(root / 'SOURCE_EXPECTATIONS.json', expected)
        H.exclusive(root / 'COLLECTED_BAG.json', dict(run=RECORD, files=expected['files'],
            local_directory=str(root / 'closed-bag'), normal_closed_before_collection=True))
        H.exclusive(root / 'BAG_CONTENT.json', O.verify_recorded_bag(root / 'closed-bag', expected))
        H.exclusive(root / 'CLEANUP.json', {'sides': {s: {'absent': True, 'forced_signals': []} for s in 'AB'}})
        H.exclusive(root / 'STOP_STATE_COMPLETED.json', dict(attempt_latched=True, intent_persisted=True,
            dispatch_entered=True, dispatch_confirmed=True, dispatch_receipt_persisted=True, recording_completed=True))
        for name in ('NORMAL_STOP_INTENT.json', 'NORMAL_STOP_DISPATCH.json', 'NATIVE.json', 'CHILD_EXITS.json', 'MAIN_RELEASE.json'):
            H.exclusive(root / name, {'synthetic_host_fixture': True})
        evidence = {str(p): H.sha(p) for p in root.rglob('*') if p.is_file()}
        H.exclusive(root / 'RESULT.json', dict(schema=1, scope=H.SCOPE, case=H.CASES[0], run=RECORD, result='PASS',
            normal_process_exit=True, recording_completed=True, persistence_fidelity='PASS', failures=[], blocked=[],
            source_sha256=H.sha(H.__file__), package=H.descriptor(self.derivation), evidence=evidence))
        audit = self.directory('closed-bag-audit')
        H.exclusive(audit / 'RESULT.json', dict(schema=1, scope='R35_INDEPENDENT_CLOSED_BAG_ADMISSION', status='PLAY_INPUT_ADMITTED',
            package=H.descriptor(self.derivation), record_result=H.descriptor(root / 'RESULT.json'),
            collection=H.descriptor(root / 'COLLECTED_BAG.json'), expected=H.descriptor(root / 'SOURCE_EXPECTATIONS.json'),
            bag_content=H.descriptor(root / 'BAG_CONTENT.json')))
        H.exclusive(audit / 'SEAL.json', {'files': {'RESULT.json': H.sha(audit / 'RESULT.json')}})
        return {'result': H.descriptor(audit / 'RESULT.json'), 'seal': H.descriptor(audit / 'SEAL.json')}

    def default_audit(self, recording, clean=True):
        root = self.directory('default-failure')
        H.exclusive(root / 'CLEANUP.json', {'sides': {s: {'absent': clean, 'forced_signals': []} for s in 'AB'}})
        H.exclusive(root / 'RESULT.json', dict(schema=1, scope=H.SCOPE, case=H.CASES[1], record_run=RECORD,
            result='FAIL', experiment_validity='PASS', functionality='PASS', persistence_fidelity='FAIL', blocked=[],
            failures=['Complete-buffer persistence fidelity FAIL; no normalization or implicit next mode'], normal_process_exit=True,
            bag_seal_sha256=recording['seal']['sha256'], source_sha256=H.sha(H.__file__), package=H.descriptor(self.derivation),
            evidence={str(root / 'CLEANUP.json'): H.sha(root / 'CLEANUP.json')}))
        audit = self.directory('default-audit')
        H.exclusive(audit / 'RESULT.json', dict(schema=1, scope='R35_INDEPENDENT_PLAY_AUDIT', status='FAIL_AUDITED',
            allows_no_loan=True, play_result=H.descriptor(root / 'RESULT.json')))
        H.exclusive(audit / 'SEAL.json', {'files': {'RESULT.json': H.sha(audit / 'RESULT.json')}})
        return {'result': H.descriptor(audit / 'RESULT.json'), 'seal': H.descriptor(audit / 'SEAL.json')}

    def complete_play_probe(self, case, outcome='success', bag_seal=None, discovery=None):
        """Real constructor/communicate/emit; only ROS/native leaves are synthetic."""
        probe = self.probe(case)
        if bag_seal is not None:
            args = copy.copy(probe.args); args.bag_seal_sha256 = bag_seal
            probe = self.probe_module.SerializedBagProbe(args)
        subscriptions, log = [], io.StringIO()
        probe.message = lambda lane, index: O.expected_value(lane, index, RECORD)
        class Response:
            pass
        future = NS(done=lambda: outcome != 'timeout', cancelled=lambda: outcome == 'cancelled',
                    exception=lambda: RuntimeError('synthetic future') if outcome == 'raised' else None,
                    result=lambda: object() if outcome == 'wrongtype' else Response())
        client = NS(call_async=Mock(return_value=future), service_is_ready=lambda: True)
        def subscribe(cls, name, callback, qos, raw=False):
            subscriptions.append((name, callback, raw))
            return NS(get_publisher_count=lambda: 1)
        node = NS(create_subscription=subscribe, create_client=lambda *a: client, destroy_node=Mock())
        fake = ModuleType('rclpy'); fake.init = Mock(); fake.create_node = Mock(return_value=node); fake.try_shutdown = Mock()
        qos = ModuleType('rclpy.qos'); qos.QoSProfile = lambda **kw: kw
        qos.ReliabilityPolicy = NS(RELIABLE=1); qos.DurabilityPolicy = NS(VOLATILE=2)
        duration = ModuleType('rclpy.duration'); duration.Duration = lambda **kw: kw
        serial = ModuleType('rclpy.serialization'); serial.serialize_message = Mock(side_effect=AssertionError('No replay reserialization'))
        values = {raw(l, i): O.expected_value(l, i, RECORD) for l in H.LANES for i in (1, 2)}
        serial.deserialize_message = lambda data, cls: values[data]
        srv = ModuleType('rosbag2_interfaces.srv'); srv.Resume = NS(Request=lambda: object(), Response=Response)
        files = H.read(self.derivation)['files']
        def types():
            probe.classes = {'event': dict, 'string': dict}
            probe.emit('TYPE_SUPPORT', origins=module_origins(files), capsules={'event': 'PyCapsule', 'string': 'PyCapsule'})
            probe.emit('DEFINITIONS', definitions={typ: dict(value, native_type_hash='') for typ, value in O.definitions(self.package).items()})
        probe.types_and_definitions = types
        good = graph(case, PLAY, RECORD)
        def ready():
            self.assertEqual(len(subscriptions), 8)
            self.assertEqual(probe.counts, {l: [0, 0] for l in H.LANES})
            self.assertFalse(probe.seen)
            probe.ready_graph = good
            probe.emit('DISCOVERY', snapshot=good, changed_snapshots=1)
            probe.emit('READY', graph=good, counts=probe.counts, slots_empty=True, source_endpoints=None,
                       requested_qos=dict(history=1, depth=10, reliability=1, durability=2))
        if discovery is None:
            probe.wait_ready = ready; probe.graph = lambda: good
        else:
            self.actual_discovery = PublicGraphLeaves(probe, node, discovery)
            fake.ok = self.actual_discovery.ros.ok; fake.spin_once = self.actual_discovery.spin
        initial_tick = probe.tick
        data = dict(schema=1, scope='R35_RESUME', case=case, run=PLAY, record_run=RECORD,
                    pid=os.getpid(), bag_seal_sha256=probe.args.bag_seal_sha256)
        info = NS(st_mode=stat.S_IFREG | 0o600, st_dev=1, st_ino=2, st_size=500, st_mtime_ns=3)
        probe.resume_path = NS(exists=lambda: True, is_symlink=lambda: False, lstat=lambda: info, read_bytes=lambda: json.dumps(data).encode())
        delivered = [False]
        def tick():
            if discovery is not None and not hasattr(probe, 'ready_graph'):
                return initial_tick()
            probe.last_spin_ns = time.monotonic_ns()
            if probe.done:
                probe.emit('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
                return False
            if client.call_async.called and not delivered[0]:
                delivered[0] = True
                for name, callback, is_raw in subscriptions:
                    lane = name.rsplit('/', 1)[1]
                    for index in (1, 2): callback(raw(lane, index) if is_raw else O.expected_value(lane, index, RECORD), {n: None for n in O.META})
                probe.last_receive = time.monotonic() - 0.3
            return True
        probe.tick = tick
        modules = {'rclpy': fake, 'rclpy.qos': qos, 'rclpy.duration': duration, 'rclpy.serialization': serial,
                   'rosbag2_interfaces': ModuleType('rosbag2_interfaces'), 'rosbag2_interfaces.srv': srv}
        if discovery is not None:
            modules['rclpy.node'] = self.actual_discovery.node_module
        try:
            with redirect_stdout(log), patch.dict(sys.modules, modules), patch.object(self.probe_module, 'plain',
                    lambda v: v if isinstance(v, dict) else {'success': True} if outcome == 'nonempty' else {}):
                probe.communicate()
                probe.emit('FINAL', success=True, node_created=True)
        finally:
            self.assertEqual(client.call_async.call_count, 1)
            node.destroy_node.assert_called_once(); fake.try_shutdown.assert_called_once()
            serial.serialize_message.assert_not_called()
        text = log.getvalue()
        rows = O.rows(text, case, PLAY, RECORD, os.getpid())
        self.assertTrue(O.probe_business(rows, case, PLAY, RECORD, True)['complete'])
        return text, rows


def module_origins(files):
    origins = {}
    for name in ('rclpy', 'rclpy._rclpy_pybind11', 'rclpy.serialization', 'rclpy.executors',
                 'rclpy.publisher', 'rclpy.subscription', 'rcl_interfaces.msg', 'std_msgs.msg', 'rosbag2_py'):
        member = 'python/' + name.replace('.', '/') + '.py'
        if member not in files: member = 'python/' + name.replace('.', '/') + '/__init__.py'
        if name == 'rclpy._rclpy_pybind11': member = next(n for n in files if n.startswith('python/rclpy/_rclpy_pybind11.') and n.endswith('.so'))
        origins[name] = dict(member=member, sha256=files[member])
    return origins


class VirtualPlayTransport:
    """Synthetic remote/native/process leaves; actual runner control composition."""
    def __init__(self, runner, records):
        self.r, self.records = runner, records
        self.listener, self.close, self.finish, self.launched = set(), set(), set(), set()
        self.resumed, self.effects, self.files = False, [], {}
        self.inventory_fault = self.after_send = self.after_resume = None
        self.remote = {n: (Path(runner.recording['collection']['local_directory']) / n).read_bytes()
                       for n in runner.recording['collection']['files']}
        self.rootids = {s: '1:' + str(100 + 'AB'.index(s)) + ':0:700' for s in 'AB'}
        self.snapshots = {s: {
            'supervisor': dict(pid=20 + 'AB'.index(s), starttime=100, exe='/system/bin/sh', argv=['sh', runner.prefix[s] + '/supervisor.sh'], ppid=1),
            'child': dict(pid=os.getpid() if s == 'A' else 23, starttime=101, exe=runner.prefix[s] + '/package/actionbag_python',
                          argv=runner.expected_argv[s], ppid=20 + 'AB'.index(s))} for s in 'AB'}
        runner.command, runner.shell, runner.snapshot, runner.maps = self.command, self.shell, self.snapshot, self.maps
        runner.deploy, runner.poll = self.deploy, lambda side: (self.stdout(side), self.status(side))
        runner.verify_files = lambda side: runner.write(side + '.synthetic-postflight.json', dict(host_only=True, count=len(runner.files)))
        runner.roots.update(self.rootids)

    def record(self, side, operation):
        self.r.sequence += 1
        self.effects.append((side, operation))

    def checkpoint(self, side, stage):
        pid = self.snapshots[side]['child']['pid']
        if stage == 'LISTENER': return f'ROS_LISTENER_READY run={self.r.run} side={side} pid={pid} listener=1 fake_sdk=0'
        return f'ROS_RUNTIME_CLOSE_READY schema=1 run={self.r.run} side={side} pid={pid} stage=RUNTIME_CLOSE stopping=1 sealed=1 active_pumps=0 pump_joined=1'

    def final_native(self, side):
        r = self.r; index = str('AB'.index(side)); pid = self.snapshots[side]['child']['pid']
        lines = ['ROS_PYTHON_INTERPRETER_RETURN execution_rc=0 finalize_rc=0',
            'ROS_PYTHON_FINAL finalized=1 python_rc=0 teardown_observed=1 teardown_rc=0 communication=NOT_ASSERTED',
            'ROS_IDENTITY_FINAL restored=1 business_rc=0',
            f'ROS_NATIVE_SUMMARY run={r.run} side={side} send_enter=1 send_return=1 send_error=0 ingress=1 detailed_stored=2 metadata_limit=8192 overflow=0 malformed=0 sdk_epoch=NOT_PROVEN']
        for i, direction in enumerate(('SDK_SEND', 'SDK_INGRESS'), 1):
            lines.append(f'ROS_NATIVE_DATA run={r.run} side={side} observation={i} direction={direction} socket=1 writer=' + '1' * 32 + ' sequence_hi=0 sequence_lo=1 scope=STRUCTURAL_APP_OBSERVER')
        lines.append(f'ROS_STARTUP_CAPTURE run={r.run} side={side} source=REAL_TARGET completeness=CAPTURED overflow=0 output_errors=0 text_truncated=0 first_observation_seq=0 failure_ref=0 stored=1 limit=256')
        for op in ('Socket', 'Listen', 'Bind', 'SendBytes', 'Shutdown'):
            semantics = 'VOID_NO_RC' if op == 'Shutdown' else 'HANDLE_OR_ERROR' if op == 'Socket' else 'SDK_RC'
            lines.append(f'ROS_STARTUP_OPERATION run={r.run} side={side} source=REAL_TARGET state=RETURN_OBSERVED return_semantics={semantics} operation={op} entered=1 returned=1')
        lines += [f'ROS_STARTUP_EVENT run={r.run} side={side} source=REAL_TARGET truncated=0',
            f'FDDST_ROS_PRE_PARTICIPANT run={r.run} side={index} builtin=0 intraprocess=0 mode=OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
            f'FDDST_REAL_SOCKET run={r.run} side={index} fake_sdk=0',
            f'FDDST_ROS_CLOSED run={r.run} side={index} pump_joined=1 runtime_closed=1 local_only=1',
            f'ROS_TEARDOWN_CAPTURE run={r.run} side={side} pid={pid} completeness=CAPTURED overflow=0 output_errors=0',
            f'ROS_TEARDOWN_EVENT run={r.run} side={side} pid={pid} schema=1 operation=Factory_delete_participant returned=1 value_type=PUBLIC_RETURN_CODE value=0']
        derive = H.runpy.run_path(str(H.REPO / 'scripts/public_socket/analyze_v1_locators.py'))['derive']
        other = 'B' if side == 'A' else 'A'
        for direction, owner in (('SDK_SEND', side), ('SDK_INGRESS', other)):
            loc = derive(dict(namespace='mdds.r25.action', domain=201, udid=r.udids[owner]), 'AB'.index(owner) + 1)
            for param in ('0031', '0032'):
                lines.append(f'ROS_V1_SPDP run={r.run} side={side} bytes=24 pid={param} direction={direction} writer=' + '1' * 24 +
                    '000100c2 observation=1 socket=1 sequence_high=0 sequence_low=1 data_offset=0 ' + ' '.join(k + '=' + v for k, v in loc.items()))
        loc = derive(dict(namespace='mdds.r25.action', domain=201, udid=r.udids[other]), 'AB'.index(other) + 1)
        lines += [f'ROS_V1_SELECTED run={r.run} side={side} scope=ACTUAL_SELECTOR_VALUES ' + ' '.join(k + '=' + v for k, v in loc.items()),
                  f'ROS_V1_OBSERVATION run={r.run} side={side} captured=1 faults=0 adverts=4 selected=1']
        return '\n'.join(lines)

    def stdout(self, side):
        lines = [self.checkpoint(side, 'LISTENER')]
        if len(self.listener) < 2: return '\n'.join(lines)
        lines += ['ROS_CONNECT_ALLOWED run=' + self.r.run, *H.ORDINARY.ERROR_PAIR]
        if side == 'A':
            records = self.records if side in self.finish else [x for x in self.records if x['event'] not in
                ('FINISH_ACCEPTED', 'OBSERVATION_END', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL')]
            if not self.resumed:
                records = records[:next(i + 1 for i, row in enumerate(records) if row['event'] == 'READY')]
            lines += ['R35_PROBE ' + json.dumps(x, separators=(',', ':')) for x in records]
        if (side == 'A' and side in self.finish) or (side == 'B' and self.resumed): lines.append(self.checkpoint(side, 'CLOSE'))
        if len(self.close) == 2: lines.append(self.final_native(side))
        return '\n'.join(lines)

    def stderr(self, side):
        return '\n'.join('FDDST_ENDPOINT topic=ros_discovery_info role=' + role + ' data_sharing=OFF' for role in ('writer', 'reader', 'reader'))

    def status(self, side):
        return f'R25_CHILD_EXIT run={self.r.run} side={side} pid={self.snapshots[side]["child"]["pid"]} rc=0' if len(self.close) == 2 else ''

    def remote_bytes(self, side, path):
        name = path.rsplit('/', 1)[-1]
        if name in ('stdout', 'stderr', 'exit', 'supervisor.stdout', 'supervisor.stderr'):
            return (self.stdout(side) if name == 'stdout' else self.stderr(side) if name == 'stderr' else self.status(side) if name == 'exit' else '').encode()
        if '/input/bag/' in path: return self.remote[name]
        return self.files[path]

    def command(self, side, argv, timeout=15):
        self.record(side, argv)
        assert argv[:2] in (['file', 'send'], ['file', 'recv'])
        if argv[1] == 'recv': Path(argv[3]).write_bytes(self.remote_bytes(side, argv[2]))
        else:
            self.files[argv[3]] = Path(argv[2]).read_bytes()
            if '/input/bag/' in argv[3]: self.remote[argv[3].rsplit('/', 1)[-1]] = self.files[argv[3]]
        return b'transfer finish'

    def shell(self, side, text, timeout=15):
        self.record(side, text); r = self.r; root = r.prefix[side]
        if text.startswith('test ! -L ' + root + ' && stat -c '): return self.rootids[side].encode()
        if 'nohup sh ' in text: self.launched.add(side); return str(self.snapshots[side]['supervisor']['pid']).encode()
        if '/child.pid' in text: return str(self.snapshots[side]['child']['pid']).encode()
        if text.startswith('kill -'): raise AssertionError('No real or virtual forced signal is allowed in successful play')
        if 'control.LISTENER_READY' in text: self.listener.add(side); return b''
        if 'control.R35_RESUME' in text: self.resumed = True; return b''
        if 'control.PROBE_FINISH' in text:
            self.finish.add(side)
            if self.after_resume: self.after_resume()
            return b''
        if 'control.RUNTIME_CLOSE' in text: self.close.add(side); return b''
        if text.startswith('if test -f ' + root + '/exit'): return self.status(side).encode()
        if text.startswith('stat -c %s '): return str(len(self.remote_bytes(side, text.split(' ', 3)[3]))).encode()
        if text.startswith('sha256sum '):
            rows = []
            for path in shlex.split(text)[1:]:
                digest = r.files[path[len(root + '/package/'):]] if path.startswith(root + '/package/') else O.digest(self.remote_bytes(side, path))
                rows.append(digest + '  ' + path)
            # Fault injection is after the inherited final per-file hash receipt.
            if text.endswith('/input/bag/' + next(reversed(r.recording['collection']['files']))) and self.after_send:
                callback, self.after_send = self.after_send, None; callback()
            return '\n'.join(rows).encode()
        if text.startswith('test -d ' + root + '/input/bag'):
            inventory = {n: dict(bytes=len(data), sha256=O.digest(data)) for n, data in self.remote.items()}
            if self.inventory_fault: self.inventory_fault(inventory)
            return '\n'.join('FILE ' + str(row['bytes']) + ':1\n' + row['sha256'] + '  ' + name for name, row in inventory.items()).encode()
        return b''

    def snapshot(self, side, pid):
        if len(self.close) == 2: return None
        return next(copy.deepcopy(row) for row in self.snapshots[side].values() if row['pid'] == pid)

    def maps(self, side, phase):
        path = self.r.prefix[side] + '/package/lib/libpython3.12.so.1.0'
        rows = {path: self.r.files['lib/libpython3.12.so.1.0']}
        self.r.maps_observed[side] = rows
        self.r.write(side + '.' + phase + '.providers.json', dict(sha256=rows, synthetic_host_only=True))

    def deploy(self):
        self.r.stage = 'DEPLOY'
        for side in 'AB': self.r.configure(side)


class ContentTests(Fixtures):
    def test_resume_exact_schema_boolean_types_and_mode(self):
        for field, value in [('future_done', False), ('future_done', 1), ('future_cancelled', True),
                             ('future_cancelled', 0), ('future_raised', True), ('future_raised', None),
                             ('exception', None), ('unknown', 'extra')]:
            rows = fixture_records(H.CASES[1], PLAY, RECORD)
            O.one(rows, 'RESUME_RETURNED')[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                O.probe_business(rows, H.CASES[1], PLAY, RECORD, True)
        for control in ('missing', 'unknown-event', 'wrong-mode'):
            rows = fixture_records(H.CASES[1], PLAY, RECORD)
            response = O.one(rows, 'RESUME_RETURNED')
            if control == 'missing': del response['future_done']
            elif control == 'unknown-event': response['event'] = 'UNKNOWN'
            else:
                header = {key: value for key, value in response.items() if key in O.COMMON_FIELDS}
                response.clear(); response.update(header)
                response.update(event='ACKED', lane='typed_event', returned=True, timeout_ns=1)
            text = '\n'.join('R35_PROBE ' + json.dumps(row) for row in rows)
            with self.subTest(control=control), self.assertRaises(ValueError): O.rows(text, H.CASES[1], PLAY, RECORD, 22)

    def test_selected_stock_parser_functions_accept_exact_four_topic_argv(self):
        # Execute the actual selected Python parser AST. Only native plugin
        # enumeration/entry-point loading is stubbed; this is not runtime proof.
        files = H.read(self.derivation)['files']
        def selected(ending):
            return self.package / next(n for n in files if n.endswith(ending))
        ns = dict(ArgumentParser=argparse.ArgumentParser, FileType=argparse.FileType,
            SplitLineFormatter=argparse.HelpFormatter, NODE_NAME_PREFIX='rosbag2',
            rosbag2_py=NS(get_registered_readers=lambda: ['sqlite3']),
            get_registered_writers=lambda: ['sqlite3'], get_default_storage_id=lambda: 'sqlite3',
            get_registered_serializers=lambda: ['cdr_converter'], get_registered_compressors=lambda: [],
            get_entry_points=lambda group: {'sqlite3': NS(load=lambda: NS(get_preset_profiles=lambda: [('none', 'default')]))})
        def load(path, nodes):
            tree = ast.Module(body=[ast.ImportFrom(module='__future__', names=[ast.alias(name='annotations')], level=0), *nodes], type_ignores=[])
            exec(compile(ast.fix_missing_locations(tree), str(path), 'exec'), ns)
        api = selected('/ros2bag/api/__init__.py')
        load(api, [n for n in ast.parse(api.read_text()).body if isinstance(n, ast.FunctionDef)])
        ns['_parse_cli_storage_plugin'] = lambda: 'sqlite3'
        record = selected('/ros2bag/verb/record.py')
        load(record, [n for n in ast.parse(record.read_text()).body if isinstance(n, ast.FunctionDef) and n.name == 'add_recorder_arguments'])
        play = selected('/ros2bag/verb/play.py'); tree = ast.parse(play.read_text())
        method = copy.deepcopy(next(n for n in next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'PlayVerb').body if isinstance(n, ast.FunctionDef) and n.name == 'add_arguments'))
        method.name = 'play_arguments'
        load(play, [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'positive_float'] + [method])
        qos = self.directory('parser') / 'qos.yaml'; qos.write_text('{}')
        for case in H.CASES:
            parser = argparse.ArgumentParser()
            (ns['add_recorder_arguments'](parser) if case == H.CASES[0] else ns['play_arguments'](None, parser, 'ros2 bag play'))
            argv = H.operation_argv(case, RECORD if case == H.CASES[0] else PLAY, RECORD, 'B', '/owned', '1' * 64)[4:]
            argv[argv.index('/owned/qos.yaml')] = str(qos)
            args = parser.parse_args(argv)
            self.assertEqual(args.topics, [O.topic(RECORD, l) for l in H.LANES])
            self.assertTrue(args.disable_keyboard_controls)
            if case == H.CASES[0]: self.assertEqual(args.max_cache_size, 0)
            else:
                self.assertEqual(args.disable_loan_message, case == H.CASES[2])
                self.assertEqual(args.wait_for_all_acked, 5000)
                self.assertTrue(args.start_paused)
            args.qos_profile_overrides_path.close()

    def test_route_provenance_exception_is_exact_path_and_hash(self):
        root = self.directory('sdk-constraint')
        sdk, sibling = root / 'libc++_shared.so', root / 'sibling.so'
        sdk.write_bytes(b'synthetic fixed product'); sibling.write_bytes(sdk.read_bytes())
        digest = H.sha(sdk)
        with patch.object(H.Q, 'SDK_LIBCXX', sdk), patch.object(H.Q, 'SDK_LIBCXX_SHA', digest), \
                patch.object(H.Q.BASE, 'runtime_path', side_effect=ValueError('Outside permitted build evidence')):
            self.assertEqual(H.Q.route_binding(sdk, digest), sdk.resolve())
            for path, expected in ((sibling, digest), (sdk, '0' * 64)):
                with self.assertRaises(ValueError): H.Q.route_binding(path, expected)
            sdk.write_bytes(b'changed')
            with self.assertRaises(ValueError): H.Q.route_binding(sdk, digest)

    def test_complete_counters_resume_order_and_buffer_chains_cannot_be_forged(self):
        for case in H.CASES[:2]:
            run = RECORD if case == H.CASES[0] else PLAY
            original = fixture_records(case, run, RECORD)
            complete = 'RECORD_COMPLETE' if case == H.CASES[0] else 'PLAY_COMPLETE'
            for event, field, wrong in [('READY', 'slots_empty', False), ('NODE_READY', 'name', '/other'),
                    (complete, 'published' if case == H.CASES[0] else 'callbacks', 9),
                    ('NATIVE_DECODE', 'buffer_seq', 999), ('OBSERVATION_END', 'last_spin_ns', None),
                    ('FINAL', 'node_created', False)]:
                altered = copy.deepcopy(original)
                next(r for r in altered if r['event'] == event)[field] = wrong
                with self.subTest(case=case, event=event), self.assertRaises(ValueError):
                    O.probe_business(altered, case, run, RECORD, True)

    def test_complete_stored_buffers_are_actual_blobs(self):
        root = self.directory('actual-blobs') / 'bag'
        expected = bag_fixture(root, self.package, typed_diff=True)
        actual = O.stored_buffers(root, expected)
        self.assertEqual(set(actual), {(l, i) for l in H.LANES for i in (1, 2)})
        for (lane, index), value in actual.items():
            self.assertEqual(value, raw(lane, index, b'' if lane == 'typed_event' else b'fixture!'))

    def test_nonmonotonic_and_equal_writer_timing(self):
        self.assertEqual(O.writer_timing([100, 200, 50]), (50, 100))
        self.assertEqual(O.writer_timing([10] * 8), (10, 0))

    def test_real_selected_definitions_and_whole_suffixes(self):
        self.assertEqual(O.digest(O.definitions(self.package)['rcl_interfaces/msg/ParameterEvent']['encoded_message_definition'].encode()), 'd663b4cdaa3b577d7e0125c066f439b8514c290307b98c043318b7b45a159a79')
        for lane in H.LANES:
            _, value, report = O.inspect_buffer(raw(lane, 1), lane, RECORD)
            self.assertEqual(value, O.expected_value(lane, 1, RECORD))
            self.assertEqual(report['suffix_bytes'], 0 if lane.endswith('string') else 8)
        with self.assertRaises(ValueError):
            O.inspect_buffer(raw('typed_string', 1) + b'bad', 'typed_string', RECORD)

    def test_exact_bag_and_typed_observation_is_not_fidelity_failure(self):
        root = self.directory('valid') / 'bag'
        expected = bag_fixture(root, self.package, typed_diff=True)
        before = dict(expected['files'])
        report = O.verify_recorded_bag(root, expected)
        self.assertTrue(report['play_eligible'])
        self.assertEqual((report['writer_start'], report['writer_duration']), (50, 100))
        self.assertTrue(any(not r['equal'] for r in report['comparisons'] if r['comparison'].startswith('OBSERVATIONAL')))
        self.assertEqual({p.name: H.sha(p) for p in root.iterdir()}, before)
        self.assertNotIn('base64', json.dumps(report))

    def test_same_semantics_changed_serialized_suffix_bars_both_plays(self):
        root = self.directory('suffix-diff') / 'bag'
        report = O.verify_recorded_bag(root, bag_fixture(root, self.package, changed=True))
        self.assertEqual(report['functionality'], 'PASS')
        self.assertEqual(report['persistence_fidelity'], 'FAIL')
        self.assertFalse(report['play_eligible'])

    def test_sqlite_malformed_missing_extra_foreign_duplicate_and_metadata(self):
        controls = ['missing', 'extra', 'foreign', 'duplicate', 'truncated', 'bad-type', 'definition', 'hash', 'metadata', 'view']
        for control in controls:
            with self.subTest(control=control):
                root = self.directory(control) / 'bag'
                expected = bag_fixture(root, self.package)
                connection = sqlite3.connect(root / 'fixture.db3')
                if control == 'missing': connection.execute('DELETE FROM messages WHERE id=8')
                elif control == 'extra': connection.execute('INSERT INTO messages(topic_id,timestamp,data) SELECT topic_id,timestamp,data FROM messages WHERE id=1')
                elif control == 'foreign': connection.execute('UPDATE messages SET topic_id=99 WHERE id=1')
                elif control == 'duplicate': connection.execute('UPDATE messages SET data=(SELECT data FROM messages WHERE id=1) WHERE id=5')
                elif control == 'truncated': connection.execute('UPDATE messages SET data=? WHERE id=1', (raw('typed_event', 1)[:30],))
                elif control == 'bad-type': connection.execute("UPDATE topics SET type='foreign/msg/Type' WHERE id=1")
                elif control == 'definition': connection.execute("UPDATE message_definitions SET encoded_message_definition='' WHERE id=1")
                elif control == 'hash': connection.execute("UPDATE topics SET type_description_hash='other' WHERE id=1")
                elif control == 'metadata': connection.execute("UPDATE metadata SET metadata_version=8")
                else: connection.execute('CREATE VIEW forged AS SELECT * FROM messages')
                connection.commit(); connection.close()
                expected['files'] = {p.name: H.sha(p) for p in root.iterdir()}
                with self.assertRaises((ValueError, KeyError)):
                    O.verify_recorded_bag(root, expected)

    def test_all_defaults_and_wrong_run_are_checked(self):
        value = O.expected_value('typed_event', 1, RECORD)
        value['changed_parameters'][0]['value']['integer_value'] = 1
        with self.assertRaises(ValueError): O.inspect_buffer(encode('ParameterEvent', value), 'typed_event', RECORD)
        with self.assertRaises(ValueError): O.inspect_buffer(raw('typed_event', 1), 'typed_event', PLAY)

    def test_full_probe_tables_and_different_record_play_run(self):
        for case, run in [(H.CASES[0], RECORD), (H.CASES[1], PLAY), (H.CASES[2], PLAY)]:
            rows = fixture_records(case, run, RECORD)
            text = '\n'.join('R35_PROBE ' + json.dumps(r) for r in rows)
            actual = O.rows(text, case, run, RECORD, 22)
            result = O.probe_business(actual, case, run, RECORD, True)
            self.assertTrue(result['complete'])
            self.assertEqual(len(result['received']), 0 if case == H.CASES[0] else 16)

    def test_early_duplicate_late_missing_metadata_and_resume_response_rejected(self):
        for control in ['early', 'duplicate', 'late', 'missing', 'metadata', 'resume']:
            with self.subTest(control=control):
                rows = fixture_records(H.CASES[1], PLAY, RECORD)
                receive = next(r for r in rows if r['event'] == 'TYPED_RECEIVE')
                if control == 'early': next(r for r in rows if r['event'] == 'RESUME_CALLED')['seq'] = receive['seq'] + 1
                elif control in ('duplicate', 'late'): rows.insert(-3 if control == 'late' else 12, copy.deepcopy(receive))
                elif control == 'missing': rows.remove(receive)
                elif control == 'metadata': receive['metadata']['source_timestamp'] = True
                else: next(r for r in rows if r['event'] == 'RESUME_RETURNED')['fields'] = {'success': True}
                with self.assertRaises(ValueError): O.probe_business(rows, H.CASES[1], PLAY, RECORD, True)

    def test_graph_four_primary_and_separate_ancillary(self):
        good = graph(H.CASES[1], PLAY, RECORD)
        O.validate_graph(good, H.CASES[1], PLAY, RECORD)
        self.assertGreater(len(good['entities']['/r35bagplayer_' + PLAY]['publishers']), 4)
        for control in ['fifth', 'owner', 'node', 'ancillary', 'qos', 'type']:
            changed = copy.deepcopy(good)
            item = changed['data']['typed_event'][0]
            if control == 'fifth': changed['data']['typed_event'].append(copy.deepcopy(item))
            elif control == 'owner': item['owner'] = '/foreign'
            elif control == 'node': changed['nodes'].append('/third')
            elif control == 'ancillary': changed['entities']['/r35bagplayer_' + PLAY]['publishers']['/extra'] = 'std_msgs/msg/String'
            elif control == 'qos': item['qos']['durability'] = 'transient_local'
            else: item['type'] = 'std_msgs/msg/String'
            with self.assertRaises(ValueError): O.validate_graph(changed, H.CASES[1], PLAY, RECORD)


class ConstructorAndControlTests(Fixtures):
    def test_real_workflow_record_order_and_early_close(self):
        runner, texts, business = self.stop_runner()
        events, polls = [], [0]
        runner.launch = lambda side: events.append(('launch', side))
        def pair():
            polls[0] += 1
            return texts | {'A': ''}, dict(A='', B='')
        runner.poll_pair = pair
        runner.maps = lambda side, phase: events.append(('maps', side, phase))
        runner.publish = lambda side, name, contents, **kw: events.append(('control', side, name))
        runner.finish_fixture = lambda side, reason: events.append(('finish', side, reason))
        runner.wait_exit = lambda seconds: events.append(('exit', seconds))
        runner.business = lambda text: business
        runner.ready = lambda side, text, phase: phase == 'LISTENER' or polls[0] >= (4 if side == 'A' else 3)
        original = runner.normal_recorder_stop
        def stop(text, business):
            events.append(('TERM', 'B')); original(text, business)
        runner.normal_recorder_stop = stop
        texts['B'] += '\nRecording stopped'
        def pair():
            polls[0] += 1
            return {'A': '', 'B': texts['B'].replace('\nRecording stopped', '') if polls[0] < 3 else texts['B']}, dict(A='', B='')
        runner.poll_pair = pair
        runner.workflow()
        self.assertEqual(events.count(('TERM', 'B')), 1)
        self.assertLess(events.index(('TERM', 'B')), events.index(('finish', 'A', 'NORMAL_COMPLETION')))
        self.assertLess(events.index(('finish', 'A', 'NORMAL_COMPLETION')), events.index(('control', 'B', 'control.RUNTIME_CLOSE')))
        self.assertEqual(events[-1], ('exit', 20))
        # Same constructed owner, no second operation: pure prerequisite rejection.
        runner.normal_stop_attempted = False
        runner.ready = lambda side, text, phase: True
        with self.assertRaisesRegex(ValueError, 'Recorder closed before normal TERM'): runner.workflow()

    def test_consumed_release_cannot_dispatch_again(self):
        runner, args, _ = self.construct()
        H.exclusive(args.prepared.parent / (runner.run + '.CONSUMED.json'), {'already': 'consumed'})
        runner.deploy = Mock(side_effect=AssertionError('Consumed release cannot deploy'))
        self.assertEqual(runner.execute(), 1)
        runner.deploy.assert_not_called()
        self.assertEqual(H.read(runner.output / 'RESULT.json')['result'], 'BLOCKED')

    def test_real_complete_constructor_and_current_package(self):
        runner, args, _ = self.construct()
        self.assertEqual(runner.plan['limits'], H.LIMITS)
        self.assertEqual(len(runner.files), 3240)
        self.assertEqual(runner.expected_argv['B'][1:4], ['ros2', 'bag', 'record'])
        self.assertIsNone(runner.roles['B'])
        probe = self.probe(H.CASES[1])
        self.assertIsInstance(probe.topic, str)
        self.assertTrue(callable(probe.lane_topic))
        self.assertEqual(probe.counts, {l: [0, 0] for l in H.LANES})
        self.assertEqual(probe.seen, set())
        self.assertTrue(all(O.topic(PLAY, l, True) == probe.lane_topic(l) for l in H.LANES))

    def test_argv_default_and_control_are_distinct_and_qos_original_names(self):
        for case in H.CASES[1:]:
            argv = H.operation_argv(case, PLAY, RECORD, 'B', '/owned', '1' * 64, '2' * 64)
            self.assertEqual('--disable-loan-message' in argv, case == H.CASES[2])
            for lane in H.LANES:
                self.assertIn(O.topic(RECORD, lane), argv)
                self.assertIn(O.topic(RECORD, lane) + ':=' + O.topic(PLAY, lane, True), argv)
            self.assertNotIn('--loop', argv)

    def test_wrong_source_qualification_release_and_stale_future_routes(self):
        runner, args, _ = self.construct()
        for control in ['mode', 'prepared', 'runner', 'ops']:
            value = H.read(args.release)
            if control == 'mode': value['case'] = H.CASES[1]
            elif control == 'prepared': value['prepared_sha256'] = '0' * 64
            elif control == 'runner': value['runner_sha256'] = '0' * 64
            else: value['operations_review']['sha256'] = '0' * 64
            bad = self.directory(control) / 'release.json'; H.exclusive(bad, value)
            with self.assertRaises(ValueError): H.checked_release(NS(prepared=args.prepared, release=bad), runner.plan)
        path = Path(runner.plan['selection_file']['path'])
        for offset in [-301, 10]:
            selection = H.read(path); data = H.read(selection['routes']['path'])
            data['observed_utc'] = (datetime.now(timezone.utc) + timedelta(seconds=offset)).isoformat()
            root = self.directory('stale'); H.exclusive(root / 'routes.json', data)
            selection['routes'] = H.descriptor(root / 'routes.json'); H.exclusive(root / 'selection.json', selection)
            with self.assertRaises(ValueError): H.prepare(root / 'selection.json')

    def stop_runner(self):
        runner, _, _ = self.construct()
        runner.owned = {s: {'child': dict(pid=22 if s == 'A' else 23), 'supervisor': dict(pid=20 if s == 'A' else 21)} for s in 'AB'}
        runner.guard = Mock(return_value=True)
        runner.root_guard = Mock()
        runner.shell = Mock(return_value=b'')
        for side in 'AB': runner.write(side + '.business.providers.json', {'sha256': {}})
        text = 'Recording...\n' + '\n'.join("Subscribed to topic '" + O.topic(RECORD, l) + "'" for l in H.LANES)
        return runner, {'B': text}, {'complete': True}

    def test_normal_term_state_is_once_and_not_completion(self):
        runner, texts, business = self.stop_runner()
        runner.normal_recorder_stop(texts, business)
        self.assertTrue(runner.stop_state['attempt_latched'])
        self.assertTrue(runner.stop_state['dispatch_confirmed'])
        self.assertFalse(runner.stop_state['recording_completed'])
        self.assertEqual(runner.shell.call_args.args, ('B', 'kill -TERM 23'))
        with self.assertRaises(ValueError): runner.normal_recorder_stop(texts, business)
        self.assertEqual(runner.shell.call_count, 1)

    def stop_failure(self, control):
        runner, texts, business = self.stop_runner()
        real = H.exclusive
        def exclusive(path, value):
            if (control == 'intent' and path.name == 'NORMAL_STOP_INTENT.json') or (control == 'receipt' and path.name == 'NORMAL_STOP_DISPATCH.json'):
                raise OSError('synthetic persistence failure')
            return real(path, value)
        if control == 'guard': runner.guard.side_effect = OSError('guard drift')
        if control == 'dispatch': runner.shell.side_effect = OSError('uncertain dispatch')
        with patch.object(H, 'exclusive', exclusive), self.assertRaises(OSError): runner.normal_recorder_stop(texts, business)
        self.assertTrue(runner.normal_stop_attempted)
        self.assertFalse(runner.normal_stop_confirmed)
        self.assertEqual(runner.stop_classification() == 'NOT_DISPATCHED', control in ('intent', 'guard'))
        self.assertEqual(runner.stop_state['dispatch_confirmed'], control == 'receipt')
        self.assertEqual(runner.shell.call_count, int(control in ('dispatch', 'receipt')))
        with self.assertRaises(ValueError): runner.normal_recorder_stop(texts, business)

    def test_stop_intent_failure_is_not_dispatched(self): self.stop_failure('intent')
    def test_stop_guard_drift_is_not_dispatched(self): self.stop_failure('guard')
    def test_stop_dispatch_ambiguity_is_latched_no_repeat(self): self.stop_failure('dispatch')
    def test_stop_receipt_failure_keeps_confirmation_distinct(self): self.stop_failure('receipt')

    def test_failed_stop_cleanup_never_signals_B_or_claims_pass(self):
        for entered in (False, True):
            runner, _, _ = self.stop_runner()
            runner.normal_stop_attempted = runner.stop_state['attempt_latched'] = True
            runner.stop_state['dispatch_entered'] = entered
            runner.cleanup_observe = lambda side, **kwargs: side == 'B'
            runner.verify_files = Mock()
            runner.shell = Mock(return_value=b'0')
            clock = [0]
            def tick(): clock[0] += 1; return clock[0]
            with patch.object(H.time, 'monotonic', tick), patch.object(H.time, 'sleep', lambda _: None): runner.cleanup(True)
            calls = [c.args for c in runner.shell.call_args_list]
            self.assertFalse(any(s == 'B' and 'kill -' in text for s, text in calls))
            if entered: self.assertFalse(any(s == 'B' and 'WINDOW_FAILED' in text for s, text in calls))
            self.assertTrue(runner.blocked)
            self.assertFalse(H.read(runner.output / 'CLEANUP.json')['sides']['B']['absent'])

    def test_partial_launch_unknown_child_has_no_signal(self):
        runner, _, _ = self.stop_runner()
        runner.owned = {'B': {'supervisor': {'pid': 21}}}
        runner.launch_pending = {'B': {'state': 'LAUNCH_PENDING'}}
        runner.cleanup(True)
        runner.shell.assert_not_called()
        self.assertTrue(runner.blocked)

    def test_complete_callback_keeps_same_raw_bytes_and_never_serializes_typed(self):
        probe = self.probe(H.CASES[1])
        probe.deadline = time.monotonic() + 40
        probe.resume_issued = True
        probe.classes = {'event': dict, 'string': dict}
        probe.message = lambda lane, index: O.expected_value(lane, index, RECORD)
        captured = []
        probe.emit = lambda event, **fields: (setattr(probe, 'sequence', probe.sequence + 1), captured.append(dict(event=event, **fields)))
        serial = ModuleType('rclpy.serialization')
        serial.serialize_message = Mock(side_effect=AssertionError('typed callback must not reserialize'))
        serial.deserialize_message = Mock(side_effect=lambda data, cls: O.inspect_buffer(data, current[0], RECORD)[1])
        current = ['typed_event']
        with patch.dict(sys.modules, {'rclpy': ModuleType('rclpy'), 'rclpy.serialization': serial}), patch.object(self.probe_module, 'plain', lambda value: value):
            for lane in H.LANES:
                current[0] = lane
                for index in (1, 2):
                    data = raw(lane, index)
                    probe.callback(lane, True)(data, {n: None for n in O.META})
                    self.assertIs(serial.deserialize_message.call_args.args[0], data)
                    probe.callback(lane, False)(O.expected_value(lane, index, RECORD), {n: None for n in O.META})
            self.assertEqual(len(probe.seen), 16)
            self.assertEqual(probe.counts, {l: [2, 2] for l in H.LANES})
            current[0] = 'typed_event'
            with self.assertRaises(RuntimeError): probe.callback('typed_event', True)(raw('typed_event', 1), {n: None for n in O.META})
        serial.serialize_message.assert_not_called()
        self.assertEqual(len([r for r in captured if r['event'] == 'BUFFER']), 9)  # duplicate raw retained before rejection
        first = [r['event'] for r in captured[:3]]
        self.assertEqual(first, ['BUFFER', 'NATIVE_DECODE', 'RAW_RECEIVE'])

    def test_probe_constructor_rejects_role_run_and_control_alias(self):
        probe = self.probe(H.CASES[1])
        for name, value in [('side', 'B'), ('record_run', PLAY), ('contract', '/other'), ('resume_control', '/other')]:
            args = copy.copy(probe.args); setattr(args, name, value)
            with self.assertRaises(ValueError): self.probe_module.SerializedBagProbe(args)

    def test_play_real_constructor_requires_faithful_closed_audit(self):
        recording = self.sealed_recording()
        runner, _, _ = self.construct(H.CASES[1], recording)
        self.assertEqual(runner.record_run, RECORD)
        self.assertEqual(runner.run, PLAY)
        self.assertIn('--start-paused', runner.expected_argv['B'])
        self.assertEqual(runner.recording['content']['persistence_fidelity'], 'PASS')
        for field in ('result', 'seal'):
            bad = copy.deepcopy(recording); bad[field]['sha256'] = '0' * 64
            with self.assertRaises(ValueError): H.checked_recording(bad, self.derivation, {})
    def test_changed_serialized_record_cannot_be_admitted_by_forged_pass(self):
        changed = self.sealed_recording(changed=True)
        with self.assertRaises(ValueError): H.checked_recording(changed, self.derivation, {})

    def test_control_needs_independent_default_failure_audit_and_clean_ownership(self):
        recording = self.sealed_recording()
        with self.assertRaises(ValueError): self.construct(H.CASES[2], recording)
        bad = self.default_audit(recording, clean=False)
        with self.assertRaises(ValueError): self.construct(H.CASES[2], recording, bad)
        audit = self.default_audit(recording)
        runner, _, _ = self.construct(H.CASES[2], recording, audit)
        self.assertIn('--disable-loan-message', runner.expected_argv['B'])
        self.assertEqual(H.read(H.read(audit['result']['path'])['play_result']['path'])['persistence_fidelity'], 'FAIL')

    def test_resume_control_is_once_bound_to_fresh_play_and_original_record(self):
        probe = self.probe(H.CASES[1])
        emitted = []
        probe.emit = lambda event, **fields: emitted.append((event, fields))
        data = dict(schema=1, scope='R35_RESUME', case=H.CASES[1], run=PLAY, record_run=RECORD,
                    pid=os.getpid(), bag_seal_sha256='2' * 64)
        info = NS(st_mode=stat.S_IFREG | 0o600, st_dev=1, st_ino=2, st_size=500, st_mtime_ns=3)
        probe.resume_path = NS(exists=lambda: True, is_symlink=lambda: False, lstat=lambda: info,
                               read_bytes=lambda: json.dumps(data).encode())
        self.assertTrue(probe.resume_requested())
        self.assertFalse(probe.resume_requested())
        self.assertEqual(len(emitted), 1)
        for field, wrong in [('run', RECORD), ('record_run', PLAY), ('bag_seal_sha256', '3' * 64), ('pid', os.getpid() + 1)]:
            other = self.probe(H.CASES[1]); mutated = dict(data); mutated[field] = wrong
            other.resume_path = NS(exists=lambda: True, is_symlink=lambda: False, lstat=lambda: info,
                                  read_bytes=lambda: json.dumps(mutated).encode())
            with self.assertRaises(RuntimeError): other.resume_requested()

    def test_full_communicate_creates_eight_subscriptions_and_accepts_empty_resume(self):
        for outcome in ('success', 'wrongtype', 'nonempty', 'cancelled', 'raised', 'timeout'):
            with self.subTest(outcome=outcome):
                if outcome != 'success':
                    with self.assertRaises(RuntimeError): self.complete_play_probe(H.CASES[1], outcome)
                else:
                    _, rows = self.complete_play_probe(H.CASES[1])
                    returned = O.one(rows, 'RESUME_RETURNED')
                    self.assertEqual(returned['fields'], {})
                    self.assertIs(returned['future_done'], True)
                    self.assertIs(returned['future_cancelled'], False)
                    self.assertIs(returned['future_raised'], False)


class CompositionTests(Fixtures):
    def virtual_play(self, case):
        recording = self.sealed_recording()
        prior = self.default_audit(recording) if case == H.CASES[2] else None
        runner, _, _ = self.construct(case, recording, prior)
        _, records = self.complete_play_probe(case, bag_seal=runner.recording['seal_sha256'])
        backend = VirtualPlayTransport(runner, records)
        runner.write('SYNTHETIC_HOST_ONLY.json', dict(device_tested=False, native_verified=False,
            real_methods=['constructor', 'configure', 'workflow', 'resume_playback', 'analyze', 'execute', 'cleanup', 'closed_bag'],
            real_producer='complete SerializedBagProbe.communicate/emit with synthetic ROS leaves',
            real_diagnostics='unchanged R.recordplay_errors scans full raw stdout/stderr'))
        return runner, backend

    def execute_success(self, case):
        runner, backend = self.virtual_play(case)
        self.assertEqual(runner.execute(), 0)
        result = H.read(runner.output / 'RESULT.json')
        self.assertEqual((result['result'], result['experiment_validity'], result['functionality'], result['persistence_fidelity']),
                         ('PASS', 'PASS', 'PASS', 'PASS'))
        self.assertTrue(backend.resumed)
        self.assertEqual(sum(isinstance(op, str) and 'control.R35_RESUME' in op for _, op in backend.effects), 1)
        for phase in ('PREFLIGHT', 'PRERESUME', 'POSTFLIGHT'):
            self.assertEqual(H.read(runner.output / ('PLAY_INPUT_' + phase + '.json')), runner.recording['inventory'])

    def test_default_actual_producer_scanner_analyze_execute(self): self.execute_success(H.CASES[1])
    def test_no_loan_actual_producer_scanner_analyze_execute(self): self.execute_success(H.CASES[2])

    def test_actual_analyze_preserves_strict_diagnostics_and_probe_schema(self):
        runner, backend = self.virtual_play(H.CASES[1])
        self.assertEqual(runner.execute(), 0)
        stdout = (runner.output / 'A.raw.stdout.log').read_text(encoding='utf-8')
        stderr = (runner.output / 'A.raw.stderr.log').read_text(encoding='utf-8')
        other = backend.stdout('B')
        pair = H.ORDINARY.ERROR_PAIR
        controls = {'additional': stdout + '\nexception: unexpected actual failure',
            'duplicate': stdout.replace(pair[0], pair[0] + '\n' + pair[0]),
            'reordered': stdout.replace(pair[0] + '\n' + pair[1], pair[1] + '\n' + pair[0]),
            'malformed': stdout.replace(pair[0], pair[0] + ' extra'),
            'wrong-phase': stdout.replace('ROS_CONNECT_ALLOWED run=' + PLAY, 'OTHER_PHASE run=' + PLAY)}
        for control in ('extra-json', 'unknown-event', 'wrong-mode'):
            rows = copy.deepcopy(backend.records)
            response = O.one(rows, 'RESUME_RETURNED')
            if control == 'extra-json': response['exception'] = None
            elif control == 'unknown-event': response['event'] = 'UNKNOWN'
            else: response['case'] = H.CASES[0]
            controls[control] = '\n'.join('R35_PROBE ' + json.dumps(row, separators=(',', ':')) for row in rows) + '\n' + \
                '\n'.join(line for line in stdout.splitlines() if not line.startswith('R35_PROBE '))
        for label, candidate in controls.items():
            with self.subTest(control=label):
                (runner.output / (label + '.synthetic.stdout')).write_text(candidate, encoding='utf-8')
                (runner.output / 'A.raw.stdout.log').write_text(candidate, encoding='utf-8')
                with self.assertRaises((ValueError, OSError)): runner.analyze({'A': candidate, 'B': other})
        (runner.output / 'A.raw.stdout.log').write_text(stdout, encoding='utf-8')
        (runner.output / 'A.raw.stderr.log').write_text(stderr + '\nexception: stderr failure', encoding='utf-8')
        with self.assertRaises(ValueError): runner.analyze({'A': stdout, 'B': other})

    def configure_negatives(self, case):
        runner, backend = self.virtual_play(case)
        original = copy.deepcopy(backend.remote)
        db = next(n for n in original if n.endswith('.db3'))
        for control in ('hash', 'size', 'missing', 'extra', 'name'):
            def mutate(inventory):
                if control == 'hash': inventory[db]['sha256'] = '0' * 64
                elif control == 'size': inventory[db]['bytes'] += 1
                elif control == 'missing': del inventory['metadata.yaml']
                elif control == 'extra': inventory['extra.db3'] = dict(inventory[db])
                else: inventory['different.db3'] = inventory.pop(db)
            backend.inventory_fault = mutate
            with self.subTest(control=control), self.assertRaises(ValueError): runner.configure('B')
            self.assertFalse(backend.launched)
            self.assertFalse(backend.resumed)
            self.assertFalse((runner.output / 'B.dispatch.json').exists())
        backend.inventory_fault = None
        backend.after_send = lambda: backend.remote.__setitem__(db, original[db] + b'changed-after-per-file-hash')
        with self.assertRaises(ValueError): runner.configure('B')
        self.assertFalse(backend.launched)
        self.assertFalse((runner.output / 'B.dispatch.json').exists())
        path = Path(runner.recording['collection']['local_directory']) / db
        old = path.read_bytes()
        backend.after_send = lambda: path.write_bytes(old + b'local-drift-after-send-hash')
        try:
            with self.assertRaises(ValueError): runner.configure('B')
            self.assertFalse(backend.launched)
            self.assertFalse((runner.output / 'B.dispatch.json').exists())
        finally:
            path.write_bytes(old)

    def test_default_configure_complete_preflight_rejects_inventory_drift(self): self.configure_negatives(H.CASES[1])
    def test_no_loan_configure_complete_preflight_rejects_inventory_drift(self): self.configure_negatives(H.CASES[2])

    def execute_preflight_failure(self, case):
        runner, backend = self.virtual_play(case)
        def corrupt(inventory):
            inventory[next(n for n in inventory if n.endswith('.db3'))]['sha256'] = '0' * 64
        backend.inventory_fault = corrupt
        self.assertEqual(runner.execute(), 1)
        self.assertFalse(backend.launched)
        self.assertFalse(backend.resumed)
        self.assertFalse((runner.output / 'RESUME_INTENT.json').exists())
        self.assertTrue(any('DEPLOY' in reason for reason in H.read(runner.output / 'RESULT.json')['failures']))

    def test_default_preflight_drift_prevents_startup_through_execute(self): self.execute_preflight_failure(H.CASES[1])
    def test_no_loan_preflight_drift_prevents_startup_through_execute(self): self.execute_preflight_failure(H.CASES[2])

    def before_resume_drift(self, case, local):
        runner, backend = self.virtual_play(case)
        runner.configure('A'); runner.configure('B')
        db = next(n for n in backend.remote if n.endswith('.db3'))
        path = Path(runner.recording['collection']['local_directory']) / db
        old = path.read_bytes()
        if local: path.write_bytes(old + b'local-drift-after-preflight')
        else: backend.remote[db] += b'remote-drift-after-preflight'
        try:
            with self.assertRaises(ValueError): runner.workflow()
            self.assertEqual(backend.launched, {'A', 'B'})
            self.assertFalse(backend.resumed)
            self.assertFalse(runner.resume_attempted)
            self.assertFalse((runner.output / 'RESUME_INTENT.json').exists())
            self.assertLess(time.monotonic(), runner.origin + 35)
        finally:
            if local: path.write_bytes(old)

    def test_default_workflow_local_drift_blocks_unique_resume(self): self.before_resume_drift(H.CASES[1], True)
    def test_default_workflow_remote_drift_blocks_unique_resume(self): self.before_resume_drift(H.CASES[1], False)
    def test_no_loan_workflow_local_drift_blocks_unique_resume(self): self.before_resume_drift(H.CASES[2], True)
    def test_no_loan_workflow_remote_drift_blocks_unique_resume(self): self.before_resume_drift(H.CASES[2], False)

    def test_preresume_checks_cannot_extend_original_business_deadline(self):
        runner, backend = self.virtual_play(H.CASES[1])
        runner.configure('A'); runner.configure('B')
        clock = [time.monotonic()]
        def expire(inventory): clock[0] = runner.stage_deadline + 0.1
        backend.inventory_fault = expire
        with patch.object(H.time, 'monotonic', lambda: clock[0]), self.assertRaisesRegex(ValueError, 'original business budget'):
            runner.workflow()
        self.assertEqual(runner.stage_deadline, runner.origin + 35)
        self.assertFalse(backend.resumed)
        self.assertFalse(runner.resume_attempted)
        self.assertFalse((runner.output / 'RESUME_INTENT.json').exists())

    def test_postplay_drift_still_fails_actual_execute(self):
        runner, backend = self.virtual_play(H.CASES[1])
        db = next(n for n in backend.remote if n.endswith('.db3'))
        backend.after_resume = lambda: backend.remote.__setitem__(db, backend.remote[db] + b'postplay-drift')
        self.assertEqual(runner.execute(), 1)
        result = H.read(runner.output / 'RESULT.json')
        self.assertFalse(result['normal_process_exit'])
        self.assertTrue(any('POSTCLOSE_BAG' in reason for reason in result['failures']))


class DiscoveryTests(Fixtures):
    def leaves(self, case, states, step=0.01):
        probe = self.probe(case)
        node = NS(); probe.node = node
        leaves = PublicGraphLeaves(probe, node, states, step)
        probe.type_names = dict(O.TYPES); probe.deadline = 140.0
        probe.finish_requested = lambda: False
        probe.publishers = {l: NS(get_subscription_count=lambda l=l: leaves.match_count(l)) for l in H.LANES} if probe.recording else {}
        probe.subscriptions = {} if probe.recording else {l: [NS(get_publisher_count=lambda l=l: leaves.match_count(l)) for _ in (False, True)] for l in H.LANES}
        probe.client = NS(service_is_ready=lambda: leaves.current.get('service_ready', True))
        self.assertIsInstance(probe.topic, str)
        self.assertEqual(len(probe.publishers) if probe.recording else sum(map(len, probe.subscriptions.values())), 4 if probe.recording else 8)
        self.assertFalse(probe.seen); self.assertEqual(probe.counts, {l: [0, 0] for l in H.LANES})
        return probe, leaves, io.StringIO()

    def environment(self, leaves, log):
        stack = ExitStack()
        stack.enter_context(redirect_stdout(log))
        stack.enter_context(patch.dict(sys.modules, {'rclpy': leaves.ros, 'rclpy.node': leaves.node_module}))
        stack.enter_context(patch.object(self.probe_module.time, 'monotonic', lambda: leaves.clock[0]))
        stack.enter_context(patch.object(self.probe_module.time, 'monotonic_ns', lambda: int(leaves.clock[0] * 1000000000)))
        return stack

    def wait(self, probe, leaves, log):
        try:
            with self.environment(leaves, log):
                probe.emit('TYPE_SUPPORT', origins={}, capsules={'event': 'PyCapsule', 'string': 'PyCapsule'})
                probe.emit('DEFINITIONS', definitions={})
                probe.emit('NODE_READY', name='/' + probe.node_name)
                probe.wait_ready()
        finally:
            (self.directory('actual-discovery') / 'stdout.log').write_text(log.getvalue(), encoding='utf-8')
        rows = O.rows(log.getvalue(), probe.args.case, probe.args.run, RECORD, os.getpid())
        self.assertFalse(O.probe_business(rows, probe.args.case, probe.args.run, RECORD)['complete'])
        return rows

    def test_four_public_missing_apis_recover_in_all_modes(self):
        for case in H.CASES:
            for kind in PublicGraphLeaves.methods:
                with self.subTest(case=case, api=kind):
                    probe, leaves, log = self.leaves(case, [{'missing': kind}, {'missing': kind}, {}, {}])
                    rows = self.wait(probe, leaves, log)
                    self.assertEqual(leaves.spins, 4)
                    self.assertEqual([r['event'] for r in rows[3:]], ['DISCOVERY_PENDING', 'DISCOVERY', 'READY'])
                    self.assertEqual([r['changed_snapshots'] for r in rows if r['event'] in ('DISCOVERY_PENDING', 'DISCOVERY')], [1, 2])
                    self.assertEqual(O.one(rows, 'READY')['graph'], leaves.good)
                    self.assertEqual(probe.deadline, 140.0)

    def test_missing_between_good_snapshots_requires_two_new_consecutive_full_views(self):
        for case in H.CASES:
            probe, leaves, log = self.leaves(case, [{}, {'missing': 'services'}, {}, {}])
            rows = self.wait(probe, leaves, log)
            self.assertEqual(leaves.spins, 4)
            self.assertEqual([r['event'] for r in rows[3:]], ['DISCOVERY', 'DISCOVERY_PENDING', 'DISCOVERY', 'READY'])
            self.assertEqual([r['changed_snapshots'] for r in rows[3:-1]], [1, 2, 3])

    def test_persistent_missing_uses_original_40s_deadline_without_reset(self):
        for case in H.CASES:
            probe, leaves, log = self.leaves(case, [{'missing': 'publishers'}], step=10)
            with self.assertRaisesRegex(RuntimeError, 'Node-create-return40s'):
                self.wait(probe, leaves, log)
            self.assertEqual((probe.deadline, leaves.clock[0], leaves.spins), (140.0, 140.0, 4))
            rows = O.rows(log.getvalue(), case, probe.args.run, RECORD, os.getpid())
            self.assertEqual([r['event'] for r in rows[3:]], ['DISCOVERY_PENDING'])
            self.assertIsNone(O.probe_business(rows, case, probe.args.run, RECORD)['ready'])
            self.assertFalse(hasattr(probe, 'ready_graph'))

    def test_unexpected_api_errors_and_missing_subclasses_propagate(self):
        class OtherMissing(PublicGraphLeaves.NodeNameNonExistentError):
            pass
        for case in H.CASES:
            for error in (RuntimeError('unexpected graph failure'), ValueError('unexpected graph value'), OtherMissing('unrecognized subclass')):
                probe, leaves, log = self.leaves(case, [{'missing': 'subscriptions', 'raised': error}])
                with self.assertRaises(type(error)) as raised:
                    self.wait(probe, leaves, log)
                self.assertIs(raised.exception, error)
                self.assertEqual(leaves.spins, 1)
                self.assertNotIn('DISCOVERY_PENDING', log.getvalue())

    def test_ready_node_disappearance_is_never_pending(self):
        for case in H.CASES:
            probe, leaves, log = self.leaves(case, [{}, {}])
            self.wait(probe, leaves, log)
            leaves.current = {'missing': 'publishers'}
            before = log.getvalue()
            with self.environment(leaves, log), self.assertRaises(PublicGraphLeaves.NodeNameNonExistentError):
                probe.recheck_ready()
            self.assertEqual(log.getvalue(), before)
            self.assertNotIn('DISCOVERY_PENDING', before)

    def test_combined_64_bound_accepts_64_and_rejects_65_without_reset(self):
        for case in H.CASES:
            states = [{'missing': 'publishers'}, {}] * 32 + [{}]
            probe, leaves, log = self.leaves(case, states)
            rows = self.wait(probe, leaves, log)
            observations = [r for r in rows if r['event'] in ('DISCOVERY', 'DISCOVERY_PENDING')]
            self.assertEqual((len(observations), observations[-1]['changed_snapshots'], leaves.spins), (64, 64, 65))
            probe, leaves, log = self.leaves(case, states[:-1] + [{'missing': 'publishers'}])
            with self.assertRaisesRegex(RuntimeError, 'observation bound'):
                self.wait(probe, leaves, log)
            rows = O.rows(log.getvalue(), case, probe.args.run, RECORD, os.getpid())
            self.assertEqual(len(rows) - 3, 64)
            self.assertFalse(hasattr(probe, 'ready_graph')); self.assertEqual(probe.deadline, 140.0)

    def test_full_graph_foreign_entities_endpoints_counts_and_qos_cannot_ready(self):
        for case in H.CASES:
            run = RECORD if case == H.CASES[0] else PLAY
            for control in ('node', 'ancillary', 'extra-endpoint', 'count', 'owner', 'type', 'qos'):
                snapshot = graph(case, run, RECORD); state = {'snapshot': snapshot}
                item = snapshot['data']['typed_event'][0]
                if control == 'node': snapshot['nodes'].append('/foreign')
                elif control == 'ancillary': snapshot['entities']['/r35bagrec_' + run if case == H.CASES[0] else '/r35bagplayer_' + run]['publishers']['/extra'] = 'std_msgs/msg/String'
                elif control == 'extra-endpoint': snapshot['data']['typed_event'].append(copy.deepcopy(item))
                elif control == 'count': state['counts'] = {'typed_event': 2}
                elif control == 'owner': item['owner'] = '/foreign'
                elif control == 'type': item['type'] = 'std_msgs/msg/String'
                else: item['qos']['durability'] = 'transient_local'
                probe, leaves, log = self.leaves(case, [state], step=10)
                with self.subTest(case=case, control=control), self.assertRaises(RuntimeError):
                    self.wait(probe, leaves, log)
                self.assertFalse(hasattr(probe, 'ready_graph'))
                self.assertNotIn('"event":"READY"', log.getvalue())

    def test_actual_source_endpoints_and_initial_slots_remain_strict(self):
        for control in ('count', 'owner', 'type', 'qos'):
            source = dict(graph(H.CASES[0], RECORD, RECORD)['data']['typed_event'][0], owner='/r35bagpub_' + RECORD, kind='publisher')
            values = [source]
            if control == 'count': values.append(copy.deepcopy(source))
            elif control == 'owner': source['owner'] = '/foreign'
            elif control == 'type': source['type'] = 'std_msgs/msg/String'
            else: source['qos']['reliability'] = 'best_effort'
            probe, leaves, log = self.leaves(H.CASES[0], [{'sources': {'typed_event': values}}])
            with self.assertRaisesRegex(RuntimeError, 'source publisher'):
                self.wait(probe, leaves, log)
        for case in H.CASES[1:]:
            for control in ('seen', 'count'):
                probe, leaves, log = self.leaves(case, [{}])
                if control == 'seen': probe.seen.add(('typed_event', 1, 'raw'))
                else: probe.counts['typed_event'][0] = 1
                with self.assertRaisesRegex(RuntimeError, 'Early replay'):
                    self.wait(probe, leaves, log)

    def test_pending_exact_schema_types_and_combined_ledger_forgery_rejected(self):
        probe, leaves, log = self.leaves(H.CASES[1], [{'missing': 'publishers'}, {}, {}])
        rows = self.wait(probe, leaves, log)
        prefix = rows[:4]
        self.assertIsNone(O.probe_business(prefix, H.CASES[1], PLAY, RECORD)['ready'])
        pending = O.one(rows, 'DISCOVERY_PENDING')
        for field, value in [('known_error_type', None), ('known_error_type', True), ('known_error_type', 'RuntimeError'),
                             ('changed_snapshots', True), ('changed_snapshots', 0), ('changed_snapshots', 65),
                             ('snapshot', leaves.good), ('exception', None), ('unknown', 'extra')]:
            changed = copy.deepcopy(pending); changed[field] = value
            with self.subTest(field=field, value=value), self.assertRaises(ValueError): O.receipt_shape(changed, H.CASES[1])
        for control in ('missing-field', 'skip-count', 'duplicate-pending', 'before-node', 'after-ready', 'no-recovery'):
            changed = copy.deepcopy(rows); p = O.one(changed, 'DISCOVERY_PENDING')
            if control == 'missing-field': del p['known_error_type']
            elif control == 'skip-count': O.one(changed, 'DISCOVERY')['changed_snapshots'] = 3
            elif control == 'duplicate-pending': changed.insert(4, dict(p, seq=5, changed_snapshots=2))
            elif control == 'before-node': p['seq'] = 1
            elif control == 'after-ready': p['seq'] = O.one(changed, 'READY')['seq'] + 1
            else: changed.remove(O.one(changed, 'DISCOVERY'))
            with self.subTest(control=control), self.assertRaises(ValueError): O.probe_business(changed, H.CASES[1], PLAY, RECORD)
        for event in ('FAIL', 'UNKNOWN', 'DISCOVERY_WAIT'):
            bad = dict(pending, event=event)
            candidate = '\n'.join('R35_PROBE ' + json.dumps(r) for r in rows[:3] + [bad])
            with self.assertRaisesRegex(ValueError, 'identity/event'):
                O.rows(candidate, H.CASES[1], PLAY, RECORD, os.getpid())

    def pending_execute(self, case):
        recording = self.sealed_recording()
        prior = self.default_audit(recording) if case == H.CASES[2] else None
        runner, _, _ = self.construct(case, recording, prior)
        producer, rows = self.complete_play_probe(case, bag_seal=runner.recording['seal_sha256'],
            discovery=[{'missing': 'publishers'}, {'missing': 'publishers'}, {}, {}])
        self.assertEqual(self.actual_discovery.spins, 4)
        self.assertEqual(len([r for r in rows if r['event'] == 'DISCOVERY_PENDING']), 1)
        self.assertEqual(len([r for r in rows if r['event'] in ('RAW_RECEIVE', 'TYPED_RECEIVE')]), 16)
        backend = VirtualPlayTransport(runner, rows)
        self.assertEqual(runner.execute(), 0)
        stdout = (runner.output / 'A.raw.stdout.log').read_text(encoding='utf-8')
        stderr = (runner.output / 'A.raw.stderr.log').read_text(encoding='utf-8')
        # Every exact producer JSON line survives into the unchanged scanner input.
        self.assertEqual([s for s in stdout.splitlines() if s.startswith('R35_PROBE ')], producer.splitlines())
        H.R.recordplay_errors(stdout, stderr, PLAY, 'A', runner.owned['A']['child']['pid'], runner.files)
        for word in ('error', 'exception', 'failed'):
            with self.assertRaises((ValueError, OSError)):
                H.R.recordplay_errors(stdout + '\n' + word + ': extra diagnostic', stderr, PLAY, 'A', runner.owned['A']['child']['pid'], runner.files)
            with self.assertRaises((ValueError, OSError)):
                H.R.recordplay_errors(stdout, stderr + '\n' + word + ': extra diagnostic', PLAY, 'A', runner.owned['A']['child']['pid'], runner.files)
        result = H.read(runner.output / 'RESULT.json')
        self.assertEqual((result['experiment_validity'], result['functionality'], result['persistence_fidelity']), ('PASS', 'PASS', 'PASS'))

    def test_pending_default_real_producer_scanner_analyze_execute(self): self.pending_execute(H.CASES[1])
    def test_pending_no_loan_real_producer_scanner_analyze_execute(self): self.pending_execute(H.CASES[2])


class NativeBuiltinGraph(unittest.TestCase):
    """Independent stock-node goldens, not graph_contract-generated fixtures.

    Record entities follow the sanitized complete DISCOVERY seq6 from the
    diagnosed R35 record attempt. Player entities follow rosbag2 0.26.11 Player
    and rclcpp 28.1.22 NodeTimeSource/AsyncParametersClient sources; both play
    profiles share that native Node construction. These are host controls only.
    """
    record_entities = {
        '/r35bagpub_0123456789abcdef': {
            'publishers': {
                '/rosout': 'rcl_interfaces/msg/Log',
                '/parameter_events': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/typed_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/serialized_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/typed_string': 'std_msgs/msg/String',
                '/r35_0123456789abcdef/serialized_string': 'std_msgs/msg/String'},
            'subscriptions': {},
            'services': {'/r35bagpub_0123456789abcdef/get_type_description':
                         'type_description_interfaces/srv/GetTypeDescription'},
            'clients': {}},
        '/r35bagrec_0123456789abcdef': {
            'publishers': {'/rosout': 'rcl_interfaces/msg/Log',
                           '/events/write_split': 'rosbag2_interfaces/msg/WriteSplitEvent'},
            'subscriptions': {
                '/parameter_events': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/typed_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/serialized_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35_0123456789abcdef/typed_string': 'std_msgs/msg/String',
                '/r35_0123456789abcdef/serialized_string': 'std_msgs/msg/String'},
            'services': {
                '/r35bagrec_0123456789abcdef/get_type_description': 'type_description_interfaces/srv/GetTypeDescription',
                '/r35bagrec_0123456789abcdef/describe_parameters': 'rcl_interfaces/srv/DescribeParameters',
                '/r35bagrec_0123456789abcdef/get_parameter_types': 'rcl_interfaces/srv/GetParameterTypes',
                '/r35bagrec_0123456789abcdef/get_parameters': 'rcl_interfaces/srv/GetParameters',
                '/r35bagrec_0123456789abcdef/list_parameters': 'rcl_interfaces/srv/ListParameters',
                '/r35bagrec_0123456789abcdef/set_parameters': 'rcl_interfaces/srv/SetParameters',
                '/r35bagrec_0123456789abcdef/set_parameters_atomically': 'rcl_interfaces/srv/SetParametersAtomically',
                '/r35bagrec_0123456789abcdef/split_bagfile': 'rosbag2_interfaces/srv/SplitBagfile',
                '/r35bagrec_0123456789abcdef/start_discovery': 'rosbag2_interfaces/srv/StartDiscovery',
                '/r35bagrec_0123456789abcdef/stop_discovery': 'rosbag2_interfaces/srv/StopDiscovery',
                '/r35bagrec_0123456789abcdef/is_discovery_running': 'rosbag2_interfaces/srv/IsDiscoveryRunning',
                '/r35bagrec_0123456789abcdef/record': 'rosbag2_interfaces/srv/Record',
                '/r35bagrec_0123456789abcdef/stop': 'rosbag2_interfaces/srv/Stop',
                '/r35bagrec_0123456789abcdef/pause': 'rosbag2_interfaces/srv/Pause',
                '/r35bagrec_0123456789abcdef/resume': 'rosbag2_interfaces/srv/Resume',
                '/r35bagrec_0123456789abcdef/is_paused': 'rosbag2_interfaces/srv/IsPaused'},
            'clients': {}}}
    play_entities = {
        '/r35bagsub_fedcba9876543210': {
            'publishers': {'/rosout': 'rcl_interfaces/msg/Log',
                           '/parameter_events': 'rcl_interfaces/msg/ParameterEvent'},
            'subscriptions': {
                '/r35play_fedcba9876543210/typed_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35play_fedcba9876543210/serialized_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35play_fedcba9876543210/typed_string': 'std_msgs/msg/String',
                '/r35play_fedcba9876543210/serialized_string': 'std_msgs/msg/String'},
            'services': {'/r35bagsub_fedcba9876543210/get_type_description':
                         'type_description_interfaces/srv/GetTypeDescription'},
            'clients': {'/r35bagplayer_fedcba9876543210/resume': 'rosbag2_interfaces/srv/Resume'}},
        '/r35bagplayer_fedcba9876543210': {
            'publishers': {
                '/rosout': 'rcl_interfaces/msg/Log',
                '/parameter_events': 'rcl_interfaces/msg/ParameterEvent',
                '/events/read_split': 'rosbag2_interfaces/msg/ReadSplitEvent',
                '/r35play_fedcba9876543210/typed_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35play_fedcba9876543210/serialized_event': 'rcl_interfaces/msg/ParameterEvent',
                '/r35play_fedcba9876543210/typed_string': 'std_msgs/msg/String',
                '/r35play_fedcba9876543210/serialized_string': 'std_msgs/msg/String'},
            'subscriptions': {'/parameter_events': 'rcl_interfaces/msg/ParameterEvent'},
            'services': {
                '/r35bagplayer_fedcba9876543210/get_type_description': 'type_description_interfaces/srv/GetTypeDescription',
                '/r35bagplayer_fedcba9876543210/describe_parameters': 'rcl_interfaces/srv/DescribeParameters',
                '/r35bagplayer_fedcba9876543210/get_parameter_types': 'rcl_interfaces/srv/GetParameterTypes',
                '/r35bagplayer_fedcba9876543210/get_parameters': 'rcl_interfaces/srv/GetParameters',
                '/r35bagplayer_fedcba9876543210/list_parameters': 'rcl_interfaces/srv/ListParameters',
                '/r35bagplayer_fedcba9876543210/set_parameters': 'rcl_interfaces/srv/SetParameters',
                '/r35bagplayer_fedcba9876543210/set_parameters_atomically': 'rcl_interfaces/srv/SetParametersAtomically',
                '/r35bagplayer_fedcba9876543210/pause': 'rosbag2_interfaces/srv/Pause',
                '/r35bagplayer_fedcba9876543210/resume': 'rosbag2_interfaces/srv/Resume',
                '/r35bagplayer_fedcba9876543210/toggle_paused': 'rosbag2_interfaces/srv/TogglePaused',
                '/r35bagplayer_fedcba9876543210/is_paused': 'rosbag2_interfaces/srv/IsPaused',
                '/r35bagplayer_fedcba9876543210/get_rate': 'rosbag2_interfaces/srv/GetRate',
                '/r35bagplayer_fedcba9876543210/set_rate': 'rosbag2_interfaces/srv/SetRate',
                '/r35bagplayer_fedcba9876543210/play': 'rosbag2_interfaces/srv/Play',
                '/r35bagplayer_fedcba9876543210/play_next': 'rosbag2_interfaces/srv/PlayNext',
                '/r35bagplayer_fedcba9876543210/burst': 'rosbag2_interfaces/srv/Burst',
                '/r35bagplayer_fedcba9876543210/seek': 'rosbag2_interfaces/srv/Seek',
                '/r35bagplayer_fedcba9876543210/stop': 'rosbag2_interfaces/srv/Stop'},
            'clients': {}}}

    @classmethod
    def snapshot(cls, case):
        play = case != 'record_A_to_B'
        owner = '/r35bagplayer_fedcba9876543210' if play else '/r35bagrec_0123456789abcdef'
        entities = copy.deepcopy(cls.play_entities if play else cls.record_entities)
        data = {}
        for lane, typ in (('typed_event', 'rcl_interfaces/msg/ParameterEvent'),
                          ('serialized_event', 'rcl_interfaces/msg/ParameterEvent'),
                          ('typed_string', 'std_msgs/msg/String'), ('serialized_string', 'std_msgs/msg/String')):
            data[lane] = [dict(owner=owner, type=typ, kind='publisher' if play else 'subscription',
                gid='1' * 32, qos=copy.deepcopy(QOS), type_hash='')]
        return dict(nodes=sorted(entities), entities=entities, data=data)

    def validate(self, snapshot, case):
        return O.validate_graph(snapshot, case, RECORD if case == 'record_A_to_B' else PLAY, RECORD)

    def test_record_complete_stock_builtin_golden(self):
        snapshot = self.snapshot('record_A_to_B')
        self.assertEqual(self.validate(snapshot, 'record_A_to_B'), snapshot)
        self.assertEqual(O.graph_contract('record_A_to_B', RECORD, RECORD), snapshot['entities'])

    def test_default_play_source_level_stock_builtin_golden(self):
        snapshot = self.snapshot('play_default_B_to_A')
        self.assertEqual(self.validate(snapshot, 'play_default_B_to_A'), snapshot)
        self.assertEqual(O.graph_contract('play_default_B_to_A', PLAY, RECORD), snapshot['entities'])

    def test_no_loan_play_source_level_stock_builtin_golden(self):
        snapshot = self.snapshot('play_no_loan_B_to_A')
        self.assertEqual(self.validate(snapshot, 'play_no_loan_B_to_A'), snapshot)
        self.assertEqual(O.graph_contract('play_no_loan_B_to_A', PLAY, RECORD), snapshot['entities'])

    def test_builtin_required_exact_type_and_native_owner_all_cases(self):
        for case in ('record_A_to_B', 'play_default_B_to_A', 'play_no_loan_B_to_A'):
            for control in ('missing', 'wrong-type', 'wrong-owner', 'also-on-A'):
                snapshot = self.snapshot(case)
                a, b = (('/r35bagpub_' + RECORD, '/r35bagrec_' + RECORD) if case == 'record_A_to_B' else
                        ('/r35bagsub_' + PLAY, '/r35bagplayer_' + PLAY))
                if control == 'missing': del snapshot['entities'][b]['subscriptions']['/parameter_events']
                elif control == 'wrong-type': snapshot['entities'][b]['subscriptions']['/parameter_events'] = 'std_msgs/msg/String'
                else:
                    if control == 'wrong-owner': del snapshot['entities'][b]['subscriptions']['/parameter_events']
                    snapshot['entities'][a]['subscriptions']['/parameter_events'] = 'rcl_interfaces/msg/ParameterEvent'
                with self.subTest(case=case, control=control), self.assertRaises(ValueError):
                    self.validate(snapshot, case)

    def test_full_graph_extras_and_required_lane_details_stay_strict(self):
        controls = ('unknown-subscription', 'unknown-node', 'unknown-service', 'missing-lane',
                    'extra-endpoint', 'wrong-owner', 'wrong-type', 'wrong-kind', 'zero-gid', 'unreliable-qos')
        for case in ('record_A_to_B', 'play_default_B_to_A', 'play_no_loan_B_to_A'):
            for control in controls:
                snapshot = self.snapshot(case)
                b = '/r35bagrec_' + RECORD if case == 'record_A_to_B' else '/r35bagplayer_' + PLAY
                endpoint = snapshot['data']['typed_event'][0]
                if control == 'unknown-subscription': snapshot['entities'][b]['subscriptions']['/unexpected'] = 'std_msgs/msg/String'
                elif control == 'unknown-node':
                    snapshot['nodes'].append('/unexpected')
                    snapshot['entities']['/unexpected'] = dict(publishers={}, subscriptions={}, services={}, clients={})
                elif control == 'unknown-service': snapshot['entities'][b]['services'][b + '/unexpected'] = 'rosbag2_interfaces/srv/Resume'
                elif control == 'missing-lane': del snapshot['data']['typed_event']
                elif control == 'extra-endpoint': snapshot['data']['typed_event'].append(copy.deepcopy(endpoint))
                elif control == 'wrong-owner': endpoint['owner'] = '/unexpected'
                elif control == 'wrong-type': endpoint['type'] = 'std_msgs/msg/String'
                elif control == 'wrong-kind': endpoint['kind'] = 'publisher' if case == 'record_A_to_B' else 'subscription'
                elif control == 'zero-gid': endpoint['gid'] = '0' * 32
                else: endpoint['qos']['reliability'] = 'best_effort'
                with self.subTest(case=case, control=control), self.assertRaises(ValueError):
                    self.validate(snapshot, case)

    def test_record_parameter_publisher_disabled_and_player_publisher_required(self):
        for case in ('record_A_to_B', 'play_default_B_to_A', 'play_no_loan_B_to_A'):
            snapshot = self.snapshot(case)
            b = '/r35bagrec_' + RECORD if case == 'record_A_to_B' else '/r35bagplayer_' + PLAY
            if case == 'record_A_to_B':
                snapshot['entities'][b]['publishers']['/parameter_events'] = 'rcl_interfaces/msg/ParameterEvent'
            else:
                del snapshot['entities'][b]['publishers']['/parameter_events']
            with self.subTest(case=case), self.assertRaises(ValueError):
                self.validate(snapshot, case)


if __name__ == '__main__':
    unittest.main()
