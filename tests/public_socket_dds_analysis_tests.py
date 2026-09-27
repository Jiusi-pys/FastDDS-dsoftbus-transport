import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts/public_socket"))
import unittest,copy
from analyze_dds import validate_pair,parse_board

def good():
 rows={}
 for s,d in [('A','B'),('B','A')]:
  pw=s*24;remote=d*24;w=pw+'00000102';rd=pw+'00000107';rw=remote+'00000102';rr=remote+'00000107'
  rows[s]={'run':'0123456789abcdef','side':s,'factory':True,'config':True,'writer':w,'reader':rd,'remote_writer':rw,'remote_reader':rr,'writes':[{'ordinal':n,'sn':n,'writer':w,'rc':0} for n in range(1,6)],'takes':[{'ordinal':n,'sn':n,'writer':rw,'reader':rd,'run':'0123456789abcdef','valid':1} for n in range(1,6)],'frames':[],'wait':0,'deleted':True,'overflow':0}
  for stage,writer in [('SDK_SEND',w),('SDK_INGRESS',rw),('RX_OWNED',rw),('RX_RUNNING',rw)]:
   for n in range(1,6):rows[s]['frames'].append({'stage':stage,'kind':'DATA','writer':writer,'sn':n,'hash':(n if stage=='SDK_SEND' else n),'owner':1,'app':1,'receiver':1,'admitted':1,'entered':1})
  for kind in ['SPDP','SEDP','HEARTBEAT']:
   for stage in ['SDK_SEND','RX_RUNNING']:rows[s]['frames'].append({'stage':stage,'kind':kind,'writer':'','sn':0})
 return rows
class Tests(unittest.TestCase):
 def test_good(self):self.assertEqual(validate_pair(good()),[])
 def bad(self,mutate):x=good();mutate(x);self.assertTrue(validate_pair(x))
 def test_missing(self):self.bad(lambda x:x['B']['takes'].pop())
 def test_duplicate(self):self.bad(lambda x:x['A']['takes'].__setitem__(4,x['A']['takes'][0]))
 def test_local_match(self):self.bad(lambda x:x['A'].__setitem__('remote_writer',x['A']['writer']))
 def test_wrong_reader(self):self.bad(lambda x:x['A']['takes'][0].__setitem__('reader',x['B']['reader']))
 def test_wrong_sn(self):self.bad(lambda x:x['A']['takes'][0].__setitem__('sn',99))
 def test_old_run(self):self.bad(lambda x:x['A']['takes'][0].__setitem__('run','f'*16))
 def test_wait_only(self):self.bad(lambda x:x['A'].__setitem__('takes',[]))
 def test_sdk_missing(self):self.bad(lambda x:x['A'].__setitem__('frames',[e for e in x['A']['frames'] if e['stage']!='SDK_SEND']))
 def test_binding_missing(self):self.bad(lambda x:x['B'].__setitem__('frames',[e for e in x['B']['frames'] if e['stage']!='RX_OWNED']))
 def test_ticket_missing(self):self.bad(lambda x:x['A']['frames'][0].__setitem__('entered',0))
 def test_hash_mismatch(self):self.bad(lambda x:x['B']['frames'][5].__setitem__('hash',999))
 def test_overflow(self):self.bad(lambda x:x['A'].__setitem__('overflow',1))
 def test_factory(self):self.bad(lambda x:x['A'].__setitem__('factory',False))
 def test_cleanup(self):self.bad(lambda x:x['A'].__setitem__('deleted',False))
 def test_qos(self):self.bad(lambda x:x['A'].__setitem__('config',False))
class TraceTests(unittest.TestCase):
 def parse(self,trace,api,exit_,frames='',events=''):
  cfg={'run':'0123456789abcdef','source_state_sha256':'x','header_sha256':{'socket':'s','trans_type':'t'}}
  return parse_board(trace+'\n'+api+'\n'+exit_+'\n'+frames+'\n'+events,cfg,'A')
 def test_negative_overflow_cannot_cancel(self):
  b=self.parse('DDS_FRAME_SUMMARY count=0 limit=4096 overflow=-1','PROBE_TRACE count=0 capacity=1024 overflow=1','DDS_EXIT trace_overflow=0 frame_overflow=0')
  self.assertNotEqual(b['overflow'],0)
 def test_missing_frame_count(self):
  b=self.parse('DDS_FRAME_SUMMARY count=2 limit=4096 overflow=0','PROBE_TRACE count=0 capacity=1024 overflow=0','DDS_EXIT trace_overflow=0 frame_overflow=0','DDS_FRAME event=1 packet=1 stage=SDK_SEND kind=DATA socket=1 length=44 hash=1 writer=aaa sn=1')
  self.assertIn('FRAME_TRACE_COMPLETENESS',b['errors'])
 def test_api_sequence_gap(self):
  b=self.parse('DDS_FRAME_SUMMARY count=0 limit=4096 overflow=0','PROBE_TRACE count=2 capacity=1024 overflow=0','DDS_EXIT trace_overflow=0 frame_overflow=0',events='PROBE_API event=1 name=other\nPROBE_API event=3 name=other')
  self.assertIn('API_TRACE_COMPLETENESS',b['errors'])
if __name__=='__main__':unittest.main()
