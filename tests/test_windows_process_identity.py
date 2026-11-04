"""Bounded CPU tests and one owned Windows child/CIM comparison per run."""
import copy
import ctypes
import datetime
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import windows_process_identity as identity

BASE = identity.NativeProcessIdentity(123, 134000000000000007,
    r'c:\fixture\python.exe', 'WINDOWS', 'fixture-user', 1)


class FakeMetadata:
    birth_source = identity.SOURCE
    resolution_100ns = 1

    def __init__(self, value=BASE):
        self.value = value
        self.calls = []
        self.changed_after = False

    def pid_birth(self, handle):
        self.calls.append(('birth', handle))
        count = sum(name == 'birth' for name, _ in self.calls)
        return self.value.pid, self.value.birth_100ns + int(self.changed_after and count > 1)

    def image(self, handle):
        self.calls.append(('image', handle))
        return self.value.image

    def owner_session(self, handle):
        self.calls.append(('owner', handle))
        return self.value.domain, self.value.user, self.value.session


class IdentityTests(unittest.TestCase):
    def test_integer_filetime_and_lossless_record(self):
        value = identity.combine_filetime(0xffffffff, 0xffffffff)
        self.assertEqual(value, 0xffffffffffffffff)
        self.assertEqual(identity.combine_filetime(0x12345678, 0x87654321), 0x1234567887654321)
        for birth in (BASE.birth_100ns, value):
            native = identity.NativeProcessIdentity(BASE.pid, birth, BASE.image,
                BASE.domain, BASE.user, BASE.session)
            payload = json.loads(json.dumps(native.to_record()))
            self.assertIs(type(payload['birth_100ns']), str)
            self.assertEqual(identity.NativeProcessIdentity.from_record(payload), native)
        for bad in (float(BASE.birth_100ns), True, 0, -1):
            with self.assertRaises(identity.IdentityBindingError):
                identity.NativeProcessIdentity(BASE.pid, bad, BASE.image, BASE.domain, BASE.user, 1)

    def test_coarse_or_unknown_source_rejected_before_api(self):
        for key, value in [('source', 'CIM.CreationDate'), ('source', 'UNKNOWN'),
                           ('unit', 'microseconds'), ('resolution_100ns', 10),
                           ('resolution_100ns', True), ('birth_100ns', BASE.birth_100ns),
                           ('birth_100ns', float(BASE.birth_100ns))]:
            record = BASE.to_record()
            record[key] = value
            api = FakeMetadata()
            with self.subTest(key=key, value=value), self.assertRaises(identity.IdentityBindingError):
                identity.verify_bound_handle(99, record, api)
            self.assertEqual(api.calls, [])
        api = FakeMetadata()
        api.birth_source = 'UNKNOWN'
        with self.assertRaises(identity.IdentityBindingError):
            identity.capture_native_identity(99, api)
        self.assertEqual(api.calls, [])

    def test_same_microsecond_bucket_one_tick_still_rejected(self):
        other = identity.NativeProcessIdentity(BASE.pid, BASE.birth_100ns + 1,
            BASE.image, BASE.domain, BASE.user, BASE.session)
        self.assertEqual(BASE.birth_100ns // 10, other.birth_100ns // 10)
        with self.assertRaises(identity.IdentityBindingError):
            identity.verify_bound_handle(99, BASE.to_record(), FakeMetadata(other))

    def test_full_identity_mismatch_and_unknown_fields(self):
        for key, value in [('pid', 124), ('image', r'c:\other\python.exe'),
                           ('domain', 'OTHER'), ('user', 'other-user'), ('session', 2)]:
            fields = dict(pid=BASE.pid, birth_100ns=BASE.birth_100ns, image=BASE.image,
                          domain=BASE.domain, user=BASE.user, session=BASE.session)
            fields[key] = value
            with self.subTest(key=key), self.assertRaises(identity.IdentityBindingError):
                identity.verify_bound_handle(99, BASE.to_record(), FakeMetadata(identity.NativeProcessIdentity(**fields)))
        for record in ({}, dict(BASE.to_record(), session=None),
                       dict(BASE.to_record(), owner={'domain': None, 'user': None})):
            with self.assertRaises(identity.IdentityBindingError):
                identity.expected_for_stop_core(record)

    def test_retained_handle_changes_and_adapter(self):
        api = FakeMetadata()
        api.changed_after = True
        with self.assertRaises(identity.IdentityBindingError):
            identity.capture_native_identity(99, api)
        api = FakeMetadata()
        self.assertEqual(identity.verify_bound_handle(99, BASE.to_record(), api), BASE)
        self.assertTrue(all(handle == 99 for _, handle in api.calls))
        expected = identity.expected_for_stop_core(BASE.to_record())
        self.assertEqual(expected['birth'], BASE.birth_100ns)
        self.assertEqual(expected['pid'], BASE.pid)

    @unittest.skipUnless(os.name == 'nt', 'Windows native fixture only')
    def test_owned_native_vs_cim_precision(self):
        # No PID-taking API exists in the query module: only this Popen handle.
        directory = os.environ.get('PROCESS_IDENTITY_EVIDENCE_DIR')
        self.assertIsNotNone(directory, 'set an owned evidence directory on E:')
        directory = Path(directory)
        self.assertEqual(directory.drive.lower(), 'e:')
        directory.mkdir(parents=True, exist_ok=False)
        api = identity.Win32ProcessMetadata()
        child = subprocess.Popen([sys.executable, '-I', '-B', '-c', 'import time; time.sleep(10)'],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.assertNotEqual(child.pid, 16072)
        handle = int(child._handle)  # Popen-created object, retained until finally.
        report = {'owned_child_pid': child.pid, 'before_poll': child.poll(),
                  'sample_limit': 1, 'HDC_targets_opened': 0}
        try:
            before = identity.capture_native_identity(handle, api)
            self.assertEqual(before.pid, child.pid)
            command = (
                "$ErrorActionPreference='Stop';"
                f"$p=Get-CimInstance Win32_Process -Filter 'ProcessId={child.pid}' "
                "-Property ProcessId,CreationDate;"
                "if(-not $p){exit 3};"
                "[ordered]@{pid=[int]$p.ProcessId;"
                "utc_display=$p.CreationDate.ToUniversalTime().ToString('o');"
                "dotnet_ticks=$p.CreationDate.ToUniversalTime().Ticks.ToString()}|ConvertTo-Json -Compress"
            )
            ps = Path('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe')
            try:
                query = subprocess.run([str(ps), '-NoLogo', '-NoProfile', '-NonInteractive',
                    '-Command', command], capture_output=True, timeout=8)
            except subprocess.TimeoutExpired:
                self.fail('OWNED_CIM_QUERY_TIMEOUT_NO_RETRY')
            self.assertEqual(query.returncode, 0, 'OWNED_CIM_QUERY_FAILED')
            self.assertLessEqual(len(query.stdout) + len(query.stderr), 4096)
            cim = json.loads(query.stdout.decode('utf-8-sig'))
            self.assertEqual(cim['pid'], child.pid)
            self.assertTrue(cim['dotnet_ticks'].isdigit())
            after_pid, after_birth = api.pid_birth(handle)
            self.assertEqual((after_pid, after_birth), (before.pid, before.birth_100ns))
            self.assertEqual(identity.NativeProcessIdentity.from_record(before.to_record()), before)
            # Diagnostic comparison only. Never construct a native record from it.
            cim_derived = int(cim['dotnet_ticks']) - 504911232000000000
            report.update(native_before=before.to_record(), native_after_birth=str(after_birth),
                cim_auxiliary={'source': 'CIM.CreationDate via .NET DateTime',
                    'declared_format_fraction_digits': 6, 'for_execution_identity': False, **cim},
                native_minus_cim_ticks=before.birth_100ns-cim_derived,
                native_submicrosecond_digit=before.birth_100ns % 10,
                nonzero_submicrosecond_observed=before.birth_100ns % 10 != 0,
                after_poll=child.poll())
        finally:
            report['cleanup_stop_requests'] = 0
            if child.poll() is None:
                child.terminate()  # This Popen-owned handle only, never a PID lookup.
                report['cleanup_stop_requests'] = 1
            report['cleanup_exit'] = child.wait(timeout=2)
            end_pid, end_birth = api.pid_birth(handle)
            report['terminal_native_pid_birth'] = dict(pid=end_pid, birth_100ns=str(end_birth))
            with (directory / 'OWNED_PRECISION_SAMPLE_D01.json').open('x', encoding='utf-8') as f:
                json.dump(report, f, indent=2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
