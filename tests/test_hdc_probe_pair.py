"""Fixed A-pair policy against generated runtime; no live APIs or run data."""
import contextlib
import copy
import ctypes
import importlib.util
import json
from pathlib import Path
import sys
import time
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('pair_builder_tests',ROOT/'scripts/hdc_goal/prepare_probe_pair.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
NAMES=('goal_contract','goal_driver','goal_windows_backend','goal_notice','goal_handle_lifecycle',
       'goal_authorization','goal_evidence','probe_pair','goal_workflow','goal_entry','goal_storage')

@contextlib.contextmanager
def runtime():
    source=json.loads(builder.read_safe(builder.ACTIVE/'SOURCE_BINDING_D01.json'))
    rows={Path(x['path']).name:x for x in source['sources']}
    expected,_=builder.source_buffers();saved={n:sys.modules.get(n) for n in NAMES};path=list(sys.path)
    try:
        with patch.object(ctypes,'WinDLL',side_effect=AssertionError('LIVE_DLL_FORBIDDEN'),create=True):
            for name in NAMES:
                file=name+'.py';data=builder.read_safe(builder.ACTIVE/file)
                if data!=expected[file] or builder.sha(data)!=rows[file]['sha256']:raise AssertionError('CANDIDATE_SOURCE_DRIFT')
                m=types.ModuleType(name);m.__file__=str(builder.ACTIVE/file);sys.modules[name]=m
                exec(compile(data,m.__file__,'exec'),m.__dict__)
            yield types.SimpleNamespace(**{n:sys.modules[n] for n in NAMES})
    finally:
        sys.path[:]=path
        for n,v in saved.items():
            if v is None:sys.modules.pop(n,None)
            else:sys.modules[n]=v

class Clock:
    def __init__(self):self.value=time.monotonic()
    def __call__(self):return self.value

class Root:
    def __init__(self,case,clock,grant,check_native,stdout=b'',stderr=b''):
        self.case=case;self.clock=clock;self.grant=grant;self.check_native=check_native
        number={'server':11,'probe.A1':21,'probe.A2':22}[case]
        self.pi=types.SimpleNamespace(hProcess=number);self.owner={'pid':number,'birth':100+number,'image':'SYNTHETIC.exe'}
        self.dest=Path(case);self.closed=False;self.goal_started=clock();self.terminal_at=clock()
        self.output=[bytearray(),bytearray()];self.pending=[stdout,stderr];self.sent=False
        self.close_calls=0;self.stop_calls=0;self.create_calls=0;self.delay_create=0;self.bad_close=False
        self.delay_after_create=0
        self.native_rc=0;self.auto_eof=True;self.bad_proof=False
        self.reads=[{'name':s,'eof':False} for s in ('stdout','stderr')]
        self.receipt={'case':case,'created':False,'bound':True,'resumed':False,'terminal':False,
            'terminal_binding_verified':False,'EOF':False,'native_exit':None,'signal_authority':True,
            'stop_attempts':0,'cleanup_errors':[],'hProcess':number,'owner':self.owner,'FileID':[1,number,3]}
    def create(self):
        self.clock.value+=self.delay_create;self.check_native(self.grant)
        self.create_calls+=1;self.receipt['created']=True;self.clock.value+=self.delay_after_create
    def resume(self):self.check_native(self.grant);self.receipt['resumed']=True
    def state(self):return 'TERMINAL' if self.receipt['terminal'] else 'LIVE'
    def identity(self,require_live=False):
        if require_live and self.state()!='LIVE':raise AssertionError('NOT_LIVE')
        if self.state()=='TERMINAL':
            self.receipt['terminal_binding_verified']=not self.bad_proof
            self.receipt['terminal_proof']={'hProcess':self.pi.hProcess,'pid':self.owner['pid'],
              'birth':self.owner['birth'],'image':self.owner['image'],'FileID':self.receipt['FileID'],
              'method':'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN','required_apis_ok':True,
              'image_pin_retained_at_check':True,'full_image_was_verified_while_live':True}
    def read(self,cap,observer):
        if self.case=='server' or self.sent:return []
        self.sent=True;chunks=[]
        for index,stream in enumerate(('stdout','stderr')):
            data=self.pending[index]
            if data:
                self.output[index].extend(data);observer(stream,data,1,2);chunks.append((stream,data))
        self.receipt.update(terminal=True,EOF=self.auto_eof,native_exit=self.native_rc,signal_authority=False)
        self.terminal_at=self.clock()
        for r in self.reads:r['eof']=self.auto_eof
        self.identity();return chunks
    def stop(self,deadline):
        if self.stop_calls:raise AssertionError('STOP_REPLAY')
        self.stop_calls+=1;self.receipt.update(terminal=True,native_exit=125,signal_authority=False,stop_attempts=1)
        self.identity()
    def drain(self,deadline,cap,observer):
        self.receipt['EOF']=True
        for r in self.reads:r['eof']=True
        self.receipt['drain']={'status':'EOF','bytes':0,'EOF':True}
        self.identity();return self.receipt['drain']
    def close(self):
        if self.closed:raise AssertionError('CLOSE_REPLAY')
        self.closed=True;self.close_calls+=1;self.receipt['signal_authority']=False
        if self.bad_close:self.receipt['cleanup_errors'].append('SYNTHETIC_CLOSE_FAILURE')

def fixture(m,first='pair',second='success'):
    c=m.goal_contract;clock=Clock();registry=c.build_registry(builder.commands(c))
    Driver,Backend=m.probe_pair.make_types(m.goal_driver,m.goal_windows_backend,m.goal_handle_lifecycle.terminal_receipt_qualified)
    b=Backend.__new__(Backend);grant=types.SimpleNamespace(registry=registry,deadline=clock()+60,
       work_deadline=clock()+56,server_unknown_continue=False,pair_collection_continue=True,pair_second_sample=True)
    grant.accepted=set();grant.terminal=set();grant.live=set();grant.stopped=False
    grant.halt=lambda:setattr(grant,'stopped',True)
    grant.mark_started=lambda name,row:grant.live.add(name)
    grant.mark_terminal=lambda name,row:(grant.live.discard(name),grant.terminal.add(name))
    grant.mark_server_ready=lambda row,owned:grant.accepted.add('server')
    grant.accept=lambda name,row:grant.accepted.add(name)
    b.grant=grant;b.roots=[];b.latest={};b.baseline=None;b.stopped=False;b.root_samples={}
    b.stream_bytes={};b.captured={};b.capture_failed=False;b.last_sample=clock();b.decoded=0
    b.records={};b.pins=[];b.closed_pins=[];b.keys_before=[];b.fail_record=False;b.fail_eligibility_record=False;b.raw_notice=[]
    rows=[]
    def record(row):
        if b.fail_record or b.fail_eligibility_record and row.get('stage')=='PAIR_FIRST_SAMPLE':
            b.journal.failed=True;raise ValueError('SYNTHETIC_RECORD_FAILURE')
        rows.append(copy.deepcopy(row))
    b.journal=types.SimpleNamespace(failed=False,rows=rows,emit=record,rejection=record,close=lambda:None,case='UNBOUND')
    b.marker=m.goal_notice.Marker(sink=b.notice_event,clock=lambda:10**15,cases=m.probe_pair.CASES)
    def put(path,row):
        if b.fail_record:raise ValueError('SYNTHETIC_RECORD_FAILURE')
        if str(path) in b.records:raise AssertionError('RESULT_REPLAY')
        b.records[str(path)]=copy.deepcopy(row)
    b.w=types.SimpleNamespace(K=types.SimpleNamespace(CloseHandle=lambda h:True),put=put,key_metadata=lambda:[],
        snapshot=lambda known:[],tcp_table=lambda:[{'pid':11,'port':8710,'address':'127.0.0.1'}],_journal=None)
    system={'commit_available_bytes':16*1024**3,'physical_available_bytes':8*1024**3,'commit_total_bytes':1024**3}
    b.system=lambda:system;b.observe=lambda *args:{'system':system};b.storage=lambda:None
    b.pause=lambda seconds:setattr(clock,'value',clock()+min(seconds,.05))
    b.init_pair_state(clock)
    m.goal_windows_backend.time=types.SimpleNamespace(monotonic=clock,sleep=lambda x:None)
    m.goal_authorization.time=types.SimpleNamespace(monotonic=clock)
    def payload(mode,side):
        if mode=='pair':return b'\r\r\n'.join(m.probe_pair.PUBLIC_LINES)+b'\r\r\n'
        if mode=='success':return ('MDDS_OBS_'+side+'_A8D3F641\r\n__MDDS_OBS_RC=0\r\n').encode()
        if mode=='request':return b'[E000002] request\n'
        if mode=='unknown':return b'auth SYNTHETIC_UNKNOWN\n'
        if mode=='e4_only':return m.probe_pair.PUBLIC_LINES[0]+b'\r\r\n'
        if mode=='selection_only':return m.probe_pair.PUBLIC_LINES[2]+b'\r\r\n'
        if mode=='duplicate':return payload('pair',side)*2
        if mode=='extra':return payload('pair',side)+b'ignored-but-not-eligible\n'
        raise AssertionError('FIXTURE_MODE')
    def register(step):
        data=b'' if step.kind=='server' else payload(first if step.id==m.probe_pair.FIRST else second,step.side)
        root=Root(step.id,clock,grant,m.goal_authorization.check_work_deadline,data)
        b.roots.append(root);b.captured[step.id]=0;return root
    b.register=register;d=Driver(registry,b,clock=clock)
    return b,d,clock

class PairTests(unittest.TestCase):
    def test_generated_registry_is_only_A_distinct_equal_length_tags_and_strict_effects(self):
        with runtime() as m:
            c=m.goal_contract;r=c.build_registry(builder.commands(c));c.validate_registry(list(r.values()))
            self.assertEqual(tuple(r),m.probe_pair.CASES)
            self.assertEqual(c.TARGETS['A1'],c.TARGETS['A2']);self.assertEqual(r[m.probe_pair.SECOND].requires,('server',))
            self.assertEqual(r[m.probe_pair.SECOND].requires_terminal,(m.probe_pair.FIRST,))
            self.assertEqual(len(c.body('A1')),len(c.body('A2')));self.assertNotEqual(c.body('A1'),c.body('A2'))
            self.assertNotIn('"',c.body('A1'));self.assertIn('r=$?',c.body('A1'))
            self.assertNotIn('server_auth_context_unknown_continue',m.goal_authorization.EFFECTS)
            self.assertIn('one_conditional_second_A_sample',m.goal_authorization.EFFECTS)
    def test_first_success_has_no_second_sample_and_owned_cleanup(self):
        with runtime() as m:
            b,d,clock=fixture(m,first='success');r=d.run_pair();closed=d.finish()
            self.assertEqual(r['diagnostic_outcome'],'FIRST_SAMPLE_SUCCESS');self.assertEqual(d.client_creates,1)
            self.assertNotIn(m.probe_pair.SECOND,d.used);self.assertTrue(m.probe_pair.observation_complete(d,closed))
            self.assertEqual([r.close_calls for r in b.roots],[1,1])
    def test_qualified_first_failure_then_second_success_keeps_failed_first(self):
        with runtime() as m:
            b,d,clock=fixture(m);r=d.run_pair();closed=d.finish()
            self.assertEqual(r['diagnostic_outcome'],'OBSERVED_FAILURE_THEN_RECOVERY')
            self.assertNotIn(m.probe_pair.FIRST,d.accepted);self.assertNotIn(m.probe_pair.FIRST,b.grant.accepted)
            self.assertIn(m.probe_pair.FIRST,d.terminal);self.assertEqual(d.client_creates,2)
            self.assertFalse(r['first_failure']['accepted']);self.assertTrue(r['first_failure']['cleanup']['business_blocked'])
            self.assertEqual(closed['failure'],'CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN')
            self.assertTrue(m.probe_pair.observation_complete(d,closed))
            self.assertEqual([r.stop_calls for r in b.roots],[1,0,0])
            self.assertEqual(len(b.marker.report()['events']),2)
    def test_non_pair_first_and_auth_unknown_never_create_second(self):
        for mode in ('request','unknown','e4_only','selection_only','extra','duplicate'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,clock=fixture(m,first=mode)
                with self.assertRaises(m.goal_contract.ContractError):d.run_pair()
                closed=d.finish();self.assertNotIn(m.probe_pair.SECOND,d.used)
                self.assertFalse(m.probe_pair.observation_complete(d,closed))
    def test_second_failure_or_unknown_stops_without_third(self):
        for mode in ('pair','request','unknown'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,clock=fixture(m,second=mode)
                with self.assertRaises(m.goal_contract.ContractError):d.run_pair()
                closed=d.finish();self.assertEqual(d.client_creates,2)
                self.assertFalse(m.probe_pair.observation_complete(d,closed));self.assertFalse(d.accepted-{*m.probe_pair.CASES})
    def first_observed(self,m):
        b,d,clock=fixture(m);d.start('server');d.wait_accepted('server');d.start(m.probe_pair.FIRST);d.wait(m.probe_pair.FIRST)
        return b,d,clock
    def test_deadline_budget_server_change_late_notice_and_record_failure_are_terminal(self):
        for mode in ('window','budget','server','notice'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,clock=self.first_observed(m)
                if mode=='window':clock.value+=2
                if mode=='budget':b.original_work_deadline=clock()+14.9
                if mode=='server':b.server_root.owner['birth']+=1
                if mode=='notice':b.marker.feed('server','stdout',b'auth unknown\n','MONITOR',1,2)
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                self.assertTrue(b.stopped);self.assertNotIn(m.probe_pair.SECOND,d.used);d.finish()
        with runtime() as m:
            b,d,clock=fixture(m);b.fail_record=True
            with self.assertRaises((m.goal_contract.ContractError,ValueError)):d.run_pair()
            d.finish();self.assertNotIn(m.probe_pair.SECOND,d.used)
    def test_exact_public_envelope_does_not_strip_extra_or_repeat(self):
        with runtime() as m:
            for eol in (b'\n',b'\r\n',b'\r\r\n'):
                data=eol.join(m.probe_pair.PUBLIC_LINES)+eol
                self.assertTrue(m.probe_pair.exact_failure_pair(data,b''))
                for bad in (data+data,data+b'\n',data[:-1],b' '+data,data.replace(eol,b'\r\r\r\n')):
                    self.assertFalse(m.probe_pair.exact_failure_pair(bad,b''))
                self.assertFalse(m.probe_pair.exact_failure_pair(data,b'warning'))
    def test_replay_finish_default_denial_and_late_unknown_do_not_gain_authority(self):
        with runtime() as m:
            b,d,clock=self.first_observed(m);stamp=b.first_closed_at;first=b.roots[1];count=first.close_calls
            b.outcome(first,d.registry[m.probe_pair.FIRST]);self.assertEqual(stamp,b.first_closed_at);self.assertEqual(count,first.close_calls)
            d.start(m.probe_pair.SECOND);d.wait(m.probe_pair.SECOND);closed=d.finish()
            self.assertIs(d.finish(),closed)
            b.marker.events.append({'event_type':'UNKNOWN','rule_id':'AUTH_CONTEXT_UNKNOWN','case':'server','complete_record':True})
            self.assertFalse(m.probe_pair.observation_complete(d,closed))
            with contextlib.redirect_stdout(__import__('io').StringIO()):self.assertEqual(m.goal_entry.main([]),78)
            with self.assertRaises(BaseException):m.probe_pair.actual_driver(object())
            self.assertNotIn('goal_win32_owned',sys.modules)
    def test_success_evidence_is_exact_and_keeps_real_remote_status(self):
        with runtime() as m:
            r=m.goal_contract.build_registry(builder.commands(m.goal_contract));s=r[m.probe_pair.FIRST]
            data=b'MDDS_OBS_A1_A8D3F641\n__MDDS_OBS_RC=0\n'
            for eol in (b'\n',b'\r\n',b'\r\r\n'):
                self.assertEqual(m.goal_evidence.evaluate_step(s,data.replace(b'\n',eol),b'',0)['remote_script_RC'],0)
            for bad in (data+b'x',data.replace(b'RC=0',b'RC=1'),data.replace(b'A1_',b'A2_')):
                with self.assertRaises(m.goal_contract.ContractError):m.goal_evidence.evaluate_step(s,bad,b'',0)
            with self.assertRaises(m.goal_contract.ContractError):m.goal_evidence.evaluate_step(s,data,b'warning',0)
    def test_first_exit_EOF_proof_close_and_durable_record_are_required(self):
        for mode in ('rc','eof','proof','close','eligibility_record'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,clock=fixture(m);d.start('server');d.wait_accepted('server');r=d.start(m.probe_pair.FIRST)
                if mode=='rc':r.native_rc=1
                if mode=='eof':r.auto_eof=False
                if mode=='proof':r.bad_proof=True
                if mode=='close':r.bad_close=True
                if mode=='eligibility_record':b.fail_eligibility_record=True
                with self.assertRaises((m.goal_contract.ContractError,ValueError)):d.wait(m.probe_pair.FIRST)
                self.assertIsNone(b.first_failure);self.assertNotIn(m.probe_pair.SECOND,d.used)
                if b.first_qualification_attempted:
                    calls=r.close_calls
                    with self.assertRaises(m.goal_contract.ContractError):b.outcome(r,d.registry[m.probe_pair.FIRST])
                    self.assertEqual(calls,r.close_calls)
                d.finish()
    def test_native_create_and_resume_window_cannot_be_extended_or_retried(self):
        for before,after in ((2.1,0),(0,2.1)):
            with self.subTest(before=before,after=after),runtime() as m:
                b,d,clock=self.first_observed(m);deadline=b.grant.work_deadline;register=b.register
                def delayed(step):
                    r=register(step)
                    if step.id==m.probe_pair.SECOND:r.delay_create=before;r.delay_after_create=after
                    return r
                b.register=delayed
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                second=b.roots[-1]
                self.assertEqual(second.create_calls,0 if before else 1)
                self.assertFalse(second.receipt['resumed']);self.assertEqual(b.grant.work_deadline,deadline)
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                d.finish();self.assertEqual(d.client_creates,2)
        # The native boundary must retain the complete 15-second client
        # budget as well as the shorter first-close + two-second window.
        for before,after in ((1.1,0),(0,1.1)):
            with self.subTest(budget_before=before,budget_after=after),runtime() as m:
                b,d,clock=fixture(m);d.start('server');d.wait_accepted('server')
                clock.value=b.original_work_deadline-16
                d.start(m.probe_pair.FIRST);d.wait(m.probe_pair.FIRST)
                deadline=b.grant.work_deadline;register=b.register
                def delayed_budget(step):
                    r=register(step)
                    if step.id==m.probe_pair.SECOND:r.delay_create=before;r.delay_after_create=after
                    return r
                b.register=delayed_budget
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                second=b.roots[-1]
                self.assertEqual(second.create_calls,0 if before else 1)
                self.assertFalse(second.receipt['resumed']);self.assertEqual(b.grant.work_deadline,deadline)
                self.assertLess(clock(),b.first_closed_at+2)
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                d.finish();self.assertEqual(d.client_creates,2)
    def test_history_owner_handle_and_endpoint_cannot_be_substituted(self):
        for mode in ('history','handle','endpoint'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,clock=self.first_observed(m)
                if mode=='history':b.marker.events[0]['offset_start']+=1
                if mode=='handle':b.server_root.pi.hProcess+=1
                if mode=='endpoint':b.w.tcp_table=lambda:[{'pid':99,'port':8710,'address':'127.0.0.1'}]
                with self.assertRaises(m.goal_contract.ContractError):d.start(m.probe_pair.SECOND)
                self.assertNotIn(m.probe_pair.SECOND,d.used);d.finish()
    def test_old_effects_or_missing_collection_permission_are_not_a_new_grant(self):
        with runtime() as m:
            a=m.goal_authorization
            source={'kind':'trusted_direct_user_reply','request_id':'SYNTHETIC_REQUEST','reply_id':'SYNTHETIC_REPLY',
                    'reply_to_request_verified':True,'sent_time_original':'SYNTHETIC_MINUTE','time_precision':'minute'}
            value={'scope':m.probe_pair.SCOPE,'nonce':m.probe_pair.NONCE,'manifest_sha256':'a'*64,
                'binding_sha256':'b'*64,'card_sha256':'c'*64,'source':source,'operator_verified_context':True,
                'fixture':False,'effects':{name:True for name in a.EFFECTS}}
            a.validate_approval_shape(value,'a'*64,'b'*64,'c'*64) # schema only; not an approval or permit
            self.assertEqual(len(a._grants),0)
            for key in ('first_exact_pair_collection','one_conditional_second_A_sample'):
                bad=copy.deepcopy(value);bad['effects'][key]=False
                with self.assertRaises(m.goal_contract.ContractError):a.validate_approval_shape(bad,'a'*64,'b'*64,'c'*64)
            bad=copy.deepcopy(value);bad['effects']['server_auth_context_unknown_continue']=True
            with self.assertRaises(m.goal_contract.ContractError):a.validate_approval_shape(bad,'a'*64,'b'*64,'c'*64)

if __name__=='__main__':unittest.main(verbosity=2)
