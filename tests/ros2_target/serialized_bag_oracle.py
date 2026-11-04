"""R35 whole-buffer and immutable SQLite audit; never grants device admission."""
import base64
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import sqlite3

import bag_sqlite_oracle as SQL
import parameter_native_oracle as CDR

SCOPE = 'R35_PHASED_STOCK_BAG'
CASES = ('record_A_to_B', 'play_default_B_to_A', 'play_no_loan_B_to_A')
LANES = ('typed_event', 'serialized_event', 'typed_string', 'serialized_string')
META = {'source_timestamp', 'received_timestamp', 'publication_sequence_number', 'reception_sequence_number'}
require, exact = CDR.require, CDR.exact
TYPES = {lane: 'std_msgs/msg/String' if lane.endswith('string') else 'rcl_interfaces/msg/ParameterEvent' for lane in LANES}


def unique(items):
    result = {}
    for key, value in items:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def topic(run, lane, play=False):
    require(re.fullmatch('[0-9a-f]{16}', run) and lane in LANES, 'Unknown topic identity')
    return ('/r35play_' if play else '/r35_') + run + '/' + lane


def expected_value(lane, index, record_run):
    require(lane in LANES and type(index) is int and index in (1, 2), 'Unknown sample')
    if lane.endswith('string'):
        return {'data': 'R35:' + record_run + ':' + format(index, '02d') + ':control'}
    return {'stamp': {'sec': 100 + index, 'nanosec': 123}, 'node': '/parameter_' + record_run + '_B',
            'new_parameters': [], 'changed_parameters': [{'name': 'p' + record_run + '.bool',
            'value': CDR.value(1, index == 2)}], 'deleted_parameters': []}


def inspect_buffer(data, lane, record_run, index=None):
    require(lane in LANES and re.fullmatch('[0-9a-f]{16}', record_run) and
            type(data) is bytes and 4 <= len(data) <= 65536, 'Whole buffer identity/size/type')
    reader = CDR.CdrReader(data)
    value = {'data': reader.read('s')} if lane.endswith('string') else reader.read('ParameterEvent')
    indices = [i for i in (1, 2) if SQL._typed_equal(value, expected_value(lane, i, record_run))]
    require(len(indices) == 1 and (index is None or index == indices[0]), 'Unexpected full message fields/index')
    consumed = 4 + reader.offset
    tail = data[consumed:]
    require(not lane.endswith('string') or not tail, 'String control has a suffix')
    return indices[0], value, dict(cdr_bytes=len(data), cdr_sha256=digest(data),
        schema_consumed_bytes=consumed, suffix_bytes=len(tail), suffix_sha256=digest(tail),
        suffix_nonzero_bytes=sum(x != 0 for x in tail), decoded_fields_equal=True)


def buffer_row(row, record_run):
    require(row.get('lane') in LANES, 'Unknown buffer lane')
    data = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    require(type(row.get('cdr_bytes')) is int and len(data) == row['cdr_bytes'] and
            digest(data) == row.get('cdr_sha256'), 'Whole buffer receipt mismatch')
    index, value, report = inspect_buffer(data, row['lane'], record_run, row.get('index'))
    return data, index, value, report


def writer_timing(timestamps):
    """Successful cache0 writer update order, protected by Writer::writer_mutex_."""
    require(bool(timestamps) and all(type(x) is int and x > 0 for x in timestamps), 'Timestamp structure')
    start, duration = timestamps[0], 0
    for timestamp in timestamps:
        start = min(start, timestamp)
        duration = max(duration, timestamp - start)
    return start, duration


