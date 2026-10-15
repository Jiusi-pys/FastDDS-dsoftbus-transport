"""Synthetic serialized-oracle controls; these do not execute ROS or DSoftBus."""
import copy
import unittest
from serialized_capture import analyze_serialized

RUN='0123456789abcdef'
PIDS={'A':123,'B':456}
GIDS={'A':'0102030405060708090a0b0c0d0e0f10','B':'1112131415161718191a1b1c1d1e1f20'}
IMPL='rmw_fastrtps_cpp'


def model():
    """Hand-authored positive contract; not imported from analyzer helpers."""
    records={'A':[],'B':[]};ns='/serialized_'+RUN
    def add(side,event,**fields):
        records[side].append(dict(event=event,**{k:str(v) for k,v in fields.items()}))
    def content(role,number):
        body='0123456789abcdef0' if number==1 else ''.join('0123456789abcdef' for _ in range(256))
        return 'run='+RUN+';role='+role+';id='+str(number)+';body='+body
    for side,role,incoming,peer in [('A','request','reply','B'),('B','reply','request','A')]:
        add(side,'READY',namespace=ns,pub_topic=ns+'/'+role,sub_topic=ns+'/'+incoming,gid=GIDS[side],impl=IMPL,
            reliability='reliable',durability='volatile',history='keep_last',depth=10)
        add(side,'MATCHED',publishers=1,subscriptions=1);add(side,'START',peers=1)
        for number,overload in [(1,'wrapper'),(2,'raw')]:
            def receive():
                add(side,'RECEIVE',role=incoming,id=number,payload_hex=content(incoming,number).encode().hex(),gid=GIDS[peer],impl=IMPL)
            if side=='B': receive()
            add(side,'SERIALIZE',role=role,id=number,overload=overload,payload_hex=content(role,number).encode().hex(),
                length=len(content(role,number))+9,capacity_before=8192,capacity_after=len(content(role,number))+9)
            add(side,'PUBLISH_ATTEMPT',role=role,id=number,overload=overload)
            add(side,'RETURN',role=role,id=number,overload=overload)
            if side=='A': receive()
        add(side,'BUSINESS_DONE',serialized=2,published=2,received=2,pending=0)
        add(side,'OBSERVATION_END',received=2,finite=1);add(side,'CLOSED',result=0)
    return records


def text(records):
    return {side:'\n'.join(f'SERIALIZED_ACCEPT run={RUN} side={side} pid={PIDS[side]} local={i} '+
        ' '.join(k+'='+v for k,v in row.items()) for i,row in enumerate(rows,1)) for side,rows in records.items()}


