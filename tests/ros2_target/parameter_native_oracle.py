"""Independent finite parameter semantics and XCDR1 decoder; no ROS imports."""
import base64
import hashlib
import json
import re
import struct
from process_restart_oracle import fields

INSTANCES = ('A', 'B')
SERVICES = ('ListParameters', 'GetParameterTypes', 'DescribeParameters', 'GetParameters',
            'SetParameters', 'SetParametersAtomically')
KINDS = ('bool', 'integer', 'double', 'string', 'bytes', 'bools', 'integers', 'doubles', 'strings')
EVENTS = {'TYPE_SUPPORT', 'NODE_READY', 'DISCOVERY_OBSERVATION', 'CLIENT_READY', 'SERVER_READY', 'REQUEST', 'RESPONSE',
          'PARAMETER_EVENT', 'STANDARD_STARTUP_EVENT', 'SERVER_VALIDATE', 'SERVER_COMMIT', 'SERVER_STATE', 'CLIENT_COMPLETE',
          'SERVER_COMPLETE', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
COMMON_EVENTS = {'TYPE_SUPPORT', 'NODE_READY', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
ROLE_EVENTS = {
    'A': COMMON_EVENTS | {'DISCOVERY_OBSERVATION', 'CLIENT_READY', 'REQUEST', 'RESPONSE',
                          'PARAMETER_EVENT', 'STANDARD_STARTUP_EVENT', 'CLIENT_COMPLETE'},
    'B': COMMON_EVENTS | {'SERVER_READY', 'SERVER_VALIDATE', 'SERVER_COMMIT', 'SERVER_STATE', 'SERVER_COMPLETE'},
}
SCHEMAS = {
    'Value': [('type', 'B'), ('bool_value', '?'), ('integer_value', 'q'), ('double_value', 'd'),
              ('string_value', 's'), ('byte_array_value', ['B']), ('bool_array_value', ['?']),
              ('integer_array_value', ['q']), ('double_array_value', ['d']), ('string_array_value', ['s'])],
    'Parameter': [('name', 's'), ('value', 'Value')],
    'Descriptor': [('name', 's'), ('type', 'B'), ('description', 's'), ('additional_constraints', 's'),
                   ('read_only', '?'), ('dynamic_typing', '?'), ('floating_point_range', ['FloatRange']),
                   ('integer_range', ['IntRange'])],
    'FloatRange': [('from_value', 'd'), ('to_value', 'd'), ('step', 'd')],
    'IntRange': [('from_value', 'q'), ('to_value', 'q'), ('step', 'Q')],
    'SetResult': [('successful', '?'), ('reason', 's')],
    'ListResult': [('names', ['s']), ('prefixes', ['s'])],
    'ParameterEvent': [('stamp', 'Time'), ('node', 's'), ('new_parameters', ['Parameter']),
                       ('changed_parameters', ['Parameter']), ('deleted_parameters', ['Parameter'])],
    'Time': [('sec', 'i'), ('nanosec', 'I')],
    'ListParameters.Request': [('prefixes', ['s']), ('depth', 'Q')],
    'ListParameters.Response': [('result', 'ListResult')],
    'GetParameterTypes.Response': [('types', ['B'])],
    'DescribeParameters.Response': [('descriptors', ['Descriptor'])],
    'GetParameters.Response': [('values', ['Value'])],
    'SetParameters.Response': [('results', ['SetResult'])],
    'SetParametersAtomically.Response': [('result', 'SetResult')],
}
for _name in ('GetParameterTypes', 'DescribeParameters', 'GetParameters'):
    SCHEMAS[_name + '.Request'] = [('names', ['s'])]
for _name in ('SetParameters', 'SetParametersAtomically'):
    SCHEMAS[_name + '.Request'] = [('parameters', ['Parameter'])]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON field')
        result[key] = value
    return result


def exact(actual, expected):
    """JSON equality that cannot accept True as integer1 or float as int."""
    require(type(actual) is type(expected), 'Value type differs')
    if isinstance(expected, dict):
        require(set(actual) == set(expected), 'Value fields differ')
        for key in expected:
            exact(actual[key], expected[key])
    elif isinstance(expected, list):
        require(len(actual) == len(expected), 'Value sequence length differs')
        for a, b in zip(actual, expected):
            exact(a, b)
    else:
        require(actual == expected, 'Value differs')


class CdrReader:
    def __init__(self, data):
        require(4 <= len(data) <= 65536 and data[:4] in (b'\0\1\0\0', b'\0\0\0\0'), 'XCDR1 header/size')
        self.data, self.offset, self.endian = data[4:], 0, '<' if data[1] else '>'

    def take(self, count):
        require(0 <= count <= len(self.data) - self.offset, 'Truncated CDR')
        value = self.data[self.offset:self.offset + count]
        self.offset += count
        return value

    def read(self, kind):
        if isinstance(kind, list):
            count = self.read('I')
            require(count <= 1000, 'Oversized CDR sequence')
            return [self.read(kind[0]) for _ in range(count)]
        if kind in SCHEMAS:
            return {name: self.read(t) for name, t in SCHEMAS[kind]}
        if kind == 's':
            count = self.read('I')
            require(1 <= count <= 8192, 'CDR string size')
            raw = self.take(count)
            require(raw[-1:] == b'\0' and b'\0' not in raw[:-1], 'CDR string terminator')
            return raw[:-1].decode('utf-8')
        size = struct.calcsize(kind)
        self.take((-self.offset) % size)
        raw = self.take(size)
        if kind == '?':
            require(raw in (b'\0', b'\1'), 'Noncanonical CDR boolean')
        return struct.unpack(self.endian + kind, raw)[0]


def cdr(row, kind):
    raw = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    require(type(row.get('cdr_bytes')) is int and row['cdr_bytes'] == len(raw) and
            row.get('cdr_sha256') == hashlib.sha256(raw).hexdigest(), 'CDR size/digest mismatch')
    reader = CdrReader(raw)
    value = reader.read(kind)
    require(reader.offset == len(reader.data), 'Trailing CDR bytes')
    exact(value, row.get('value'))
    return value


def rows(text, scenario, instance, run, pid):
    require(instance in INSTANCES and re.fullmatch('[0-9a-f]{16}', scenario) and
            re.fullmatch('[0-9a-f]{16}', run) and type(pid) is int and pid > 1, 'Process identity invalid')
    result = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=unique,
                         parse_constant=lambda x: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))
        expected = {'schema': 1, 'scenario': scenario, 'instance': instance, 'run': run, 'side': instance,
                    'pid': pid, 'seq': len(result) + 1, 'role': 'requester' if instance == 'A' else 'responder'}
        require(isinstance(row, dict) and all(row.get(k) == v and type(row.get(k)) is type(v) for k, v in expected.items()) and
                type(row.get('monotonic_ns')) is int and row['monotonic_ns'] > 0 and row.get('event') in ROLE_EVENTS[instance],
                'Parameter process record invalid/failure')
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Clock regressed')
        result.append(row)
    return result


