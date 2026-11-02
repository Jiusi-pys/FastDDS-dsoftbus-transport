"""Bounded synthetic list/state/schedule checks; no HDC or real pipe access."""
import ast
import builtins
import contextlib
import ctypes
from ctypes import wintypes
import importlib.util
import io
import json
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]


def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m


adapter=module('_state_builder_test',ROOT/'scripts/hdc_goal/prepare_state_samples.py')
builder=adapter.builder
pair=module('_state_old_fixture',ROOT/'tests/test_hdc_probe_pair.py')


def client_stdout(server_message):
    # Public reference chain: server EchoClient -> ListTargetsAll -> Windows
    # stdout newline conversion. This is not a formatter from the parser.
    echo=server_message if server_message.endswith(b'\n') else server_message+b'\r\n'
    return echo.replace(b'\n',b'\thdc\n').replace(b'\n',b'\r\n')


@contextlib.contextmanager
def runtime():
    names=tuple(n for n in pair.NAMES if n!='probe_pair')+('state_samples',)
    with patch.object(pair,'builder',builder),patch.object(pair,'NAMES',names):
        with pair.runtime() as m:yield m


def fixture(m,first='Unknown',second='Connected'):
    def factory(*args):
        D,B=m.state_samples.make_types(*args);B.init_pair_state=B.init_state
        return D,B
    fake=types.SimpleNamespace(**vars(m));fake.probe_pair=types.SimpleNamespace(
        make_types=factory,CASES=m.state_samples.CASES,FIRST='state.S1',SECOND='state.S2')
    with patch.object(pair,'builder',builder):b,d,clock=pair.fixture(fake,first='success',second='success')
    b.grant.strict_state_samples=True;m.state_samples.time=types.SimpleNamespace(monotonic=clock)
    register=b.register
    def state_root(step):
        if step.kind=='server':return register(step)
        b.parsers[step.id]=m.state_samples.Listing(m.goal_contract.TARGETS['A1'])
        state=first if step.id=='state.S1' else second
        data=client_stdout(b'[Empty]' if state=='ABSENT' else (
            m.goal_contract.TARGETS['A1']+'\t\tUSB\t'+state+'\tCANARY_NAME\n').encode())
        legacy='probe.A1' if step.id=='state.S1' else 'probe.A2'
        root=pair.Root(legacy,clock,b.grant,m.goal_authorization.check_work_deadline,data)
        root.case=step.id;root.dest=Path(step.id);root.receipt['case']=step.id
        b.roots.append(root);b.captured[step.id]=0
        return root
    b.register=state_root
    return b,d,clock


