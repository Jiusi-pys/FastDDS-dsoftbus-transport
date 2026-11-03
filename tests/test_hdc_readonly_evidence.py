"""Synthetic D04 typed/proc/wire boundaries. Never opens real evidence or HDC."""
import ast
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts/hdc_goal'))
import readonly_evidence as r

EXPECTED=dict(pid=1234,parent=77,birth=9876,uid=2000,euid=2000,gid=2000,egid=2000,run=99)
DIAG='phase=14\nnative_rc_valid=0\nnative_rc=0\n'
RECORDS={
 'failure':('format=S1GOALFAIL1\n'+DIAG+'origin_bound=1\nrestored_entry=1\nrestore_rc_valid=1\nrestore_rc=0\nSDK_or_data_success=0\n').encode(),
 'stage':DIAG.encode(),
 'exit':('format=S1GOALEXIT1\nrun=99\npid=1234\nstatus=-9\nrestored_entry=1\nrestore_rc_valid=1\nrestore_rc=0\nworker_parent_restoration=UNKNOWN\nSDK_quiet=UNKNOWN\n'+DIAG).encode(),
 'result':b'format=S1GOALRESULT1\nstatus=-9\nresult_status=-9\nphase=1\nwrites=0\ntakes=3\nlocal_control_drained=1\nowners_retained=0\nSDK_quiescence_unproven=1\ndestroy_rc=0\nprimary_phase=18\nnative_rc_valid=1\nnative_rc=-9\n'}


def stat_record(pid=1234,birth=9876,parent=77,state='S',comm='fixture'):
    fields=[state,str(parent),*['0']*17,str(birth),*['0']*30]
    return (str(pid)+' ('+comm+') '+' '.join(fields)+'\n').encode()


def status_record(pid=1234,parent=77,uid=2000):
    return ('Name:\tPRIVATE_CANARY_NAME\nState:\tS (sleeping)\nPid:\t'+str(pid)+'\nPPid:\t'+str(parent)+
      '\nUid:\t'+(' '.join([str(uid)]*4))+'\nGid:\t2000 2000 2000 2000\n').encode()


def item(label,data):
    size=r.CAPS[label] if label in ('P0','P1','status') else len(data)
    wire=data+b'\x00MDDS_READ_RC=0\x00'
    return (('S1RO_ITEM '+label+' DATA '+str(size)+'\n').encode()+b''.join(
      (' '+' '.join('%02x'%v for v in wire[i:i+16])+'\n').encode() for i in range(0,len(wire),16))+
      ('S1RO_END '+label+' OK\n').encode())


def envelope(side='A',missing=False,proc_missing=False):
    result=('S1RO_BEGIN '+side+'\n').encode()
    labels=(('P0',) if side=='B' else ())+('failure','result','exit','stage')+(('status','P1') if side=='B' else ())
    for label in labels:
        if label in RECORDS:
            result+=(('S1RO_ITEM '+label+' MISSING 0\n').encode() if missing else item(label,RECORDS[label]))
        elif proc_missing:result+=('S1RO_ITEM '+label+' NOT_VISIBLE 0\n').encode()
        else:result+=item(label,status_record() if label=='status' else stat_record())
    return result+('S1RO_DONE '+side+' 0\n').encode()


