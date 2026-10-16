"""Synthetic String business and retained budget controls; not actual ROS evidence."""
import copy
import unittest
from fragment_capture import analyze_fragment
from host_receive_capture import validate_receive_capture
from test_host_receive_capture import events as budget_events, text as budget_text

RUN = '0123456789abcdef'
PIDS = {'A': 123, 'B': 456}
GIDS = {'A': '0102030405060708090a0b0c0d0e0f10', 'B': '1112131415161718191a1b1c1d1e1f20'}


def model(reliability='reliable'):
    """Independent fixed positive script; sequence numbers deliberately are not IDs."""
    rows = {'A': [], 'B': []}
    ns = '/fragment_'+RUN
    def add(side, event, **fields):
        rows[side].append(dict(event=event, **{k: str(v) for k, v in fields.items()}))
    def content(side, phase, number):
        prefix = ('run='+RUN+';qos='+reliability+';side='+side+';phase='+phase+';id='+str(number)+';body=').encode()
        length = 1024 if phase == 'small' else 66000
        offset = number + (7 if side == 'B' else 0) + {'small': 0, 'large': 11, 'burst': 19}[phase]
        return prefix + bytes(ord('a')+(i+offset) % 26 for i in range(len(prefix), length))
    for side in 'AB':
        peer = 'B' if side == 'A' else 'A'
        add(side, 'READY', namespace=ns, pub_topic=ns+'/from_'+side, sub_topic=ns+'/from_'+peer,
            gid=GIDS[side], impl='rmw_fastrtps_cpp', reliability=reliability,
            durability='volatile', history='keep_last', depth=10)
        add(side, 'MATCHED', publishers=1, subscriptions=1)
        add(side, 'START', peers=1)
        for phase_index, phase in enumerate(('small', 'large', 'burst')):
            size = 1024 if phase == 'small' else 66000
            add(side, 'PHASE_READY', phase=phase)
            add(side, 'PHASE_START', phase=phase, peer_pid=PIDS[peer])
            def receive(number):
                add(side, 'RECEIVE', phase=phase, id=number, payload_hex=content(peer, phase, number).hex(),
                    gid=GIDS[peer], impl='rmw_fastrtps_cpp', sequence=700+phase_index*100+number*3,
                    cdr_length=size+9)
            for number in range(1, 6):
                if phase != 'burst' and side == 'B':
                    receive(number)
                add(side, 'PUBLISH_ATTEMPT', phase=phase, id=number, bytes=size, cdr_length=size+9)
                add(side, 'RETURN', phase=phase, id=number)
                if phase != 'burst' and side == 'A':
                    receive(number)
            if phase == 'burst':
                for number in (3, 1, 5, 2, 4):
                    receive(number)
            add(side, 'PHASE_DONE', phase=phase, published=5, received=5, missing='-')
            add(side, 'PHASE_CONFIRM', phase=phase, peer_pid=PIDS[peer])
        add(side, 'BUSINESS_DONE', published=15, received=15, pending=0)
        add(side, 'OBSERVATION_END', received=15, finite=1)
        add(side, 'CLOSED', result=0)
    return rows


def text(rows):
    return {side: '\n'.join(f'FRAGMENT_ACCEPT run={RUN} side={side} pid={PIDS[side]} local={i} '+
                           ' '.join(k+'='+v for k, v in row.items()) for i, row in enumerate(records, 1))
            for side, records in rows.items()}