def definitions(package):
    """Independent MSG dependency expansion; no native execution is implied."""
    package = Path(package)
    primitive = {'bool', 'byte', 'char', 'float32', 'float64', 'int8', 'uint8', 'int16',
                 'uint16', 'int32', 'uint32', 'int64', 'uint64', 'string', 'wstring'}
    result = {}
    for root in sorted(set(TYPES.values())):
        seen = set()

        def append(name, depth=0):
            require(depth < 16, 'Definition recursion bound')
            parts = name.split('/')
            pkg, leaf = parts[0], parts[-1]
            index = package / 'share/ament_index/resource_index/rosidl_interfaces' / pkg
            member = 'msg/' + leaf + '.msg'
            require(member in index.read_text(encoding='utf-8').splitlines(), 'Definition index missing')
            text = (package / 'share' / pkg / member).read_bytes().decode('utf-8')
            deps = set()
            for line in text.splitlines():
                line = line.split('#', 1)[0].strip()
                if line:
                    token = line.split()[0].split('[', 1)[0]
                    if token not in primitive:
                        deps.add(token if '/' in token else pkg + '/' + token)
            for dep in sorted(deps):
                if dep not in seen:
                    seen.add(dep)
                    text += '\n' + '=' * 80 + '\nMSG: ' + dep + '\n' + append(dep, depth + 1)
            return text

        text = append(root)
        result[root] = {'encoding': 'ros2msg', 'encoded_message_definition': text}
    return result


def graph_contract(case, run, record_run):
    play = case != CASES[0]
    a = '/r35bagsub_' + run if play else '/r35bagpub_' + run
    b = '/r35bagplayer_' + run if play else '/r35bagrec_' + run
    params = {'describe_parameters': 'DescribeParameters', 'get_parameter_types': 'GetParameterTypes',
              'get_parameters': 'GetParameters', 'list_parameters': 'ListParameters',
              'set_parameters': 'SetParameters', 'set_parameters_atomically': 'SetParametersAtomically'}
    bag = {n: ''.join(x.title() for x in n.split('_')) for n in
           (('pause', 'resume', 'toggle_paused', 'is_paused', 'get_rate', 'set_rate', 'play',
             'play_next', 'burst', 'seek', 'stop') if play else
            ('split_bagfile', 'start_discovery', 'stop_discovery', 'is_discovery_running', 'record',
             'stop', 'pause', 'resume', 'is_paused'))}
    result = {}
    for owner in (a, b):
        pubs = {'/rosout': 'rcl_interfaces/msg/Log'}
        if owner == a or play:
            pubs['/parameter_events'] = 'rcl_interfaces/msg/ParameterEvent'
        services = {owner + '/get_type_description': 'type_description_interfaces/srv/GetTypeDescription'}
        if owner == b:
            services.update({owner + '/' + n: 'rcl_interfaces/srv/' + t for n, t in params.items()})
            services.update({owner + '/' + n: 'rosbag2_interfaces/srv/' + t for n, t in bag.items()})
            pubs['/events/read_split' if play else '/events/write_split'] = (
                'rosbag2_interfaces/msg/ReadSplitEvent' if play else 'rosbag2_interfaces/msg/WriteSplitEvent')
        # Native NodeTimeSource subscribes through AsyncParametersClient even
        # when Recorder disables the parameter-event publisher.
        subs = {'/parameter_events': 'rcl_interfaces/msg/ParameterEvent'} if owner == b else {}
        for lane in LANES:
            dest = topic(run if play else record_run, lane, play)
            (pubs if (owner == b) == play else subs)[dest] = TYPES[lane]
        result[owner] = {'publishers': pubs, 'subscriptions': subs, 'services': services,
                         'clients': {b + '/resume': 'rosbag2_interfaces/srv/Resume'} if play and owner == a else {}}
    return result


def validate_graph(snapshot, case, run, record_run):
    require(type(snapshot) is dict and set(snapshot) == {'nodes', 'entities', 'data'}, 'Graph receipt shape')
    contract = graph_contract(case, run, record_run)
    exact(snapshot['nodes'], sorted(contract))
    exact(snapshot['entities'], contract)
    require(set(snapshot['data']) == set(LANES), 'Four primary data topics required')
    owner = '/r35bagplayer_' + run if case != CASES[0] else '/r35bagrec_' + run
    for lane, entries in snapshot['data'].items():
        require(type(entries) is list and len(entries) == 1, 'Extra/missing primary data endpoint')
        item = entries[0]
        require(set(item) == {'owner', 'type', 'kind', 'gid', 'qos', 'type_hash'} and item['owner'] == owner and
                item['type'] == TYPES[lane] and item['kind'] == ('publisher' if case != CASES[0] else 'subscription') and
                re.fullmatch('[0-9a-f]{32}', item['gid']) and int(item['gid'], 16) != 0, 'Data endpoint identity')
        q = item['qos']
        require(q['reliability'] == 'reliable' and q['durability'] == 'volatile' and q['liveliness'] == 'automatic' and
                q['history'] == 'unknown' and type(q['depth']) is int and q['depth'] == 0 and
                q['avoid_ros_namespace_conventions'] is False, 'Selected observed graph QoS')
        require(item['type_hash'] == '' or re.fullmatch('RIHS01_[0-9a-f]{64}', item['type_hash']), 'Observed type hash')
    return snapshot


