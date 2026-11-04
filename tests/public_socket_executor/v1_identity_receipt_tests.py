"""Independent raw export receipt negatives; synthetic data, no HDC driver."""
import hashlib,importlib.util,json,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('identity_receipts',REPO/'scripts/public_socket/shared_reattach/executor-route-query/query_receipt.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class IdentityReceipts(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.w=Path(self.temp.name)
  self.c=dict(run='1234567890abcdef',hdc_path='UNIT_HDC_NOT_EXECUTED',targets=dict(A='UNIT_A'),remote=dict(A='/data/local/tmp/mdds_dds_1234567890abcdef_A'))
  self.root=self.c['remote']['A'];self.output=self.root+'/local-udid.bin';self.image=self.w/'A/identity/local-udid.bin';self.image.parent.mkdir(parents=True)
  self.image.write_bytes(b'unit-udid');self.sha=hashlib.sha256(self.image.read_bytes()).hexdigest()
  wrapper='; mdds_rc=$?; echo MDDS_REMOTE_EXIT=$mdds_rc'
  metadata="if test ! -e "+self.output+" && test ! -L "+self.output+"; then echo MDDS_IDENTITY_ABSENT=1; else test -f "+self.output+" && test ! -L "+self.output+" && stat -c 'MDDS_IDENTITY mode=%a uid=%u bytes=%s links=%h inode=%i' "+self.output+" && sha256sum "+self.output+" && stat -c 'MDDS_IDENTITY_PARENT uid=%u' "+self.root+"; fi"
  self.step('identity-file-metadata',['shell',metadata+wrapper],f'MDDS_IDENTITY mode=600 uid=0 bytes=9 links=1 inode=123\n{self.sha}  {self.output}\nMDDS_IDENTITY_PARENT uid=0\nMDDS_REMOTE_EXIT=0\n')
  self.step('identity-file-receive',['file','recv',self.output,str(self.image)],'SYNTHETIC_TRANSFER_COMPLETE\n')
  removal='test ! -L '+self.output+' && stat -c %i '+self.output+' | grep -qx 123 && sha256sum '+self.output+' | grep -q '+self.sha+' && rm '+self.output+' && test ! -e '+self.output+' && test ! -L '+self.output
  self.step('identity-file-remove-owned',['shell',removal+wrapper],'MDDS_REMOTE_EXIT=0\n')
  self.summary=dict(run=self.c['run'],scope='PRIVATE_IDENTITY_EXPORT_ONLY',**{'pass':True},sides=dict(A=dict(present=True,remote_absent=True,path=self.output,local_path=str(self.image),bytes=9,sha256=self.sha,mode='600',uid='0',inode='123',disposition='EXACT_BYTES_RECEIVED_AND_OWNED_EXPORT_REMOVED')))
  self.put(self.w/'IDENTITY_ARTIFACT_CLEANUP.json',self.summary)
 def put(self,p,x):p.write_text(json.dumps(x),encoding='utf-8')
 def step(self,name,args,stdout):
  p=self.w/'A'/name;p.mkdir()
  self.put(p/'command.json',[self.c['hdc_path'],'-t','UNIT_A',*args]);(p/'exit-code.txt').write_text('0');(p/'stdout.log').write_text(stdout);(p/'stderr.log').write_text('')
 def check(self):return m.verify_identity_export_receipts(self.c,self.w,'A',lambda p:Path(p).read_bytes(),lambda p:json.loads(Path(p).read_text()))
 def test_positive_raw_receipts(self):self.assertTrue(self.check()['remote_absent'])
 def test_summary_cannot_hide_failed_remove(self):
  (self.w/'A/identity-file-remove-owned/exit-code.txt').write_text('1')
  self.assertRaises(ValueError,self.check)
 def test_remote_failure_not_hidden_by_hdc_zero(self):
  (self.w/'A/identity-file-remove-owned/stdout.log').write_text('MDDS_REMOTE_EXIT=1\n')
  self.assertRaises(ValueError,self.check)
 def test_wrong_target_and_remove_inode(self):
  p=self.w/'A/identity-file-remove-owned/command.json';x=json.loads(p.read_text());x[2]='FOREIGN';self.put(p,x)
  self.assertRaises(ValueError,self.check)
  x[2]='UNIT_A';x[-1]=x[-1].replace('grep -qx 123','grep -qx 124');self.put(p,x)
  self.assertRaises(ValueError,self.check)
 def test_wrong_mode_and_parent_owner(self):
  p=self.w/'A/identity-file-metadata/stdout.log';original=p.read_text();p.write_text(original.replace('mode=600','mode=644'))
  self.assertRaises(ValueError,self.check)
  p.write_text(original.replace('PARENT uid=0','PARENT uid=1'));self.assertRaises(ValueError,self.check)
 def test_transfer_bytes_not_summary_expected(self):
  self.image.write_bytes(b'other-id!');self.assertRaises(ValueError,self.check)
 def test_missing_removal_absence_contract(self):
  p=self.w/'A/identity-file-remove-owned/command.json';x=json.loads(p.read_text());x[-1]=x[-1].replace(' && test ! -e '+self.output,'');self.put(p,x)
  self.assertRaises(ValueError,self.check)

if __name__=='__main__':unittest.main()
