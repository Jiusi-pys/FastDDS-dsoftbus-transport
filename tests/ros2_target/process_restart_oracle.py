"""Strict per-instance content and both-lane observation checks; no dispatch."""
import base64
import hashlib
import json
from pathlib import Path
import re
import runpy

CDR = Path(__file__).with_name('bag_sqlite_oracle.py')
if hashlib.sha256(CDR.read_bytes()).hexdigest() != '88ab482bee3acfca529b1b61d21605c078b3a50fa42444ab7ee3c98b857b5c8b':
    raise ValueError('Frozen String CDR oracle changed')
decode_string = runpy.run_path(str(CDR))['decode_string_cdr']


def require(value, message):
    if not value:
        raise ValueError(message)


def unique(pairs):
    out = {}
    for key, value in pairs:
        require(key not in out, 'Duplicate JSON field')
        out[key] = value
    return out


def fields(text, prefix):
    result = []
    for line in text.splitlines():
        if line.startswith(prefix + ' '):
            tokens = [token.split('=', 1) for token in line[len(prefix) + 1:].split()]
            require(all(len(t) == 2 for t in tokens), 'Malformed ' + prefix)
            result.append(unique(tokens))
    return result


def rows(text, scenario, instance, run, pid):
    events = {'NODE_READY', 'ENTITY_READY', 'SEND', 'RECEIVE', 'ECHO', 'PHASE_COMPLETE',
              'POST_ACCEPTED', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
    require(re.fullmatch('[0-9a-f]{16}', scenario) and re.fullmatch('[0-9a-f]{16}', run) and
            instance in ('A', 'B0', 'B1') and type(pid) is int and pid > 1, 'Invalid process identity')
    result = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=unique)
        expected = {'schema': 1, 'scenario': scenario, 'instance': instance, 'run': run, 'side': instance[0],
                    'pid': pid, 'seq': len(result) + 1, 'role': 'requester' if instance == 'A' else 'responder'}
        require(isinstance(row, dict) and all(row.get(k) == v for k, v in expected.items()) and
                all(type(row.get(k)) is int for k in ('schema', 'pid', 'seq', 'monotonic_ns')) and
                row['monotonic_ns'] > 0 and row.get('event') in events, 'Process record identity/event invalid')
        require(not result or row['monotonic_ns'] >= result[-1]['monotonic_ns'], 'Process clock regressed')
        result.append(row)
    return result