class RecordTests(unittest.TestCase):
    def test_all_producer_grammars(self):
        for k,v in RECORDS.items():self.assertEqual(r.typed(k,v,EXPECTED)['record'],'VALID')
    def test_failure_phase_is_not_sdk_attribution(self):
        a=r.typed('failure',RECORDS['failure'],EXPECTED);self.assertEqual(a['phase'],14);self.assertFalse(a['native_rc_valid'])
    def test_identifiers_never_projected(self):
        a=r.typed('exit',RECORDS['exit'],EXPECTED);out=json.dumps(a)
        for value in ('1234','9876','2000','"run"','"pid"'):self.assertNotIn(value,out)
    def test_missing_extra_duplicate_and_order_fail(self):
        v=RECORDS['stage']
        for bad in (v[:-1],v+b'phase=14\n',v+b'other=1\n',b'native_rc=0\nphase=14\nnative_rc_valid=0\n'):
            with self.assertRaises(r.EvidenceUnknown):r.typed('stage',bad,EXPECTED)
    def test_number_canonical_and_bounds(self):
        for v in ('-0','00','+1',' 1','1.0','18446744073709551616'):
            with self.assertRaises(r.EvidenceUnknown):r.number(v)
    def test_error_and_phase_bounds(self):
        for old,new in ((b'phase=14',b'phase=23'),(b'native_rc=0',b'native_rc=2147483648'),(b'native_rc_valid=0',b'native_rc_valid=2')):
            with self.assertRaises(r.EvidenceUnknown):r.typed('stage',RECORDS['stage'].replace(old,new),EXPECTED)
    def test_invalid_rc_validity(self):
        with self.assertRaises(r.EvidenceUnknown):r.typed('stage',RECORDS['stage'].replace(b'native_rc=0',b'native_rc=-9'),EXPECTED)
    def test_exit_run_pid_mismatch(self):
        for k,v in (('run',100),('pid',12)):
            with self.assertRaises(r.EvidenceUnknown):r.typed('exit',RECORDS['exit'],dict(EXPECTED,**{k:v}))
    def test_quiet_cannot_be_promoted(self):
        with self.assertRaises(r.EvidenceUnknown):r.typed('exit',RECORDS['exit'].replace(b'SDK_quiet=UNKNOWN',b'SDK_quiet=PASS'),EXPECTED)
    def test_native_failure_rc_and_counts(self):
        a=r.typed('result',RECORDS['result'],EXPECTED)
        self.assertEqual((a['status'],a['native_rc'],a['writes'],a['takes']),(-9,-9,'ZERO','THREE'))
    def test_encoding_nul_and_newline(self):
        for bad in (RECORDS['stage']+b'\x00',RECORDS['stage'].replace(b'\n',b'\r\n'),b'\xff\n'):
            with self.assertRaises(r.EvidenceUnknown):r.typed('stage',bad,EXPECTED)


class ProcTests(unittest.TestCase):
    def test_stat_and_status_public_projection(self):
        a=r.proc_stat(stat_record(),EXPECTED);b=r.proc_status(status_record(),EXPECTED)
        self.assertTrue(a['parent_matches'] and b['owner_matches']);self.assertNotIn('PRIVATE_CANARY',json.dumps(b))
    def test_comm_parenthesis_handled(self):self.assertTrue(r.proc_stat(stat_record(comm='x) embedded ) x'),EXPECTED)['parent_matches'])
    def test_pid_and_birth_reuse_rejected(self):
        for v in (stat_record(pid=23),stat_record(birth=1)):
            with self.assertRaises(r.EvidenceUnknown):r.proc_stat(v,EXPECTED)
    def test_parent_change_is_not_reuse(self):self.assertFalse(r.proc_stat(stat_record(parent=1),EXPECTED)['parent_matches'])
    def test_owner_mismatch_and_duplicate(self):
        for data in (status_record(uid=1),status_record()+b'Uid:\t2000 2000 2000 2000\n'):
            with self.assertRaises(r.EvidenceUnknown):r.proc_status(data,EXPECTED)
    def test_cap_equality_unknown(self):
        for func,cap in ((r.proc_stat,4096),(r.proc_status,8192)):
            with self.assertRaises(r.EvidenceUnknown):func(b'x'*(cap-1)+b'\n',EXPECTED)
    def test_dead_is_not_running(self):
        for state in ('X','x'):self.assertEqual(r.proc_stat(stat_record(state=state),EXPECTED)['state'],'DEAD_SNAPSHOT')


