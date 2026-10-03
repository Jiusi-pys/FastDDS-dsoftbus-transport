"""Actual guarded SIGCONT shell on one controlled Host process, no HDC."""
from pathlib import Path
import importlib.util,subprocess,unittest
REPO=Path(__file__).resolve().parents[2]
p=REPO/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
s=importlib.util.spec_from_file_location('resume_shell',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
class Resume(unittest.TestCase):
 def test_wrong_birth_wrong_exe_refuse_and_exact_stopped_owner_resumes(self):
  good=m.resume_command(12345,'/usr/bin/sleep','123456',['/usr/bin/sleep','20'])
  good=good.replace('/proc/12345','/proc/$p').replace('kill -CONT 12345','kill -CONT "$p"').replace("'123456'",'"$birth"')
  wrong_birth=good.replace('"$birth"',"'0'")
  wrong_exe=good.replace("= '/usr/bin/sleep'","= '/nonexistent/unit'")
  script='''set -eu
/usr/bin/sleep 20 &
p=$!
trap 'kill -CONT "$p" 2>/dev/null || true; kill "$p" 2>/dev/null || true; wait "$p" 2>/dev/null || true' EXIT
i=0
while [ "$(readlink /proc/$p/exe)" != /usr/bin/sleep ]; do i=$((i+1)); [ "$i" -lt 128 ] || exit 9; done
s=$(cat /proc/$p/stat); f=${s##*) }; set -- $f; shift 19; birth=$1
kill -STOP "$p"
i=0
while ! grep -q '^State:.*T' /proc/$p/status; do i=$((i+1)); [ "$i" -lt 128 ] || exit 10; done
set +e
'''+wrong_birth+'''
set -e
grep -q '^State:.*T' /proc/$p/status
set +e
'''+wrong_exe+'''
set -e
grep -q '^State:.*T' /proc/$p/status
set +e
'''+good+'''
set -e
i=0
while grep -q '^State:.*T' /proc/$p/status; do i=$((i+1)); [ "$i" -lt 128 ] || exit 11; done
echo EXACT_OWNED_PROCESS_RESUMED
'''
  result=subprocess.run(['wsl','-d','Ubuntu-20.04','--','sh','-s'],input=script.encode(),capture_output=True,timeout=15)
  self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
  self.assertEqual(result.stdout.decode().splitlines(),['MDDS_REMOTE_EXIT=6','MDDS_REMOTE_EXIT=6','MDDS_REMOTE_EXIT=0','EXACT_OWNED_PROCESS_RESUMED'])
if __name__=='__main__':unittest.main()