class FragmentReceiptTests(unittest.TestCase):
    def row(self, rows, side, event, **fields):
        return next(r for r in rows[side] if r['event'] == event and all(r.get(k) == str(v) for k, v in fields.items()))

    def reject(self, rows, reliability='reliable'):
        with self.assertRaises(ValueError):
            analyze_fragment(text(rows), RUN, PIDS, reliability=reliability)

    def test_both_fixed_windows_all_sixty_samples(self):
        results = [analyze_fragment(text(model(q)), RUN, PIDS, reliability=q) for q in ('reliable', 'best_effort')]
        self.assertEqual(sum(r['publishes'] for r in results), 60)
        self.assertEqual(sum(r['received'] for r in results), 60)
        for result in results:
            self.assertFalse(result['best_effort_loss_allowed'])
            self.assertFalse(result['sequence_equals_ordinal_claimed'])
            self.assertFalse(result['wire_fragment_causality_claimed'])
            self.assertEqual(len(result['received_sequence_map']['A']), 15)

    def test_payload_qos_gid_and_phase_fields(self):
        cases = [('READY', {}, 'gid', '0'*32), ('READY', {}, 'gid', GIDS['B']),
                 ('READY', {}, 'reliability', 'best_effort'), ('READY', {}, 'history', 'keep_all'),
                 ('READY', {}, 'depth', '1'), ('READY', {}, 'durability', 'transient_local'),
                 ('READY', {}, 'pub_topic', '/wrong'),
                 ('RECEIVE', {'phase': 'large', 'id': 3}, 'payload_hex', '00'),
                 ('RECEIVE', {'phase': 'burst', 'id': 4}, 'gid', GIDS['A']),
                 ('RECEIVE', {'phase': 'small', 'id': 1}, 'impl', 'foreign_rmw'),
                 ('RECEIVE', {'phase': 'large', 'id': 5}, 'phase', 'small'),
                 ('PUBLISH_ATTEMPT', {'phase': 'burst', 'id': 2}, 'bytes', '66001'),
                 ('PHASE_START', {'phase': 'large'}, 'peer_pid', '123'),
                 ('PHASE_CONFIRM', {'phase': 'small'}, 'peer_pid', '999')]
        for event, fields, key, value in cases:
            with self.subTest(event=event, key=key):
                rows = model()
                self.row(rows, 'A', event, **fields)[key] = value
                self.reject(rows)

    def test_sequence_zero_unsupported_noncanonical_and_duplicates(self):
        for value in ('0', str(2**64-1), str(2**64), '-1', 'nan', '0703'):
            rows = model()
            self.row(rows, 'A', 'RECEIVE', phase='small', id=1)['sequence'] = value
            self.reject(rows)
        rows = model()
        self.row(rows, 'A', 'RECEIVE', phase='burst', id=5)['sequence'] = self.row(rows, 'A', 'RECEIVE', phase='small', id=2)['sequence']
        self.reject(rows)

    def test_every_missing_sample_fails_including_best_effort(self):
        for reliability in ('reliable', 'best_effort'):
            for side in 'AB':
                for phase in ('small', 'large', 'burst'):
                    for number in range(1, 6):
                        rows = model(reliability)
                        rows[side].remove(self.row(rows, side, 'RECEIVE', phase=phase, id=number))
                        self.reject(rows, reliability)

    def test_duplicate_sample_and_count_relabel_do_not_hide_loss(self):
        rows = model()
        missing = self.row(rows, 'A', 'RECEIVE', phase='burst', id=5)
        index = rows['A'].index(missing)
        rows['A'][index] = copy.deepcopy(self.row(rows, 'A', 'RECEIVE', phase='burst', id=4))
        self.reject(rows)
        rows = model('best_effort')
        self.row(rows, 'B', 'PHASE_DONE', phase='burst').update(received='4', missing='5')
        self.reject(rows, 'best_effort')

    def test_actual_cdr_lengths_are_measured_bounded_and_peer_consistent(self):
        rows = model()
        self.row(rows, 'A', 'PUBLISH_ATTEMPT', phase='large', id=2)['cdr_length'] = '66017'
        self.row(rows, 'B', 'RECEIVE', phase='large', id=2)['cdr_length'] = '66017'
        analyze_fragment(text(rows), RUN, PIDS, reliability='reliable')
        for length in ('66000', '66033', '0', '066009'):
            rows = model()
            self.row(rows, 'A', 'PUBLISH_ATTEMPT', phase='large', id=2)['cdr_length'] = length
            self.reject(rows)

    def test_burst_cannot_dispatch_receive_between_contiguous_publishes(self):
        rows = model()
        received = self.row(rows, 'A', 'RECEIVE', phase='burst', id=3)
        rows['A'].remove(received)
        rows['A'].insert(rows['A'].index(self.row(rows, 'A', 'RETURN', phase='burst', id=1))+1, received)
        self.reject(rows)

    def test_sequential_exchange_and_phase_barrier_order(self):
        for side, event, before in [('A', 'RECEIVE', 'RETURN'), ('B', 'PUBLISH_ATTEMPT', 'RECEIVE')]:
            rows = model()
            row = self.row(rows, side, event, phase='small', id=1)
            rows[side].remove(row)
            rows[side].insert(rows[side].index(self.row(rows, side, before, phase='small', id=1)), row)
            self.reject(rows)
        rows = model()
        row = self.row(rows, 'A', 'PHASE_CONFIRM', phase='small')
        rows['A'].remove(row)
        rows['A'].append(row)
        self.reject(rows)

    def test_close_observation_and_pending_are_strict(self):
        for event, key, value in [('CLOSED', 'result', '4'), ('OBSERVATION_END', 'finite', '0'),
                                  ('OBSERVATION_END', 'received', '14'), ('BUSINESS_DONE', 'pending', '1')]:
            rows = model()
            self.row(rows, 'A', event)[key] = value
            self.reject(rows)
        rows = model()
        rows['B'].remove(self.row(rows, 'B', 'CLOSED'))
        self.reject(rows)

    def test_business_success_cannot_replace_budget_refund_validation(self):
        analyze_fragment(text(model()), RUN, PIDS, reliability='reliable')
        # Reuse the existing budget model/oracle; do not invent fragment-to-ingress causality.
        valid = budget_text(budget_events()).replace('pid=42', 'pid=123')
        self.assertTrue(validate_receive_capture(valid, RUN, 'A', PIDS['A'], profile='budget_only')['reconciled'])
        for rows in [budget_events()[:4], [r for r in budget_events() if r['kind'] != 'REFUND_RECORD']]:
            with self.assertRaises(ValueError):
                validate_receive_capture(budget_text(rows).replace('pid=42', 'pid=123'), RUN, 'A', PIDS['A'], profile='budget_only')

    def test_run_pid_schema_and_duplicate_fields(self):
        original = text(model())
        for old, new in [('run='+RUN, 'run=ffffffffffffffff'), ('pid=456', 'pid=123'),
                         ('local=1 ', 'local=2 '), ('event=READY', 'event=UNKNOWN'),
                         ('event=READY', 'event=READY event=READY'), ('peers=1', 'peers=1 extra=1')]:
            changed = dict(original)
            changed['B'] = changed['B'].replace(old, new, 1)
            with self.assertRaises(ValueError):
                analyze_fragment(changed, RUN, PIDS, reliability='reliable')


if __name__ == '__main__':
    unittest.main()
