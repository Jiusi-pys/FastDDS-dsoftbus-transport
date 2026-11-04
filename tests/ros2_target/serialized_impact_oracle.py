"""Independent full-buffer audit. No ROS imports; reports contain no sample bytes."""
import base64
from collections import Counter
import hashlib
import json
import re
import parameter_native_oracle as V1

require, exact, fields = V1.require, V1.exact, V1.fields
one, select, INSTANCES = V1.one, V1.select, V1.INSTANCES
LANES = ('typed_event', 'serialized_event', 'typed_string', 'serialized_string')
MODULES = {'rclpy', 'rclpy._rclpy_pybind11', 'rclpy.publisher', 'rclpy.subscription',
           'rclpy.serialization', 'rclpy.executors', 'rcl_interfaces.msg', 'std_msgs.msg'}
META = {'source_timestamp', 'received_timestamp', 'publication_sequence_number', 'reception_sequence_number'}
COMMON = {'TYPE_SUPPORT', 'NODE_READY', 'DISCOVERY_OBSERVATION', 'BUFFER', 'OBSERVATION_END',
          'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
EVENTS = {'A': COMMON | {'CLIENT_READY', 'LOCAL_DESERIALIZE', 'PUBLISH_RETURN', 'CLIENT_COMPLETE'},
          'B': COMMON | {'SERVER_READY', 'TYPED_RECEIVE', 'RAW_RECEIVE', 'SERVER_COMPLETE'}}
IDENTITY = {'schema', 'scenario', 'instance', 'run', 'side', 'pid', 'seq', 'role', 'monotonic_ns', 'event'}
PAYLOAD = {
    'TYPE_SUPPORT': {'origins', 'capsules'}, 'NODE_READY': {'name'},
    'DISCOVERY_OBSERVATION': {'topics'},
    'CLIENT_READY': {'topics', 'discovery_observations', 'qos'},
    'SERVER_READY': {'topics', 'discovery_observations', 'qos'},
    'BUFFER': {'lane', 'topic', 'index', 'message_type', 'origin', 'cdr_base64', 'cdr_bytes', 'cdr_sha256'},
    'LOCAL_DESERIALIZE': {'lane', 'index', 'buffer_seq', 'value'},
    'PUBLISH_RETURN': {'lane', 'index', 'buffer_seq', 'input_kind'},
    'TYPED_RECEIVE': {'lane', 'topic', 'index', 'value', 'buffer_seq', 'metadata'},
    'RAW_RECEIVE': {'lane', 'topic', 'index', 'value', 'buffer_seq', 'metadata'},
    'CLIENT_COMPLETE': {'publish_calls'}, 'SERVER_COMPLETE': {'received', 'quiet_seconds'},
    'FINISH_ACCEPTED': {'reason'}, 'OBSERVATION_END': {'last_spin_ns', 'receives', 'publish_calls'},
    'NODE_DESTROY_RETURN': {'returned'}, 'SHUTDOWN_RETURN': {'returned'}, 'FINAL': {'success', 'node_created'},
}


def rows(text, scenario, instance, run, pid):
    require(instance in INSTANCES and re.fullmatch('[0-9a-f]{16}', scenario) and
            re.fullmatch('[0-9a-f]{16}', run) and type(pid) is int and pid > 1, 'Invalid process identity')
    result = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=V1.unique,
            parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))
        expected = dict(schema=1, scenario=scenario, instance=instance, run=run, side=instance, pid=pid,
                        seq=len(result) + 1, role='requester' if instance == 'A' else 'responder')
        require(type(row) is dict and all(row.get(k) == v and type(row.get(k)) is type(v) for k, v in expected.items()) and
                row.get('event') in EVENTS[instance] and type(row.get('monotonic_ns')) is int and row['monotonic_ns'] > 0,
                'Invalid or failing process record')
        require(set(row) == IDENTITY | PAYLOAD[row['event']], 'Unexpected record fields')
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Clock regressed')
        result.append(row)
    return result


def expected_value(lane, index, scenario):
    require(lane in LANES and type(index) is int and index in (1, 2), 'Unknown lane/sample')
    if lane.endswith('_string'):
        return {'data': 'R34:' + scenario + ':' + format(index, '02d') + ':control'}
    return {'stamp': {'sec': 100 + index, 'nanosec': 123}, 'node': '/parameter_' + scenario + '_B',
            'new_parameters': [], 'changed_parameters': [{'name': 'p' + scenario + '.bool',
                'value': V1.value(1, index == 2)}], 'deleted_parameters': []}


def audit_buffer(row, lane, index, scenario):
    require(row.get('lane') == lane and row.get('topic') == '/r34_' + scenario + '/' + lane and
            row.get('message_type') == ('String' if lane.endswith('_string') else 'ParameterEvent'), 'Buffer lane/type/topic differs')
    raw = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    require(type(row.get('cdr_bytes')) is int and row['cdr_bytes'] == len(raw) and
            row.get('cdr_sha256') == hashlib.sha256(raw).hexdigest(), 'Buffer length/hash differs')
    reader = V1.CdrReader(raw)
    value = {'data': reader.read('s')} if lane.endswith('_string') else reader.read('ParameterEvent')
    exact(value, expected_value(lane, index, scenario))
    consumed = 4 + reader.offset
    suffix = raw[consumed:]
    return raw, {'cdr_bytes': len(raw), 'cdr_sha256': hashlib.sha256(raw).hexdigest(),
        'schema_consumed_bytes': consumed, 'suffix_bytes': len(suffix),
        'suffix_sha256': hashlib.sha256(suffix).hexdigest(), 'suffix_nonzero_bytes': sum(c != 0 for c in suffix),
        'decoded_fields_equal': True}


