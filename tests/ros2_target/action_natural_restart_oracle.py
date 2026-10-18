"""Strict natural PRE/NEW witnesses; OLD observations never qualify recovery."""
import base64
import hashlib
import json
import re
import struct
import action_restart_oracle as R30

require, unique, select, one = R30.require, R30.unique, R30.select, R30.one
identity, sequence, fields = R30.identity, R30.sequence, R30.fields
owners, a_continuity, action_endpoints = R30.owners, R30.a_continuity, R30.action_endpoints
INSTANCES, MODULES, CAPSULES = R30.INSTANCES, R30.MODULES, R30.CAPSULES
EVENTS = R30.EVENTS - {'CONTROL_ACCEPTED', 'OLD_PENDING', 'OLD_EXECUTING', 'OLD_HELD', 'OLD_SERVER_ABANDON'} | {'OLD_SERVER_FINAL'}


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
                row['monotonic_ns'] > 0 and row.get('event') in EVENTS, 'Natural Action process record invalid')
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Clock regressed')
        result.append(row)
    return result


def payload(row, scenario):
    uuid, order = identity(scenario, row.get('phase'))
    require(row.get('uuid') == uuid and type(row.get('order')) is int and row['order'] == order, 'Foreign UUID/order')
    event = row['event']
    goal, feedback = event.endswith('_GOAL'), event.endswith('_FEEDBACK')
    require(goal or feedback or event.endswith('_RESULT'), 'Invalid payload event')
    expected = [order] if goal else sequence(order)
    if feedback:
        index = row.get('index')
        require(type(index) is int and 1 <= index <= order - 1, 'Feedback index invalid')
        expected = expected[:index + 2]
    if event.endswith('_RESULT'):
        status = row.get('status')
        require(type(status) is int and (status == 4 or (event == 'CLIENT_RESULT' and row['phase'] == 'OLD' and status == 0)),
                'Wrong terminal result status')
        if status == 0:
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


def accepted_pair(a, b, scenario, phase):
    uuid, order = identity(scenario, phase)
    sent, taken = one(a, 'CLIENT_GOAL', phase), one(b, 'SERVER_GOAL', phase)
    accepted, requested = one(a, 'CLIENT_ACCEPTED', phase), one(a, 'CLIENT_RESULT_REQUEST', phase)
    require(payload(sent, scenario) == payload(taken, scenario) and taken.get('status') == 2 and
            accepted.get('accepted') is True and accepted.get('uuid') == requested.get('uuid') == uuid and
            type(accepted.get('order')) is int and accepted['order'] == order and
            sent['seq'] < accepted['seq'] < requested['seq'], 'Goal acceptance/request/peer CDR differs')
    return {'phase': phase, 'uuid': uuid, 'order': order}


def pair(a, b, scenario, phase):
    require(phase in ('PRE', 'NEW'), 'OLD cannot satisfy successful NEW')
    result = accepted_pair(a, b, scenario, phase)
    count = result['order'] - 1
    client, server = select(a, 'CLIENT_FEEDBACK', phase), select(b, 'SERVER_FEEDBACK', phase)
    require(len(client) == len(server) == count, 'Exact fixed-fixture feedback count required')
    for index, (received, published) in enumerate(zip(client, server), 1):
        require(received.get('index') == published.get('index') == index and
                one(a, 'CLIENT_GOAL', phase)['seq'] < received['seq'] and
                one(b, 'SERVER_GOAL', phase)['seq'] < published['seq'] and
                payload(received, scenario) == payload(published, scenario), 'Feedback order/peer CDR differs')
    complete = one(a, 'CLIENT_FEEDBACK_COMPLETE', phase)
    received, sent = one(a, 'CLIENT_RESULT', phase), one(b, 'SERVER_RESULT', phase)
    require(complete.get('count') == count and type(complete['count']) is int and
            client[-1]['seq'] < complete['seq'] < received['seq'] and
            one(a, 'CLIENT_RESULT_REQUEST', phase)['seq'] < received['seq'] and
            server[-1]['seq'] < sent['seq'] and payload(received, scenario) == payload(sent, scenario),
            'Natural result/feedback witnesses invalid')
    if phase == 'NEW':
        lost, ready = one(a, 'ACTION_UNAVAILABLE'), one(a, 'ACTION_READY', 'NEW')
        require(one(a, 'CLIENT_RESULT_REQUEST', 'OLD')['seq'] < lost['seq'] < ready['seq'] <
                one(a, 'CLIENT_GOAL', 'NEW')['seq'] and lost.get('ready') is False and
                type(lost.get('old_done')) is bool and ready.get('ready') is True, 'No local disappearance/rediscovery before NEW')
    return dict(result, feedback_count=count, result_status='SUCCEEDED', peer_CDR='EQUAL', intermediate_status='OBSERVATION_ONLY')