class SerializedReceiptTests(unittest.TestCase):
    def row(self,records,side,event,**fields):
        return next(r for r in records[side] if r['event']==event and all(r.get(k)==str(v) for k,v in fields.items()))

    def reject(self,records):
        with self.assertRaises(ValueError): analyze_serialized(text(records),RUN,PIDS)

    def test_valid_model(self):
        result=analyze_serialized(text(model()),RUN,PIDS)
        self.assertEqual([result[k] for k in ('exchanges','serializations','publish_attempts','publish_returns','received')],[2,4,4,4,4])
        self.assertEqual([result['wrapper_publishes'],result['raw_publishes']],[2,2])
        self.assertEqual(result['body_lengths'],[17,4096]);self.assertEqual(result['initial_capacity'],8192)
        self.assertEqual(result['post_capacity_contract'],'EQUALS_SERIALIZED_LENGTH_FOR_RMW_FASTRTPS_CPP_8_4_3')
        self.assertFalse(result['generic_tested']);self.assertFalse(result['loaned_tested'])
        self.assertFalse(result['wire_byte_identity_claimed'])

    def test_finite_semantics_not_exact_cdr_or_cross_process_clock(self):
        records=model()
        # Length is bounded evidence, not an exact endian/padding oracle.
        self.row(records,'A','SERIALIZE',id=1).update(length='200',capacity_after='200')
        # Dictionaries carry per-process records; no shared timestamps/order are fabricated.
        texts=text(records);analyze_serialized({'B':texts['B'],'A':texts['A']},RUN,PIDS)

    def test_semantic_fields(self):
        cases=[
            ('A','READY',{},'pub_topic','/wrong'),('B','READY',{},'sub_topic','/wrong'),
            ('A','READY',{},'gid','0'*32),('B','READY',{},'gid',GIDS['A']),('B','READY',{},'impl','other_rmw'),
            ('A','READY',{},'reliability','best_effort'),('B','READY',{},'durability','transient_local'),
            ('A','READY',{},'history','keep_all'),('B','READY',{},'depth','1'),
            ('A','MATCHED',{},'subscriptions','0'),('B','MATCHED',{},'publishers','2'),('B','START',{},'peers','0'),
            ('A','SERIALIZE',{'id':1},'overload','ordinary'),('B','SERIALIZE',{'id':2},'overload','wrapper'),
            ('A','PUBLISH_ATTEMPT',{'id':2},'overload','wrapper'),('B','RETURN',{'id':1},'overload','raw'),
            ('A','SERIALIZE',{'id':1},'payload_hex','00'),('B','SERIALIZE',{'id':2},'role','request'),
            ('B','PUBLISH_ATTEMPT',{'id':1},'id','2'),('A','RETURN',{'id':2},'id','1'),
            ('A','RECEIVE',{'id':1},'payload_hex','00'),('B','RECEIVE',{'id':2},'role','reply'),
            ('A','RECEIVE',{'id':1},'gid',GIDS['A']),('B','RECEIVE',{'id':2},'gid','ff'*16),
            ('A','RECEIVE',{'id':2},'impl','other_rmw'),('B','OBSERVATION_END',{},'finite','0'),
            ('A','BUSINESS_DONE',{},'pending','1'),('B','CLOSED',{},'result','4')]
        for side,event,fields,key,value in cases:
            with self.subTest(side=side,event=event,fields=fields,key=key):
                records=model();self.row(records,side,event,**fields)[key]=value;self.reject(records)

    def test_serialized_capacity_and_length(self):
        for side in 'AB':
            for identifier in (1,2):
                original=self.row(model(),side,'SERIALIZE',id=identifier)
                length=int(original['length']);payload_bytes=len(bytes.fromhex(original['payload_hex']))
                for key,values in [
                    ('length',['0','-1','nan','1','8193','01',str(payload_bytes)]),
                    ('capacity_before',['0','8191','8193','nan','-1','08192']),
                    ('capacity_after',['0','16384','nan','-1',str(length-1),str(length+1),'8192','0'+str(length),'+'+str(length)])]:
                    for value in values:
                        with self.subTest(key=key,side=side,id=identifier,value=value):
                            records=model();row=self.row(records,side,'SERIALIZE',id=identifier);row[key]=value
                            # Invalid length must fail its own bounds even when capacity agrees.
                            if key=='length':row['capacity_after']=value
                            self.reject(records)

    def test_missing_duplicate_or_late_evidence(self):
        for side in 'AB':
            for event in ('READY','MATCHED','START','SERIALIZE','PUBLISH_ATTEMPT','RETURN','RECEIVE','BUSINESS_DONE','OBSERVATION_END','CLOSED'):
                for duplicate in (False,True):
                    with self.subTest(side=side,event=event,duplicate=duplicate):
                        records=model();row=self.row(records,side,event)
                        if duplicate: records[side].insert(records[side].index(row),copy.deepcopy(row))
                        else: records[side].remove(row)
                        self.reject(records)
        records=model();row=self.row(records,'A','RECEIVE',id=2);records['A'].remove(row)
        records['A'].insert(records['A'].index(self.row(records,'A','OBSERVATION_END')),row);self.reject(records)

    def test_same_count_exchange_and_role_swaps(self):
        for side in 'AB':
            for event in ('SERIALIZE','PUBLISH_ATTEMPT','RETURN','RECEIVE'):
                with self.subTest(side=side,event=event):
                    records=model();a=self.row(records,side,event,id=1);b=self.row(records,side,event,id=2)
                    i,j=records[side].index(a),records[side].index(b);records[side][i],records[side][j]=b,a;self.reject(records)
            records=model();a=self.row(records,side,'RECEIVE',id=1);b=self.row(records,side,'RECEIVE',id=2)
            a['payload_hex'],b['payload_hex']=b['payload_hex'],a['payload_hex'];self.reject(records)
        for event in ('SERIALIZE','RECEIVE'):
            records=model();a=self.row(records,'A',event,id=1);b=self.row(records,'B',event,id=1)
            a['payload_hex'],b['payload_hex']=b['payload_hex'],a['payload_hex'];self.reject(records)
        records=model();a=self.row(records,'A','RECEIVE',id=1);b=self.row(records,'B','RECEIVE',id=1)
        a['gid'],b['gid']=b['gid'],a['gid'];self.reject(records)
        records=model()
        for rows in records.values():
            for row in rows:
                if 'impl' in row: row['impl']='foreign_rmw'
        self.reject(records)

    def test_call_and_reply_local_causality(self):
        for side,event,boundary in [('A','RETURN','PUBLISH_ATTEMPT'),('A','RECEIVE','RETURN'),
                                    ('B','SERIALIZE','RECEIVE'),('B','RETURN','PUBLISH_ATTEMPT')]:
            with self.subTest(side=side,event=event):
                records=model();row=self.row(records,side,event,id=1);records[side].remove(row)
                records[side].insert(records[side].index(self.row(records,side,boundary,id=1)),row);self.reject(records)

    def test_identity_and_schema(self):
        original=text(model())
        for old,new in [('pid=456','pid=123'),('run='+RUN,'run=ffffffffffffffff'),('local=1 ','local=2 '),
                        ('event=READY','event=UNKNOWN'),('peers=1','peers=1 extra=1'),('event=READY','event=READY event=READY')]:
            with self.subTest(old=old,new=new):
                changed=dict(original);changed['B']=changed['B'].replace(old,new,1)
                with self.assertRaises(ValueError): analyze_serialized(changed,RUN,PIDS)
        with self.assertRaises(ValueError): analyze_serialized(original,RUN,{'A':123,'B':123})


if __name__=='__main__': unittest.main()
