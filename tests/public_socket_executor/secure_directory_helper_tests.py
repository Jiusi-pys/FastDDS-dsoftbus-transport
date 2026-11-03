"""Directory-only selection of a source-bound helper; no executable is run."""
from pathlib import Path
import json,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import flow_tests as fixture
from flow_tests import put,sha,read

class DirectoryHelper(unittest.TestCase):
    def setUp(self):
        self.flow=fixture.Flow();self.flow.setUp();self.addCleanup(self.flow.tearDown);f=self.flow;c=f.c
        source=f.repo/'probes/softbus_record_probe.cpp';source.parent.mkdir();source.write_bytes(b'LOCAL_READER_FIXTURE_NOT_TARGET_SOURCE')
        image=Path(c.PRIVATE['query_helper_path']);artifact=dict(path=str(image),bytes=image.stat().st_size,sha256=sha(image))
        receipt=put(f.task/'helper-build.json',dict(schema='IDENTITY_HELPER_TARGET_COMPILE',scope='COMPILE_LINK_ONLY_NO_DEVICE',compile_exit=0,artifact=artifact,inputs=[dict(path=str(source),sha256=sha(source))]))
        compiler=f.task/'compiler.input';compiler.write_bytes(b'READER_INPUT_NOT_EXECUTABLE')
        libraries=[]
        for i in range(3):
            p=f.task/f'library-{i}.input';p.write_bytes(b'READER_INPUT_NOT_PLATFORM_DSO');libraries.append(dict(path=str(p),sha256=sha(p)))
        units=[]
        for i in range(4):
            p=f.task/f'unit-{i}.deps';p.write_text('LOCAL_READER_INPUT');units.append(dict(depfile=str(p),depfile_sha256=sha(p),dependency_count=1,dependencies=[dict(path=str(source),sha256=sha(source))]))
        closure=put(f.task/'helper-dependencies.json',dict(schema='IDENTITY_HELPER_TARGET_COMPILE',scope='COMPILE_LINK_ONLY_NO_DEVICE',compile_link_exit=0,device_operations=0,artifact_correspondence=dict(same_bytes=True,original_binding_receipt_sha256=sha(receipt),original_artifact_sha256=sha(image),fresh_artifact_sha256=sha(image)),compiler=dict(path=str(compiler),sha256=sha(compiler)),direct_link_libraries=libraries,translation_units=units,closure=dict(translation_unit_count=4,system_headers_included=True,all_dependency_entries=4)))
        self.bound=dict(path=str(image),sha256=sha(image),size=image.stat().st_size,source=dict(path='probes/softbus_record_probe.cpp',sha256=sha(source)),binding_receipt=dict(path=str(receipt),sha256=sha(receipt)),dependency_closure=dict(path=str(closure),sha256=sha(closure)))
        self.frozen=dict(authorization='TASK_SCOPED_SECURE_CANDIDATE_DEVICE_VALIDATION',identity_qualification=False,directory_query_helper=self.bound)
        self.cfg=dict(query_helper=dict(path=str(image),sha256=sha(image)),command_contract='--list-nodes --expected-token <board-token> --preflight-confirmed')
    def test_directory_only_and_export_injection_rejected(self):
        c=self.flow.c;c.verify_identity_query_binding(self.cfg,self.frozen)
        cfg=dict(self.cfg,private_identity_output={'A':'unrequested'})
        self.assertRaisesRegex(ValueError,'may not export identities',c.verify_identity_query_binding,cfg,self.frozen)
        cfg=dict(self.cfg,command_contract=self.cfg['command_contract']+' --check-identities')
        self.assertRaisesRegex(ValueError,'may not export identities',c.verify_identity_query_binding,cfg,self.frozen)
    def test_dependency_or_missing_binding_rejected(self):
        c=self.flow.c;bound=dict(self.bound,dependency_closure=dict(self.bound['dependency_closure'],sha256='0'*64))
        self.assertRaisesRegex(ValueError,'dependency closure receipt drift',c.verify_identity_query_binding,self.cfg,dict(self.frozen,directory_query_helper=bound))
        self.assertRaises(Exception,c.verify_identity_query_binding,self.cfg,dict(self.frozen,directory_query_helper={}))

if __name__=='__main__':unittest.main()
