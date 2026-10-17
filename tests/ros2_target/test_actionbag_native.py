"""Focused Host fixtures only; modeled text is never native execution evidence."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('actionbag_native_fixture_subject', Path(__file__).with_name('run_actionbag_native.py'))
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


class ActionbagNativeFixtures(unittest.TestCase):
    def test_actionbag_native(self):
        with self.subTest('stat parenthesized name and starttime'):
            line = '123 (name with ) spaces) S 42 ' + '0 ' * 17 + '987 0'
            self.assertEqual(M.actionbag_proc_stat(line), {'pid': 123, 'ppid': 42, 'starttime': 987, 'state': 'S'})
            with self.assertRaises(ValueError):
                M.actionbag_proc_stat('123 missing comm')
        with self.subTest('duplicate fields'):
            with self.assertRaises(ValueError):
                M.actionbag_fields('X run=one run=two', 'X')
        prefix = '/data/local/tmp/mdds_dds_0123456789abcdef_A'
        names = ['libfastrtps.so.2.14', 'libfastcdr.so.2', 'librmw_fastrtps_cpp.so', 'libfastdds_dsoftbus_ros2_target_bridge.so', 'libmdds_actionbag_token_bridge.so', 'libc++_shared.so']
        lines = ['100-200 rw-p 0 00:00 0 [anon:ld-musl-aarch64.so.1.bss]'] + ['100-200 r-xp 0 00:00 1 ' + prefix + '/' + name for name in names]
        lines += ['100-200 r-xp 0 00:00 1 /system/lib64/platformsdk/libsoftbus_client.z.so', '100-200 r-xp 0 00:00 1 /system/lib64/libhilog.z.so']
        with self.subTest('real paths versus anonymous BSS; system transitives observed'):
            mapped = M.actionbag_verify_maps('\n'.join(lines), prefix, names)
            self.assertNotIn('[anon:ld-musl-aarch64.so.1.bss]', mapped)
            self.assertIn('/system/lib64/libhilog.z.so', mapped)
            for bad in ['/data/local/tmp/ros2-generic/Lib/libfastrtps.so.2.14', '/system/lib64/libfastcdr.so.2', prefix + '/libfastcdr.so.2 (deleted)', 'relative.so']:
                with self.assertRaises(ValueError):
                    M.actionbag_verify_maps('\n'.join(lines + ['100-200 r-xp 0 00:00 1 ' + bad]), prefix, names)
        peer = '\n'.join(['PEER_TASKS pid=123 tid=123 count=1 complete=1', 'PEER_TOKEN stage=original_token matches=0 rc=0',
            'PEER_TOKEN stage=select_token matches=1 rc=0', 'PEER_IDENTITY bridge=0 called=15 granted=1', 'PEER_LOCAL rc=0', 'PEER_ONLINE rc=0 count=1 allocation=1',
            'PEER_BUFFER released=1 allocation=1', 'PEER_UDID rc=0 terminated=1 valid=1 bytes=6 exported=1 mode=0600 identity_bytes=NOT_LOGGED',
            'route_kind=local network_id=' + 'a' * 64, 'route_kind=remote index=0 network_id=' + 'b' * 64,
            'PEER_DIRECTORY valid=1 remotes=1 free_returned=1 cap=32 communication=NOT_RUN',
            'PEER_TOKEN stage=restore_token matches=1 rc=0', 'PEER_QUERY_FINAL success=1 admitted=1 restored=1 socket_calls=0 provisioning_calls=0'])
        with self.subTest('peer free, restoration, identity, terminated export'):
            self.assertEqual(M.actionbag_parse_peer(peer)['remotes'], ['b' * 64])
            for old, new in [('restored=1', 'restored=0'), ('released=1', 'released=0'), ('terminated=1', 'terminated=0'), ('granted=1', 'granted=0')]:
                with self.assertRaises(ValueError):
                    M.actionbag_parse_peer(peer.replace(old, new))
        run = '0123456789abcdef'
        texts = {}
        for side, other in [('A', 'B'), ('B', 'A')]:
            pid = {'A': 10, 'B': 20}[side]
            identity = 'run=' + run + ' side=' + side
            text = ['ROS_NATIVE_SUMMARY ' + identity + ' send_enter=1 send_return=1 send_error=0 ingress=1 detailed_stored=2 metadata_limit=8192 overflow=0 malformed=0 sdk_epoch=NOT_PROVEN']
            for direction, writer in [('SDK_SEND', side.lower() * 32), ('SDK_INGRESS', other.lower() * 32)]:
                text.append('ROS_NATIVE_DATA ' + identity + ' observation=1 direction=' + direction + ' socket=1 writer=' + writer + ' sequence_hi=0 sequence_lo=1 scope=STRUCTURAL_APP_OBSERVER')
            text.append('ROS_STARTUP_CAPTURE ' + identity + ' source=REAL_TARGET completeness=CAPTURED overflow=0 output_errors=0 text_truncated=0 first_observation_seq=0 failure_ref=0 stored=1 limit=256 omitted=99')
            text.append('ROS_STARTUP_EVENT ' + identity + ' source=REAL_TARGET truncated=0 operation=Socket_return')
            for op in ['Socket', 'Listen', 'Bind', 'SendBytes', 'Shutdown']:
                semantics = 'VOID_NO_RC' if op == 'Shutdown' else 'HANDLE_OR_ERROR' if op == 'Socket' else 'SDK_RC'
                text.append('ROS_STARTUP_OPERATION ' + identity + ' operation=' + op + ' entered=1 returned=1 state=RETURN_OBSERVED return_semantics=' + semantics + ' source=REAL_TARGET')
            index = str('AB'.index(side))
            text += ['FDDST_ROS_PRE_PARTICIPANT run=' + run + ' side=' + index + ' builtin=0 intraprocess=0 mode=OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
                'FDDST_ENDPOINT role=writer data_sharing=OFF topic=fixture', 'FDDST_REAL_SOCKET run=' + run + ' side=' + index + ' fake_sdk=0',
                'FDDST_ROS_CLOSED run=' + run + ' side=' + index + ' pump_joined=1 runtime_closed=1 local_only=1', 'ROS_IDENTITY_FINAL restored=1 business_rc=0',
                'ROS_TEARDOWN_CAPTURE ' + identity + ' pid=' + str(pid) + ' completeness=CAPTURED overflow=0 output_errors=0',
                'ROS_TEARDOWN_EVENT schema=1 ' + identity + ' pid=' + str(pid) + ' operation=Factory_delete_participant returned=1 value_type=PUBLIC_RETURN_CODE value=0']
            texts[side] = '\n'.join(text)
        with self.subTest('modeled native structural ledger, not device evidence'):
            self.assertEqual(M.actionbag_native_capture(texts, run, {'A': 10, 'B': 20})['sides']['A']['peer_matched_writer_sequences'], 1)
            self.assertEqual(M.actionbag_native_capture({s: t.replace('socket=1', 'socket=0') for s, t in texts.items()}, run, {'A': 10, 'B': 20})['sides']['B']['peer_matched_writer_sequences'], 1)
            self.assertEqual(M.actionbag_native_capture({s: t.replace('socket=1', 'socket=2147483647') for s, t in texts.items()}, run, {'A': 10, 'B': 20})['sides']['B']['peer_matched_writer_sequences'], 1)
            for bad in ['-1', '2147483648']:
                with self.assertRaises(ValueError):
                    M.actionbag_native_capture({**texts, 'A': texts['A'].replace('socket=1', 'socket=' + bad)}, run, {'A': 10, 'B': 20})
            for old, new in [('send_return=1', 'send_return=0'), ('overflow=0', 'overflow=1'), ('sdk_epoch=NOT_PROVEN', 'sdk_epoch=PROVEN'),
                             ('source=REAL_TARGET', 'source=HOST_FAKE_INJECTION'), ('VOID_NO_RC', 'SDK_RC'), ('first_observation_seq=0', 'first_observation_seq=1'),
                             ('builtin=0', 'builtin=1'), ('restored=1', 'restored=0'), ('value=0', 'value=1'), ('detailed_stored=2', 'detailed_stored=1')]:
                with self.assertRaises(ValueError):
                    M.actionbag_native_capture({**texts, 'A': texts['A'].replace(old, new)}, run, {'A': 10, 'B': 20})
            with self.assertRaises(ValueError):
                M.actionbag_native_capture({**texts, 'B': texts['B'].replace('writer=' + 'a' * 32, 'writer=' + 'f' * 32)}, run, {'A': 10, 'B': 20})
        runner = M.ActionbagNativeRunner.__new__(M.ActionbagNativeRunner)
        runner.owned = {'A': {'child': {'pid': 123, 'ppid': 42, 'starttime': 987, 'exe': prefix + '/ros2_action_acceptance', 'argv': [prefix + '/ros2_action_acceptance', 'A', '0123456789abcdef']}}}
        expected = runner.owned['A']['child']
        with self.subTest('PID reuse and argv mismatch refuse signals/releases'):
            with patch.object(runner, 'snapshot', return_value=dict(expected)):
                self.assertTrue(runner.guard('A'))
            for change in [{'starttime': 988}, {'exe': '/system/bin/sh'}, {'argv': ['unrelated']}]:
                with patch.object(runner, 'snapshot', return_value={**expected, **change}):
                    with self.assertRaises(OSError):
                        runner.guard('A')
            with patch.object(runner, 'snapshot', return_value=None):
                self.assertFalse(runner.guard('A', absent_ok=True))
        with tempfile.TemporaryDirectory(prefix='actionbag-fixture-', dir=Path(__file__).resolve().parents[2] / 'build') as directory:
            runner.output, runner.sequence = Path(directory), 0
            runner.stage_deadline = None
            runner.args = SimpleNamespace(hdc=Path(M.HDCPATH))
            with self.subTest('host exit zero is not transfer success'):
                with patch.object(M.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, b'[Fail] transfer failed', b'')):
                    with self.assertRaises(OSError):
                        runner.command('A', ['file', 'send', 'C:\\short\\p.tar', prefix + '.upload.tar'])
            with self.subTest('missing remote marker is infrastructure failure'):
                with patch.object(runner, 'command', return_value=b'command output without remote status'):
                    with self.assertRaises(OSError):
                        runner.shell('A', 'true')
            with self.subTest('HDC client timeout recorded separately'):
                with patch.object(M.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['hdc'], 1, output=b'partial')):
                    with self.assertRaises(OSError):
                        runner.command('A', ['shell', 'true'], timeout=1)


if __name__ == '__main__':
    unittest.main()
