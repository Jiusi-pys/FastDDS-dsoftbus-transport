"""Synthetic contract controls only: these records are not ROS execution evidence."""
import copy
import unittest
from lifecycle_publisher_capture import analyze_publisher

RUN = '0123456789abcdef'
PIDS = {'A': 123, 'B': 456}
GID = '0102030405060708090a0b0c0d0e0f10'
IMPL = 'rmw_fastrtps_cpp'


def model():
    """Hand-authored scenario, independent of analyzer constants and control flow."""
    records = {'A': [], 'B': []}
    ns = '/publisher_' + RUN
    def add(side, event, **fields):
        records[side].append(dict(event=event, **{k:str(v) for k,v in fields.items()}))
    for side in 'AB':
        add(side, 'READY', namespace=ns, reliability='reliable', durability='volatile', history='keep_last',
            depth=10, **(dict(gid=GID, impl=IMPL) if side == 'A' else {}))
        add(side, 'MATCHED', nodes=1, samples=1, transitions=1); add(side, 'START', nodes=1)
    def snapshot(operation, state, label):
        add('B', 'STATE', operation=operation, service=ns+'/managed/get_state', state=state, label=label)
    event_number = 0
    def change(operation, transition, previous, transient, state, label, callback, ordinal, active):
        nonlocal event_number
        add('A', 'CALLBACK', callback=callback, ordinal=ordinal, previous=previous, current=transient,
            activated=active, **{'return':97})
        add('B', 'CHANGE_REQUEST', operation=operation, service=ns+'/managed/change_state', transition=transition)
        for start, goal in ((previous,transient),(transient,state)):
            event_number += 1
            add('B', 'TRANSITION', ordinal=event_number, transition=0, start=start, goal=goal)
        add('B', 'CHANGE_RESPONSE', operation=operation, success=1)
        snapshot(operation, state, label)
    def probe(phase, state, active, attempts):
        add('A', 'PROBE', phase=phase, state=state, activated=active, matched=1, gid=GID, impl=IMPL)
        add('B', 'PROBE_REQUEST', phase=phase, service=ns+'/probe', matched=1)
        for attempt, overload in attempts:
            encoded = f'run={RUN};phase={phase};attempt={attempt};overload={overload}'.encode().hex()
            add('A', 'ATTEMPT', phase=phase, attempt=attempt, overload=overload, activated=active, payload_hex=encoded)
            add('A', 'RETURN', phase=phase, attempt=attempt, overload=overload)
            if active:
                add('B', 'RECEIVE', phase=phase, attempt=attempt, overload=overload, gid=GID, impl=IMPL, payload_hex=encoded)
        add('B', 'PROBE_RESPONSE', phase=phase, state=state, activated=active, first=attempts[0][0],
            last=attempts[1][0], gid=GID, impl=IMPL, success=1)
    snapshot(0,1,'unconfigured')
    change(1,1,1,10,2,'inactive','configure',1,0); probe(1,2,0,[(1,'ref'),(2,'unique')])
    change(2,3,2,13,3,'active','activate',1,1); probe(2,3,1,[(3,'ref'),(4,'unique')])
    change(3,4,3,14,2,'inactive','deactivate',1,0); probe(3,2,0,[(5,'ref'),(6,'unique')])
    change(4,3,2,13,3,'active','activate',2,1); probe(4,3,1,[(7,'ref'),(8,'unique')])
    change(5,4,3,14,2,'inactive','deactivate',2,0)
    change(6,6,2,12,4,'finalized','shutdown',1,0)
    add('A','BUSINESS_DONE',probes=4,attempts=8,callbacks=6,finalized=1,activated=0,gid=GID)
    add('B','BUSINESS_DONE',changes=6,states=7,probes=4,events=12,received=4,pending=0)
    add('B','OBSERVATION_END',received=4,inactive_received=0,finite=1)
    for side in 'AB': add(side,'CLOSED',result=0)
    return records


def text(records):
    return {side:'\n'.join(f'PUBLISHER_ACCEPT run={RUN} side={side} pid={PIDS[side]} local={i} ' +
        ' '.join(k+'='+v for k,v in row.items()) for i,row in enumerate(rows,1)) for side,rows in records.items()}


