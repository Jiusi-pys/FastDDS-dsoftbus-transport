"""Synthetic target-format cycle contracts; real device stdout is a separate gate."""
import copy
import unittest
from public_socket_cycle_analysis_tests import fixture,render,a

def target(n=2):
    base=fixture(n);boards=[];run='0123456789abcdef'
    for side in range(2):
        rows=[];frames=[];apis=[];waits=set()
        def add(tag,**kw):rows.append((tag,dict(run=run,side=str(side),**kw)))
        add('DDS_MAIN',pid=str(side+10));rows[-1][1]['side']='AB'[side]
        phase_names=['SHARED_INITIAL_READY','SHARED_INITIAL_MATCH','SHARED_INITIAL_DONE','P_OLD_DELETED','Q_GAP_DONE','P_NEW_READY','P_NEW_MATCH','SHARED_DATA_DONE']
        key=lambda c,p:f'C{c}_{p}' if n>1 else p
        for stage in ['LISTENER_READY','NATIVE_READY']+[key(c,p) for c in range(1,n+1) for p in phase_names]+[key(n,p) for p in ('DELETE','DDS_DELETED')]:
            add('DDS_STAGE',pid=str(side+10),stage=stage);rows[-1][1]['side']='AB'[side]
        for tag,source in base:
            if tag=='SHARED_SAMPLING':
                r=dict(source,side=str(side),sdk_scope='stage_local');r.pop('fake_scope');rows.append((tag,r));continue
            if str(source.get('side'))!=str(side):continue
            r=copy.deepcopy(source)
            if tag=='SHARED_LINK':continue
            if tag=='HOST_MODEL':
                if r['stage']=='FAKE_C_SHUTDOWN':apis.append(dict(name='Shutdown_enter',socket=r['detail'].split('=')[1]));continue
                if r['stage']=='FAKE_TERMINAL_NOTIFICATION':apis.append(dict(name='OnShutdown',socket=r['detail'].split('=')[1]));continue
                r['stage']={'FAKE_SEND':'SDK_SEND','FAKE_INGRESS':'SDK_INGRESS'}.get(r['stage'],r['stage']);r.update(socket=str(101+100*side if r['stage']=='SDK_SEND' else 102+100*side),owner='2' if r['stage']=='SDK_SEND' else '3',admitted='1',entered='1',packet='1',ingress='1',correlation='1');frames.append(r)
                if r['stage']=='RX_OWNED':frames.append(dict(r,stage='RX_RUNNING'))
                continue
            if tag=='SHARED_ID':
                r['token']=str(int(r['app'])+1)
                add('SHARED_CONFIG',cycle=r['cycle'],group=r['group'],life=r['life'],reliability='RELIABLE',durability='VOLATILE',history=16,samples=32,instances=1,per_instance=32,builtin=0,intraprocess=0,sharing=0,max_record=8192,local_records=32,detail=16384,payload=32,meta_user_same=1)
            if tag=='SHARED_DELETE':r.update(entities_rc=0,participant_rc=0)
            if tag=='SHARED_NATIVE':r.update(listener_record=21,accepted_record=21)
            if tag=='SHARED_WRITE':waits.add((r['cycle'],r['phase'],r['group'],r['life']))
            rows.append((tag,r))
        for i,r in enumerate(frames,1):r['event']=i;rows.append(('DDS_FRAME',r))
        for i,r in enumerate(apis,1):r['event']=i;rows.append(('PROBE_API',r))
        for tag,r in rows:
            if tag=='SHARED_DELETE':r['frames']=max((f['event'] for f in frames if f['stage']=='RX_RUNNING' and f['app']==r['app'] and f['receiver']==r['receiver']),default=len(frames))
        for c,p,g,l in waits:add('SHARED_WAIT',cycle=c,phase=p,group=g,life=l,rc=0)
        add('SHARED_SETTLE',cycle=n,drained=1,fallback=0)
        rows.extend([('DDS_FRAME_SUMMARY',dict(count=len(frames),limit=16384,overflow=0,observations=2,omitted=1)),('PROBE_TRACE',dict(count=len(apis),capacity=16384,total=len(apis),overflow=0,omitted=0)),('RX_DECISION_SUMMARY',dict(count=0,total=0,limit=16384,overflow=0,omitted=0,unknown=0))])
        add('DDS_EXIT',**{'pass':1,'restored':1,'trace_overflow':0,'frame_overflow':0})
        boards.append(rows)
    return boards
