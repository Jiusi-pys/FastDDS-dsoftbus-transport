"""DEVICE-mode PS selection, stopped at a deliberately invalid validator contract.

No device authority is granted by this test. The executable is a local trap file;
the real HDC executable is neither selected nor invoked.
"""
import copy,json,os,subprocess,sys,unittest
from pathlib import Path
import flow_tests as flow
from flow_tests import read,put,sha

class DeviceBootstrap(unittest.TestCase):
    setUp=flow.Flow.setUp
    tearDown=flow.Flow.tearDown

    def test_device_query_claim_branch_executes_before_effects(self):
        # Execute the actual production branch in PowerShell. The bound validator
        # is a boundary trap: it records claim argv and exits99, so no grant or
        # remote command can execute. It does not pretend to issue a real claim.
        script=(self.task/'executor-route-query/run-window.ps1').read_text()
        start=script.index('if ($bootstrapLocal -eq $true -or $bootstrapFreeze.scope')
        end=script.index("[IO.File]::WriteAllText",start)
        body=script[start:end]
        log=self.repo/'claim-trap.json'
        trap=self.repo/'claim-trap.py'
        trap.write_text('import json,sys\nfrom pathlib import Path\nPath('+repr(str(log))+').write_text(json.dumps(sys.argv[1:]))\nraise SystemExit(99)\n')
        window=self.repo/'claim-window';window.mkdir();put(window/'window.json',{})
        psfile=self.repo/'claim-branch.ps1'
        psfile.write_text("$ErrorActionPreference='Stop'\n$bootstrapLocal=$false\n$bootstrapFreeze=[pscustomobject]@{scope='ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE'}\n$contractValidator='"+str(trap)+"'\n$windowRoot='"+str(window)+"'\n"+body,encoding='utf-8')
        ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
        result=subprocess.run([str(ps),'-NoProfile','-File',str(psfile)],capture_output=True,timeout=15)
        self.assertNotEqual(result.returncode,0);self.assertTrue(log.is_file(),result.stderr)
        self.assertEqual(read(log),['--claim-query',str(window/'window.json')])
        self.assertFalse((window/'EXECUTION_STARTED').exists())

    def test_real_mode_branches_stop_before_tool_dispatch(self):
        flow.Flow.prepare(self)
        base_cfg=copy.deepcopy(self.cfg)
        manifest=read(self.build/'build-manifest.json');manifest.update(scenario='ROS2_DSOFTBUS',main='ordinary_ros2_transport_consumer',abi=dict(fastrtps='2.14.5',cpp_runtime='c++_shared'));put(self.build/'build-manifest.json',manifest)
        trap=self.repo/'NEVER_EXECUTE_THIS_DEVICE_TOOL.exe';trap.write_bytes(b'LOCAL_TRAP_NOT_EXECUTABLE')
        private=read(self.task/'PRIVATE_CONFIG.json');private['hdc_path']=str(trap);put(self.task/'PRIVATE_CONFIG.json',private)
        mat=read(self.task/'MATERIALIZATION.json');mat['private_config_sha256']=sha(self.task/'PRIVATE_CONFIG.json');put(self.task/'MATERIALIZATION.json',mat)
        # All cases deliberately stop before any command could reach the trap.
        cases={'valid_device_then_validator':{},'missing_authority':{'device_authorized':False},
               'local_driver':{'local_driver':{'kind':'LOCAL_FAKE_SANDBOX'}},
               'local_basis':{'local_test_basis':'not allowed'},'local_authority':{'local_contract_test':True},
               'device_evidence':{'device_evidence':True}}
        ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
        env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules'
        for n,(name,change) in enumerate(cases.items(),20):
            frozen=dict(self.f,scope=self.c.ROS_SCOPE,authorization=self.c.ROS_AUTHORIZATION,limits={},
                execution_admitted=True,device_authorized=True,private_config_sha256=sha(self.task/'PRIVATE_CONFIG.json'),
                materialization_sha256=sha(self.task/'MATERIALIZATION.json'))
            frozen.update(change)
            path=put(self.task/f'DEVICE_CONTRACT-r{n}.json',frozen)
            for mode in ('query','data'):
                window=self.task/'windows'/(mode+'-'+name);window.mkdir(parents=True)
                cfg=dict(base_cfg,scenario='readonly-directory-query' if mode=='query' else self.c.ROS_SCENARIO,
                    repo=str(self.repo),window_root=str(window),hdc_path=str(trap),targets=private['targets'],token=private['tokens'],permission_baseline_sha256=private['permission_baseline_sha256'],
                    task_contract=dict(task_root=str(self.task),freeze_path=str(path),freeze_sha256=sha(path)),
                    package=str(window/'helpers'),package_artifacts=[],build_manifest=str(self.build/'build-manifest.json'),build_manifest_sha256=sha(self.build/'build-manifest.json'),source_state=str(self.build/'source-state.json'),source_state_sha256=sha(self.build/'source-state.json'))
                put(window/'window.json',cfg)
                script=self.task/('executor-route-query' if mode=='query' else 'executor')/'run-window.ps1'
                result=subprocess.run([str(ps),'-NoProfile','-NonInteractive','-File',str(script),'-WindowDirectory',str(window)],cwd=self.repo,env=env,capture_output=True,timeout=20)
                self.assertNotEqual(result.returncode,0);self.assertFalse((window/'EXECUTION_STARTED').exists())
                output=result.stdout+result.stderr
                if name=='valid_device_then_validator':
                    self.assertNotIn(b'Device contract may not carry local-test authority fields',output)
                    if mode=='query':self.assertTrue((window/'task-contract-check.stderr.log').exists(),output.decode(errors='replace'))
                    else:self.assertIn(b'no device operations',output)
                self.assertFalse(list(self.task.rglob('DATA_ATTEMPT_STARTED')))

if __name__=='__main__':unittest.main()
