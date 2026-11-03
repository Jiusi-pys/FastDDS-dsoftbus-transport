"""Current executor integration with marked effects in memory/new temp files.

No Win32 constructor, HDC/tool process, native image or identity API is used.
The fake controller does not qualify the already separately tested C++ codec.
"""
import json,tempfile,unittest,time,sys
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import goal_contract as contract
import goal_workflow as workflow
import goal_evidence as evidence
from goal_contract import GoalStep,need,sha,ContractError
from goal_authorization import PAYLOAD_TARGETS,validate_fresh
from goal_driver import GoalDriver
from goal_records import qualify_window_result

APP='a'*64;LAUNCHER='b'*64;BRIDGE='c'*64
ARTIFACTS={'app':{'sha256':APP},'launcher':{'sha256':LAUNCHER},'bridge':{'sha256':BRIDGE}}

def record(values):return ''.join(k+'='+str(v)+'\n' for k,v in values.items()).encode('ascii')

class Effects:
    evidence_kind='MARKED_FAKE_CONTROLLER_NATIVE_AND_TRANSPORT_NO_BUSINESS'
    def __init__(self,root,registry,fault=None):
        self.root=root;self.registry=registry;self.fault=fault;self.created=[];self.live={};self.stopped=False;self.stage=1
    def halt(self):self.stopped=True
    def precreate(self,step,deadline):need(not self.stopped,'FAKE_STOPPED')
    def register(self,step):
        root=SimpleNamespace(case=step.id,output=[bytearray(),bytearray()]);self.live[step.id]=root;return root
    def create_and_resume(self,root,step,deadline):
        self.created.append(step.id)
        if step.kind=='long':
            pid=200+(step.side=='B');parent=100+(step.side=='B');birth=300+(step.side=='B')
            root.output[0].extend(('DIRECT_SUP_BEGIN parent=%s child=%s birth=%s scope=DIRECT_CHILD_ONLY descendants=UNKNOWN\n'%(parent,pid,birth)).encode())
        if step.kind=='receive':
            dest=Path(step.args[-1]);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(('MARKED_FIXTURE_JOURNAL_'+str(self.stage)).encode())
    def observe_all(self,roots,deadline):
        if 'controller' not in self.live:return
        directory=self.root/'control/data'
        if self.stage>1 and not (directory/('delivered.'+str(self.stage-1))).exists():return
        if self.stage<=4 and all((directory/('events.'+s)).exists() for s in ['A','B']):
            for side in ['A','B']:
                path=directory/('release.'+side+'.'+str(self.stage))
                if not path.exists():path.write_bytes(('MARKED_FIXTURE_RELEASE_'+side+'_'+str(self.stage)).encode())
            if (directory/('delivered.'+str(self.stage))).exists():self.stage+=1
    def pause(self,seconds):pass
    def cleanup(self,root,deadline):return {'step':root.case,'state':'TERMINAL','EOF':True,'stops':0,'only_fake':True}
    def body(self,step):
        side=step.side;window=step.phase;run='5c7a9e2130d0aa60' if window=='probe' else '5c7a9e2130d0aa61';pid=200+(side=='B');parent=100+(side=='B');birth=300+(side=='B')
        if step.id.endswith('.boot'):
            return record(dict(format='S1GOALBOOT1',side=side,pid=999 if self.fault=='boot' else pid,parent=parent,birth=birth,caller=1000,uid=2000,euid=2000,gid=2000,egid=2000,wall=1700000000,mono=1000000000,image=contract.DEVICE_ROOT+'/own_goal_launcher',image_sha=LAUNCHER))
        if step.id.endswith('.node'):
            return record(dict(format='S1GOALNODE1',run=int(run,16),side=side,pid=pid,parent=parent,target=3000+(side=='B'),network='network'+side,udid='udid'+side,image_sha=APP))
        if step.id.endswith('.result'):
            final=record(dict(format='S1GOALEXIT1',run=int(run,16)+(self.fault=='run'),pid=pid,status=0,restored_entry=1,restore_rc_valid=1,restore_rc=0,worker_parent_restoration='UNKNOWN',SDK_quiet='UNKNOWN',phase=10,native_rc_valid=1,native_rc=0))
            if window=='probe':return record(dict(format='S1GOALPROBE1',run=int(run,16),pid=pid,entry_selected=1,one_owned_worker_selected=1,all_SDK_workers='UNKNOWN',project_business_calls=0))+final
            return record(dict(format='S1GOALRESULT1',status=0,result_status=0,phase=4,writes=3,takes=3,local_control_drained=1,owners_retained=0,SDK_quiescence_unproven=1,destroy_rc=0,primary_phase=10,native_rc_valid=1,native_rc=0))+final
        if step.byte_source:return (sha(Path(step.byte_source).read_bytes())+'  '+contract.DEVICE_ROOT+'/fixture.incoming\n').encode()
        return b''
    def outcome(self,root,step):
        if step.kind=='server':return dict(step=step.id,accepted=True,terminal=False)
        if step.kind=='controller' and self.stage<=4:return None
        if step.kind=='long':
            directory=self.root/'control'/step.phase
            if step.phase=='probe' and not (directory/('admission.'+step.side)).exists():return None
            if step.phase=='data' and self.stage<=4:return None
            parsed={'native_root':dict(created_parent=100+(step.side=='B'),created_child=200+(step.side=='B'),birth=300+(step.side=='B'))}
        elif step.id.startswith('own.'):
            parsed={'own':{'target':3000+(step.side=='B'),'Native':True,'normalAPL':True,'single_DATASYNC':'GRANTED'}}
        elif step.id.startswith('readonly.') or step.id=='targets':parsed={'fixture_RO':True}
        elif step.kind=='controller':parsed={'controller_protocol_complete':True,'fixture':True}
        else:
            body=self.body(step);stdout=body+('\nS1_GOAL_STEP:'+step.id+':0\n').encode()
            parsed=evidence.evaluate_step(step,stdout,b'',0)
        return dict(step=step.id,accepted=True,terminal=True,native_exit=0,evidence={'accepted':True,'parsed':parsed})

