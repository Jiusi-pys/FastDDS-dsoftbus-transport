"""Offline synthetic tests; never invoke HDC, read raw streams or load Win32."""
import ast,importlib.util,pathlib,sys,types,unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('_prepared_test_builder',ROOT/'scripts/hdc_goal/prepare_prepared_readonly_evidence.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
buffers,_=builder.source_buffers();contract,commands,registry=builder.commands(buffers)
def load(name):return builder.b.load_buffer(buffers[name+'.py'],name,builder.parent.ACTIVE/(name+'.py'))
listing=load('fixed_target_listing');evidence=load('readonly_evidence')
with patch.dict(sys.modules,readonly_evidence=evidence):prepared=load('prepared_readonly_evidence')


def row(target,state='Connected',label='SYNTHETIC_DEVICE',eol=b'\n'):
    return (target+'\t\tUSB\t'+state+'\t'+label+'\thdc').encode()+eol


class ProjectionTests(unittest.TestCase):
    def setUp(self):
        self.patch=patch.dict(sys.modules,fixed_target_listing=listing);self.patch.start()
        self.addCleanup(self.patch.stop);self.p=prepared.TargetPair({'A':'SYNTH_A','B':'SYNTH_B'})
    def test_connected_pair_and_other_fields_are_discarded(self):
        self.p.feed(row('OTHER',label='PRIVATE_CANARY')+row('SYNTH_B')+row('SYNTH_A'))
        result=self.p.finish();self.assertEqual(set(result['fixed_targets']),{'A','B'})
        self.assertTrue(all(r['state']=='CONNECTED' for r in result['fixed_targets'].values()))
        self.assertNotIn('PRIVATE_CANARY',str(result));self.assertFalse(result['handshake_proved'])
    def test_fragmented_crcrlf(self):
        raw=row('SYNTH_A',eol=b'\r\r\n')+row('SYNTH_B',eol=b'\r\r\n')
        for value in raw:self.p.feed(bytes([value]))
        self.assertTrue(self.p.finish()['list_complete'])
    def test_absent_is_not_ready(self):
        self.p.feed(row('SYNTH_A'))
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
    def test_rejection_retains_only_fixed_states_and_reason(self):
        rows=[];self.p=prepared.TargetPair({'A':'SYNTH_A','B':'SYNTH_B'},rows.append)
        self.p.feed(row('SYNTH_A')+row('SYNTH_B','Ready',label='PRIVATE_CANARY'))
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
        self.assertEqual(rows[0]['prepared_target_snapshot']['fixed_targets']['B']['state'],'READY')
        self.assertFalse(rows[0]['prepared_target_snapshot']['accepted'])
        self.assertEqual(rows[1]['prepared_target_rejection']['reason'],'FIXED_TARGET_NOT_CONNECTED')
        self.assertNotIn('PRIVATE_CANARY',str(rows));self.assertNotIn('SYNTH_B',str(rows))
    def test_incomplete_rejection_retains_fixed_reason(self):
        rows=[];self.p=prepared.TargetPair({'A':'SYNTH_A','B':'SYNTH_B'},rows.append)
        self.p.feed(row('SYNTH_A')[:-1])
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
        self.assertEqual(rows,[{'prepared_target_rejection':{'reason':'TARGET_LIST_FORMAT'}}])
    def test_d04_accepted_whitespace_row_and_optional_suffix(self):
        self.p.feed(b'SYNTH_A USB Connected Device A\nSYNTH_B\tTCP\tConnected\tDevice B\thdc\n')
        self.assertTrue(self.p.finish()['list_complete'])
    def test_ready_is_not_connected(self):
        self.p.feed(row('SYNTH_A')+row('SYNTH_B','Ready'))
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
    def test_unknown_device_state_is_not_authorization_or_connected(self):
        self.p.feed(row('SYNTH_A')+row('SYNTH_B','Unknown'))
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
    def test_unauthorized_stops_immediately(self):
        with self.assertRaises(evidence.EvidenceUnknown):self.p.feed(row('SYNTH_B','Unauthorized'))
    def test_duplicate_fixed_target_rejected(self):
        with self.assertRaises(evidence.EvidenceUnknown):self.p.feed(row('SYNTH_A')*2)
    def test_malformed_and_incomplete_rejected(self):
        with self.assertRaises(evidence.EvidenceUnknown):self.p.feed(b'malformed\n')
        self.p=prepared.TargetPair({'A':'SYNTH_A','B':'SYNTH_B'});self.p.feed(row('SYNTH_A')+row('SYNTH_B')[:-1])
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()
    def test_bounded_total_and_line_size(self):
        with self.assertRaises(evidence.EvidenceUnknown):
            for _ in range(9):self.p.feed(b'X'*4096)
    def test_total_list_cap_is_independent_of_row_cap(self):
        with self.assertRaises(evidence.EvidenceUnknown):
            for _ in range(120):self.p.feed(row('OTHER',label='X'*256))
    def test_empty_list_rejected(self):
        self.p.feed(b'[Empty]\r\thdc\r\n')
        with self.assertRaises(evidence.EvidenceUnknown):self.p.finish()


class FakeBackend:
    def __init__(self):self.driver=None;self.fail_pump=False
    def pause(self,n):self.driver.now+=n


class FakeDriver:
    def __init__(self,*,costs=None):
        self.now=0.;self.deadline=56.;self.log=[];self.costs=costs or {};self.pumps=0
        self.backend=FakeBackend();self.backend.driver=self;self.clock=lambda:self.now
    def start(self,name):self.log.append((name,self.now))
    def wait_accepted(self,name):pass
    def wait(self,name):self.now+=self.costs.get(name,0)
    def check_time(self):
        if self.now>=self.deadline:raise RuntimeError('GLOBAL_DEADLINE')
    def pump(self):
        self.check_time();self.pumps+=1
        if self.backend.fail_pump:raise RuntimeError('NOTICE_OR_RESOURCE_FAILURE')
    def summary(self):return {'offline_fake':True}


class WorkflowTests(unittest.TestCase):
    def setUp(self):
        d=types.SimpleNamespace(need=evidence.require)
        with patch.object(prepared.evidence,'make_types',return_value=(FakeDriver,FakeBackend)):
            self.D,self.B=prepared.make_types(d,types.SimpleNamespace(),lambda r:True)
    def test_reuses_monitored_ten_seconds_and_exact_order(self):
        d=self.D();result=d.run_readonly()
        self.assertEqual([r[0] for r in d.log],list(prepared.CASES));self.assertGreaterEqual(d.log[1][1],10)
        self.assertGreater(d.pumps,100);self.assertFalse(result['prequery_observation_window']['handshake_proved'])
    def test_window_observes_notice_resource_failure(self):
        d=self.D();d.backend.fail_pump=True
        with self.assertRaises(RuntimeError):d.run_readonly()
        self.assertEqual([r[0] for r in d.log],['server'])
    def test_global_deadline_is_not_extended(self):
        d=self.D();d.deadline=5
        with self.assertRaises(RuntimeError):d.run_readonly()
        self.assertEqual([r[0] for r in d.log],['server'])
    def test_budget_exhaustion_prevents_remaining_queries(self):
        d=self.D(costs={'targets':20})
        with self.assertRaises(evidence.EvidenceUnknown):d.run_readonly()
        self.assertEqual([r[0] for r in d.log],['server','targets'])
    def test_targets_creation_has_five_second_deadline(self):
        class Base:
            def create_and_resume(self,root,step,deadline):return deadline
        with patch.object(prepared.evidence,'make_types',return_value=(FakeDriver,Base)):
            _,B=prepared.make_types(types.SimpleNamespace(need=evidence.require),types.SimpleNamespace(),lambda r:True)
        backend=B();backend.grant=types.SimpleNamespace(work_deadline=100.)
        with patch.object(prepared.time,'monotonic',return_value=10.):
            self.assertEqual(backend.create_and_resume(None,types.SimpleNamespace(id='targets'),90),15)
            self.assertEqual(backend.create_and_resume(None,types.SimpleNamespace(id='read.B'),90),25)
            with self.assertRaises(evidence.EvidenceUnknown):backend.create_and_resume(None,types.SimpleNamespace(id='read.B'),24)
    def test_live_target_deadline_checked_before_and_after_observation(self):
        class Base:
            def observe_all(self,roots,deadline):self.calls+=1
        with patch.object(prepared.evidence,'make_types',return_value=(FakeDriver,Base)):
            _,B=prepared.make_types(types.SimpleNamespace(need=evidence.require),types.SimpleNamespace(),lambda r:True)
        backend=B();backend.calls=0;root=types.SimpleNamespace(case='targets',identity_deadline=5.)
        with patch.object(prepared.time,'monotonic',return_value=5.):
            with self.assertRaises(evidence.EvidenceUnknown):backend.observe_all([root],56.)
        self.assertEqual(backend.calls,0)
        with patch.object(prepared.time,'monotonic',side_effect=[4.9,5.1]):
            with self.assertRaises(evidence.EvidenceUnknown):backend.observe_all([root],56.)
        self.assertEqual(backend.calls,1)


class WiringTests(unittest.TestCase):
    def test_exact_registry_dependencies_and_caps(self):
        self.assertEqual(list(registry),list(prepared.CASES));self.assertEqual(contract.LIMITS['clients'],3)
        self.assertEqual(registry['read.B'].requires,('server','targets'))
        self.assertEqual(registry['read.A'].requires,('server','targets','read.B'))
        self.assertEqual(registry['targets'].args,('-p','-s',contract.ENDPOINT,'list','targets','-v'))
        for k,v in [('total_ms',60000),('work_ms',56000),('concurrent_clients',1)]:self.assertEqual(contract.LIMITS[k],v)
    def test_query_bodies_unchanged_and_source_caps(self):
        original,_=builder.original()
        old=builder.b.load_buffer(original['readonly_evidence.py'],'_old_query',builder.parent.ACTIVE/'readonly_evidence.py')
        context=load('goal_read_context')
        with patch.dict(sys.modules,readonly_evidence=evidence,goal_read_context=context):
            self.assertTrue(all(contract.body(s)==old.query(s,context.EXPECTED[s]) for s in ('A','B')),'fixed typed/proc query changed')
        self.assertEqual(sum(evidence.CAPS.values())+4096,24576);self.assertEqual(evidence.SOURCE_CAP,25600)
    def test_listing_framing_ast_reused_with_only_d04_row_adaptation(self):
        old=ast.parse((ROOT/'scripts/hdc_goal/state_samples.py').read_text())
        new=ast.parse(buffers['fixed_target_listing.py'])
        a=next(n for n in old.body if isinstance(n,ast.ClassDef) and n.name=='Listing')
        b=next(n for n in new.body if isinstance(n,ast.ClassDef) and n.name=='Listing')
        old={n.name:ast.dump(n) for n in a.body if isinstance(n,ast.FunctionDef)}
        new={n.name:ast.dump(n) for n in b.body if isinstance(n,ast.FunctionDef)}
        self.assertEqual(set(old),set(new))
        self.assertEqual([n for n in old if old[n]!=new[n]],['row'])
    def test_case_tables_and_completion_shape(self):
        self.assertEqual(evidence.CASES,prepared.CASES)
        for name in ('goal_notice','goal_storage'):
            tree=ast.parse(buffers[name+'.py'])
            cases=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='CASES' for t in n.targets))
            self.assertEqual(cases,prepared.CASES)
        text=buffers['readonly_evidence.py'].decode();self.assertIn('len(b.roots)!=4',text)
        self.assertIn("set(b.observations)!={'targets','read.B','read.A'}",text)
    def test_live_import_requires_new_grant(self):
        with patch.dict(sys.modules,goal_authorization=types.SimpleNamespace(_grants=set()),goal_contract=types.SimpleNamespace(need=evidence.require)):
            with self.assertRaises(evidence.EvidenceUnknown):prepared.actual_driver(object())
    def test_all_generated_sources_parse_and_are_deterministic(self):
        again,_=builder.source_buffers();self.assertTrue(buffers==again,'generated closure changed')
        self.assertEqual(len(buffers),19)
        for name,data in buffers.items():ast.parse(data,filename=name)
    def test_stream_and_authorization_contract_retained(self):
        self.assertIn(b'FIXED_READONLY_COMMAND_PIN',buffers['goal_win32_owned.py'])
        self.assertNotIn(b"open(self.dest/(name+'.raw')",buffers['goal_win32_owned.py'])
        self.assertIn(b'one_fixed_AB_target_list_after_monitored_window',buffers['goal_authorization.py'])
        self.assertIn(b'from prepared_readonly_evidence import actual_driver',buffers['goal_entry.py'])


