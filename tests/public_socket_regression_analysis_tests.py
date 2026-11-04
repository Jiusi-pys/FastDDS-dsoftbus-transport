import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'scripts/public_socket/analyze_regression.py'
spec = importlib.util.spec_from_file_location('regression', path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class Contracts(unittest.TestCase):
    def test_fragment_hole_despite_equal_total(self):
        with self.assertRaises(ValueError):
            m.coverage([{'first':1,'count':2,'fragment_size':8,'sample_size':32},
                        {'first':2,'count':2,'fragment_size':8,'sample_size':32}],32)
    def test_fragment_union_duplicate_not_extra(self):
        f={'first':1,'count':4,'fragment_size':8,'sample_size':32}
        self.assertEqual(m.coverage([f,f],32),{1,2,3,4})
    def test_fragment_size_change(self):
        with self.assertRaises(ValueError):
            m.coverage([{'first':1,'count':4,'fragment_size':8,'sample_size':33}],32)
    def test_fragment_outside_sample(self):
        with self.assertRaises(ValueError):
            m.coverage([{'first':1,'count':5,'fragment_size':8,'sample_size':32}],32)
    def test_drop_not_exercised(self):
        with self.assertRaises(ValueError):m.loss_proof([],[],[],'w',1)
    def test_missing_nack_not_recovery(self):
        with self.assertRaises(ValueError):m.loss_proof([{'writer':'w','sn':1,'first':3,'count':1,'packet':5}],[],[],'w',1)
    def test_nack_must_cover_target(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'00000000,0,0,0,0,0,0,0','writer_known':1,'packet':6}
        with self.assertRaises(ValueError):m.loss_proof([d],[n],[],'w',1)
    def test_exact_repair(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'80000000,0,0,0,0,0,0,0','writer_known':1,'packet':6}
        r={**d,'packet':8}
        self.assertEqual(m.loss_proof([d],[n],[r],'w',1),{3})
    def test_old_repair_not_future_recovery(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'80000000,0,0,0,0,0,0,0','writer_known':1,'packet':6}
        with self.assertRaises(ValueError):m.loss_proof([d],[n],[{**d,'packet':4}],'w',1)
    def test_be_window_required(self):
        with self.assertRaises(ValueError):m.absence([])
        with self.assertRaises(ValueError):m.absence([{'completed':0,'window_ms':3000,'ordinal':1,'started_after_drop':0,'after_drop_us':1}])
        m.absence([{'completed':1,'window_ms':3000,'ordinal':1,'started_after_drop':1,'after_drop_us':3000000}])
    def test_expected_key_sets_separate(self):
        self.assertEqual(m.expected('KEYED_LAST'),{(k,n) for k in (1,2) for n in (3,4,5)})
    def test_unknown_never_harmless(self):
        with self.assertRaises(ValueError):m.expected('UNKNOWN')
    def rows(self):
        w={'key':1,'ordinal':1,'writer':'writer','run':'run','rc':0,'sn':1,'hash':'full'}
        return w,{**w,'reader':'reader','valid':1}
    def test_reader_required_not_sdk_success(self):
        w,t=self.rows()
        with self.assertRaises(ValueError):m.ledger([w],[],{(1,1)},'writer','reader','run')
    def test_wrong_writer(self):
        w,t=self.rows();t['writer']='other'
        with self.assertRaises(ValueError):m.ledger([w],[t],{(1,1)},'writer','reader','run')
    def test_wrong_sequence(self):
        w,t=self.rows();t['sn']=2
        with self.assertRaises(ValueError):m.ledger([w],[t],{(1,1)},'writer','reader','run')
    def test_application_rewrite_not_history(self):
        w,t=self.rows()
        with self.assertRaises(ValueError):m.ledger([w,{**w,'sn':2}],[t],{(1,1)},'writer','reader','run')
    def test_key_merge_rejected(self):
        w,t=self.rows()
        with self.assertRaises(ValueError):m.ledger([w],[t],{(1,1),(2,1)},'writer','reader','run')
    def test_extra_reader_duplicate(self):
        w,t=self.rows()
        with self.assertRaises(ValueError):m.ledger([w],[t,t],{(1,1)},'writer','reader','run')
    def test_payload_mismatch(self):
        w,t=self.rows();t['hash']='corrupt'
        with self.assertRaises(ValueError):m.ledger([w],[t],{(1,1)},'writer','reader','run')
    def test_valid_reader(self):
        w,t=self.rows();m.ledger([w],[t],{(1,1)},'writer','reader','run')
    def test_pre_drop_nack(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'80000000,0,0,0,0,0,0,0','writer_known':1,'packet':1}
        with self.assertRaises(ValueError):m.loss_proof([d],[n],[{**d,'packet':8}],'w',1)
    def test_pre_request_repair(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'80000000,0,0,0,0,0,0,0','writer_known':1,'packet':9}
        with self.assertRaises(ValueError):m.loss_proof([d],[n],[{**d,'packet':8}],'w',1)
    def test_copacked_control_one_drop(self):
        d={'writer':'w','sn':1,'first':3,'count':1,'packet':5,'kind':'DATA_FRAG'}
        n={'writer':'w','sn':1,'first':3,'bits':1,'bitmap':'80000000,0,0,0,0,0,0,0','writer_known':1,'packet':6}
        self.assertEqual(m.loss_proof([d,{**d,'kind':'HEARTBEAT'}],[n],[{**d,'packet':8}],'w',1),{3})
        with self.assertRaises(ValueError):m.loss_proof([d,{**d,'packet':7}],[n],[{**d,'packet':8}],'w',1)
    def test_short_post_drop_window(self):
        with self.assertRaises(ValueError):m.absence([{'completed':1,'window_ms':3000,'ordinal':1,'started_after_drop':1,'after_drop_us':2999999}])
    def test_huge_geometry_bounded(self):
        with self.assertRaises(ValueError):m.coverage([{'first':1,'count':1,'fragment_size':1,'sample_size':2**63}],2**63)


def complete_vol_logs():
    """Synthetic analyzer fixture, never used as device evidence."""
    run='0123456789abcdef';w='a'*24+'00000102';r='b'*24+'00000207';wa='a'*24+'00000207';wb='b'*24+'00000102'
    def line(tag,**fields):return tag+' '+' '.join(f'{k}={v}' for k,v in fields.items())
    logs=[[],[]]
    for side,(own_w,own_r,peer_w,peer_r) in enumerate([(w,wa,wb,r),(wb,r,w,wa)]):
        x=logs[side]
        x += [line('DDS_IDENTITIES',writer=own_w,reader=own_r),line('DDS_MATCH',writer=own_w,reader=own_r,remote_writer=peer_w,remote_reader=peer_r),line('DDS_NATIVE_READY',outgoing=2,accepted=3,app=1,receiver=3,owner=7),line('REG_CONFIG',case='VOL_LATE',payload=32,max_record=8192,max_samples=64,max_instances=4,max_per_instance=16,reliability='RELIABLE',durability='VOLATILE',writer_depth=16,reader_history='KEEP_ALL'),line('DDS_FACTORY',implementation='DSoftBusTransport',variant='isolated_experiment',mode=2)]
    for i in range(1,7):
        logs[0].append(line('REG_WRITE',key=1,ordinal=i,writer=w,run=run,rc=0,sn=i,hash=i,payload=32,serialized=60))
        if i==3:logs[0]+=[line('DDS_STAGE',stage='PREWRITE_DONE'),line('DDS_STAGE',stage='HISTORY_DONE')]
    logs[1]+=[line('DDS_STAGE',stage='PREWRITE_DONE'),line('REG_READER_CREATED',after_prewrite=1)]
    for i in (4,5,6):logs[1].append(line('REG_TAKE',key=1,ordinal=i,writer=w,reader=r,run=run,valid=1,sn=i,hash=i))
    logs[0].append(line('REG_WAIT',rc=0));frames=[[],[]]
    for i in (4,5,6):
        base=dict(writer=w,sn=i,kind='DATA',hash=100+i,length=128,offset=32,owner=4,admitted=1,entered=1,correlation=0,app=0,receiver=0,ingress=0)
        frames[0].append(dict(base,stage='SDK_SEND',socket=2))
        occurrence=len(frames[1])+1
        frames[1].append(dict(base,stage='SDK_INGRESS',socket=3,ingress=occurrence))
        for st in ('RX_CAPTURED','RX_OWNED','RX_RUNNING'):frames[1].append(dict(base,stage=st,socket=3,owner=7,app=1,receiver=3,correlation=i,ingress=occurrence if st!="RX_RUNNING" else 0))
    for side,x in enumerate(logs):
        for j,f in enumerate(frames[side],1):x.append(line('DDS_FRAME',**f,event=j,packet=j))
        x.append(line('DDS_FRAME_SUMMARY',count=len(frames[side]),overflow=0))
        apis=[('Socket_enter',-1,0),('Socket_return',2,2),('Listen_enter',1,0),('Listen_return',1,0),('Bind_enter',2,0),('Bind_return',2,0),('OnBind',3,0),('OnNegotiate',3,0)]
        for _ in range(3):apis += [('SendBytes_enter',2,0),('SendBytes_return',2,0),('OnBytes',3,0)]
        apis += [('Shutdown_enter',1,0),('Shutdown_void_return',1,0)]
        for j,(name,sock,val) in enumerate(apis,1):x.append(line('PROBE_API',event=j,name=name,socket=sock,value=val,length=128))
        x += [line('PROBE_TRACE',count=len(apis),overflow=0),line('DDS_RESULT',success=1,drained=1)]
    return ['\n'.join(x) for x in logs]

class CombinedEvidence(unittest.TestCase):
    def test_complete_positive(self):self.assertTrue(m.analyze('VOL_LATE',complete_vol_logs())['pass'])
    def test_no_native_or_incomplete_api(self):
        logs=complete_vol_logs();logs=[ '\n'.join(x for x in s.splitlines() if not x.startswith(('DDS_FRAME ','PROBE_API '))) for s in logs]
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_zero_frames_even_consistent_summary(self):
        logs=complete_vol_logs();logs=['\n'.join('DDS_FRAME_SUMMARY count=0 overflow=0' if x.startswith('DDS_FRAME_SUMMARY') else x for x in s.splitlines() if not x.startswith('DDS_FRAME ')) for s in logs]
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_sdk_error_not_masked(self):
        logs=complete_vol_logs();logs=[x.replace('name=SendBytes_return socket=2 value=0','name=SendBytes_return socket=2 value=-5') for x in logs]
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_wrong_lease(self):
        logs=complete_vol_logs();logs[1]=logs[1].replace('receiver=3 correlation=', 'receiver=4 correlation=')
        # Dict field order is explicit, replace the original receiver field in frame rows only.
        logs[1]='\n'.join(x.replace('receiver=3','receiver=4') if x.startswith('DDS_FRAME ') else x for x in logs[1].splitlines())
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_fake_rejected(self):
        logs=complete_vol_logs();logs[0]+='\nFAKE_C_SEND\n'
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_wrong_payload_size(self):
        logs=[s.replace('payload=32','payload=31') for s in complete_vol_logs()]
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)
    def test_no_reader(self):
        logs=complete_vol_logs();logs[1]='\n'.join(x for x in logs[1].splitlines() if not x.startswith('REG_TAKE'))
        with self.assertRaises(ValueError):m.analyze('VOL_LATE',logs)


