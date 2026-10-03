"""Invoke the real preflight entry under optimization; spy prevents all HDC."""
import hashlib,json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
TOOL=ROOT/'scripts/public_socket/readonly_preflight.py'
class Preflight(unittest.TestCase):
 def test_rejects_bad_review_with_optimization_and_zero_driver_calls(self):
  for optimized in (False,True):
   with self.subTest(optimized=optimized),tempfile.TemporaryDirectory() as d:
    d=Path(d);selection=d/'selection.json';review=d/'review.json';count=d/'count';wrapper=d/'wrapper.py'
    selection.write_text(json.dumps({'preflight_output_parent':str(ROOT/'.artifacts')}))
    review.write_text(json.dumps({'verdict':'FAIL','selection_sha256':hashlib.sha256(selection.read_bytes()).hexdigest(),'allow_readonly_preflight':False}))
    wrapper.write_text("import sys,subprocess,runpy\nfrom pathlib import Path\ndef spy(*a,**k):\n Path(sys.argv[1]).write_text('DRIVER_CALLED');raise RuntimeError('TEST_SPY_NO_DEVICE')\nsubprocess.run=spy\nsubprocess.Popen=spy\ncount=sys.argv[1];tool=sys.argv[2];sys.argv=[tool]+sys.argv[3:];runpy.run_path(tool,run_name='__main__')\n")
    env=os.environ.copy();env['PYTHONOPTIMIZE']='1' if optimized else '0'
    argv=[sys.executable]+(['-O'] if optimized else [])+[str(wrapper),str(count),str(TOOL),'--selection',str(selection),'--review',str(review),'--output',str(ROOT/'.artifacts/preflight-negative-never-created')]
    r=subprocess.run(argv,cwd=ROOT,env=env,capture_output=True,shell=False,timeout=10)
    self.assertNotEqual(r.returncode,0);self.assertIn(b'Preflight constraint failed',r.stderr);self.assertFalse(count.exists())
if __name__=='__main__':unittest.main()
