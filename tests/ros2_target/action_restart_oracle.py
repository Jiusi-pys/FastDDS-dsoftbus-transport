"""Independent UUID, CDR, feedback and result evidence for finite Action recovery."""
import base64
import hashlib
import json
import re
import struct
from process_restart_oracle import require, unique, fields, owners, a_continuity

INSTANCES = ('A', 'B0', 'B1')
EVENTS = {'TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY', 'ACTION_READY', 'CLIENT_GOAL', 'CLIENT_ACCEPTED',
          'CLIENT_STATUS',
          'OLD_EXECUTING',
          'CLIENT_RESULT_REQUEST', 'CLIENT_FEEDBACK', 'CLIENT_FEEDBACK_COMPLETE', 'CLIENT_RESULT',
          'SERVER_GOAL', 'SERVER_FEEDBACK', 'SERVER_RESULT', 'OLD_HELD', 'OLD_PENDING', 'ACTION_UNAVAILABLE',
          'CONTROL_ACCEPTED', 'PHASE_COMPLETE', 'FINISH_ACCEPTED', 'OLD_FINAL_STATE', 'OLD_LOCAL_DISPOSE',
          'OLD_SERVER_ABANDON', 'ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
SUFFIX = '.cpython-312-aarch64-linux-ohos.so'
MODULES = {'rclpy._rclpy_pybind11': 'python/rclpy/_rclpy_pybind11' + SUFFIX,
           'rclpy.action.client': 'python/rclpy/action/client.py', 'rclpy.action.server': 'python/rclpy/action/server.py',
           'example_interfaces.action._fibonacci': 'python/example_interfaces/action/_fibonacci.py',
           'example_interfaces.example_interfaces_s__rosidl_typesupport_c':
               'python/example_interfaces/example_interfaces_s__rosidl_typesupport_c' + SUFFIX,
           'action_msgs.srv._cancel_goal': 'python/action_msgs/srv/_cancel_goal.py',
           'action_msgs.msg._goal_status_array': 'python/action_msgs/msg/_goal_status_array.py',
           'action_msgs.action_msgs_s__rosidl_typesupport_c': 'python/action_msgs/action_msgs_s__rosidl_typesupport_c' + SUFFIX,
           'unique_identifier_msgs.msg._uuid': 'python/unique_identifier_msgs/msg/_uuid.py',
           'unique_identifier_msgs.unique_identifier_msgs_s__rosidl_typesupport_c':
               'python/unique_identifier_msgs/unique_identifier_msgs_s__rosidl_typesupport_c' + SUFFIX}
CAPSULE_FIELDS = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
CAPSULES = {label: ['_TYPE_SUPPORT'] if label in ('Action', 'SendGoal', 'GetResult', 'CancelGoal') else CAPSULE_FIELDS
            for label in ('Action', 'Goal', 'Feedback', 'Result', 'SendGoal', 'GetResult', 'FeedbackMessage',
                          'CancelGoal', 'GoalStatusArray', 'UUID')}


def identity(scenario, phase):
    require(re.fullmatch('[0-9a-f]{16}', scenario) and phase in ('PRE', 'OLD', 'NEW'), 'Goal generation invalid')
    return hashlib.sha256(('R30/' + scenario + '/' + phase).encode('ascii')).hexdigest()[:32], {'PRE': 6, 'OLD': 7, 'NEW': 8}[phase]


def sequence(order):
    values = [0, 1]
    while len(values) < order + 1:
        values.append(sum(values[-2:]))
    return values


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
                row['monotonic_ns'] > 0 and row.get('event') in EVENTS, 'Action process record invalid')
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
    uuid, order = identity(scenario, row.get('phase'))
    require(row.get('uuid') == uuid and type(row.get('order')) is int and row['order'] == order, 'Foreign UUID/order')
    event = row['event']
    goal, feedback = event.endswith('_GOAL'), event.endswith('_FEEDBACK')
    require(goal or feedback or event.endswith('_RESULT'), 'Invalid payload event')
    expected = [order] if goal else sequence(order)
    if feedback:
        index = row.get('index')
        require(type(index) is int and 1 <= index <= (1 if row['phase'] == 'OLD' else order - 1), 'Feedback index invalid')
        expected = expected[:index + 2]
    if event.endswith('_RESULT'):
        require(type(row.get('status')) is int and row['status'] == (0 if row['phase'] == 'OLD' else 4), 'Wrong terminal status')
        if row['phase'] == 'OLD':
            expected = []
    data = base64.b64decode(row.get('cdr_base64', ''), validate=True)
    require(len(data) >= 4 and data[:4] in (b'\0\1\0\0', b'\0\0\0\0'), 'Invalid Fibonacci CDR header')
    fmt = '<' if data[1] else '>'
    if goal:
        require(len(data) == 8, 'Invalid Goal CDR size')
        values = list(struct.unpack(fmt + 'i', data[4:]))
    else:
        require(len(data) >= 8, 'Sequence CDR missing length')
        size = struct.unpack(fmt + 'I', data[4:8])[0]
        require(size <= 9 and len(data) == 8 + 4 * size, 'Invalid sequence CDR size')
        values = list(struct.unpack(fmt + 'i' * size, data[8:]))
    require(values == expected and row.get('values') == expected and all(type(n) is int for n in row['values']),
            'Fibonacci CDR/content mismatch')
    require(row.get('cdr_sha256') == hashlib.sha256(data).hexdigest() and type(row.get('cdr_bytes')) is int and
            row['cdr_bytes'] == len(data), 'CDR digest/length mismatch')
    return data


