"""Regression for native controller completion versus child-exit observation."""
import sys
import json
import subprocess
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/standards'))
from run_ua_device_acceptance import CompleteCase
from ua_device_support import Session


class CompletionTests(unittest.IsolatedAsyncioTestCase):
    def case(self,process):
        case=object.__new__(CompleteCase)
        case.ensure_peer=lambda:None
        case.session=SimpleNamespace(sync=lambda:None)
        case.children=[(process,None,{})]
        return case

    async def test_success_exit_between_result_check_and_poll(self):
        process=SimpleNamespace(returncode=None)
        def poll():
            process.returncode=0
            return 0
        process.poll=poll
        # The first check sees a running process; poll then collects its actual
        # successful exit. Completion includes the required semantic receipt.
        receipt={'status':'PASS'}
        await self.case(process).wait(lambda:process.returncode==0 and receipt['status']=='PASS','semantic completion')

    async def test_zero_exit_without_semantic_result_fails(self):
        process=SimpleNamespace(returncode=0,poll=lambda:0)
        with self.assertRaisesRegex(RuntimeError,'early D5 child exit'):
            await self.case(process).wait(lambda:False,'missing receipt')

    async def test_nonzero_exit_fails(self):
        process=SimpleNamespace(returncode=3,poll=lambda:3)
        with self.assertRaisesRegex(RuntimeError,'early D5 child exit'):
            await self.case(process).wait(lambda:process.returncode==0,'failed oracle')


class CleanupTests(unittest.TestCase):
    @contextmanager
    def directory(self):
        parent=(Path(__file__).resolve().parents[1]/'build').resolve();parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='d5_cleanup_',dir=parent) as name:
            path=Path(name).resolve()
            self.assertTrue(path.is_relative_to(parent)) # Verify the automatic cleanup target.
            yield path

    def case(self,path,process,stream,cleanup=lambda:None):
        case=object.__new__(CompleteCase);case.path=path;case.peer_group=None;case.modules=[]
        record=dict(name='client',ua_only=True,directory=str(path),log=str(path/'process.log'),pid=process.pid,forced_cleanup=False)
        case.children=[(process,stream,record)]
        case.session=SimpleNamespace(sync=lambda:None,cleanup=cleanup,native=[])
        return case,record

    def test_timed_out_owned_child_is_terminated_and_reaped(self):
        with self.directory() as path:
            stream=(path/'process.log').open('wb')
            process=subprocess.Popen([sys.executable,'-B','-c','import time; time.sleep(60)'],stdout=stream,stderr=subprocess.STDOUT)
            class InitialTimeout:
                def __init__(self,owned):self.owned=owned;self.injected=False
                def __getattr__(self,name):return getattr(self.owned,name)
                def wait(self,timeout=None):
                    if timeout==12 and not self.injected:
                        self.injected=True
                        raise subprocess.TimeoutExpired(self.owned.args,timeout)
                    return self.owned.wait(timeout)
            try:
                case,record=self.case(path,InitialTimeout(process),stream)
                rows=case.finish()
                self.assertIsNotNone(process.poll())
                self.assertEqual(rows[0]['exit_code'],process.returncode)
                self.assertIsInstance(record['exit_code'],int)
                self.assertTrue(record['forced_cleanup'])
                self.assertTrue(stream.closed)
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
                stream.close()

    def test_unrelated_cleanup_error_preserves_natural_exit(self):
        with self.directory() as path:
            stream=(path/'process.log').open('wb')
            process=subprocess.Popen([sys.executable,'-B','-c','print("done",flush=True)'],stdout=stream,stderr=subprocess.STDOUT)
            process.wait(timeout=5)
            def failed_cleanup():raise RuntimeError('forwarder cleanup error')
            case,record=self.case(path,process,stream,failed_cleanup)
            with self.assertRaisesRegex(RuntimeError,'controller cleanup failed'):
                case.finish()
            self.assertEqual(record['exit_code'],0)
            self.assertFalse(record['forced_cleanup'])
            receipt=json.loads((path/'result.json').read_text(encoding='utf-8'))
            self.assertEqual(receipt['status'],'FAIL')
            self.assertIn('forwarder cleanup error',receipt['cleanup_error'])
            self.assertEqual(receipt['processes'][0]['exit_code'],0)
            self.assertFalse(receipt['processes'][0]['forced_cleanup'])
            self.assertTrue(stream.closed)

    def test_native_sync_error_still_collects_owned_process(self):
        with self.directory() as path:
            stream=(path/'process.log').open('wb')
            process=subprocess.Popen([sys.executable,'-B','-c','import time; time.sleep(60)'],stdout=stream,stderr=subprocess.STDOUT)
            class NativeChannel:
                text='';waits=0;terminations=0
                def __init__(self,owned):self.owned=owned
                @property
                def returncode(self):return self.owned.returncode
                def poll(self):return self.owned.poll()
                def wait(self,timeout=None):
                    self.waits+=1
                    if self.waits==1:raise subprocess.TimeoutExpired(self.owned.args,timeout)
                    return self.owned.wait(timeout)
                def terminate(self):self.terminations+=1;self.owned.terminate()
                def kill(self):self.owned.kill()
            channel=NativeChannel(process);session=object.__new__(Session)
            def failed_sync():raise RuntimeError('control mirror error')
            session.sync=failed_sync
            record=dict(target='test-native',directory=str(path),forced_cleanup=False)
            try:
                with self.assertRaisesRegex(RuntimeError,'D5 stop failed'):
                    session.stop((channel,stream,record))
                self.assertGreaterEqual(channel.waits,2)
                self.assertEqual(channel.terminations,1)
                self.assertIsNotNone(process.poll())
                self.assertEqual(record['exit_code'],process.returncode)
                self.assertTrue(record['collected'])
                self.assertTrue(record['forced_cleanup'])
                self.assertIn('control mirror error',record['control_error'])
                self.assertIn('collection_error',record)
                self.assertTrue(stream.closed)
            finally:
                if process.poll() is None:process.kill();process.wait(timeout=5)
                stream.close()

    def test_uncollected_native_exit_is_not_marked_collected(self):
        with self.directory() as path:
            class UnavailableChannel:
                returncode=None;text=''
                def poll(self):return None
                def wait(self,timeout=None):raise subprocess.TimeoutExpired('unavailable execution channel',timeout)
                def terminate(self):raise OSError('termination unavailable')
                def kill(self):raise OSError('kill unavailable')
            session=object.__new__(Session)
            def failed_sync():raise RuntimeError('control mirror error')
            session.sync=failed_sync
            record=dict(target='test-native',directory=str(path),forced_cleanup=False)
            with (path/'process.log').open('w') as stream:
                with self.assertRaisesRegex(RuntimeError,'D5 stop failed'):
                    session.stop((UnavailableChannel(),stream,record))
            self.assertFalse(record['collected'])
            self.assertIsNone(record['exit_code'])
            self.assertFalse(record['forced_cleanup'])
            self.assertIn('control mirror error',record['control_error'])
            self.assertIn('collection_error',record)


if __name__=='__main__':unittest.main()
