"""Offline replay of preserved recovery, with hostile read seams, no HDC."""
from pathlib import Path
import copy,importlib.util,json,unittest
from unittest.mock import patch
REPO=Path(__file__).resolve().parents[2]
SOURCE=REPO/'scripts/public_socket/shared_reattach/executor/rmw_debugger.py'
s=importlib.util.spec_from_file_location('resource_reader_under_test',SOURCE);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
WINDOW=REPO/'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002/iterations/iteration-10/windows/data-8'
class ResourceReader(unittest.TestCase):
 def setUp(self):
  if not (WINDOW/'INDEPENDENT_RESOURCE_RESTORATION_ACCEPTANCE.json').is_file():self.skipTest('optional historical raw fixture not distributed')
 def modified_json(self,path,change):
  original=m.json.loads;data=path.read_text();replacement=original(data);change(replacement)
  return patch.object(m.json,'loads',side_effect=lambda raw,*a,**kw:copy.deepcopy(replacement) if raw==data else original(raw,*a,**kw))
 def test_actual_complete_resource_replay_keeps_old_failure(self):
  before=m.sha(WINDOW/'result.json');facts=m.resource_recovery_facts(WINDOW)
  self.assertEqual(facts['scope'],'REAL_DEBUGGER_AND_WINDOW_RESOURCE_RECOVERY');self.assertEqual(facts['actions_checked'],261)
  self.assertTrue(facts['current_environment_restored']);self.assertEqual(facts['normal_ros_close'],'FAIL_NOT_REPAIRED')
  self.assertTrue(json.loads((WINDOW/'result.json').read_text())['timedOut']);self.assertEqual(before,m.sha(WINDOW/'result.json'))
 def test_simulated_or_local_has_no_device_authority(self):
  for key in ('simulated','local_contract_test'):
   with self.modified_json(WINDOW/'window.json',lambda v:v.update({key:True})):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_missing_side_and_metadata_difference_rejected(self):
  p=WINDOW/'remaining-resource-recovery/attempt-1/RESOURCE_RESTORATION_RESULT.json'
  with self.modified_json(p,lambda v:v['sides'].pop('B')):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
  with self.modified_json(p,lambda v:v['sides']['A']['metadata'].update(uid='1')):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_raw_tamper_and_open_child_rejected(self):
  original=m.sha;p=WINDOW/'remaining-resource-recovery/attempt-1/A/platform-hash-0.stdout'
  with patch.object(m,'sha',side_effect=lambda q:'0'*64 if Path(q).resolve()==p.resolve() else original(q)):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
  result=WINDOW/'remaining-resource-recovery/attempt-1/A/guard-current.result.json'
  with self.modified_json(result,lambda v:v.update(capture_complete=False)):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_missing_stream_is_not_empty_pass(self):
  original=Path.read_bytes;p=WINDOW/'remaining-resource-recovery/attempt-1/B/processes-current.stdout'
  def read(path):
   if path.resolve()==p.resolve():raise FileNotFoundError('UNIT_MISSING_STREAM')
   return original(path)
  with patch.object(Path,'read_bytes',read):self.assertRaises(FileNotFoundError,m.resource_recovery_facts,WINDOW)
 def test_unknown_dispatch_rejected(self):
  original=Path.glob;folder=WINDOW/'remaining-resource-recovery/attempt-1/A'
  def glob(path,pattern,*args,**kwargs):
   rows=list(original(path,pattern,*args,**kwargs))
   if path==folder and pattern=='*.intent.json':rows.append(folder/'unknown-mutation.intent.json')
   return iter(rows)
  with patch.object(Path,'glob',glob):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_frozen_primitive_drift_rejected_before_import(self):
  original=m.sha;p=WINDOW.parent.parent/'recovery/pregrant_recovery.py'
  with patch.object(m,'sha',side_effect=lambda q:'0'*64 if Path(q).resolve()==p.resolve() else original(q)):
   self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_new_dispatch_outside_core_resource_folder_rejected(self):
  original=Path.rglob
  def glob(path,pattern,*args,**kwargs):
   rows=list(original(path,pattern,*args,**kwargs))
   if path==WINDOW and pattern=='*.intent.json':rows.append(WINDOW/'A/debugger/unknown-new.intent.json')
   return iter(rows)
  with patch.object(Path,'rglob',glob):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
 def test_missing_duplicate_or_wrong_token_hash_semantics_rejected(self):
  original=Path.read_text;p=WINDOW/'remaining-resource-recovery/attempt-1/A/token-hash-after-denied.stdout'
  text=p.read_text();good_line=next(line for line in text.splitlines() if line.startswith('ac52'))
  for altered in ('MDDS_REMOTE_EXIT=0\n',good_line+'\n'+good_line+'\nMDDS_REMOTE_EXIT=0\n','0'*64+good_line[64:]+'\nMDDS_REMOTE_EXIT=0\n'):
   def read(path,*args,**kwargs):return altered if path.resolve()==p.resolve() else original(path,*args,**kwargs)
   with patch.object(Path,'read_text',read):self.assertRaises(ValueError,m.resource_recovery_facts,WINDOW)
if __name__=='__main__':unittest.main()
