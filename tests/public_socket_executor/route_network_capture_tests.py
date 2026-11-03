"""Pure local route collector fixtures. No HDC invocation or platform events."""
import hashlib,importlib.util,json,math,sys,tempfile,unittest
from pathlib import Path
from unittest import mock

REPO=Path(__file__).resolve().parents[2]
SOURCE=REPO/'scripts/public_socket/shared_reattach/executor/route-network-capture.py'
spec=importlib.util.spec_from_file_location('route_capture_fixture',SOURCE)
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)

class OwnedChild:
    def __init__(self):self.stdout=mock.Mock();self.stderr=mock.Mock();self.pid=4321;self.kill=mock.Mock();self.wait=mock.Mock()
    def poll(self):return 0

class RouteCaptureTests(unittest.TestCase):
    def setUp(self):
        self.scratch=tempfile.TemporaryDirectory();self.addCleanup(self.scratch.cleanup)
        self.root=Path(self.scratch.name);self.task=self.root/'task';self.window=self.task/'windows/window';self.window.mkdir(parents=True)
        self.original=self.task/'executor/route-network-capture.py';self.original.parent.mkdir();self.original.write_bytes(SOURCE.read_bytes())
        self.copy=self.window/'route-network-capture.py';self.copy.write_bytes(SOURCE.read_bytes())
        self.collector=self.task/'recovery/pregrant_recovery.py';self.collector.parent.mkdir()
        self.marker=self.root/'imported'
        self.driver=self.root/'hdc.exe';self.driver.write_bytes(b'NEVER_EXECUTED_OWNED_FIXTURE')
        self.collector.write_text('from pathlib import Path\nPath('+repr(str(self.marker))+').write_text("imported")\n'
            'class CaptureLimitExceeded(Exception):\n def __init__(self):self.stdout=b"prefix";self.stderr=b"error"\n'
            'def process_identity(pid):return {"pid":pid,"birth":"fixture"}\n'
            'def collect_child(child,timeout,limit):\n assert 0<timeout<=15 and limit==1048576\n return b"raw",b"",False,True\n',encoding='utf-8')
        self.freeze=self.task/'CONTRACT.json';self.cfg=self.window/'window.json';self.save_binding()
        patch=mock.patch.object(helper,'__file__',str(self.copy));patch.start();self.addCleanup(patch.stop)
    def save_binding(self):
        h=helper.sha(self.original);c=helper.sha(self.collector)
        binding={'helper_sha256':h,'collector_sha256':c}
        private={'schema':1,'task_root':str(self.task),'repo_root':str(self.root),'hdc_path':str(self.driver),'targets':{'A':'PRIVATE_FIXTURE_TARGET'},'route_lifecycle_native':{}}
        private_path=self.task/'PRIVATE_CONFIG.json';private_path.write_text(json.dumps(private));private_hash=helper.sha(private_path)
        (self.task/'MATERIALIZATION.json').write_text(json.dumps({'private_config_sha256':private_hash}))
        frozen={'task_root':str(self.task),'private_config_sha256':private_hash,'device_driver':{'path':str(self.driver),'sha256':helper.sha(self.driver)},'route_network_capture':binding,'route_lifecycle_native':{},'execution_files':[{'path':str(self.original),'sha256':h},{'path':str(self.collector),'sha256':c}]}
        self.freeze.write_text(json.dumps(frozen))
        cfg={'repo':str(self.root),'window_root':str(self.window),'private_config_sha256':private_hash,'route_lifecycle_native':{},'hdc_path':str(self.driver),'targets':private['targets'],
            'task_contract':{'freeze_path':str(self.freeze),'freeze_sha256':helper.sha(self.freeze)},'route_network_capture':binding,'executor_sha256':{'route-network-capture.py':h}}
        self.cfg.write_text(json.dumps(cfg))
    def invoke(self,step='route-eth1-finally-up',budget='15'):
        child=OwnedChild()
        with mock.patch.object(sys,'argv',['fixture','--window',str(self.cfg),'--step',step,'--budget-seconds',budget]),mock.patch.object(helper.subprocess,'Popen',return_value=child) as spawn:
            helper.main()
        return child,spawn
    def test_exact_whitelist(self):
        self.assertEqual(helper.command_for('route-usb-adjacent','driver','target'),['driver','list','targets','-v'])
        self.assertEqual(helper.command_for('route-eth1-finally-up','driver','target'),['driver','-t','target','shell','ip link set dev eth1 up'+helper.WRAPPER])
        for phase in ['baseline']+['finally-'+str(i) for i in range(10)]:
            for kind,command in [('link','ip link show dev eth1'),('addr','ip -4 addr show dev eth1'),('routes','ip route show')]:
                self.assertEqual(helper.command_for('route-eth1-'+phase+'-'+kind,'driver','target')[-1],command+helper.WRAPPER)
    def test_arbitrary_scope_rejected(self):
        for step in ['route-eth0-finally-up','route-eth1-finally-down','route-eth1-finally-10-link','route-eth1-baseline-link; reboot','route-usb-adjacent --target other','anything']:
            with self.assertRaises(ValueError):helper.command_for(step,'driver','target')
    def test_tampered_self_before_import(self):
        self.copy.write_text('tampered')
        with self.assertRaises(ValueError):self.invoke()
        self.assertFalse(self.marker.exists())
    def test_tampered_collector_before_import(self):
        self.collector.write_text('raise AssertionError("must not import")')
        with self.assertRaises(ValueError):self.invoke()
        self.assertFalse(self.marker.exists())
    def test_deadline_bounds_before_import_or_spawn(self):
        for budget in ['0','-1','15.1','nan','inf']:
            with self.assertRaises(ValueError):self.invoke(budget=budget)
        self.assertFalse(self.marker.exists())
    def test_bounded_complete_private_receipts(self):
        child,spawn=self.invoke(budget='0.25');root=self.window/'A/route-eth1-finally-up'
        receipt=json.loads((root/'result.json').read_text())
        self.assertTrue(receipt['capture_complete']);self.assertFalse(receipt['timeout']);self.assertEqual(receipt['max_output_bytes'],1048576)
        self.assertEqual(receipt['command_seconds'],0.25);self.assertEqual((root/'stdout.log').read_bytes(),b'raw')
        self.assertEqual(json.loads((root/'command.json').read_text()),helper.command_for('route-eth1-finally-up',str(self.driver),'PRIVATE_FIXTURE_TARGET'))
        child.kill.assert_not_called();child.stdout.close.assert_called_once();spawn.assert_called_once()
    def change_collector(self,body):
        text=self.collector.read_text();text=text[:text.index('def collect_child')]+body
        self.collector.write_text(text);self.save_binding()
    def test_timeout_is_incomplete_sticky(self):
        self.change_collector('def collect_child(child,timeout,limit):return b"partial",b"",True,False\n')
        with self.assertRaises(ValueError):self.invoke()
        result=json.loads((self.window/'A/route-eth1-finally-up/result.json').read_text())
        self.assertTrue(result['timeout']);self.assertFalse(result['capture_complete']);self.assertFalse(result['remote_termination_proven'])
    def test_incomplete_eof_rejected_even_rc0(self):
        self.change_collector('def collect_child(child,timeout,limit):return b"partial",b"",False,False\n')
        with self.assertRaises(ValueError):self.invoke()
    def test_overflow_preserves_partial_private_bytes(self):
        self.change_collector('def collect_child(child,timeout,limit):raise CaptureLimitExceeded()\n')
        with self.assertRaises(ValueError):self.invoke()
        root=self.window/'A/route-eth1-finally-up';result=json.loads((root/'result.json').read_text())
        self.assertEqual(result['failure'],'CAPTURE_LIMIT_EXCEEDED');self.assertFalse(result['capture_complete']);self.assertEqual((root/'stdout.log').read_bytes(),b'prefix')
    def test_unknown_up_and_observed_state_are_separate(self):
        source=(SOURCE.parent/'run-window.ps1').read_text()
        up=source.index("Invoke-RouteNetworkCapture -Step 'route-eth1-finally-up'")
        sticky=source.index("ETH1_UP_COMMAND_UNCONFIRMED:",up)
        observe=source.index("$restoreClock=[Diagnostics.Stopwatch]::StartNew()",sticky)
        self.assertLess(up,sticky);self.assertLess(sticky,observe)
        self.assertIn('up_command_capture_confirmed=$upCaptureConfirmed;physical_state_verified=$true',source)
        self.assertIn('21.0-$GroupClock.Elapsed.TotalSeconds-6.0',source)
        self.assertIn("if($_.Exception.Message -in @('ROUTE_NETWORK_CAPTURE_INCOMPLETE'",source)
    def mutate_window(self,change):
        cfg=json.loads(self.cfg.read_text());change(cfg);self.cfg.write_text(json.dumps(cfg))
        with self.assertRaises(ValueError):self.invoke()
        self.assertFalse(self.marker.exists());self.assertFalse((self.window/'A').exists())
    def test_mutated_target_before_import_and_dispatch(self):
        self.mutate_window(lambda c:c['targets'].update(A='OTHER_TARGET'))
    def test_mutated_driver_before_import_and_dispatch(self):
        self.mutate_window(lambda c:c.update(hdc_path=str(self.root/'other.exe')))
    def test_mutated_mode_before_import_and_dispatch(self):
        self.mutate_window(lambda c:c.update(route_lifecycle_native={'other':'scope'}))
    def test_mutated_capture_binding_before_import_and_dispatch(self):
        self.mutate_window(lambda c:c['route_network_capture'].update(collector_sha256='0'*64))
    def test_mutated_private_config_before_import_and_dispatch(self):
        (self.task/'PRIVATE_CONFIG.json').write_text('{}')
        with self.assertRaises(ValueError):self.invoke()
        self.assertFalse(self.marker.exists())
    def test_collector_exception_live_owned_child_waits_after_kill(self):
        self.change_collector('def collect_child(child,timeout,limit):raise OSError("synthetic owned pipe error")\n')
        child=OwnedChild();alive={'value':True}
        child.poll=lambda:None if alive['value'] else 0
        child.wait.side_effect=lambda timeout:alive.update(value=False)
        child.stdout.close.side_effect=lambda:self.assertFalse(alive['value'])
        child.stderr.close.side_effect=lambda:self.assertFalse(alive['value'])
        with mock.patch.object(sys,'argv',['fixture','--window',str(self.cfg),'--step','route-eth1-finally-up']),mock.patch.object(helper.subprocess,'Popen',return_value=child):
            with self.assertRaises(ValueError):helper.main()
        child.kill.assert_called_once();child.wait.assert_called_once_with(timeout=2)
        child.stdout.close.assert_called_once();child.stderr.close.assert_called_once()
        receipt=json.loads((self.window/'A/route-eth1-finally-up/result.json').read_text())
        self.assertEqual(receipt['failure'],'OSError');self.assertFalse(receipt['capture_complete']);self.assertEqual(receipt['exit_code'],0)

if __name__=='__main__':unittest.main()
