"""Targeted pure/fake tests. Never import a live Win32 Root or run HDC."""
import unittest,sys,tempfile,json
from pathlib import Path
from unittest.mock import patch
from goal_contract import *
from goal_authorization import GoalGrant,StepPermit,require_process_permission,validate_approval_shape
from goal_driver import GoalDriver,aggregate_reason,actual_driver
from goal_storage import scan

class FakeBackend:
    evidence_kind='SYNTHETIC_NONBUSINESS_ONLY'
    def __init__(self):self.created=[];self.stopped=False;self.results={}
    def precreate(self,step,deadline):
        if self.stopped:raise ContractError('STOPPED')
    def register(self,step):return {'step':step.id}
    def create_and_resume(self,root,step,deadline):self.created.append(step.id)
    def observe_all(self,roots,deadline):pass
    def outcome(self,root,step):return self.results.get(step.id)
    def pause(self,seconds):pass
    def halt(self):self.stopped=True
    def cleanup(self,root,deadline):return {'step':root['step'],'state':'FAKE_CLOSED','signals':0}

class AdapterTests(unittest.TestCase):
    def test_no_actual_grant_before_any_live_import(self):
        self.assertNotIn('goal_win32_owned',sys.modules)
        with self.assertRaises((ContractError,FileNotFoundError)):
            actual_driver(ACTIVE_ROOT,'a'*64,'b'*64,{})
        self.assertNotIn('goal_win32_owned',sys.modules)
    def test_synthetic_objects_are_not_process_permission(self):
        s=GoalStep('server','channel','',('-s',ENDPOINT,'-m'),'server')
        g=GoalGrant('a'*64,'b'*64,{s.id:s},100)
        for value in [None,True,{},StepPermit(g,s)]:
            with self.assertRaises(ContractError):require_process_permission(HDC,list(s.args),value)
        with self.assertRaises(ContractError):g.reserve_step('server')
    def test_old_scope_or_nonce_not_repurposed(self):
        for old in ['NEW_D05_NOTICE_90S_HDC_SERVER_FIVE_READONLY_CLIENTS','ONE_NEW_D04_CACHE_90S_SIX_PROCESS_BATCH','LOCAL_NONBUSINESS_ONLY']:
            with self.assertRaises(ContractError):validate_approval_shape({'scope':old,'nonce':NONCE},'a'*64,'b'*64)
    def test_fixed_argv_target_and_control_names(self):
        a,verify,b=publication_steps('data','A','release.A.1',RUN_ROOT/'control/data/release.A.1')
        self.assertEqual(a.args[:5],('-p','-s',ENDPOINT,'-t',TARGETS['A']))
        self.assertIn('S1_GOAL_STEP:',b.args[-1])
        self.assertIn(verify.id,b.requires)
        for name in ['../key','release.B.1','arbitrary.log','admission.B']:
            with self.assertRaises(ContractError):publication_steps('data','A',name,RUN_ROOT/'input')
    def test_once_and_live_long_dependency(self):
        s=GoalStep('server','channel','',('-s',ENDPOINT,'-m'),'server')
        c=GoalStep('boot','probe','A',device_args('A','fixed'),requires_started=('server',))
        backend=FakeBackend();driver=GoalDriver({s.id:s,c.id:c},backend,clock=lambda:0)
        with self.assertRaises(ContractError):driver.start('boot')
        driver.start('server');driver.start('boot')
        with self.assertRaises(ContractError):driver.start('server')
        self.assertEqual(backend.created,['server','boot'])
    def test_unknown_stops_dependent_creation(self):
        s=GoalStep('first','channel','A',device_args('A','fixed'))
        n=GoalStep('next','channel','B',device_args('B','fixed'),requires=('first',))
        backend=FakeBackend();driver=GoalDriver({s.id:s,n.id:n},backend,clock=lambda:0)
        driver.start('first');backend.results['first']={'step':'first','accepted':False,'terminal':True}
        with self.assertRaises(ContractError):driver.pump()
        with self.assertRaises(ContractError):driver.start('next')
        self.assertEqual(backend.created,['first']);self.assertFalse(driver.finish()['DDS_verified'])
    def test_aggregate_and_absolute_deadline(self):
        system={'commit_available_bytes':16*1024**3,'physical_available_bytes':8*1024**3,'commit_total_bytes':8*1024**3}
        processes=[{'private_bytes':LIMITS['private'],'working_set_bytes':1}]*5
        self.assertEqual(aggregate_reason(system,processes,None),'AGGREGATE_MEMORY')
        now=[0];backend=FakeBackend();driver=GoalDriver({},backend,clock=lambda:now[0]);now[0]=241
        with self.assertRaises(ContractError):driver.pump()
        self.assertTrue(backend.stopped)
    def test_cache_metadata_only_peak_and_unknown_neighbor(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'cases/server/tmp').mkdir(parents=True);(root/'evidence').mkdir()
            cache=root/'cases/server/tmp/.hdc.cache.log';cache.write_bytes(b'x'*17)
            with patch.object(Path,'read_bytes',side_effect=AssertionError('PRIVATE_CONTENT_READ')):
                first=scan(root,{'server'},{});self.assertFalse(first['content_read_or_hash'])
                cache.unlink();second=scan(root,{'server'},first['cache_state']);self.assertEqual(sum(v[2] for v in second['cache_state'].values()),17)
                (root/'cases/server/tmp/.hdc.cache.log.1').write_bytes(b'')
                with self.assertRaises(ContractError):scan(root,{'server'},second['cache_state'])
    def test_enumeration_error_cannot_be_empty_success(self):
        def denied(root,*,followlinks,onerror):
            onerror(PermissionError('synthetic unreadable task directory'))
            return iter(())
        with patch('goal_storage.os.walk',denied):
            with self.assertRaises(RuntimeError):scan(Path('E:/synthetic_goal'),{'server'},{})
    def test_package_content_and_size_are_bound_before_grant(self):
        from goal_authorization import verify_package
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);pins=[]
            for n in range(10):
                path=root/('source'+str(n)+'.py');data=('safe fixture'+str(n)).encode('ascii');path.write_bytes(data);pins.append({'path':str(path),'sha256':sha(data),'bytes':len(data),'purpose':'source'})
            manifest={'scope':SCOPE,'nonce':NONCE,'registry_sha256':registry_digest([]),'active_pins':pins}
            raw=canonical(manifest);(root/'GOAL_PACKAGE_FINAL_D01.json').write_bytes(raw)
            binding={'scope':SCOPE,'nonce':NONCE,'manifest_sha256':sha(raw),'run_root':str(RUN_ROOT),'full_executor_final':True,'actual_execution_authorized':False,'active_closure_bytes':sum(p['bytes'] for p in pins)}
            data=canonical(binding);(root/'FINAL_EXECUTION_BINDING_D01.json').write_bytes(data)
            # Ten arbitrary files cannot masquerade as the exact Goal roles.
            with self.assertRaises(ContractError):verify_package(root,sha(raw),sha(data),{})
            (root/'source0.py').write_bytes(b'changed fixture')
            with self.assertRaises(ContractError):verify_package(root,sha(raw),sha(data),{})
    def test_terminal_name_crosses_actual_backend_seam(self):
        from goal_windows_backend import GoalWindowsBackend
        from goal_notice import Marker
        class Root:
            case='probe.a.launch';output=[b'',b''];closed=False
            receipt={'EOF':True,'native_exit':1,'cleanup_errors':[],'stop_attempts':0}
            dest=Path('E:/synthetic_not_written')
            def state(self):return 'TERMINAL'
            def identity(self):pass
            def close(self):self.closed=True
        class Grant:
            def mark_terminal(self,*args):self.marked=True
        class Writer:
            def put(self,*args):pass
        backend=object.__new__(GoalWindowsBackend);backend.grant=Grant();backend.w=Writer();backend.notice_gate=lambda:None
        backend.marker=Marker(cases=(Root.case,)) # Pure parser only; no live constructor.
        from types import SimpleNamespace
        backend.journal=SimpleNamespace(failed=False)
        step=GoalStep('probe.a.launch','probe','A',device_args('A','fixed'),'long')
        value=backend.outcome(Root(),step)
        self.assertIsNotNone(value);self.assertFalse(value['accepted']);self.assertTrue(backend.grant.marked)

if __name__=='__main__':unittest.main(verbosity=2)
