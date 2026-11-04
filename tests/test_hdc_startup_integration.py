"""Synthetic inline frames/ownership adapters and offline composition only."""
import ast,contextlib,ctypes,importlib.util,io,json,subprocess,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/hdc_goal'))
import readonly_evidence as r
import dds_startup_evidence as s
spec=importlib.util.spec_from_file_location('startup_builder',ROOT/'scripts/hdc_goal/prepare_startup_directdds.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
EXPECTED=dict(pid=1234,parent=77,birth=9876,uid=2000,euid=2000,gid=2000,egid=2000,run=99)
CAUSE=b'format=S1GOALCAUSE1\navailable=1\nentered_stage=12\nfirst_reason=7\nfailure_stage=12\ngate_entered_stage=4\ngate_reason=3\ngate_failure_stage=4\n'

def item(label,data):
    wire=data+b'\x00MDDS_READ_RC=0\x00'
    n=r.CAPS[label] if label in ('P0','status','P1') else len(data)
    return ('S1RO_ITEM '+label+' DATA '+str(n)+'\n').encode()+b''.join(
      (' '+' '.join('%02x'%v for v in wire[i:i+16])+'\n').encode() for i in range(0,len(wire),16))+('S1RO_END '+label+' OK\n').encode()
def envelope(side='A',data=CAUSE,missing=False,process=False):
    out=('S1RO_BEGIN '+side+'\n').encode()
    if process:
        out+=b''.join(('S1RO_ITEM '+x+' NOT_VISIBLE 0\n').encode() for x in ('P0','status','P1'))
    else:out+=b'S1RO_ITEM cause MISSING 0\n' if missing else item('cause',data)
    return out+('S1RO_DONE '+side+' 0\n').encode()
def framed(side='A',mode='cause',**kw):
    return ('S1DIAG_BEGIN '+side+' '+mode+'\n').encode()+envelope(side,process=mode=='process',**kw)+('S1DIAG_END '+side+' OK\n').encode()
class Root:
    def __init__(self,case='data.a.launch'):
        self.case=case;self.output=[bytearray(),bytearray()];self.captures=[io.BytesIO(),io.BytesIO()];self.receipt={'EOF':True}
    def state(self):return 'TERMINAL'
def deliver(adapter,root,data):
    adapter.feed(data);root.captures[0].write(data);root.captures[0].flush();root.output[0].extend(data)

class ProjectionTests(unittest.TestCase):
    def test_new_mode_does_not_expand_legacy_shared_caps(self):
        self.assertNotIn('cause',r.CAPS);self.assertEqual(sum(r.CAPS.values())+4096,24576)
        self.assertEqual(r.Projection('A',{},mode='cause').caps,{'cause':512})
    def test_cause_is_bounded_and_independent(self):
        p=r.Projection('A',{},mode='cause')
        p.feed(envelope());out=p.finish()
        self.assertEqual(out['files']['cause']['first_reason'],'PARTICIPANT_NULL')
        self.assertFalse(out['files']['cause']['success_inferred']);self.assertEqual(out['SDK_quiet'],'UNKNOWN')
    def test_512_remote_and_local_bound(self):
        q=r.query('A',{},mode='cause',root=b.parent.DEVICE+'/data')
        self.assertIn('read_record cause "$root/cause.A" 512',q)
        self.assertIn('dd bs=1 count="$size"',q);self.assertIn('"$size" -le "$limit"',q)
        with self.assertRaises(r.EvidenceUnknown):r.Projection('A',{},mode='cause').feed(envelope(data=b'x'*513))
        with self.assertRaises(r.EvidenceUnknown):r.typed('cause',b'x'*513,{})
    def test_unknown_content_never_exports_private_text(self):
        p=r.Projection('B',{},mode='cause');p.feed(envelope('B',b'SYNTHETIC_PRIVATE_NAME=hidden\n'))
        value=json.dumps(p.finish());self.assertNotIn('PRIVATE_NAME',value);self.assertNotIn('hidden',value)
        self.assertIn('UNKNOWN',value)
    def test_missing_remains_missing(self):
        p=r.Projection('A',{},mode='cause');p.feed(envelope(missing=True))
        self.assertEqual(p.finish()['files']['cause']['record'],'MISSING')
    def test_process_only_preserves_unknown_tree(self):
        p=r.Projection('B',EXPECTED,mode='process');p.feed(envelope('B',process=True));v=p.finish()
        self.assertEqual(v['process'],'PID_NOT_VISIBLE_AT_TWO_SNAPSHOTS');self.assertEqual(v['remote_descendants'],'UNKNOWN')
        self.assertEqual(v['files'],{})

class AdapterTests(unittest.TestCase):
    def test_real_owned_pipe_and_disk_capture_exclude_diagnostic_wire(self):
        parent=(ROOT/'build/directdds-startup-cause-integration-20261103-d01/pipe-fixtures').resolve()
        parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='dds-pipe-',dir=parent) as tmp:
            directory=Path(tmp).resolve();self.assertTrue(directory.is_relative_to(parent))
            self.assertTrue(directory.name.startswith('dds-pipe-'))
            root=Root();root.captures[0]=open(directory/'stdout.filtered','xb')
            original=root.captures[0];a=s.Adapter(root,'A',{},'cause')
            wire=b'NORMAL\n'+framed(data=b'SYNTHETIC_PRIVATE_CANARY=not_exported\n')+b'DONE\n'
            code='import sys;sys.stdout.buffer.write(bytes.fromhex('+repr(wire.hex())+'));sys.stdout.flush()'
            child=subprocess.Popen([sys.executable,'-I','-B','-c',code],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                   creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            try:
                output,errors=child.communicate(timeout=5)
                self.assertEqual(child.returncode,0);self.assertEqual(errors,b'')
                self.assertEqual(len(output),len(wire))
                for offset in range(0,len(output),17):deliver(a,root,output[offset:offset+17])
                self.assertEqual(a.finish()['files']['cause']['record'],'UNKNOWN')
                root.captures[0].flush()
                self.assertEqual((directory/'stdout.filtered').read_bytes(),b'NORMAL\nDONE\n')
                self.assertEqual(bytes(root.output[0]),b'NORMAL\nDONE\n')
            finally:
                if child.poll() is None:child.kill();child.wait(timeout=2)
                child.stdout.close();child.stderr.close();original.close();a.erase()
    def test_every_single_byte_boundary_and_both_sinks(self):
        for ending in (b'\n',b'\r\n',b'\r\r\n'):
            root=Root();a=s.Adapter(root,'A',{},'cause')
            data=b'NORMAL_BEFORE\n'+framed().replace(b'\n',ending)+b'NORMAL_AFTER\n'
            for byte in data:deliver(a,root,bytes([byte]))
            v=a.finish();self.assertEqual(bytes(root.output[0]),b'NORMAL_BEFORE\nNORMAL_AFTER\n')
            self.assertEqual(root.captures[0].original.getvalue(),bytes(root.output[0]))
            self.assertEqual(v['files']['cause']['first_reason'],'PARTICIPANT_NULL')
    def test_plain_partial_tail_is_not_lost(self):
        root=Root();a=s.Adapter(root,'A',{},'cause');deliver(a,root,b'NORMAL_NO_LF')
        self.assertEqual(a.finish()['record'],'NOT_OBSERVED');self.assertEqual(bytes(root.output[0]),b'NORMAL_NO_LF')
    def test_short_magic_prefix_at_eof_never_reaches_sinks(self):
        for magic in (b'S1DIAG_',b'S1RO_'):
            for count in range(1,len(magic)+1):
                root=Root();a=s.Adapter(root,'A',{},'cause');deliver(a,root,magic[:count])
                with self.assertRaises(r.EvidenceUnknown):a.finish()
                self.assertEqual(root.captures[0].original.getvalue(),b'');self.assertEqual(bytes(root.output[0]),b'')
    def test_truncation_duplicate_bad_frame_and_unknown_close_fail(self):
        for wire in (framed()[:-7],framed()+framed(),framed().replace(b'S1RO_BEGIN A',b'S1RO_BEGIN B'),framed().replace(b'S1DIAG_END A OK',b'S1DIAG_END A FAILED')):
            root=Root();a=s.Adapter(root,'A',{},'cause')
            with self.assertRaises(r.EvidenceUnknown):
                for i in range(0,len(wire),64):deliver(a,root,wire[i:i+64])
                a.finish()
            self.assertNotIn(b'S1RO_',root.captures[0].original.getvalue());a.erase()
            self.assertFalse(a.frame.pending);self.assertFalse(a.frame.parser.payload)
    def test_close_error_not_swallowed(self):
        root=Root();a=s.Adapter(root,'A',{},'cause')
        with patch.object(a.capture.original,'close',side_effect=OSError('SYNTHETIC_CLOSE')):
            with self.assertRaises(OSError):a.capture.close()
    def test_preinstall_guard_prevents_partial_create_capture(self):
        class Base:
            def __init__(self,grant):pass
            def register(self,step):return Root(step.id)
            def create_and_resume(self,*args):raise RuntimeError('SYNTHETIC_AFTER_RESUME')
            def observe_chunk(self,*args):raise AssertionError('MUST_NOT_REACH_RAW_WRITE')
        class Backend(s.StartupEvidenceMixin,Base):pass
        x=Backend(types.SimpleNamespace(startup_evidence_bound=True));step=types.SimpleNamespace(id='data.a.launch')
        root=x.register(step)
        with self.assertRaises(RuntimeError):x.create_and_resume(root,step,1)
        with self.assertRaises(r.EvidenceUnknown):x.observe_chunk(root,'stdout',framed(),0,1,'CLEANUP')
        self.assertEqual(root.captures[0].getvalue(),b'')
    def test_notice_sees_full_chunk_before_projection(self):
        class Base:
            def __init__(self,grant):pass
            def observe_chunk(self,root,stream,data,*args):
                self.seen=data
                if b'authorization request' in data:raise RuntimeError('SYNTHETIC_NOTICE_STOP')
        class Backend(s.StartupEvidenceMixin,Base):pass
        x=Backend(types.SimpleNamespace(startup_evidence_bound=True));root=Root();root.startup_required=True
        root.startup_adapter=s.Adapter(root,'A',{},'cause')
        wire=framed().replace(b'S1RO_BEGIN A',b'authorization request')
        with self.assertRaises(RuntimeError):x.observe_chunk(root,'stdout',wire,0,1,'MONITOR')
        self.assertEqual(x.seen,wire);self.assertEqual(root.captures[0].original.getvalue(),b'')
    def test_legacy_gate_does_not_assume_exit(self):
        class Base:
            def __init__(self,grant):self.journal=types.SimpleNamespace(emit=lambda v:None,failed=False)
            def outcome(self,*args):return {'accepted':True}
        class Backend(s.StartupEvidenceMixin,Base):pass
        for process in ('SAME_STARTTIME_RUNNING_SNAPSHOT','SAME_STARTTIME_ZOMBIE','UNKNOWN'):
            x=Backend(types.SimpleNamespace(startup_evidence_bound=True));root=Root('readonly.4')
            root.startup_adapter=types.SimpleNamespace(finish=lambda:{'process':process},done=True,final_error=None,
              frame=types.SimpleNamespace(projected={'process':process}))
            with self.assertRaises(r.EvidenceUnknown):x.outcome(root,types.SimpleNamespace(id=root.case))

class ClosingFile(io.BytesIO):
    def close(self):self.was_closed=True # keep synthetic bytes inspectable

class CleanupBase:
    def __init__(self,grant):
        self.events=[];self.journal=types.SimpleNamespace(emit=self.events.append,failed=False)
    def observe_chunk(self,*args):pass
    def cleanup(self,root,deadline):
        wire=self.cleanup_wire
        for offset in range(0,len(wire),31):
            data=wire[offset:offset+31]
            self.observe_chunk(root,'stdout',data,0,1,'CLEANUP')
            root.captures[0].write(data);root.captures[0].flush();root.output[0].extend(data)
        root.receipt['EOF']=True
        try:root.captures[0].close()
        except OSError:return {'step':root.case,'state':'UNKNOWN','EOF':True,'cleanup_errors':['SYNTHETIC_CAPTURE_CLOSE']}
        return {'step':root.case,'state':'TERMINAL','EOF':True,'cleanup_errors':[]}
class CleanupBackend(s.StartupEvidenceMixin,CleanupBase):pass
def cleanup_fixture(wire):
    x=CleanupBackend(types.SimpleNamespace(startup_evidence_bound=True));x.cleanup_wire=wire
    root=Root('data.b.launch');root.receipt['EOF']=False;original=ClosingFile();root.captures[0]=original
    root.startup_required=True;root.startup_adapter=s.Adapter(root,'B',{},'cause')
    return x,root,original

class CleanupTests(unittest.TestCase):
    def test_complete_missing_frame_arriving_only_during_cleanup_is_retained_once(self):
        x,root,f=cleanup_fixture(framed('B',missing=True));row=x.cleanup(root,1)
        self.assertEqual(row['state'],'TERMINAL');self.assertTrue(f.was_closed)
        e=x.startup_projections[root.case];self.assertFalse(e['transport_qualified'])
        self.assertEqual(e['projection']['files']['cause']['record'],'MISSING')
        x.publish_projection(root,'OTHER');self.assertEqual(len(x.events),1)
    def test_truncated_cleanup_frame_is_protocol_error_with_original_close(self):
        x,root,f=cleanup_fixture(framed('B')[:-7]);row=x.cleanup(root,1)
        self.assertEqual(row['state'],'TERMINAL');self.assertTrue(f.was_closed)
        self.assertTrue(row['business_blocked']);self.assertEqual(row['cleanup_errors'],[])
        self.assertEqual(row['startup_diagnostic_error'],'INLINE_TRUNCATED_FRAME')
        self.assertIn(root.case,x.startup_projection_errors)
    def test_plain_partial_tail_is_flushed_before_close(self):
        x,root,f=cleanup_fixture(b'NORMAL_PARTIAL');row=x.cleanup(root,1)
        self.assertEqual(row['state'],'TERMINAL');self.assertEqual(f.getvalue(),b'NORMAL_PARTIAL')
        self.assertEqual(bytes(root.output[0]),b'NORMAL_PARTIAL');self.assertTrue(f.was_closed)
    def test_record_failure_cannot_skip_close_or_become_success(self):
        x,root,f=cleanup_fixture(framed('B',missing=True))
        def fail(event):raise RuntimeError('SYNTHETIC_RECORD_FAILURE')
        x.journal.emit=fail;row=x.cleanup(root,1)
        self.assertTrue(f.was_closed);self.assertTrue(row['business_blocked'])
        self.assertEqual(row['startup_diagnostic_error'],'INLINE_RECORD_FAILED')
        self.assertEqual(x.startup_projections,{})
        x.publish_projection(root,'REPEAT');self.assertEqual(len(x.startup_attempted),1)
    def test_record_failure_cannot_be_accepted_by_repeated_outcome(self):
        x,root,f=cleanup_fixture(framed('B',missing=True))
        def fail(event):raise RuntimeError('SYNTHETIC_RECORD_FAILURE')
        x.journal.emit=fail;x.cleanup(root,1)
        with self.assertRaises(r.EvidenceUnknown):x.outcome(root,types.SimpleNamespace(id=root.case))
        self.assertTrue(f.was_closed);self.assertEqual(len(x.startup_attempted),1)
    def test_os_close_failure_stays_distinct_from_projection(self):
        x,root,f=cleanup_fixture(framed('B',missing=True))
        def fail_close():f.was_closed=True;raise OSError('SYNTHETIC_CLOSE_FAILURE')
        f.close=fail_close;row=x.cleanup(root,1)
        self.assertEqual(row['state'],'UNKNOWN');self.assertEqual(row['cleanup_errors'],['SYNTHETIC_CAPTURE_CLOSE'])
        self.assertEqual(x.startup_projection_errors,{})
        self.assertEqual(x.startup_projections[root.case]['projection']['files']['cause']['record'],'MISSING')

@contextlib.contextmanager
def runtime():
    buffers,inputs,manifest=b.source_buffers();saved={n[:-3]:sys.modules.get(n[:-3]) for n in b.parent.SOURCES}
    order=('goal_contract','goal_policy','goal_handle_lifecycle','goal_records','resource_receipt','goal_evidence',
      'startup_cause','readonly_evidence','goal_read_context','dds_startup_evidence','goal_authorization','goal_driver',
      'goal_notice','dds_observation','dds_notice','goal_windows_backend','goal_storage','goal_workflow','goal_entry')
    modules={}
    try:
        with patch.object(ctypes,'WinDLL',side_effect=AssertionError('LIVE_DLL_FORBIDDEN'),create=True):
            for n in order:
                m=types.ModuleType(n);m.__file__=str(b.parent.ACTIVE/(n+'.py'));sys.modules[n]=m
                exec(compile(buffers[n+'.py'],m.__file__,'exec'),m.__dict__);modules[n]=m
            yield types.SimpleNamespace(**modules,buffers=buffers,inputs=inputs,manifest=manifest)
    finally:
        for n,m in saved.items():
            if m is None:sys.modules.pop(n,None)
            else:sys.modules[n]=m

class CompositionTests(unittest.TestCase):
    def test_two_payloads_and_registry_budget_only(self):
        with runtime() as m:
            c=m.goal_contract;r=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            self.assertEqual(len(r),123);self.assertEqual(sum(x.kind not in ('server','controller') for x in r.values()),121)
            self.assertEqual(c.LIMITS['total_ms'],240000);self.assertEqual(c.LIMITS['work_ms'],236000)
            self.assertEqual(len(m.inputs['payloads']),11);self.assertEqual(sum(x['executable'] for x in m.inputs['payloads']),4)
            self.assertEqual(m.inputs['artifacts']['app']['sha256'],b.REPLACEMENTS['payload.app'][2])
            for side in ('a','b'):
                self.assertIn('S1DIAG_BEGIN',r['data.'+side+'.launch'].args[-1])
                self.assertNotIn('S1DIAG_BEGIN',r['probe.'+side+'.launch'].args[-1])
            self.assertIn('S1DIAG_BEGIN B process',r['readonly.4'].args[-1])
    def test_fail_stop_driver_and_dds_oracles_unchanged(self):
        with runtime() as m:
            old=(b.parent.BASE/'active/goal_records.py').read_bytes().replace(b'9a72c6e431d0aa60',b.parent.PROBE_RUN.encode()).replace(b'9a72c6e431d0aa61',b.parent.DATA_RUN.encode())
            self.assertEqual(old,m.buffers['goal_records.py'])
            maintained=(ROOT/'scripts/hdc_goal/goal_driver.py').read_bytes()
            def cls(raw):return ast.dump(next(n for n in ast.parse(raw).body if isinstance(n,ast.ClassDef) and n.name=='GoalDriver'),include_attributes=False)
            self.assertEqual(cls(maintained),cls(m.buffers['goal_driver.py']))
    def test_valid_diagnostic_cannot_accept_native_failure(self):
        with runtime() as m:
            c=m.goal_contract;r=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            root=Root();a=s.Adapter(root,'A',{},'cause')
            raw=framed()+b'S1_GOAL_REMOTE_RC:88\nS1_GOAL_STEP:data.a.launch:0\n'
            for i in range(0,len(raw),64):deliver(a,root,raw[i:i+64])
            a.finish()
            with self.assertRaises(m.goal_evidence.EvidenceRejected) as error:m.goal_evidence.evaluate_step(r['data.a.launch'],bytes(root.output[0]),b'',0)
            self.assertEqual(error.exception.reason.value,'NATIVE_REMOTE_NONZERO')
    def test_new_effects_required_old_approval_not_reused(self):
        with runtime() as m:
            shape=next(n for n in ast.parse(m.buffers['goal_authorization.py']).body if isinstance(n,ast.FunctionDef) and n.name=='validate_approval_shape')
            required=next(ast.literal_eval(n.value) for n in shape.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='required' for t in n.targets))
            self.assertIn('fixed_old_B_process_snapshots_no_signal',required)
            self.assertIn('inline_current_cause_512_projection_only',required)
            self.assertEqual(m.goal_authorization.SOURCE_NAMES,b.parent.SOURCES)
    def test_shell_syntax_and_subshell_preserves_native_rc(self):
        shell=Path('C:/Program Files/Git/bin/bash.exe');self.assertTrue(shell.is_file())
        with runtime() as m:
            c=m.goal_contract;r=c.validate_registry(c.build_registry(m.inputs['payloads'],m.inputs['readonly_commands'],m.inputs['controller']))
            for key in ('readonly.4','data.a.launch','data.b.launch'):
                q=subprocess.run([str(shell),'--noprofile','--norc','-n','-c',r[key].args[-1]],capture_output=True,timeout=10)
                self.assertEqual(q.returncode,0,'SYNTHETIC_SHELL_SYNTAX')
        with patch.object(s,'query',return_value="printf 'SYNTHETIC_QUERY_ONLY\\n'; exit 122"):
            script='native_rc=7; '+s.postlude('A',{},'cause',b.parent.DEVICE+'/data')+'printf "NATIVE_RC=%s\\n" "$native_rc"'
        q=subprocess.run([str(shell),'--noprofile','--norc','-c',script],capture_output=True,timeout=10)
        self.assertEqual(q.returncode,0);self.assertIn(b'NATIVE_RC=7\n',q.stdout)

if __name__=='__main__':unittest.main()