def content(records, scenario, instance, phase, *, final=False):
    phases = ['PRE'] if instance == 'B0' else ['POST'] if instance == 'B1' else ['PRE', 'POST']
    wanted = phases if phase == 'ALL' else [phase]
    require(all(p in phases for p in wanted), 'Wrong instance phase')
    payloads = [r for r in records if r['event'] in ('SEND', 'RECEIVE', 'ECHO')]
    chosen = [r for r in payloads if ('PRE' if r.get('sequence', -1) < 5 else 'POST') in wanted]
    numbers = list(range(5)) if wanted == ['PRE'] else list(range(5, 10)) if wanted == ['POST'] else list(range(10))
    require(len(chosen) == len(numbers) * 2, 'Incomplete/excess phase payloads')
    pattern = ('SEND', 'RECEIVE') if instance == 'A' else ('RECEIVE', 'ECHO')
    decoded = {}
    for index, row in enumerate(chosen):
        n = numbers[index // 2]
        expected = 'r27_' + scenario + '_' + str(n).zfill(2)
        require(type(row.get('sequence')) is int and row['sequence'] == n and row['event'] == pattern[index % 2] and
                row.get('text') == expected, 'Duplicate/reordered/foreign content')
        data = base64.b64decode(row['cdr_base64'], validate=True)
        require(decode_string(data) == expected and row.get('cdr_sha256') == hashlib.sha256(data).hexdigest() and
                type(row.get('cdr_bytes')) is int and row['cdr_bytes'] == len(data), 'CDR mismatch')
        decoded[(n, row['event'])] = data
    for p in wanted:
        complete = [r for r in records if r['event'] == 'PHASE_COMPLETE' and r.get('phase') == p]
        last = 4 if p == 'PRE' else 9
        require(len(complete) == 1 and type(complete[0].get('received')) is int and complete[0]['received'] == last + 1 and
                complete[0]['seq'] > max(r['seq'] for r in chosen if r['sequence'] == last), 'Phase checkpoint missing/order')
    require(len([r for r in records if r['event'] == 'NODE_READY']) == 1 and
            len([r for r in records if r['event'] == 'ENTITY_READY']) == 1 and
            [r['event'] for r in records[:2]] == ['NODE_READY', 'ENTITY_READY'], 'Node recreated/startup incomplete')
    if instance == 'A' and 'POST' in wanted:
        post = [r for r in records if r['event'] == 'POST_ACCEPTED']
        pre = [r for r in records if r['event'] == 'PHASE_COMPLETE' and r.get('phase') == 'PRE']
        require(len(post) == len(pre) == 1 and pre[0]['seq'] < post[0]['seq'] <
                min(r['seq'] for r in chosen if r['sequence'] == 5), 'POST not explicitly separated from PRE')
    if final:
        require([r['event'] for r in records[-4:]] ==
                ['FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'] and
                records[-4].get('reason') == 'NORMAL_COMPLETION' and records[-3].get('returned') is True and
                records[-2].get('returned') is True and records[-1].get('success') is True and
                records[-1].get('node_created') is True, 'Normal public teardown missing')
        require(len(payloads) == len(numbers) * 2 and
                [r.get('phase') for r in records if r['event'] == 'PHASE_COMPLETE'] == phases, 'Unexpected phase/payload')
    return decoded


def pair_content(a, b, scenario, phase):
    lhs, rhs = content(a, scenario, 'A', phase), content(b, scenario, 'B0' if phase == 'PRE' else 'B1', phase)
    for n in (range(5) if phase == 'PRE' else range(5, 10)):
        require(lhs[n, 'SEND'] == rhs[n, 'RECEIVE'] == rhs[n, 'ECHO'] == lhs[n, 'RECEIVE'],
                'Four public CDR records differ')
    return {'phase': phase, 'roundtrips': 5, 'logical_publications': 10, 'CDR': 'FOUR_COPIES_EQUAL'}


def owners(text, run, stage):
    observed = fields(text, 'NATIVE_PROCESS_OWNERS')
    allowed = ['OLD_BOTH_READY', 'NEW_BOTH_ACTIVE']
    require([r.get('stage') for r in observed] == allowed[:len(observed)] and len(observed) <= 2,
            'Process owner event cardinality/order')
    for row in observed:
        expected = {'schema': '1', 'run': run, 'side': 'A', 'source': 'SDK_PUBLIC_SOCKET',
                    'callbacks': '0', 'receivers': '0', 'sdk_epoch': 'NOT_PROVEN', 'sdk_drain': 'NOT_PROVEN'}
        require(all(row.get(k) == v for k, v in expected.items()), 'Owner event source/identity/leases')
        for key in ('old_dial', 'old_accepted', 'listener', 'listener_entry', 'client_entry', 'wall_ms',
                    'old_dial_owner', 'old_accepted_owner'):
            require(re.fullmatch('[0-9]+', row.get(key, '')) is not None, 'Owner numeric identity missing')
        require(int(row['old_dial_owner']) > 0 and int(row['old_accepted_owner']) > 0 and int(row['wall_ms']) > 0,
                'Invalid old owner identity')
        for lane in ('dial', 'accepted'):
            require(row.get(lane + '_shutdown_returned') in ('0', '1') and row.get(lane + '_on_shutdown') in ('0', '1') and
                    '1' in (row[lane + '_shutdown_returned'], row[lane + '_on_shutdown']), 'Old owner not settled')
        if row['stage'] == 'NEW_BOTH_ACTIVE':
            old_ids = {int(row['old_dial']), int(row['old_accepted']), int(row['listener'])}
            old_owners = {int(row['old_dial_owner']), int(row['old_accepted_owner'])}
            for key in ('new_dial', 'new_accepted', 'new_dial_owner', 'new_accepted_owner'):
                require(re.fullmatch('[0-9]+', row.get(key, '')) is not None, 'New identity missing')
            new_ids = {int(row['new_dial']), int(row['new_accepted'])}
            new_owners = {int(row['new_dial_owner']), int(row['new_accepted_owner'])}
            require(len(new_ids) == len(new_owners) == 2 and not old_ids & new_ids and not old_owners & new_owners and
                    min(new_owners) > 0, 'Native IDs/owners reused')
    if len(observed) == 2:
        require(all(observed[0][k] == observed[1][k] for k in ('listener', 'listener_entry', 'client_entry',
                'old_dial', 'old_accepted', 'old_dial_owner', 'old_accepted_owner')) and
                int(observed[1]['wall_ms']) >= int(observed[0]['wall_ms']), 'Original native identity changed')
    found = [r for r in observed if r['stage'] == stage]
    return found[0] if found else None


def a_continuity(text, run):
    anchors = fields(text, 'NATIVE_ENTITY_ANCHOR')
    before = [r for r in anchors if r.get('point') == 'BEFORE']
    after = [r for r in anchors if r.get('point') == 'PROCESS_AFTER']
    require(len(before) == len(after) == 1, 'A wire participant anchors incomplete')
    for row in before + after:
        require(row.get('run') == run and row.get('side') == 'A' and row.get('identity_source') == 'SDK_SEND_SPDP_PREFIX' and
                re.fullmatch('[0-9a-f]{32}', row.get('guid', '')) and re.fullmatch('[1-9][0-9]*', row.get('runtime', '')),
                'A anchor source/identity invalid')
    require(before[0]['runtime'] == after[0]['runtime'] and before[0]['guid'] == after[0]['guid'], 'A recreated Runtime/Participant')
    endpoints = fields(text, 'NATIVE_ENDPOINT_ANCHOR')
    selected = []
    for point in ('BEFORE', 'PROCESS_AFTER'):
        subset = [r for r in endpoints if r.get('point') == point]
        require(subset and all(r.get('run') == run and r.get('side') == 'A' and r.get('identity_source') == 'SDK_SEND_SEDP'
                and re.fullmatch('[0-9a-f]{32}', r.get('guid', '')) and r.get('role') in ('writer', 'reader') for r in subset),
                'Endpoint anchor source invalid')
        values = {(r['role'], r['guid'], r['topic']) for r in subset}
        require(len(values) == len(subset), 'Duplicate endpoint anchor')
        selected.append(values)
    require(selected[0] == selected[1], 'A endpoint identities changed')
    return {'participant': before[0]['guid'], 'runtime': before[0]['runtime'], 'endpoints': len(selected[0])}
