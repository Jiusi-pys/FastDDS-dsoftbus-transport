"""Same actual input on the Fake with full and limited observations."""
import subprocess
import sys
rows=[]
for mode in ('full','limited'):
    r=subprocess.run([sys.argv[1],mode],capture_output=True,text=True,timeout=20)
    assert r.returncode==0,(mode,r.returncode,r.stderr)
    result=[s for s in r.stdout.splitlines() if s.startswith('FAKE_ACTIVITY ')]
    assert len(result)==1,(mode,result)
    rows.append(result[0])
assert rows[0]==rows[1],rows
print('PASS actual Fake complete/sampled same payload and responsibility counts:',rows[0])
