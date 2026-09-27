import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts/public_socket"))
import unittest
from analyze_probe import side_report
C={'run':'0123456789abcdef','scenario':'socket','source_state_sha256':'a'*64,'header_sha256':{'socket.h':'b'*64,'trans_type.h':'d'*64}}
def fixture():
 lines=[f"PROBE_MAIN run={C['run']} side=A pid=1 mode=socket",f"PROBE_BUILD source_receipt={C['source_state_sha256']} socket_header={'b'*64} trans_type_header={'d'*64}"]
 lines += ['PROBE_SYMBOL name='+s+' origin=/system/lib64/platformsdk/libsoftbus_client.z.so' for s in ['Socket','Listen','Bind','SendBytes','Shutdown']]
 lines += ['PROBE_OWNERS stage=READY outgoing=1 listener=2 accepted=3']
 events=['name=OnNegotiate_result socket=3 value=1','name=OnBind socket=3 value=2']
 for i in range(1,6):
  lines += [f'PROBE_SEND side=0 sequence={i} returned=1 code=0 outcome=1',f'PROBE_RECEIVER valid=1 direction=1 sequence={i} length=256']
  events += ['name=SendBytes_return socket=1 value=0 length=256','name=OnBytes socket=3 length=256']
 events += ['name=Shutdown_enter socket=1','name=Shutdown_void_return socket=1','name=Shutdown_enter socket=2','name=Shutdown_void_return socket=2','name=OnShutdown socket=3 value=0']
 lines += ['PROBE_API event='+str(i+1)+' '+s for i,s in enumerate(events)]
 lines += [f'PROBE_TRACE count={len(events)} overflow=0','PROBE_RESULT side=0 success=1 sent=5 received=5 invalid=0 drained=1 first_failure= sdk_drain=UNPROVEN','PROBE_EXIT pass=1 restored=1 trace_overflow=0']
 return '\n'.join(lines)
class Analyze(unittest.TestCase):
 def test_synthetic_positive(self):self.assertTrue(side_report(fixture(),C,'A')['pass_'])
 def test_missing_reader(self):self.assertFalse(side_report(fixture().replace('PROBE_RECEIVER valid=1 direction=1 sequence=5 length=256',''),C,'A')['pass_'])
 def test_wrong_source(self):self.assertFalse(side_report(fixture().replace('direction=1 sequence=1','direction=0 sequence=1'),C,'A')['pass_'])
 def test_duplicate(self):self.assertFalse(side_report(fixture().replace('direction=1 sequence=5','direction=1 sequence=4'),C,'A')['pass_'])
 def test_unknown_close(self):self.assertIn('OWNED_VOID_CLOSE_CALLS',side_report(fixture().replace('name=Shutdown_enter socket=1','name=Shutdown_enter socket=9'),C,'A')['errors'])
 def test_trace_missing(self):self.assertFalse(side_report(fixture().replace('PROBE_API event=1','MISSING event=1'),C,'A')['pass_'])
 def test_false_return(self):self.assertFalse(side_report(fixture().replace('returned=1 code=0','returned=0 code=0'),C,'A')['pass_'])
 def test_source_receipt(self):self.assertFalse(side_report(fixture().replace('source_receipt='+('a'*64),'source_receipt='+('b'*64)),C,'A')['pass_'])
 def test_source_library(self):self.assertFalse(side_report(fixture().replace('/system/lib64/platformsdk/libsoftbus_client.z.so','/data/old/libsoftbus_client.z.so'),C,'A')['pass_'])
 def test_late_error(self):self.assertFalse(side_report(fixture().replace('invalid=0 drained=1','invalid=1 drained=1'),C,'A')['pass_'])
 def test_missing_all_terminals(self):
  text=fixture();lines=[l for l in text.splitlines() if not any(x in l for x in ['name=Shutdown_','name=OnShutdown'])];n=0
  for i,l in enumerate(lines):
   if l.startswith('PROBE_API '):n+=1;lines[i]=__import__('re').sub(r'event=\d+',f'event={n}',l)
  lines=[__import__('re').sub(r'count=\d+',f'count={n}',l) if l.startswith('PROBE_TRACE ') else l for l in lines]
  self.assertIn('ALL_OWNERS_TERMINAL',side_report('\n'.join(lines),C,'A')['errors'])
 def test_header_mismatch(self):self.assertFalse(side_report(fixture().replace('socket_header='+'b'*64,'socket_header='+'c'*64),C,'A')['pass_'])
if __name__=='__main__':unittest.main()
