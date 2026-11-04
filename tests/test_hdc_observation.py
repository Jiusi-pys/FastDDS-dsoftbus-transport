"""Synthetic scoped-observation tests; WinDLL is forbidden by the fixture."""
import contextlib
import builtins
import io
import importlib.util
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


adapter=module('_observation_builder_tests',ROOT/'scripts/hdc_goal/prepare_observation.py')
builder=adapter.builder
pair=module('_strict_pair_test_helpers',ROOT/'tests/test_hdc_probe_pair.py')


@contextlib.contextmanager
def runtime():
    with patch.object(pair,'builder',builder),patch.object(pair,'NAMES',(*pair.NAMES,'probe_observe')):
        with pair.runtime() as m:yield m


def fixture(m,first='success',second='success'):
    # Adapt only the existing synthetic helper, not its strict policy module.
    delegate=types.SimpleNamespace(**vars(m.probe_pair))
    delegate.make_types=m.probe_observe.make_types
    proxy=types.SimpleNamespace(**vars(m));proxy.probe_pair=delegate
    with patch.object(pair,'builder',builder):b,d,clock=pair.fixture(proxy,first,second)
    b.grant.scoped_server_keyword_observation=True
    register=b.register
    def fresh_marker(step):
        root=register(step)
        root.pending=tuple(x.replace(b'A8D3F641',b'B6F13D90') for x in root.pending)
        return root
    b.register=fresh_marker
    return b,d,clock


def unknown(b,case='server',stream='stdout',data=b'auth SYNTHETIC_TELEMETRY\n',phase='MONITOR'):
    b.marker.feed(case,stream,data,phase,1,2)


