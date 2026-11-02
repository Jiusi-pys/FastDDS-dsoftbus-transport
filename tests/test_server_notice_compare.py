"""Public/synthetic data only. Never opens a project run or private output."""
from pathlib import Path
import contextlib
import hashlib
import importlib.util
import io
import os
import sys
import tempfile
import unittest
import uuid
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('server_notice_compare_tested', ROOT/'scripts/server_notice_compare.py')
m = importlib.util.module_from_spec(spec); sys.modules[spec.name] = m; spec.loader.exec_module(m)

# Independently transcribed public printf format and messages, not RULES-derived.
FIRST = b'[I][2026-11-02 00:00:00.000][aaaaaaa][GetDaemonAuthType:535] daemon auth type is rsa_3072_sha512 for 123******789(L:10) session\r\r\n'
UNSOURCED_SECOND = b'[W][2026-11-02 00:00:00][aaaaa][server.cpp:549] daemon did not confirm encrypt-tcp negotiation for 1******9 session, enforcement deferred to HandServerAuth/ServerSessionHandshake\r\r\n'
SECOND = b'[W][2026-11-02 00:00:00.000][aaaaaa][server.cpp:549] daemon did not confirm encrypt-tcp negotiation for 1*1 session, enforcement deferred to HandServerAuth/ServerSessionHandshake\r\r\n'


class FakeOps:
    def __init__(self, identity):
        self.identity = identity; self.used = False; self.opened = False
        self.reads = []; self.closes = 0; self.identities = 0; self.position = None
        self.short = False; self.changed = False; self.bad_close = False
    def claim(self):
        if self.used: raise ValueError('ALREADY_USED')
        self.used = True
    def path_identity(self, path): return self.identity
    def open(self, path): self.opened = True
    def handle_identity(self):
        self.identities += 1
        if self.changed: raise ValueError('SYNTHETIC_BINDING_CHANGE')
        return self.identity
    def seek(self, offset): self.position = offset
    def read(self, length):
        self.reads.append((self.position, length))
        data = FIRST if self.position == 10832 else SECOND
        return data[:-1] if self.short else data
    def close(self): self.closes += 1; return not self.bad_close