def select(records, event):
    return [r for r in records if r['event'] == event]


def one(records, event):
    selected = select(records, event)
    require(len(selected) == 1, 'Exactly one ' + event + ' required')
    return selected[0]


def value(kind, data=None):
    result = dict(type=kind, bool_value=False, integer_value=0, double_value=0.0, string_value='',
                  byte_array_value=[], bool_array_value=[], integer_array_value=[], double_array_value=[], string_array_value=[])
    if kind:
        result[list(result)[kind]] = data
    return result


def state(scenario, stage):
    prefix = 'p' + scenario + '.'
    values = ([True, 1099511627776, 1.25, 'initial', [0, 255], [True, False], [-1099511627776, 7],
               [-0.5, 2.25], ['alpha', '参数']] if stage == 0 else
              [False, -1099511627777, -3.5, 'updated_参数', [255, 0, 127], [False, True, False],
               [1099511627777, -8], [0.125, -4.5], ['beta', '事件', '']])
    if stage >= 2:
        values[1], values[3] = 42, 'atomic_参数'
    result = [{'name': prefix + name, 'value': value(i, v)} for i, (name, v) in enumerate(zip(KINDS, values), 1)]
    result.append({'name': prefix + 'readonly', 'value': value(4, 'fixed')})
    if stage < 3:
        result.append({'name': prefix + 'dynamic', 'value': value(0) if stage == 0 else value(4, 'temporary')})
    return result


