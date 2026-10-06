"""Offline application/SDK observation ledger; never infers SDK drain or DDS."""
from pathlib import Path
import json,re,hashlib,argparse
def rows(text,prefix):
 return [dict(re.findall(r'(\w+)=([^\s]*)',line)) for line in text.splitlines() if line.startswith(prefix+' ')]
def side_report(text,cfg,side):
 errors=[]
 def need(ok,name):
  if not ok:errors.append(name)
 main=rows(text,'PROBE_MAIN');build=rows(text,'PROBE_BUILD');symbols=rows(text,'PROBE_SYMBOL')
 need(len(main)==1 and main[0].get('run')==cfg['run'] and main[0].get('side')==side and main[0].get('mode')==cfg['scenario'],'MAIN_IDENTITY')
 need(len(build)==1 and build[0].get('source_receipt')==cfg['source_state_sha256'],'COMPILED_SOURCE_RECEIPT')
 need(len(build)==1 and build[0].get('socket_header')==cfg['header_sha256']['socket.h'] and build[0].get('trans_type_header')==cfg['header_sha256']['trans_type.h'],'COMPILED_HEADER_RECEIPT')
 need(len(symbols)==5 and {e.get('name') for e in symbols}=={'Socket','Listen','Bind','SendBytes','Shutdown'},'SYMBOL_ORIGINS')
 need(all(e.get('origin') in ['/system/lib64/platformsdk/libsoftbus_client.z.so','libsoftbus_client.z.so'] for e in symbols),'UNDECLARED_SYMBOL_ORIGIN')
 api=rows(text,'PROBE_API');record={'main':main,'build':build,'symbols':symbols,'api':api}
 if cfg['scenario']=='load-check':
  need(not api and 'mode=load-check explicit_socket_calls=0' in text,'LOAD_ONLY_EXPLICIT_CALLS')
  denied=rows(text,'PROBE_DENIED_READBACK');need(len(denied)==1 and denied[0].get('denied')=='1' and denied[0].get('state')=='-1','DENIED_READBACK')
 else:
  local=0 if side=='A' else 1;send=rows(text,'PROBE_SEND');recv=rows(text,'PROBE_RECEIVER');result=rows(text,'PROBE_RESULT');exits=rows(text,'PROBE_EXIT');trace=rows(text,'PROBE_TRACE');owners=rows(text,'PROBE_OWNERS')
  need(len(send)==5 and sorted(e.get('sequence') for e in send)==['1','2','3','4','5'] and all(e.get('side')==str(local) and e.get('returned')=='1' and e.get('code')=='0' and e.get('outcome')=='1' for e in send),'ORIGINAL_SEND_RESULTS')
  need(len(recv)==5 and sorted(e.get('sequence') for e in recv)==['1','2','3','4','5'] and all(e.get('valid')=='1' and e.get('direction')==str(1-local) and e.get('length')=='256' for e in recv),'UNIQUE_RECEIVER_RECORDS')
  need(len(result)==1 and all(result[0].get(k)==v for k,v in {'side':str(local),'success':'1','sent':'5','received':'5','invalid':'0','drained':'1','first_failure':''}.items()),'LOCAL_RESULT')
  need(len(exits)==1 and exits[0].get('pass')=='1' and exits[0].get('restored')=='1' and exits[0].get('trace_overflow')=='0','EXIT_AND_RESTORATION')
  need(len(trace)==1 and trace[0].get('overflow')=='0' and int(trace[0].get('count','-1'))==len(api),'COMPLETE_BOUNDED_TRACE')
  need([e.get('event') for e in api]==[str(i+1) for i in range(len(api))],'TRACE_SEQUENCE')
  ready=[e for e in owners if e.get('stage')=='READY'];need(len(ready)==1,'READY_OWNERS')
  if len(ready)==1:
   o=ready[0];owned={o['outgoing'],o['listener'],o['accepted']};need(len(owned)==3 and '-1' not in owned,'DISTINCT_OWNERS')
   sent=[e for e in api if e['name']=='SendBytes_return'];incoming=[e for e in api if e['name']=='OnBytes']
   need(len(sent)==5 and all(e['socket']==o['outgoing'] and e['value']=='0' and e['length']=='256' for e in sent),'ACTUAL_PUBLIC_SEND_CALLS')
   need(len([e for e in incoming if e['socket']==o['accepted'] and e['length']=='256'])==5,'ACTUAL_ACCEPTED_BYTES_CALLBACKS')
   negotiated=[e for e in api if e['name']=='OnNegotiate_result' and e['socket']==o['accepted'] and e['value']=='1'];bound=[e for e in api if e['name']=='OnBind' and e['socket']==o['accepted']]
   need(bool(negotiated) and bool(bound) and int(negotiated[0]['event'])<int(bound[0]['event']),'ORDINARY_NEGOTIATION_BEFORE_BIND')
   begins=[e['socket'] for e in api if e['name']=='Shutdown_enter'];ends=[e['socket'] for e in api if e['name']=='Shutdown_void_return']
   need(set(begins).issubset(owned) and len(begins)==len(set(begins)) and sorted(begins)==sorted(ends),'OWNED_VOID_CLOSE_CALLS')
   observed={e['socket'] for e in api if e['name']=='OnShutdown'}
   need(owned.issubset(set(ends)|observed),'ALL_OWNERS_TERMINAL')
  need(not any(e['name'] in ['OnNegotiate2_unsupported','OnError'] for e in api),'UNSUPPORTED_OR_ERROR_CALLBACK')
  record.update(sent_records=send,received_records=recv,owners=owners,result=result,trace=trace)
 record.update(errors=errors,pass_=not errors,scope='SYNTHETIC_BYTES_NOT_DDS; SDK_DRAIN_UNPROVEN; NO_PHYSICAL_OLD_FRAME_PROOF');return record
def analyze(window):
 cfg=json.loads((window/'window.json').read_text());result={'run':cfg['run'],'scenario':cfg['scenario'],'boards':{},'errors':[]}
 for side in ['A','B']:
  p=window/(side+'.raw.stdout.log');text=p.read_text(errors='strict');r=side_report(text,cfg,side);r['raw_sha256']=hashlib.sha256(p.read_bytes()).hexdigest()
  load=json.loads((window/side/'actual-load-origin.json').read_text());guard=json.loads((window/side/'final-guard.json').read_text(encoding='utf-8-sig'))
  if not load.get('complete'):r['errors'].append('LOAD_ORIGIN_INCOMPLETE')
  if not all(guard.get(k) is True for k in ['Passed','byte_match','metadata_match','process_absent']) or guard.get('service_permission')!='DENIED':r['errors'].append('GUARD_INCOMPLETE')
  r['pass_']=not r['errors'];result['boards'][side]=r
 run=json.loads((window/'result.json').read_text());summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
 if not run.get('pass') or run.get('timedOut') or summary.get('first_failure') or summary.get('cleanup_failures'):result['errors'].append('EXECUTION_OR_RESTORATION_FAILURE')
 result['pass']=not result['errors'] and all(r['pass_'] for r in result['boards'].values())
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('window',type=Path);a=p.parse_args();out=analyze(a.window)
 with (a.window/'PROBE_ANALYSIS.json').open('x',encoding='utf-8') as f:json.dump(out,f,indent=2)
 print(json.dumps({'pass':out['pass'],'errors':out['errors'],'boards':{s:r['errors'] for s,r in out['boards'].items()}}));raise SystemExit(0 if out['pass'] else 1)
