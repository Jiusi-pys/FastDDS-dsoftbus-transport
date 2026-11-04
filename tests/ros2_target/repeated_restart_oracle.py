"""Strict finite continuous A/B0/B1/B2 evidence, never a device dispatcher."""
import base64
import hashlib
import json
import re
from process_restart_oracle import require, unique, fields, decode_string

INSTANCES = ('A', 'B0', 'B1', 'B2')


def rows(text, scenario, instance, run, pid):
    events = {'NODE_READY', 'ENTITY_READY', 'SEND', 'RECEIVE', 'ECHO', 'PHASE_COMPLETE',
              'PHASE_ACCEPTED', 'FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}
    require(instance in INSTANCES and re.fullmatch('[0-9a-f]{16}', scenario) and
            re.fullmatch('[0-9a-f]{16}', run) and type(pid) is int and pid > 1, 'Invalid instance identity')
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
    phases = [0, 1, 2] if instance == 'A' else [int(instance[1])]
    wanted = phases if phase == 'ALL' else [phase]
    require(all(type(p) is int and p in phases for p in wanted), 'Wrong instance phase')
    payloads = [r for r in records if r['event'] in ('SEND', 'RECEIVE', 'ECHO')]
    require(all(type(r.get('sequence')) is int and 0 <= r['sequence'] < 15 for r in payloads), 'Invalid content ordinal')
    chosen = [r for r in payloads if r['sequence'] // 5 in wanted]
    numbers = [n for p in wanted for n in range(p * 5, (p + 1) * 5)]
    require(len(chosen) == len(numbers) * 2, 'Incomplete/excess phase content')
    pattern = ('SEND', 'RECEIVE') if instance == 'A' else ('RECEIVE', 'ECHO')
    decoded = {}
    for index, row in enumerate(chosen):
        n = numbers[index // 2]
        expected = 'r28_' + scenario + '_' + str(n).zfill(2)
        require(row['sequence'] == n and row['event'] == pattern[index % 2] and row.get('text') == expected,
                'Duplicate/reordered/foreign content')
        data = base64.b64decode(row['cdr_base64'], validate=True)
        require(decode_string(data) == expected and row.get('cdr_sha256') == hashlib.sha256(data).hexdigest() and
                type(row.get('cdr_bytes')) is int and row['cdr_bytes'] == len(data), 'CDR mismatch')
        decoded[n, row['event']] = data
    for p in wanted:
        complete = [r for r in records if r['event'] == 'PHASE_COMPLETE' and r.get('phase') == p]
        require(len(complete) == 1 and type(complete[0].get('phase')) is int and
                type(complete[0].get('received')) is int and complete[0]['received'] == (p + 1) * 5 and
                complete[0]['seq'] > max(r['seq'] for r in chosen if r['sequence'] == (p + 1) * 5 - 1),
                'Phase checkpoint missing/order')
        if instance == 'A' and p:
            accepted = [r for r in records if r['event'] == 'PHASE_ACCEPTED' and r.get('phase') == p]
            previous = [r for r in records if r['event'] == 'PHASE_COMPLETE' and r.get('phase') == p - 1]
            require(len(accepted) == len(previous) == 1 and type(accepted[0].get('phase')) is int and
                    previous[0]['seq'] < accepted[0]['seq'] < min(r['seq'] for r in chosen if r['sequence'] == p * 5),
                    'Phase lacks explicit sequential release')
    require([r['event'] for r in records[:2]] == ['NODE_READY', 'ENTITY_READY'] and
            sum(r['event'] == 'NODE_READY' for r in records) == sum(r['event'] == 'ENTITY_READY' for r in records) == 1,
            'Node/endpoint creation repeated or absent')
    if final:
        require([r['event'] for r in records[-4:]] == ['FINISH_ACCEPTED', 'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'] and
                records[-4].get('reason') == 'NORMAL_COMPLETION' and records[-3].get('returned') is True and
                records[-2].get('returned') is True and records[-1].get('success') is True and records[-1].get('node_created') is True,
                'Normal public teardown missing')
        require(len(payloads) == len(numbers) * 2 and
                [r.get('phase') for r in records if r['event'] == 'PHASE_COMPLETE'] == phases and
                [r.get('phase') for r in records if r['event'] == 'PHASE_ACCEPTED'] == ([1, 2] if instance == 'A' else []),
                'Unexpected phase/payload/release')
    return decoded


def pair_content(a, b, scenario, phase):
    lhs = content(a, scenario, 'A', phase)
    rhs = content(b, scenario, 'B' + str(phase), phase)
    for n in range(phase * 5, (phase + 1) * 5):
        require(lhs[n, 'SEND'] == rhs[n, 'RECEIVE'] == rhs[n, 'ECHO'] == lhs[n, 'RECEIVE'], 'Four CDR copies differ')
    return {'phase': phase, 'roundtrips': 5, 'logical_publications': 10, 'CDR': 'FOUR_COPIES_EQUAL'}


def owners(text, run, round, stage):
    observed = fields(text, 'NATIVE_REPEAT_EVENT')
    stages = ['ARMED', 'OLD_BOTH_READY', 'PERMIT_CONSUMED', 'NEW_BOTH_ACTIVE']
    expected_order = [(str(r), s) for r in (1, 2) for s in stages]
    require([(x.get('round'), x.get('stage')) for x in observed] == expected_order[:len(observed)] and len(observed) <= 8,
            'Repeated native event order/cardinality')
    ids, incarnations, previous = set(), set(), None
    fixed = ('listener', 'listener_entry', 'client_entry')
    old_keys = ('old_dial', 'old_accepted', 'old_dial_owner', 'old_accepted_owner')
    for row in observed:
        require(all(row.get(k) == v for k, v in {'schema': '1', 'run': run, 'side': 'A', 'source': 'SDK_PUBLIC_SOCKET',
                'sdk_epoch': 'NOT_PROVEN', 'sdk_drain': 'NOT_PROVEN'}.items()), 'Native event identity/source')
        for key in (*fixed, *old_keys, 'wall_ms'):
            require(re.fullmatch('[0-9]+', row.get(key, '')), 'Invalid native identity number')
        require(int(row['wall_ms']) > 0 and int(row['old_dial_owner']) > 0 and int(row['old_accepted_owner']) > 0,
                'Invalid old owner identity')
        require(row.get('permit_consumed') == ('1' if row['stage'] in stages[2:] else '0'), 'Permit lifecycle mismatch')
        if previous:
            require(all(row[k] == previous[k] for k in fixed) and int(row['wall_ms']) >= int(previous['wall_ms']),
                    'Listener/clock continuity failed')
            if row['round'] == previous['round']:
                require(all(row[k] == previous[k] for k in old_keys), 'Old round anchor changed')
            else:
                require(all(row['old_' + k] == previous['new_' + k] for k in ('dial', 'accepted', 'dial_owner', 'accepted_owner')),
                        'Round2 did not retire round1 owners')
        else:
            ids.update(int(row[k]) for k in ('listener', 'old_dial', 'old_accepted'))
            incarnations.update(int(row[k]) for k in ('old_dial_owner', 'old_accepted_owner'))
            require(len(ids) == 3 and len(incarnations) == 2, 'Original identity collision')
        if row['stage'] != 'ARMED':
            require(row.get('old_callbacks') == row.get('old_receives') == '0', 'Old work remains entered')
            for lane in ('dial', 'accepted'):
                require(row.get('retired_' + lane) == row['old_' + lane] and
                        row.get('retired_' + lane + '_owner') == row['old_' + lane + '_owner'] and
                        row.get(lane + '_shutdown_returned') in ('0', '1') and row.get(lane + '_on_shutdown') in ('0', '1') and
                        '1' in (row[lane + '_shutdown_returned'], row[lane + '_on_shutdown']), 'Old owner retirement missing')
        if row['stage'] == 'NEW_BOTH_ACTIVE':
            require(all(re.fullmatch('[0-9]+', row.get('new_' + k, '')) for k in ('dial', 'accepted', 'dial_owner', 'accepted_owner')),
                    'New identity missing')
            fresh = {int(row['new_dial']), int(row['new_accepted'])}
            owners = {int(row['new_dial_owner']), int(row['new_accepted_owner'])}
            require(len(fresh) == len(owners) == 2 and min(owners) > 0 and not fresh & ids and not owners & incarnations,
                    'Any historical native ID/owner reused')
            ids.update(fresh)
            incarnations.update(owners)
        previous = row
    found = [r for r in observed if r['round'] == str(round) and r['stage'] == stage]
    return found[0] if found else None


def continuity(text, run, *, final=False):
    points = ['R28_SAMPLE0', 'R28_BEFORE1', 'R28_AFTER1', 'R28_SAMPLE1', 'R28_BEFORE2', 'R28_AFTER2', 'R28_SAMPLE2']
    anchors = fields(text, 'NATIVE_ENTITY_ANCHOR')
    require(anchors and [r.get('point') for r in anchors] == points[:len(anchors)] and len(anchors) <= len(points),
            'Unexpected/repeated entity anchor')
    if final:
        require(len(anchors) == len(points), 'Full A continuity missing')
    selected = []
    for row in anchors:
        require(row.get('run') == run and row.get('side') == 'A' and row.get('identity_source') == 'SDK_SEND_SPDP_PREFIX' and
                re.fullmatch('[0-9a-f]{32}', row.get('guid', '')) and re.fullmatch('[1-9][0-9]*', row.get('runtime', '')),
                'Invalid A entity anchor')
        require(row['guid'] == anchors[0]['guid'] and row['runtime'] == anchors[0]['runtime'], 'A Runtime/Participant recreated')
        subset = [r for r in fields(text, 'NATIVE_ENDPOINT_ANCHOR') if r.get('point') == row['point']]
        require(subset and all(r.get('run') == run and r.get('side') == 'A' and r.get('identity_source') == 'SDK_SEND_SEDP' and
                re.fullmatch('[0-9a-f]{32}', r.get('guid', '')) and r.get('role') in ('reader', 'writer') for r in subset),
                'Invalid endpoint anchor')
        values = {(r['role'], r['guid'], r['topic']) for r in subset}
        require(len(values) == len(subset) and (not selected or values == selected[0]), 'A endpoints changed')
        selected.append(values)
    return {'participant': anchors[0]['guid'], 'runtime': anchors[0]['runtime'], 'endpoints': len(selected[0]), 'points': len(anchors)}


def resources(text, run, *, final=False):
    rows = fields(text, 'NATIVE_REPEAT_RESOURCE')
    require([r.get('phase') for r in rows] == ['0', '1', '2'][:len(rows)] and len(rows) <= 3, 'Resource phase order')
    if final:
        require(len(rows) == 3, 'Resource samples incomplete')
    required = set('schema run side phase owners owned_ids high_water namespaces callbacks receives ready listening retained '
                   'failed_retained setup_reserved setup_entered send_reserved send_entered close_reserved close_entered '
                   'owner_limit dial accepted listener socket_calls shutdown_calls shutdown_returns wall_ms source sdk_drain'.split())
    seen_ids, previous = set(), None
    for row in rows:
        require(set(row) == required, 'Resource schema incomplete or extended')
        require(all(row.get(k) == v for k, v in {'schema': '1', 'run': run, 'side': 'A', 'source': 'SDK_PUBLIC_SOCKET',
                'sdk_drain': 'NOT_PROVEN'}.items()), 'Resource source/identity')
        numeric = set(row) - {'run', 'side', 'source', 'sdk_drain'}
        require(all(re.fullmatch('[0-9]+', row[k]) for k in numeric), 'Invalid resource metric')
        values = {k: int(row[k]) for k in numeric}
        phase = values['phase']
        # Retired SDK-visible cells remain charged. Extra candidates can consume
        # the same finite pool; 3/5/7 is a lower bound, not a general exact count.
        require(3 + 2 * phase <= values['owned_ids'] <= values['owners'] <= values['high_water'] <= values['owner_limit'] == 12 and
                1 <= values['namespaces'] <= 8 and values['ready'] == 2 and values['listening'] == 1 and
                values['retained'] >= 2 * phase and values['ready'] + values['listening'] + values['retained'] <= values['owners'] and
                values['failed_retained'] == 0 and values['wall_ms'] > 0 and values['socket_calls'] >= 2 + phase and
                values['shutdown_returns'] <= values['shutdown_calls'], 'Resource invariant failed')
        for operation, limit in (('setup', 1), ('send', 2), ('close', 1)):
            require(values[operation + '_reserved'] + values[operation + '_entered'] <= limit,
                    'Operation occupancy exceeds unchanged limit')
        current = {values['dial'], values['accepted']}
        require(len(current) == 2 and values['listener'] not in current and not current & seen_ids,
                'Resource live IDs repeat history/listener')
        seen_ids.update(current)
        if previous:
            require(row['listener'] == previous['listener'] and row['namespaces'] == previous['namespaces'] and
                    all(values[k] >= int(previous[k]) for k in ('owners', 'owned_ids', 'high_water', 'socket_calls',
                        'shutdown_calls', 'shutdown_returns', 'wall_ms')), 'Resource history/clock regressed')
        active = owners(text, run, phase, 'NEW_BOTH_ACTIVE') if phase else owners(text, run, 1, 'ARMED')
        require(phase == 0 or active is not None, 'Resource phase lacks actual owner activation')
        if active:
            prefix = 'new_' if phase else 'old_'
            require(all(row[k] == active[prefix + k] for k in ('dial', 'accepted')) and row['listener'] == active['listener'],
                    'Resource IDs differ from native round anchors')
            require(values['wall_ms'] >= int(active['wall_ms']) if phase else values['wall_ms'] <= int(active['wall_ms']),
                    'Resource sample precedes activation or follows initial retirement')
        next_arm = owners(text, run, phase + 1, 'ARMED') if phase < 2 else None
        if next_arm:
            require(values['wall_ms'] <= int(next_arm['wall_ms']) and
                    all(row[k] == next_arm['old_' + k] for k in ('dial', 'accepted')), 'Resource sample after next round began')
        previous = row
    return {'samples': rows, 'owners_include_retired_history': True, 'operation_usage_is_current_occupancy': True,
            'leak_freedom': 'NOT_PROVEN'}


def completion(text, run):
    rows = fields(text, 'NATIVE_REPEAT_COMPLETE')
    require(len(rows) <= 1, 'Duplicate observation completion')
    if not rows:
        return None
    row = rows[0]
    expected = {'schema': '1', 'run': run, 'side': 'A', 'round': '2', 'phase': '2', 'permit_closed': '1',
                'source': 'SDK_PUBLIC_SOCKET', 'sdk_epoch': 'NOT_PROVEN', 'sdk_drain': 'NOT_PROVEN'}
    require(set(row) == set(expected) | {'wall_ms'} and all(row.get(k) == v for k, v in expected.items()) and
            re.fullmatch('[1-9][0-9]*', row.get('wall_ms', '')), 'Invalid observation completion')
    samples = resources(text, run, final=True)['samples']
    require(int(row['wall_ms']) >= int(samples[-1]['wall_ms']), 'Completion preceded final resource sample')
    return row