EVENTS = {'TYPE_SUPPORT', 'DEFINITIONS', 'NODE_READY', 'DISCOVERY', 'DISCOVERY_PENDING', 'READY', 'BUFFER', 'NATIVE_DECODE',
          'PUBLISH_RETURN', 'ACKED', 'RECORD_COMPLETE', 'RESUME_ACCEPTED', 'RESUME_CALLED', 'RESUME_RETURNED',
          'TYPED_RECEIVE', 'RAW_RECEIVE', 'PLAY_COMPLETE', 'OBSERVATION_END', 'FINISH_ACCEPTED',
          'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
EVENT_FIELDS = {
    'TYPE_SUPPORT': {'origins', 'capsules'}, 'DEFINITIONS': {'definitions'}, 'NODE_READY': {'name'},
    'DISCOVERY': {'snapshot', 'changed_snapshots'},
    'DISCOVERY_PENDING': {'known_error_type', 'changed_snapshots'},
    'READY': {'graph', 'counts', 'slots_empty', 'requested_qos', 'source_endpoints'},
    'BUFFER': {'lane', 'index', 'origin', 'cdr_base64', 'cdr_bytes', 'cdr_sha256'},
    'NATIVE_DECODE': {'buffer_seq', 'lane', 'index', 'value'},
    'PUBLISH_RETURN': {'lane', 'index', 'buffer_seq', 'input_kind'}, 'ACKED': {'lane', 'returned', 'timeout_ns'},
    'RECORD_COMPLETE': {'published', 'acknowledged', 'settle_seconds'},
    'RESUME_ACCEPTED': {'bag_seal_sha256'}, 'RESUME_CALLED': {'service'},
    'RESUME_RETURNED': {'response_type', 'fields', 'future_done', 'future_cancelled', 'future_raised'},
    'TYPED_RECEIVE': {'lane', 'index', 'value', 'metadata'},
    'RAW_RECEIVE': {'lane', 'index', 'value', 'metadata', 'buffer_seq'},
    'PLAY_COMPLETE': {'callbacks', 'counters', 'quiet_seconds'},
    'OBSERVATION_END': {'callbacks', 'published', 'last_spin_ns', 'cutoff_ns'},
    'FINISH_ACCEPTED': {'reason'}, 'NODE_DESTROY_RETURN': {'returned'}, 'SHUTDOWN_RETURN': {'returned'},
    'FINAL': {'success', 'node_created'}}
COMMON_FIELDS = {'schema', 'scope', 'case', 'run', 'record_run', 'side', 'pid', 'role', 'seq', 'event', 'monotonic_ns'}


def receipt_shape(row, case):
    event = row.get('event')
    require(event in EVENT_FIELDS and set(row) == COMMON_FIELDS | EVENT_FIELDS[event], 'Exact probe event schema required')
    record_only = {'PUBLISH_RETURN', 'ACKED', 'RECORD_COMPLETE'}
    play_only = {'RESUME_ACCEPTED', 'RESUME_CALLED', 'RESUME_RETURNED', 'TYPED_RECEIVE', 'RAW_RECEIVE', 'PLAY_COMPLETE'}
    require(event not in (play_only if case == CASES[0] else record_only), 'Probe event belongs to another mode')
    if event in ('DISCOVERY', 'DISCOVERY_PENDING'):
        require(type(row['changed_snapshots']) is int and 1 <= row['changed_snapshots'] <= 64,
                'Bounded integer discovery observation required')
    if event == 'DISCOVERY_PENDING':
        require(type(row['known_error_type']) is str and row['known_error_type'] == 'NodeNameNonExistentError',
                'Only the exact public missing-node observation is pending')
    if event == 'RESUME_RETURNED':
        require(row['response_type'] == 'rosbag2_interfaces/srv/Resume_Response' and type(row['fields']) is dict and
                row['fields'] == {} and row['future_done'] is True and row['future_cancelled'] is False and
                row['future_raised'] is False, 'Exact empty response/completed nonthrowing future receipt required')


def rows(text, case, run, record_run, pid):
    result = []
    for line in text.splitlines():
        if not line.startswith('R35_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=unique)
        expected = dict(schema=1, scope=SCOPE, case=case, run=run, record_run=record_run, side='A', pid=pid,
                        role='bag-publisher' if case == CASES[0] else 'bag-subscriber', seq=len(result) + 1)
        require(type(row) is dict and all(type(row.get(k)) is type(v) and row.get(k) == v for k, v in expected.items()) and
                row.get('event') in EVENTS and type(row.get('monotonic_ns')) is int and row['monotonic_ns'] > 0,
                'Unknown/failing probe receipt identity/event')
        receipt_shape(row, case)
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Probe clock regressed')
        result.append(row)
    return result


def one(records, event, required=True):
    found = [r for r in records if r['event'] == event]
    require(len(found) == 1 if required else len(found) <= 1, 'Duplicate/missing ' + event)
    return found[0] if found else None


def metadata(info):
    require(type(info) is dict and set(info) == META and
            all(v is None or (type(v) is int and v >= 0) for v in info.values()), 'MessageInfo representation')
    return info


def probe_business(records, case, run, record_run, final=False):
    for row in records:
        receipt_shape(row, case)
    ready = one(records, 'READY', final)
    discoveries = [r for r in records if r['event'] in ('DISCOVERY', 'DISCOVERY_PENDING')]
    require(len(discoveries) <= 64 and [r['changed_snapshots'] for r in discoveries] == list(range(1, len(discoveries) + 1)),
            'Bounded combined discovery ledger')
    if discoveries:
        created = one(records, 'NODE_READY')
        require(all(created['seq'] < r['seq'] and (ready is None or r['seq'] < ready['seq']) for r in discoveries),
                'Discovery observation outside initial readiness')
        require(all(a['event'] != 'DISCOVERY_PENDING' or b['event'] != 'DISCOVERY_PENDING'
                    for a, b in zip(discoveries, discoveries[1:])), 'Duplicate unchanged pending observation')
    if ready:
        validate_graph(ready['graph'], case, run, record_run)
        exact(ready['counts'], {lane: [1] if case == CASES[0] else [0, 0] for lane in LANES})
        require(ready['slots_empty'] is True, 'Initial observation slots not empty')
        exact(ready['requested_qos'], dict(history=1, depth=10, reliability=1, durability=2))
        require(one(records, 'NODE_READY')['name'] == ('/r35bagpub_' if case == CASES[0] else '/r35bagsub_') + run,
                'Actual fixture node identity')
        ordered = [one(records, e)['seq'] for e in ('TYPE_SUPPORT', 'DEFINITIONS', 'NODE_READY')]
        require(ordered == sorted(set(ordered)) and ordered[-1] < ready['seq'], 'Initialization/readiness order')
        require(discoveries and discoveries[-1]['event'] == 'DISCOVERY' and discoveries[-1]['snapshot'] == ready['graph'],
                'Readiness requires recovery to the final complete graph')
    buffers = {r['seq']: r for r in records if r['event'] == 'BUFFER'}
    native = {r['buffer_seq']: r for r in records if r['event'] == 'NATIVE_DECODE'}
    require(len(native) == sum(r['event'] == 'NATIVE_DECODE' for r in records), 'Duplicate native decode')
    sent, received, metrics, raw_data = {}, {}, {}, {}
    for row in records:
        if row['event'] not in ('PUBLISH_RETURN', 'TYPED_RECEIVE', 'RAW_RECEIVE'):
            continue
        lane, index = row.get('lane'), row.get('index')
        require(lane in LANES and type(index) is int and index in (1, 2), 'Foreign sample identity')
        require(ready is not None and ready['seq'] < row['seq'], 'Sample before exact readiness')
        key = (lane, index)
        if row['event'] == 'PUBLISH_RETURN':
            require(case == CASES[0] and key not in sent, 'Duplicate/foreign publication')
            exact(row['input_kind'], 'bytes' if lane.startswith('serialized_') else 'message')
            table, receipt_key = sent, key
        else:
            require(case != CASES[0] and one(records, 'RESUME_CALLED')['seq'] < row['seq'], 'Early/foreign receive')
            receipt_key = (*key, 'raw' if row['event'] == 'RAW_RECEIVE' else 'typed')
            require(receipt_key not in received, 'Duplicate callback, including after provisional completion')
            exact(row['value'], expected_value(lane, index, record_run))
            metadata(row['metadata'])
            table = received
        if row['event'] != 'TYPED_RECEIVE':
            ref = row.get('buffer_seq')
            require(ref in buffers and ref in native and ref < native[ref]['seq'] < row['seq'], 'Whole buffer/native chain')
            b = buffers[ref]
            require(ready['seq'] < b['seq'] and b['lane'] == lane and
                    b['origin'] == ('LOCAL_SERIALIZE_MESSAGE' if case == CASES[0] else 'RAW_SUBSCRIPTION'), 'Buffer origin/readiness')
            data, actual_index, value, report = buffer_row(b, record_run)
            exact(native[ref]['value'], value)
            require(actual_index == index and native[ref]['index'] == index and native[ref]['lane'] == lane, 'Native decode identity')
            metrics[key] = report
            raw_data[key] = data
        table[receipt_key] = row
    complete = one(records, 'RECORD_COMPLETE' if case == CASES[0] else 'PLAY_COMPLETE', final)
    if complete:
        require(ready is not None, 'Completion before readiness')
        keys = {(lane, i) for lane in LANES for i in (1, 2)}
        require(set(sent) == keys if case == CASES[0] else set(received) == {(*k, kind) for k in keys for kind in ('typed', 'raw')}, 'Incomplete observations')
        require(len(buffers) == len(native) == 8, 'Exactly eight original/raw buffers required')
        if case == CASES[0]:
            acks = [r for r in records if r['event'] == 'ACKED']
            require(Counter(r['lane'] for r in acks) == Counter(LANES) and all(r['returned'] is True and
                type(r.get('timeout_ns')) is int and 0 < r['timeout_ns'] <= 5000000000 and
                max(x['seq'] for x in sent.values()) < r['seq'] < complete['seq'] for r in acks), 'Four successful bounded ACK observations')
            require(complete['published'] == 8 and complete['acknowledged'] == 4 and complete['settle_seconds'] == 0.25,
                    'Record completion counts/settlement')
        else:
            response = one(records, 'RESUME_RETURNED')
            require(response['response_type'] == 'rosbag2_interfaces/srv/Resume_Response' and
                    response['fields'] == {} and response['future_done'] is True and response['future_cancelled'] is False and
                    response['future_raised'] is False, 'Actual empty Resume response and completed nonthrowing future required')
            accepted, called = one(records, 'RESUME_ACCEPTED'), one(records, 'RESUME_CALLED')
            require(ready['seq'] < accepted['seq'] < called['seq'] < response['seq'] < complete['seq'] and
                    called['service'] == '/r35bagplayer_' + run + '/resume' and
                    re.fullmatch('[0-9a-f]{64}', accepted['bag_seal_sha256']), 'One source-bound Resume chronology')
            exact(complete['counters'], {lane: [2, 2] for lane in LANES})
            require(complete['callbacks'] == 16 and complete['quiet_seconds'] == 0.25, 'Playback completion counts/quiet period')
        observations = sent if case == CASES[0] else received
        require(all(r['seq'] < complete['seq'] for r in observations.values()), 'Observation after completion')
    if final:
        end = one(records, 'OBSERVATION_END')
        require(end['seq'] > complete['seq'] and end['callbacks'] == (0 if case == CASES[0] else 16) and
                end['published'] == (8 if case == CASES[0] else 0), 'Final observation cutoff/count')
        require(type(end['last_spin_ns']) is int and type(end['cutoff_ns']) is int and
                0 < end['last_spin_ns'] <= end['cutoff_ns'] and
                complete['seq'] < one(records, 'FINISH_ACCEPTED')['seq'] < end['seq'], 'Final spin/finish/cutoff chronology')
        require([r['event'] for r in records[-3:]] == ['NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'], 'Final teardown tail')
        require(one(records, 'FINISH_ACCEPTED')['reason'] == 'NORMAL_COMPLETION' and
                one(records, 'NODE_DESTROY_RETURN')['returned'] is True and
                one(records, 'SHUTDOWN_RETURN')['returned'] is True and one(records, 'FINAL')['success'] is True and
                one(records, 'FINAL')['node_created'] is True, 'Normal fixture finalization')
    return {'complete': complete is not None, 'ready': ready, 'metrics': metrics, 'raw_data': raw_data,
            'published': sent, 'received': received}


def stored_buffers(root, expected):
    """Read actual complete BLOBs for byte comparison, with immutable pre/post audit."""
    before = verify_recorded_bag(root, expected)
    require(before['play_eligible'], 'Unfaithful record cannot supply playback comparisons')
    root = Path(root).absolute()
    filename = next(n for n in expected['files'] if n != 'metadata.yaml')
    connection = sqlite3.connect((root / filename).as_uri() + '?mode=ro&immutable=1', uri=True)
    try:
        connection.execute('PRAGMA query_only=ON')
        connection.execute('PRAGMA trusted_schema=OFF')
        data = connection.execute('SELECT topics.name,messages.data FROM messages JOIN topics ON topics.id=messages.topic_id ORDER BY messages.id').fetchall()
        result = {}
        for name, blob in data:
            lane = next(l for l in LANES if topic(expected['record_run'], l) == name)
            index, _, _ = inspect_buffer(blob, lane, expected['record_run'])
            result[(lane, index)] = blob
    finally:
        connection.close()
    require(verify_recorded_bag(root, expected) == before, 'Playback comparison input drift')
    return result


def verify_recorded_bag(root, expected):
    root = Path(root).absolute()
    require(expected.get('schema') == 1 and re.fullmatch('[0-9a-f]{16}', expected.get('record_run', '')), 'Source expectation identity')
    files, run = expected['files'], expected['record_run']
    require(len(files) == 2 and 'metadata.yaml' in files, 'Two closed files required')
    filename = next(n for n in files if n != 'metadata.yaml')
    require(re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.-]*\.db3', filename), 'Unsafe DB name')
    for parent in (root, *root.parents):
        SQL._plain_path(parent, directory=True)
    require(SQL._snapshot(root, files) == files, 'Collection hash mismatch')
    external = SQL._yaml((root / 'metadata.yaml').read_text(encoding='utf-8'))
    require(set(external) == {'rosbag2_bagfile_information'}, 'Metadata wrapper')
    connection = sqlite3.connect((root / filename).as_uri() + '?mode=ro&immutable=1', uri=True)
    reports, comparisons = [], []
    try:
        connection.execute('PRAGMA query_only=ON')
        connection.execute('PRAGMA trusted_schema=OFF')
        require(connection.execute('PRAGMA integrity_check').fetchall() == [('ok',)], 'SQLite integrity')
        objects = connection.execute("SELECT type,name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchall()
        require(set(objects) == {('table', n) for n in SQL._SCHEMA} | {('index', 'timestamp_idx')}, 'SQLite object set')
        for table, shape in SQL._SCHEMA.items():
            columns = connection.execute('PRAGMA table_info(' + table + ')').fetchall()
            require([(c[1], c[2], c[3], c[5]) for c in columns] == shape and all(c[4] is None for c in columns), 'SQLite schema ' + table)
        require(connection.execute('SELECT schema_version,ros_distro FROM schema').fetchall() == [(4, '')], 'Schema4/env-i distro')
        topics = connection.execute('SELECT id,name,type,serialization_format,offered_qos_profiles,type_description_hash FROM topics').fetchall()
        require(len(topics) == 4 and set(expected['topics']) == set(LANES), 'Exactly four topics')
        ids, wanted_topics = {}, []
        for tid, name, typ, fmt, qos, thash in topics:
            lanes = [l for l in LANES if topic(run, l) == name]
            require(type(tid) is int and tid > 0 and len(lanes) == 1 and tid not in ids, 'Topic identity')
            lane = lanes[0]
            info = expected['topics'][lane]
            require((typ, fmt, thash) == (TYPES[lane], 'cdr', info['type_description_hash']) and
                    SQL._typed_equal(SQL._yaml(qos), info['offered_qos_profiles']), 'Topic type/QoS/hash')
            ids[tid] = lane
            wanted_topics.append({'topic_metadata': dict(name=name, type=typ, serialization_format=fmt,
                offered_qos_profiles=info['offered_qos_profiles'], type_description_hash=thash), 'message_count': 2})
        require(len(set(ids.values())) == 4, 'Duplicate topic lane')
        defs = connection.execute('SELECT topic_type,encoding,encoded_message_definition,type_description_hash FROM message_definitions').fetchall()
        require(len(defs) == 2 and len({d[0] for d in defs}) == 2, 'Exactly two root definitions')
        for typ, encoding, text, thash in defs:
            exact(dict(encoding=encoding, encoded_message_definition=text, type_description_hash=thash), expected['definitions'][typ])
        rows_db = connection.execute('SELECT id,topic_id,timestamp,data FROM messages ORDER BY id').fetchall()
        require(len(rows_db) == 8 and [x[0] for x in rows_db] == list(range(1, 9)), 'Eight fresh successful inserts')
        seen = set()
        for row_id, tid, timestamp, data in rows_db:
            require(tid in ids and type(timestamp) is int and timestamp > 0, 'Row topic/timestamp')
            lane = ids[tid]
            index, _, report = inspect_buffer(data, lane, run)
            key = (lane, index)
            require(key not in seen, 'Duplicate embedded index')
            seen.add(key)
            source = expected['published'][lane + ':' + str(index)]
            original = base64.b64decode(source['cdr_base64'], validate=True)
            require(digest(original) == source['cdr_sha256'] and len(original) == source['cdr_bytes'], 'Original buffer hash/length')
            inspect_buffer(original, lane, run, index)
            equal = original == data
            comparisons.append({'lane': lane, 'index': index, 'equal': equal,
                'comparison': 'DIRECT_SERIALIZED_SOURCE_STORAGE' if lane.startswith('serialized_') else 'OBSERVATIONAL_TYPED_LOCAL_STORAGE',
                'source_bytes': len(original), 'source_sha256': digest(original), 'storage_bytes': len(data), 'storage_sha256': digest(data)})
            reports.append(dict(lane=lane, index=index, row_id=row_id, timestamp=timestamp, **report))
        require(seen == {(l, i) for l in LANES for i in (1, 2)}, 'Incomplete bag samples')
        start, duration = writer_timing([r[2] for r in rows_db])
        metadata_rows = connection.execute('SELECT metadata_version,metadata FROM metadata ORDER BY id').fetchall()
        require(metadata_rows and metadata_rows[-1][0] == 9, 'Final metadata9 missing')
        for data in (SQL._yaml(metadata_rows[-1][1]), external['rosbag2_bagfile_information']):
            require(data.get('version') == 9 and type(data['version']) is int and data.get('message_count') == 8 and
                    type(data['message_count']) is int and data.get('storage_identifier') == 'sqlite3' and
                    data.get('relative_file_paths') == [filename] and data.get('compression_format') == '' and
                    data.get('compression_mode') == '' and data.get('ros_distro') == '' and 'custom_data' in data and
                    (data['custom_data'] is None or data['custom_data'] == {}), 'Metadata contract')
            exact(data['starting_time'], {'nanoseconds_since_epoch': start})
            exact(data['duration'], {'nanoseconds': duration})
            exact(sorted(data['topics_with_message_count'], key=lambda x: x['topic_metadata']['name']),
                  sorted(wanted_topics, key=lambda x: x['topic_metadata']['name']))
            exact(data['files'], [{'path': filename, 'starting_time': {'nanoseconds_since_epoch': start},
                                  'duration': {'nanoseconds': duration}, 'message_count': 8}])
    finally:
        connection.close()
    require(SQL._snapshot(root, files) == files, 'Immutable bag changed while querying')
    faithful = all(r['equal'] for r in comparisons if r['comparison'].startswith('DIRECT_'))
    return {'schema': 1, 'status': 'PASS_CLOSED_BAG_CONTENT_ONLY', 'record_run': run, 'functionality': 'PASS',
            'persistence_fidelity': 'PASS' if faithful else 'FAIL', 'play_eligible': faithful, 'files': files,
            'rows': reports, 'comparisons': comparisons, 'writer_start': start, 'writer_duration': duration,
            'timestamp_min': min(x[2] for x in rows_db), 'timestamp_max': max(x[2] for x in rows_db),
            'device_execution_verified': False, 'recorder_internal_take': 'UNOBSERVABLE',
            'strong_dds': 'BLOCKED_PROVENANCE_GAP'}
