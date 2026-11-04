"""Strict public AddTwoInts generations; OLD is never recovery success."""
import base64
import hashlib
import json
import re
import struct
from process_restart_oracle import require, unique, fields, owners, a_continuity

INSTANCES = ('A', 'B0', 'B1')
EVENTS = {'TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY', 'SERVICE_READY', 'CLIENT_REQUEST', 'CLIENT_RESPONSE',
          'SERVER_REQUEST', 'SERVER_RESPONSE', 'SERVER_PAUSED', 'OLD_PENDING', 'SERVICE_UNAVAILABLE',
          'CONTROL_ACCEPTED', 'PHASE_COMPLETE', 'FINISH_ACCEPTED', 'OLD_FINAL_STATE', 'OLD_LOCAL_DISPOSE',
          'ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}


def operands(scenario, phase):
    require(re.fullmatch('[0-9a-f]{16}', scenario) and phase in ('PRE', 'OLD', 'NEW'), 'Request identity invalid')
    return int(scenario[:12], 16) * 4 + ('PRE', 'OLD', 'NEW').index(phase), 29


def rows(text, scenario, instance, run, pid):
    require(instance in INSTANCES and re.fullmatch('[0-9a-f]{16}', scenario) and
            re.fullmatch('[0-9a-f]{16}', run) and type(pid) is int and pid > 1, 'Process identity invalid')
    result = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=unique)
        expected = {'schema': 1, 'scenario': scenario, 'instance': instance, 'run': run, 'side': instance[0],
                    'pid': pid, 'seq': len(result) + 1, 'role': 'requester' if instance == 'A' else 'responder'}
        require(isinstance(row, dict) and all(row.get(k) == v for k, v in expected.items()) and
                all(type(row.get(k)) is int for k in ('schema', 'pid', 'seq', 'monotonic_ns')) and
                row['monotonic_ns'] > 0 and row.get('event') in EVENTS, 'Service process record invalid')
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Clock regressed')
        result.append(row)
    return result


def select(records, event, phase=None):
    return [r for r in records if r['event'] == event and (phase is None or r.get('phase') == phase)]


def one(records, event, phase=None):
    found = select(records, event, phase)
    require(len(found) == 1, 'Expected exactly one ' + event + '/' + str(phase))
    return found[0]


def payload(row, scenario):
    phase = row.get('phase')
    a, b = operands(scenario, phase)
    require(type(row.get('a')) is int and type(row.get('b')) is int and row['a'] == a and row['b'] == b,
            'Foreign request generation/operands')
    data = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    response = row['event'].endswith('RESPONSE')
    require(len(data) == (12 if response else 20) and data[:4] in (b'\0\1\0\0', b'\0\0\0\0'), 'Invalid AddTwoInts CDR')
    values = struct.unpack(('<' if data[1] else '>') + ('q' if response else 'qq'), data[4:])
    require(values == ((a + b,) if response else (a, b)), 'CDR value mismatch')
    require(row.get('cdr_sha256') == hashlib.sha256(data).hexdigest() and type(row.get('cdr_bytes')) is int and
            row['cdr_bytes'] == len(data), 'CDR digest/length mismatch')
    if response:
        require(type(row.get('sum')) is int and row['sum'] == a + b, 'Response sum mismatch')
    return data


def startup(records, scenario, instance):
    require([r['event'] for r in records[:3]] == ['TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY'], 'Service startup order')
    one(records, 'TYPE_SUPPORT_READY')
    one(records, 'NODE_READY')
    entity = one(records, 'ENTITY_READY')
    require(all(entity.get(k) == v for k, v in {'name': '/service_restart_' + scenario,
            'interface': 'example_interfaces/srv/AddTwoInts', 'entity': 'client' if instance == 'A' else 'service',
            'depth': 10, 'reliability': 'RELIABLE', 'durability': 'VOLATILE'}.items()) and
            type(entity.get('depth')) is int, 'Actual service/QoS identity missing')


