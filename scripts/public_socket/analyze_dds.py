"""Offline logical DDS/native correlation; never a unique physical packet proof."""
import argparse,json,re,hashlib
from pathlib import Path
def fields(line):return dict(re.findall(r'(\w+)=([^ ]*)',line))
def rows(text,prefix):return [dict(fields(line),line=i+1) for i,line in enumerate(text.splitlines()) if line.startswith(prefix+' ')]
def number(d,key,default=0):
 try:return int(d.get(key,default))
 except (ValueError,TypeError):return -1
def validate_pair(pair):
 errors=[]
 def need(value,label):
  if not value:errors.append(label)
 for s,d in [('A','B'),('B','A')]:
  b=pair[s];other=pair[d];prefix=s+':'
  need(b.get('factory') and b.get('config'),prefix+'FACTORY_OR_CONFIG')
  need(b.get('remote_writer')==other.get('writer') and b.get('remote_reader')==other.get('reader') and b.get('writer')!=other.get('writer'),prefix+'EXACT_REMOTE_MATCH')
  writes=b.get('writes',[]);takes=b.get('takes',[]);frames=b.get('frames',[])
  need(len(writes)==5 and {e['ordinal'] for e in writes}==set(range(1,6)) and len({e['sn'] for e in writes})==5 and all(e['sn']>0 and e['rc']==0 and e['writer']==b.get('writer') for e in writes),prefix+'WRITES')
  need(len(takes)==5 and {e['ordinal'] for e in takes}==set(range(1,6)) and len({e['sn'] for e in takes})==5 and all(e['valid']==1 and e['run']==b['run'] and e['reader']==b['reader'] and e['writer']==other['writer'] for e in takes),prefix+'READER_LEDGER')
  for e in takes:
   remote=[x for x in other.get('writes',[]) if x['ordinal']==e['ordinal'] and x['sn']==e['sn'] and x['writer']==e['writer']];need(len(remote)==1,prefix+'SN_SOURCE')
   sent=[x for x in other.get('frames',[]) if x['stage']=='SDK_SEND' and x['kind']=='DATA' and x['writer']==e['writer'] and x['sn']==e['sn'] and x.get('owner',0)>0 and x.get('admitted',0)>0 and x.get('entered',0)>0]
   need(bool(sent),prefix+'ORIGINAL_TICKET_SEND')
   hashes={x['hash'] for x in sent}
   for stage in ['SDK_INGRESS','RX_OWNED','RX_RUNNING']:
    matched=[x for x in frames if x['stage']==stage and x['kind']=='DATA' and x['writer']==e['writer'] and x['sn']==e['sn'] and x['hash'] in hashes]
    if stage!='SDK_INGRESS':matched=[x for x in matched if x.get('owner',0)>0 and x.get('app',0)>0 and x.get('receiver',0)>0]
    need(bool(matched),prefix+stage+'_CHAIN')
  for kind in ['SPDP','SEDP']:
   need(any(x['stage']=='SDK_SEND' and x['kind']==kind for x in frames),prefix+kind+'_SEND')
   need(any(x['stage']=='RX_RUNNING' and x['kind']==kind for x in frames),prefix+kind+'_RECEIVE')
  need(any(x['stage']=='SDK_SEND' and x['kind'] in ['HEARTBEAT','ACKNACK'] for x in frames),prefix+'RELIABILITY_CONTROL')
  need(b.get('wait')==0,prefix+'DDS_WAIT_OPERATION');need(b.get('deleted'),prefix+'PUBLIC_DELETE_SETTLE');need(b.get('overflow')==0,prefix+'TRACE_INCOMPLETE')
 return sorted(set(errors))