class ServerNoticeTests(unittest.TestCase):
    def test_fixed_whole_lengths_and_CRCRLF(self):
        self.assertEqual((len(FIRST), len(SECOND)), (130, 181))
        self.assertEqual(m.classify_server_record(FIRST)['template'], 'PUBLIC_AUTH_ALGORITHM_PROGRESS')
        self.assertEqual(m.classify_server_record(SECOND)['category'], 'PROTOCOL_WARNING_NOT_USER_REQUEST')
        for record in [FIRST, SECOND]:
            for wrong in [record[:-1], record+b'x', b'x'+record,
                          record.replace(b'daemon', b'DAEMON', 1), record[:-3]+b'\x00\r\r\n']:
                self.assertEqual(m.classify_server_record(wrong)['state'], 'UNKNOWN')

    def test_review_public_mask_and_logger_correlation(self):
        valid = FIRST.replace(b'123******789(L:10)', b'120******789(L:10)')
        without_debug = FIRST.replace(b'[aaaaaaa][GetDaemonAuthType:535]', b'')
        for data,state in [(valid,'PUBLIC_TEMPLATE_MATCH'),(UNSOURCED_SECOND,'UNKNOWN'),
                           (without_debug,'UNKNOWN')]:
            with self.subTest(state=state):
                self.assertEqual(m.classify_server_record(data)['state'],state)

    def test_mutually_exclusive_public_categories_and_unsupported_fields(self):
        cases = [(b'response auth signture success', 'PROGRESS_NOT_DEVICE_ACCEPTANCE'),
                 (b'Auth failed', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL'),
                 (b'[Fail][E000002]:The device unauthorized.', 'REQUEST_TEXT_NOT_OS_ATTESTATION'),
                 (b'The user denied the access for the device.', 'DENIAL_TEXT_NOT_OS_ATTESTATION')]
        for data, category in cases:
            for ending in [b'\n', b'\r\n', b'\r\r\n']:
                result = m.classify_server_record(data+ending)
                self.assertEqual(result['category'], category)
                self.assertEqual(result['state'], 'PUBLIC_TEMPLATE_MATCH')
                self.assertEqual(set(result), {'code','template','category','state','dynamic_fields_discarded'})
        for bad in [b'daemonauthstatus = SECRET_VALUE\n', b'handshake.buf = SECRET_VALUE\n',
                    b'Auth failed response auth signture success\n', b'\xffauth\n',
                    b'response auth signture success\nAuth failed\n',
                    FIRST.replace(b'[I]', b'[F]', 1), FIRST.replace(b'GetDaemonAuthType', b'UnknownFunction'),
                    FIRST.replace(b'123******789(L:10)', b'PRIVATE_SESSION'),
                    FIRST.replace(b'123******789(L:10)', b'0**5')]:
            self.assertEqual(m.classify_server_record(bad)['state'], 'UNKNOWN')

    def test_adapter_keeps_original_bytes_and_default_refusal(self):
        before = (ROOT/'scripts/bounded_public_compare.py').read_bytes()
        e = m.engine()
        self.assertEqual(e.RANGES, ((10832,130),(14131,181)))
        self.assertEqual((ROOT/'scripts/bounded_public_compare.py').read_bytes(), before)
        self.assertEqual(hashlib.sha256(before).hexdigest(), m.ENGINE_SHA256)
        with contextlib.redirect_stdout(io.StringIO()): self.assertEqual(m.main(), 78)
        for size in [193, 14311, 14313]:
            with self.assertRaises(e.ControlledFailure): e.Identity(1,2,size,3,32,1).validate()
        with patch.object(Path, 'read_bytes', return_value=b'NOT_THE_PUBLIC_ENGINE'):
            with self.assertRaises(ValueError): m.engine()

    def test_reused_engine_reads_only311_and_is_not_reentrant(self):
        e = m.engine(); identity = e.Identity(1,2,14312,3,32,1)
        ops = FakeOps(identity); s = e.ReadSpec('C:/SYNTHETIC_SCOPE/fixture.bin', identity)
        result = e.compare_once(s, ops)
        self.assertEqual(result['error'], 'NONE'); self.assertEqual(result['requested_bytes'], 311)
        self.assertEqual(ops.reads, [(10832,130),(14131,181)])
        self.assertTrue(result['closed']); self.assertEqual(ops.identities, 5)
        reads = list(ops.reads); again = e.compare_once(s, ops)
        self.assertEqual(again['outcome'], 'UNKNOWN'); self.assertEqual(ops.reads, reads)
        for mode in ['short','changed','bad_close']:
            o = FakeOps(identity); setattr(o, mode, True)
            value = e.compare_once(s, o)
            self.assertEqual(value['outcome'], 'UNKNOWN'); self.assertEqual(value['records'], [])

    @unittest.skipUnless(os.name == 'nt', 'Windows-only synthetic native read')
    def test_owned_synthetic_native_reads_and_native_binding_negative(self):
        folder = Path(tempfile.gettempdir())/('Codex-server-notice-SYNTHETIC-'+uuid.uuid4().hex)
        folder.mkdir(); path = folder/'SYNTHETIC_public_fixture.bin'
        try:
            data = bytearray(b'X'*14312); data[10832:10962] = FIRST; data[14131:14312] = SECOND
            with path.open('xb') as f: f.write(data)
            e = m.engine(); probe = e.Win32ReadOps(str(path)); probe.claim(); probe.open(str(path))
            expected = probe.handle_identity(); C,W,k,FI = probe.api; info = FI()
            self.assertTrue(k.GetFileInformationByHandle(probe.handle, C.byref(info)))
            self.assertTrue(probe.close())
            native = dict(volume=int(info.volume), file_index=(int(info.indexhi)<<32)|int(info.indexlo),
                          write_100ns=(int(info.written.high)<<32)|int(info.written.low))
            ops = m.native_ops(e, str(path), **native); lengths = []; read = ops.read
            def bounded(length): lengths.append(length); return read(length)
            ops.read = bounded
            result = e.compare_once(e.ReadSpec(str(path), expected), ops)
            self.assertEqual(result['error'], 'NONE'); self.assertEqual(lengths, [130,181])
            self.assertEqual(result['interpretation'], 'PUBLIC_TEMPLATE_MATCHES_ONLY')
            self.assertTrue(result['closed'])
            wrong = dict(native, file_index=native['file_index']+1)
            bad = m.native_ops(e, str(path), **wrong)
            rejected = e.compare_once(e.ReadSpec(str(path), expected), bad)
            self.assertEqual(rejected['read_calls'], 0); self.assertTrue(rejected['closed'])
            self.assertEqual(rejected['outcome'], 'UNKNOWN')
        finally:
            if path.exists(): path.unlink()  # only this newly owned synthetic file
            folder.rmdir()


if __name__ == '__main__': unittest.main(verbosity=2)