class GoalWorkflowTests(unittest.TestCase):
    def setup_case(self,root,fault=None):
        payloads=[dict(source=str(root/('payload'+str(n))),target=target,sha256='a'*64,bytes=1,executable=n<4) for n,target in enumerate(PAYLOAD_TARGETS.values())]
        readonly=[{'argv':[contract.HDC,'-p','-s',contract.ENDPOINT,'list','targets','-v']}]
        for side in ['A','A','B','B']:readonly.append({'argv':[contract.HDC,*contract.device_args(side,'FIXED_SYNTHETIC_RO')]})
        controller={'path':str(root/'goal_controller.exe'),'sha256':'d'*64}
        steps=contract.build_registry(payloads,readonly,controller);registry=contract.validate_registry(steps)
        backend=Effects(root,registry,fault);driver=GoalDriver(registry,backend)
        return workflow.FixedGoalWorkflow(driver,ARTIFACTS,'e'*64),backend,steps
    def test_full_fixed_procedure_marked_fake_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            with patch.multiple(contract,RUN_ROOT=root),patch.multiple(workflow,RUN_ROOT=root),patch.multiple(evidence,RUN_ROOT=root):
                procedure,backend,steps=self.setup_case(root);result=procedure.run()
                self.assertTrue(result['all_four_windows_and_controller_accepted'])
                self.assertEqual(len(result['Native_results']),4)
                self.assertEqual(result['evidence_kind'],backend.evidence_kind)
                self.assertEqual(sum(s.kind not in ('server','controller') for s in steps),121)
                self.assertLessEqual(len(backend.created),123)
                self.assertEqual(sum(x=='server' for x in backend.created),1)
                self.assertTrue(all((root/'control/data'/('delivered.'+str(n))).is_file() for n in range(1,5)))
                self.assertLess(backend.created.index('probe.a.admission.a.publish'),backend.created.index('probe.b.boot'))
    def test_bad_boot_or_wrong_run_stops_before_data(self):
        for fault in ['boot','run']:
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as directory:
                root=Path(directory)
                with patch.multiple(contract,RUN_ROOT=root),patch.multiple(workflow,RUN_ROOT=root),patch.multiple(evidence,RUN_ROOT=root):
                    procedure,backend,_=self.setup_case(root,fault)
                    with self.assertRaises(ContractError):procedure.run()
                    self.assertNotIn('data.a.launch',backend.created)
    def test_current_controller_exact_bytes_not_prefix(self):
        from goal_authorization import safe_input
        step=GoalStep('controller','data','',('--coordinate','fixed','110000'),'controller')
        template='S1CONTROLLER1\nfixture=0\nrun=%s\nROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1\nNative_attestation=0\nentry_restore_and_exit_checked_separately=1\n'
        with patch('goal_authorization.safe_input',return_value=(template%'5c7a9e2130d0aa61').encode()):self.assertTrue(evidence.evaluate_step(step,b'',b'',0)['controller_protocol_complete'])
        for run in ['5c7a9e2130d0aa60','0'*16]:
            with patch('goal_authorization.safe_input',return_value=(template%run).encode()):
                with self.assertRaises(ContractError):evidence.evaluate_step(step,b'',b'',0)
    def test_dynamic_transfer_mismatch_and_delivered_both_required(self):
        from goal_records import delivery_receipt
        s=GoalStep('data.a.pair.verify','data','A',(),byte_source='E:/synthetic_qualified_record')
        with patch('goal_authorization.safe_input',return_value=b'new current control'):
            with self.assertRaises(ContractError):evidence.evaluate_step(s,('a'*64+'  fixed\nS1_GOAL_STEP:'+s.id+':0\n').encode(),b'',0)
        with self.assertRaises((ContractError,KeyError)):delivery_receipt('5c7a9e2130d0aa61',1,[b'a',b'b'],{'A':{'accepted':True}})
    def test_fresh_scalar_or_wrong_context_cannot_admit(self):
        with self.assertRaises(ContractError):validate_fresh({'all_required_pass':True},'a'*64,'b'*64)
        from goal_entry import main
        with patch('builtins.print'):
            self.assertEqual(main([]),78)
        self.assertNotIn('goal_win32_owned',sys.modules)
    def test_real_backend_terminal_and_live_grant_bookkeeping(self):
        from goal_authorization import GoalGrant,_grants
        from goal_windows_backend import GoalWindowsBackend
        from goal_notice import Marker
        server=GoalStep('server','channel','',('-s',contract.ENDPOINT,'-m'),'server')
        step=contract.shell_step('stage.a.mkdir','staging','A','fixed',requires=('server',))
        grant=GoalGrant('a'*64,'b'*64,{server.id:server,step.id:step},time.monotonic()+30);_grants.add(grant)
        class Root:
            case='stage.a.mkdir';dest=Path('E:/synthetic_not_written');output=[b'\nS1_GOAL_STEP:stage.a.mkdir:0\n',b''];closed=False
            receipt=dict(case=case,created=True,bound=True,resumed=True,terminal=True,signal_authority=False,EOF=True,native_exit=0,cleanup_errors=[],stop_attempts=0,terminal_binding_verified=True,hProcess=123,owner={'pid':17,'birth':19,'image':contract.HDC},FileID=[1,2,3],tool_sha256=contract.HDC_SHA,terminal_proof={'method':'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN','hProcess':123,'pid':17,'birth':19,'image':contract.HDC,'FileID':[1,2,3],'required_apis_ok':True,'image_pin_retained_at_check':True,'full_image_was_verified_while_live':True})
            def state(self):return 'TERMINAL'
            def identity(self):pass
            def close(self):self.closed=True
        live=SimpleNamespace(case='server',receipt=dict(case='server',created=True,bound=True,resumed=True,terminal=False),state=lambda:'LIVE')
        backend=object.__new__(GoalWindowsBackend);backend.grant=grant;backend.marker=Marker(cases=('server',step.id));backend.decoded=0
        backend.journal=SimpleNamespace(failed=False)
        backend.w=SimpleNamespace(put=lambda *args:None);backend.server_ok=lambda:True
        try:
            grant.seen.add('server');grant.mark_started('server',live.receipt)
            self.assertFalse(backend.outcome(live,server)['terminal']);self.assertIn('server',grant.accepted)
            permit=grant.reserve_step(step.id);self.assertEqual(permit.step.id,step.id)
            root=Root();result=backend.outcome(root,step)
            self.assertTrue(result['accepted']);self.assertTrue(root.closed);self.assertIn(step.id,grant.accepted)
            live.state=lambda:'TERMINAL';backend.roots=[live]
            with self.assertRaises(ContractError):GoalWindowsBackend.server_ok(backend)
        finally:_grants.discard(grant)
        self.assertNotIn('goal_win32_owned',sys.modules)
    def test_partial_eof_late_cleanup_and_record_error_remain_blocking(self):
        from goal_windows_backend import GoalWindowsBackend
        from goal_notice import Marker
        backend=object.__new__(GoalWindowsBackend);backend.marker=Marker(cases=('server',))
        backend.marker.feed('server','stdout',b'please confirm','MONITOR',1,2);backend.marker.advance()
        with self.assertRaises(ContractError):backend.notice_gate()
        backend.marker=Marker(cases=('server',));backend.marker.feed('server','stdout',b'[E000002]','MONITOR',1,2);backend.marker.eof('server','stdout')
        with self.assertRaises(ContractError):backend.notice_gate()
        backend.marker=Marker(lambda row:False,cases=('server',));backend.marker.feed('server','stdout',b'auth unmatched\n','MONITOR',1,2)
        with self.assertRaises(ContractError):backend.notice_gate()
        class Root:
            case='server';closed=False;reads=[{'name':'stdout','eof':True},{'name':'stderr','eof':True}]
            dest=Path('E:/synthetic_not_written')
            receipt={'native_exit':0,'stop_attempts':0,'EOF':True}
            def state(self):return 'TERMINAL'
            def read(self,cap,observer):observer('stdout',b'[E000002] please authorize\n',1,2)
            def drain(self,deadline,cap,observer):
                self.read(cap,observer);return {'bytes':0,'EOF':True,'status':'EOF'}
            def close(self):self.closed=True
        root=Root();backend.marker=Marker(cases=('server',));backend.journal=SimpleNamespace(case='server');backend.captured={'server':0}
        backend.w=SimpleNamespace(put=lambda *args:None)
        with self.assertRaises(ContractError):backend.cleanup(root,time.monotonic()+2)
        self.assertTrue(root.closed)
        encoded=json.dumps(backend.marker.report())
        self.assertNotIn('please authorize',encoded);self.assertIn('CODE_E000002',encoded)
    def test_close_and_last_record_failures_revoke_success(self):
        from goal_windows_backend import GoalWindowsBackend
        from goal_notice import Marker
        class Root:
            case='stage.a.mkdir';dest=Path('E:/synthetic_not_written');output=[b'\nS1_GOAL_STEP:stage.a.mkdir:0\n',b''];closed=False
            def __init__(self):self.receipt=dict(EOF=True,native_exit=0,cleanup_errors=[],stop_attempts=0)
            def state(self):return 'TERMINAL'
            def identity(self):pass
            def close(self):self.closed=True;self.receipt['cleanup_errors'].append('CONTROLLED_CLOSE_FAILURE')
        accepted=[];persisted=[];backend=object.__new__(GoalWindowsBackend);backend.marker=Marker(cases=(Root.case,));backend.decoded=0;backend.journal=SimpleNamespace(failed=False)
        backend.grant=SimpleNamespace(mark_terminal=lambda *a:None,accept=lambda *a:accepted.append(a));backend.w=SimpleNamespace(put=lambda p,r:persisted.append(dict(r)))
        step=contract.shell_step(Root.case,'staging','A','fixed');root=Root();result=backend.outcome(root,step)
        self.assertFalse(result['accepted']);self.assertFalse(accepted);self.assertFalse(persisted[0]['accepted_evidence'])
        self.assertEqual(backend.cleanup(root,time.monotonic()+2)['state'],'UNKNOWN')
        events=[]
        class Journal:
            failed=False
            def emit(self,row):events.append('emit')
            def close(self):events.append('journal-close')
        backend.marker=Marker(cases=('server',));backend.journal=Journal();backend.keys_before=[];backend.pins=[123]
        def failed_close(handle):
            self.assertNotIn('journal-close',events);backend.journal.failed=True;events.append('pin-close');return True
        backend.w=SimpleNamespace(key_metadata=lambda:[],K=SimpleNamespace(CloseHandle=failed_close),_journal=backend.journal)
        with self.assertRaises(ContractError):backend.complete_cleanup()
        self.assertEqual(events[-1],'journal-close')
    def test_provider_mismatch_stops_before_native(self):
        def body(hash_value):return ('Linux fixture\n/system/lib/ld-musl-aarch64.so.1\n/system/lib/ld-musl-aarch64.so.1\n1|2|3|755\n1|2|3|755\n'+hash_value+'  /lib/ld-musl-aarch64.so.1\n'+hash_value+'  /system/lib/ld-musl-aarch64.so.1\n__RO_RC=0\n').encode()
        self.assertEqual(evidence.parse_client('RO-A-provider',body(evidence.INTERPRETER_SHA),b'',0)['remote_RC'],0)
        with self.assertRaises(ContractError):evidence.parse_client('RO-A-provider',body('0'*64),b'',0)
    def test_board_resource_values_and_minimum_native_admission(self):
        def body(total,available,disk):return ('MemTotal: '+str(total)+' kB\nMemAvailable: '+str(available)+' kB\nFilesystem 1K-blocks Used Available Use% Mounted on\nfixture 1000000 1 '+str(disk)+' 1% /data\n/data/local/tmp\ndirectory|755|0|0\ndirectory|755|0|0\ndirectory|755|0|0\n__RO_BASE_RC=0\n__RO_ROOT_STAT_RC=1\n').encode()
        err=('stat: '+contract.DEVICE_ROOT+': No such file or directory\n').encode()
        accepted=evidence.parse_client('RO-A-resource-path',body(4194304,1048576,65536),err,0)
        self.assertEqual(accepted['MemAvailable_KiB'],1048576);self.assertEqual(accepted['data_available_KiB'],65536)
        for values in [(4194303,1048576,65536),(4194304,1048575,65536),(4194304,1048576,65535)]:
            with self.assertRaises(ContractError):evidence.parse_client('RO-A-resource-path',body(*values),err,0)
    def test_sealed_app_file_size_fits_finite_native_limit(self):
        from goal_authorization import validate_app_fsize
        validate_app_fsize({'payloads':[{'target':'own_goal_runner','bytes':302104}]})
        validate_app_fsize({'payloads':[{'target':'own_goal_runner','bytes':contract.LIMITS['native_fsize']}]})
        for size in [0,contract.LIMITS['native_fsize']+1]:
            with self.assertRaises(ContractError):validate_app_fsize({'payloads':[{'target':'own_goal_runner','bytes':size}]})

if __name__=='__main__':unittest.main(verbosity=2)
