"""Current DDS composition, synthetic inputs only; no live API or old output."""
import ast
import contextlib
import copy
import ctypes
import importlib.util
import json
import re
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('dds_builder_tests',ROOT/'scripts/hdc_goal/prepare_directdds.py')
b = importlib.util.module_from_spec(spec);spec.loader.exec_module(b)


@contextlib.contextmanager
def runtime():
    buffers, inputs, manifest = b.source_buffers()
    names = [name[:-3] for name in b.SOURCES]
    saved = {name:sys.modules.get(name) for name in names}
    modules = {}
    order = ('goal_contract','goal_policy','goal_handle_lifecycle','goal_records','resource_receipt','goal_evidence',
             'goal_authorization','goal_driver','goal_notice','dds_observation','dds_notice','goal_windows_backend',
             'goal_storage','goal_workflow','goal_entry')
    try:
        with patch.object(ctypes,'WinDLL',side_effect=AssertionError('LIVE_DLL_FORBIDDEN'),create=True):
            for name in order:
                module = types.ModuleType(name);module.__file__ = str(b.ACTIVE/(name+'.py'))
                sys.modules[name] = module
                exec(compile(buffers[name+'.py'],module.__file__,'exec'),module.__dict__)
                modules[name] = module
            yield types.SimpleNamespace(**modules,inputs=inputs,buffers=buffers,manifest=manifest)
    finally:
        for name, module in saved.items():
            if module is None:sys.modules.pop(name,None)
            else:sys.modules[name] = module


def event(**overrides):
    row = dict(schema='NOTICE_EVENT_V2',case='server',stream='stdout',phase='MONITOR',
               event_type='UNKNOWN',rule_id='AUTH_CONTEXT_UNKNOWN',trigger_family='AUTH_WORD_ONLY',complete_record=True)
    row.update(overrides);return row


def marker(events=(),classification=None,**kw):
    rows = list(events)
    report = dict(schema='NOTICE_EVIDENCE_V2',record_failed=False,overflow=False,
                  failure_rules=[],emergency_events=[],events=rows,
                  classification=classification or ('UNKNOWN' if rows else 'NONE'))
    report.update(kw)
    return types.SimpleNamespace(report=lambda:copy.deepcopy(report))


