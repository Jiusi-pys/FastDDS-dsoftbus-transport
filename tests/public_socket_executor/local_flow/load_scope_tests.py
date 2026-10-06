"""Actual current formal parser over saved full-flow modeled capture bytes."""
import copy,hashlib,importlib.util,json,subprocess,sys,unittest,tempfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[3];WINDOW=REPO/'.artifacts/installed-runtime-device/local-verified-20260930-current/windows/data-2'
spec=importlib.util.spec_from_file_location('formal_load_scope',REPO/'scripts/public_socket/load_origin.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Scope(unittest.TestCase):
 def test_real_saved_model_logs_and_device_rejection(self):
  c=json.loads((WINDOW/'window.json').read_text());self.assertIs(c['simulated'],True)
  for s in 'AB':
   good=m.analyze(WINDOW/s/'load',c,s);self.assertTrue(good['complete'],good['errors']);self.assertEqual(good['scope'],'LOCAL_MODELED_CAPTURE_NOT_DEVICE_EVIDENCE')
   for value in (False,None,1,'true'):
    bad=copy.deepcopy(c);bad['simulated']=value;r=m.analyze(WINDOW/s/'load',bad,s);self.assertFalse(r['complete']);self.assertIn('LOAD_CAPTURE_MODE_SCOPE_MISMATCH',r['errors'])
 def test_actual_cli_fresh_derived_output(self):
  for s in 'AB':
   original=WINDOW/s/'actual-load-origin.json';before=hashlib.sha256(original.read_bytes()).hexdigest();out=Path(tempfile.mkdtemp(prefix='load-scope-replay-',dir=WINDOW/s))/'result.json'
   p=subprocess.run([sys.executable,str(REPO/'scripts/public_socket/load_origin.py'),str(WINDOW),s,'--output',str(out)],capture_output=True)
   self.assertEqual(p.returncode,0,p.stderr);self.assertEqual(json.loads(out.read_text())['scope'],'LOCAL_MODELED_CAPTURE_NOT_DEVICE_EVIDENCE');self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(),before)
if __name__=='__main__':unittest.main()
