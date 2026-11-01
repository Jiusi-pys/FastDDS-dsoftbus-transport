"""Synthetic-only tests. No real target path or private metadata is an input."""
import contextlib
import importlib.util
import io
import json
import os
import stat
from types import SimpleNamespace
from pathlib import Path
import sys
import unittest
import uuid
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]

def load_module():
    name='bounded_public_compare_under_test';spec=importlib.util.spec_from_file_location(name,ROOT/'scripts/bounded_public_compare.py')
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod);return mod
m=load_module()
IDENTITY=m.Identity(7,8,193,123456789,32,1)
FAKE_PATH='E:/SYNTHETIC_IN_MEMORY_ONLY.bin'

class SecretError(Exception):
    def __str__(self):raise AssertionError('STR_FORBIDDEN')
    def __repr__(self):raise AssertionError('REPR_FORBIDDEN')

class FakeOps:
    def __init__(self):
        self.used=False;self.opened=False;self.closes=0;self.path_calls=0;self.ident_calls=0
        self.reads=[];self.seeks=[];self.pos=0;self.fail=None;self.change_at=None
        self.data=bytearray(b'X'*193)
        first=b'[Fail][E000004]:The communication channel is being established.'
        self.data[:len(first)]=first
        self.data[65:115]=b'OUTSIDE_CANARY'.ljust(50,b'X')
        self.data[115:192]=b'auth INSIDE_CANARY'.ljust(77,b'X')
    def claim(self):
        if self.used:raise SecretError('CANARY')
        self.used=True
    def path_identity(self,path):
        self.path_calls+=1
        if self.fail=='path':raise SecretError('CANARY')
        if self.fail=='path_changed' and self.path_calls>1:return m.Identity(7,9,193,123456789,32,1)
        return IDENTITY
    def open(self,path):
        self.opened=True
        if self.fail=='open':raise SecretError('CANARY')
    def handle_identity(self):
        self.ident_calls+=1
        if self.fail=='identity':raise SecretError('CANARY')
        if self.ident_calls==self.change_at:return m.Identity(7,9,193,123456789,32,1)
        return IDENTITY
    def seek(self,pos):
        if self.fail=='seek':raise SecretError('CANARY')
        self.pos=pos;self.seeks.append(pos)
    def read(self,size):
        self.reads.append((self.pos,size))
        if self.fail=='read':raise SecretError('CANARY')
        result=bytes(self.data[self.pos:self.pos+size])
        return result[:-1] if self.fail=='short' else result
    def close(self):
        self.closes+=1
        if self.fail=='close':raise SecretError('CANARY')
        return True