class ServerIntegrationTests(unittest.TestCase):
    def fixture(self):
        # Use the real generated scheduler and the exact maintained server
        # outcome body. Replace only OS-facing observation/creation primitives.
        with patch.dict(sys.modules,goal_contract=contract):real_driver=load('goal_driver')
        tree=ast.parse(buffers['goal_windows_backend.py'])
        method=next(n for n in ast.walk(tree) if isinstance(n,ast.FunctionDef) and n.name=='outcome')
        ns={'need':contract.need,'LIMITS':contract.LIMITS,'time':prepared.time}
        exec(compile(ast.Module(body=[method],type_ignores=[]),'synthetic-server-outcome','exec'),ns)
        now=[0.];counters={'monitor':0,'identity':0,'listener':0,'notice':0};healthy=[True]
        def monitor(self,roots,deadline):counters['monitor']+=1;self.notice_gate()
        def server_ok(self):counters['listener']+=1;return healthy[0]
        def report():
            counters['notice']+=1
            return dict(schema='NOTICE_EVIDENCE_V2',record_failed=False,overflow=False,failure_rules=[],emergency_events=[],events=[])
        Base=type('Base',(),dict(outcome=ns['outcome'],observe_all=monitor,server_ok=server_ok,
             pause=lambda self,n:now.__setitem__(0,now[0]+n),halt=lambda self:None))
        D,B=prepared.make_types(real_driver,types.SimpleNamespace(GoalWindowsBackend=Base),lambda r:True)
        class ReachedTargets(Exception):pass
        class Driver(D):
            def start(self,name):
                if name=='server':return root
                raise ReachedTargets(name)
        backend=B.__new__(B);backend.observations={};backend.parsers={}
        backend.marker=types.SimpleNamespace(report=report);backend.journal=types.SimpleNamespace(case='server')
        accepted=set()
        backend.grant=types.SimpleNamespace(accepted=accepted,deadline=60.,work_deadline=56.,
            mark_server_ready=lambda receipt,owned:accepted.add('server'))
        def identity(**kw):counters['identity']+=1;return 'LIVE'
        root=types.SimpleNamespace(case='server',goal_started=0.,identity_deadline=56.,
             receipt={'case':'server'},state=lambda:'LIVE',identity=identity)
        driver=Driver(registry,backend,clock=lambda:now[0]);driver.live={'server':root};driver.used={'server'}
        return driver,backend,now,counters,healthy,ReachedTargets
    def test_real_scheduler_server_outcome_survives_monitored_window(self):
        d,b,now,count,healthy,Reached=self.fixture()
        with patch.object(prepared.time,'monotonic',side_effect=lambda:now[0]):
            with self.assertRaises(Reached):d.run_readonly()
        self.assertGreaterEqual(now[0],10.)
        self.assertIn('server',d.accepted);self.assertIn('server',d.live)
        for name in ('monitor','identity','listener','notice'):self.assertGreater(count[name],100)
    def test_first_ready_acceptance_still_has_original_ten_second_cap(self):
        d,b,now,count,healthy,Reached=self.fixture();now[0]=10.1
        with patch.object(prepared.time,'monotonic',side_effect=lambda:now[0]):
            with self.assertRaisesRegex(contract.ContractError,'SERVER_ACCEPT_DEADLINE'):d.pump()
        self.assertNotIn('server',d.accepted)
    def test_accepted_server_listener_loss_still_stops(self):
        d,b,now,count,healthy,Reached=self.fixture()
        with patch.object(prepared.time,'monotonic',side_effect=lambda:now[0]):
            d.pump();now[0]=10.1;healthy[0]=False
            with self.assertRaises(contract.ContractError):d.pump()
        self.assertIsNotNone(d.failure)


if __name__=='__main__':unittest.main()
