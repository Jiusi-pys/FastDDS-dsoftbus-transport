"""Actual PS entry bootstraps; deliberately stop before any Fake command."""
import hashlib,json,os,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]).resolve();repo=Path(__file__).resolve().parents[3]
read=lambda p:json.loads(Path(p).read_text(encoding='utf-8-sig'))
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def put(p,x):Path(p).write_text(json.dumps(x,indent=2)+'\n')
ros=root.name=='local-verified-20261001-ros-final-r2'
if not ros and root.name!='local-negative-20260930-wrong_main':raise ValueError('Dedicated local test evidence root only')
baseline=read(root/'DEVICE_CONTRACT.json');source={mode:read(root/'windows'/('query-2' if mode=='query' else 'data-2')/'window.json') for mode in ('query','data')}
cases={'valid_local_then_validator':{},'missing_local':{'local_contract_test':None},'false_local':{'local_contract_test':False},'string_local':{'local_contract_test':'true'},'number_local':{'local_contract_test':1},'device_authority':{'device_authorized':True},'device_evidence':{'device_evidence':True},'device_admitted':{'execution_admitted':True},'foreign_test_root':{'local_driver':dict(baseline['local_driver'],test_root=str(root.parent))},'foreign_sandbox':{'local_driver':dict(baseline['local_driver'],sandbox_root=str(root.parent))}}
ps=str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe');env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules'
before=sha(root/'sandbox/fake-hdc-invocations.jsonl');results=[]
version='ros-final' if ros else 'v4'
for number,(name,change) in enumerate(cases.items(),63):
 f=dict(baseline,**change);f['limits']={} # valid bootstrap must stop at Python validator
 contract=root/f'DEVICE_CONTRACT-r{number}.json';put(contract,f)
 for mode in ('query','data'):
  w=root/'windows'/('bootstrap-'+mode+'-'+name+'-'+version);w.mkdir();c=dict(source[mode]);c.update(window_root=str(w));
  if mode=='query':c['package']=str(w/'helpers')
  c['task_contract']=dict(c['task_contract'],freeze_path=str(contract),freeze_sha256=sha(contract));put(w/'window.json',c)
  script=root/('executor-route-query' if mode=='query' else 'executor')/'run-window.ps1'
  p=subprocess.run([ps,'-NoProfile','-NonInteractive','-File',str(script),'-WindowDirectory',str(w)],cwd=repo,env=env,capture_output=True,timeout=20)
  (w/'bootstrap.stdout').write_bytes(p.stdout);(w/'bootstrap.stderr').write_bytes(p.stderr)
  assert p.returncode!=0 and not (w/'EXECUTION_STARTED').exists(),(mode,name)
  if name=='valid_local_then_validator':
   reason=b'ROS authorization/limits mismatch' if ros else b'Wrong installed limits'
   assert (w/'task-contract-check.stderr.log').exists() if mode=='query' else reason in p.stderr,mode
   cli=subprocess.run([sys.executable,str(root/'executor/task_contract.py'),'--check-query' if mode=='query' else '--check-window',str(w/'window.json')],cwd=repo,capture_output=True)
   (w/'validator-direct.stderr').write_bytes(cli.stderr)
   assert cli.returncode!=0 and reason in cli.stderr
  results.append(dict(mode=mode,case=name,exit=p.returncode,dispatch=False))
assert before==sha(root/'sandbox/fake-hdc-invocations.jsonl')
put(root/('ROS_BOOTSTRAP_MATRIX.json' if ros else 'BOOTSTRAP_MATRIX.json'),dict(pass_=True,cases=results,actual_PS_entries=True,no_fake_dispatch=True))
print('PASS 10 bootstrap cases x 2 actual PowerShell entries; zero Fake dispatch')