class StateTests(unittest.TestCase):
    def listing(self,m,data):
        p=m.state_samples.Listing('FIXED_A')
        for i in range(0,len(data),7):p.feed(data[i:i+7])
        return p

    def test_reference_rows_empty_and_non_target_are_projected(self):
        with runtime() as m:
            for eol in (b'\n',b'\r\n',b'\r\r\n'):
                for state in ('Unknown','Ready','Connected','Offline','Unauthorized'):
                    server=b'OTHER\t\tTCP\tReady\tDISCARD_NAME\nFIXED_A\t\tUSB\t'+state.encode()+b'\tCANARY_NAME\n'
                    data=client_stdout(server).replace(b'\r\n',eol)
                    p=self.listing(m,data);row=p.finish()
                    self.assertEqual(row['device_state'],m.state_samples.STATES[state])
                    self.assertTrue(row['present']);self.assertNotIn('CANARY',json.dumps(row))
                    self.assertEqual(p.pending,b'')
                self.assertEqual(self.listing(m,client_stdout(b'[Empty]').replace(b'\r\n',eol)).finish()['device_state'],'A_ABSENT')
            row=self.listing(m,client_stdout(b'OTHER\t\tUSB\tConnected\tDISCARD\n')).finish()
            self.assertFalse(row['present'])

    def test_public_final_client_chain_is_exact_not_an_optional_tail(self):
        raw=b'FIXED_A\t\tUSB\tConnected\tNAME\n'
        self.assertEqual(client_stdout(raw),b'FIXED_A\t\tUSB\tConnected\tNAME\thdc\r\n')
        self.assertEqual(client_stdout(b'[Empty]'),b'[Empty]\r\thdc\r\n')
        with runtime() as m:
            self.assertEqual(self.listing(m,b'FIXED_A\t\tUSB\tConnected\tNAME\thdc\r\n').finish()['device_state'],'CONNECTED')
            for bad in (raw,b'[Empty]\r\n',b'[Empty]\thdc\r\n',client_stdout(raw).replace(b'\thdc',b'\thdcx'),
                        client_stdout(raw).replace(b'\thdc',b'\textra\thdc')):
                with self.assertRaises(ValueError):self.listing(m,bad).finish()

    def test_bad_format_duplicate_truncation_and_extra_records_fail(self):
        row=client_stdout(b'FIXED_A\t\tUSB\tConnected\tNAME\n')
        for data in (b'',row[:-1],row+row,row+b'extra\n',client_stdout(b'[Empty]')+row,
                     b'FIXED_A USB Connected NAME\n',row.replace(b'Connected',b'connected'),
                     row.replace(b'USB',b'NEW_KIND'),row.replace(b'NAME',b'bad\x00name'),
                     row.replace(b'NAME',b'\xff'),row+b'[Empty]\n'):
            with self.subTest(size=len(data)),runtime() as m:
                with self.assertRaisesRegex(ValueError,'FORMAT_UNKNOWN'):self.listing(m,data).finish()

    def test_memory_line_total_chunk_and_row_caps(self):
        with runtime() as m:
            for data in (b'x'*513+b'\n',b'x'*4097):
                p=m.state_samples.Listing('FIXED_A');p.feed(data)
                self.assertTrue(p.failed);self.assertEqual(p.pending,b'')
            p=m.state_samples.Listing('FIXED_A')
            for _ in range(129):p.feed(client_stdout(b'OTHER\t\tUSB\tReady\tN\n'))
            self.assertTrue(p.failed);self.assertEqual(p.rows,129);self.assertLess(p.total,32768)
            p=m.state_samples.Listing('FIXED_A')
            row=client_stdout(b'OTHER\t\tUSB\tReady\t'+b'x'*250+b'\n')
            for _ in range(120):p.feed(row)
            self.assertFalse(p.failed);self.assertLessEqual(p.total,32768)
            p.feed(row)
            self.assertTrue(p.failed);self.assertGreater(p.total,32768);self.assertLess(p.rows,128)
            self.assertLessEqual(p.peak,4608)
            sink=m.state_samples.MemoryCapture();self.assertEqual(sink.write(b'CANARY'),6)
            self.assertFalse(hasattr(sink,'__dict__'))

    def test_generated_scope_commands_capture_and_strict_effects(self):
        with runtime() as m:
            c=m.goal_contract;buffers,_=builder.source_buffers()
            self.assertEqual(c.LIMITS['client_ms'],5000)
            registry=c.build_registry(builder.commands(c));self.assertEqual(tuple(registry),m.state_samples.CASES)
            for k in ('state.S1','state.S2'):
                self.assertEqual(registry[k].argv(),(c.HDC,'-p','-s',c.ENDPOINT,'list','targets','-v'))
                self.assertEqual(registry[k].requires_terminal,())
            self.assertNotIn(b"name+'.raw'",buffers['goal_win32_owned.py'])
            self.assertNotIn(b'self.output[i].extend(data)',buffers['goal_win32_owned.py'])
            self.assertNotIn(b"'stdout.raw'",buffers['goal_storage.py'])
            self.assertNotIn('server_auth_word_unknown_observation',m.goal_authorization.EFFECTS)
            self.assertNotIn('new_remote_printf_commands',m.goal_authorization.EFFECTS)
            self.assertNotIn('remote_shell_termination_unknown',m.goal_authorization.EFFECTS)
            self.assertIn('strict_auth_context_unknown_stop',m.goal_authorization.EFFECTS)
            schema=json.loads(builder.read_safe(builder.ACTIVE/'INPUT_SCHEMAS_D01.json'))
            self.assertEqual(set(schema['effects']),m.goal_authorization.EFFECTS)

    def test_two_planned_samples_keep_state_unknown_as_data(self):
        with runtime() as m:
            b,d,c=fixture(m);result=d.run_samples();closed=d.finish()
            self.assertTrue(m.state_samples.observation_complete(d,closed))
            self.assertEqual([r['device_state'] for r in result['state_samples']],['DEVICE_STATE_UNKNOWN','CONNECTED'])
            self.assertEqual([r['planned_offset_s'] for r in result['state_samples']],[0,10])
            for k,row in b.samples.items():
                lo,hi=b.grant.sample_windows[k];self.assertLessEqual(lo,row['started_monotonic']);self.assertLessEqual(row['started_monotonic'],hi)
            self.assertGreaterEqual(b.samples['state.S2']['started_monotonic']-b.t0,10)
            self.assertNotIn('CANARY_NAME',json.dumps(b.records));self.assertNotIn('CANARY_NAME',json.dumps(b.journal.rows))

    def test_absent_at_both_points_is_valid_not_continuity_proof(self):
        with runtime() as m:
            b,d,_=fixture(m,'ABSENT','ABSENT');r=d.run_samples();closed=d.finish()
            self.assertTrue(m.state_samples.observation_complete(d,closed))
            self.assertTrue(all(not row['present'] for row in r['state_samples']))
            self.assertEqual(r['continuous_state'],'NOT_PROVED');self.assertEqual(r['handshake'],'NOT_OBSERVED')

    def test_missed_slot_is_not_caught_up_and_later_slot_remains_fixed(self):
        with runtime() as m:
            b,d,c=fixture(m);d.start('server');d.wait_accepted('server');anchor=b.t0;c.value=anchor+.6
            self.assertIsNone(d.start_sample('state.S1'))
            c.value=anchor+10;root=d.start_sample('state.S2');self.assertIsNotNone(root);d.wait('state.S2')
            result=d.summary();closed=d.finish()
            self.assertEqual(b.t0,anchor);self.assertEqual(d.client_creates,1)
            self.assertEqual(result['comparison'],'INSUFFICIENT_SAMPLES')
            self.assertFalse(m.state_samples.observation_complete(d,closed))
            with self.assertRaises(m.goal_contract.ContractError):d.start_sample('state.S1')

    def test_precreate_delay_does_not_shift_or_launch_missed_sample(self):
        with runtime() as m:
            b,d,c=fixture(m);d.start('server');d.wait_accepted('server');anchor=b.t0;pre=b.precreate
            def delayed(step,deadline):pre(step,deadline);c.value+=.6
            b.precreate=delayed
            self.assertIsNone(d.start_sample('state.S1'));self.assertEqual(d.client_creates,0)
            self.assertEqual(b.t0,anchor);self.assertEqual(len(b.roots),1);d.finish()

    def test_native_slot_check_uses_member_permit_before_call(self):
        with runtime() as m:
            a=m.goal_authorization;c=m.goal_contract;clock=pair.Clock();m.state_samples.time=types.SimpleNamespace(monotonic=clock)
            g=a.GoalGrant('a'*64,'b'*64,c.build_registry(builder.commands(c)),clock()+60)
            g.strict_state_samples=True;g.sample_windows={'state.S1':(clock(),clock()+.5)}
            p=a.StepPermit(g,g.registry['state.S1']);a._grants.add(g);a._permits.add(p)
            try:
                m.state_samples.check_creation_slot(p);clock.value+=.6
                with self.assertRaises(m.state_samples.SlotMissed):m.state_samples.check_creation_slot(p)
                a._permits.discard(p)
                with self.assertRaises(c.ContractError):m.state_samples.check_creation_slot(p)
            finally:a._grants.discard(g);a._permits.discard(p)

    def test_allocated_slot_loss_is_safe_only_before_any_native_call(self):
        for mode in ('allocated','created','call_started','close','result_record','skip_record'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,c=fixture(m);d.start('server');d.wait_accepted('server');anchor=b.t0
                create=b.create_and_resume;emit=b.journal.emit;old_deadline=b.grant.work_deadline
                def lose(root,step,deadline):
                    if step.id!='state.S1':return create(root,step,deadline)
                    if mode=='created':root.create()
                    if mode=='call_started':root.receipt['create_call_before_ns']=1
                    if mode=='close':root.bad_close=True
                    if mode=='result_record':b.fail_record=True
                    raise m.state_samples.SlotMissed('NOT_RUN_SLOT_MISSED')
                def fail_skip(row):
                    if 'state_sample' in row:
                        b.journal.failed=True;raise ValueError('SYNTHETIC_RECORD_FAILURE')
                    return emit(row)
                b.create_and_resume=lose
                if mode=='skip_record':b.journal.emit=fail_skip
                if mode=='allocated':
                    self.assertIsNone(d.start_sample('state.S1'));unused=b.roots[-1]
                    self.assertEqual((unused.create_calls,unused.stop_calls,unused.close_calls),(0,0,1))
                    self.assertFalse(b.grant.stopped);self.assertEqual(b.grant.work_deadline,old_deadline)
                    with self.assertRaises(m.goal_contract.ContractError):d.start_sample('state.S1')
                    c.value=anchor+10;d.start_sample('state.S2');d.wait('state.S2')
                    self.assertEqual(b.t0,anchor);self.assertEqual(d.client_creates,2)
                    self.assertEqual(d.summary()['comparison'],'INSUFFICIENT_SAMPLES')
                else:
                    with self.assertRaises((m.goal_contract.ContractError,ValueError)):d.start_sample('state.S1')
                    self.assertTrue(b.grant.stopped);self.assertNotIn('state.S1',d.skipped)
                    c.value=anchor+10
                    with self.assertRaises(m.goal_contract.ContractError):d.start_sample('state.S2')
                    self.assertNotIn('state.S2',d.used)
                self.assertFalse(m.state_samples.observation_complete(d,d.finish()))

    def test_server_and_client_auth_unknown_requests_and_denials_stop(self):
        for case,data in [('server',b'auth unknown\n'),('server',b'[E000002] request\n'),
                          ('server',b'[E000003] denied\n'),('state.S1',b'auth unknown\n')]:
            with self.subTest(case=case),runtime() as m:
                b,d,_=fixture(m);b.marker.feed(case,'stdout',data,'MONITOR',1,2)
                with self.assertRaises(m.goal_contract.ContractError):d.run_samples()
                self.assertEqual(d.client_creates,0);self.assertFalse(m.state_samples.observation_complete(d,d.finish()))

    def test_unauthorized_and_format_fault_stop_before_second(self):
        for mode in ('Unauthorized','malformed'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,_=fixture(m,mode)
                with self.assertRaises(m.goal_contract.ContractError):d.run_samples()
                self.assertNotIn('state.S2',d.used)
                self.assertFalse(m.state_samples.observation_complete(d,d.finish()))
                if mode=='Unauthorized':self.assertTrue(any('state_notice' in r for r in b.journal.rows))

    def test_query_timeout_budget_and_record_failure_do_not_continue(self):
        for mode in ('timeout','budget','record'):
            with self.subTest(mode=mode),runtime() as m:
                b,d,c=fixture(m);d.start('server');d.wait_accepted('server')
                if mode=='budget':d.deadline=c()+6
                if mode=='record':b.fail_record=True
                if mode=='timeout':
                    root=d.start_sample('state.S1');c.value=root.goal_started+5.1
                    with self.assertRaises(m.goal_contract.ContractError):d.wait('state.S1')
                elif mode=='record':
                    d.start_sample('state.S1')
                    with self.assertRaises((m.goal_contract.ContractError,ValueError)):d.wait('state.S1')
                else:
                    with self.assertRaises(m.goal_contract.ContractError):d.start_sample('state.S1')
                self.assertNotIn('state.S2',d.used);self.assertFalse(m.state_samples.observation_complete(d,d.finish()))

    def test_default_entry_and_member_wrong_backend_paths_are_denied(self):
        with runtime() as m,contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(m.goal_entry.main([]),78)
            a=m.goal_authorization;c=m.goal_contract;g=a.GoalGrant('a'*64,'b'*64,c.build_registry(builder.commands(c)),10000)
            a._grants.add(g)
            try:
                with self.assertRaises(c.ContractError):m.state_samples.actual_driver(g)
                with self.assertRaises(c.ContractError):m.goal_windows_backend.GoalWindowsBackend(g)
            finally:a._grants.discard(g)
            self.assertNotIn('goal_win32_owned',sys.modules)

    def test_actual_generated_native_read_never_archives_or_keeps_list(self):
        with runtime() as m:
            buffers,_=builder.source_buffers();tree=ast.parse(buffers['goal_win32_owned.py'])
            cls=next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='OwnedRoot')
            fn=next(n for n in cls.body if isinstance(n,ast.FunctionDef) and n.name=='read')
            data=b'FIXED_A\t\tUSB\tConnected\tCANARY_NAME\thdc\r\n';pending={11:data,22:b''}
            def peek(h,*args):
                if not pending[h]:ctypes.set_last_error(109);return False
                args[3]._obj.value=len(pending[h]);return True
            def read(h,buf,n,got,unused):
                chunk=pending[h][:n];pending[h]=pending[h][n:];ctypes.memmove(buf,chunk,len(chunk));got._obj.value=len(chunk);return True
            ns={'W':wintypes,'C':ctypes,'K':types.SimpleNamespace(PeekNamedPipe=peek,ReadFile=read),
                'need':m.goal_contract.need,'Halt':m.goal_contract.ContractError,'safe_clock':lambda:1}
            exec(compile(ast.fix_missing_locations(ast.Module(body=[fn],type_ignores=[])),'synthetic_native_read','exec'),ns)
            obj=types.SimpleNamespace(reads=[{'h':11,'eof':False,'name':'stdout'},{'h':22,'eof':False,'name':'stderr'}],
                captures=[m.state_samples.MemoryCapture(),m.state_samples.MemoryCapture()],output=[bytearray(),bytearray()],receipt={})
            parser=m.state_samples.Listing('FIXED_A')
            with patch.object(builtins,'open',side_effect=AssertionError('RAW_FILE_FORBIDDEN')):
                ns['read'](obj,65536,lambda stream,chunk,*args:parser.feed(chunk))
                ns['read'](obj,65536,lambda *args:None)
            self.assertEqual(obj.output,[bytearray(),bytearray()]);self.assertTrue(obj.receipt['EOF'])
            self.assertEqual(parser.finish()['device_state'],'CONNECTED')


if __name__=='__main__':unittest.main(verbosity=2)
