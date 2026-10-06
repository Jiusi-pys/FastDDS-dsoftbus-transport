"""Bounded new-shim evidence only. No device access or inferred physical causality."""
import argparse
import json
import re
from collections import Counter
from pathlib import Path

CASES = ('F256','F1M','REL_LOSS','BE_LOSS','VOL_LATE','TL_LATE','KEYED_LAST')

def need(condition, reason):
    if not condition:
        raise ValueError(reason)

def number(row, key):
    return int(row[key])

def expected(case):
    need(case in CASES, 'unknown case')
    if case == 'KEYED_LAST': return {(k,n) for k in (1,2) for n in (3,4,5)}
    if case == 'VOL_LATE': return {(1,n) for n in (4,5,6)}
    if case == 'TL_LATE': return {(1,n) for n in range(1,7)}
    return {(1,n) for n in ((2,3) if case == 'BE_LOSS' else (1,2,3))}

def coverage(rows, size):
    need(rows and 0<size<=1048588, 'missing DATA_FRAG or sample bound')
    sizes={number(r,'fragment_size') for r in rows}
    need(len(sizes)==1 and 0<min(sizes)<=8192, 'fragment size changes/zero/bound')
    f=sizes.pop(); total=(size+f-1)//f; seen=set()
    need(total<=16384,'fragment evidence exceeds bounded capacity')
    for r in rows:
        first,count=number(r,'first'),number(r,'count')
        need(number(r,'sample_size')==size and first>0 and count>0 and first+count-1<=total,'invalid fragment geometry')
        seen.update(range(first,first+count))
    need(seen==set(range(1,total+1)), 'fragment range hole')
    return seen