class WireTests(unittest.TestCase):
    def parse(self,data,side='A'):
        p=r.Projection(side,EXPECTED)
        for i in range(0,len(data),7):p.feed(data[i:i+7])
        return p.finish()
    def test_A_complete_and_EOL_variants(self):
        for eol in (b'\n',b'\r\n',b'\r\r\n'):
            row=self.parse(envelope().replace(b'\n',eol));self.assertEqual(row['files']['failure']['phase'],14)
    def test_B_complete(self):self.assertEqual(self.parse(envelope('B'),'B')['process'],'SAME_STARTTIME_RUNNING_SNAPSHOT')
    def test_missing_is_observation_not_pass_or_exit(self):
        row=self.parse(envelope('B',True,True),'B')
        self.assertEqual(row['process'],'PID_NOT_VISIBLE_AT_TWO_SNAPSHOTS')
        self.assertTrue(all(x['record']=='MISSING' for x in row['files'].values()))
        self.assertEqual(row['SDK_quiet'],'UNKNOWN')
    def test_duplicate_done_and_trailing_rejected(self):
        for tail in (b'S1RO_DONE A 0\n',b'private\n'):
            with self.assertRaises(r.EvidenceUnknown):self.parse(envelope()+tail)
    def test_order_and_remote_nonzero(self):
        for data in (envelope().replace(b'ITEM failure',b'ITEM result',1),envelope().replace(b'DONE A 0',b'DONE A 1')):
            with self.assertRaises(r.EvidenceUnknown):self.parse(data)
    def test_hex_or_footer_error_no_echo(self):
        data=envelope().replace(b'00 4d 44 44 53',b'00 4d 44 44 ff',1)
        with self.assertRaises(r.EvidenceUnknown) as e:self.parse(data)
        self.assertNotIn('PRIVATE',str(e.exception))
    def test_partial_cap_and_error_erase(self):
        p=r.Projection('A',EXPECTED)
        with self.assertRaises(r.EvidenceUnknown):p.feed(b'x'*257)
        self.assertFalse(p.pending or p.payload)
        with self.assertRaises(r.EvidenceUnknown):self.parse(envelope()[:-4])
    def test_exact_read_count_not_only_grammar(self):
        with self.assertRaises(r.EvidenceUnknown):self.parse(envelope().replace(b'failure DATA '+str(len(RECORDS['failure'])).encode(),b'failure DATA 1',1))
    def test_body_not_in_projection(self):
        row=self.parse(envelope('B'),'B');out=json.dumps(row)
        for v in ('PRIVATE_CANARY_NAME','9876','1234','2000','MDDS_READ_RC'):self.assertNotIn(v,out)
    def test_status_not_visible_after_stat_is_unknown(self):
        p=r.Projection('B',EXPECTED)
        data=envelope('B');start=data.index(b'S1RO_ITEM status');end=data.index(b'S1RO_ITEM P1')
        with self.assertRaises(r.EvidenceUnknown):self.parse(data[:start]+b'S1RO_ITEM status NOT_VISIBLE 0\n'+data[end:],'B')
    def test_parent_change_preserved_but_not_accepted(self):
        data=envelope('B').replace(item('P0',stat_record()),item('P0',stat_record(parent=1)))
        p=r.Projection('B',EXPECTED)
        for i in range(0,len(data),256):p.feed(data[i:i+256])
        self.assertFalse(p.rows['P0']['parent_matches'])
        with self.assertRaises(r.EvidenceUnknown):p.finish()


class FinalBoundaryTests(unittest.TestCase):
    def setup_boundary(self,late=None):
        clock=[1.];accepted=[];writes=[]
        driver=types.SimpleNamespace(need=r.require,GoalDriver=object,ContractError=r.EvidenceUnknown)
        _,B=r.make_types(driver,types.SimpleNamespace(GoalWindowsBackend=object),lambda x:x.get('qualified') is True)
        b=B.__new__(B);b.journal=types.SimpleNamespace(case='',failed=False)
        report=dict(schema='NOTICE_EVIDENCE_V2',record_failed=False,overflow=False,failure_rules=[],emergency_events=[],events=[])
        b.marker=types.SimpleNamespace(report=lambda:report,eof=lambda *a:None,advance=lambda:None)
        p=r.Projection('A',EXPECTED);data=envelope()
        for i in range(0,len(data),256):p.feed(data[i:i+256])
        b.parsers={'read.A':p};b.observations={};b.server_ok=lambda:True
        root=types.SimpleNamespace(case='read.A',receipt=dict(native_exit=0,stop_attempts=0,EOF=True,cleanup_errors=[],qualified=True),
          dest=Path('SYNTHETIC_DEST'),closed=False,identity_deadline=15.,state=lambda:'TERMINAL')
        def identity():
            if late=='identity':clock[0]=15.01
        def close():
            root.closed=True
            if late=='close':clock[0]=15.01
        def put(*a):
            writes.append(True)
            if late=='write':clock[0]=15.01
        root.identity=identity;root.close=close;b.w=types.SimpleNamespace(put=put)
        b.grant=types.SimpleNamespace(work_deadline=56.,mark_terminal=lambda *a:None,accept=lambda *a:accepted.append(True))
        return b,root,clock,accepted,writes
    def test_normal_final_boundary(self):
        b,root,clock,accepted,writes=self.setup_boundary()
        with patch.object(r.time,'monotonic',lambda:clock[0]):out=b.outcome(root,types.SimpleNamespace(id='read.A'))
        self.assertTrue(out['accepted']);self.assertEqual(len(accepted),1);self.assertTrue(root.closed and writes)
    def test_each_late_final_action_prevents_accept(self):
        for late in ('identity','close','write'):
            b,root,clock,accepted,writes=self.setup_boundary(late)
            with patch.object(r.time,'monotonic',lambda:clock[0]):
                with self.assertRaises(r.EvidenceUnknown):b.outcome(root,types.SimpleNamespace(id='read.A'))
            self.assertFalse(accepted)
            self.assertFalse(root.receipt['accepted_evidence'])
    def test_work_deadline_and_exact_client_deadline(self):
        for now in (15.,56.):
            b,root,clock,accepted,writes=self.setup_boundary();clock[0]=now
            with patch.object(r.time,'monotonic',lambda:clock[0]):
                with self.assertRaises(r.EvidenceUnknown):b.outcome(root,types.SimpleNamespace(id='read.A'))
            self.assertFalse(accepted)