def old_observation(a, b, scenario):
    result = accepted_pair(a, b, scenario, 'OLD')
    client, server = select(a, 'CLIENT_FEEDBACK', 'OLD'), select(b, 'SERVER_FEEDBACK', 'OLD')
    published = {r['index']: payload(r, scenario) for r in server}
    require(len(server) == len(published) == 6 and [r['index'] for r in server] == list(range(1, 7)), 'OLD finite server work differs')
    indexes = [r['index'] for r in client]
    require(indexes == sorted(set(indexes)) and all(payload(r, scenario) == published.get(r['index']) for r in client),
            'OLD feedback is not a valid observed subset')
    sent = one(b, 'SERVER_RESULT', 'OLD')
    payload(sent, scenario)
    client_goal, server_goal = one(a, 'CLIENT_GOAL', 'OLD'), one(b, 'SERVER_GOAL', 'OLD')
    require(all(client_goal['seq'] < row['seq'] for row in client) and
            all(server_goal['seq'] < row['seq'] < sent['seq'] for row in server) and
            one(b, 'SERVER_RESULT', 'PRE')['seq'] < server_goal['seq'],
            'OLD same-process causal order invalid')
    observed = select(a, 'CLIENT_RESULT', 'OLD')
    require(len(observed) <= 1, 'Duplicate OLD result')
    if observed:
        payload(observed[0], scenario)
        require(observed[0]['seq'] > one(a, 'CLIENT_RESULT_REQUEST', 'OLD')['seq'], 'OLD result before request')
        if observed[0]['status'] == 4:
            require(payload(observed[0], scenario) == payload(sent, scenario), 'OLD result peer mismatch')
    return dict(result, feedback_observed=len(client), server_result='SUCCEEDED',
                client_outcome=('SUCCEEDED' if observed[0]['status'] == 4 else 'UNKNOWN_EMPTY') if observed else 'PENDING',
                result_before_disappearance=bool(observed and observed[0]['seq'] < one(a, 'ACTION_UNAVAILABLE')['seq']),
                old_goal_recovery='NOT_CLAIMED', remote_cancel='NOT_CLAIMED')