def loss_proof(drops, requests, repairs, writer, sn):
    need(drops and len({number(r,'packet') for r in drops})==1,'drop not exactly one native record')
    fragments=[r for r in drops if r.get('kind','DATA_FRAG')=='DATA_FRAG']
    need(len(fragments)==1,'drop fragment shape')
    d=fragments[0];need(d['writer']==writer and number(d,'sn')==sn,'wrong drop identity')
    need(0<number(d,'first')<=16384 and 0<number(d,'count')<=16384 and number(d,'first')+number(d,'count')<=16385,'drop geometry bound')
    missing=set(range(number(d,'first'),number(d,'first')+number(d,'count')))
    need(3 in missing,'target fragment not hit')
    requested=set();request_packets=[]
    for q in requests:
        if q['writer']!=writer or number(q,'sn')!=sn or number(q,'writer_known')!=1 or number(q,'packet')<=number(d,'packet'):continue
        words=[int(x,16) for x in q['bitmap'].split(',')]
        bits=number(q,'bits');need(0<=bits<=256 and len(words)==8,'NACK bitmap bounds')
        part={number(q,'first')+i for i in range(bits) if words[i//32]&(1<<(31-i%32))}
        requested.update(part)
        if missing<=part:request_packets.append(number(q,'packet'))
    need(missing<=requested,'no matching missing-range request')
    need(request_packets,'no single observed request covers dropped range')
    request_packet=min(request_packets)
    repaired=set()
    for q in repairs:
        if q['writer']==writer and number(q,'sn')==sn and number(q,'packet')>request_packet:
            repaired.update(range(number(q,'first'),number(q,'first')+number(q,'count')))
    need(missing<=repaired,'no later same-range repair at B ingress')
    return missing

def absence(rows):
    need(len(rows)==1 and number(rows[0],'completed')==1 and number(rows[0],'window_ms')==3000 and number(rows[0],'ordinal')==1 and number(rows[0],'started_after_drop')==1 and number(rows[0],'after_drop_us')>=3000000,'BE observation incomplete')

def parse(text):
    rows=[]
    for line_no,line in enumerate(text.splitlines(),1):
        if not line.startswith(('REG_','DDS_','PROBE_')):continue
        row=dict(re.findall(r'(\w+)=([^\s]+)',line));row['_event']=line.split()[0];row['_line']=line_no;rows.append(row)
    return rows

def select(rows,event,**values):
    return [r for r in rows if r['_event']==event and all(r.get(k)==str(v) for k,v in values.items())]

def ledger(writes,takes,want,writer,reader,run):
    key=lambda r:(number(r,'key'),number(r,'ordinal'))
    wm={key(r):r for r in writes}
    need(len(wm)==len(writes),'application resend')
    need(len({number(r,'sn') for r in writes})==len(writes),'write SN alias')
    for r in writes:need(r['writer']==writer and r['run']==run and number(r,'rc')==0 and number(r,'sn')>0,'invalid write identity/return')
    need(len(takes)==len(want) and {key(r) for r in takes}==want,'per-reader expected set mismatch')
    need(len({number(r,'sn') for r in takes})==len(takes),'reader duplicate SN')
    for r in takes:
        w=wm.get(key(r));need(w is not None,'no original write')
        need(r['run']==run and r['writer']==writer and r['reader']==reader and number(r,'valid')==1 and r['sn']==w['sn'] and r['hash']==w['hash'],'Reader identity/payload/SN mismatch')

def native_chain(source, ingress, owned, running):
    need(source and ingress and owned and running,'missing formal native/receiver chain')
    need(all('ingress' in f for f in ingress+owned),'missing callback occurrence metadata')
    key=lambda f:tuple(str(f.get(k,0)) for k in ('writer','sn','kind','hash','length','offset','first','count','fragment_size','sample_size'))
    good_source={key(f) for f in source if number(f,'owner')>0 and number(f,'admitted')>0 and number(f,'entered')>0}
    seen={(key(f),str(f['ingress'])) for f in ingress if key(f) in good_source and number(f,'ingress')>0}
    lease=lambda f:(key(f),str(f['correlation']),str(f['app']),str(f['receiver']),str(f['owner']))
    captured={lease(f) for f in owned if (key(f),str(f['ingress'])) in seen and all(number(f,k)>0 for k in ('correlation','app','receiver','owner'))}
    matched=[f for f in running if lease(f) in captured]
    need(matched,'no matching original owned-copy lease/delivery')
    return matched

def full_fragment_chain(source,ingress,captured,owned,running,size):
    good=native_chain(source,ingress,owned,running)
    good=native_chain(source,ingress,captured,good)
    return coverage(good,size)

def dropped_occurrence(drops,frames):
    need(drops and len({r['packet'] for r in drops})==1,'one dropped native buffer required')
    occurrence=drops[0].get('ingress','0');need(int(occurrence)>0 and all(r.get('ingress')==occurrence for r in drops),'drop occurrence missing/split')
    original=[f for f in frames if f['stage']=='SDK_INGRESS' and f['ingress']==occurrence]
    need(original and all(any(f['hash']==d['hash'] and f['length']==d['length'] and f['socket']==d['socket'] for f in original) for d in drops),'drop not linked to original ingress')
    need(not [f for f in frames if f['stage'] in ('RX_CAPTURED','RX_OWNED') and f['ingress']==occurrence],'dropped callback entered Binding')

def api_evidence(rows):
    api=select(rows,'PROBE_API');summary=select(rows,'PROBE_TRACE')
    need(len(summary)==1 and summary[0]['overflow']=='0' and len(api)==number(summary[0],'count'),'API trace missing/incomplete')
    need([number(r,'event') for r in api]==list(range(1,len(api)+1)),'API sequence gap')
    names={r['name'] for r in api}
    need({'Socket_enter','Socket_return','Listen_enter','Listen_return','Bind_enter','Bind_return','SendBytes_enter','SendBytes_return','OnBytes','OnBind','OnNegotiate','Shutdown_enter','Shutdown_void_return'}<=names,'actual API/callback path not exercised')
    for r in api:
        if r['name'] in ('Listen_return','Bind_return','SendBytes_return'):need(number(r,'value')==0,'SDK call returned error')
        if r['name']=='Socket_return':need(number(r,'value')>=0,'Socket handle failure')
        need(r['name'] not in ('OnError','OnNegotiate2_unsupported'),'unexpected callback error/order')
    sends=[r for r in api if r['name']=='SendBytes_enter'];returns=[r for r in api if r['name']=='SendBytes_return']
    need(len(sends)==len(returns) and [(r['socket'],r['length']) for r in sends]==[(r['socket'],r['length']) for r in returns],'unreturned/different SDK send')
    for stage,events in [('SDK_SEND',sends),('SDK_INGRESS',[r for r in api if r['name']=='OnBytes'])]:
        observed={(r['packet'],r['socket'],r['length']) for r in select(rows,'DDS_FRAME',stage=stage)}
        required=Counter((socket,length) for _,socket,length in observed)
        actual=Counter((r['socket'],r['length']) for r in events)
        need(all(actual[k]>=n for k,n in required.items()),'native frames lack actual matching API/callback events')

def _analyze(case, texts):
    want=expected(case); a,b=map(parse,texts); allrows=a+b
    need(all('FAKE_' not in s and 'ExperimentalTransport' not in s for s in texts),'wrong/Fake device path')
    ids=[select(x,'DDS_IDENTITIES') for x in (a,b)]
    need(all(len(x)==1 for x in ids),'current endpoint identities missing/duplicate')
    ia,ib=ids[0][0],ids[1][0];writer,reader=ia['writer'],ib['reader']
    need(writer[:24]!=ib['writer'][:24],'local match masquerades as remote')
    for rows,own,peer in ((a,ia,ib),(b,ib,ia)):
        m=select(rows,'DDS_MATCH');need(len(m)==1 and m[0]['writer']==own['writer'] and m[0]['reader']==own['reader'] and m[0]['remote_writer']==peer['writer'] and m[0]['remote_reader']==peer['reader'],'wrong current match graph')
        cfg=select(rows,'REG_CONFIG');need(len(cfg)==1 and cfg[0]['case']==case and cfg[0]['max_record']=='8192' and cfg[0]['max_samples']=='64' and cfg[0]['max_instances']=='4' and cfg[0]['max_per_instance']=='16','effective config missing/drift')
        payload=1048576 if case=='F1M' else 262144 if case in ('F256','REL_LOSS','BE_LOSS') else 32
        need(number(cfg[0],'payload')==payload and number(cfg[0],'writer_depth')==(2 if case=='KEYED_LAST' else 16) and cfg[0]['reader_history']=='KEEP_ALL','effective size/history drift')
        need(cfg[0]['reliability']==('BEST_EFFORT' if case=='BE_LOSS' else 'RELIABLE') and cfg[0]['durability']==('TRANSIENT_LOCAL' if case in ('TL_LATE','KEYED_LAST') else 'VOLATILE'),'effective QoS mismatch')
        results=select(rows,'DDS_RESULT');need(len(results)==1 and results[0]['success']=='1' and results[0]['drained']=='1','process result failed/missing')
        need(len(select(rows,'DDS_FACTORY',implementation='DSoftBusTransport',variant='isolated_experiment',mode=2))==1,'wrong factory path')
        for e in ('DDS_FRAME_SUMMARY','PROBE_TRACE'):
            summaries=select(rows,e);need(len(summaries)==1 and summaries[0]['overflow']=='0','missing/overflow '+e)
        frames=select(rows,'DDS_FRAME');need(len(frames)==int(select(rows,'DDS_FRAME_SUMMARY')[0]['count']),'incomplete frame log')
        need([int(r['event']) for r in frames]==list(range(1,len(frames)+1)),'frame event gap')
        need(not select(rows,'DDS_TX_RESULT',terminal_class=0) and not select(rows,'DDS_RX_ERROR'),'unsettled execution error')
        api_evidence(rows)
    writes=select(a,'REG_WRITE');takes=select(b,'REG_TAKE');need(not select(b,'REG_WRITE') and not select(a,'REG_TAKE'),'wrong directional sample')
    need(all(number(w,'payload')==payload and number(w,'serialized')==(60 if payload==32 else payload+12) for w in writes),'write payload/serialized size drift')
    need(writes,'no writes');run=writes[0]['run'];ledger(writes,takes,want,writer,reader,run)
    expected_writes=10 if case=='KEYED_LAST' else 6 if case in ('VOL_LATE','TL_LATE') else 3
    need(len(writes)==expected_writes,'write count')
    if case=='BE_LOSS':
        absence(select(b,'REG_ABSENCE'))
        need(all(r.get('status')=='NotCalled' for r in select(allrows,'REG_WAIT')) and len(select(allrows,'REG_WAIT'))==2,'BE wait called/missing')
    else:need(len(select(a,'REG_WAIT',rc=0))==1,'DDS wait missing/fail (not packet ACK)')
    stages={side:select(rows,'DDS_STAGE') for side,rows in [('A',a),('B',b)]}
    if case in ('VOL_LATE','TL_LATE','KEYED_LAST'):
        created=select(b,'REG_READER_CREATED',after_prewrite=1);need(len(created)==1,'late Reader creation receipt')
        pre=select(a,'DDS_STAGE',stage='PREWRITE_DONE');need(len(pre)==1,'prewrite stage missing')
        pre_b=select(b,'DDS_STAGE',stage='PREWRITE_DONE');need(len(pre_b)==1 and pre_b[0]['_line']<created[0]['_line'],'late Reader phase order')
        n=8 if case=='KEYED_LAST' else 3
        need(all(r['_line']<pre[0]['_line'] for r in writes[:n]),'historical write after prewrite barrier')
        history_a=select(a,'DDS_STAGE',stage='HISTORY_DONE');need(len(history_a)==1 and all(r['_line']>history_a[0]['_line'] for r in writes[n:]),'new write before history barrier')
        if case!='VOL_LATE':
            history=select(b,'DDS_STAGE',stage='HISTORY_DONE');need(len(history)==1,'history completion missing')
            historical={(k,o) for k,o in want if o<5} if case=='KEYED_LAST' else {(1,o) for o in (1,2,3)}
            need({(number(r,'key'),number(r,'ordinal')) for r in takes if r['_line']<history[0]['_line']}==historical,'historical set not completed before new writes')
    if case=='KEYED_LAST':
        handles={k:{r['instance'] for r in takes if int(r['key'])==k} for k in (1,2)}
        need(all(len(x)==1 and next(iter(x))!='0'*32 for x in handles.values()) and handles[1]!=handles[2],'key instance handles alias')
    frames_a,frames_b=select(a,'DDS_FRAME'),select(b,'DDS_FRAME')
    ready=[select(x,'DDS_NATIVE_READY') for x in (a,b)]
    need(all(len(x)==1 for x in ready),'native owner setup not observed')
    for w in writes:
        if (number(w,'key'),number(w,'ordinal')) not in want:continue
        identity=lambda f:f.get('writer')==writer and f.get('sn')==w['sn'] and f.get('kind') in ('DATA','DATA_FRAG')
        src=[f for f in frames_a if identity(f) and f['stage']=='SDK_SEND' and f['socket']==ready[0][0]['outgoing']]
        stages={stage:[f for f in frames_b if identity(f) and f['stage']==stage and f['socket']==ready[1][0]['accepted']] for stage in ('SDK_INGRESS','RX_OWNED','RX_RUNNING')}
        for stage in ('RX_OWNED','RX_RUNNING'):
            need(all(f['app']==ready[1][0]['app'] and f['receiver']==ready[1][0]['receiver'] and f['owner']==ready[1][0]['owner'] for f in stages[stage]),'foreign application/receiver/owner lease')
        native_chain(src,stages['SDK_INGRESS'],stages['RX_OWNED'],stages['RX_RUNNING'])
        captured=[f for f in frames_b if identity(f) and f['stage']=='RX_CAPTURED' and f['socket']==ready[1][0]['accepted']]
        native_chain(src,stages['SDK_INGRESS'],captured,stages['RX_RUNNING'])
        if payload!=32:full_fragment_chain(src,stages['SDK_INGRESS'],captured,stages['RX_OWNED'],stages['RX_RUNNING'],number(w,'serialized'))
    fragment_proof=[]
    if case in ('F256','F1M','REL_LOSS','BE_LOSS'):
        for w in writes:
            sn=w['sn'];size=int(w['serialized']);source=[f for f in frames_a if f.get('writer')==writer and f.get('sn')==sn and f.get('kind')=='DATA_FRAG' and f.get('stage')=='SDK_SEND']
            covered=coverage(source,size)
            if (int(w['key']),int(w['ordinal'])) in want:
                for stage in ('SDK_INGRESS','RX_OWNED','RX_RUNNING'):
                    coverage([f for f in frames_b if f.get('writer')==writer and f.get('sn')==sn and f.get('kind')=='DATA_FRAG' and f.get('stage')==stage],size)
            fragment_proof.append({'sn':sn,'fragment_count':len(covered),'serialized':size,'fragment_size':source[0]['fragment_size']})
    drops=[f for f in frames_b if f.get('stage')=='EXPERIMENT_DROP']
    if case in ('REL_LOSS','BE_LOSS'):
        need(len(select(b,'DDS_DROP_ARM',writer=writer,sn=1,fragment=3))==1 and len({r['packet'] for r in drops})==1,'drop not exercised exactly once')
        data_drops=[r for r in drops if r['kind']=='DATA_FRAG']
        need(len(data_drops)==1 and data_drops[0]['writer']==writer and data_drops[0]['sn']==writes[0]['sn'],'wrong drop target')
        dropped_occurrence(drops,frames_b)
        missing=set(range(int(data_drops[0]['first']),int(data_drops[0]['first'])+int(data_drops[0]['count'])))
        if case=='BE_LOSS':need(not [f for f in frames_b if f['stage']=='RX_RUNNING' and f['kind']=='DATA_FRAG' and f['writer']==writer and f['sn']==writes[0]['sn'] and missing.intersection(range(int(f['first']),int(f['first'])+int(f['count'])))],'BE selected missing range repaired')
        if case=='REL_LOSS':
            requests=[f for f in frames_b if f.get('kind')=='NACK_FRAG' and f.get('stage')=='SDK_SEND' and f.get('reader')==reader and f.get('reader_known')=='1']
            repairs=[f for f in frames_b if f.get('kind')=='DATA_FRAG' and f.get('stage')=='SDK_INGRESS']
            missing=loss_proof(drops,requests,repairs,writer,int(writes[0]['sn']))
            post=[q for q in requests if q['writer']==writer and q['sn']==writes[0]['sn'] and int(q['packet'])>int(data_drops[0]['packet'])]
            native_requests=[f for f in frames_a if f['stage']=='RX_RUNNING' and f['kind']=='NACK_FRAG' and f['writer']==writer and f['reader']==reader and any(q['hash']==f['hash'] and q['length']==f['length'] for q in post)]
            need(native_requests,'NACK not observed through A receiver')
            first_request=min(int(f['packet']) for f in native_requests)
            source_repairs=[f for f in frames_a if f['stage']=='SDK_SEND' and f['kind']=='DATA_FRAG' and f['writer']==writer and f['sn']==writes[0]['sn'] and int(f['packet'])>first_request and missing.intersection(range(int(f['first']),int(f['first'])+int(f['count'])))]
            b_after=[f for f in repairs if f['writer']==writer and f['sn']==writes[0]['sn'] and int(f['packet'])>min(int(q['packet']) for q in post) and missing.intersection(range(int(f['first']),int(f['first'])+int(f['count'])))]
            own=[f for f in frames_b if f['stage']=='RX_OWNED'];running=[f for f in frames_b if f['stage']=='RX_RUNNING']
            good=native_chain(source_repairs,b_after,own,running)
            good=native_chain(source_repairs,b_after,[f for f in frames_b if f['stage']=='RX_CAPTURED'],good)
            observed=set().union(*(set(range(int(f['first']),int(f['first'])+int(f['count']))) for f in good))
            need(missing<=observed,'repair missing range lacks complete original lease chain')
    else:need(not drops,'unexpected controlled drop')
    return {'case':case,'run':run,'pass':True,'writer':writer,'reader':reader,'writes':writes,'takes':takes,'fragments':fragment_proof,'drop':drops,'scope':'reader and observed logical chains; not unique physical causality','admission_snapshots':select(allrows,'REG_ADMISSION'),'rejected_callbacks':[r for r in allrows if r.get('name')=='OnBytesRejected']}

def analyze(case,texts):
    try:return _analyze(case,texts)
    except (KeyError,TypeError,OverflowError) as e:raise ValueError('incomplete or malformed evidence: '+str(e)) from e

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--case',choices=CASES,required=True);p.add_argument('--a',type=Path,required=True);p.add_argument('--b',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    try:result=analyze(args.case,[args.a.read_text(errors='strict'),args.b.read_text(errors='strict')])
    except (ValueError,KeyError,TypeError) as e:result={'case':args.case,'pass':False,'first_failure':str(e)}
    args.output.write_text(json.dumps(result,indent=2));raise SystemExit(0 if result['pass'] else 1)
