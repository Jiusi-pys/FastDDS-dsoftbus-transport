"""Synthetic raw-observation acceptance negatives; no real identity fixture."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[2];spec=importlib.util.spec_from_file_location('v1_analysis',REPO/'scripts/public_socket/analyze_v1_locators.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Locators(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory(dir=REPO/'build');self.addCleanup(self.temp.cleanup);self.w=Path(self.temp.name);self.run='1234567890abcdef';(self.w/'window.json').write_text(json.dumps(dict(run=self.run)))
  self.expected={side:dict(kind='40000000',address=('11' if side=='A' else '22')*16,port='01000001') for side in 'AB'}
  for side,other in [('A','B'),('B','A')]:
   lines=[]
   for direction,owner,obs in [('SDK_SEND',side,1),('SDK_INGRESS',other,2)]:
    for pid in ('0031','0032'):
     e=self.expected[owner];lines.append(f'ROS_V1_SPDP run={self.run} side={side} observation={obs} direction={direction} socket=7 writer={"aa"*12}000100c2 sequence_high=0 sequence_low=7 data_offset=20 pid={pid} bytes=24 kind={e["kind"]} address={e["address"]} port={e["port"]}')
   e=self.expected[other];lines.append(f'ROS_V1_SELECTED run={self.run} side={side} kind={e["kind"]} address={e["address"]} port={e["port"]} scope=ACTUAL_SELECTOR_VALUES');lines.append(f'ROS_V1_OBSERVATION run={self.run} side={side} captured=1 adverts=4 selected=1 faults=0 sdk_epoch=NOT_PROVEN')
   (self.w/(side+'.raw.stdout.log')).write_text('\n'.join(lines)+'\n')
 def check(self):return m.analyze(self.w,self.expected)
 def change(self,old,new):
  p=self.w/'A.raw.stdout.log';p.write_text(p.read_text().replace(old,new))
 def test_complete_raw_groups_and_full_selection(self):self.assertTrue(self.check()['pass'])
 def test_wrong_address_profile_slot_kind(self):
  p=self.w/'A.raw.stdout.log';original=p.read_text()
  for old,new in [('address='+'11'*16,'address='+'33'*16),('port=01000001','port=02000001'),('port=01000001','port=01000002'),('kind=40000000','kind=00000001')]:
   p.write_text(original.replace(old,new));self.assertRaises(ValueError,self.check)
 def test_same_writer_sn_separate_data_cannot_fake_meta_user_pair(self):
  p=self.w/'A.raw.stdout.log';lines=p.read_text().splitlines();lines[1]=lines[1].replace('data_offset=20','data_offset=80');p.write_text('\n'.join(lines));self.assertRaises(ValueError,self.check)
 def test_duplicate_conflicting_locator_not_collapsed(self):
  p=self.w/'A.raw.stdout.log';lines=p.read_text().splitlines();lines.insert(1,lines[0].replace('address='+'11'*16,'address='+'33'*16));p.write_text('\n'.join(lines));self.assertRaises(ValueError,self.check)
 def test_missing_peer_or_selection_and_overflow(self):
  p=self.w/'A.raw.stdout.log';original=p.read_text()
  for value in ['\n'.join(line for line in original.splitlines() if 'SDK_INGRESS' not in line),'\n'.join(line for line in original.splitlines() if not line.startswith('ROS_V1_SELECTED ')),original.replace('faults=0','faults=1')]:
   p.write_text(value);self.assertRaises(ValueError,self.check)
 def test_independent_frozen_codec_vector(self):
  value=m.derive(dict(namespace='alpha',domain=0x01020304,udid='udid-1'))
  self.assertEqual(value['address'],'c3797784976ed68cef55b2f0f22e8cc4')
if __name__=='__main__':unittest.main()