def old_accepted(a, b, scenario):
    uuid, order = identity(scenario, 'OLD')
    sent, taken = one(a, 'CLIENT_GOAL', 'OLD'), one(b, 'SERVER_GOAL', 'OLD')
    accepted, requested = one(a, 'CLIENT_ACCEPTED', 'OLD'), one(a, 'CLIENT_RESULT_REQUEST', 'OLD')
    require(payload(sent, scenario) == payload(taken, scenario) and taken.get('status') == 1 and
            accepted.get('accepted') is True and accepted.get('uuid') == requested.get('uuid') == uuid and
            type(accepted.get('order')) is int and accepted['order'] == order and
            sent['seq'] < accepted['seq'] < requested['seq'] and
            not select(b, 'OLD_EXECUTING') and not select(b, 'SERVER_FEEDBACK', 'OLD') and
            not select(a, 'CLIENT_FEEDBACK', 'OLD') and not select(a, 'CLIENT_RESULT', 'OLD'),
            'OLD must await permission after observed acceptance; no early execution/feedback/result')
    return {'uuid': uuid, 'order': order, 'accepted': True, 'result_requested': True, 'execution_not_released': True}


def feedback_pair(a, b, scenario, phase):
    uuid, order = identity(scenario, phase)
    sent, taken = one(a, 'CLIENT_GOAL', phase), one(b, 'SERVER_GOAL', phase)
    accepted, request = one(a, 'CLIENT_ACCEPTED', phase), one(a, 'CLIENT_RESULT_REQUEST', phase)
    require(payload(sent, scenario) == payload(taken, scenario) and taken.get('status') == (1 if phase == 'OLD' else 2),
            'Goal peer CDR/state differs')
    require(accepted.get('accepted') is True and accepted.get('uuid') == request.get('uuid') == uuid and
            type(accepted.get('order')) is int and accepted['order'] == order and
            sent['seq'] < accepted['seq'] < request['seq'], 'Goal acceptance/result request identity/order')
    client, server = select(a, 'CLIENT_FEEDBACK', phase), select(b, 'SERVER_FEEDBACK', phase)
    count = 1 if phase == 'OLD' else order - 1
    require(len(client) == len(server) == count, 'Exact feedback cardinality required')
    for index, (received, published) in enumerate(zip(client, server), 1):
        require(received.get('index') == published.get('index') == index and
                sent['seq'] < received['seq'] and taken['seq'] < published['seq'] and
                payload(received, scenario) == payload(published, scenario), 'Feedback UUID/order/peer CDR differs')
    complete = one(a, 'CLIENT_FEEDBACK_COMPLETE', phase)
    require(type(complete.get('count')) is int and complete['count'] == count and complete['seq'] > client[-1]['seq'],
            'Feedback completion invalid')
    if phase == 'OLD':
        started, control = one(b, 'OLD_EXECUTING'), one(b, 'CONTROL_ACCEPTED', 'START_OLD')
        require(started.get('phase') == 'OLD' and started.get('uuid') == uuid and started.get('status') == 2 and
                taken['seq'] < control['seq'] < started['seq'] < server[0]['seq'], 'OLD start permission/state missing')
    return {'phase': phase, 'uuid': uuid, 'order': order, 'feedback_count': count, 'feedback_CDR': 'PEER_COPIES_EQUAL'}


