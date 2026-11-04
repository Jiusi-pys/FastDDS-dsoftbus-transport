"""Bounded production RTTI reader with injected memory, never target evidence."""
import importlib.util,sys,types,unittest,time,collections
from pathlib import Path
from unittest.mock import patch

class Error:
 def __init__(self):self.bad=False
 def Fail(self):return self.bad
 def __str__(self):return 'INJECTED_READ_FAILURE' if self.bad else 'success'
api=types.ModuleType('lldb');api.SBError=Error
with patch.dict(sys.modules,{'lldb':api}):
 spec=importlib.util.spec_from_file_location('rtti_reader',Path(__file__).resolve().parents[2]/'scripts/public_socket/rmw_lldb_trace.py')
 m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
PROFILE='0ab1972136307f84ba55ca353c533708175aaa0bfbc60ab8762c49257e4e6a5e'

class Memory:
 def __init__(self):self.bytes={};self.reads=[]
 def put(self,address,data):self.bytes.update({address+i:b for i,b in enumerate(data)})
 def word(self,address,value):self.put(address,value.to_bytes(8,'little'))
 def name(self,address,value):self.put(address,value.encode()+b'\0'+b'\0'*128)
 def ReadMemory(self,address,size,error):
  self.reads.append((address,size))
  if any(address+i not in self.bytes for i in range(size)):error.bad=True;return b''
  return bytes(self.bytes[address+i] for i in range(size))

class Snapshot(unittest.TestCase):
 def setUp(self):
  self.mem=Memory();self.trace=m.Trace.__new__(m.Trace);self.trace.process=self.mem;self.trace.fastrtps_sha=PROFILE
  self.trace.rtti_pairs={};self.trace.module_at=lambda address:'INJECTED_MODULE_NOT_DEVICE'
  def header(address,nameptr,name,vptr):
   self.mem.word(address,vptr);self.mem.word(address+8,nameptr);self.mem.name(nameptr,name);self.mem.word(vptr+40,0x7000)
  header(0x1000,0x9000,'N8eprosima7fastdds3dds16TopicDescriptionE',0x6000)
  header(0x2000,0xa000,'N8eprosima7fastdds3dds5TopicE',0x6100)
  header(0x3000,0xb000,'N8eprosima7fastdds3dds12DomainEntityE',0x6200)
  header(0x4000,0xc000,'N8eprosima7fastdds3dds16TopicDescriptionE',0x6000)
  self.mem.word(0x2010,2<<32);self.mem.word(0x2018,0x3000);self.mem.word(0x2020,2)
  self.mem.word(0x2028,0x4000);self.mem.word(0x2030,(72<<8)|2)
 def test_actual_reader_records_distinct_source_and_base_identity(self):
  row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000)
  self.assertEqual(row['status'],'CAPTURED_BOUND_TOPIC_LAYOUT');self.assertEqual(row['bases'][1]['header']['address'],0x4000)
  self.assertFalse(row['bases'][1]['source_rtti_equal']);self.assertFalse(row['bases'][1]['source_name_pointer_equal'])
  self.assertEqual(row['source']['name'],row['bases'][1]['header']['name']);self.assertEqual(row['target']['slot_40'],0x7000)
 def test_same_pair_references_saved_snapshot_without_second_read(self):
  self.trace.rtti_snapshot(0x1000,0x2000,0x2000);n=len(self.mem.reads)
  row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000);self.assertEqual(row['status'],'REFERENCED_SNAPSHOT');self.assertEqual(n,len(self.mem.reads))
 def test_unknown_profile_has_no_memory_reads(self):
  self.trace.fastrtps_sha='0'*64;row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000)
  self.assertEqual(row['status'],'UNBOUND_PROFILE');self.assertFalse(self.mem.reads)
 def test_invalid_memory_and_oversize_layout_are_not_complete(self):
  row=self.trace.rtti_snapshot(0x5550,0x2000,0x2000);self.assertNotEqual(row['source']['status'],'CAPTURED')
  self.trace.rtti_pairs={};self.mem.word(0x2010,200<<32)
  row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000);self.assertEqual(row['status'],'LAYOUT_NOT_CONFIRMED');self.assertNotIn('bases',row)
 def test_names_and_pairs_are_bounded(self):
  self.mem.put(0x9000,b'X'*128);row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000)
  self.assertTrue(row['source']['name_truncated']);self.assertIsNone(row['source']['name'])
  self.trace.rtti_pairs={(i,2,2):i for i in range(4)};n=len(self.mem.reads)
  self.assertEqual(self.trace.rtti_snapshot(10,20,20)['status'],'SNAPSHOT_LIMIT');self.assertEqual(n,len(self.mem.reads))
 def test_missing_base_offset_is_partial_not_complete(self):
  for address in (0x2020,0x2030):
   with self.subTest(address=address):
    saved={a:self.mem.bytes.pop(a) for a in range(address,address+8)};self.trace.rtti_pairs={}
    row=self.trace.rtti_snapshot(0x1000,0x2000,0x2000)
    self.assertEqual(row['status'],'CAPTURED_WITH_READ_FAILURE')
    self.assertIsNone(row['bases'][0 if address==0x2020 else 1]['offset_flags_raw'])
    self.mem.bytes.update(saved)
 def test_cast_hit_reuses_existing_dynamic_rtti_word(self):
  trace=self.trace;trace.fastrtps_sha='0'*64;trace.start=time.monotonic();trace.hits=0;trace.disabled=False
  trace.cfg=dict(seconds=10,hit_limit=256);trace.breakpoints={1:'CAST_CALL'};trace.layout=dict(got=0x100,cast_return=0)
  trace.slide=0;trace.callees=set();reads=[];rows=[]
  def word(address):reads.append(address);return {0x5000:0x5100,0x50f8:0x2000,0x50f0:0,0x100:0x7000}.get(address)
  trace.word=word;trace.reg=lambda f,n:dict(x0=0x5000,x1=0x1000,x2=0x2000,x3=72)[n]
  trace.emit=lambda event,**fields:rows.append(fields)
  frame=types.SimpleNamespace(GetThread=lambda:types.SimpleNamespace(GetThreadID=lambda:123),GetPC=lambda:0x1000)
  loc=types.SimpleNamespace(GetBreakpoint=lambda:types.SimpleNamespace(GetID=lambda:1))
  trace.hit(frame,loc);self.assertEqual(collections.Counter(reads)[0x50f8],1)
  self.assertEqual(len(reads),4);self.assertEqual(rows[0]['rtti_snapshot']['status'],'UNBOUND_PROFILE')
if __name__=='__main__':unittest.main()
