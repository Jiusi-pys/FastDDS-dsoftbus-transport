"""Full local snapshot recovery after a bounded pre-business failure.

Unknown/rejected command is not relabeled NO_STATE_CHANGE. Require intent/result
closure, actual ended children and initial sandbox state; invoke official guards.
"""
import hashlib,json,os,subprocess,sys
from pathlib import Path
from finite_model import read,write,live
w=Path(sys.argv[1]).resolve();task=w.parent.parent;sandbox=task/'sandbox';c=read(w/'window.json')
if task.name!='local-flow-20260930-isolated' or c['window_root']!=str(w):raise ValueError('Isolated root only')
f=read(c['task_contract']['freeze_path'])
if f.get('local_contract_test') is not True:raise ValueError('Local-only')
if (w/'DATA_ATTEMPT_STARTED').exists():raise ValueError('Data failure requires actual business cleanup')
rows=[json.loads(l) for l in (sandbox/'actions.jsonl').read_text().splitlines()]
rows=[r for r in rows if r['run']==c['run']]
intents=[r for r in rows if r['event']=='DISPATCH_INTENT'];ends=[r for r in rows if r['event']=='DISPATCH_COMPLETED']
if len(intents)!=len(ends) or not intents:raise ValueError('Incomplete action journal')
for r in intents:
 matches=[x for x in ends if x['pid']==r['pid'] and x['argv']==r['argv'] and x['window']==r['window']]
 if len(matches)!=1 or live(r['pid']):raise ValueError('Incomplete/running dispatch')
driver=[json.loads(l) for l in (sandbox/'fake-hdc-invocations.jsonl').read_text().splitlines() if json.loads(l)['test_run']==c['run']]
commands=list(w.glob('[AB]/*/command.json'))
if len(commands)!=len(driver) or len(driver)!=len(intents):raise ValueError('Command/driver mismatch')
for p in commands:
 argv=read(p)
 actual=argv[1:]
 # One observed WindowsPowerShell/CRT transformation, bound to this exact
 # tool-availability grammar. Never normalize arbitrary shell or quoted paths.
 if p.parent.name=='declared-tool-availability' and actual[:3]==['-t',c['targets'][p.parent.parent.name],'shell']:
  expected="env -i PATH=/system/bin:/system/xbin /system/bin/sh -c 'for mdds_tool in cat readlink stat sha256sum mv env timeout; do command -v \"$mdds_tool\" || exit 3; done'; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc"
  if actual[3]==expected:actual=actual[:3]+[expected.replace('"$mdds_tool"','$mdds_tool')]
 if len([x for x in driver if x['argv']==actual])!=1:raise ValueError('Command identity mismatch')
for p in (sandbox/'processes').glob('*.json'):
 if live(read(p)['pid']):raise ValueError('Local child still running')
if any(r['event'].startswith('CONSUMER') for r in rows):raise ValueError('Business cleanup required')
write(w/'LOCAL_RECOVERY_CLOSURE.json',dict(run=c['run'],window=str(w),commands=len(commands),complete=True,action_sha256=hashlib.sha256((sandbox/'actions.jsonl').read_bytes()).hexdigest(),driver_sha256=hashlib.sha256((sandbox/'fake-hdc-invocations.jsonl').read_bytes()).hexdigest(),process_absent=True))
ps=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe';env=os.environ.copy();env['PSModulePath']=os.environ['SystemRoot']+'/System32/WindowsPowerShell/v1.0/Modules'
p=subprocess.run([str(ps),'-NoProfile','-NonInteractive','-File',str(Path(__file__).with_suffix('.ps1')),'-WindowDirectory',str(w)],env=env,capture_output=True)
(w/'local-recovery.stdout').write_bytes(p.stdout);(w/'local-recovery.stderr').write_bytes(p.stderr)
sys.exit(p.returncode)
