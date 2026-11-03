"""Adversarial analysis of supplied Host receipts, never native SDK evidence."""
import argparse,sys,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/public_socket'))
from analyze_bounded_native_reaccept import analyze

def check(root,order):
 texts={s:(root/(s+'.stdout')).read_text() for s in 'AB'}
 run=re.search(r'ROS_MAIN .*?run=([0-9a-f]{16})',texts['A']).group(1)
 def valid(t):return analyze(t,run,order,'HOST_OPAQUE_PUBLIC_API_SHAPE')['pass']
 assert valid(texts)
 for label,mutate in [
  ('reused_id',lambda t:t.replace('new_id=4','new_id=2')),
  ('missing_participant_source',lambda t:t.replace('identity_source=SDK_SEND_SPDP_PREFIX','identity_source=UNPROVEN')),
  ('unrelated_participant',lambda t:re.sub(r'(NATIVE_ENTITY_ANCHOR[^\n]*guid=)[0-9a-f]{24}',r'\g<1>'+('f'*24),t)),
  ('changed_main_pid',lambda t:re.sub(r'(ROS_MAIN[^\n]*pid=)[0-9]+',r'\g<1>999',t)),
  ('missing_reader',lambda t:re.sub(r'^NATIVE_ENDPOINT_ANCHOR[^\n]*role=reader[^\n]*\n','',t,flags=re.M)),
  ('old_history',lambda t:t.replace('segment=POST','segment=PRE')),
  ('premature_post',lambda t:next(x for x in t.splitlines() if x.startswith('ROS_WRITE ') and 'segment=POST ' in x)+'\n'+t),
  ('fake_native_source',lambda t:t.replace('source=HOST_OPAQUE_PUBLIC_API_SHAPE','source=FAKE')),
 ]:
  altered=dict(texts,A=mutate(texts['A']));assert altered!=texts,label;assert not valid(altered),label
 print('PASS 9 supplied Host ledger cases:',order,'NO_DEVICE_EVIDENCE')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--fixture',type=Path,required=True);p.add_argument('--order',required=True,choices=['outgoing-first','accepted-first']);a=p.parse_args();check(a.fixture,a.order)
