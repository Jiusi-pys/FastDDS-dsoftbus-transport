"""Read-only content oracle. PASS here never substitutes native-path/exit proof."""
import base64
import hashlib
import json
from pathlib import Path
import re
import runpy

ORACLE = Path(__file__).with_name('bag_sqlite_oracle.py')
if hashlib.sha256(ORACLE.read_bytes()).hexdigest() != '88ab482bee3acfca529b1b61d21605c078b3a50fa42444ab7ee3c98b857b5c8b':
    raise ValueError('Selected CDR decoder changed')
decode_string_cdr = runpy.run_path(str(ORACLE))['decode_string_cdr']
EVENTS = {'NODE_READY', 'ENTITY_READY', 'ROUNDTRIP_SEND', 'ROUNDTRIP_RECEIVE',
          'ROUNDTRIP_ECHO', 'RESOURCES', 'ROUNDTRIP_COMPLETE', 'FINISH_ACCEPTED',
          'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL'}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON key')
        result[key] = value
    return result


def roundtrip_rows(text, run, side, pid):
    if not re.fullmatch('[0-9a-f]{16}', run) or side not in ('A', 'B') or type(pid) is not int or pid <= 0:
        raise ValueError('Invalid run/side/PID')
    rows = []
    for line in text.splitlines():
        if not line.startswith('R25_PROBE '):
            continue
        row = json.loads(line[10:], object_pairs_hook=unique_object)
        expected = {'schema': 1, 'run': run, 'side': side, 'pid': pid, 'seq': len(rows) + 1,
                    'role': 'requester' if side == 'A' else 'responder'}
        if (not isinstance(row, dict) or any(row.get(k) != v for k, v in expected.items()) or
                any(type(row.get(k)) is not int for k in ('schema', 'pid', 'seq')) or row.get('event') not in EVENTS):
            raise ValueError('Probe identity, sequence or event differs')
        rows.append(row)
    return rows