class CompareTests(unittest.TestCase):
    def spec(self,identity=IDENTITY):return m.ReadSpec(FAKE_PATH,identity)
    def clean_output(self,result):
        text=json.dumps(result)
        self.assertTrue('CANARY' not in text)
        self.assertTrue('SYNTHETIC_IN_MEMORY' not in text)
        self.assertFalse(result['further_action_authorized'])
    def test_public_codes_and_exact_whole_templates(self):
        for data,code,kind in ((b'[Fail][E000002]:The device unauthorized.','E000002','PUBLIC_REQUEST_HEAD'),
            (b'[Fail][E000003]:The device unauthorized.','E000003','PUBLIC_DENIAL_HEAD'),
            (b'[Fail][E000004]:The communication channel is being established.','E000004','PUBLIC_CHANNEL_PENDING'),
            (b'response auth signture success','OTHER_OR_UNMATCHED','PUBLIC_AUTH_DIAGNOSTIC')):
            for ending in (b'',b'\r'):
                value=m.classify_record(data+ending);self.assertEqual(value['code'],code)
                self.assertEqual(value['template'],kind)
        self.assertEqual(m.classify_record(b'Please check for a confirmation dialog on your device.')['template'],'PUBLIC_REQUEST_INSTRUCTION')
        self.assertEqual(m.classify_record(b'The user denied the access for the device.')['template'],'PUBLIC_DENIAL_SENTENCE')
    def test_unknown_mixed_codes_and_sensitive_markers_never_leak(self):
        for data in (b'[E000002] [E000004] INSIDE_CANARY',b'auth INSIDE_CANARY',b'\xffINSIDE_CANARY'):
            value=m.classify_record(data);self.assertEqual(value['state'],'UNKNOWN')
            self.assertTrue('CANARY' not in json.dumps(value))
        self.assertEqual(m.classify_record(b'[E000002] [E000004]')['code'],'OTHER_OR_UNMATCHED')
        o=FakeOps();v=m.compare_once(self.spec(),o);self.assertEqual(v['outcome'],'COMPARED_METADATA_ONLY')
        self.assertEqual(o.reads,[(0,65),(115,77)]);self.assertEqual(v['requested_bytes'],142)
        self.clean_output(v)
    def test_pre_read_identity_link_reparse_and_failures_are_zero_reads(self):
        for failure in ('path','open','identity','seek'):
            with self.subTest(failure=failure):
                o=FakeOps();o.fail=failure;v=m.compare_once(self.spec(),o)
                self.assertEqual(v['outcome'],'UNKNOWN');self.assertEqual(v['read_calls'],0)
                self.assertEqual(o.closes,1);self.clean_output(v)
        for bad in (m.Identity(7,8,193,123456789,32,2),m.Identity(7,8,193,123456789,1056,1),
                    m.Identity(7,8,192,123456789,32,1),m.Identity(7,9,193,123456789,32,1)):
            o=FakeOps();v=m.compare_once(self.spec(bad),o)
            self.assertEqual(v['read_calls'],0);self.clean_output(v)
    def test_identity_changes_after_open_and_each_range_reject_output(self):
        for call,reads in ((1,0),(3,1),(5,2)):
            o=FakeOps();o.change_at=call;v=m.compare_once(self.spec(),o)
            self.assertEqual(v['outcome'],'UNKNOWN');self.assertEqual(v['read_calls'],reads)
            self.assertEqual(v['records'],[]);self.assertEqual(o.closes,1)
        o=FakeOps();o.fail='path_changed';v=m.compare_once(self.spec(),o)
        self.assertEqual(v['error'],'PATH_IDENTITY_CHANGED');self.assertEqual(v['records'],[])
    def test_short_read_and_read_exception_do_not_retry(self):
        for failure,error in (('short','SHORT_OR_INVALID_READ'),('read','READ_FAILED')):
            o=FakeOps();o.fail=failure;v=m.compare_once(self.spec(),o)
            self.assertEqual(v['error'],error);self.assertEqual(o.reads,[(0,65)])
            self.assertEqual(v['requested_bytes'],65);self.assertEqual(v['records'],[]);self.clean_output(v)
    def test_close_error_and_reuse_keep_fixed_errors_without_repeat_effects(self):
        o=FakeOps();o.fail='close';v=m.compare_once(self.spec(),o)
        self.assertEqual(v['error'],'CLOSE_UNKNOWN');self.assertEqual(v['close_error'],'CLOSE_UNKNOWN')
        before=(list(o.reads),o.closes,o.path_calls);again=m.compare_once(self.spec(),o)
        self.assertEqual(again['error'],'ALREADY_USED');self.assertEqual(before,(o.reads,o.closes,o.path_calls))
        self.clean_output(v);self.clean_output(again)
    def test_import_and_default_cli_deny_before_any_target_IO(self):
        with patch('ctypes.WinDLL',side_effect=AssertionError('DLL_FORBIDDEN'),create=True):
            other=load_module()
            with patch.object(other.Win32ReadOps,'path_identity',side_effect=AssertionError('STAT_FORBIDDEN')):
                with patch.object(other.Win32ReadOps,'open',side_effect=AssertionError('OPEN_FORBIDDEN')):
                    out=io.StringIO()
                    with contextlib.redirect_stdout(out):rc=other.main(['--actual','SYNTHETIC_NOT_A_TARGET'])
                    self.assertEqual(rc,78);self.assertEqual(json.loads(out.getvalue())['target_access'],0)
    def test_outside_path_is_lexically_refused_without_stat_or_open(self):
        ops=m.Win32ReadOps(FAKE_PATH)
        with patch('os.lstat',side_effect=AssertionError('STAT_FORBIDDEN')) as query:
            with self.assertRaises(m.ControlledFailure):ops.path_identity('Z:/NEVER_PROBE_OTHER.bin')
            query.assert_not_called()
        with patch('ctypes.WinDLL',side_effect=AssertionError('DLL_FORBIDDEN'),create=True):
            with self.assertRaises(m.ControlledFailure):ops.open('Z:/NEVER_PROBE_OTHER.bin')
    def test_ancestor_guard_is_root_first_and_uses_only_canonical_path(self):
        allowed='E:/SYNTHETIC_SCOPE/link/sub/synthetic.bin';ops=m.Win32ReadOps(allowed);calls=[]
        def meta(path):
            value=m.canonical_path(str(path));calls.append(value)
            return SimpleNamespace(st_file_attributes=1024 if value.endswith('\\link') else 16)
        with patch('os.lstat',side_effect=meta):
            with self.assertRaises(m.ControlledFailure):ops.path_identity(allowed)
        self.assertTrue(calls[-1].endswith('\\link'))
        self.assertFalse(any('\\link\\sub' in q for q in calls))
        ops=m.Win32ReadOps('E:/SYNTHETIC_SCOPE/ok/synthetic.bin');calls=[]
        def ordinary(path):
            value=m.canonical_path(str(path));calls.append(value);leaf=value==ops.allowed
            return SimpleNamespace(st_file_attributes=32 if leaf else 16,st_mode=stat.S_IFREG if leaf else stat.S_IFDIR,
                st_dev=7,st_ino=8,st_size=193,st_mtime_ns=123456789,st_nlink=1)
        with patch('os.lstat',side_effect=ordinary):
            self.assertEqual(ops.path_identity('E:/SYNTHETIC_SCOPE/ignored/../ok/synthetic.bin'),IDENTITY)
        self.assertFalse(any('ignored' in q for q in calls))
        self.assertEqual(calls[-1],ops.allowed)
    def test_real_Win32_single_owned_synthetic_file_two_ReadFile_calls_only(self):
        # No environment/actual-path fallback. This is the only disk fixture.
        folder=Path('E:/Codex-FastDDS-bounded-public-compare-20261102-d01/fixtures')
        for parent in (folder,*folder.parents):self.assertFalse(os.lstat(parent).st_file_attributes&1024)
        own=folder/('owned_'+uuid.uuid4().hex);own.mkdir();path=own/'synthetic.bin'
        data=bytes(FakeOps().data)
        with path.open('xb') as stream:stream.write(data)
        class Spy(m.Win32ReadOps):
            def __init__(self,p):super().__init__(p);self.requests=[];self.last_seek=None
            def open(self,p):
                super().open(p);native=self.api[2].ReadFile
                def counted(h,buf,n,got,over):
                    self.requests.append((self.last_seek,n));return native(h,buf,n,got,over)
                self.api[2].ReadFile=counted
            def seek(self,offset):super().seek(offset);self.last_seek=offset
        ops=Spy(str(path));expected=ops.path_identity(str(path))
        value=m.compare_once(m.ReadSpec(str(path),expected),ops)
        self.assertEqual(value['outcome'],'COMPARED_METADATA_ONLY')
        self.assertEqual(ops.requests,[(0,65),(115,77)]);self.assertEqual(value['requested_bytes'],142)
        self.assertTrue(value['closed']);self.assertTrue(value['opened'])
        self.assertEqual(value['records'][0]['code'],'E000004');self.assertEqual(value['interpretation'],'UNKNOWN');self.clean_output(value)
        # Keep this unique193B fixture; no cleanup of user/old data.

if __name__=='__main__':unittest.main(verbosity=2)