def pair(a, b, scenario, phase):
    require(phase in ('PRE', 'NEW'), 'OLD cannot satisfy recovery pair')
    result = feedback_pair(a, b, scenario, phase)
    received, sent = one(a, 'CLIENT_RESULT', phase), one(b, 'SERVER_RESULT', phase)
    control = one(b, 'CONTROL_ACCEPTED', 'COMPLETE_' + phase)
    require(any(r.get('status') == 4 for r in select(a, 'CLIENT_STATUS', phase)), 'SUCCEEDED status topic not observed')
    require(payload(received, scenario) == payload(sent, scenario) and
            one(a, 'CLIENT_FEEDBACK_COMPLETE', phase)['seq'] < received['seq'] and
            one(a, 'CLIENT_RESULT_REQUEST', phase)['seq'] < received['seq'] and
            select(b, 'SERVER_FEEDBACK', phase)[-1]['seq'] < control['seq'] < sent['seq'], 'Result precedes feedback/control')
    if phase == 'NEW':
        lost, ready, released = one(a, 'ACTION_UNAVAILABLE'), one(a, 'ACTION_READY', 'NEW'), one(a, 'CONTROL_ACCEPTED', 'POST')
        require(one(a, 'OLD_PENDING')['seq'] < lost['seq'] < released['seq'] < ready['seq'] <
                one(a, 'CLIENT_GOAL', 'NEW')['seq'] and lost.get('ready') is False and
                type(lost.get('old_done')) is bool and ready.get('ready') is True, 'No disappearance/rediscovery before NEW')
    return dict(result, result_status='SUCCEEDED', result_CDR='PEER_COPIES_EQUAL')


def pending(records):
    marker = one(records, 'OLD_PENDING')
    require(any(r.get('status') == 2 and r['seq'] < marker['seq'] for r in select(records, 'CLIENT_STATUS', 'OLD')),
            'OLD EXECUTING status not observed')
    require(one(records, 'CLIENT_RESULT', 'PRE')['seq'] < one(records, 'CONTROL_ACCEPTED', 'OLD')['seq'] <
            one(records, 'CLIENT_GOAL', 'OLD')['seq'] < one(records, 'CLIENT_ACCEPTED', 'OLD')['seq'] <
            one(records, 'CLIENT_RESULT_REQUEST', 'OLD')['seq'] < marker['seq'] and
            one(records, 'CLIENT_FEEDBACK_COMPLETE', 'OLD')['seq'] < marker['seq'] and
            marker.get('checkpoint') == 'BEFORE_B0_STOP' and marker.get('accepted') is True and
            type(marker.get('feedback_count')) is int and marker['feedback_count'] == 1 and
            marker.get('done') is False and marker.get('cancelled') is False, 'OLD not separately accepted/feedback/pending')
    return {'old_goal_accepted': True, 'feedback_count': 1, 'result_pending_before_stop': True, 'not_recovery_success': True}


def type_support(records, scenario, package_files):
    row = one(records, 'TYPE_SUPPORT_READY')
    require(row.get('interface') == 'example_interfaces/action/Fibonacci' and row.get('node_created') is False and
            row.get('soabi') == 'cpython-312-aarch64-linux-ohos' and row.get('capsules') == CAPSULES,
            'Action native typesupport/ABI/capsules missing')
    require(isinstance(package_files, dict) and set(row.get('origins', {})) == set(MODULES), 'Exact Action module set required')
    root = '/data/local/tmp/mdds_dds_' + row['run'] + '_' + row['side'] + '/package/'
    for name, member in MODULES.items():
        digest = package_files.get(member)
        require(isinstance(digest, str) and re.fullmatch('[0-9a-f]{64}', digest) and
                row['origins'][name] == {'path': root + member, 'member': member, 'sha256': digest}, 'Unselected module: ' + name)
    witnesses = row.get('cdr')
    require(isinstance(witnesses, list) and len(witnesses) == 3, 'Three native CDR witnesses required')
    uuid, order = identity(scenario, 'PRE')
    for witness, label in zip(witnesses, ('Goal', 'Feedback', 'Result')):
        require(set(witness) == {'type', 'values', 'bytes', 'sha256', 'base64'} and witness['type'] == label,
                'CDR witness identity invalid')
        payload(dict(event='CLIENT_' + label.upper(), phase='PRE', uuid=uuid, order=order, index=1, status=4,
                     values=witness['values'], cdr_base64=witness['base64'], cdr_bytes=witness['bytes'],
                     cdr_sha256=witness['sha256']), scenario)
    return {'modules': len(MODULES), 'capsules': list(CAPSULES), 'CDR_roundtrips': 3}