def expected_calls(scenario):
    initial, changed, atomic, final = (state(scenario, i) for i in range(4))
    names = [p['name'] for p in initial]
    prefix = 'p' + scenario
    p = lambda name, kind, data=None: {'name': prefix + '.' + name, 'value': value(kind, data)}
    return [
        ('ListParameters', {'prefixes': [prefix], 'depth': 0}),
        ('GetParameterTypes', {'names': names}), ('DescribeParameters', {'names': names}),
        ('GetParameters', {'names': names}), ('SetParameters', {'parameters': changed[:9] + changed[-1:]}),
        ('GetParameters', {'names': names}),
        ('SetParametersAtomically', {'parameters': [atomic[1], atomic[3]]}),
        ('SetParametersAtomically', {'parameters': [p('integer', 2, -99), p('string', 4, 'rejected')]}),
        ('SetParametersAtomically', {'parameters': [p('integer', 2, 123), p('readonly', 4, 'rejected')]}),
        ('SetParametersAtomically', {'parameters': [p('integer', 4, 'wrong_type'), p('string', 4, 'rejected')]}),
        ('GetParameters', {'names': names}), ('SetParameters', {'parameters': [p('dynamic', 0)]}),
        ('ListParameters', {'prefixes': [prefix], 'depth': 0}),
        ('GetParameters', {'names': [p['name'] for p in final]})]


def business(a, b, scenario):
    """V1 always requires strict exhaustion of every captured message."""
    for records in (a, b):
        for row, kind in _captures(records):
            cdr(row, kind)
    return _verify_parameter_values(a, b, scenario)


def _captures(records):
    for row in records:
        event = row.get('event')
        if event in ('REQUEST', 'RESPONSE'):
            require(row.get('service') in SERVICES, 'Unknown parameter service')
            yield row, row['service'] + ('.Request' if event == 'REQUEST' else '.Response')
        elif event in ('PARAMETER_EVENT', 'STANDARD_STARTUP_EVENT'):
            yield row, 'ParameterEvent'