class FragmentAndDropEvidence(unittest.TestCase):
    def rows(self):
        source=[];ingress=[];captured=[];owned=[];running=[]
        for n in (1,2):
            f=dict(writer='w',sn='1',kind='DATA_FRAG',hash=str(n),length='64',offset=32,first=n,count=1,fragment_size=8,sample_size=16,owner=4,admitted=1,entered=1,app=1,receiver=3,correlation=n,ingress=n)
            source.append(dict(f));ingress.append(dict(f));captured.append(dict(f));owned.append(dict(f));running.append(dict(f))
        return source,ingress,captured,owned,running
    def test_full_fragment_positive(self):self.assertEqual(m.full_fragment_chain(*self.rows(),16),{1,2})
    def test_second_fragment_each_layer_wrong_hash(self):
        for layer in range(5):
            rows=self.rows();rows[layer][1]['hash']='foreign'
            with self.assertRaises(ValueError):m.full_fragment_chain(*rows,16)
    def test_geometry_cannot_borrow_another_record(self):
        rows=self.rows();rows[3][1]['first']=1
        with self.assertRaises(ValueError):m.full_fragment_chain(*rows,16)
    def test_same_hash_repair_different_occurrence_allowed(self):
        d=dict(packet='2',ingress='1',hash='h',length='64',socket='3')
        f=dict(d,stage='SDK_INGRESS');r=dict(d,stage='RX_CAPTURED',ingress='9')
        m.dropped_occurrence([d],[f,r])
    def test_actual_dropped_occurrence_cannot_enter_binding(self):
        d=dict(packet='2',ingress='1',hash='h',length='64',socket='3');f=dict(d,stage='SDK_INGRESS');r=dict(d,stage='RX_CAPTURED')
        with self.assertRaises(ValueError):m.dropped_occurrence([d],[f,r])
    def test_drop_missing_original_callback(self):
        d=dict(packet='2',ingress='1',hash='h',length='64',socket='3')
        with self.assertRaises(ValueError):m.dropped_occurrence([d],[])

if __name__=='__main__':unittest.main()