def keyed(records, event):
    table = {}
    for row in select(records, event):
        key = (row.get('lane'), row.get('index'))
        require(key[0] in LANES and type(key[1]) is int and key[1] in (1, 2) and key not in table,
                'Foreign or duplicate sample')
        table[key] = row
    require(set(table) == {(lane, index) for lane in LANES for index in (1, 2)}, 'Incomplete keyed sample table')
    return table


def ready(records, side, scenario):
    row = one(records, 'CLIENT_READY' if side == 'A' else 'SERVER_READY')
    remote = '/parameter_' + scenario + ('_B' if side == 'A' else '_A')
    expected = {'matches': [2], 'owners': [remote, remote]} if side == 'A' else {'matches': [1, 1], 'owners': [remote]}
    exact(row.get('topics'), {lane: expected for lane in LANES})
    exact(row.get('qos'), {'history': 1, 'depth': 10, 'reliability': 1, 'durability': 2})
    observations = select(records, 'DISCOVERY_OBSERVATION')
    require(1 <= len(observations) <= 64 and type(row.get('discovery_observations')) is int and
            row['discovery_observations'] == len(observations) and observations[-1]['seq'] < row['seq'], 'Readiness history invalid')
    exact(observations[-1]['topics'], row['topics'])
    return row


def corroborate(typed, raw):
    """Publisher fields can agree across readers; reader-specific counters need not."""
    result = {}
    for field in ('publication_sequence_number', 'source_timestamp'):
        left, right = typed[field], raw[field]
        if left is None or right is None:
            result[field] = 'UNSUPPORTED' if left is right else 'PARTIAL_SUPPORT'
        else:
            require(left == right, 'Conflicting publisher metadata for same sample')
            result[field] = 'MATCHED' if left > 0 else 'ZERO_NOT_CORROBORATING'
    return {'typed': dict(typed), 'raw': dict(raw), 'publisher_fields': result,
            'reader_fields': 'PRESERVED_NOT_COMPARED'}