class CompositionTests(unittest.TestCase):
    def test_registry_keeps_complete_dds_and_new_roots(self):
        with runtime() as m:
            c = m.goal_contract;r = c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            self.assertEqual(len(r),123)
            self.assertEqual(sum(s.kind=='long' for s in r.values()),4)
            self.assertEqual(sum('.cancel.' in s.id for s in r.values()),6)
            self.assertEqual(c.LIMITS['work_ms'],236000)
            self.assertEqual(c.LIMITS['total_ms'],240000)
            self.assertEqual(c.DEVICE_ROOT,b.DEVICE)
            for side in ('a','b'):
                self.assertIn('own.'+side,r)
                self.assertIn('data.'+side+'.result',r)
                for n in range(1,5):self.assertIn('data.'+side+'.release.'+side+'.'+str(n)+'.publish',r)
            self.assertEqual(r['controller'].kind,'controller')
            self.assertEqual(m.goal_authorization.SOURCE_NAMES,b.SOURCES)

    def test_payload_sources_and_native_oracles_are_exact_reuse(self):
        with runtime() as m:
            base=b.BASE/'active'
            for name in ('goal_records.py','goal_evidence.py','goal_win32_owned.py'):
                old=b.h.read_safe(base/name).replace(b'9a72c6e431d0aa60',b.PROBE_RUN.encode()).replace(b'9a72c6e431d0aa61',b.DATA_RUN.encode())
                if name=='goal_win32_owned.py':
                    # Only the observation adapter changes; creation, permits,
                    # hash/pin association and resume/stop remain exact ASTs.
                    def classes(data):
                        return {n.name:{f.name:ast.dump(f,include_attributes=False)
                                for f in n.body if isinstance(f,ast.FunctionDef)}
                                for n in ast.parse(data).body if isinstance(n,ast.ClassDef)}
                    before,after=classes(old),classes(m.buffers[name])
                    self.assertEqual(set(before),set(after))
                    for cls,methods in before.items():
                        self.assertEqual(set(after[cls])-set(methods),{'wait_terminal'} if cls=='IdentityOps' else set())
                        for method in methods:
                            if (cls,method)!=('OwnedRoot','identity'):
                                self.assertEqual(methods[method],after[cls][method])
                    self.assertIn(b'observe_owned_binding(self,IdentityOps()',m.buffers[name])
                elif name!='goal_evidence.py':self.assertEqual(m.buffers[name],old)
                else:
                    # Only the receipt framing/resource adapter changes here;
                    # full DDS/restore/transfer evaluators remain exact AST reuse.
                    def definitions(data):return {x.name:ast.dump(x,include_attributes=False) for x in ast.parse(data).body if isinstance(x,ast.FunctionDef)}
                    before,after=definitions(old),definitions(m.buffers[name])
                    self.assertEqual(set(before),set(after))
                    for symbol in before:
                        if symbol not in ('parse_client','step_stdout'):self.assertEqual(before[symbol],after[symbol])
            self.assertEqual(m.buffers['goal_handle_lifecycle.py'],b.h.read_safe(b.HERE/'goal_handle_lifecycle.py'))
            expected=json.loads(b.h.read_safe(base/'GOAL_INPUTS_D01.json'))
            for row in expected['readonly_commands']:
                row['argv']=[arg.replace('/data/local/tmp/mdds_direct_20261031_9a72c6e4',b.DEVICE) for arg in row['argv']]
                if row['argv'][-1].startswith(b.resource_query.MEMINFO_AWK):
                    row['argv'][-1]=b.resource_query.replace_meminfo_query(row['argv'][-1])
            self.assertEqual(m.inputs,expected)
            self.assertEqual(len(b.verify_external(m.manifest)),14)
            for row in m.inputs['readonly_commands']:
                self.assertNotIn('9a72c6e4',str(row))

    def test_result_is_published_only_after_successful_fsync(self):
        parent=(ROOT/'build/directdds-composition-20261103-d01').resolve()
        self.assertTrue(parent.is_relative_to(ROOT.resolve()));parent.mkdir(exist_ok=True)
        with runtime() as m,tempfile.TemporaryDirectory(prefix='publication-',dir=parent) as td:
            root=Path(td);self.assertTrue(root.resolve().is_relative_to(parent))
            failed=root/'failed';failed.mkdir();target=failed/'RESULT.json'
            with patch.object(m.goal_entry.os,'fsync',side_effect=OSError('SYNTHETIC_FSYNC_FAILURE')):
                with self.assertRaises(OSError):m.goal_entry.publish_result(target,{'status':'PASS'})
            self.assertFalse(target.exists());self.assertTrue((failed/'RESULT.pending.json').is_file())
            succeeded=root/'normal';succeeded.mkdir();target=succeeded/'RESULT.json'
            m.goal_entry.publish_result(target,{'status':'SYNTHETIC'})
            self.assertEqual(json.loads(target.read_text(encoding='utf-8')),{'status':'SYNTHETIC'})
            self.assertFalse((succeeded/'RESULT.pending.json').exists())
            with self.assertRaises(m.goal_contract.ContractError):m.goal_entry.publish_result(target,{'status':'REPLAY'})

    def test_backend_preserves_dds_budget_and_no_missing_probe_fields(self):
        with runtime() as m:
            tree=ast.parse(m.buffers['goal_windows_backend.py'])
            keys={n.slice.value for n in ast.walk(tree) if isinstance(n,ast.Subscript)
                  and isinstance(n.value,ast.Name) and n.value.id=='LIMITS'
                  and isinstance(n.slice,ast.Constant)}
            self.assertFalse(keys-set(m.goal_contract.LIMITS))
            self.assertNotIn(b'active_inventory',m.buffers['goal_windows_backend.py'])
            self.assertIn(b'max_total=12*1048576',m.buffers['goal_windows_backend.py'])
            self.assertIn(b'110000',m.buffers['goal_windows_backend.py'])

    def test_backend_real_pre_resume_counter_path(self):
        with runtime() as m:
            backend=object.__new__(m.goal_windows_backend.GoalWindowsBackend)
            backend.samples=0;backend.epochs=0;backend.latest={}
            sample={'jobs':{'caller_visible_layer':{}},'process':{'private_bytes':1},'system':{}}
            calls=[];backend.trace=types.SimpleNamespace(observe=lambda h,p:(calls.append((h,p)) or sample))
            backend.w=types.SimpleNamespace(fingerprint=lambda _: 'synthetic',ObservationError=RuntimeError)
            backend.journal=types.SimpleNamespace(case=None,resource=False,emit=lambda _:None)
            root=types.SimpleNamespace(case='server',pi=types.SimpleNamespace(hProcess=7),receipt={},identity=lambda **_: 'LIVE')
            self.assertEqual(backend.observe(root,'PRE_RESUME'),sample)
            self.assertEqual((backend.samples,backend.epochs,len(calls)),(1,1,1))

    def test_generated_resource_and_supervision_use_supported_receipt_framing(self):
        with runtime() as m:
            c=m.goal_contract;e=m.goal_evidence
            rows=['MemTotal: 8000000 kB','MemAvailable: 4000000 kB',
                  'Filesystem 1K-blocks Used Available Use% Mounted on','/dev/test 1000000 1000 999000 1% /data',
                  '/data/local/tmp','directory|755|1|1','directory|755|1|1','directory|755|1|1',
                  '__RO_BASE_RC=0','stat: '+c.DEVICE_ROOT+': No such file or directory','__RO_ROOT_STAT_RC=1']
            for ending in (b'\n',b'\r\n',b'\r\r\n'):
                for case in ('RO-A-resource-path','RO-B-resource-path'):
                    data=ending.join(row.encode() for row in rows)+ending
                    self.assertEqual(e.parse_client(case,data,b'',0)['root_absence'],'ENOENT_WITH_BASE_ACCESS')
                registry=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
                step=registry['stage.a.mkdir']
                self.assertIn('S1_GOAL_STEP:'+step.id+':0',e.step_stdout(step,('S1_GOAL_STEP:'+step.id+':0').encode()+ending))
                line=b'DIRECT_SUP_BEGIN parent=50 child=101 birth=700 scope=DIRECT_CHILD_ONLY descendants=UNKNOWN'+ending
                root=types.SimpleNamespace(output=[line,b''])
                driver=types.SimpleNamespace(live={'probe.a.launch':root},pump=lambda:None)
                workflow=m.goal_workflow.FixedGoalWorkflow(driver,{},'a'*64)
                self.assertEqual(workflow.created('probe','A')['created_child'],101)

    def test_narrow_unknown_requires_new_effect(self):
        with runtime() as m:
            p=m.dds_observation
            self.assertTrue(p.check_notice(marker([event()]),{'server':1},True,m.goal_contract.need))
            with self.assertRaises(m.goal_contract.ContractError):p.check_notice(marker([event()]),{'server':1},False,m.goal_contract.need)

    def test_every_request_denial_client_or_malformed_event_stops(self):
        cases=[dict(case='data.a.launch'),dict(stream='stderr'),dict(phase='PRE_RESUME'),
               dict(complete_record=False),dict(trigger_family='AUTH_WORD_AND_ECODE'),
               dict(event_type='REQUEST_TEXT',rule_id='CODE_E000002'),
               dict(event_type='DENIED_TEXT',rule_id='CODE_E000003'),
               dict(event_type='COMMAND_FAILURE',rule_id='CODE_E000004_CHANNEL_ESTABLISHING')]
        with runtime() as m:
            for overrides in cases:
                with self.subTest(overrides=overrides),self.assertRaises(m.goal_contract.ContractError):
                    m.dds_observation.check_notice(marker([event(),event(**overrides)]),{'server':1,'data.a.launch':1},True,m.goal_contract.need)
            for kw in ({'record_failed':True},{'overflow':True},{'failure_rules':['X']},{'emergency_events':[{}]}):
                with self.subTest(kw=kw),self.assertRaises(m.goal_contract.ContractError):
                    m.dds_observation.check_notice(marker([event()],**kw),{'server':1},True,m.goal_contract.need)

    def test_retained_unknown_never_unconditional_pass(self):
        with runtime() as m:
            proc={'all_four_windows_and_controller_accepted':True,'Native_results':{k:{} for k in ('probe.A','probe.B','data.A','data.B')}}
            cleanup={'failure':'CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN','finalization':{'state':'COMPLETE','cleanup_errors':[]},
                     'cleanup':[{'step':'server','state':'TERMINAL','EOF':True,'terminal_binding_verified':True,'cleanup_errors':[]}]}
            q=m.dds_observation.qualify(proc,cleanup,marker([event()]),{'server':1},True,m.goal_contract.need)
            self.assertTrue(q['DDS_data_observed']);self.assertFalse(q['overall_unconditional_pass'])
            self.assertEqual(q['authentication_semantics'],'UNKNOWN_NOT_ATTESTED')
            cleanup['finalization']['state']='UNKNOWN'
            self.assertFalse(m.dds_observation.qualify(proc,cleanup,marker([event()]),{'server':1},True,m.goal_contract.need)['DDS_data_observed'])

    def test_authorization_shape_rejects_old_broad_effect(self):
        with runtime() as m:
            c=m.goal_contract;a=m.goal_authorization
            effects={'hostkey','automatic_USB_auth_state','existing_peer_Socket_auth','endpoint_write_misdelivery_residual',
                     'opaque_external_Job_residual','entry_select_and_restore','Native_project_execution',
                     'server_stdout_auth_word_only_unknown_observation'}
            approval={'scope':c.SCOPE,'nonce':c.NONCE,'manifest_sha256':'a'*64,'binding_sha256':'b'*64,
                      'source':{'kind':'trusted_direct_user_reply','reply_id':'synthetic','request_id':'synthetic',
                                'reply_to_request_verified':True,'time_precision':'minute','sent_time_original':'synthetic'},
                      'operator_verified_context':True,'fixture':False,'effects':{x:True for x in effects}}
            self.assertTrue(a.validate_approval_shape(approval,'a'*64,'b'*64))
            approval['effects']['server_auth_context_unknown_continue']=True
            with self.assertRaises(c.ContractError):a.validate_approval_shape(approval,'a'*64,'b'*64)

    def test_assemble_cannot_reuse_existing_package_or_refund_a_consumed_run(self):
        # Always target the old consumed snapshot, even when the builder moves
        # to a fresh candidate. Never assemble a real package from a unit test.
        old=ROOT/'build/directdds-observation-package-20261103-d01'
        run=Path('E:/Codex-FastDDS-directdds-once-20261103_4f81c2a9')
        paths=[run,*[old/'active'/name for name in b.CONTROLS]]
        before={str(p):(p.exists(),p.stat().st_mtime_ns if p.exists() else None) for p in paths}
        with patch.object(b,'PACKAGE',old),patch.object(b,'RUN',run):
            with self.assertRaises(ValueError):b.assemble()
        self.assertEqual(before,{str(p):(p.exists(),p.stat().st_mtime_ns if p.exists() else None) for p in paths})

    def test_full_workflow_real_driver_backend_and_record_oracles(self):
        # Only the OS/remote producers are synthetic. Execute the actual composed
        # workflow, scheduler, PRE_RESUME/outcome/cleanup and immutable parsers.
        # No production-result mock, fabricated PASS shortcut or live API import.
        temporary_parent=(ROOT/'build/directdds-composition-20261103-d01').resolve()
        self.assertTrue(temporary_parent.is_relative_to(ROOT.resolve()))
        temporary_parent.mkdir(exist_ok=True)
        with runtime() as m,tempfile.TemporaryDirectory(prefix='synthetic-',dir=temporary_parent) as td:
            path=Path(td)
            self.assertTrue(path.resolve().is_relative_to(temporary_parent))
            for name in ('goal_contract','goal_workflow','goal_evidence','goal_windows_backend'):
                setattr(getattr(m,name),'RUN_ROOT',path)
            c=m.goal_contract
            class Clock:
                def __init__(self):self.value=__import__('time').monotonic()
                def __call__(self):return self.value
            clock=Clock()
            registry=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            artifacts=m.inputs['artifacts'];emitter=m.goal_evidence
            def record(row):return ''.join(k+'='+str(v)+'\n' for k,v in row.items())
            def boot(side):
                return dict(format='S1GOALBOOT1',side=side,pid=101 if side=='A' else 102,parent=50,birth=700,
                            caller=8,uid=2000,euid=2000,gid=2000,egid=2000,wall=12345,mono=500,
                            image=c.DEVICE_ROOT+'/own_goal_launcher',image_sha=artifacts['launcher']['sha256'])
            def supervised(final=False,side='A'):
                pid=101 if side=='A' else 102
                if not final:return f'DIRECT_SUP_CREATED parent=50 child={pid} generation=UNVERIFIED no_signal_authority=1\nDIRECT_SUP_BEGIN parent=50 child={pid} birth=700 scope=DIRECT_CHILD_ONLY descendants=UNKNOWN\n'
                return f'DIRECT_SUP_RESULT child={pid} phase=3 status_valid=1 raw_status=0 signal_sent=0 timeout=0 cancelled=0 late=0 root_reaped=1 descendants=UNKNOWN no_group_signal=1 peak_rss_kib=100 errno=0\nS1_GOAL_REMOTE_RC:0\n'
            def output(step):
                side=step.side;identifier=step.id
                if step.kind in ('send','receive','controller'):return b'',b''
                if identifier=='targets':return (''.join(t+' USB Connected SYNTHETIC\n' for t in c.TARGETS.values()).encode(),b'')
                if identifier.startswith('readonly.'):
                    if identifier in ('readonly.1','readonly.3'):
                        text='Linux SYNTHETIC\n/lib/ld-musl-aarch64.so.1\n/system/lib/ld-musl-aarch64.so.1\n1|2|3|755\n1|2|3|755\n'
                        text+=''.join(emitter.INTERPRETER_SHA+' '+p+'\n' for p in ('/lib/ld-musl-aarch64.so.1','/system/lib/ld-musl-aarch64.so.1'))+'__RO_RC=0\n'
                        return text.encode(),b''
                    text='MemTotal: 8000000 kB\nMemAvailable: 4000000 kB\nFilesystem 1K-blocks Used Available Use% Mounted on\n/dev/test 1000000 1000 999000 1% /data\n/data/local/tmp\ndirectory|755|1|1\ndirectory|755|1|1\ndirectory|755|1|1\n__RO_BASE_RC=0\n__RO_ROOT_STAT_RC=1\n'
                    return text.encode(),('stat: '+c.DEVICE_ROOT+': No such file or directory\n').encode()
                body=''
                if step.byte_source:
                    remote=re.fullmatch(r"\( sha256sum '([^'\r\n]+)' \); rc=\$\?; printf .*",step.args[-1])[1]
                    body=c.sha(Path(step.byte_source).read_bytes())+'  '+remote+'\n'
                elif identifier.startswith('own.'):
                    body=supervised(side=side)+supervised(True,side)
                    body+='OWN_QUERY name='+c.OWN+' bridge_cpp=__h raw_rc=0 info_rc=0 query_rc=0 type=1 native_type=1 apl=1 normal_apl=1 query_count=1 granted=1 called=31 caller_before=8 caller_bridge=8 caller_after=8 no_identity_setter=1 lookup=9\n'
                elif identifier.endswith('.boot'):body=record(boot(side))
                elif identifier.endswith('.node'):
                    body=record(dict(format='S1GOALNODE1',run=int(b.DATA_RUN,16),side=side,pid=101 if side=='A' else 102,parent=50,target=9,network='SYNTHETIC_'+side,udid='SYNTHETIC_'+side,image_sha=artifacts['app']['sha256']))
                elif identifier.endswith('.result'):
                    window=identifier.split('.')[0];run=int(b.PROBE_RUN if window=='probe' else b.DATA_RUN,16)
                    pid=101 if side=='A' else 102
                    if window=='probe':body=record(dict(format='S1GOALPROBE1',run=run,pid=pid,entry_selected=1,one_owned_worker_selected=1,all_SDK_workers='UNKNOWN',project_business_calls=0))
                    else:body=record(dict(format='S1GOALRESULT1',status=0,result_status=0,phase=4,writes=3,takes=3,local_control_drained=1,owners_retained=0,SDK_quiescence_unproven=1,destroy_rc=0,primary_phase=0,native_rc_valid=1,native_rc=0))
                    body+=record(dict(format='S1GOALEXIT1',run=run,pid=pid,status=0,restored_entry=1,restore_rc_valid=1,restore_rc=0,worker_parent_restoration='UNKNOWN',SDK_quiet='UNKNOWN',phase=4,native_rc_valid=1,native_rc=0))
                return (body+'\nS1_GOAL_STEP:'+identifier+':0\n').encode(),b''

            class FakeRoot:
                def __init__(self,step):
                    self.case=step.id;self.step=step;self.dest=path/'cases'/step.id;self.dest.mkdir(parents=True)
                    self.closed=False;self.output=[bytearray(),bytearray()];self.owner={'pid':len(backend.roots)+10,'birth':100,'image':step.executable}
                    self.pi=types.SimpleNamespace(hProcess=self.owner['pid']);self.reads=[{'name':s,'eof':False} for s in ('stdout','stderr')]
                    self.receipt={'case':step.id,'created':False,'bound':True,'resumed':False,'terminal':False,'EOF':False,
                                  'native_exit':None,'stop_attempts':0,'cleanup_errors':[],'tool_sha256':step.executable_sha256,
                                  'hProcess':self.pi.hProcess,'owner':self.owner,'FileID':[1,self.pi.hProcess,3],
                                  'signal_authority':False}
                    self.initial=False
                def create(self):self.receipt.update(created=True,signal_authority=True)
                def resume(self):self.receipt['resumed']=True
                def state(self):return 'TERMINAL' if self.receipt['terminal'] else 'LIVE'
                def identity(self,require_live=False):
                    if require_live:c.need(self.state()=='LIVE','FAKE_LIVE')
                    if self.state()=='TERMINAL':
                        self.receipt['signal_authority']=False
                        self.receipt['terminal_binding_verified']=True
                        self.receipt['terminal_proof']={'hProcess':self.pi.hProcess,'pid':self.owner['pid'],'birth':100,
                            'image':self.owner['image'],'FileID':self.receipt['FileID'],
                            'method':'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN','required_apis_ok':True,
                            'image_pin_retained_at_check':True,'full_image_was_verified_while_live':True}
                    return self.state()
                def read(self,cap,observer):
                    if self.state()=='TERMINAL' or self.case=='server':return []
                    chunks=[]
                    def emit(data,index=0):
                        if data:
                            stream=('stdout','stderr')[index];self.output[index].extend(data);observer(stream,data,1,2);chunks.append((stream,data))
                    if self.step.kind=='long':
                        if not self.initial:emit(supervised(side=self.step.side).encode());self.initial=True
                        end=self.case.startswith('probe.') or (path/'control/data/delivered.4').exists()
                        if not end:return chunks
                        # The probe must wait until its admission has been published.
                        if self.case.startswith('probe.') and ('probe.'+self.step.side.lower()+'.admission.'+self.step.side.lower()+'.publish') not in driver.accepted:return chunks
                        emit((supervised(True,self.step.side)+'\nS1_GOAL_STEP:'+self.case+':0\n').encode())
                    elif self.step.kind=='controller':
                        if not (path/'control/data/delivered.4').exists():return []
                    else:
                        a,z=output(self.step);emit(a);emit(z,1)
                    self.receipt.update(terminal=True,native_exit=0,EOF=True,signal_authority=False)
                    import time
                    self.terminal_at=time.monotonic()
                    for row in self.reads:row['eof']=True
                    self.identity();return chunks
                def stop(self,deadline):
                    c.need(self.receipt['stop_attempts']==0,'FAKE_STOP_REPLAY')
                    self.receipt.update(terminal=True,native_exit=125,stop_attempts=1)
                    self.identity()
                def drain(self,deadline,cap,observer):
                    self.receipt['EOF']=True
                    for row in self.reads:row['eof']=True
                    self.receipt['drain']={'status':'EOF','bytes':0};return self.receipt['drain']
                def close(self):c.need(not self.closed,'FAKE_CLOSE_REPLAY');self.closed=True

            class Backend(m.goal_windows_backend.GoalWindowsBackend):
                evidence_kind='SYNTHETIC_OS_AND_REMOTE_NOT_DEVICE'
                def __init__(self):
                    self.roots=[];self.latest={};self.baseline=None;self.stopped=False;self.stream_bytes={};self.capture_failed=False
                    self.epochs=0;self.samples=0;self.last_sample=0;self.captured={};self.cache={};self.raw_notice=[];self.pins=[];self.decoded=0
                    self.grant=types.SimpleNamespace(deadline=__import__('time').monotonic()+240,registry=registry,
                        scoped_server_keyword_observation=True,halt=lambda:None,mark_started=lambda *a:None,
                        mark_terminal=lambda *a:None,mark_server_ready=lambda *a:None,accept=lambda *a:None)
                    self.journal=types.SimpleNamespace(case=None,resource=False,failed=False,emit=lambda _:None,rejection=lambda _:None,close=lambda:None)
                    self.w=types.SimpleNamespace(put=lambda *a:None,ObservationError=RuntimeError,fingerprint=lambda _: 'synthetic',
                        snapshot=lambda _:[],key_metadata=lambda:[],K=types.SimpleNamespace(CloseHandle=lambda _:True))
                    self.trace=types.SimpleNamespace(observe=lambda *a:{'jobs':{'caller_visible_layer':{}},'process':{'private_bytes':1,'working_set_bytes':1},
                        'system':{'commit_available_bytes':20*1024**3,'physical_available_bytes':10*1024**3,'commit_total_bytes':1}})
                    self.marker=m.dds_notice.DDSMarker(self.notice_event,registry=registry,cases=tuple(registry),max_total=12*1048576);self.keys_before=[]
                def server_ok(self):return True
                def precreate(self,step,deadline):self.notice_gate()
                def storage(self):return None
                def pause(self,seconds):clock.value+=seconds
                def register(self,step):
                    if step.id=='targets':self.target_started=clock()
                    root=FakeRoot(step);self.roots.append(root);self.captured[step.id]=0
                    if step.kind=='receive':
                        target=Path(step.args[-1]);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(b'SYNTHETIC_EVENTS\n')
                    if step.kind=='controller':
                        directory=path/'control/data'
                        for side in ('A','B'):
                            for seq in range(1,5):(directory/('release.'+side+'.'+str(seq))).write_bytes(('SYNTHETIC_RELEASE_'+str(seq)+'_'+side).encode())
                        (directory/'controller-result').write_bytes(('S1CONTROLLER1\nfixture=0\nrun='+b.DATA_RUN+'\nROOT_MATCH_GUID_SN_PAYLOAD_ACK_DONE_TEARDOWN=1\nNative_attestation=0\nentry_restore_and_exit_checked_separately=1\n').encode())
                    return root
            backend=Backend();driver=m.goal_driver.GoalDriver(registry,backend,clock=clock)
            procedure=m.goal_workflow.FixedGoalWorkflow(driver,artifacts,'a'*64).run()
            # Add retained telemetry only after the actual four-window ledger has
            # completed. Cleanup must preserve UNKNOWN while closing own roots.
            backend.marker.feed('server','stdout',b'auth: SYNTHETIC_UNCLASSIFIED\n','MONITOR',1,2)
            cleanup=driver.finish()
            self.assertEqual(cleanup['failure'],'CLEANUP_NOTICE_BLOCKED_OR_UNKNOWN')
            qualified=m.dds_observation.qualify(procedure,cleanup,backend.marker,registry,True,c.need)
            self.assertTrue(qualified['DDS_data_observed'])
            self.assertFalse(qualified['overall_unconditional_pass'])
            self.assertEqual(set(procedure['Native_results']),{'probe.A','probe.B','data.A','data.B'})
            self.assertGreaterEqual(backend.target_started-procedure['prequery_observation_window']['start_monotonic'],10)
            self.assertFalse(procedure['prequery_observation_window']['handshake_proved'])
            # Early controller completion leaves only unused finite polling
            # opportunities and cancellation commands; it must omit no action
            # from either admission, release, result or restore path.
            self.assertEqual(set(registry)-driver.used,
                {identifier for identifier in registry if '.cancel.' in identifier}
                | {'poll.'+str(n)+'.'+side for n in range(5,12) for side in ('a','b')})
            self.assertEqual(len(driver.used),103)
            self.assertTrue(all(root.closed for root in backend.roots))

    def test_observation_window_preserves_hard_stops_without_queries(self):
        with runtime() as m:
            class Driver:
                def __init__(self,stop=False):
                    self.now=0;self.pumps=0;self.stop=stop
                    self.backend=types.SimpleNamespace(pause=lambda s:setattr(self,'now',self.now+s))
                def clock(self):return self.now
                def pump(self):
                    self.pumps+=1
                    if self.stop:raise m.goal_contract.ContractError('SYNTHETIC_HARD_STOP')
            driver=Driver();workflow=m.goal_workflow.FixedGoalWorkflow(driver,{},'a'*64)
            result=workflow.observe_fixed_window()
            self.assertGreaterEqual(result['end_monotonic'],10);self.assertLess(result['end_monotonic'],10.051)
            driver=Driver(True);workflow=m.goal_workflow.FixedGoalWorkflow(driver,{},'a'*64)
            with self.assertRaises(m.goal_contract.ContractError):workflow.observe_fixed_window()
            self.assertEqual(driver.pumps,1);self.assertEqual(driver.now,0)

    def test_exact_public_record_grammar_does_not_exempt_variants(self):
        with runtime() as m:
            c=m.goal_contract;r=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            native=b'DIRECT_SUP_CREATED parent=50 child=101 generation=UNVERIFIED no_signal_authority=1'
            transfer=r['data.a.pair.verify'];remote=re.fullmatch(r"\( sha256sum '([^'\r\n]+)' \); rc=\$\?; printf .*",transfer.args[-1])[1]
            cases=[('own.a',native,'DIAG_NATIVE_CREATED'),
                   (transfer.id,('a'*64+'  '+remote).encode(),'DIAG_FIXED_TRANSFER_HASH'),
                   ('data.a.pair.publish',b'S1_GOAL_STEP:data.a.pair.publish:0','DIAG_FIXED_STEP_STATUS')]
            for case,data,rule in cases:
                for ending in (b'\n',b'\r\n',b'\r\r\n'):
                    mark=m.dds_notice.DDSMarker(registry=r,cases=tuple(r));mark.feed(case,'stdout',data+ending,'MONITOR',1,2)
                    self.assertEqual(mark.report()['classification'],'DIAGNOSTIC');self.assertEqual(mark.report()['events'][0]['rule_id'],rule)
                for altered in (data+b' extra',data+b' [E000003]',data+b'\r\r\r'):
                    mark=m.dds_notice.DDSMarker(registry=r,cases=tuple(r));mark.feed(case,'stdout',altered+b'\n','MONITOR',1,2)
                    self.assertIn(mark.report()['classification'],('UNKNOWN','DENIED_TEXT'))
            mark=m.dds_notice.DDSMarker(registry=r,cases=tuple(r));mark.feed('own.a','stdout',native,'MONITOR',1,2);mark.eof('own.a','stdout')
            self.assertEqual(mark.report()['classification'],'UNKNOWN')


if __name__=='__main__':unittest.main(verbosity=2)
