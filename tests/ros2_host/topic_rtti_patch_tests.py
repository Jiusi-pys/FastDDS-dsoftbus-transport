"""Exact patch boundaries; target object import proof is a separate compile."""
import importlib.util,json,tempfile,unittest,os,subprocess
from pathlib import Path

REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('topic_key',REPO/'integrations/ros2/apply_topic_rtti_key.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
BASE=REPO/'build/ros2-target/src/fastdds'
class KeyPatch(unittest.TestCase):
 def fixture(self):
  temp=tempfile.TemporaryDirectory(dir=REPO/'build/ros2-target/tmp');self.addCleanup(temp.cleanup)
  root=Path(temp.name)
  for row in json.loads((m.HERE/'TOPIC_RTTI_UPSTREAM.json').read_text())['files']:
   p=root/row['path'];p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((BASE/row['path']).read_bytes())
  return root
 def test_only_empty_body_moves_preserving_members_and_virtual_order(self):
  root=self.fixture();h=root/'include/fastdds/dds/topic/TopicDescription.hpp';before=h.read_text()
  result=m.apply(root);after=h.read_text();cpp=(root/'src/cpp/fastdds/topic/Topic.cpp').read_text()
  self.assertEqual(len(result['files']),2);self.assertIn('RTPS_DllAPI virtual ~TopicDescription();',after)
  self.assertIn('TopicDescription::~TopicDescription()\n{\n}',cpp)
  self.assertEqual(before.count('virtual '),after.count('virtual '))
  self.assertEqual(before[before.index('    std::string name_;'):],after[after.index('    std::string name_;'):])
 def test_mismatch_rejects_before_any_file_change(self):
  root=self.fixture();cpp=root/'src/cpp/fastdds/topic/Topic.cpp';cpp.write_text(cpp.read_text()+'\n')
  h=root/'include/fastdds/dds/topic/TopicDescription.hpp';before=h.read_bytes()
  self.assertRaisesRegex(ValueError,'upstream input mismatch',m.apply,root);self.assertEqual(before,h.read_bytes())
 def test_reapplication_is_rejected(self):
  root=self.fixture();m.apply(root);self.assertRaisesRegex(ValueError,'receipt already exists',m.apply,root)
 def test_original_source_repository_is_never_accepted(self):
  self.assertRaisesRegex(ValueError,'isolated',m.apply,REPO/'ros2/src/eProsima/Fast-DDS')
 def test_nested_directory_link_cannot_modify_external_header(self):
  source=self.fixture();victim=self.fixture()
  link=source/'include/fastdds/dds/topic';header=link/'TopicDescription.hpp'
  header.unlink();link.rmdir()
  target=victim/'include/fastdds/dds/topic';before=(target/'TopicDescription.hpp').read_bytes()
  if os.name=='nt':
   created=subprocess.run(['cmd.exe','/c','mklink','/J',str(link),str(target)],capture_output=True)
   self.assertEqual(created.returncode,0,created.stderr)
  else:link.symlink_to(target,target_is_directory=True)
  try:
   self.assertRaisesRegex(ValueError,'linked|escape',m.apply,source)
   self.assertEqual(before,(target/'TopicDescription.hpp').read_bytes())
   self.assertFalse((source/'FDDST_TOPIC_RTTI_PATCH.json').exists())
  finally:
   if os.name=='nt':os.rmdir(link)
   else:link.unlink()
 def test_hardlinked_member_is_rejected_without_change(self):
  source=self.fixture();victim=self.fixture()
  header=source/'include/fastdds/dds/topic/TopicDescription.hpp';other=victim/'include/fastdds/dds/topic/TopicDescription.hpp'
  header.unlink();os.link(other,header);before=other.read_bytes()
  self.assertRaisesRegex(ValueError,'linked',m.apply,source);self.assertEqual(before,other.read_bytes())
 def test_linked_receipt_rejected_before_source_write(self):
  source=self.fixture();victim=self.fixture()
  target=victim/'receipt.json';target.write_text('PRESERVE')
  os.link(target,source/'FDDST_TOPIC_RTTI_PATCH.json')
  header=source/'include/fastdds/dds/topic/TopicDescription.hpp';before=header.read_bytes()
  self.assertRaisesRegex(ValueError,'linked',m.apply,source)
  self.assertEqual(target.read_text(),'PRESERVE');self.assertEqual(before,header.read_bytes())
if __name__=='__main__':unittest.main()
