"""Pure integration of policy, real cleanup methods and diagnostic qualification."""
import ast,copy,hashlib,importlib.util,json,pathlib,sys,types,unittest
from unittest.mock import patch
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/hdc_goal'))
import readonly_evidence as current
from goal_handle_lifecycle import terminal_receipt_qualified

class ContractError(Exception):pass
def need(ok,reason):
    if not ok:raise ContractError(reason)

def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def event(**changed):
    return dict(dict(schema='NOTICE_EVENT_V2',case='server',stream='stdout',phase='MONITOR',
      event_type='UNKNOWN',rule_id='AUTH_CONTEXT_UNKNOWN',trigger_family='AUTH_WORD_ONLY',complete_record=True),**changed)

def receipt(case):
    proof=dict(hProcess=11,pid=22,birth=33,image='SYNTHETIC_IMAGE',FileID=[1,2,3],
      method='ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN',required_apis_ok=True,
      image_pin_retained_at_check=True,full_image_was_verified_while_live=True)
    return dict(case=case,hProcess=11,owner=dict(pid=22,birth=33,image='SYNTHETIC_IMAGE'),FileID=[1,2,3],
      terminal_proof=proof,created=True,bound=True,terminal=True,EOF=True,
      terminal_binding_verified=True,signal_authority=False,cleanup_errors=[],
      native_exit=125 if case=='server' else 0,stop_attempts=1 if case=='server' else 0)

def fixture(module=current,events=(),flags=None,pin_error=False):
    # Exact maintained cleanup methods, with only OS operations and sinks fake.
    tree=ast.parse((ROOT/'scripts/hdc_goal/goal_windows_backend.py').read_text(encoding='utf-8-sig'))
    cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='GoalWindowsBackend')
    methods=[n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name in ('cleanup_notice','cleanup','complete_cleanup')]
    ns={'need':need,'ContractError':ContractError,'LIMITS':{'server_capture':4194304,'client_capture':65536}}
    exec(compile(ast.Module(body=methods,type_ignores=[]),'synthetic-owned-cleanup','exec'),ns)
    Base=type('PureCleanup',(),{n.name:ns[n.name] for n in methods})
    dmodule=types.SimpleNamespace(need=need,GoalDriver=object)
    _,B=module.make_types(dmodule,types.SimpleNamespace(GoalWindowsBackend=Base),terminal_receipt_qualified)
    b=B.__new__(B);b.parsers={};b.latest={};b.capture_failed=False
    report=dict(schema='NOTICE_EVIDENCE_V2',events=copy.deepcopy(list(events)),record_failed=False,
      overflow=False,failure_rules=[],emergency_events=[],closed=False,blocking=bool(events))
    report.update(flags or {})
    b.marker=types.SimpleNamespace(report=lambda:copy.deepcopy(report),notice=bool(events),
      eof=lambda *a:None,advance=lambda:None,close=lambda:report.update(closed=True))
    journal=[];b.journal=types.SimpleNamespace(failed=False,case=None,emit=journal.append,close=lambda:None)
    b.pins=[101] if pin_error else [];b.keys_before={};b.w=types.SimpleNamespace(
      key_metadata=lambda:{},K=types.SimpleNamespace(CloseHandle=lambda h:False),put=lambda *a:None,_journal=None)
    b.halt=lambda:None;b.evidence_kind='HOST_SYNTHETIC_ONLY'
    b.observations={c:{'source_bytes':0} for c in module.CASES if c!='server'}
    roots=[]
    for case in module.CASES:
        r=types.SimpleNamespace(case=case,receipt=receipt(case),closed=case!='server',output=[bytearray(),bytearray()],
          reads=[dict(name='stdout',eof=True),dict(name='stderr',eof=True)],dest=pathlib.Path('SYNTHETIC_ONLY'))
        r.state=lambda:'TERMINAL';r.identity=lambda:None
        r.drain=lambda *a:dict(bytes=0,status='EOF')
        r.close=lambda r=r:setattr(r,'closed',True)
        roots.append(r)
    b.roots=roots;b.captured={r.case:0 for r in roots}
    limits=dict(total_ms=60000,work_ms=56000)
    with patch.dict(sys.modules,goal_contract=types.SimpleNamespace(LIMITS=limits,ContractError=ContractError,need=need)):
        driver_module=load(ROOT/'scripts/hdc_goal/goal_driver.py','_qualification_driver')
    registry={r.case:types.SimpleNamespace(kind='server' if r.case=='server' else 'readonly') for r in roots}
    d=driver_module.GoalDriver(registry,b,clock=lambda:0.)
    d.used=set(module.CASES);d.accepted=set(module.CASES);d.live={'server':roots[0]}
    return d,b,report,journal

