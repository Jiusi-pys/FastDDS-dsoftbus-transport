import importlib.util
import tempfile
from pathlib import Path
import unittest
import json
import shutil
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('secure',ROOT/'scripts/public_socket/shared_reattach/executor/secure_contract.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Secure(unittest.TestCase):
    def test_case_activation_and_fragment_limits(self):
        self.assertEqual(m.case_spec('S1')['enabled'],{'A':True,'B':True})
        self.assertEqual(m.case_spec('S2')['enabled'],{'A':True,'B':False})
        self.assertEqual(m.case_spec('S1_FRAGMENT')['fragment_bytes'],70000)
        self.assertRaises(ValueError,m.case_spec,'attack')
    def test_material_drift_and_missing_key_fail_closed(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
            root=Path(temp)
            for side in 'AB':
                (root/'enclaves'/side).mkdir(parents=True)
                for name in m.FILES:(root/'enclaves'/side/name).write_bytes(b'test-only')
            rows=m.material(root)
            m.verify_material(root,rows)
            (root/'enclaves/A/key.pem').write_bytes(b'changed')
            self.assertRaises(ValueError,m.verify_material,root,rows)
            (root/'enclaves/A/key.pem').unlink()
            self.assertRaises(ValueError,m.material,root)
    def test_private_recovery_tree_rejects_unknowns_and_missing_confirmed_copy(self):
        root='/data/local/tmp/mdds_dds_1234567890abcdef_A'
        cfg=dict(remote={'A':root},secure_candidate=dict(material=dict(sha256={'A':{n:'a'*64 for n in m.FILES}})))
        calls=[]
        def shell(tag,command):
            calls.append(command)
            if tag=='secure-root-mode':return '700'
            if tag=='secure-tree-0':return 'enclaves'
            if tag=='secure-tree-1':return 'A'
            if tag=='secure-tree-2':return ''
            raise AssertionError('Unexpected operation')
        result=m.recovery_tree(cfg,'A',[],shell)
        self.assertEqual(result['files'],[])
        self.assertRaises(ValueError,m.recovery_tree,cfg,'A',['key.pem'],shell)
        self.assertTrue(all('grant' not in x and 'rm ' not in x for x in calls))
        def unknown(tag,command):return '700' if tag=='secure-root-mode' else 'unowned'
        self.assertRaises(ValueError,m.recovery_tree,cfg,'A',[],unknown)
        def retained(tag,command):
            if tag=='secure-tree-2':return 'key.pem'
            if tag=='secure-file-0':return 'a'*64+'  '+root+'/security/enclaves/A/key.pem\nFILE=regular file|600|0|0|12'
            return shell(tag,command)
        result=m.recovery_tree(cfg,'A',['key.pem'],retained)
        self.assertEqual(result['files'][0]['mode'],'600')
        def drift(tag,command):return retained(tag,command).replace('a'*64,'b'*64)
        self.assertRaises(ValueError,m.recovery_tree,cfg,'A',['key.pem'],drift)
    def test_governance_negative_cannot_change_other_identity_or_permissions(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as temp:
            task=Path(temp);good=task/'good';bad=task/'bad'
            for side in 'AB':
                (good/'enclaves'/side).mkdir(parents=True)
                for name in m.FILES:(good/'enclaves'/side/name).write_bytes(b'fixture-only')
            receipt=good/'ROS_PKI_INPUTS.json'
            receipt.write_text(json.dumps(dict(run='test123',domain=201,default='DENY',
                plugins=['PKI-DH','Access-Permissions','AES-GCM-GMAC'],discovery='SIGN',
                allow_unauthenticated=False,join_access_control=True,data_protection='NONE',
                production=False,granted_dds_topics=m.topics_for('test123'))))
            shutil.copytree(good,bad)
            for side in 'AB':(bad/'enclaves'/side/'governance.p7s').write_bytes(m.INVALID_GOVERNANCE)
            scene=dict(case='S3',run='test123',material=dict(root=str(bad),sha256=m.material(bad)),
                legitimate_material=dict(root=str(good),sha256=m.material(good)),
                pki_inputs=dict(path=str(receipt),sha256=m.digest(receipt)))
            m.verify_pki(scene,task)
            inputs=json.loads(receipt.read_text());inputs['granted_dds_topics']=['wrong-topic-test123','rt/unrelated-extra-topic']
            receipt.write_text(json.dumps(inputs));scene['pki_inputs']['sha256']=m.digest(receipt)
            self.assertRaises(ValueError,m.verify_pki,scene,task)
            inputs['granted_dds_topics']=m.topics_for('test123');receipt.write_text(json.dumps(inputs));scene['pki_inputs']['sha256']=m.digest(receipt)
            (bad/'enclaves/A/permissions.p7s').write_bytes(b'foreign')
            self.assertRaises(ValueError,m.verify_pki,scene,task)
            (bad/'enclaves/A/permissions.p7s').write_bytes(b'fixture-only')
            (bad/'enclaves/B/governance.p7s').write_bytes(b'other-invalid')
            self.assertRaises(ValueError,m.verify_pki,scene,task)
if __name__=='__main__':unittest.main()