class OfflineTests(unittest.TestCase):
    def test_query_fixed_whitelist_no_write_signal_readlink(self):
        q=r.query('B',EXPECTED)
        for text in ('dd bs=1 count=','exec 3<','proc_snapshot P0','proc_snapshot P1'):self.assertIn(text,q)
        for text in ('readlink ','kill ','chmod ','mkdir ','rm ','SetSelf','app.out','app.err','cmdline','environ','sha256sum','cat '):self.assertNotIn(text,q)
        self.assertLess(q.index('proc_snapshot P0\n'),q.index('read_record failure'))
    def test_shell_syntax_only_real_Git_shell(self):
        shell=Path('C:/Program Files/Git/bin/bash.exe')
        self.assertTrue(shell.is_file(),'required local shell missing')
        p=subprocess.run([str(shell),'--noprofile','--norc','-n','-c',r.query('B',EXPECTED)],capture_output=True,timeout=10)
        self.assertEqual(p.returncode,0,'shell syntax failure; content deliberately not echoed')
    def test_memory_capture_has_no_retained_state(self):
        sink=r.MemoryCapture();self.assertEqual(sink.write(b'PRIVATE_CANARY'),14);self.assertFalse(hasattr(sink,'__dict__'))
    def test_new_grant_required_before_live_import(self):
        a=types.ModuleType('goal_authorization');a._grants=set();c=types.ModuleType('goal_contract');c.need=r.require;c.SCOPE=r.SCOPE;c.NONCE=r.NONCE
        with patch.dict(sys.modules,goal_authorization=a,goal_contract=c):
            with self.assertRaises(r.EvidenceUnknown):r.actual_driver(object())
    def test_telemetry_exception_exactly_scoped(self):
        row=dict(case='server',stream='stdout',phase='MONITOR',event_type='UNKNOWN',rule_id='AUTH_CONTEXT_UNKNOWN',trigger_family='AUTH_WORD_ONLY',complete_record=True)
        self.assertTrue(r.telemetry(row))
        for k,v in [('case','read.B'),('stream','stderr'),('phase','OTHER'),('complete_record',False),('event_type','REQUEST_TEXT'),('trigger_family','OTHER')]:self.assertFalse(r.telemetry(dict(row,**{k:v})))
    def test_no_production_or_raw_access_entry_in_query_module(self):
        tree=ast.parse((ROOT/'scripts/hdc_goal/readonly_evidence.py').read_text())
        self.assertFalse(any(isinstance(n,ast.Call) and isinstance(n.func,ast.Name) and n.func.id in ('open','eval','exec') for n in ast.walk(tree)))
    def test_real_local_dd_source_count(self):
        shell='C:/Program Files/Git/bin/bash.exe'
        p=subprocess.run([shell,'--noprofile','--norc','-c',"printf '%s' 'SYNTHETIC_CANARY' | dd bs=1 count=5 2>/dev/null"],capture_output=True,timeout=10)
        self.assertTrue(p.returncode==0 and p.stdout==b'SYNTH','bounded local dd contract failed')
    def test_real_local_missing_metadata_and_denial_are_distinct(self):
        shell='C:/Program Files/Git/bin/bash.exe'
        helpers=r.query('A',EXPECTED).split("root='",1)[0]
        with tempfile.TemporaryDirectory(prefix='mdds_ro_missing_') as directory:
            d=Path(directory).resolve()
            self.assertTrue(d.parent==Path(tempfile.gettempdir()).resolve() and d.name.startswith('mdds_ro_missing_'))
            body=helpers+"root='"+d.as_posix()+"'\nread_record stage \"$root/absent\"\n"
            p=subprocess.run([shell,'--noprofile','--norc','-c',body],capture_output=True,timeout=10)
            self.assertTrue(p.returncode==0 and p.stdout==b'S1RO_ITEM stage MISSING 0\n','local ENOENT classification failed')
            denied=helpers+"stat() { printf '%s\\n' 'stat: fixed-path: Permission denied' >&2; return 1; }\nroot='"+d.as_posix()+"'\nread_record stage \"$root/absent\"\n"
            p=subprocess.run([shell,'--noprofile','--norc','-c',denied],capture_output=True,timeout=10)
            self.assertTrue(p.returncode==122 and p.stdout==b'S1RO_ABORT BOUND_OR_IDENTITY_UNKNOWN\n','permission must not become missing')


class WiringTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('_readonly_builder_test',ROOT/'scripts/hdc_goal/prepare_readonly_evidence.py')
        cls.builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.builder)
        cls.buffers,_=cls.builder.source_buffers()
        cls.contract,cls.commands,cls.registry=cls.builder.commands(cls.buffers)
    def test_exact_registry_and_dependencies(self):
        self.assertTrue(tuple(self.registry)==('server','read.B','read.A'),'registry shape changed')
        self.assertTrue(self.registry['read.A'].requires==('server','read.B'),'B acceptance dependency missing')
        self.assertTrue(self.registry['read.B'].requires==('server',),'server dependency missing')
    def test_scope_limits_and_total_bytes(self):
        c=self.contract
        self.assertTrue(c.SCOPE==r.SCOPE and c.NONCE==r.NONCE,'scope binding changed')
        for name,value in [('total_ms',60000),('work_ms',56000),('client_ms',15000),('clients',2),('concurrent_clients',1),('client_capture',65536)]:
            self.assertTrue(c.LIMITS[name]==value,'limit '+name)
        self.assertLessEqual(8*1024+2*4096+8192,25600)
    def test_no_raw_persistence_and_current_terminal_fix(self):
        text=self.buffers['goal_win32_owned.py'].decode()
        self.assertNotIn("name+'.raw'",text)
        self.assertNotIn('self.output[i].extend(data)',text)
        self.assertIn('MemoryCapture()',text)
        self.assertIn('observe_owned_binding',text)
        self.assertTrue(self.buffers['goal_handle_lifecycle.py']==(ROOT/'scripts/hdc_goal/goal_handle_lifecycle.py').read_bytes(),'current lifecycle was not used')
        self.assertEqual(text.count('FIXED_READONLY_COMMAND_PIN'),2)
        self.assertNotIn("'argv':[str(exe),*args]",text)
        self.assertNotIn("'argv':[str(self.exe),*self.args]",text)
        self.assertIn('subprocess.list2cmdline([str(self.exe),*self.args])',text)
    def test_effects_do_not_authorize_business_or_identity(self):
        tree=ast.parse(self.buffers['goal_authorization.py'])
        effects=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='EFFECTS' for t in n.targets))
        self.assertIn('fixed_D04_records_and_B_proc_bounded_read',effects)
        for e in effects:self.assertNotIn('selection',e);self.assertNotIn('deployment',e)
        self.assertNotIn('strict_auth_context_unknown_stop',effects)
    def test_old_registry_mutation_refused(self):
        with self.assertRaises(Exception):self.contract.validate_registry(list(reversed(list(self.registry.values()))))
    def test_private_context_never_projected(self):
        tree=ast.parse(self.buffers['goal_read_context.py']);ctx=ast.literal_eval(next(n.value for n in tree.body if isinstance(n,ast.Assign)))
        self.assertTrue(set(ctx)=={'A','B'} and all(set(v)==set(EXPECTED) for v in ctx.values()),'context fields changed')
    def test_all_generated_syntax(self):
        for name,data in self.buffers.items():ast.parse(data,filename=name)
    def test_authorization_cross_process_determinism(self):
        code=('import importlib.util,hashlib;from pathlib import Path;'
          "p=Path('scripts/hdc_goal/prepare_readonly_evidence.py');"
          "s=importlib.util.spec_from_file_location('_det',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);"
          "data=m.source_buffers()[0]['goal_authorization.py'];print(hashlib.sha256(data).hexdigest())")
        rows=[subprocess.run([sys.executable,'-I','-B','-c',code],cwd=ROOT,capture_output=True,timeout=20) for _ in range(2)]
        self.assertTrue(all(x.returncode==0 for x in rows),'pure deterministic source worker failed')
        self.assertTrue(rows[0].stdout==rows[1].stdout,'public authorization source differs across processes')


if __name__=='__main__':unittest.main()