class QualificationTests(unittest.TestCase):
    def check(self,events=(),flags=None,pin_error=False):
        d,b,report,journal=fixture(events=events,flags=flags,pin_error=pin_error)
        cleanup=d.finish();return current.observation_complete(d,cleanup),d,b,report,cleanup
    def test_clean_diagnostic_uses_real_cleanup_and_driver(self):
        ok,d,b,report,cleanup=self.check()
        self.assertTrue(ok);self.assertIsNone(cleanup['failure'])
        self.assertFalse(cleanup['finalization']['business_blocked']);self.assertFalse(cleanup['DDS_verified'])
    def test_scoped_unknown_retained_without_fabricated_cleanup_error(self):
        ok,d,b,report,cleanup=self.check([event()]);self.assertTrue(ok)
        self.assertTrue(report['blocking']);self.assertEqual(report['events'],[event()])
        self.assertFalse(cleanup['cleanup'][0]['business_blocked'])
        self.assertFalse(cleanup['finalization']['business_blocked']);self.assertIsNone(cleanup['failure'])
    def test_scoped_cleanup_phase_telemetry(self):self.assertTrue(self.check([event(phase='CLEANUP')])[0])
    def test_other_unknowns_and_requests_stay_blocked(self):
        for changed in [dict(case='read.B'),dict(stream='stderr'),dict(complete_record=False),
          dict(trigger_family='OTHER'),dict(event_type='REQUEST_TEXT',rule_id='REQUEST_SENTENCE'),
          dict(event_type='DENIED_TEXT',rule_id='CODE_E000003')]:
            with self.subTest(kind=changed):self.assertFalse(self.check([event(**changed)])[0])
    def test_record_and_overflow_failures_stay_blocked(self):
        for flags in [dict(record_failed=True),dict(overflow=True),dict(failure_rules=['CLOCK']),dict(emergency_events=[{}])]:
            with self.subTest(kind=flags):self.assertFalse(self.check([event()],flags)[0])
    def test_pin_close_failure_stays_unknown(self):
        ok,d,b,report,cleanup=self.check([event()],pin_error=True)
        self.assertFalse(ok);self.assertEqual(cleanup['finalization']['state'],'UNKNOWN')
    def test_missing_client_acceptance_rejected(self):
        ok,d,b,report,c=self.check([event()]);d.accepted.remove('read.A');self.assertFalse(current.observation_complete(d,c))
    def test_client_nonzero_or_stop_rejected(self):
        for key,value in [('native_exit',1),('stop_attempts',1)]:
            ok,d,b,report,c=self.check([event()]);b.roots[1].receipt[key]=value
            self.assertFalse(current.observation_complete(d,c))
    def test_unclosed_or_invalid_binding_rejected(self):
        for key in ('closed','binding'):
            ok,d,b,report,c=self.check([event()])
            if key=='closed':b.roots[1].closed=False
            else:b.roots[1].receipt['terminal_proof']['birth']=44
            self.assertFalse(current.observation_complete(d,c))
    def test_cleanup_blocks_or_errors_not_waived(self):
        for target,key,value in [('row','business_blocked',True),('final','business_blocked',True),
          ('final','cleanup_errors',['FAILED']),('final','state','UNKNOWN'),('row','EOF',False)]:
            ok,d,b,report,c=self.check([event()])
            (c['cleanup'][0] if target=='row' else c['finalization'])[key]=value
            self.assertFalse(current.observation_complete(d,c))
    def test_existing_driver_failure_not_waived(self):
        ok,d,b,report,c=self.check([event()]);c['failure']='OBSERVATION_OR_RECEIPT_UNKNOWN'
        self.assertFalse(current.observation_complete(d,c))
    def test_failed_journal_and_source_cap_not_waived(self):
        ok,d,b,report,c=self.check([event()]);b.journal.failed=True;self.assertFalse(current.observation_complete(d,c))
        b.journal.failed=False;b.observations['read.A']['source_bytes']=25601
        self.assertFalse(current.observation_complete(d,c))

class PreparedRegressionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        builder=load(ROOT/'scripts/hdc_goal/prepare_prepared_readonly_evidence.py','_qualification_prepared_builder')
        buffers,_=builder.source_buffers()
        cls.future=builder.b.load_buffer(buffers['readonly_evidence.py'],'_qualified_future',builder.parent.ACTIVE/'readonly_evidence.py')
        cls.frozen=load(builder.parent.ACTIVE/'readonly_evidence.py','_qualified_frozen')
    def test_saved_frozen_defect_and_corrected_prepared_composition(self):
        d,b,report,journal=fixture(self.future,[event()]);cleanup=d.finish()
        self.assertTrue(self.future.observation_complete(d,cleanup))
        self.assertFalse(self.frozen.observation_complete(d,cleanup))
        self.assertEqual(len(b.roots),4);self.assertEqual(set(b.observations),{'targets','read.B','read.A'})
    def test_history_exit_one_and_frozen_source_preserved(self):
        p=ROOT/'build/d04-prepared-readonly-package-20261103-d01/prepare/ACTUAL_TEST_RESULT_D01.json'
        before=p.read_bytes();j=json.loads(before)
        self.assertEqual(hashlib.sha256(before).hexdigest(),'74041aba43b812e7234c8488eed1e3c1be306c1049f7cd134c47ff1afa243a82')
        self.assertEqual(j['native_exit'],1);self.assertEqual(j['outer_controller_exit'],1)
        d,b,report,events=fixture(self.future,[event()]);self.assertTrue(self.future.observation_complete(d,d.finish()))
        self.assertTrue(p.read_bytes()==before,'historical evidence changed')

if __name__=='__main__':unittest.main()