class PublisherReceiptTests(unittest.TestCase):
    def row(self, records, side, event, **fields):
        return next(r for r in records[side] if r['event']==event and all(r.get(k)==str(v) for k,v in fields.items()))

    def reject(self, records):
        with self.assertRaises(ValueError): analyze_publisher(text(records),RUN,PIDS)

    def test_valid_model(self):
        result = analyze_publisher(text(model()),RUN,PIDS)
        self.assertEqual([result[k] for k in ('changes','states','probes','publish_attempts','received','transition_events','callbacks')],
                         [6,7,4,8,4,12,6])
        self.assertEqual(result['negative_observation'],'FINITE_MATCHED_WINDOW_ONLY')
        self.assertFalse(result['rtps_sequence_numbers_observed'])

    def test_valid_async_response_order(self):
        records = model()
        for operation in range(1,7):
            response = self.row(records,'B','CHANGE_RESPONSE',operation=operation)
            records['B'].remove(response)
            event = self.row(records,'B','TRANSITION',ordinal=operation*2-1)
            records['B'].insert(records['B'].index(event),response)
        for phase in (2,4):
            response = self.row(records,'B','PROBE_RESPONSE',phase=phase)
            records['B'].remove(response)
            sample = self.row(records,'B','RECEIVE',phase=phase)
            records['B'].insert(records['B'].index(sample),response)
        analyze_publisher(text(records),RUN,PIDS)

    def test_inactive_delivery_before_or_after_markers(self):
        for attempt in (1,2,5,6):
            for late in (False,True):
                with self.subTest(attempt=attempt,late=late):
                    records=model(); phase=1 if attempt<3 else 3; overload='ref' if attempt%2 else 'unique'
                    leak=dict(event='RECEIVE',phase=str(phase),attempt=str(attempt),overload=overload,gid=GID,impl=IMPL,
                        payload_hex=f'run={RUN};phase={phase};attempt={attempt};overload={overload}'.encode().hex())
                    boundary=self.row(records,'B','OBSERVATION_END') if late else self.row(records,'B','PROBE_RESPONSE',phase=phase)
                    records['B'].insert(records['B'].index(boundary),leak); self.reject(records)

    def test_semantic_mutants(self):
        cases=[
            ('A','READY',{},'gid','0'*32), ('A','READY',{},'impl','other_rmw'),
            ('A','READY',{},'reliability','best_effort'), ('B','READY',{},'durability','transient_local'),
            ('B','READY',{},'depth','1'), ('B','READY',{},'history','keep_all'),
            ('A','MATCHED',{},'samples','0'), ('B','MATCHED',{},'transitions','0'),
            ('A','CALLBACK',{'callback':'activate'},'activated','0'), ('A','CALLBACK',{},'return','98'),
            ('A','CALLBACK',{'callback':'deactivate'},'previous','2'),
            ('A','PROBE',{'phase':3},'gid','ff'*16), ('A','PROBE',{'phase':1},'matched','0'),
            ('A','ATTEMPT',{'attempt':5},'activated','1'), ('A','ATTEMPT',{'attempt':3},'overload','unique'),
            ('A','RETURN',{'attempt':8},'attempt','7'), ('A','BUSINESS_DONE',{},'gid','ff'*16),
            ('B','CHANGE_REQUEST',{'operation':6},'transition','7'), ('B','CHANGE_RESPONSE',{},'success','0'),
            ('B','STATE',{'operation':3},'state','3'), ('B','STATE',{'operation':6},'label','inactive'),
            ('B','TRANSITION',{'ordinal':1},'transition','1'), ('B','TRANSITION',{'ordinal':12},'goal','2'),
            ('B','PROBE_REQUEST',{'phase':1},'matched','0'), ('B','PROBE_RESPONSE',{'phase':3},'activated','1'),
            ('B','PROBE_RESPONSE',{'phase':4},'gid','ff'*16), ('B','PROBE_RESPONSE',{},'success','0'),
            ('B','RECEIVE',{'attempt':3},'gid','ff'*16), ('B','RECEIVE',{'attempt':4},'gid','0'*32),
            ('B','RECEIVE',{'attempt':7},'impl','other_rmw'), ('B','RECEIVE',{'attempt':8},'phase','3'),
            ('B','RECEIVE',{'attempt':3},'payload_hex','00'), ('B','RECEIVE',{'attempt':4},'overload','ref'),
            ('B','OBSERVATION_END',{},'finite','0'), ('B','OBSERVATION_END',{},'inactive_received','1'),
            ('B','BUSINESS_DONE',{},'pending','1'), ('B','CLOSED',{},'result','4')]
        for side,event,fields,key,value in cases:
            with self.subTest(side=side,event=event,fields=fields,key=key):
                records=model(); self.row(records,side,event,**fields)[key]=value; self.reject(records)

    def test_missing_or_duplicate_evidence(self):
        for side,event in [('A','START'),('A','PROBE'),('A','ATTEMPT'),('A','RETURN'),('A','CALLBACK'),
                           ('B','CHANGE_REQUEST'),('B','CHANGE_RESPONSE'),('B','STATE'),('B','TRANSITION'),
                           ('B','PROBE_REQUEST'),('B','PROBE_RESPONSE'),('B','RECEIVE'),('B','OBSERVATION_END')]:
            for duplicate in (False,True):
                with self.subTest(side=side,event=event,duplicate=duplicate):
                    records=model(); row=self.row(records,side,event)
                    if duplicate: records[side].insert(records[side].index(row),copy.deepcopy(row))
                    else: records[side].remove(row)
                    self.reject(records)

    def test_sample_or_event_outside_interval(self):
        for event,fields,boundary in [('RECEIVE',{'attempt':3},'START'),('RECEIVE',{'attempt':8},'OBSERVATION_END'),
                                      ('TRANSITION',{'ordinal':1},'START'),('PROBE_RESPONSE',{'phase':3},'OBSERVATION_END')]:
            with self.subTest(event=event,fields=fields):
                records=model(); row=self.row(records,'B',event,**fields); records['B'].remove(row)
                records['B'].insert(records['B'].index(self.row(records,'B',boundary)),row); self.reject(records)
        records=model(); a=self.row(records,'B','RECEIVE',attempt=3); b=self.row(records,'B','RECEIVE',attempt=4)
        i,j=records['B'].index(a),records['B'].index(b); records['B'][i],records['B'][j]=b,a; self.reject(records)

    def test_identity_and_schema(self):
        original=text(model())
        for old,new in [('pid=456','pid=123'),('run='+RUN,'run=ffffffffffffffff'),('local=1 ','local=2 '),
                        ('event=READY','event=UNKNOWN'),('nodes=1','nodes=1 extra=1'),('event=READY','event=READY event=READY')]:
            with self.subTest(old=old,new=new):
                changed=dict(original); changed['B']=changed['B'].replace(old,new,1)
                with self.assertRaises(ValueError): analyze_publisher(changed,RUN,PIDS)
        with self.assertRaises(ValueError): analyze_publisher(original,RUN,{'A':123,'B':123})


if __name__ == '__main__':
    unittest.main()
