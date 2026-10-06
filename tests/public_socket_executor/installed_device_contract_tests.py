"""Installed real-mode contract/claim unit tests; never dispatch any tool."""
import hashlib,json,unittest
from pathlib import Path
from flow_tests import Flow,put,read,sha

class InstalledDevice(unittest.TestCase):
 def setUp(self):
  self.flow=Flow();self.flow.setUp();self.c=self.flow.c;self.task=self.flow.task
  self.driver=Path(self.flow.tmp.name)/'sdk-tool/hdc.exe';self.driver.parent.mkdir();self.driver.write_bytes(b'UNIT_ONLY_NEVER_EXECUTED')
  self.c.PRIVATE['hdc_path']=str(self.driver)
  self.f=dict(scope=self.c.INSTALLED_SCOPE,device_authorized=True,device_driver=dict(path=str(self.driver),sha256=sha(self.driver)),limits=self.c.INSTALLED_LIMITS)
 def tearDown(self):self.flow.tearDown()
 def test_real_mode_rejects_every_local_authority_input(self):
  self.c.verify_installed_device_mode(self.f)
  for k,v in [('local_contract_test',True),('local_driver',{}),('local_test_basis','fake'),('device_evidence',True)]:
   with self.assertRaises(ValueError):self.c.verify_installed_device_mode(dict(self.f,**{k:v}))
  for v in (False,1,'true',None):
   with self.assertRaises(ValueError):self.c.verify_installed_device_mode(dict(self.f,device_authorized=v))
  with self.assertRaises(ValueError):self.c.verify_installed_device_mode(self.f,dict(simulated=False))
  self.driver.write_bytes(b'DRIFT')
  with self.assertRaises(ValueError):self.c.verify_installed_device_mode(self.f)
 def test_consumed_query_accepted_readonly_but_not_reclaimed(self):
  cfg=dict(run='0123456789abcdef',window_root=str(self.task/'windows/query'),installed_attempt=1)
  claim=self.c.installed_claim_path('query',1);claim.parent.mkdir(parents=True)
  put(claim,dict(scope=self.c.INSTALLED_SCOPE,attempt=1,kind='INSTALLED_QUERY_DISPATCH_INTENT',run=cfg['run'],window=cfg['window_root']))
  self.c.verify_installed_query_claim(cfg)
  self.assertRaises(FileExistsError,lambda:claim.open('x'))
  put(claim,dict(read(claim),attempt=2))
  with self.assertRaises(ValueError):self.c.verify_installed_query_claim(cfg)
 def test_budget_requires_consumption_restore_and_repair(self):
  self.assertEqual(self.c.installed_next_attempt(),1)
  for v in (0,3,True,'1'):
   with self.assertRaises(ValueError):self.c.installed_claim_path('query',v)
  w=self.task/'windows/failed-real-unit';w.mkdir();cfg=dict(run='0123456789abcdef',window_root=str(w),installed_attempt=1)
  ledger=self.c.installed_claim_path('data',1);ledger.parent.mkdir(parents=True)
  put(ledger,dict(scope=self.c.INSTALLED_SCOPE,attempt=1,kind='DATA_DISPATCH_INTENT',run=cfg['run'],window=str(w)))
  put(w/'DATA_ATTEMPT_STARTED',read(ledger));put(w/'window.json',cfg)
  # No normal restoration -> cannot get the repair window or query.
  with self.assertRaises((ValueError,OSError)):self.c.installed_next_attempt()

if __name__=='__main__':unittest.main()