def _verify_parameter_values(a, b, scenario):
    """Private shared semantics; fixed V1/V2 wrappers validate captures first."""
    require(all(row.get('event') in ROLE_EVENTS[side] for side, records in (('A', a), ('B', b)) for row in records),
            'Parameter record event belongs to wrong process role')
    startup = select(a, 'STANDARD_STARTUP_EVENT')
    require(len(startup) <= 2 and not select(b, 'STANDARD_STARTUP_EVENT'), 'Standard startup event duplicated/wrong side')
    defaults, seen = {'use_sim_time': False, 'start_type_description_service': True}, set()
    for row in startup:
        event = row['value']
        require(event['stamp']['sec'] > 0 and 0 <= event['stamp']['nanosec'] < 1000000000, 'Startup timestamp invalid')
        require(len(event['new_parameters']) == 1, 'Exactly one standard startup declaration required')
        name = event['new_parameters'][0]['name']
        require(name in defaults and name not in seen, 'Unknown/duplicate standard startup declaration')
        exact(event, dict(stamp=event['stamp'], node='/parameter_' + scenario + '_B',
                         new_parameters=[{'name': name, 'value': value(1, defaults[name])}],
                         changed_parameters=[], deleted_parameters=[]))
        seen.add(name)
    requests, responses = select(a, 'REQUEST'), select(a, 'RESPONSE')
    calls = expected_calls(scenario)
    require(len(requests) == len(responses) == len(calls) == 14, 'Exact14 RPCs required')
    initial, changed, atomic, final = (state(scenario, i) for i in range(4))
    prefix = 'p' + scenario
    successful = {'successful': True, 'reason': ''}
    for index, ((service, expected), request, response) in enumerate(zip(calls, requests, responses), 1):
        require(request.get('call') == response.get('call') == index and type(request['call']) is type(response['call']) is int and
                request.get('service') == response.get('service') == service and request['seq'] < response['seq'] and
                (index == 1 or responses[index - 2]['seq'] < request['seq']), 'RPC identity/order')
        exact(request['value'], expected)
        result = response['value']
        if index in (1, 13):
            wanted = initial if index == 1 else final
            exact(sorted(result['result']['names']), sorted(p['name'] for p in wanted))
            exact(result['result']['prefixes'], [prefix])
        elif index == 2:
            exact(result, {'types': [p['value']['type'] for p in initial]})
        elif index == 3:
            descriptors = []
            for i, p in enumerate(initial):
                descriptors.append(dict(name=p['name'], type=p['value']['type'], description='', additional_constraints='',
                                        read_only=i == 9, dynamic_typing=i == 10, floating_point_range=[], integer_range=[]))
            exact(result, {'descriptors': descriptors})
        elif index in (4, 6, 11, 14):
            wanted = {4: initial, 6: changed, 11: atomic, 14: final}[index]
            exact(result, {'values': [p['value'] for p in wanted]})
        elif index in (5, 12):
            exact(result, {'results': [successful] * (10 if index == 5 else 1)})
        elif index == 7:
            exact(result, {'result': successful})
        else:
            failure = result['result']
            require(failure['successful'] is False and isinstance(failure['reason'], str) and failure['reason'], 'Rejection absent')
            if index == 8:
                exact(failure['reason'], 'r33 callback rejects -99')
            else:
                require(('read-only' if index == 9 else 'Wrong parameter type') in failure['reason'], 'Wrong rejection reason')
    commits = [[p] for p in changed[:9] + changed[-1:]] + [[atomic[1], atomic[3]], calls[11][1]['parameters']]
    exact([r.get('parameters') for r in select(b, 'SERVER_COMMIT')], commits)
    validations = select(b, 'SERVER_VALIDATE')
    exact([{'parameters': r.get('parameters'), 'accepted': r.get('accepted')} for r in validations],
          [{'parameters': p, 'accepted': True} for p in commits[:11]] +
          [{'parameters': calls[7][1]['parameters'], 'accepted': False}, {'parameters': commits[-1], 'accepted': True}])
    exact(one(b, 'SERVER_STATE').get('parameters'), final)
    require(one(b, 'SERVER_COMPLETE').get('commits') == 12, 'Server completion missing')
    mutations, declarations = [], []
    for row in select(a, 'PARAMETER_EVENT'):
        event = row['value']
        require(event['node'] == '/parameter_' + scenario + '_B' and event['stamp']['sec'] > 0 and
                0 <= event['stamp']['nanosec'] < 1000000000, 'Event origin/timestamp differs')
        content = {k: event[k] for k in ('new_parameters', 'changed_parameters', 'deleted_parameters')}
        if event['new_parameters'] and all(p in initial for p in event['new_parameters']) and not event['changed_parameters'] and not event['deleted_parameters']:
            declarations.extend(event['new_parameters'])
        else:
            mutations.append(content)
    require(len({p['name'] for p in declarations}) == len(declarations), 'Duplicate startup declaration event')
    expected_events = [dict(new_parameters=[], changed_parameters=[p], deleted_parameters=[]) for p in changed[:9]]
    expected_events += [dict(new_parameters=changed[-1:], changed_parameters=[], deleted_parameters=[]),
                       dict(new_parameters=[], changed_parameters=[atomic[1], atomic[3]], deleted_parameters=[]),
                       dict(new_parameters=[], changed_parameters=[], deleted_parameters=commits[-1])]
    exact(mutations, expected_events)
    complete = one(a, 'CLIENT_COMPLETE')
    require(complete.get('calls') == 14 and complete.get('observed_events') == len(select(a, 'PARAMETER_EVENT')) and
            complete.get('quiet_seconds') == 0.25 and responses[-1]['seq'] < complete['seq'], 'Client completion/quiet witness differs')
    ready = one(a, 'CLIENT_READY')
    discovery = select(a, 'DISCOVERY_OBSERVATION')
    require(1 <= len(discovery) <= 64 and not select(b, 'DISCOVERY_OBSERVATION'), 'Discovery observations missing/overflow/wrong side')
    previous = None
    for row in discovery:
        require(row['seq'] < ready['seq'], 'Discovery observation after readiness')
        services, matched, publishers = (row.get(k) for k in ('ready_services', 'matched_publishers', 'publishers'))
        require(isinstance(services, list) and services == [name for name in SERVICES if name in services] and
                type(matched) is int and matched >= 0 and isinstance(publishers, list), 'Discovery observation fields invalid')
        require(all(isinstance(p, dict) and set(p) == {'name', 'namespace'} and
                    all(isinstance(v, str) for v in p.values()) for p in publishers), 'Publisher identity observation invalid')
        snapshot = {'ready_services': services, 'matched_publishers': matched, 'publishers': publishers}
        require(snapshot != previous, 'Duplicate discovery observation')
        previous = snapshot
    expected_names = ['/parameter_' + scenario + '_' + side for side in INSTANCES]
    last_names = sorted(p['namespace'].rstrip('/') + '/' + p['name'] for p in discovery[-1]['publishers'])
    exact(discovery[-1]['ready_services'], list(SERVICES))
    exact(discovery[-1]['matched_publishers'], 2)
    exact(last_names, expected_names)
    exact(ready.get('publisher_fqns'), expected_names)
    exact(ready.get('discovery_observations'), len(discovery))
    exact(ready.get('services'), list(SERVICES))
    exact(ready.get('event_qos'), {'history': 1, 'depth': 1000, 'reliability': 1, 'durability': 2})
    require(ready.get('matched_publishers') == 2 and ready['seq'] < requests[0]['seq'], 'Subscription not matched before RPCs')
    return {'rpc_count': 14, 'standard_services': list(SERVICES), 'value_types': 9, 'mutation_events': 12,
            'discovery_observations': len(discovery), 'event_publisher_fqns': expected_names,
            'startup_events_observed': len(declarations), 'standard_startup_events_observed': len(startup),
            'atomic_rejections': 3, 'dynamic_deleted': True,
            'negative_event_observation': 'FINITE_250MS_NOT_UNBOUNDED', 'wire_payload_mapping': 'NOT_DIRECTLY_MAPPED'}


