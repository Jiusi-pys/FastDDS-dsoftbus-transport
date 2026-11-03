"""Current materializer/validators, isolated synthetic repository, no device I/O."""
import copy
import json
import unittest
import flow_tests as flow
from flow_tests import read,put,sha

class FinalMaterialization(unittest.TestCase):
    setUp=flow.Flow.setUp
    tearDown=flow.Flow.tearDown

    def test_current_ros_requires_all_bound_external_inputs(self):
        frozen=dict(self.f,scope=self.c.ROS_SCOPE,local_contract_test=True)
        mat=read(self.task/'MATERIALIZATION.json')
        self.assertEqual({e['path'] for e in mat['external_files']},{
            'scripts/NativePermissionGuard.ps1','scripts/public_socket/analyze_shared.py',
            'scripts/public_socket/analyze_ros2.py','scripts/public_socket/analyze_ros2_native.py',
            'scripts/public_socket/analyze_dds214.py'})
        self.c.materialization_record(frozen)
        for omitted in mat['external_files']:
            bad=copy.deepcopy(mat);bad['external_files']=[e for e in bad['external_files'] if e!=omitted]
            put(self.task/'MATERIALIZATION.json',bad)
            altered=dict(frozen,materialization_sha256=sha(self.task/'MATERIALIZATION.json'))
            with self.subTest(path=omitted['path']):
                self.assertRaises(ValueError,self.c.materialization_record,altered)
        put(self.task/'MATERIALIZATION.json',mat)

    def test_wrong_external_path_or_hash_is_rejected(self):
        mat=read(self.task/'MATERIALIZATION.json')
        for field,value in [('path','scripts/public_socket/not-approved.py'),('sha256','0'*64)]:
            bad=copy.deepcopy(mat);bad['external_files'][-1][field]=value;put(self.task/'MATERIALIZATION.json',bad)
            altered=dict(self.f,scope=self.c.ROS_SCOPE,materialization_sha256=sha(self.task/'MATERIALIZATION.json'))
            self.assertRaises(ValueError,self.c.materialization_record,altered)

    def test_ros_declared_stages_match_actual_controller(self):
        import subprocess
        script=self.repo/'scripts/public_socket/shared_reattach/executor/ros-control.cjs'
        stages=json.loads(subprocess.check_output(['node','-e','console.log(JSON.stringify(require(process.argv[1]).STAGES))',str(script)],text=True))
        self.assertEqual(self.c.ROS_STAGES,stages)
        self.assertEqual(self.c.ROS_LIMITS['stages'],len(stages))
        self.assertIn('TEARDOWN_READY',stages)

    def test_actual_node_runner_rejects_current_members_with_missing_fifth_input(self):
        import subprocess,shutil
        private=read(self.task/'PRIVATE_CONFIG.json')
        private.update(hdc_path=str(self.task/'test/fake-hdc.exe'),targets={'A':'SIM-A','B':'SIM-B'})
        put(self.task/'PRIVATE_CONFIG.json',private)
        mat=read(self.task/'MATERIALIZATION.json');mat['private_config_sha256']=sha(self.task/'PRIVATE_CONFIG.json');put(self.task/'MATERIALIZATION.json',mat)
        frozen=dict(self.f,scope=self.c.ROS_SCOPE,local_contract_test=True,execution_admitted=False,device_authorized=False,device_evidence=False,
            local_test_basis='READ_ONLY_NODE_MATERIALIZATION_VALIDATION_NO_DRIVER',local_driver=dict(kind='LOCAL_FAKE_SANDBOX',test_root=str(self.task),sandbox_root=str(self.task/'sandbox')),
            private_config_sha256=sha(self.task/'PRIVATE_CONFIG.json'),materialization_sha256=sha(self.task/'MATERIALIZATION.json'))
        contract=put(self.task/'LOCAL_METADATA_CONTRACT.json',frozen)
        window=self.task/'windows/metadata-validator';window.mkdir()
        for original,copyname in [('runner-template.cjs','runner.cjs'),('dds214-control.cjs','dds214-control.cjs'),('ros-control.cjs','ros-control.cjs'),('shared-control.cjs','shared-control.cjs'),('record-pair.cjs','record-pair.cjs'),('observe-native.cjs','observe-native.cjs'),('bounded-output.cjs','bounded-output.cjs')]:
            shutil.copyfile(self.task/'executor'/original,window/copyname)
        cfg=dict(scenario='ROS2_DSOFTBUS',work_package=self.c.ROS_SCOPE,task_contract=dict(task_root=str(self.task),freeze_path=str(contract),freeze_sha256=sha(contract)),
            private_config_sha256=sha(self.task/'PRIVATE_CONFIG.json'),hdc_path=private['hdc_path'],targets=private['targets'],simulated=True,window_root=str(window),
            shared_script_sha256={'NativePermissionGuard.ps1':sha(self.repo/'scripts/NativePermissionGuard.ps1')},analyzer_sha256=sha(self.repo/'scripts/public_socket/analyze_ros2.py'))
        cp=put(self.task/'LOCAL_READONLY_CONFIG.json',cfg)
        script="const fs=require('node:fs'),r=require(process.argv[1]);r.readBoundPrivate(JSON.parse(fs.readFileSync(process.argv[2])));"
        command=['node','-e',script,str(self.task/'executor/runner-template.cjs'),str(cp)]
        good=subprocess.run(command,capture_output=True,text=True);self.assertEqual(good.returncode,0,good.stderr)
        mat['external_files']=[e for e in mat['external_files'] if not e['path'].endswith('analyze_dds214.py')];put(self.task/'MATERIALIZATION.json',mat)
        frozen['materialization_sha256']=sha(self.task/'MATERIALIZATION.json');put(contract,frozen);cfg['task_contract']['freeze_sha256']=sha(contract);put(cp,cfg)
        bad=subprocess.run(command,capture_output=True,text=True);self.assertNotEqual(bad.returncode,0,'actual Node runner accepted current20 + old4')

    def test_new_script_bytes_do_not_match_old_materialization(self):
        path=self.task/'executor/runner-template.cjs';path.write_bytes(path.read_bytes()+b'\n// wrong future source\n')
        self.assertRaises(ValueError,self.c.verify_materialized_execution,self.f)

    def test_local_contract_cannot_claim_device_authority(self):
        base=dict(scope=self.c.ROS_SCOPE,local_contract_test=True,local_test_basis='isolated local test',
                  device_authorized=False,device_evidence=False,execution_admitted=False,
                  local_driver=dict(kind='LOCAL_FAKE_SANDBOX',test_root=str(self.task),sandbox_root=str(self.task/'sandbox')))
        self.c.verify_local_driver(base)
        for key in ('device_authorized','device_evidence','execution_admitted'):
            self.assertRaises(ValueError,self.c.verify_local_driver,dict(base,**{key:True}))
        self.assertRaises(ValueError,self.c.verify_installed_device_mode,base)

    def test_wrong_local_roots_are_rejected(self):
        base=dict(local_test_basis='isolated local test',device_evidence=False,device_authorized=False,execution_admitted=False,
                  local_driver=dict(kind='LOCAL_FAKE_SANDBOX',test_root=str(self.task),sandbox_root=str(self.task/'sandbox')))
        for field in ('test_root','sandbox_root'):
            bad=copy.deepcopy(base);bad['local_driver'][field]=str(self.task.parent)
            self.assertRaises(ValueError,self.c.verify_local_driver,bad)

if __name__=='__main__':unittest.main()