def parse_board(text,cfg,side):
 errors=[]
 def need(v,k):
  if not v:errors.append(k)
 def one(prefix):
  r=rows(text,prefix);need(len(r)==1,prefix+'_COUNT');return r[0] if len(r)==1 else {}
 main=one('DDS_MAIN');build=one('DDS_BUILD');factory=one('DDS_FACTORY');created=one('DDS_CREATE');identity=one('DDS_IDENTITIES');match=one('DDS_MATCH');q=one('DDS_CONFIG');ready=one('DDS_NATIVE_READY');delete=one('DDS_DELETE_RETURN');settle=one('DDS_LOCAL_SETTLE');result=one('DDS_RESULT');exit_=one('DDS_EXIT');wait=one('DDS_WAIT');trace=one('DDS_FRAME_SUMMARY');api_trace=one('PROBE_TRACE')
 need(main.get('run')==cfg['run'] and main.get('side')==side and main.get('mode')=='dds' and number(main,'pid')>0,'MAIN_IDENTITY')
 need(build.get('source_receipt')==cfg['source_state_sha256'] and build.get('socket_header')==cfg['header_sha256']['socket'] and build.get('trans_type_header')==cfg['header_sha256']['trans_type'],'SOURCE_HEADERS')
 symbols=rows(text,'DDS_SYMBOL');need({e.get('name') for e in symbols}=={'Socket','Listen','Bind','SendBytes','Shutdown'} and len(symbols)==5 and all(e.get('origin') in ['/system/lib64/platformsdk/libsoftbus_client.z.so','libsoftbus_client.z.so'] for e in symbols),'SYMBOL_ORIGIN')
 need(number(result,'success')==1 and number(result,'writes')==number(result,'takes')==5 and number(result,'drained')==1 and result.get('first_failure')=='','BUSINESS_RESULT')
 need(number(exit_,'pass')==number(exit_,'restored')==1 and exit_.get('run')==cfg['run'],'NORMAL_EXIT')
 post=one('DDS_POST_ACK');need(post.get('observation_ms')=='100' and post.get('valid_extra')=='0' and not rows(text,'DDS_EXTRA'),'POST_ACK_EXTRA_CHECK')
 need(not rows(text,'DDS_EXCEPTION_FINALIZER') and not rows(text,'DDS_RX_ERROR') and not rows(text,'PROBE_FIRST_FAILURE'),'UNEXPECTED_FAILURE')
 for e in rows(text,'DDS_TX_RESULT'):need(number(e,'terminal_class')==1 and number(e,'outcome')==0 and number(e,'status') in [20, 29, 43] and (number(e,'sealed')==1 or number(e,'outgoing_stopped')==1),'UNCLASSIFIED_TX_FAILURE')
 need(len({number(ready,k,-1) for k in ['listener','outgoing','accepted']})==3 and all(number(ready,k,-1)>=0 for k in ['listener','outgoing','accepted']) and all(number(ready,k)>0 for k in ['record','owner','app','receiver']),'NATIVE_OWNER_READY')
 api=rows(text,'PROBE_API');frame_rows=rows(text,'DDS_FRAME')
 need(number(trace,'limit',-1)==4096 and number(trace,'count',-1)==len(frame_rows) and len(frame_rows)<=4096 and [number(e,'event',-1) for e in frame_rows]==list(range(1,len(frame_rows)+1)) and number(trace,'overflow',-1)==0,'FRAME_TRACE_COMPLETENESS')
 need(number(api_trace,'capacity',-1)==1024 and number(api_trace,'count',-1)==len(api) and len(api)<=1024 and [number(e,'event',-1) for e in api]==list(range(1,len(api)+1)) and number(api_trace,'overflow',-1)==0,'API_TRACE_COMPLETENESS')
 accepted=str(ready.get('accepted'));owned={str(ready.get(k)) for k in ['listener','outgoing','accepted']}
 neg=[e for e in api if e.get('name')=='OnNegotiate_result' and e.get('socket')==accepted and e.get('value')=='1'];bind=[e for e in api if e.get('name')=='OnBind' and e.get('socket')==accepted]
 need(bool(neg and bind) and number(neg[0],'event')<number(bind[0],'event'),'ACTUAL_NEGOTIATION_BIND')
 starts=[e['socket'] for e in api if e.get('name')=='Shutdown_enter'];ends=[e['socket'] for e in api if e.get('name')=='Shutdown_void_return'];terminal={e['socket'] for e in api if e.get('name')=='OnShutdown'}
 need(len(starts)==len(set(starts)) and sorted(starts)==sorted(ends) and set(starts)<=owned and owned<=set(ends)|terminal and terminal<=owned,'OWNER_CLOSE_RESPONSIBILITY')
 need(not any(e.get('name') in ['OnError','OnNegotiate2_unsupported'] for e in api),'SDK_ERROR_OR_UNSUPPORTED_ORDER')
 need(all(e.get('value')=='0' for e in api if e.get('name') in ['Listen_return','Bind_return','SendBytes_return']),'SDK_RETURN_ERROR')
 writes=[{'ordinal':number(e,'ordinal'),'sn':number(e,'sn'),'writer':e.get('writer'),'rc':number(e,'rc'),'serialized':number(e,'serialized'),'line':e['line']} for e in rows(text,'DDS_WRITE')]
 takes=[{'ordinal':number(e,'ordinal'),'sn':number(e,'sn'),'writer':e.get('writer'),'reader':e.get('reader'),'run':e.get('run'),'valid':number(e,'valid'),'line':e['line']} for e in rows(text,'DDS_TAKE')]
 fs=[]
 for e in rows(text,'DDS_FRAME'):
  fs.append({k:(number(e,k) if k in ['sn','hash','owner','app','receiver','admitted','entered','packet','correlation','length','socket','event','line'] else v) for k,v in e.items()})
 need(all(0<e.get('length',0)<=8192 for e in fs),'FRAME_LENGTH')
 need(all(e.get('socket')==number(ready,'outgoing') for e in fs if e.get('stage')=='SDK_SEND') and all(e.get('socket')==number(ready,'accepted') for e in fs if e.get('stage') in ['SDK_INGRESS','RX_OWNED','RX_RUNNING']),'FRAME_OWNER_LANES')
 need(all(e['line']>match.get('line',10**9) for e in writes),'WRITE_BEFORE_MATCH')
 stages=rows(text,'DDS_STAGE');need([e.get('stage') for e in stages]==['LISTENER_READY','NATIVE_READY','MATCH','DATA_DONE','DELETE','DDS_DELETED'],'STAGE_ORDER')
 expected={'domain':'151','builtin':'0','intraprocess':'0','data_sharing':'0','ignore_local':'1','reliability':'RELIABLE','durability':'VOLATILE','history':'16','max_samples':'32','max_instances':'1','max_per_instance':'32','max_record':'8192','payload':'32','meta_user_same':'1'}
 return {'run':cfg['run'],'side':side,'pid':number(main,'pid'),'factory':factory.get('implementation')=='DSoftBusTransport' and factory.get('variant')=='isolated_experiment' and factory.get('mode')=='2' and created.get('non_null')=='1','config':all(q.get(k)==v for k,v in expected.items()),'writer':identity.get('writer'),'reader':identity.get('reader'),'participant':identity.get('participant'),'remote_writer':match.get('remote_writer'),'remote_reader':match.get('remote_reader'),'writes':writes,'takes':takes,'frames':fs,'wait':number(wait,'rc',-1),'deleted':number(delete,'contained_rc',-1)==number(delete,'participant_rc',-1)==0 and settle.get('settled')=='1' and settle.get('fallback')=='0' and number(settle,'input_close')>0 and number(settle,'shutdown')>0,'overflow':int(not (number(trace,'overflow',-1)==0 and number(api_trace,'overflow',-1)==0 and number(exit_,'trace_overflow',-1)==0 and number(exit_,'frame_overflow',-1)==0)),'errors':errors,'native_ready':ready,'close_events':{'explicit':ends,'typed':list(terminal)},'decoded_text_sha256':hashlib.sha256(text.encode()).hexdigest()}