def final(records, scenario, instance):
    require([r['event'] for r in records[:3]] == ['TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY'], 'Startup order')
    one(records, 'TYPE_SUPPORT_READY')
    one(records, 'NODE_READY')
    entity = one(records, 'ENTITY_READY')
    require(all(entity.get(k) == v for k, v in {'name': '/action_restart_' + scenario,
            'interface': 'example_interfaces/action/Fibonacci', 'entity': 'client' if instance == 'A' else 'server',
            'qos': 'SELECTED_PUBLIC_DEFAULTS'}.items()), 'Actual Action entity missing')
    common = {'TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY', 'PHASE_COMPLETE', 'FINISH_ACCEPTED', 'CONTROL_ACCEPTED',
              'ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
    specific = ({'ACTION_READY', 'CLIENT_GOAL', 'CLIENT_ACCEPTED', 'CLIENT_RESULT_REQUEST', 'CLIENT_FEEDBACK', 'CLIENT_STATUS',
                 'CLIENT_FEEDBACK_COMPLETE', 'CLIENT_RESULT', 'OLD_PENDING', 'ACTION_UNAVAILABLE',
                 'OLD_FINAL_STATE', 'OLD_LOCAL_DISPOSE'} if instance == 'A' else
                {'SERVER_GOAL', 'SERVER_FEEDBACK', 'SERVER_RESULT'} |
                ({'OLD_HELD', 'OLD_EXECUTING', 'OLD_SERVER_ABANDON'} if instance == 'B0' else set()))
    require(all(r['event'] in common | specific for r in records), 'Event belongs to another process role')
    finish, complete = one(records, 'FINISH_ACCEPTED'), one(records, 'PHASE_COMPLETE')
    require(finish.get('reason') == 'NORMAL_COMPLETION' and complete.get('phase') == ('OLD_HELD' if instance == 'B0' else 'NEW') and
            complete['seq'] < finish['seq'] and [r['event'] for r in records[-4:]] ==
            ['ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'] and
            finish['seq'] < records[-4]['seq'] and one(records, 'FINAL').get('success') is True and
            records[-1].get('node_created') is True, 'Normal completion/teardown missing')
    for event in ('ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN'):
        require(one(records, event).get('returned') is True, 'Public cleanup did not return')
    require(one(records, 'ENTITY_DESTROY_RETURN').get('entity') == ('client' if instance == 'A' else 'server'), 'Wrong destroyed entity')
    phases = ('PRE', 'OLD', 'NEW') if instance == 'A' else ('PRE', 'OLD') if instance == 'B0' else ('NEW',)
    goal_event = 'CLIENT_GOAL' if instance == 'A' else 'SERVER_GOAL'
    require([r.get('phase') for r in select(records, goal_event)] == list(phases), 'Goal count/generation differs')
    for event in ('CLIENT_ACCEPTED', 'CLIENT_RESULT_REQUEST', 'CLIENT_FEEDBACK_COMPLETE') if instance == 'A' else ():
        require([r.get('phase') for r in select(records, event)] == list(phases), 'Acknowledgement/feedback count differs')
    for row in records:
        if row['event'] in ('CLIENT_GOAL', 'SERVER_GOAL', 'CLIENT_FEEDBACK', 'SERVER_FEEDBACK', 'CLIENT_RESULT', 'SERVER_RESULT'):
            require(row.get('phase') in phases, 'Foreign goal generation')
            payload(row, scenario)
    if instance == 'A':
        for row in select(records, 'CLIENT_STATUS'):
            require(row.get('phase') in phases and row.get('uuid') == identity(scenario, row['phase'])[0] and
                    type(row.get('status')) is int and row['status'] in (0, 1, 2, 4) and
                    (row['phase'] != 'OLD' or row['status'] != 4), 'Foreign/invalid status observation')
        pending(records)
        require([r.get('phase') for r in select(records, 'ACTION_READY')] == ['PRE', 'NEW'] and
                [r.get('phase') for r in select(records, 'CONTROL_ACCEPTED')] == ['OLD', 'POST'] and
                one(records, 'ACTION_READY', 'PRE').get('ready') is True and
                one(records, 'ACTION_READY', 'PRE')['seq'] < one(records, 'CLIENT_GOAL', 'PRE')['seq'] and
                one(records, 'CLIENT_RESULT', 'NEW')['seq'] < complete['seq'], 'Client stages differ')
        old = select(records, 'CLIENT_RESULT', 'OLD')
        require(len(select(records, 'CLIENT_RESULT')) == 2 + len(old) and len(old) <= 1, 'Result count differs')
        state = one(records, 'OLD_FINAL_STATE')
        require(type(state.get('done')) is bool and state['done'] == bool(old) and state.get('cancelled') is False and
                state.get('response_observed') is bool(old) and state['seq'] > finish['seq'], 'OLD final state differs')
        if old:
            require(one(records, 'ACTION_UNAVAILABLE')['seq'] < old[0]['seq'] < state['seq'] and
                    not select(records, 'OLD_LOCAL_DISPOSE'), 'OLD UNKNOWN outcome accounting differs')
        else:
            disposed = one(records, 'OLD_LOCAL_DISPOSE')
            require(disposed.get('cancelled') is True and disposed.get('remote_cancel') is False and
                    state['seq'] < disposed['seq'] < records[-4]['seq'], 'Pending OLD not locally disposed')
        return {'old_outcome': 'STATUS_UNKNOWN_EMPTY_RESULT' if old else 'PENDING_LOCALLY_CANCELLED', 'remote_cancel': 'NOT_CLAIMED'}
    expected = 'PRE' if instance == 'B0' else 'NEW'
    require([r.get('phase') for r in select(records, 'SERVER_RESULT')] == [expected] and
            [r.get('phase') for r in select(records, 'CONTROL_ACCEPTED')] ==
            (['COMPLETE_PRE', 'START_OLD'] if instance == 'B0' else ['COMPLETE_NEW']) and
            one(records, 'SERVER_RESULT', expected)['seq'] < complete['seq'], 'Server completion differs')
    if instance == 'B0':
        held, abandoned = one(records, 'OLD_HELD'), one(records, 'OLD_SERVER_ABANDON')
        require(held.get('status') == abandoned.get('status') == 2 and held.get('result_set') is False and
                held.get('callback_active') is False and abandoned.get('result_set') is False and
                abandoned.get('remote_cancel') is False and one(records, 'SERVER_FEEDBACK', 'OLD')['seq'] <
                held['seq'] < complete['seq'] < finish['seq'] < abandoned['seq'] < records[-4]['seq'], 'OLD server state differs')
    return {'normal_server_close': True, 'old_goal_recovery': 'NOT_CLAIMED'}


def action_endpoints(texts, scenario, a_run):
    name = 'action_restart_' + scenario + '/_action/'
    expected = {('writer', 'rq/' + name + n + 'Request') for n in ('send_goal', 'get_result', 'cancel_goal')}
    expected |= {('reader', 'rr/' + name + n + 'Reply') for n in ('send_goal', 'get_result', 'cancel_goal')}
    expected |= {('reader', 'rt/' + name + n) for n in ('feedback', 'status')}
    anchors, result = fields(texts['A'], 'NATIVE_ENDPOINT_ANCHOR'), {}
    for role, topic in sorted(expected):
        selected = []
        for point in ('BEFORE', 'PROCESS_AFTER'):
            matches = [r for r in anchors if r.get('point') == point and r.get('topic') == topic]
            require(len(matches) == 1 and matches[0].get('role') == role and matches[0].get('run') == a_run and
                    matches[0].get('side') == 'A' and matches[0].get('identity_source') == 'SDK_SEND_SEDP' and
                    re.fullmatch('[0-9a-f]{32}', matches[0].get('guid', '')), 'Persistent Action endpoint missing')
            selected.append(matches[0]['guid'])
        require(selected[0] == selected[1], 'Action client endpoint recreated')
        result[topic] = {'role': role, 'guid': selected[0]}
    require(len({r['guid'] for r in result.values()}) == 8, 'Action endpoint identity aliased')
    topics = {topic for _, topic in expected}
    for key in INSTANCES:
        needed = expected if key == 'A' else {('reader' if role == 'writer' else 'writer', topic) for role, topic in expected}
        relevant = [r for r in fields(texts[key], 'FDDST_ENDPOINT') if r.get('topic') in topics]
        require(len(relevant) == 8 and {(r.get('role'), r.get('topic')) for r in relevant} == needed and
                all(r.get('data_sharing') == 'OFF' for r in relevant), 'Action endpoint role set differs')
    return result


def all_content(records, scenario, package_files):
    require(set(records) == set(INSTANCES), 'Exact three original instances required')
    summaries = {k: final(v, scenario, k) for k, v in records.items()}
    qualification = {k: type_support(v, scenario, package_files) for k, v in records.items()}
    pre = pair(records['A'], records['B0'], scenario, 'PRE')
    old = feedback_pair(records['A'], records['B0'], scenario, 'OLD')
    post = pair(records['A'], records['B1'], scenario, 'NEW')
    return {'pre': pre, 'old': old, 'new': post, 'instances': summaries, 'type_support': qualification,
            'new_goal_recovery': True, 'application_goals': 3, 'application_retries': 0,
            'default_automatic_reconnect': 'NOT_CLAIMED', 'old_goal_recovery': 'NOT_CLAIMED', 'cancel_behavior': 'NOT_TESTED'}
