"""Strict fixed-window typed String oracle; synthetic logs are not ROS evidence."""
import re


def analyze_fragment(texts, run, pids, *, reliability):
    def require(ok, why):
        if not ok:
            raise ValueError(why)
    require(reliability in ('reliable', 'best_effort'), 'fixed reliability window')
    require(set(texts) == set(pids) == {'A', 'B'} and pids['A'] != pids['B'], 'two process identities')
    require(re.fullmatch('[0-9a-f]{16}', run) and all(type(p) is int and p > 0 for p in pids.values()), 'run/PID format')
    schemas = {
        'READY': {'namespace', 'pub_topic', 'sub_topic', 'gid', 'impl', 'reliability', 'durability', 'history', 'depth'},
        'MATCHED': {'publishers', 'subscriptions'}, 'START': {'peers'},
        'PHASE_READY': {'phase'}, 'PHASE_START': {'phase', 'peer_pid'},
        'PUBLISH_ATTEMPT': {'phase', 'id', 'bytes', 'cdr_length'}, 'RETURN': {'phase', 'id'},
        'RECEIVE': {'phase', 'id', 'payload_hex', 'gid', 'impl', 'sequence', 'cdr_length'},
        'PHASE_DONE': {'phase', 'published', 'received', 'missing'}, 'PHASE_CONFIRM': {'phase', 'peer_pid'},
        'BUSINESS_DONE': {'published', 'received', 'pending'}, 'OBSERVATION_END': {'received', 'finite'},
        'CLOSED': {'result'}}
    rows = {}
    for side in 'AB':
        rows[side] = []
        for line in texts[side].splitlines():
            if not line.startswith('FRAGMENT_ACCEPT'):
                continue
            pairs = [token.split('=', 1) for token in line.split()[1:]]
            require(line.startswith('FRAGMENT_ACCEPT ') and all(len(p) == 2 for p in pairs), 'record syntax')
            row = dict(pairs)
            event = row.get('event')
            require(len(row) == len(pairs) and event in schemas, 'event/schema')
            require(set(row) == schemas[event] | {'run', 'side', 'pid', 'local', 'event'}, 'record fields')
            require((row['run'], row['side'], row['pid'], row['local']) ==
                    (run, side, str(pids[side]), str(len(rows[side])+1)), 'record identity/order')
            rows[side].append(row)

    def body(row):
        return {k: v for k, v in row.items() if k not in {'run', 'side', 'pid', 'local'}}

    def select(side, event, **fields):
        return [r for r in rows[side] if r['event'] == event and all(r.get(k) == str(v) for k, v in fields.items())]

    def one(side, event, **fields):
        found = select(side, event, **fields)
        require(len(found) == 1, 'exactly one '+event+' '+str(fields))
        return found[0]

    def payload(side, phase, number):
        size = 1024 if phase == 'small' else 66000
        header = f'run={run};qos={reliability};side={side};phase={phase};id={number};body='.encode()
        salt = number + (0 if side == 'A' else 7) + {'small': 0, 'large': 11, 'burst': 19}[phase]
        return header + bytes(97+(index+salt) % 26 for index in range(len(header), size))

    writers = {s: one(s, 'READY')['gid'] for s in 'AB'}
    require(all(re.fullmatch('[0-9a-f]{32}', g) and g != '0'*32 for g in writers.values()) and
            writers['A'] != writers['B'], 'distinct nonzero publisher GIDs')
    ns = '/fragment_'+run
    mappings = {}
    for side in 'AB':
        peer = 'B' if side == 'A' else 'A'
        expected = [dict(event='READY', namespace=ns, pub_topic=ns+'/from_'+side,
                         sub_topic=ns+'/from_'+peer, gid=writers[side], impl='rmw_fastrtps_cpp',
                         reliability=reliability, durability='volatile', history='keep_last', depth='10'),
                    dict(event='MATCHED', publishers='1', subscriptions='1'), dict(event='START', peers='1')]
        sequences = set()
        mappings[side] = []
        for phase in ('small', 'large', 'burst'):
            expected.extend([dict(event='PHASE_READY', phase=phase),
                             dict(event='PHASE_START', phase=phase, peer_pid=str(pids[peer]))])
            incoming = select(side, 'RECEIVE', phase=phase)
            received_ids = {r['id'] for r in incoming}
            require(len(incoming) == 5 and received_ids == {'1', '2', '3', '4', '5'},
                    f'{side}/{phase} missing IDs: '+','.join(sorted({'1', '2', '3', '4', '5'}-received_ids)))
            published, returned, received = {}, {}, {}
            size = 1024 if phase == 'small' else 66000
            for number in range(1, 6):
                pub = one(side, 'PUBLISH_ATTEMPT', phase=phase, id=number)
                ret = one(side, 'RETURN', phase=phase, id=number)
                rec = one(side, 'RECEIVE', phase=phase, id=number)
                for row in (pub, rec):
                    require(re.fullmatch('[1-9][0-9]*', row['cdr_length']) and
                            size < int(row['cdr_length']) <= size+32, 'actual CDR length bounds')
                require(rec['cdr_length'] == one(peer, 'PUBLISH_ATTEMPT', phase=phase, id=number)['cdr_length'],
                        'peer serialization length agreement')
                require(re.fullmatch('[1-9][0-9]*', rec['sequence']) and
                        int(rec['sequence']) < 2**64-1 and rec['sequence'] not in sequences,
                        'publication sequence zero/unsupported/duplicate')
                sequences.add(rec['sequence'])
                published[number] = dict(event='PUBLISH_ATTEMPT', phase=phase, id=str(number),
                                         bytes=str(size), cdr_length=pub['cdr_length'])
                returned[number] = dict(event='RETURN', phase=phase, id=str(number))
                received[number] = dict(event='RECEIVE', phase=phase, id=str(number),
                                        payload_hex=payload(peer, phase, number).hex(), gid=writers[peer],
                                        impl='rmw_fastrtps_cpp', sequence=rec['sequence'], cdr_length=rec['cdr_length'])
                mappings[side].append(dict(phase=phase, id=number, publisher_gid=writers[peer],
                                           publication_sequence=int(rec['sequence'])))
            if phase == 'burst':
                for number in range(1, 6):
                    expected.extend([published[number], returned[number]])
                expected.extend(received[int(r['id'])] for r in incoming)
            else:
                for number in range(1, 6):
                    expected.extend([published[number], returned[number], received[number]] if side == 'A' else
                                    [received[number], published[number], returned[number]])
            expected.extend([dict(event='PHASE_DONE', phase=phase, published='5', received='5', missing='-'),
                             dict(event='PHASE_CONFIRM', phase=phase, peer_pid=str(pids[peer]))])
        expected.extend([dict(event='BUSINESS_DONE', published='15', received='15', pending='0'),
                         dict(event='OBSERVATION_END', received='15', finite='1'), dict(event='CLOSED', result='0')])
        require([body(r) for r in rows[side]] == expected, 'phase/payload/QoS/GID/local causality/closure contract')
    return dict(reliability=reliability, phases={'small': 1024, 'large': 66000, 'burst': 66000},
                publishes=30, received=30, samples_per_direction_per_phase=5, all_required=True,
                received_sequence_map=mappings, sequence_equals_ordinal_claimed=False,
                wire_fragment_causality_claimed=False, best_effort_loss_allowed=False)