def verify_roundtrip_logs(texts, run, pids, *, final=True):
    if set(texts) != {'A', 'B'} or set(pids) != {'A', 'B'}:
        raise ValueError('Exact pair required')
    contents, resources = {}, {}
    for side in 'AB':
        rows = roundtrip_rows(texts[side], run, side, pids[side])
        expected_events = ('ROUNDTRIP_SEND', 'ROUNDTRIP_RECEIVE') if side == 'A' else (
            'ROUNDTRIP_RECEIVE', 'ROUNDTRIP_ECHO')
        payloads = [r for r in rows if r['event'].startswith('ROUNDTRIP_') and r['event'] != 'ROUNDTRIP_COMPLETE']
        if len(payloads) != 400:
            raise ValueError('Expected 200 complete ordered exchanges on each side')
        times = []
        for index, row in enumerate(payloads):
            sequence = index // 2
            if (row['event'] != expected_events[index % 2] or type(row.get('sequence')) is not int or
                    row['sequence'] != sequence or row.get('text') != 'r26_' + run + '_' + str(sequence).zfill(3)):
                raise ValueError('Lost, duplicate, reordered or foreign exchange')
            data = base64.b64decode(row['cdr_base64'], validate=True)
            if (decode_string_cdr(data) != row['text'] or row.get('cdr_sha256') != hashlib.sha256(data).hexdigest() or
                    type(row.get('cdr_bytes')) is not int or row['cdr_bytes'] != len(data) or
                    type(row.get('monotonic_ns')) is not int or row['monotonic_ns'] <= 0):
                raise ValueError('Content/CDR/time mismatch')
            times.append(row['monotonic_ns'])
            contents.setdefault((sequence, index % 2, side), data)
        if any(a > b for a, b in zip(times, times[1:])):
            raise ValueError('Process event time regressed')
        if side == 'A' and any(b - a < 100_000_000 for a, b in zip(times[::2], times[2::2])):
            raise ValueError('Requests exceeded fixed10Hz pacing')
        samples = [r for r in rows if r['event'] == 'RESOURCES']
        if [r.get('received') for r in samples] != [0, 50, 100, 150, 200] or any(type(r.get('received')) is not int for r in samples) or any(
                type(r.get(k)) is not int or r[k] <= 0 for r in samples
                for k in ('VmRSS', 'VmHWM', 'Threads', 'fd_count', 'monotonic_ns')):
            raise ValueError('Incomplete or malformed actual resource samples')
        resources[side] = samples
        singles = {event: [r for r in rows if r['event'] == event] for event in (
            'NODE_READY', 'ENTITY_READY', 'ROUNDTRIP_COMPLETE', 'FINISH_ACCEPTED',
            'NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN', 'FINAL')}
        required = list(singles) if final else list(singles)[:3]
        if any(len(singles[event]) != 1 for event in required):
            raise ValueError('Missing/duplicate lifecycle event')
        done = singles['ROUNDTRIP_COMPLETE'][0]
        if (type(done.get('received')) is not int or done['received'] != 200 or
                type(done.get('quiet_seconds')) not in (int, float) or not 2 <= done['quiet_seconds'] < 40 or
                type(done.get('monotonic_ns')) is not int or done['monotonic_ns'] - times[-1] < 2_000_000_000 or
                done['seq'] <= payloads[-1]['seq']):
            raise ValueError('Incomplete completion/quiet interval')
        node, entity = singles['NODE_READY'][0], singles['ENTITY_READY'][0]
        outgoing, incoming = ('request', 'reply') if side == 'A' else ('reply', 'request')
        if (node.get('name') != '/roundtrip_' + run + '_' + side or
                entity.get('entity') != ('requester' if side == 'A' else 'responder') or
                entity.get('outgoing') != '/roundtrip_' + run + '/' + outgoing or
                entity.get('incoming') != '/roundtrip_' + run + '/' + incoming or
                not node['seq'] < entity['seq'] < samples[0]['seq'] < payloads[0]['seq']):
            raise ValueError('Node/topic identity or startup ordering differs')
        for sample in samples[1:]:
            count = sample['received']
            lower = payloads[count * 2 - 1]['seq']
            upper = payloads[count * 2]['seq'] if count < 200 else done['seq']
            if not lower < sample['seq'] < upper:
                raise ValueError('Resource sample outside its exact sequence interval')
        stamped = [r['monotonic_ns'] for r in rows if r['event'] in
                   {'RESOURCES', 'ROUNDTRIP_SEND', 'ROUNDTRIP_RECEIVE', 'ROUNDTRIP_ECHO', 'ROUNDTRIP_COMPLETE'}]
        if any(a > b for a, b in zip(stamped, stamped[1:])):
            raise ValueError('Resource/content/completion clock regressed')
        if final:
            if (singles['FINISH_ACCEPTED'][0].get('reason') != 'NORMAL_COMPLETION' or
                    singles['FINAL'][0].get('success') is not True or singles['FINAL'][0].get('node_created') is not True or
                    any(singles[event][0].get('returned') is not True for event in ('NODE_DESTROY_RETURN', 'SHUTDOWN_RETURN')) or
                    any(singles[a][0]['seq'] >= singles[b][0]['seq'] for a, b in zip(required, required[1:]))):
                raise ValueError('Normal one-Node lifecycle is not established')
    for sequence in range(200):
        if len({contents[(sequence, index, side)] for index in (0, 1) for side in 'AB'}) != 1:
            raise ValueError('Request/receive/echo/reply CDR bytes differ')
    return {'scope': 'BOUNDED_200_STRING_ROUNDTRIP_CONTENT_ONLY', 'status': 'PASS',
            'roundtrips': 200, 'resources': resources, 'resource_leak_freedom': 'NOT_PROVEN',
            'native_path': 'REQUIRES_SEPARATE_EVIDENCE', 'process_exit': 'REQUIRES_SEPARATE_EVIDENCE',
            'process_restart': 'NOT_TESTED', 'participant_recreation': 'NOT_TESTED'}
