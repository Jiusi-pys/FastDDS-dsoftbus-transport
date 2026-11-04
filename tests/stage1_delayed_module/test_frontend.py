"""Real Windows loader + explicitly fake identity; never SDK/device proof."""
import argparse,json,pathlib,subprocess,unittest,sys
parser=argparse.ArgumentParser();parser.add_argument('--exe',required=True);parser.add_argument('--module',required=True);parser.add_argument('--missing-export',required=True);parser.add_argument('--result',required=True)
args,rest=parser.parse_known_args();sys.argv=[sys.argv[0],*rest];rows=[]
class DelayedTests(unittest.TestCase):
    def case(self,name,rc,events,phases=None):
        if name=='default':argv=[args.exe,args.module]
        elif name=='real':argv=[args.exe,'--real',args.module]
        else:argv=[args.exe,'--fixture',name,args.missing_export if name=='missing_export' else args.module+'.absent' if name=='missing_dll' else args.module]
        p=subprocess.run(argv,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=3)
        row={'case':name,'argv':argv,'child_exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr};rows.append(row);print(json.dumps(row),flush=True)
        self.assertEqual(p.returncode,rc,row);self.assertEqual(p.stderr,'',row)
        self.assertIn('scope=HOST_FIXTURE_ONLY real_identity_calls=0',p.stdout)
        import re
        observed=[(int(a),int(b)) for a,b in re.findall(r'EVENT kind=(\d+) phase=(\d+)',p.stdout)]
        self.assertEqual([a for a,b in observed],events,row)
        if phases is not None:self.assertEqual([b for a,b in observed],phases,row)
        if name!='hard_exit':self.assertIn('workers_joined=1',p.stdout)
        if 110 in events:self.assertIn('fixture_restored=1',p.stdout)
        else:self.assertNotIn('fixture_restored=1',p.stdout)
    def test_default_and_real_refuse_before_load(self):
        for name,rc in [('default',77),('real',78)]:
            with self.subTest(case=name):self.case(name,rc,[])
    def test_identity_and_actual_controlled_worker_fail_closed(self):
        self.case('identity_error',79,[10,110]);self.case('worker_reject',80,[])
    def test_real_dll_constructor_and_close_unload_restore_order(self):
        self.case('normal',0,[10,20,30,40,50,60,70,80,90,100,110],[1,2,2,2,2,2,2,2,2,2,0])
    def test_early_constructor_cannot_gain_later_credit(self):
        self.case('early_load',53,[30,40,10,20,90,100,110],[0,0,1,2,2,2,0])
    def test_loader_and_export_failures_unload_before_restore(self):
        self.case('missing_dll',51,[10,20,110]);self.case('missing_export',52,[10,20,30,40,90,100,110])
    def test_abi_and_function_owner_errors(self):
        self.case('api_rejections',53,[10,20,30,40,90,100,110]);self.case('foreign_api',55,[10,20,30,40,90,100,110])
    def test_input_result_ownership_and_size_errors_leave_buffers(self):
        for name in ['request_size','request_rejections','result_size','short_output','overlap']:
            with self.subTest(case=name):self.case(name,54,[10,20,30,40,50,80,90,100,110])
    def test_cancel_and_error_close_without_lifecycle_reentry(self):
        self.case('cancel',43,[10,20,30,40,50,60,70,80,90,100,110]);self.case('module_error',42,[10,20,30,40,50,60,70,80,90,100,110])
    def test_hard_exit_does_not_claim_restoration(self):
        self.case('hard_exit',90,[10,20,30,40,50,60])
if __name__=='__main__':
    try:unittest.main(verbosity=2)
    finally:
        path=pathlib.Path(args.result)
        with path.open('x',encoding='utf-8') as f:json.dump({'scope':'HOST_FIXTURE_ONLY','child_cases':rows,'count':len(rows),'Native_SDK_OH_qualification':'NOT_TESTED'},f,indent=2)
