"""Cycle negatives exercise the real text entry, including per-side compensating errors."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/public_socket'))
import analyze_shared as a

def fixture(n=2):
    rows=[];run='0123456789abcdef'
    def add(tag,**kw):rows.append((tag,dict(run=run,**kw)))
    def ident(side,g,life):
        base=10000*side+1000*(g=='Q')+life*10
        return dict(participant=f'{base+1:032x}',writer=f'{base+2:032x}',reader=f'{base+3:032x}',app=str(base+4),receiver=str(base+5))
    add('SHARED_SAMPLING',cycles=n,detailed_cycles=1,detail_scope='per_sample_frames',ledger_scope='all_cycles',lifecycle_scope='all_cycles',observer_scope='stage_local',fake_scope='send_latched')
    for side in range(2):
        for g,life in [('P',1),('Q',1)]+[('P',l) for l in range(2,n+2)]:add('SHARED_ID',side=side,cycle=max(1,life-1),group=g,life=life,name=g,**ident(side,g,life))
        for c in range(1,n+1):
            for phase,g,life,ordinals in [('INITIAL','P',c,range(1,4)),('INITIAL','Q',1,range(9*c-8,9*c-5)),('GAP','Q',1,range(9*c-5,9*c-2)),('REATTACH','P',c+1,range(1,4)),('REATTACH','Q',1,range(9*c-2,9*c+1))]:
                own=ident(side,g,life);peer=ident(1-side,g,life)
                for ordinal in ordinals:
                    sn=ordinal+(3 if g=='P' and phase=='INITIAL' and c>1 else 0)
                    common=dict(side=side,cycle=c,phase=phase,group=g,life=life,ordinal=ordinal,sn=sn,app=own['app'],receiver=own['receiver'])
                    add('SHARED_WRITE',**common,writer=own['writer'],rc=0,serialized=60)
                    add('SHARED_TAKE',**common,writer=peer['writer'],reader=own['reader'],valid=1)
                    if c==1:
                        for stage,frame_side in [('FAKE_SEND',1-side),('FAKE_INGRESS',side),('RX_OWNED',side)]:
                            add('HOST_MODEL',stage=stage,side=frame_side,cycle=c,kind='DATA',writer=peer['writer'],sn=sn,app=own['app'],receiver=own['receiver'],hash=42,length=44)
            for phase,life in [('INITIAL',c),('REATTACH',c+1)]:
                for g,l in [('P',life),('Q',1)]:
                    own=ident(side,g,l);peer=ident(1-side,g,l)
                    add('SHARED_MATCH',side=side,cycle=c,phase=phase,group=g,life=l,writer=own['writer'],reader=own['reader'],app=own['app'],receiver=own['receiver'],remote_writer=peer['writer'],remote_reader=peer['reader'])
            add('SHARED_DELETE',side=side,cycle=c,phase='GAP',group='P',life=c,deleted=1,input_open=0,egress_closed=1,egress_drained=1,tx_records=0,tx_bytes=0,**ident(side,'P',c))
        for g,l in [('P',n+1),('Q',1)]:add('SHARED_DELETE',side=side,cycle=n,phase='FINAL',group=g,life=l,deleted=1,input_open=0,egress_closed=1,egress_drained=1,tx_records=0,tx_bytes=0,**ident(side,g,l))
        for c,stage,v in [(c,s,v) for c in range(1,n+1) for s,v in [('INITIAL',2),('GAP',1),('REATTACHED',2)]]+[(n,'FINAL',0)]:
            add('SHARED_NATIVE',side=side,cycle=c,stage=stage,views=v,active_apps=v,records=0,bytes=0,records_limit=64,bytes_limit=524288,retained_apps=0,view_controls=2,runtime='runtime'+str(side),provider='provider'+str(side),listener_entry=0,outgoing_entry=1,listener=100+100*side,outgoing=101+100*side,accepted=102+100*side,listener_owner=1,outgoing_owner=2,accepted_owner=3,socket_calls=2,listen_calls=1,bind_calls=1,send_calls=100,shutdown_calls=2 if stage=='FINAL' else 0)
        add('SHARED_LINK',side=side,cycle=n,phase='FINAL',sends=100,forwards=100,queued=0,bytes=0,record_peak=1,byte_peak=44,invalid=0,shutdowns=2,notifications=1)
        add('SHARED_RESULT',side=side,cycles=n,p_created=n+1,p_deleted=n+1,q_created=1,q_deleted=1,writes=15*n,takes=15*n,success=1)
    for side in range(2):
        for role in range(3):add('HOST_MODEL',side=side,stage='FAKE_C_SHUTDOWN' if role<2 else 'FAKE_TERMINAL_NOTIFICATION',detail=('original_owned_id=' if role<2 else 'id=')+str(100+100*side+role))
    event=0
    for tag,row in rows:
        if tag=='HOST_MODEL':
            event+=1;row.update(run=str(int(run,16)),event=event);row.setdefault('cycle',0)
    add('TRACE_SUMMARY',count=sum(t=='HOST_MODEL' for t,r in rows),limit=8192,overflow=0,detail_observations=100,omitted_observations=1,stored_detail_rows=90,lost_detail_rows=0,required_lost=0)
    return rows

def render(rows):return '\n'.join(t+' '+' '.join(f'{k}={v}' for k,v in r.items()) for t,r in rows)
class Cycles(unittest.TestCase):
    def reject(self,mutate):
        rows=fixture();mutate(rows)
        with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[render(rows)])
    def test_positive(self):self.assertEqual(a.analyze('HOST_CYCLE',[render(fixture())])['reader_samples'],60)
    def test_missing_cycle(self):self.reject(lambda rs:rs.__setitem__(slice(None),[(t,r) for t,r in rs if str(r.get('cycle'))!='2']))
    def test_duplicate_cycle(self):self.reject(lambda rs:rs.extend(copy.deepcopy([(t,r) for t,r in rs if r.get('cycle')==1])))
    def test_compensating_side(self):self.reject(lambda rs:[r.update(side=0) for t,r in rs if r.get('side')==1 and r.get('cycle')==2])
    def test_creates_not_rebuilds(self):self.reject(lambda rs:[r.update(p_created=2) for t,r in rs if t=='SHARED_RESULT'])
    def test_q_rebuilt(self):self.reject(lambda rs:[r.update(life=2) for t,r in rs if r.get('group')=='Q' and r.get('cycle')==2])
    def test_stale_cycle(self):self.reject(lambda rs:[r.update(cycle=1) for t,r in rs if t=='SHARED_MATCH' and r.get('cycle')==2])
    def test_wrong_phase(self):self.reject(lambda rs:[r.update(phase='GAP') for t,r in rs if t=='SHARED_TAKE' and r.get('phase')=='INITIAL'])
    def test_claim_complete_frames(self):self.reject(lambda rs:[r.update(detail_scope='all_frames') for t,r in rs if t=='SHARED_SAMPLING'])
    def test_missing_final(self):self.reject(lambda rs:rs.__setitem__(slice(None),[(t,r) for t,r in rs if not(t=='SHARED_DELETE' and r['phase']=='FINAL')]))
    def test_reuse_old_sample(self):
        def mutate(rs):
            for t,r in rs:
                if t in ('SHARED_WRITE','SHARED_TAKE') and r.get('group')=='P' and r.get('phase')=='INITIAL' and r.get('cycle')==2:r['sn']=r['ordinal']
        self.reject(mutate)
    def test_missing_host100_is_failure(self):
        with self.assertRaises((ValueError,KeyError)):a.analyze('HOST_CYCLE',[''])
if __name__=='__main__':unittest.main()