def analyze(window):
 cfg=json.loads((window/'window.json').read_text());pair={s:parse_board((window/(s+'.raw.stdout.log')).read_text(),cfg,s) for s in ['A','B']};errors=validate_pair(pair)
 for s in ['A','B']:
  errors += [s+':'+e for e in pair[s]['errors']]
  pair[s]['raw_sha256']=hashlib.sha256((window/(s+'.raw.stdout.log')).read_bytes()).hexdigest()
  guard=json.loads((window/s/'final-guard.json').read_text(encoding='utf-8-sig'));load=json.loads((window/s/'actual-load-origin.json').read_text())
  if not all(guard.get(k) is True for k in ['Passed','byte_match','metadata_match','process_absent']) or guard.get('service_permission')!='DENIED':errors.append(s+':GUARD')
  if not load.get('complete') or load.get('run')!=cfg['run'] or load.get('board')!=s:errors.append(s+':LOAD')
 run=json.loads((window/'result.json').read_text());summary=json.loads((window/'window-summary.json').read_text(encoding='utf-8-sig'))
 if not run.get('pass') or run.get('timedOut') or summary.get('first_failure') or summary.get('cleanup_failures'):errors.append('EXECUTION_OR_CLEANUP')
 controller=run.get('controller',{});expected=cfg['delete_first'];deletes=controller.get('deletion_order',[])
 if deletes!=[expected,'B' if expected=='A' else 'A']:errors.append('DELETE_ORDER')
 return {'run':cfg['run'],'scenario':cfg['scenario'],'pass':not errors,'errors':errors,'boards':pair,'delete_order':deletes,'scope':'Real DDS over new shim at tested profile; logical frame association, not unique physical packet or SDK drain proof'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('window',type=Path);a=p.parse_args();result=analyze(a.window)
 with (a.window/'DDS_ANALYSIS.json').open('x') as f:json.dump(result,f,indent=2)
 print(json.dumps({'pass':result['pass'],'errors':result['errors']}));raise SystemExit(0 if result['pass'] else 1)