def business(a, b, scenario, final=False):
    require(re.fullmatch('[0-9a-f]{16}', scenario), 'Invalid scenario')
    a_ready, b_ready = ready(a, 'A', scenario), ready(b, 'B', scenario)
    sent, local = keyed(a, 'PUBLISH_RETURN'), keyed(a, 'LOCAL_DESERIALIZE')
    typed, received = keyed(b, 'TYPED_RECEIVE'), keyed(b, 'RAW_RECEIVE')
    buffers = {side: {r['seq']: r for r in select(records, 'BUFFER')} for side, records in [('A', a), ('B', b)]}
    require(all(len(table) == 8 for table in buffers.values()), 'Exactly sixteen original buffers required')
    used, samples = {'A': set(), 'B': set()}, []
    for lane, index in sent:
        key, expected = (lane, index), expected_value(lane, index, scenario)
        s, l, t, r = sent[key], local[key], typed[key], received[key]
        require(type(s.get('buffer_seq')) is int and type(r.get('buffer_seq')) is int and
                s['buffer_seq'] in buffers['A'] and r['buffer_seq'] in buffers['B'] and
                l.get('buffer_seq') == s['buffer_seq'] and t.get('buffer_seq') is None, 'Buffer reference mismatch')
        for side, ref in [('A', s['buffer_seq']), ('B', r['buffer_seq'])]:
            require(ref not in used[side], 'Buffer reference reused')
            used[side].add(ref)
        source, target = buffers['A'][s['buffer_seq']], buffers['B'][r['buffer_seq']]
        require(source.get('origin') == 'LOCAL_SERIALIZE_MESSAGE' and source.get('index') == index and
                type(source.get('index')) is int and target.get('origin') == 'RAW_SUBSCRIPTION' and
                target.get('index') is None and a_ready['seq'] < source['seq'] < l['seq'] < s['seq'] and
                target['seq'] < r['seq'], 'Capture/deserialize/publish order or origin differs')
        require(s.get('input_kind') == ('bytes' if lane.startswith('serialized_') else 'message'), 'Wrong public publish path')
        for row in (l, t, r):
            exact(row.get('value'), expected)
        for row in (t, r):
            require(row.get('topic') == '/r34_' + scenario + '/' + lane and type(row.get('metadata')) is dict and
                    set(row['metadata']) == META and all(value is None or (type(value) is int and value >= 0)
                    for value in row['metadata'].values()), 'Callback topic/metadata differs')
        source_bytes, source_summary = audit_buffer(source, lane, index, scenario)
        received_bytes, target_summary = audit_buffer(target, lane, index, scenario)
        samples.append({'lane': lane, 'index': index, 'source': source_summary, 'received': target_summary,
                        'serialized_sent_received_equal': source_bytes == received_bytes if lane.startswith('serialized_') else None,
                        'metadata': corroborate(t['metadata'], r['metadata']),
                        'typed_semantics_equal': True, 'full_native_deserialize_equal': True})
    ac, bc = one(a, 'CLIENT_COMPLETE'), one(b, 'SERVER_COMPLETE')
    exact(ac.get('publish_calls'), 8)
    exact(bc.get('received'), 16)
    exact(bc.get('quiet_seconds'), 0.25)
    require(max(r['seq'] for r in sent.values()) < ac['seq'] and b_ready['seq'] < bc['seq'] and
            max(r['seq'] for r in [*typed.values(), *received.values()]) < bc['seq'], 'Completion precedes required observations')
    # The final log includes callbacks after provisional completion. keyed() rejects any extra.
    if final:
        for side, records, complete in [('A', a, ac), ('B', b, bc)]:
            cutoff = one(records, 'OBSERVATION_END')
            finish = one(records, 'FINISH_ACCEPTED')
            require(complete['seq'] < finish['seq'] < cutoff['seq'] and type(cutoff.get('last_spin_ns')) is int and
                    0 < cutoff['last_spin_ns'] <= cutoff['monotonic_ns'], 'Missing final observation cutoff')
            exact(cutoff.get('receives'), 0 if side == 'A' else 16)
            exact(cutoff.get('publish_calls'), 8 if side == 'A' else 0)
    positive = [r for r in samples if r['lane'].endswith('_event')]
    control = [r for r in samples if r['lane'].endswith('_string')]
    return {'experiment_validity': 'VALID_BOUNDED_OBSERVATION', 'semantic_delivery': 'PASS',
        'local_serializer_anomaly': 'REPRODUCED' if all(r['source']['suffix_bytes'] > 0 for r in positive) else 'INCONCLUSIVE',
        'clean_control': 'QUALIFIED' if all(r['source']['cdr_bytes'] == r['received']['cdr_bytes'] == 40 and
            r['source']['suffix_bytes'] == r['received']['suffix_bytes'] == 0 for r in control) else 'INCONCLUSIVE',
        'publish_calls': 8, 'receive_callbacks': 16, 'samples': samples,
        'raw_observation_origin': 'NATIVE_RAW_SUBSCRIPTION_NO_RESERIALIZATION',
        'dds_payload_boundary': 'REQUIRES_SEPARATE_NATIVE_PROVENANCE',
        'bag_recordplay': 'NOT_TESTED', 'general_serialized_compatibility': 'OPEN',
        'final_observation_audited': final}


def finalized(records, package_files):
    support = one(records, 'TYPE_SUPPORT')
    require(set(support.get('origins', {})) == MODULES, 'Module provenance incomplete')
    for row in support['origins'].values():
        require(set(row) == {'member', 'sha256'} and package_files.get(row['member']) == row['sha256'], 'Module outside selected package')
    exact(support.get('capsules'), {'event': 'PyCapsule', 'string': 'PyCapsule'})
    finish, cutoff, destroy, shutdown, final = (one(records, event) for event in
        ('FINISH_ACCEPTED', 'OBSERVATION_END', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'))
    require(finish.get('reason') == 'NORMAL_COMPLETION' and destroy.get('returned') is True and shutdown.get('returned') is True and
            final.get('success') is True and final.get('node_created') is True and
            finish['seq'] < cutoff['seq'] < destroy['seq'] < shutdown['seq'] < final['seq'], 'Normal finalization missing')


def endpoints(texts, scenario):
    require(set(texts) == set(INSTANCES), 'Exact paired endpoint logs required')
    result = {}
    for side in INSTANCES:
        actual = fields(texts[side], 'FDDST_ENDPOINT')
        node = 'parameter_' + scenario + '_' + side
        expected = Counter({('writer', 'rt/parameter_events'): 1, ('writer', 'rt/rosout'): 1,
            ('writer', 'ros_discovery_info'): 1, ('reader', 'ros_discovery_info'): 2,
            ('reader', 'rq/' + node + '/get_type_descriptionRequest'): 1,
            ('writer', 'rr/' + node + '/get_type_descriptionReply'): 1})
        for lane in LANES:
            expected[('writer' if side == 'A' else 'reader', 'rt/r34_' + scenario + '/' + lane)] = 1 if side == 'A' else 2
        if side == 'B':
            for suffix in ('list_parameters', 'get_parameter_types', 'describe_parameters', 'get_parameters',
                           'set_parameters', 'set_parameters_atomically'):
                expected[('reader', 'rq/' + node + '/' + suffix + 'Request')] = 1
                expected[('writer', 'rr/' + node + '/' + suffix + 'Reply')] = 1
        require(all(set(row) == {'role', 'topic', 'data_sharing'} and row['data_sharing'] == 'OFF' for row in actual) and
                Counter((row['role'], row['topic']) for row in actual) == expected, 'Unexpected/missing endpoint or sharing mode')
        result[side] = actual
    return result