def finalized(records, package_files):
    support = one(records, 'TYPE_SUPPORT')
    expected_modules = {'rclpy', 'rclpy._rclpy_pybind11', 'rclpy.parameter_service', 'rcl_interfaces.msg', 'rcl_interfaces.srv'}
    require(set(support.get('origins', {})) == expected_modules, 'Module provenance incomplete')
    for row in support['origins'].values():
        require(set(row) == {'member', 'sha256'} and package_files.get(row['member']) == row['sha256'], 'Module outside package')
    expected_capsules = {'ParameterEvent.message'} | {n + '.' + p for n in SERVICES for p in ('Request', 'Response')}
    require(set(support.get('capsules', {})) == expected_capsules and set(support['capsules'].values()) == {'PyCapsule'}, 'Native type support missing')
    finish = one(records, 'FINISH_ACCEPTED')
    destroy, shutdown, final = (one(records, n) for n in ('NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'))
    require(finish.get('reason') == 'NORMAL_COMPLETION' and destroy.get('returned') is True and shutdown.get('returned') is True and
            final.get('success') is True and final.get('node_created') is True and
            finish['seq'] < destroy['seq'] < shutdown['seq'] < final['seq'], 'Orderly probe finalization missing')


def parameter_endpoints(texts, scenario):
    """Existing DDS endpoint declarations, not a payload-to-wire attribution."""
    require(set(texts) == set(INSTANCES) and re.fullmatch('[0-9a-f]{16}', scenario), 'Exact endpoint scene required')
    server = 'parameter_' + scenario + '_B/'
    suffixes = ('list_parameters', 'get_parameter_types', 'describe_parameters', 'get_parameters',
                'set_parameters', 'set_parameters_atomically')
    client = {('writer', 'rq/' + server + name + 'Request') for name in suffixes}
    client |= {('reader', 'rr/' + server + name + 'Reply') for name in suffixes}
    result = {}
    for side in INSTANCES:
        declarations = fields(texts[side], 'FDDST_ENDPOINT')
        all_services = [r for r in declarations if r.get('topic', '').startswith(('rq/', 'rr/'))]
        standard = {('reader', 'rq/parameter_' + scenario + '_' + side + '/get_type_descriptionRequest'),
                    ('writer', 'rr/parameter_' + scenario + '_' + side + '/get_type_descriptionReply')}
        standard_services = [r for r in all_services if (r.get('role'), r.get('topic')) in standard]
        services = [r for r in all_services if (r.get('role'), r.get('topic')) not in standard]
        events = [r for r in declarations if r.get('topic') == 'rt/parameter_events']
        expected = client if side == 'A' else {('reader' if role == 'writer' else 'writer', topic) for role, topic in client}
        require(len(services) == 12 and {(r.get('role'), r.get('topic')) for r in services} == expected,
                'Six paired parameter service endpoint declarations missing/foreign/duplicated')
        require(len(standard_services) == 2 and {(r.get('role'), r.get('topic')) for r in standard_services} == standard,
                'Default local type-description service endpoint declarations missing/duplicated')
        event_roles = {'reader', 'writer'} if side == 'A' else {'writer'}
        require(len(events) == len(event_roles) and {r.get('role') for r in events} == event_roles,
                'Exact parameter event publisher/subscriber declarations missing')
        require(all(set(r) == {'role', 'topic', 'data_sharing'} and r['data_sharing'] == 'OFF'
                    for r in all_services + events), 'Endpoint schema/sharing changed')
        result[side] = {'services': sorted(services, key=lambda r: r['topic']),
                        'standard_type_description': sorted(standard_services, key=lambda r: r['topic']),
                        'events': sorted(events, key=lambda r: r['role'])}
    return {'declarations': result, 'type_evidence': 'NATIVE_GENERATED_CAPSULE_AND_REQUEST_RESPONSE_CDR_SCHEMA',
            'type_description_rpc': 'NOT_EXERCISED_ENDPOINTS_ONLY',
            'wire_type_observer': 'NOT_PROVIDED', 'payload_to_rtps_sequence': 'NOT_DIRECTLY_MAPPED'}
