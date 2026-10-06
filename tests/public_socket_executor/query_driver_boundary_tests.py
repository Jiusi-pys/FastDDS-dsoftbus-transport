"""Actual PowerShell query caller and formal Python claim validator, local only."""
import copy,importlib.util,json,os,subprocess,sys,unittest
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
ROOT=Path(os.environ.get('QUERY_BOUNDARY_TEST_ROOT',str(REPO/'.artifacts/ros2-device-acceptance/local-negative-20261001-ros-admission_boundary'))).resolve()
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))

class Boundary(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if ROOT.exists():raise ValueError('Fresh isolated fixture root required')
        tool=Path(__file__).parent/'local_flow/run.py'
        for args in [ ['--phase','init'],['--phase','query','--window','query-2'] ]:
            # query preparation only: no device/model directory dispatch yet.
            if args[1]=='query':break
            subprocess.run([sys.executable,str(tool),'--root',str(ROOT),'--scenario','ROS2_DSOFTBUS',*args],cwd=REPO,check=True)
        prepare=ROOT/'executor-route-query/prepare-query.py'
        subprocess.run([sys.executable,str(prepare),str(ROOT/'windows/query-2'),'--preflight-config',str(ROOT/'preflight.local.json'),'--helper-provenance',str(ROOT/'helper-provenance.json'),'--contract',str(ROOT/'DEVICE_CONTRACT.json')],cwd=REPO,stdout=subprocess.DEVNULL,check=True)
        cls.cfg=read(ROOT/'windows/query-2/window.json')
        spec=importlib.util.spec_from_file_location('query_boundary_contract',ROOT/'executor/task_contract.py');cls.c=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.c)
        script=(ROOT/'executor-route-query/run-window.ps1').read_text()
        begin=script.index('function Invoke-HdcLogged(');end=script.index('function ',begin+10)
        cls.function=script[begin:end]
    def call(self,side='A'):
        trap=ROOT/'driver-call-count.txt';trap.unlink(missing_ok=True)
        spy=ROOT/'driver-spy.py';spy.write_text('from pathlib import Path\nPath('+repr(str(trap))+').write_text("CALLED")\n')
        ps=ROOT/'actual-caller-test.ps1'
        ps.write_text("$ErrorActionPreference='Stop'\n$windowRoot='"+str(ROOT/'windows/query-2')+"'\n$contractValidator='"+str(ROOT/'executor/task_contract.py')+"'\n$cfg=Get-Content -Raw ($windowRoot+'/window.json')|ConvertFrom-Json\n$bootstrapFreeze=[pscustomobject]@{scope='ROS2_DSOFTBUS_DUAL_BOARD_FUNCTIONAL_ACCEPTANCE'}\n$utf8=[Text.UTF8Encoding]::new($false)\n$hdcPath='"+str(ROOT/'local-flow/fake-hdc.exe')+"'\n"+self.function+"\nInvoke-HdcLogged -Board "+side+" -Step 'directory-query' -NativeArguments @('LOCAL_BOUNDARY_SPY_ONLY')\n",encoding='utf-8')
        env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules'
        env.update(FAKE_HDC_SCRIPT=str(spy),FAKE_HDC_PYTHON=sys.executable)
        p=subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'),'-NoProfile','-File',str(ps)],cwd=REPO,env=env,capture_output=True)
        return p,trap.exists()
    def test_01_missing_claim_no_driver(self):
        result,called=self.call();self.assertNotEqual(result.returncode,0);self.assertFalse(called)
    def test_02_claim_exact_identity_and_single_dispatch(self):
        self.c.claim_query(self.cfg)
        claim=ROOT/'local-test-claims'/('query-'+self.cfg['run']+'.json');original=claim.read_bytes();row=read(claim)
        for key,value in [('run','0'*16),('purpose','F1M'),('window',str(ROOT/'windows/foreign')),('targets',{'A':'WRONG','B':'SIM-B'}),('materialization_sha256','0'*64)]:
            bad=dict(row,**{key:value});claim.write_text(json.dumps(bad))
            result,called=self.call();self.assertNotEqual(result.returncode,0);self.assertFalse(called)
        claim.write_bytes(original)
        result,called=self.call();self.assertEqual(result.returncode,0,result.stderr);self.assertTrue(called)
        result,called=self.call();self.assertNotEqual(result.returncode,0);self.assertFalse(called)
        self.assertRaises((ValueError,FileExistsError),self.c.claim_query,self.cfg)
        self.assertEqual(claim.read_bytes(),original)

if __name__=='__main__':unittest.main()
