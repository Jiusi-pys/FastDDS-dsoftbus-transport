"""Frozen-input reader tests only; synthetic files never invoke a device tool."""
from pathlib import Path
import hashlib,importlib.util,json,tempfile,unittest,shutil
ROOT=Path(__file__).resolve().parents[2]
def module(path,name):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
s=module(ROOT/'scripts/public_socket/shared_reattach/executor/secure_contract.py','secure_reader_fixture')
p=module(ROOT/'scripts/public_socket/pregrant_recovery.py','recovery_reader_fixture')
def put(path,value):Path(path).write_text(json.dumps(value),encoding='utf-8')
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
class SecureReader(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=ROOT/'build');self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.root=self.base/'task';self.root.mkdir();root=self.root
        (root/'executor').mkdir();shutil.copyfile(Path(s.__file__),root/'executor/secure_contract.py')
        driver=self.base/'hdc.exe';driver.write_bytes(b'NONEXECUTABLE_PARSER_FIXTURE_NOT_DEVICE_TOOL')
        private=dict(targets={'A':'FIXTURE-A','B':'FIXTURE-B'},tokens={'A':'1','B':'2'},hdc_path=str(driver),repo_root=str(ROOT))
        put(root/'PRIVATE_CONFIG.json',private)
        put(root/'AUTHORIZATION.json',dict(authorization=s.AUTH,task_id=s.TASK,base_commit=s.BASE,task_root=str(root),targets=private['targets'],tokens=private['tokens'],platform_policy_change=False,production_qualification=False))
        good=root/'pki';good.mkdir()
        for side in 'AB':
            leaf=good/'enclaves'/side;leaf.mkdir(parents=True)
            for name in s.FILES:(leaf/name).write_bytes(b'LOCAL_PARSER_TEST_ONLY')
        run='1234567890abcdef';receipt=good/'ROS_PKI_INPUTS.json'
        put(receipt,dict(run=run,domain=201,default='DENY',plugins=['PKI-DH','Access-Permissions','AES-GCM-GMAC'],discovery='SIGN',allow_unauthenticated=False,join_access_control=True,data_protection='NONE',production=False,granted_dds_topics=s.topics_for(run)))
        scene=dict(case='S1',run=run,spec=s.case_spec('S1'),material=dict(root=str(good),sha256=s.material(good)),legitimate_material=dict(root=str(good),sha256=s.material(good)),pki_inputs=dict(path=str(receipt),sha256=sha(receipt)))
        manifest=root/'manifest.json';put(manifest,dict(abi=dict(fastrtps='2.14.7'),security_mode='ON'))
        material=root/'MATERIALIZATION.json';put(material,dict(files=[dict(destination='executor/secure_contract.py',sha256=sha(root/'executor/secure_contract.py'))]))
        frozen=dict(authorization=s.AUTH,authorization_record=dict(path=str(root/'AUTHORIZATION.json'),sha256=sha(root/'AUTHORIZATION.json')),private_config_sha256=sha(root/'PRIVATE_CONFIG.json'),materialization_sha256=sha(material),build_binding=dict(package_artifacts=[]),secure_candidate=scene,device_authorized=True,execution_admitted=False,device_driver=dict(sha256=sha(driver)))
        freeze=root/'DEVICE_CONTRACT.json';put(freeze,frozen)
        window=root/'windows/parser-only';window.mkdir(parents=True);self.window=window
        self.cfg=dict(run=run,scenario='ROS2_DSOFTBUS',task_contract=dict(task_root=str(root),freeze_path=str(freeze),freeze_sha256=sha(freeze)),window_root=str(window),private_config_sha256=sha(root/'PRIVATE_CONFIG.json'),targets=private['targets'],token=private['tokens'],hdc_path=str(driver),remote={side:f'/data/local/tmp/mdds_dds_{run}_{side}' for side in 'AB'},repo=str(ROOT),package_artifacts=[],main_member='ordinary_ros2_transport_consumer',main_prefix='ROS_MAIN',system_library_dirs=['/system/lib64/platformsdk','/system/lib64/chipset-sdk-sp','/system/lib64/chipset-sdk','/system/lib64'],secure_candidate=scene,build_manifest=str(manifest),timeout_seconds=300,max_record_bytes=8192,auxiliary_sha256={})
        for side in 'AB':
            aux=window/side/'aux';aux.mkdir(parents=True)
            for name in ['capture-load.sh','native_permission_probe','required-load-paths.txt','ros.conf']:
                (aux/name).write_bytes(b'LOCAL_PARSER_TEST_ONLY')
            for name in s.FILES:shutil.copyfile(good/'enclaves'/side/name,aux/('security_'+name))
            self.cfg['auxiliary_sha256'][side]={x.name:sha(x) for x in aux.iterdir()}
        put(window/'window.json',self.cfg);put(window/'window-summary.json',dict(failure_stage='PREFLIGHT_OR_DEPLOY',first_failure='FIXTURE_ONLY',attempted_grants=[],process_outcome='NO_BUSINESS_PROCESS_STARTED',cleanup_failures=[]));(window/'EXECUTION_STARTED').write_text('PARSER_FIXTURE_NO_TOOL_CALLED')
    def test_secure_shape_and_material_drift(self):
        self.assertEqual(p.original(self.window)[3],self.root)
        (self.window/'A/aux/security_key.pem').write_bytes(b'DRIFT')
        self.assertRaisesRegex(ValueError,'SECURE_AUXILIARY_HASH_DRIFT',p.original,self.window)
    def test_simulated_claim_or_driver_drift_cannot_enter_real_recovery(self):
        cfg=dict(self.cfg,simulated=True);put(self.window/'window.json',cfg)
        self.assertRaisesRegex(ValueError,'NOT_SECURE_DEVICE_AUTHORITY',p.original,self.window)
        put(self.window/'window.json',self.cfg);Path(self.cfg['hdc_path']).write_bytes(b'changed')
        self.assertRaisesRegex(ValueError,'SECURE_DRIVER_DRIFT',p.original,self.window)
    def test_wrong_authority_and_executor_drift_are_rejected(self):
        grant=self.root/'AUTHORIZATION.json';value=json.loads(grant.read_text());value['task_id']='OTHER_TASK';put(grant,value)
        self.assertRaisesRegex(ValueError,'Secure task authority drift',p.original,self.window)
        # This separate edge reaches the existing materialization reader first.
        (self.root/'executor/secure_contract.py').write_bytes(b'drift')
        self.assertRaisesRegex(ValueError,'ORIGINAL_EXECUTOR_DRIFT',p.original,self.window)
if __name__=='__main__':unittest.main()
