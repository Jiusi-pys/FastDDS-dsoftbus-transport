"""Fail-closed joins for complete Host endpoint observations, not SDK identities.

Only unique reversed TCP tuples from the same controlled run are paired. Tuple
reuse is deliberately inconclusive even when local lifetimes do not overlap:
process-local sequence numbers cannot order lifetimes across processes.
"""
import re
from host_io_capture import validate_capture


def _row(line):
    tokens = line.split()
    pairs = [item.split('=', 1) for item in tokens[1:]]
    if any(len(pair) != 2 for pair in pairs):
        raise ValueError('connection syntax')
    row = dict(pairs)
    if len(row) != len(pairs):
        raise ValueError('duplicate field')
    return tokens[0], row


def _number(row, key, signed=False):
    value = row[key]
    if not re.fullmatch(('-?' if signed else '') + r'(0|[1-9][0-9]*)', value):
        raise ValueError('connection numeric field: ' + key)
    return int(value)


def _capture(text, run, side, pid):
    if not validate_capture(text, run, side, pid)['complete']:
        raise ValueError('incomplete IO capture')
    events, footers = [], []
    identity = {'schema': '1', 'run': run, 'side': side, 'pid': str(pid)}
    event_fields = set(identity) | {'seq', 'socket', 'epoch', 'handle', 'role',
        'local_address', 'local_port', 'peer_address', 'peer_port',
        'local_result', 'local_error', 'peer_result', 'peer_error'}
    footer_fields = set(identity) | {'capacity', 'reserved', 'published', 'overflow',
        'unpublished', 'complete', 'clock', 'tuple_reuse'}
    for line in text.splitlines():
        if not line.startswith('HOST_CONNECTION_'):
            continue
        kind, row = _row(line)
        if any(row.get(key) != value for key, value in identity.items()):
            raise ValueError('connection identity')
        if kind == 'HOST_CONNECTION_EVENT':
            if set(row) != event_fields or row['role'] not in ('LISTENER', 'ACCEPTED', 'OUTGOING'):
                raise ValueError('connection event fields')
            for key in event_fields - set(identity) - {'role'}:
                row[key] = _number(row, key, key in {'local_result', 'peer_result'})
            if row['seq'] <= 0 or row['epoch'] <= 0 or row['socket'] <= 0:
                raise ValueError('connection lifetime identity')
            if (row['local_result'] != 0 or row['local_error'] != 0
                    or not 0 < row['local_address'] <= 0xffffffff
                    or not 0 < row['local_port'] <= 65535):
                raise ValueError('local endpoint unavailable')
            if row['role'] == 'LISTENER':
                if (row['peer_result'], row['peer_error'], row['peer_address'], row['peer_port']) != (-1, 0, 0, 0):
                    raise ValueError('listener peer identity')
            elif (row['peer_result'] != 0 or row['peer_error'] != 0
                    or not 0 < row['peer_address'] <= 0xffffffff
                    or not 0 < row['peer_port'] <= 65535):
                raise ValueError('peer endpoint unavailable')
            events.append(row)
        elif kind == 'HOST_CONNECTION_CAPTURE':
            if set(row) != footer_fields:
                raise ValueError('connection capture fields')
            footers.append(row)
        else:
            raise ValueError('unknown connection record')
    if len(footers) != 1:
        raise ValueError('connection footer count')
    footer = footers[0]
    count = _number(footer, 'reserved')
    if (_number(footer, 'capacity') != 64 or not 0 < count <= 64
            or _number(footer, 'published') != count or len(events) != count
            or _number(footer, 'overflow') or _number(footer, 'unpublished')
            or footer['complete'] != '1' or footer['clock'] != 'PROCESS_LOCAL'
            or footer['tuple_reuse'] != 'UNKNOWN'):
        raise ValueError('incomplete connection capture')
    io = [_row(line)[1] for line in text.splitlines() if line.startswith('HOST_IO_EVENT ')]
    installs = [row for row in io if row['kind'] == 'CONNECTION_INSTALL']
    keys = [(e['socket'], e['epoch'], e['handle']) for e in events]
    if len(set(keys)) != len(keys) or len(installs) != len(events):
        raise ValueError('duplicate or missing installation')
    sequences = [e['seq'] for e in events] + [int(row['seq']) for row in io]
    if len(set(sequences)) != len(sequences):
        raise ValueError('duplicate observation sequence')
    for e in events:
        matching = [row for row in installs if all(int(row[k]) == e[k] for k in ('socket', 'epoch', 'handle'))]
        if len(matching) != 1 or int(matching[0]['seq']) >= e['seq']:
            raise ValueError('endpoint installation order')
        closes = [row for row in io if row['kind'] == 'CLOSE_RETURN'
                  and all(int(row[k]) == e[k] for k in ('socket', 'epoch', 'handle'))]
        if (len(closes) != 1 or int(closes[0]['seq']) <= e['seq']
                or closes[0]['result_known'] != '1' or closes[0]['result'] != '0'):
            raise ValueError('unqualified connection close lifetime')
    return [e for e in events if e['role'] != 'LISTENER']


def match_connections(texts, run, pids):
    """Return [{A: endpoint, B: endpoint}, ...], or raise on any incomplete join."""
    if set(texts) != {'A', 'B'} or set(pids) != {'A', 'B'} or pids['A'] == pids['B']:
        raise ValueError('two independent process identities required')
    sides = {side: _capture(texts[side], run, side, pids[side]) for side in 'AB'}
    if not sides['A'] or len(sides['A']) != len(sides['B']):
        raise ValueError('unmatched connection count')
    def endpoint(e):
        return e['local_address'], e['local_port'], e['peer_address'], e['peer_port']
    for rows in sides.values():
        if len({endpoint(row) for row in rows}) != len(rows):
            raise ValueError('tuple reuse is UNKNOWN')
    pairs = []
    for a in sides['A']:
        reverse = (a['peer_address'], a['peer_port'], a['local_address'], a['local_port'])
        matches = [b for b in sides['B'] if endpoint(b) == reverse]
        if len(matches) != 1 or {a['role'], matches[0]['role']} != {'ACCEPTED', 'OUTGOING'}:
            raise ValueError('ambiguous or absent reversed peer endpoint')
        pairs.append({'A': a, 'B': matches[0]})
    return pairs