def pair(a, b, scenario, phase):
    require(phase in ('PRE', 'NEW'), 'OLD cannot satisfy recovery pair')
    request, response = one(a, 'CLIENT_REQUEST', phase), one(a, 'CLIENT_RESPONSE', phase)
    taken, sent = one(b, 'SERVER_REQUEST', phase), one(b, 'SERVER_RESPONSE', phase)
    require(request['seq'] < response['seq'] and taken['seq'] < sent['seq'], 'Service request/response ordering')
    require(payload(request, scenario) == payload(taken, scenario) and
            payload(response, scenario) == payload(sent, scenario), 'Service peer CDR differs')
    if phase == 'PRE':
        paused = one(b, 'SERVER_PAUSED')
        require(paused['seq'] > sent['seq'] and paused.get('after_callback_return') is True and
                paused.get('takes_further_requests') is False, 'B0 not paused after completed callback')
    else:
        lost = one(a, 'SERVICE_UNAVAILABLE')
        ready = one(a, 'SERVICE_READY', 'NEW')
        released = one(a, 'CONTROL_ACCEPTED', 'POST')
        old = one(a, 'OLD_PENDING')
        require(old['seq'] < lost['seq'] < released['seq'] < ready['seq'] < request['seq'] and
                lost.get('ready') is False and type(lost.get('old_done')) is bool and ready.get('ready') is True,
                'Missing genuine disappearance/rediscovery before NEW')
    return {'phase': phase, 'completed_requests': 1, 'request_and_response_CDR': 'PEER_COPIES_EQUAL'}


def pending(records):
    request = one(records, 'CLIENT_REQUEST', 'OLD')
    marker = one(records, 'OLD_PENDING')
    control = one(records, 'CONTROL_ACCEPTED', 'OLD')
    pre = one(records, 'CLIENT_RESPONSE', 'PRE')
    require(pre['seq'] < control['seq'] < request['seq'] < marker['seq'] and
            marker.get('checkpoint') == 'BEFORE_B0_STOP' and marker.get('done') is False and
            marker.get('cancelled') is False, 'Old request not separately pending before stop')
    return {'old_request_pending_before_stop': True, 'not_recovery_success': True}


