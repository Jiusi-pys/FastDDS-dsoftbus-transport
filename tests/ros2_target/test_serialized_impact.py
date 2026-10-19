"""Synthetic host fixtures only. These tests are not native or device evidence."""
import base64
import ast
import asyncio
import copy
import hashlib
import importlib.util
import inspect
import json
from pathlib import Path
import sys
import time
from types import SimpleNamespace, ModuleType
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import serialized_impact_oracle as O
import run_serialized_impact as H
from test_parameter_native import encode

SCENARIO, RUN = '0123456789abcdef', 'abcdef0123456789'
spec = importlib.util.spec_from_file_location('impact_probe_fixture', H.PROBE)
PROBE = importlib.util.module_from_spec(spec)
spec.loader.exec_module(PROBE)


def encoded(lane, index, suffix=b''):
    value = O.expected_value(lane, index, SCENARIO)
    raw = encode('s', value['data']) if lane.endswith('_string') else encode('ParameterEvent', value)
    return raw + suffix


def capture(lane, index, origin, suffix=b''):
    raw = encoded(lane, index, suffix)
    return dict(lane=lane, topic='/r34_' + SCENARIO + '/' + lane,
        index=index if origin == 'LOCAL_SERIALIZE_MESSAGE' else None,
        message_type='String' if lane.endswith('_string') else 'ParameterEvent', origin=origin,
        cdr_base64=base64.b64encode(raw).decode(), cdr_bytes=len(raw), cdr_sha256=hashlib.sha256(raw).hexdigest())


def append(records, side, event, **fields):
    row = dict(schema=1, scenario=SCENARIO, instance=side, run=RUN, side=side, pid=10 if side == 'A' else 11,
        seq=len(records) + 1, role='requester' if side == 'A' else 'responder', monotonic_ns=(len(records) + 1) * 1000000000,
        event=event, **fields)
    records.append(row)
    return row['seq']


