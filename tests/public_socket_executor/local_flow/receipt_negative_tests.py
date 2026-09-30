"""Formal postrun/claim tests against new local test evidence, never old history."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(sys.argv[1]).resolve();repo=Path(__file__).resolve().parents[3]
if root.name!='local-negative-20260930-wrong_main':raise ValueError('Dedicated negative evidence only')
w=root/'windows/query-2';guard=w/'B/final-guard.json';original=guard.read_bytes();h=hashlib.sha256(original).hexdigest()
logs=root/'receipt-negative-results';logs.mkdir();guard.rename(logs/'original-final-guard.json')
try:
 p=subprocess.run([sys.executable,str(root/'executor-route-query/route_query.py'),str(w),'--output',str(root/'missing-guard-replay')],cwd=repo,capture_output=True,timeout=30)
 (logs/'missing-guard.stdout').write_bytes(p.stdout);(logs/'missing-guard.stderr').write_bytes(p.stderr)
 assert p.returncode!=0 and not (root/'missing-guard-replay/PRIVATE_ROUTE_BINDINGS.json').exists()
finally:guard.write_bytes(original)
assert hashlib.sha256(guard.read_bytes()).hexdigest()==h
claim=root/'local-test-claims'/('query-'+json.loads((w/'window.json').read_text())['run']+'.json');old=claim.read_bytes()
p=subprocess.run([sys.executable,str(root/'executor/task_contract.py'),'--claim-query',str(w/'window.json')],cwd=repo,capture_output=True,timeout=30)
(logs/'duplicate-claim.stderr').write_bytes(p.stderr);assert p.returncode!=0 and claim.read_bytes()==old
p=subprocess.run([sys.executable,str(root/'executor-route-query/route_query.py'),str(w),'--output',str(root/'consumed-claim-replay')],cwd=repo,capture_output=True,timeout=30)
(logs/'consumed-replay.stderr').write_bytes(p.stderr);assert p.returncode==0,p.stderr
cfg=json.loads((root/'windows/data-2/window.json').read_text());bad=logs/'simulated-marker.json'
for value in (None,False,'true',1):
 c=dict(cfg,simulated=value);bad.write_text(json.dumps(c))
 p=subprocess.run([sys.executable,str(root/'executor/task_contract.py'),'--check-window',str(bad)],cwd=repo,capture_output=True,timeout=30)
 assert p.returncode!=0
(logs/'RESULT.json').write_text(json.dumps(dict(missing_guard_rejected=True,duplicate_claim_rejected=True,consumed_claim_replay_pass=True,simulated_marker_rejections=4,restored_original_guard_sha256=h)))
print('PASS actual completed-query missing guard, consumed replay, duplicate claim and simulated marker negatives')
