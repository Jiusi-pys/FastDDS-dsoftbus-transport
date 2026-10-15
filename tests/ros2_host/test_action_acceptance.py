"""Synthetic receipt-validator tests; these do not execute ROS or DSoftBus."""
import copy
import unittest
from run_action_acceptance import analyze

RUN = '0123456789abcdef'
PIDS = {'A': 123, 'B': 456}
IDS = {str(order): f'{index:032x}' for index, order in enumerate((6, 40, -1, 0, 5), 1)}


def model():
    records = {'A': [], 'B': []}
    def add(side, event, **fields):
        records[side].append(dict(event=event, **{k: str(v) for k, v in fields.items()}))
    def accept(order):
        accepted = int(order != -1)
        add('B', 'SENT', order=order)
        add('A', 'GOAL', order=order, uuid=IDS[str(order)], accepted=accepted)
        if accepted:
            add('A', 'ACCEPTED', order=order, uuid=IDS[str(order)])
        add('B', 'RESPONSE', order=order, uuid=IDS[str(order)] if accepted else '-', accepted=accepted)
    def feedback(order, values):
        for side in 'AB':
            add(side, 'FEEDBACK', order=order, uuid=IDS[str(order)], values=values)
    def terminal(order, code, values):
        add('A', 'TERMINAL', order=order, uuid=IDS[str(order)], code=code, values=values,
            prestate='CANCELING' if order == 40 else 'EXECUTING')
        add('B', 'RESULT', order=order, uuid=IDS[str(order)], code=code, values=values)
    action = '/action_' + RUN + '/fibonacci'
    add('A', 'SERVER_READY', action=action)
    add('B', 'CLIENT_READY', action=action)
    accept(6)
    accept(40)
    add('A', 'CONCURRENT', normal=IDS['6'], target=IDS['40'], normal_active=1, target_active=1)
    feedback(6, '0,1')
    feedback(40, '0,1')
    add('B', 'CANCEL_REQUEST', uuid=IDS['40'], normal=IDS['6'], feedback_seen=1)
    add('A', 'CANCEL', order=40, uuid=IDS['40'], normal=IDS['6'], normal_active=1, accepted=1)
    add('B', 'CANCEL_RESPONSE', uuid=IDS['40'], return_code=0, count=1)
    terminal(40, 5, '0,1')
    for value in ('0,1,1', '0,1,1,2', '0,1,1,2,3', '0,1,1,2,3,5'):
        feedback(6, value)
    terminal(6, 4, '0,1,1,2,3,5')
    accept(-1)
    accept(0)
    terminal(0, 6, '-')
    accept(5)
    for value in ('0,1', '0,1,1', '0,1,1,2', '0,1,1,2,3'):
        feedback(5, value)
    terminal(5, 4, '0,1,1,2,3')
    for side in 'AB':
        add(side, 'BUSINESS_DONE', requested=5, accepted=4, rejected=1, terminals=4, pending=0)
        add(side, 'CLOSED', result=0)
    return records


def text(records):
    return {side: '\n'.join(f'ACTION_ACCEPT run={RUN} side={side} pid={PIDS[side]} local={i} ' +
                            ' '.join(k + '=' + v for k, v in row.items())
                            for i, row in enumerate(rows, 1)) for side, rows in records.items()}