def fixtures(anomaly=True, changed=False):
    a, b = [], []
    for side, records in [('A', a), ('B', b)]:
        remote = '/parameter_' + SCENARIO + ('_B' if side == 'A' else '_A')
        topics = {lane: {'matches': [2], 'owners': [remote, remote]} if side == 'A' else
                  {'matches': [1, 1], 'owners': [remote]} for lane in O.LANES}
        append(records, side, 'DISCOVERY_OBSERVATION', topics=topics)
        append(records, side, 'CLIENT_READY' if side == 'A' else 'SERVER_READY', topics=topics,
               discovery_observations=1, qos=dict(history=1, depth=10, reliability=1, durability=2))
    for index in (1, 2):
        for lane in O.LANES:
            suffix = b'fixture!' if anomaly and lane.endswith('_event') else b''
            source = append(a, 'A', 'BUFFER', **capture(lane, index, 'LOCAL_SERIALIZE_MESSAGE', suffix))
            append(a, 'A', 'LOCAL_DESERIALIZE', lane=lane, index=index, buffer_seq=source, value=O.expected_value(lane, index, SCENARIO))
            append(a, 'A', 'PUBLISH_RETURN', lane=lane, index=index, buffer_seq=source,
                   input_kind='bytes' if lane.startswith('serialized_') else 'message')
            target = append(b, 'B', 'BUFFER', **capture(lane, index, 'RAW_SUBSCRIPTION', b'changed!' if changed and suffix else suffix))
            for raw in (False, True):
                append(b, 'B', 'RAW_RECEIVE' if raw else 'TYPED_RECEIVE', lane=lane, index=index,
                    topic='/r34_' + SCENARIO + '/' + lane, value=O.expected_value(lane, index, SCENARIO),
                    buffer_seq=target if raw else None, metadata={name: None for name in O.META})
    append(a, 'A', 'CLIENT_COMPLETE', publish_calls=8)
    append(b, 'B', 'SERVER_COMPLETE', received=16, quiet_seconds=0.25)
    for side, records in [('A', a), ('B', b)]:
        append(records, side, 'FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
        append(records, side, 'OBSERVATION_END', last_spin_ns=records[-1]['monotonic_ns'],
               receives=0 if side == 'A' else 16, publish_calls=8 if side == 'A' else 0)
    return a, b


def probe(side='A'):
    args = SimpleNamespace(scenario=SCENARIO, side=side, instance=side, run=RUN,
        role='requester' if side == 'A' else 'responder',
        finish_control=str(Path('/data/local/tmp/mdds_dds_' + RUN + '_' + side) / 'control.PROBE_FINISH'))
    # Execute all actual constructors. Only the device package manifest is a fixture.
    with patch.object(Path, 'read_text', return_value='{"files":{}}'):
        p = PROBE.SerializedImpactProbe(args)
    p.records, p.calls, p.done, p.deadline = [], 0, False, time.monotonic() + 40
    p.last_receive, p.last_spin_ns, p.qos = None, None, object()
    p.classes = {'event': dict, 'string': dict}
    p.message = lambda lane, index: O.expected_value(lane, index, SCENARIO)
    def emit(event, **fields):
        p.sequence += 1
        p.records.append(dict(event=event, seq=p.sequence, **fields))
    p.emit = emit
    return p


class OracleTests(unittest.TestCase):
    def test_full_buffers_and_redacted_result(self):
        a, b = fixtures()
        result = O.business(a, b, SCENARIO, final=True)
        self.assertEqual(result['local_serializer_anomaly'], 'REPRODUCED')
        self.assertEqual(result['clean_control'], 'QUALIFIED')
        self.assertTrue(all(s['serialized_sent_received_equal'] is True for s in result['samples'] if s['lane'].startswith('serialized_')))
        text = json.dumps(result)
        self.assertNotIn('base64', text)
        self.assertNotIn('fixture!', text)

    def test_absent_or_changed_suffix_is_observation(self):
        self.assertEqual(O.business(*fixtures(False), SCENARIO)['local_serializer_anomaly'], 'INCONCLUSIVE')
        result = O.business(*fixtures(changed=True), SCENARIO)
        self.assertTrue(all(s['serialized_sent_received_equal'] is False for s in result['samples'] if s['lane'] == 'serialized_event'))

    def test_duplicate_including_late_rejected(self):
        for event in ('TYPED_RECEIVE', 'RAW_RECEIVE'):
            a, b = fixtures()
            duplicate = copy.deepcopy(O.select(b, event)[0])
            duplicate['seq'] = len(b) + 1
            b.append(duplicate)
            with self.assertRaises(ValueError): O.business(a, b, SCENARIO, final=True)

    def test_wrong_field_type_index_lane_metadata_and_topic(self):
        mutations = [lambda r: r.update(index=True), lambda r: r.update(lane='foreign'),
            lambda r: r.update(topic='/other'), lambda r: r['metadata'].update(publication_sequence_number=True),
            lambda r: r['value']['stamp'].update(nanosec=123.0),
            lambda r: r['value']['changed_parameters'][0]['value'].update(integer_value=1)]
        for mutate in mutations:
            a, b = fixtures()
            mutate(O.select(b, 'RAW_RECEIVE')[0])
            with self.assertRaises(ValueError): O.business(a, b, SCENARIO)

    def test_corrupt_hash_length_truncation_and_cdr(self):
        for mode in ('hash', 'length', 'truncated', 'header', 'boolean', 'utf8'):
            a, b = fixtures()
            row = O.select(a, 'BUFFER')[0]
            raw = bytearray(base64.b64decode(row['cdr_base64']))
            if mode == 'hash': row['cdr_sha256'] = '0' * 64
            elif mode == 'length': row['cdr_bytes'] += 1
            else:
                if mode == 'truncated': raw = raw[:30]
                elif mode == 'header': raw[1] = 3
                elif mode == 'boolean':
                    offset = raw.index(b'p' + SCENARIO.encode() + b'.bool') + len('p' + SCENARIO + '.bool') + 2
                    raw[offset] = 2
                else: raw[16] = 255
                row.update(cdr_base64=base64.b64encode(raw).decode(), cdr_bytes=len(raw), cdr_sha256=hashlib.sha256(raw).hexdigest())
            with self.assertRaises((ValueError, UnicodeError)): O.business(a, b, SCENARIO)

    def test_nonzero_alignment_accepted(self):
        row = capture('typed_event', 1, 'LOCAL_SERIALIZE_MESSAGE')
        raw = bytearray(base64.b64decode(row['cdr_base64']))
        # node text terminates before the aligned new_parameters length.
        end = raw.index(b'/parameter_' + SCENARIO.encode() + b'_B') + len('/parameter_' + SCENARIO + '_B') + 1
        for offset in range(end, 4 + ((end - 4 + 3) // 4) * 4): raw[offset] = 170
        row.update(cdr_base64=base64.b64encode(raw).decode(), cdr_sha256=hashlib.sha256(raw).hexdigest())
        O.audit_buffer(row, 'typed_event', 1, SCENARIO)

    def test_receive_order_is_not_cross_topic_order(self):
        a, b = fixtures()
        # Swap complete blocks, preserving each local buffer -> callback ordering.
        middle = b[2:26]
        b[2:26] = middle[12:] + middle[:12]
        refs = {}
        for i, row in enumerate(b, 1):
            if row['event'] == 'BUFFER': refs[row['seq']] = i
            elif row['event'] == 'RAW_RECEIVE': row['buffer_seq'] = refs[row['buffer_seq']]
            row['seq'] = i
        O.business(a, b, SCENARIO)

    def test_strict_record_identity_fields_and_failure(self):
        a, _ = fixtures()
        text = '\n'.join('R25_PROBE ' + json.dumps(r) for r in a)
        self.assertEqual(O.rows(text, SCENARIO, 'A', RUN, 10), a)
        for field, value in [('side', 'B'), ('extra', 1), ('event', 'FAIL'), ('schema', True)]:
            corrupted = copy.deepcopy(a)
            corrupted[0][field] = value
            with self.assertRaises(ValueError): O.rows('\n'.join('R25_PROBE ' + json.dumps(r) for r in corrupted), SCENARIO, 'A', RUN, 10)

    def test_publisher_metadata_conflicts_and_independent_reader_values(self):
        def with_metadata():
            a, b = fixtures()
            for row in b:
                if row['event'] in ('TYPED_RECEIVE', 'RAW_RECEIVE'):
                    raw = row['event'] == 'RAW_RECEIVE'
                    row['metadata'] = dict(publication_sequence_number=row['index'], source_timestamp=1000000 + row['index'],
                        reception_sequence_number=row['index'] + (10 if raw else 0), received_timestamp=2000000 + (10 if raw else 0))
            return a, b
        def validated(a, b):
            return O.business(*[O.rows('\n'.join('R25_PROBE ' + json.dumps(r) for r in records), SCENARIO, side, RUN, pid)
                for records, side, pid in ((a, 'A', 10), (b, 'B', 11))], SCENARIO, final=True)
        result = validated(*with_metadata())
        self.assertEqual(result['samples'][0]['metadata']['publisher_fields']['source_timestamp'], 'MATCHED')
        self.assertEqual(result['samples'][0]['metadata']['reader_fields'], 'PRESERVED_NOT_COMPARED')
        self.assertEqual(validated(*fixtures())['samples'][0]['metadata']['publisher_fields']['source_timestamp'], 'UNSUPPORTED')
        for field in ('source_timestamp', 'publication_sequence_number'):
            a, b = with_metadata()
            O.select(b, 'RAW_RECEIVE')[0]['metadata'][field] += 19
            with self.assertRaises(ValueError): validated(a, b)


class ProbeTests(unittest.TestCase):
    def test_constructed_server_creates_typed_and_raw_subscriptions(self):
        p, created = probe('B'), []
        def create_subscription(cls, topic, callback, qos, *, raw):
            created.append((topic, raw, callback))
            return object()
        p.node = SimpleNamespace(create_subscription=create_subscription)
        p.wait_ready = Mock()
        p.tick = lambda: False
        p.server_work()
        self.assertEqual([(topic, raw) for topic, raw, _ in created],
            [('/r34_' + SCENARIO + '/' + lane, raw) for lane in O.LANES for raw in (False, True)])
        self.assertTrue(all(callable(callback) for _, _, callback in created))
        p.wait_ready.assert_called_once_with()

    def test_public_call_identity_and_single_serialization(self):
        p, sent, serialized, consumed = probe(), [], [], []
        p.wait_ready = lambda: None
        p.node = SimpleNamespace(create_publisher=lambda cls, topic, qos: SimpleNamespace(publish=lambda value: sent.append((topic, value))))
        api = ModuleType('rclpy.serialization')
        def serialize(message):
            data = b'host fixture ' + bytes([len(serialized)])
            serialized.append((message, data))
            return data
        def deserialize(data, cls):
            consumed.append(data)
            return next(message for message, buf in serialized if buf is data)
        api.serialize_message, api.deserialize_message = serialize, deserialize
        with patch.dict(sys.modules, {'rclpy.serialization': api}), patch.object(PROBE, 'plain', lambda x: x):
            p.client_work()
        self.assertEqual(len(serialized), 8)
        for i, (topic, value) in enumerate(sent):
            original, full = serialized[i]
            self.assertIs(consumed[i], full)
            self.assertIs(value, full if '/serialized_' in topic else original)

    def test_raw_callback_never_reserializes_and_keeps_full_bytes(self):
        p, api = probe('B'), ModuleType('rclpy.serialization')
        data = encoded('serialized_event', 1, b'fixture!')
        api.serialize_message = Mock(side_effect=AssertionError('Raw callback reserialized'))
        api.deserialize_message = Mock(return_value=O.expected_value('serialized_event', 1, SCENARIO))
        info = {name: None for name in O.META}
        with patch.dict(sys.modules, {'rclpy.serialization': api}), patch.object(PROBE, 'plain', lambda x: x):
            callback = p.callback('serialized_event', True)
            callback(data, info)
            self.assertIs(api.deserialize_message.call_args.args[0], data)
            p.done = True
            with self.assertRaises(RuntimeError): callback(data, info)
        self.assertEqual(api.serialize_message.call_count, 0)
        self.assertEqual(base64.b64decode(p.records[0]['cdr_base64']), data)

    def test_raw_evidence_precedes_deserializer_failure(self):
        p, api = probe('B'), ModuleType('rclpy.serialization')
        api.deserialize_message = Mock(side_effect=RuntimeError('Synthetic native failure'))
        with patch.dict(sys.modules, {'rclpy.serialization': api}):
            with self.assertRaises(RuntimeError): p.callback('typed_string', True)(encoded('typed_string', 1), None)
        self.assertEqual(p.records[0]['event'], 'BUFFER')

    def test_deadline_after_evidence_write(self):
        p = probe()
        def slow_emit(event, **fields): p.deadline = time.monotonic() - 1
        p.emit = slow_emit
        with self.assertRaises(RuntimeError): p.record('BUFFER')

    def test_readiness_transient_unknown_owner(self):
        p = probe()
        p.context_is_active = lambda: True
        count = [0]
        p.tick = lambda: count.__setitem__(0, count[0] + 1) or True
        p.publishers = {lane: SimpleNamespace(get_subscription_count=lambda: 2) for lane in O.LANES}
        p.node = SimpleNamespace(get_subscriptions_info_by_topic=lambda topic: [SimpleNamespace(node_namespace='/',
            node_name='unknown' if count[0] == 0 else 'parameter_' + SCENARIO + '_B')] * 2)
        p.wait_ready()
        self.assertEqual(count[0], 1)
        self.assertEqual(p.records[-1]['event'], 'CLIENT_READY')
        self.assertEqual(p.records[-1]['discovery_observations'], 2)

    def test_finish_or_context_loss_during_discovery(self):
        for finish in (False, True):
            p, state, rclpy = probe(), {'ok': True, 'finish': False}, ModuleType('rclpy')
            rclpy.ok = lambda: state['ok']
            p.finish_requested = lambda: state['finish']
            p.publishers = {lane: SimpleNamespace(get_subscription_count=lambda: 2) for lane in O.LANES}
            def graph(topic):
                state.update(ok=finish, finish=finish)
                return [SimpleNamespace(node_namespace='/', node_name='parameter_' + SCENARIO + '_B')] * 2
            p.node = SimpleNamespace(get_subscriptions_info_by_topic=graph)
            with patch.dict(sys.modules, {'rclpy': rclpy}):
                with self.assertRaises(RuntimeError): p.wait_ready()
            self.assertFalse(any(r['event'] == 'CLIENT_READY' for r in p.records))

    def test_selected_executor_passes_native_dict_to_both_callbacks(self):
        # Exact selected Python function bodies; only annotations are removed.
        # Native handle is synthetic, so this remains HOST_ONLY.
        package = Path(H.read(H.ROOT / H.PINS['derivation'][0])['package'])
        source = package / 'python/rclpy/executors.py'
        tree = ast.parse(source.read_text(encoding='utf-8'))
        nodes = [copy.deepcopy(next(n for n in ast.walk(tree) if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and n.name == name))
                 for name in ('await_or_execute', '_take_subscription')]
        module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
        for node in ast.walk(module):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                node.returns = None
                for arg in [*node.args.args, *node.args.kwonlyargs, node.args.vararg, node.args.kwarg]:
                    if arg is not None: arg.annotation = None
        namespace = {'inspect': inspect, 'InvalidHandle': RuntimeError,
                     'Subscription': SimpleNamespace(CallbackType=SimpleNamespace(MessageOnly=object()))}
        exec(compile(module, str(source), 'exec'), namespace)
        class Handle:
            def __enter__(self): return self
            def __exit__(self, *args): return False
            def take_message(self, msg_type, raw): return self.sample
        for raw in (False, True):
            p, api, handle = probe('B'), ModuleType('rclpy.serialization'), Handle()
            api.deserialize_message = lambda data, cls: O.expected_value('typed_string', 1, SCENARIO)
            handle.sample = (encoded('typed_string', 1) if raw else O.expected_value('typed_string', 1, SCENARIO), {k: None for k in O.META})
            sub = SimpleNamespace(handle=handle, msg_type=dict, raw=raw, _callback_type=object(), callback=p.callback('typed_string', raw))
            with patch.dict(sys.modules, {'rclpy.serialization': api}), patch.object(PROBE, 'plain', lambda x: x):
                asyncio.run(namespace['_take_subscription'](object(), sub)())
            self.assertEqual(sum(r['event'].endswith('_RECEIVE') for r in p.records), 1)

    def test_selected_public_publish_dispatch(self):
        package = Path(H.read(H.ROOT / H.PINS['derivation'][0])['package'])
        source = package / 'python/rclpy/publisher.py'
        tree = ast.parse(source.read_text(encoding='utf-8'))
        method = copy.deepcopy(next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == 'publish'))
        method.returns = None
        for arg in method.args.args: arg.annotation = None
        cls = ast.ClassDef(name='Publisher', bases=[], keywords=[], body=[method], decorator_list=[], type_params=[])
        module = ast.fix_missing_locations(ast.Module(body=[cls], type_ignores=[]))
        namespace = {}
        exec(compile(module, str(source), 'exec'), namespace)
        native = Mock()
        class Handle:
            def __enter__(self): return self
            def __exit__(self, *args): return False
        pub = namespace['Publisher']()
        pub.handle, pub.msg_type, pub._Publisher__publisher = Handle(), dict, native
        message, data = {}, b'synthetic\0suffix'
        pub.publish(message)
        pub.publish(data)
        self.assertIs(native.publish.call_args.args[0], message)
        self.assertIs(native.publish_raw.call_args.args[0], data)
        self.assertEqual(native.publish.call_count, 1)
        self.assertEqual(native.publish_raw.call_count, 1)


class AdmissionTests(unittest.TestCase):
    def test_fixed_scope_and_bounds(self):
        self.assertEqual(H.LIMITS, H.T.LIMITS)
        with self.assertRaises(ValueError): H.checked_acceptance(H.T.V2)
        with self.assertRaises(ValueError): H.validate_plan({'schema': True})

    def test_no_device_or_output_before_admission(self):
        with patch.object(H, 'fresh', return_value=Path('never-created')), patch.object(H, 'checked_plan', side_effect=ValueError('fixture rejection')), patch.object(H.BAG, 'bag_selected_hdc') as hdc:
            with self.assertRaises(ValueError): H.ImpactScene(SimpleNamespace(output=Path('unused'), prepared=Path('unused')))
            hdc.assert_not_called()


if __name__ == '__main__':
    unittest.main()