class ObservationTests(unittest.TestCase):
    def test_fixed_new_contract_and_old_scope_unchanged(self):
        with runtime() as m:
            c=m.goal_contract;r=c.build_registry(builder.commands(c))
            self.assertEqual(c.SCOPE,m.probe_observe.SCOPE);self.assertEqual(c.NONCE,m.probe_observe.NONCE)
            _,old=builder.source_buffers() # separately pinned historical input
            self.assertEqual(c.TARGETS,dict.fromkeys(('A1','A2'),old.TARGETS['A']))
            self.assertEqual(c.body('A1'),"printf '%s\\n' 'MDDS_OBS_A1_B6F13D90'; r=$?; printf '__MDDS_OBS_RC=%s\\n' $r; exit $r")
            self.assertEqual(tuple(r),('server','probe.A1','probe.A2'))
            self.assertNotEqual(c.SCOPE,m.probe_pair.SCOPE)
            self.assertIn('server_auth_word_unknown_observation',m.goal_authorization.EFFECTS)
            self.assertNotIn('server_auth_context_unknown_continue',m.goal_authorization.EFFECTS)
            self.assertEqual(len(m.goal_authorization._grants),0)

    def test_first_success_and_telemetry_keep_unknown_business_block(self):
        for telemetry in (False,True):
            with self.subTest(telemetry=telemetry),runtime() as m:
                b,d,_=fixture(m)
                if telemetry:unknown(b)
                result=d.run_pair();closed=d.finish()
                self.assertTrue(m.probe_observe.observation_complete(d,closed))
                self.assertEqual(result['diagnostic_outcome'],'FIRST_SAMPLE_SUCCESS')
                self.assertEqual(d.client_creates,1)
                self.assertIs(closed['finalization']['business_blocked'],telemetry)
                self.assertEqual(b.marker.report()['classification'],'UNKNOWN' if telemetry else 'NONE')
                self.assertEqual(result['authentication_semantics'],'UNKNOWN_NOT_ATTESTED')

    def test_recovered_pair_keeps_failure_and_unknown_through_cleanup(self):
        with runtime() as m:
            b,d,_=fixture(m,first='pair');unknown(b)
            result=d.run_pair();closed=d.finish()
            self.assertEqual(result['diagnostic_outcome'],'OBSERVED_FAILURE_THEN_RECOVERY')
            self.assertFalse(b.first_failure['accepted']);self.assertTrue(b.first_failure['cleanup']['business_blocked'])
            self.assertTrue(m.probe_observe.observation_complete(d,closed))
            self.assertEqual(closed['failure'],'CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN')
            self.assertTrue(closed['finalization']['business_blocked'])
            self.assertEqual(len(b.marker.report()['events']),3)
            self.assertEqual([r.stop_calls for r in b.roots],[1,0,0])

    def test_disallowed_notice_matrix_blocks_without_second_sample(self):
        cases=[('server','stderr',b'auth unknown\n','MONITOR'),
               ('probe.A1','stdout',b'auth unknown\n','MONITOR'),
               ('server','stdout',b'[E000005] other\n','MONITOR'),
               ('server','stdout',b'auth [E000005]\n','MONITOR'),
               ('server','stdout',b'[E000002] request\n','MONITOR'),
               ('server','stdout',b'[E000003] denied\n','MONITOR'),
               ('server','stdout',b'x'*513+b'\n','MONITOR')]
        for args in cases:
            with self.subTest(args=args[:2]+(args[3],)),runtime() as m:
                b,d,_=fixture(m);unknown(b,*args)
                with self.assertRaises(m.goal_contract.ContractError):d.run_pair()
                closed=d.finish();self.assertFalse(m.probe_observe.observation_complete(d,closed))
                self.assertEqual(d.client_creates,0)

    def test_unknown_cannot_mask_late_request_denial_or_client_unknown(self):
        for case,data in [('server',b'[E000002] request\n'),('server',b'[E000003] denied\n'),('probe.A1',b'auth unknown\n')]:
            with self.subTest(case=case,data=data),runtime() as m:
                b,d,_=fixture(m);unknown(b);d.run_pair();closed=d.finish()
                self.assertTrue(m.probe_observe.observation_complete(d,closed))
                # Late retained events must revoke completion even if the
                # global classification remains at its previous rank.
                b.marker.closed=False;unknown(b,case,data=data)
                self.assertFalse(m.probe_observe.observation_complete(d,closed))

    def test_partial_record_binding_and_record_failure_rejected(self):
        for mode in ('partial','phase','binding','record','overflow','classification'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,_=fixture(m);unknown(b)
                if mode=='partial':b.marker.events[0]['complete_record']=False
                if mode=='phase':b.marker.events[0]['phase']='EOF'
                if mode=='binding':b.marker.events[0]['case']='other'
                if mode=='record':b.marker.record_failed=True
                if mode=='overflow':b.marker.overflow=True
                if mode=='classification':b.marker.level='NONE'
                with self.assertRaises(m.goal_contract.ContractError):b.notice_gate()
                self.assertFalse(m.probe_observe.observation_complete(d,d.finish()))

    def test_second_failure_and_bad_close_never_observation_complete(self):
        for mode in ('pair','request','unknown','close'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,_=fixture(m,first='pair',second='success' if mode=='close' else mode);unknown(b)
                if mode=='close':
                    register=b.register
                    def bad(step):
                        root=register(step)
                        if step.id=='server':root.bad_close=True
                        return root
                    b.register=bad;d.run_pair()
                else:
                    with self.assertRaises(m.goal_contract.ContractError):d.run_pair()
                self.assertFalse(m.probe_observe.observation_complete(d,d.finish()))
                self.assertLessEqual(d.client_creates,2)

    def test_exact_final_matrix_rejects_missing_or_forged_proof(self):
        for mode in ('root','identity','EOF','accepted','failure','journal','row','blocked','sample'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,_=fixture(m);unknown(b);d.run_pair();closed=d.finish()
                if mode=='root':b.roots.pop()
                if mode=='identity':b.roots[-1].receipt['terminal_binding_verified']=False
                if mode=='EOF':b.roots[-1].receipt['EOF']=False
                if mode=='accepted':b.roots[-1].receipt['accepted_evidence']=False
                if mode=='failure':closed['failure']=None
                if mode=='journal':b.journal.failed=True
                if mode=='row':closed['cleanup']=[]
                if mode=='blocked':closed['finalization']['business_blocked']=False
                if mode=='sample':closed['used'].append('probe.A2')
                self.assertFalse(m.probe_observe.observation_complete(d,closed))

    def test_old_factory_and_missing_grant_stop_before_native_import(self):
        with runtime() as m:
            self.assertNotIn('goal_win32_owned',sys.modules)
            for factory in (m.probe_observe.actual_driver,m.probe_pair.actual_driver):
                with self.assertRaises(m.goal_contract.ContractError):factory(object())
            b,d,_=fixture(m);b.grant.scoped_server_keyword_observation=False
            with self.assertRaises(m.goal_contract.ContractError):b.notice_gate()
            self.assertNotIn('goal_win32_owned',sys.modules)

    def test_member_grant_rejects_wrong_factory_or_backend_before_native_import(self):
        with runtime() as m:
            a=m.goal_authorization;c=m.goal_contract
            registry=c.build_registry(builder.commands(c))
            grant=a.GoalGrant('a'*64,'b'*64,registry,10000)
            grant.pair_collection_continue=True;grant.pair_second_sample=True
            grant.scoped_server_keyword_observation=True
            _,Pair=m.probe_pair.make_types(m.goal_driver,m.goal_windows_backend,m.goal_handle_lifecycle.terminal_receipt_qualified)
            _,Observe=m.probe_observe.make_types(m.goal_driver,m.goal_windows_backend,m.goal_handle_lifecycle.terminal_receipt_qualified)
            original=builtins.__import__;native_imports=[]
            def no_native(name,*args,**kwargs):
                if name=='goal_win32_owned':
                    native_imports.append(name)
                    raise AssertionError('SYNTHETIC_NATIVE_IMPORT_BOUNDARY')
                return original(name,*args,**kwargs)
            # Membership is synthesized only inside this isolated in-memory
            # runtime. It is not a real permit and cannot survive the fixture.
            a._grants.add(grant)
            try:
                with patch.object(builtins,'__import__',side_effect=no_native):
                    for call in (lambda:m.probe_pair.actual_driver(grant),lambda:Pair(grant),
                                 lambda:m.goal_windows_backend.GoalWindowsBackend(grant)):
                        with self.assertRaises(c.ContractError):call()
                    for effect in (False,None):
                        grant.scoped_server_keyword_observation=effect
                        for call in (lambda:m.probe_observe.actual_driver(grant),lambda:Observe(grant)):
                            with self.assertRaises(c.ContractError):call()
                    self.assertEqual(native_imports,[])
                    grant.scoped_server_keyword_observation=True
                    # Correct new routing reaches the intercepted import,
                    # proving the negative cases did not test a blanket block.
                    with self.assertRaisesRegex(AssertionError,'SYNTHETIC_NATIVE_IMPORT_BOUNDARY'):
                        m.probe_observe.actual_driver(grant)
                    self.assertEqual(native_imports,['goal_win32_owned'])
                    self.assertNotIn('goal_win32_owned',sys.modules)
            finally:a._grants.discard(grant)
            self.assertEqual(len(a._grants),0)

    def test_default_new_entry_refuses_without_a_control_or_native_import(self):
        with runtime() as m,contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(m.goal_entry.main([]),78)
            self.assertEqual(output.getvalue(),'READINESS_NOT_GRANTED_HDC0\n')
            self.assertNotIn('goal_win32_owned',sys.modules)
            self.assertEqual(len(m.goal_authorization._grants),0)

    def test_approval_schema_requires_new_effect_but_creates_no_grant(self):
        with runtime() as m:
            a=m.goal_authorization
            source={'kind':'trusted_direct_user_reply','request_id':'SYNTHETIC_REQUEST','reply_id':'SYNTHETIC_REPLY',
                    'reply_to_request_verified':True,'sent_time_original':'SYNTHETIC_MINUTE','time_precision':'minute'}
            value={'scope':m.goal_contract.SCOPE,'nonce':m.goal_contract.NONCE,'manifest_sha256':'a'*64,
                'binding_sha256':'b'*64,'card_sha256':'c'*64,'source':source,'operator_verified_context':True,
                'fixture':False,'effects':{name:True for name in a.EFFECTS}}
            a.validate_approval_shape(value,'a'*64,'b'*64,'c'*64)
            value['effects']['server_auth_word_unknown_observation']=False
            with self.assertRaises(m.goal_contract.ContractError):a.validate_approval_shape(value,'a'*64,'b'*64,'c'*64)
            self.assertEqual(len(a._grants),0)


if __name__=='__main__':unittest.main(verbosity=2)