class TargetCycles(unittest.TestCase):
    def test_positive(self):self.assertEqual(a.analyze('SHARED_W1',[render(b) for b in target()],2)['reader_samples'],60)
    def reject(self,change):
        b=target();change(b)
        with self.assertRaises((ValueError,KeyError)):a.analyze('SHARED_W1',[render(x) for x in b],2)
    def test_wrong_stage(self):self.reject(lambda b:[r.update(stage='C1_Q_GAP_DONE') for tag,r in b[0] if tag=='DDS_STAGE' and r['stage']=='C2_Q_GAP_DONE'])
    def test_missing_side_cycle(self):self.reject(lambda b:b[1].__setitem__(slice(None),[(t,r) for t,r in b[1] if not(t=='SHARED_TAKE' and r['cycle']==2)]))
    def test_bad_sdk_trace_accounting(self):self.reject(lambda b:[r.update(omitted=2) for t,r in b[0] if t=='PROBE_TRACE'])
    def test_pid_changed(self):self.reject(lambda b:[r.update(pid='999') for t,r in b[0] if t=='DDS_STAGE'])
    def test_no_final(self):self.reject(lambda b:b[0].__setitem__(slice(None),[(t,r) for t,r in b[0] if t!='SHARED_SETTLE']))
    def test_delete_boundary_zero(self):self.reject(lambda b:[r.update(frames=0) for t,r in b[0] if t=='SHARED_DELETE'])
    def test_delete_boundary_missing(self):self.reject(lambda b:[r.pop('frames') for t,r in b[0] if t=='SHARED_DELETE'])
    def test_delete_boundary_negative(self):self.reject(lambda b:[r.update(frames=-1) for t,r in b[0] if t=='SHARED_DELETE'])
    def test_delete_boundary_beyond_end(self):self.reject(lambda b:[r.update(frames=999999) for t,r in b[0] if t=='SHARED_DELETE'])
    def test_native_record_changed(self):self.reject(lambda b:[r.update(listener_record=999) for t,r in b[0] if t=='SHARED_NATIVE' and r['cycle']==2 and r['stage']!='FINAL'])
    def test_accepted_record_changed(self):self.reject(lambda b:[r.update(accepted_record=999) for t,r in b[0] if t=='SHARED_NATIVE' and r['cycle']==2 and r['stage']!='FINAL'])
    def test_native_record_missing(self):self.reject(lambda b:[r.pop('listener_record') for t,r in b[0] if t=='SHARED_NATIVE'])
    def test_api_count_unclosed(self):self.reject(lambda b:[r.update(count=999999,total=999999) for t,r in b[0] if t=='PROBE_TRACE'])
    def test_api_ordinal_gap(self):self.reject(lambda b:[r.update(event=99) for t,r in b[0] if t=='PROBE_API' and r['event']==1])
    def test_api_negative(self):self.reject(lambda b:[r.update(count=-1,total=-1) for t,r in b[0] if t=='PROBE_TRACE'])
    def test_frame_omission_bounds(self):self.reject(lambda b:[r.update(observations=-1,omitted=999999) for t,r in b[0] if t=='DDS_FRAME_SUMMARY'])
    def test_duplicate_shutdown_identity(self):self.reject(lambda b:[r.update(socket='100') for t,r in b[0] if t=='PROBE_API' and r['name']=='Shutdown_enter'])
    def test_duplicate_api_field(self):
        texts=[render(x) for x in target()];texts[0]=texts[0].replace('PROBE_API ','PROBE_API event=999 ',1)
        with self.assertRaisesRegex(ValueError,'duplicate'):a.analyze('SHARED_W1',texts,2)
    def test_valid_notification_close_and_unsampled_tail(self):
        b=target()
        self.assertTrue(any(t=='PROBE_API' and r['name']=='OnShutdown' for t,r in b[0]))
        self.assertTrue(a.analyze('SHARED_W1',[render(x) for x in b],2)['pass_'])
    def test_missing_terminal_identity(self):
        b=target();b[0]=[(t,r) for t,r in b[0] if not(t=='PROBE_API' and r['name']=='OnShutdown')]
        for t,r in b[0]:
            if t=='PROBE_TRACE':r['count']-=1;r['total']-=1
        with self.assertRaisesRegex(ValueError,'terminal identity coverage'):a.analyze('SHARED_W1',[render(x) for x in b],2)
    def test_old_running_injected_after_delete(self):
        b=target();old=next(r for t,r in b[0] if t=='SHARED_ID' and r['group']=='P' and r['life']==1)
        row=copy.deepcopy(next(r for t,r in b[0] if t=='DDS_FRAME' and r['stage']=='RX_RUNNING' and r['app']==old['app']))
        summary=next(r for t,r in b[0] if t=='DDS_FRAME_SUMMARY');summary['count']+=1;row['event']=summary['count'];b[0].append(('DDS_FRAME',row))
        with self.assertRaisesRegex(ValueError,'old receiver after delete'):a.analyze('SHARED_W1',[render(x) for x in b],2)
if __name__=='__main__':unittest.main()