def final(records, scenario, instance):
    startup(records, scenario, instance)
    common = {'TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY', 'PHASE_COMPLETE', 'FINISH_ACCEPTED', 'ENTITY_DESTROY_RETURN',
              'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
    specific = ({'SERVICE_READY', 'CLIENT_REQUEST', 'CLIENT_RESPONSE', 'OLD_PENDING', 'SERVICE_UNAVAILABLE',
                 'CONTROL_ACCEPTED', 'OLD_FINAL_STATE', 'OLD_LOCAL_DISPOSE'} if instance == 'A' else
                {'SERVER_REQUEST', 'SERVER_RESPONSE'} | ({'SERVER_PAUSED'} if instance == 'B0' else set()))
    require(all(r['event'] in common | specific for r in records), 'Event belongs to another process role')
    for event in ('ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN'):
        require(one(records, event).get('returned') is True, 'Public cleanup failed')
    finish = one(records, 'FINISH_ACCEPTED')
    require(finish.get('reason') == 'NORMAL_COMPLETION' and
            [r['event'] for r in records[-4:]] == ['ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'] and
            finish['seq'] < records[-4]['seq'] and one(records, 'FINAL').get('success') is True and
            records[-1].get('node_created') is True, 'Normal teardown missing')
    require(one(records, 'ENTITY_DESTROY_RETURN').get('entity') == ('client' if instance == 'A' else 'service'),
            'Wrong entity destroyed')
    complete = one(records, 'PHASE_COMPLETE')
    require(complete.get('phase') == ('PRE' if instance == 'B0' else 'NEW') and complete['seq'] < finish['seq'],
            'Finite phase not complete before normal finish')
    business = [r for r in records if r['event'] in ('CLIENT_REQUEST', 'CLIENT_RESPONSE', 'SERVER_REQUEST', 'SERVER_RESPONSE')]
    seen = set()
    for row in business:
        key = row['event'], row.get('phase')
        require(key not in seen, 'Duplicate service event')
        seen.add(key)
        payload(row, scenario)
    if instance == 'A':
        required = {('CLIENT_REQUEST', p) for p in ('PRE', 'OLD', 'NEW')} | {('CLIENT_RESPONSE', p) for p in ('PRE', 'NEW')}
        require(required <= seen <= required | {('CLIENT_RESPONSE', 'OLD')}, 'Client request/response set differs')
        pending(records)
        require([r.get('phase') for r in select(records, 'SERVICE_READY')] == ['PRE', 'NEW'] and
                [r.get('phase') for r in select(records, 'CONTROL_ACCEPTED')] == ['OLD', 'POST'] and
                one(records, 'CLIENT_RESPONSE', 'NEW')['seq'] < complete['seq'], 'Client stage cardinality/order')
        ready = one(records, 'SERVICE_READY', 'PRE')
        require(ready.get('ready') is True and ready['seq'] < one(records, 'CLIENT_REQUEST', 'PRE')['seq'], 'Initial service not ready')
        state = one(records, 'OLD_FINAL_STATE')
        old_response = select(records, 'CLIENT_RESPONSE', 'OLD')
        require(type(state.get('done')) is bool and state.get('cancelled') is False and
                state.get('response_observed') is bool(old_response) and state['done'] == bool(old_response) and
                state['seq'] > finish['seq'], 'Old final state invalid')
        if old_response:
            require(old_response[0]['seq'] > one(records, 'OLD_PENDING')['seq'] and old_response[0]['seq'] < state['seq'] and
                    not select(records, 'OLD_LOCAL_DISPOSE'), 'OLD response accounting differs')
        else:
            disposed = one(records, 'OLD_LOCAL_DISPOSE')
            require(disposed.get('cancelled') is True and disposed.get('remote_cancel') is False and
                    state['seq'] < disposed['seq'] < records[-4]['seq'], 'Pending OLD not locally disposed')
        return {'old_outcome': 'RESPONSE_OBSERVED_SEPARATELY' if old_response else 'PENDING_LOCALLY_DISPOSED',
                'remote_cancellation': 'NOT_CLAIMED'}
    required = {(e, 'PRE' if instance == 'B0' else 'NEW') for e in ('SERVER_REQUEST', 'SERVER_RESPONSE')}
    optional = {(e, 'OLD') for e in ('SERVER_REQUEST', 'SERVER_RESPONSE')} if instance == 'B1' else set()
    require(seen in (required, required | optional), 'Server request/response set differs')
    if instance == 'B0':
        require(one(records, 'SERVER_PAUSED')['seq'] < complete['seq'], 'B0 pause checkpoint order')
    else:
        require(one(records, 'SERVER_RESPONSE', 'NEW')['seq'] < complete['seq'], 'B1 completes before response')
    return {'served_old': ('SERVER_REQUEST', 'OLD') in seen}


def type_support(records, scenario, package_files):
    row = one(records, 'TYPE_SUPPORT_READY')
    require(row.get('interface') == 'example_interfaces/srv/AddTwoInts' and row.get('node_created') is False and
            row.get('soabi') == 'cpython-312-aarch64-linux-ohos', 'Service native typesupport identity missing')
    suffix = '.cpython-312-aarch64-linux-ohos.so'
    modules = {'rclpy._rclpy_pybind11': 'python/rclpy/_rclpy_pybind11' + suffix,
               'rclpy.client': 'python/rclpy/client.py', 'rclpy.service': 'python/rclpy/service.py',
               'example_interfaces.srv._add_two_ints': 'python/example_interfaces/srv/_add_two_ints.py',
               'example_interfaces.example_interfaces_s__rosidl_typesupport_c':
                   'python/example_interfaces/example_interfaces_s__rosidl_typesupport_c' + suffix}
    require(isinstance(package_files, dict) and set(row.get('origins', {})) == set(modules), 'Exact service module set required')
    root = '/data/local/tmp/mdds_dds_' + row['run'] + '_' + row['side'] + '/package/'
    for name, member in modules.items():
        expected_hash = package_files.get(member)
        require(isinstance(expected_hash, str) and re.fullmatch('[0-9a-f]{64}', expected_hash) and
                row['origins'][name] == {'path': root + member, 'member': member, 'sha256': expected_hash},
                'Service module outside owned manifest: ' + name)
    fields = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
    require(row.get('capsules') == {'Service': ['_TYPE_SUPPORT'], 'Request': fields, 'Response': fields},
            'Request/response/service capsules missing')
    cdr = row.get('cdr')
    require(isinstance(cdr, list) and len(cdr) == 2, 'Request/response CDR witnesses missing')
    a, b = operands(scenario, 'PRE')
    for witness, label, values in zip(cdr, ('Request', 'Response'), ([a, b], [a+b])):
        require(set(witness) == {'type', 'values', 'bytes', 'sha256', 'base64'} and witness['type'] == label and
                witness['values'] == values and all(type(n) is int for n in witness['values']), 'Deserialize values differ')
        payload({'event': 'CLIENT_' + label.upper(), 'phase': 'PRE', 'a': a, 'b': b, 'sum': a+b,
                 'cdr_base64': witness['base64'], 'cdr_bytes': witness['bytes'], 'cdr_sha256': witness['sha256']}, scenario)
    return {'modules': len(modules), 'capsules': 'SERVICE_REQUEST_RESPONSE', 'CDR_roundtrips': 2}


def service_endpoints(texts, scenario, a_run):
    request, reply = 'rq/service_restart_' + scenario + 'Request', 'rr/service_restart_' + scenario + 'Reply'
    anchors = fields(texts['A'], 'NATIVE_ENDPOINT_ANCHOR')
    result = {}
    for role, topic in (('writer', request), ('reader', reply)):
        selected = []
        for point in ('BEFORE', 'PROCESS_AFTER'):
            match = [r for r in anchors if r.get('point') == point and r.get('topic') == topic]
            require(len(match) == 1 and match[0].get('role') == role and match[0].get('run') == a_run and
                    match[0].get('side') == 'A' and match[0].get('identity_source') == 'SDK_SEND_SEDP' and
                    re.fullmatch('[0-9a-f]{32}', match[0].get('guid', '')), 'Actual persistent service endpoint missing')
            selected.append(match[0]['guid'])
        require(selected[0] == selected[1], 'Service client endpoint recreated')
        result[role] = {'topic': topic, 'guid': selected[0]}
    require(result['writer']['guid'] != result['reader']['guid'], 'Request/reply endpoint identity aliased')
    for key in INSTANCES:
        required = {('writer', request), ('reader', reply)} if key == 'A' else {('reader', request), ('writer', reply)}
        guarded = fields(texts[key], 'FDDST_ENDPOINT')
        relevant = [r for r in guarded if r.get('topic') in (request, reply)]
        require(len(relevant) == 2 and {(r.get('role'), r.get('topic')) for r in relevant} == required and
                all(r.get('data_sharing') == 'OFF' for r in relevant), 'Service DDS endpoint role set differs')
    return result


def all_content(records, scenario, package_files):
    require(set(records) == set(INSTANCES), 'Exact three original instances required')
    summaries = {k: final(v, scenario, k) for k, v in records.items()}
    qualifications = {k: type_support(v, scenario, package_files) for k, v in records.items()}
    pre = pair(records['A'], records['B0'], scenario, 'PRE')
    post = pair(records['A'], records['B1'], scenario, 'NEW')
    old_request = one(records['A'], 'CLIENT_REQUEST', 'OLD')
    b_old = select(records['B1'], 'SERVER_REQUEST', 'OLD')
    a_old = select(records['A'], 'CLIENT_RESPONSE', 'OLD')
    if b_old:
        require(payload(old_request, scenario) == payload(b_old[0], scenario), 'Late OLD request differs')
    if a_old:
        require(b_old and payload(a_old[0], scenario) == payload(one(records['B1'], 'SERVER_RESPONSE', 'OLD'), scenario),
                'OLD response lacks B1 evidence')
    return {'pre': pre, 'new': post, 'instances': summaries, 'type_support': qualifications, 'new_request_recovery': True,
            'application_requests': 3, 'application_retries': 0, 'default_automatic_reconnect': 'NOT_CLAIMED'}
