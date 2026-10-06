"""Real local POSIX guard execution; no HDC or private/device identity."""
import hashlib,importlib.util,json,subprocess,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('identity_cleanup',REPO/'scripts/public_socket/shared_reattach/executor-route-query/query_receipt.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CleanupShell(unittest.TestCase):
 def setUp(self):
  self.shell=Path('C:/Program Files/Git/bin/sh.exe')
  if not self.shell.is_file():self.skipTest('Existing Git POSIX shell unavailable')
  parent=REPO/'build/v1-identity-cleanup-tests';parent.mkdir(exist_ok=True)
  self.temp=tempfile.TemporaryDirectory(dir=parent);self.addCleanup(self.temp.cleanup)
  self.path=Path(self.temp.name)/'local-udid.bin';self.path.write_bytes(b'SYNTHETIC_UNIT_IDENTITY')
  self.scope='/data/local/tmp/mdds_dds_1234567890abcdef_A/local-udid.bin'
  self.inode=self.run_shell('stat -c %i '+self.path.as_posix()).stdout.strip()
  self.hash=hashlib.sha256(self.path.read_bytes()).hexdigest()
 def run_shell(self,command):return subprocess.run([str(self.shell),'-c',command],text=True,capture_output=True,timeout=10)
 def guard(self,inode=None,digest=None):
  command=m.identity_removal_command(self.scope,inode or self.inode,digest or self.hash)
  self.assertNotIn('$(',command);self.assertNotIn('"',command)
  # Fixture-only mapping into repo build; production path remains strictly fixed.
  return self.run_shell(command.replace(self.scope,self.path.as_posix()))
 def test_exact_inode_hash_removes_and_proves_absence(self):
  result=self.guard();self.assertEqual(result.returncode,0,result.stderr);self.assertFalse(self.path.exists())
 def test_wrong_inode_preserves_original(self):
  result=self.guard(inode=str(int(self.inode)+1));self.assertNotEqual(result.returncode,0);self.assertEqual(self.path.read_bytes(),b'SYNTHETIC_UNIT_IDENTITY')
 def test_wrong_hash_preserves_original(self):
  result=self.guard(digest='0'*64);self.assertNotEqual(result.returncode,0);self.assertTrue(self.path.exists())
 def test_replaced_file_same_path_cannot_use_old_hash(self):
  self.path.write_bytes(b'REPLACED_UNIT_CONTENT');self.assertNotEqual(self.guard().returncode,0);self.assertEqual(self.path.read_bytes(),b'REPLACED_UNIT_CONTENT')
 def test_foreign_path_and_injection_rejected(self):
  for path in ('/tmp/local-udid.bin',self.scope+';echo bad',self.scope+'/../foreign'):
   self.assertRaises(ValueError,m.identity_removal_command,path,self.inode,self.hash)
  self.assertRaises(ValueError,m.identity_removal_command,self.scope,'1;echo bad',self.hash)
 def test_recorded_failure_shape_is_excluded_by_new_contract(self):
  # Concrete device RED: quoted command substitution failed before removal.
  failed='test "$(stat -c %i '+self.scope+')" = "123"'
  self.assertIn('$(',failed);self.assertNotIn('$(',m.identity_removal_command(self.scope,self.inode,self.hash))

if __name__=='__main__':unittest.main()
