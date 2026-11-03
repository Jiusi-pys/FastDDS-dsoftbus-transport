"""Checks actual ARM64 outputs/receipts, not ARM emulation or fake SDK PASS."""
import argparse,pathlib,json,re,unittest
p=argparse.ArgumentParser();p.add_argument('--evidence',required=True);p.add_argument('--sources',required=True);a,rest=p.parse_known_args()
E=pathlib.Path(a.evidence);S=pathlib.Path(a.sources)
class NativeStaticTests(unittest.TestCase):
    def test_four_real_ELF_shapes_and_CFI(self):
        r=json.loads((E/'ELF_AUDIT.json').read_text())
        self.assertEqual(len(r),4)
        for row in r:
            self.assertEqual(row['machine'],'AArch64');self.assertTrue(row['bind_now']);self.assertTrue(row['CFI_check']);self.assertTrue(row['strict_link_flags'])
        self.assertEqual(sum(x['kind']=='PIE' for x in r),1);self.assertEqual(sum(x['kind']=='DSO' for x in r),3)
    def test_frontend_preselection_and_fixture_no_business_imports(self):
        rows=json.loads((E/'ELF_AUDIT.json').read_text())
        for row in rows:
            if row['target'] not in ('stage1_native_frontend','stage1_native_loader_fixture'):continue
            for name in row['needed']:self.assertFalse(any(x in name.lower() for x in ('softbus','fastrtps','fastcdr','accesstoken','tokensetproc')))
    def test_namespace_and_bridge_exports(self):
        rows={x['target']:x for x in json.loads((E/'ELF_AUDIT.json').read_text())}
        self.assertIn('libc++.so',rows['stage1_actionbag_identity']['needed'])
        self.assertNotIn('libc++_shared.so',rows['stage1_actionbag_identity']['needed'])
        self.assertIn('libc++_shared.so',rows['stage1_native_frontend']['needed'])
        self.assertIn('ns_identity_get_api',rows['stage1_actionbag_identity']['exports'])
        self.assertIn('ns_consumer_get_api',rows['stage1_native_consumer']['exports'])
        self.assertIn('ns_loader_get_api',rows['stage1_native_loader_fixture']['exports'])
    def test_source_no_provisioning_or_hidden_setter(self):
        bridge=(S/'identity_bridge.cpp').read_text();fixture=(S/'loader_fixture.c').read_text()
        for text in (bridge,fixture):
            for forbidden in ('GetAccessTokenId(', 'SetSelfTokenID(', 'ReloadNativeTokenInfo(', 'DeleteToken('):self.assertNotIn(forbidden,text)
        front=(S/'frontend.cpp').read_text()
        self.assertIn('AUTHORIZATION_REQUIRED no_identity_api=1 no_optional_dlopen=1',front)
        self.assertNotIn('LD_PRELOAD',front);self.assertNotIn('org.fastdds',front)
    def test_actual_compile_and_link_profile(self):
        r=json.loads((E/'GENERATED_AUDIT.json').read_text())
        self.assertEqual(r['project_TUs'],5);self.assertTrue(r['all_output_E']);self.assertTrue(r['CFI_not_disabled']);self.assertEqual(r['sdk_namespace'],'__n1');self.assertEqual(r['platform_namespace'],'__h')
        self.assertEqual(r['old299_reused_as_DSO'],False)
if __name__=='__main__':
    import sys;unittest.main(argv=[sys.argv[0],*rest],verbosity=2)