def final(records, scenario, instance):
    require([r['event'] for r in records[:3]] == ['TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY'], 'Startup order')
    one(records, 'TYPE_SUPPORT_READY'); one(records, 'NODE_READY')
    entity = one(records, 'ENTITY_READY')
    require(all(entity.get(k) == v for k, v in {'name': '/action_restart_' + scenario,
            'interface': 'example_interfaces/action/Fibonacci', 'entity': 'client' if instance == 'A' else 'server',
            'qos': 'SELECTED_PUBLIC_DEFAULTS'}.items()), 'Actual Action entity differs')
    common = {'TYPE_SUPPORT_READY', 'NODE_READY', 'ENTITY_READY', 'PHASE_COMPLETE', 'FINISH_ACCEPTED',
              'ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
    specific = ({'ACTION_READY', 'CLIENT_GOAL', 'CLIENT_ACCEPTED', 'CLIENT_RESULT_REQUEST', 'CLIENT_FEEDBACK',
                 'CLIENT_STATUS', 'CLIENT_FEEDBACK_COMPLETE', 'CLIENT_RESULT', 'ACTION_UNAVAILABLE',
                 'OLD_FINAL_STATE', 'OLD_LOCAL_DISPOSE'} if instance == 'A' else
                {'SERVER_GOAL', 'SERVER_FEEDBACK', 'SERVER_RESULT'} | ({'OLD_SERVER_FINAL'} if instance == 'B0' else set()))
    require(all(r['event'] in common | specific for r in records), 'Foreign role or business control event')
    finish, complete = one(records, 'FINISH_ACCEPTED'), one(records, 'PHASE_COMPLETE')
    require(finish.get('reason') == 'NORMAL_COMPLETION' and complete.get('phase') == ('OLD_FINITE' if instance == 'B0' else 'NEW') and
            complete['seq'] < finish['seq'] < records[-4]['seq'] and [r['event'] for r in records[-4:]] ==
            ['ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'] and
            one(records, 'FINAL').get('success') is True and records[-1].get('node_created') is True, 'Normal teardown missing')
    for event in ('ENTITY_DESTROY_RETURN', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN'):
        require(one(records, event).get('returned') is True, 'Public cleanup did not return')
    require(one(records, 'ENTITY_DESTROY_RETURN').get('entity') == ('client' if instance == 'A' else 'server'), 'Destroyed entity differs')
    phases = ('PRE', 'OLD', 'NEW') if instance == 'A' else ('PRE', 'OLD') if instance == 'B0' else ('NEW',)
    require([r.get('phase') for r in select(records, 'CLIENT_GOAL' if instance == 'A' else 'SERVER_GOAL')] == list(phases), 'Goal generation/order/count')
    for row in records:
        if row['event'] in ('CLIENT_GOAL', 'SERVER_GOAL', 'CLIENT_FEEDBACK', 'SERVER_FEEDBACK', 'CLIENT_RESULT', 'SERVER_RESULT'):
            require(row.get('phase') in phases, 'Foreign phase')
            payload(row, scenario)
    if instance == 'A':
        for event in ('CLIENT_ACCEPTED', 'CLIENT_RESULT_REQUEST'):
            require([r.get('phase') for r in select(records, event)] == list(phases), 'Goal acknowledgement count')
        for row in select(records, 'CLIENT_STATUS'):
            require(row.get('phase') in phases and row.get('uuid') == identity(scenario, row['phase'])[0] and
                    type(row.get('status')) is int and 0 <= row['status'] <= 6, 'Invalid status observation')
        require(one(records, 'CLIENT_RESULT', 'PRE')['seq'] < one(records, 'CLIENT_GOAL', 'OLD')['seq'] and
                one(records, 'CLIENT_RESULT', 'NEW')['seq'] < complete['seq'], 'Autonomous business order invalid')
        ready = select(records, 'ACTION_READY')
        require([r.get('phase') for r in ready] == ['PRE', 'NEW'] and ready[0].get('ready') is True and
                ready[0]['seq'] < one(records, 'CLIENT_GOAL', 'PRE')['seq'], 'Readiness phases invalid')
        object_id = entity.get('client_object_id')
        require(type(object_id) is int and object_id > 0 and all(r.get('client_object_id') == object_id for r in
                [*ready, one(records, 'ACTION_UNAVAILABLE')]), 'ActionClient object changed')
        old = select(records, 'CLIENT_RESULT', 'OLD')
        require(len(select(records, 'CLIENT_RESULT')) == 2 + len(old) and len(old) <= 1, 'Result count invalid')
        state = one(records, 'OLD_FINAL_STATE')
        require(type(state.get('done')) is bool and state['done'] == bool(old) and state.get('cancelled') is False and
                state.get('response_observed') is bool(old) and finish['seq'] < state['seq'] < records[-4]['seq'], 'OLD final state invalid')
        if not old:
            disposed = one(records, 'OLD_LOCAL_DISPOSE')
            require(disposed.get('cancelled') is True and disposed.get('remote_cancel') is False and
                    state['seq'] < disposed['seq'] < records[-4]['seq'], 'Pending OLD local disposal invalid')
        else:
            require(not select(records, 'OLD_LOCAL_DISPOSE'), 'Completed OLD falsely disposed')
    else:
        require([r.get('phase') for r in select(records, 'SERVER_RESULT')] == list(phases) and
                select(records, 'SERVER_RESULT')[-1]['seq'] < complete['seq'], 'Finite server results missing')
        if instance == 'B0':
            state = one(records, 'OLD_SERVER_FINAL')
            require(state.get('status') == 4 and state.get('result_set') is True and state.get('remote_cancel') is False and
                    finish['seq'] < state['seq'] < records[-4]['seq'], 'OLD server outcome invalid')
    return {'normal_close': True, 'intermediate_client_status': 'OBSERVATION_ONLY'}


def all_content(records, scenario, package_files):
    require(set(records) == set(INSTANCES), 'Exact three instances required')
    summaries = {k: final(v, scenario, k) for k, v in records.items()}
    qualification = {k: R30.type_support(v, scenario, package_files) for k, v in records.items()}
    return {'pre': pair(records['A'], records['B0'], scenario, 'PRE'),
            'new': pair(records['A'], records['B1'], scenario, 'NEW'),
            'old': old_observation(records['A'], records['B0'], scenario),
            'instances': summaries, 'type_support': qualification, 'application_goals': 3, 'application_retries': 0,
            'new_goal_success_after_replacement': True, 'old_goal_recovery': 'NOT_CLAIMED',
            'default_automatic_reconnect': 'NOT_CLAIMED', 'business_permissions': 'FORBIDDEN'}