class ActionReceiptTests(unittest.TestCase):
    def row(self, records, side, event, **fields):
        return next(row for row in records[side] if row['event'] == event and
                    all(row.get(k) == str(v) for k, v in fields.items()))

    def bad(self, mutate):
        records = model()
        mutate(records)
        with self.assertRaises(ValueError):
            analyze(text(records), RUN, PIDS)

    def test_valid_model(self):
        result = analyze(text(model()), RUN, PIDS)
        self.assertEqual((result['requests'], result['accepted'], result['canceled']), (5, 4, 1))
        self.assertFalse(result['status_topic_validated'])

    def test_feedback_is_subsequence_not_full_delivery(self):
        records = model()
        records['B'] = [r for r in records['B'] if r['event'] != 'FEEDBACK' or r['order'] == '40']
        analyze(text(records), RUN, PIDS)

    def test_normal_feedback_before_own_send_rejected(self):
        def mutate(records):
            feedback = [r for r in records['B'] if r['event'] == 'FEEDBACK' and r['order'] == '6']
            records['B'] = [r for r in records['B'] if r not in feedback]
            position = records['B'].index(self.row(records, 'B', 'SENT', order=6))
            records['B'][position:position] = feedback
        self.bad(mutate)

    def test_recovery_feedback_before_own_send_rejected(self):
        def mutate(records):
            feedback = [r for r in records['B'] if r['event'] == 'FEEDBACK' and r['order'] == '5']
            records['B'] = [r for r in records['B'] if r not in feedback]
            position = records['B'].index(self.row(records, 'B', 'SENT', order=5))
            records['B'][position:position] = feedback
        self.bad(mutate)

    def test_feedback_after_send_before_response_is_valid(self):
        records = model()
        for order in (6, 5):
            feedback = self.row(records, 'B', 'FEEDBACK', order=order)
            records['B'].remove(feedback)
            position = records['B'].index(self.row(records, 'B', 'RESPONSE', order=order))
            records['B'].insert(position, feedback)
        analyze(text(records), RUN, PIDS)

    def test_incomplete_normal_feedback_subsequence_is_valid(self):
        records = model()
        records['B'] = [r for r in records['B'] if r['event'] != 'FEEDBACK' or r['order'] == '40' or
                        (r['order'] == '6' and len(r['values'].split(',')) in (2, 4, 6)) or
                        (r['order'] == '5' and len(r['values'].split(',')) == 3)]
        analyze(text(records), RUN, PIDS)

    def test_wrong_result_uuid(self):
        self.bad(lambda r: self.row(r, 'B', 'RESULT', order=6).update(uuid=IDS['40']))

    def test_wrong_cancel_goal(self):
        self.bad(lambda r: self.row(r, 'B', 'CANCEL_RESPONSE').update(uuid=IDS['6']))

    def test_missing_terminal(self):
        self.bad(lambda r: r['B'].remove(self.row(r, 'B', 'RESULT', order=5)))

    def test_duplicate_terminal(self):
        self.bad(lambda r: r['B'].insert(-2, copy.copy(self.row(r, 'B', 'RESULT', order=5))))

    def test_wrong_terminal_code(self):
        self.bad(lambda r: self.row(r, 'B', 'RESULT', order=40).update(code='4'))

    def test_invalid_feedback(self):
        self.bad(lambda r: self.row(r, 'B', 'FEEDBACK', order=6).update(values='0,2'))

    def test_accepted_rejection(self):
        self.bad(lambda r: self.row(r, 'B', 'RESPONSE', order=-1).update(accepted='1', uuid=IDS['-1']))

    def test_missing_causal_feedback(self):
        self.bad(lambda r: r['B'].remove(self.row(r, 'B', 'FEEDBACK', order=40)))

    def test_cancel_precedes_feedback(self):
        def mutate(r):
            row = self.row(r, 'B', 'CANCEL_REQUEST')
            r['B'].remove(row)
            r['B'].insert(r['B'].index(self.row(r, 'B', 'FEEDBACK', order=40)), row)
        self.bad(mutate)

    def test_missing_concurrency_receipt(self):
        self.bad(lambda r: r['A'].remove(self.row(r, 'A', 'CONCURRENT')))

    def test_normal_inactive_during_cancel(self):
        self.bad(lambda r: self.row(r, 'A', 'CANCEL').update(normal_active='0'))

    def test_surplus_wrong_cancel_response(self):
        self.bad(lambda r: r['B'].insert(-2, dict(event='CANCEL_RESPONSE', uuid=IDS['6'], return_code='0', count='1')))

    def test_pending_work_at_shutdown(self):
        self.bad(lambda r: self.row(r, 'A', 'BUSINESS_DONE').update(pending='1'))

    def test_zero_uuid(self):
        self.bad(lambda r: self.row(r, 'A', 'GOAL', order=6).update(uuid='0' * 32))

    def test_wrong_process_identity(self):
        transcripts = text(model())
        transcripts['B'] = transcripts['B'].replace('pid=456', 'pid=999', 1)
        with self.assertRaises(ValueError):
            analyze(transcripts, RUN, PIDS)

    def test_duplicate_field(self):
        transcripts = text(model())
        transcripts['B'] = transcripts['B'].replace('event=CLIENT_READY', 'event=CLIENT_READY event=CLIENT_READY', 1)
        with self.assertRaises(ValueError):
            analyze(transcripts, RUN, PIDS)


if __name__ == '__main__':
    unittest.main()
