"""Finite shared-channel DDS ledgers. No SDK epoch, loss, or drain inference."""
import argparse
import json
from pathlib import Path
import re

def need(ok,why):
    if not ok: raise ValueError(why)

def parse(text):
    result={}
    for line in text.splitlines():
        fields=line.split()
        if fields:result.setdefault(fields[0],[]).append(dict(x.split('=',1) for x in fields[1:] if '=' in x))
    return result


# Frozen expectation for the bounded shared windows, from the case matrix: three writes
# per group per phase, two sides, five (group, life, phase) groups. These are the
# EXPECTED values. The observed values are computed from the parsed independent Reader
# ledgers below and must equal them; the constant is never reported as an observation.
EXPECTED_READER_SAMPLES = 30
EXPECTED_WRITES = 30
EXPECTED_DDS_WAIT_OPERATIONS = 10

def expected():
    return {(g,l,p,n) for g,l,p,ns in [('P','1','INITIAL',range(1,4)),('Q','1','INITIAL',range(1,4)),('Q','1','GAP',range(4,7)),('P','2','REATTACH',range(1,4)),('Q','1','REATTACH',range(7,10))] for n in ns}

def analyze_documents(case,boards):
    need(case in ('SHARED_W1','SHARED_W2') and len(boards)==2,'case/topology')
    identities=[];ledgers=[];native=[];runs=set()
    for side,b in enumerate(boards):
        main=b.get('DDS_MAIN',[]);need(len(main)==1 and main[0]['side']=='AB'[side],'main identity');runs.add(main[0]['run'])
        ids={};rows=b.get('SHARED_ID',[]);need(len(rows)==3,'actual create count')
        for r in rows:
            k=r['group']+r['life'];need(k in ('P1','Q1','P2') and k not in ids and r['side']==str(side) and r['run']==main[0]['run'],'create identity')
            need(all(re.fullmatch('[0-9a-f]{32}',r[x]) for x in ['participant','writer','reader']),'GUID missing')
            need(all(int(r[x])>0 for x in ['app','receiver','token']),'application identity missing');ids[k]=r
        order=[r['group']+r['life'] for r in rows];need(order==(['P1','Q1','P2'] if case=='SHARED_W1' else ['Q1','P1','P2']),'creation order')
        need(ids['P1']['name']==ids['P2']['name'] and all(ids['P1'][k]!=ids['P2'][k] for k in ['writer','reader','app','receiver','token']),'new P lifetime')
        deletes=b.get('SHARED_DELETE',[]);need(len(deletes)==3,'actual delete count')
        need([r['group']+r['life'] for r in deletes]==['P1','P2','Q1'],'actual delete order')
        for r in deletes:
            need(r['participant']==ids[r['group']+r['life']]['participant'] and r['entities_rc']=='0' and r['participant_rc']=='0' and r['input_open']=='0','delete/receiver drain')
        config=b.get('SHARED_CONFIG',[]);need(len(config)==3,'effective configuration missing')
        for r in config:
            need(all(r.get(k)==v for k,v in dict(reliability='RELIABLE',durability='VOLATILE',history='16',samples='32',instances='1',per_instance='32',builtin='0',intraprocess='0',sharing='0',max_record='8192',local_records='32',detail='16384',payload='32',meta_user_same='1').items()),'effective QoS/config mismatch')
        ns=b.get('SHARED_NATIVE',[]);need([r['stage'] for r in ns]==['INITIAL','GAP','REATTACHED','FINAL'],'runtime observations')
        first=ns[0];need(all(first[k]==v for k,v in dict(socket_calls='2',listen_calls='1',bind_calls='1',shutdown_calls='0').items()),'one native topology')
        invariant=['runtime','provider','listener_entry','outgoing_entry','listener','outgoing','accepted','listener_record','accepted_record','listener_owner','outgoing_owner','accepted_owner','socket_calls','listen_calls','bind_calls','shutdown_calls']
        for r,apps in zip(ns[:3],[2,1,2]):
            need(all(r[k]==first[k] for k in invariant),'native rebuilt or changed during shared window');need(int(r['views'])==apps and int(r['active_apps'])==apps,'local live memberships')
        need(ns[-1]['views']=='0' and ns[-1]['active_apps']=='0' and ns[-1]['records']=='0','final local responsibilities')
        end=b.get('SHARED_RESULT',[]);settle=b.get('SHARED_SETTLE',[]);need(len(end)==1 and len(settle)==1,'final receipt')
        need(all(end[0][k]==v for k,v in dict(success='1',factories='3',writes='15',takes='15',drained='1').items()) and settle[0]['drained']=='1' and settle[0]['fallback']=='0','normal completion')
        tables=[]
        for tag in ['SHARED_WRITE','SHARED_TAKE']:
            table={};sequences=set()
            for r in b.get(tag,[]):
                k=(r['group'],r['life'],r['phase'],int(r['ordinal']));need(k in expected() and k not in table,'missing/duplicate/wrong phase sample')
                need(r['run']==main[0]['run'] and r['side']==str(side) and int(r['sn'])>0,'sample run/source')
                sequence=(r['writer'],r['sn']);need(sequence not in sequences,'repeated Writer/SN');sequences.add(sequence);table[k]=r
            need(set(table)==expected(),'per Reader set incomplete');tables.append(table)
        identities.append(ids);ledgers.append(tables);native.append(first)
        frames=b.get('DDS_FRAME',[]);summary=b.get('DDS_FRAME_SUMMARY',[]);need(len(summary)==1,'frame summary missing')
        need(int(summary[0]['overflow'])==0 and int(summary[0]['count'])==len(frames)<=int(summary[0]['limit'])==16384,'frame overflow/missing')
        need([int(r['event']) for r in frames]==list(range(1,len(frames)+1)),'frame detail identity')
        for deleted in deletes:
            old=ids[deleted['group']+deleted['life']];boundary=int(deleted['frames']);need(0<=boundary<=len(frames),'delete frame boundary')
            need(not any(r['stage']=='RX_RUNNING' and r['app']==old['app'] and r['receiver']==old['receiver'] and int(r['event'])>boundary for r in frames),'old receiver entered after DDS deletion')
        waits=b.get('SHARED_WAIT',[]);need(len(waits)==5 and all(r['rc']=='0' for r in waits),'DDS wait API receipt')
    need(len(runs)==1,'mixed runs')
    for side,b in enumerate(boards):
        matches=b.get('SHARED_MATCH',[]);need(len(matches)==4,'fresh match queries missing')
        need([r['group']+r['life'] for r in matches]==['P1','Q1','P2','Q1'],'match life/order')
        for r in matches:
            key=r['group']+r['life'];own=identities[side][key];peer=identities[1-side][key]
            need(r['writer']==own['writer'] and r['reader']==own['reader'] and r['remote_writer']==peer['writer'] and r['remote_reader']==peer['reader'],'current exact opposite match')
        for key,take in ledgers[side][1].items():
            own=identities[side][key[0]+key[1]];peer=identities[1-side][key[0]+key[1]];write=ledgers[1-side][0][key]
            need(take['reader']==own['reader'] and take['writer']==peer['writer']==write['writer'] and take['sn']==write['sn'] and take['valid']=='1' and take['app']==own['app'] and take['receiver']==own['receiver'],'actual Reader identity/payload/SN')
            need(write['rc']=='0' and write['serialized']=='60','write success/size')
            select=lambda r:r['kind']=='DATA' and r['writer']==take['writer'] and r['sn']==take['sn']
            incoming=[r for r in b['DDS_FRAME'] if select(r)]
            owned=[r for r in incoming if r['stage']=='RX_OWNED' and r['app']==own['app'] and r['receiver']==own['receiver'] and r['owner']==native[side]['accepted_owner']]
            def linked(o):
                same=lambda r:r['hash']==o['hash'] and r['length']==o['length']
                return any(r['stage']=='SDK_INGRESS' and r['packet']==o['ingress'] and r['socket']==native[side]['accepted'] and same(r) for r in incoming) and any(r['stage']=='RX_RUNNING' and r['app']==o['app'] and r['receiver']==o['receiver'] and r['correlation']==o['correlation'] and same(r) for r in incoming) and any(select(r) and r['stage']=='SDK_SEND' and r['socket']==native[1-side]['outgoing'] and r['owner']==native[1-side]['outgoing_owner'] and int(r['admitted'])>0 and int(r['entered'])>0 and same(r) for r in boards[1-side]['DDS_FRAME'])
            need(any(linked(o) for o in owned),'missing ticket/SDK/accepted/original receiver path')
        for kind in ['SPDP','SEDP']:
            need(any(r['stage']=='SDK_SEND' and r['kind']==kind for r in b['DDS_FRAME']) and any(r['stage']=='RX_RUNNING' and r['kind']==kind for r in b['DDS_FRAME']),'discovery path missing')
    # Observed values come from the parsed per-Reader ledgers, never from the frozen
    # expectation; the expectation is only the bound they must match.
    observed_samples=sum(len(tables[1]) for tables in ledgers)
    observed_writes=sum(len(tables[0]) for tables in ledgers)
    observed_waits=sum(len(b.get('SHARED_WAIT',[])) for b in boards)
    need(observed_samples==EXPECTED_READER_SAMPLES,'observed Reader samples differ from the frozen expectation')
    need(observed_writes==EXPECTED_WRITES,'observed writes differ from the frozen expectation')
    need(observed_waits==EXPECTED_DDS_WAIT_OPERATIONS,'observed DDS wait operations differ from the frozen expectation')
    return dict(pass_=True,case=case,run=next(iter(runs)),reader_samples=observed_samples,
                writes=observed_writes,dds_wait_api_operations=observed_waits,
                expected_reader_samples=EXPECTED_READER_SAMPLES,expected_writes=EXPECTED_WRITES,
                expected_dds_wait_api_operations=EXPECTED_DDS_WAIT_OPERATIONS,
                identities=identities,ledger=ledgers,native=native,
                continuity='OBSERVED_OBJECT_AND_PUBLIC_CALL_SCOPE_ONLY',sdk_epoch='UNPROVEN',
                sdk_drain='UNPROVEN',unique_physical_packet_causality='NOT_CLAIMED')

def analyze_cycles(text, expected_cycles=None, device_case=None):
    device=device_case is not None
    source_texts=text if device else [text]
    for line in '\n'.join(source_texts).splitlines():
        if line.startswith(('SHARED_','HOST_MODEL ','TRACE_SUMMARY ','DDS_','PROBE_API ','PROBE_TRACE ','RX_DECISION')):
            keys=[v.split('=',1)[0] for v in line.split()[1:] if '=' in v]
            need(len(keys)==len(set(keys)),'duplicate receipt field')
    b={}
    for side,source in enumerate(source_texts):
        document=parse(source)
        if device:
            main=document.get('DDS_MAIN',[]);need(len(main)==1 and main[0]['side']=='AB'[side],'actual target main identity')
            declared=document.get('SHARED_SAMPLING',[]);need(len(declared)==1 and declared[0]['run']==main[0]['run'],'target main/run')
            count=int(declared[0]['cycles']);stages=document.get('DDS_STAGE',[])
            phase_names=['SHARED_INITIAL_READY','SHARED_INITIAL_MATCH','SHARED_INITIAL_DONE','P_OLD_DELETED','Q_GAP_DONE','P_NEW_READY','P_NEW_MATCH','SHARED_DATA_DONE']
            stage_key=lambda c,p:f'C{c}_{p}' if count>1 else p
            expected_stages=['LISTENER_READY','NATIVE_READY']+[stage_key(c,p) for c in range(1,count+1) for p in phase_names]+[stage_key(count,p) for p in ('DELETE','DDS_DELETED')]
            need([r['stage'] for r in stages]==expected_stages,'target controller cycle stages')
            need(all(r['run']==main[0]['run'] and r['side']=='AB'[side] and r['pid']==main[0]['pid'] for r in stages),'target process/run/side continuity')
            exit_rows=document.get('DDS_EXIT',[]);need(len(exit_rows)==1 and exit_rows[0]['run']==main[0]['run'] and all(exit_rows[0][k]=='0' for k in ('trace_overflow','frame_overflow')) and exit_rows[0]['pass']==exit_rows[0]['restored']=='1','normal target exit')
            for tag,rows in document.items():
                for row in rows:
                    if tag.startswith('SHARED_'):need(row.get('side')==str(side),'target receipt file/side')
                    if tag in ('DDS_FRAME','DDS_FRAME_SUMMARY','PROBE_API','PROBE_TRACE','RX_DECISION','RX_DECISION_SUMMARY'):row['_source_side']=str(side)
        for tag,rows in document.items():b.setdefault(tag,[]).extend(rows)
    sampling=b.get('SHARED_SAMPLING',[]);need(len(sampling)==(2 if device else 1),'sampling declaration')
    declaration=sampling[0];run=declaration['run'];n=int(declaration['cycles'])
    need(re.fullmatch('[0-9a-f]{16}',run) is not None,'run identity format')
    need(1<=n<=100 and (expected_cycles is None or n==expected_cycles),'requested cycle count')
    scope=dict(detailed_cycles='1',detail_scope='per_sample_frames',ledger_scope='all_cycles',lifecycle_scope='all_cycles',observer_scope='stage_local')
    scope.update(dict(sdk_scope='stage_local') if device else dict(fake_scope='send_latched'))
    for declared in sampling:need(declared['run']==run and int(declared['cycles'])==n and all(declared.get(k)==v for k,v in scope.items()),'declared detail scope')
    need('FIRST_FAILURE' not in b,'fixture first failure')
    for tag,rows in b.items():
        if tag.startswith('SHARED_') and tag!='SHARED_SAMPLING':
            for r in rows:
                need(r.get('run')==run and r.get('side') in ('0','1'),'row run/side')
                if tag not in ('SHARED_RESULT','SHARED_SETTLE','SHARED_CONFIG','SHARED_WAIT'):need(1<=int(r['cycle'])<=n,'row cycle')
    summary=b.get('TRACE_SUMMARY',[]);frames=b.get('HOST_MODEL',[])
    if device:
        frames=b.get('DDS_FRAME',[]);summaries=b.get('DDS_FRAME_SUMMARY',[])
        need(len(summaries)==2 and len(b.get('PROBE_TRACE',[]))==2 and len(b.get('RX_DECISION_SUMMARY',[]))==2,'all target observation summaries')
        need('PROBE_FIRST_FAILURE' not in b,'target first failure')
        for side in range(2):
            f=[r for r in frames if r['_source_side']==str(side)];s=[r for r in summaries if r['_source_side']==str(side)][0]
            need(int(s['count'])==len(f)<=int(s['limit'])==16384 and int(s['overflow'])==0,'target frame bound/loss')
            need([int(r['event']) for r in f]==list(range(1,len(f)+1)),'target frame order')
            need(all(int(r['cycle']) in ((1,) if n>1 else (0,)) for r in f),'target stage-local detail cycle')
            observations,omitted=int(s['observations']),int(s['omitted'])
            packets={int(r['packet']) for r in f}
            need(0<=omitted<=observations and len(packets)<=observations-omitted and all(1<=v<=observations for v in packets),'target frame observation/omission bounds')
            for tag in ('PROBE_TRACE','RX_DECISION_SUMMARY'):
                r=[r for r in b[tag] if r['_source_side']==str(side)][0]
                row_tag='PROBE_API' if tag=='PROBE_TRACE' else 'RX_DECISION'
                actual=[x for x in b.get(row_tag,[]) if x['_source_side']==str(side)]
                count,total,omitted=int(r['count']),int(r['total']),int(r['omitted'])
                need(0<=count==len(actual)<=int(r['capacity' if tag=='PROBE_TRACE' else 'limit'])==16384,'target '+row_tag+' row/count/capacity')
                need([int(x['event']) for x in actual]==list(range(1,count+1)),'target '+row_tag+' event sequence')
                need(int(r['overflow'])==0 and 0<=omitted<=total and count+omitted==total,'target required/omitted accounting')
                if tag=='RX_DECISION_SUMMARY':need(int(r['unknown'])==0,'unknown target decision')
            need(n==1 or int(s['omitted'])>0,'target sampling inactive')
        summary={'omitted_observations':sum(int(r['omitted']) for r in summaries)}
    else:
        need(len(summary)==1,'complete Host trace summary required');summary=summary[0]
        validate_host_trace(summary,frames,run,n)
    ids=[];ledgers=[]
    return analyze_cycle_ledgers(b,ids,ledgers,frames,summary,run,n,device_case)

def validate_host_trace(summary,frames,run,n):
    need(int(summary['count'])==len(frames)<=int(summary['limit'])==8192,'trace capacity/count')
    need(all(int(summary[k])==0 for k in ('overflow','lost_detail_rows','required_lost')),'unexpected evidence loss')
    need(0<=int(summary['omitted_observations'])<=int(summary['detail_observations']),'omission accounting')
    if n>1:need(int(summary['omitted_observations'])>0,'cycle detail not bounded')
    need(not any(r.get('stage') in ('TRACE_ERROR','PUMP_ERROR') for r in frames),'recorded error')
    detail_stages={'FAKE_SEND','FAKE_INGRESS','FAKE_SAVED_SERVER_CALLBACK','SHARED_SEND_TICKET','RX_OWNED','RX_RUNNING'}
    need([int(r['event']) for r in frames]==list(range(1,len(frames)+1)),'frame event order')
    for r in frames:
        need(int(r['run'])==int(run,16) and r['side'] in ('0','1'),'frame run/side')
        need(int(r['cycle'])==(1 if r['stage'] in detail_stages else 0),'frame observation cycle')
    need(int(summary['stored_detail_rows'])==sum(r['stage'] in detail_stages for r in frames),'stored detail row accounting')
def validate_send_cancellations(b,index,side,n):
    declarations=b.get('SHARED_SAMPLING',[])
    policies=[r.get('send_policy') for r in declarations]
    rows=[r for r in b.get('SHARED_SEND_CANCELLED',[]) if r.get('side')==str(side)]
    summaries=[r for r in b.get('SHARED_SEND_SUMMARY',[]) if r.get('side')==str(side)]
    if not any(policies):
        need(not rows and not summaries,'undeclared send cancellation evidence')
        return
    need(all(p=='selected_retirement_v1' for p in policies),'send cancellation policy')
    need(len(summaries)==1 and summaries[0]['control_ok']=='1' and int(summaries[0]['cycle'])==n,'send control summary')
    need(int(summaries[0]['cancelled'])==len(rows),'actual cancellation count')
    need(len(rows)<=n+2,'cancellation control bound')
    retired=set()
    for count,r in enumerate(rows,1):
        need(int(r['count'])==count,'cancellation sequence')
        need(r['status']=='29' and r['outcome']=='0' and r['code_present']=='0','pre SDK Revoked only')
        need(all(r[k]=='1' for k in ('selected','view_closing','admission_closed','registration_ok','shared_live')),'selected registration closure')
        key=(r['group'],int(r['life']));need(key in index,'cancelled lifetime')
        need(key not in retired,'duplicate cancelled lifetime');retired.add(key)
        own=index[key]
        need(all(r[k]==own[k] for k in ('app','receiver','token')),'cancelled selected identity')
        final=key==('Q',1) or key==('P',n+1)
        need(r['phase']==('FINAL' if final else 'GAP') and int(r['cycle'])==(n if final else key[1]),'cancelled control cycle/phase')
        deleted=[d for d in b.get('SHARED_DELETE',[]) if d.get('side')==str(side) and (d['group'],int(d['life']))==key]
        need(len(deleted)==1 and all(r[k]==deleted[0][k] for k in ('cycle','phase','app','receiver')),'cancelled deletion evidence')

def analyze_cycle_ledgers(b,ids,ledgers,frames,summary,run,n,device_case=None):
    device=device_case is not None
    native_baselines=[]
    actual_sends=0
    for side in range(2):
        ownrows=lambda tag:[r for r in b.get(tag,[]) if r.get('side')==str(side)]
        created=ownrows('SHARED_ID');index={}
        need(len(created)==n+2,'per-side create count')
        if device:
            need([r['group'] for r in created]==(['P','Q'] if device_case=='SHARED_W1' else ['Q','P'])+['P']*n,'target creation order')
            need(all(int(r['token'])>0 for r in created) and len({r['token'] for r in created})==n+2,'target logical tokens')
            config=ownrows('SHARED_CONFIG');need(len(config)==n+2,'target effective config count')
            for r in config:need(all(r.get(k)==v for k,v in dict(reliability='RELIABLE',durability='VOLATILE',history='16',samples='32',instances='1',per_instance='32',builtin='0',intraprocess='0',sharing='0',max_record='8192',local_records='32',detail='16384',payload='32',meta_user_same='1').items()),'target effective config')
        for r in created:
            k=(r['group'],int(r['life']));need(k not in index,'duplicate lifetime')
            need(k==('Q',1) or k[0]=='P' and 1<=k[1]<=n+1,'P/Q lifetime')
            need(int(r['cycle'])==max(1,k[1]-1),'creation cycle')
            need(all(re.fullmatch('[0-9a-f]{32}',r[x]) for x in ('participant','writer','reader')),'GUID missing')
            need(int(r['app'])>0 and int(r['receiver'])>0,'receiver identity missing');index[k]=r
        need(set(index)=={('Q',1)}|{('P',l) for l in range(1,n+2)},'actual lifetimes')
        for field in ('participant','writer','reader','app','receiver'):
            need(len({r[field] for r in created})==n+2,'reused create identity')
        need(len({r['name'] for r in created if r['group']=='P'})==1,'P display name drift')
        validate_send_cancellations(b,index,side,n)
        deleted=ownrows('SHARED_DELETE');need(len(deleted)==n+2,'final deletions required');seen=set()
        for r in deleted:
            k=(r['group'],int(r['life']));need(k in index and k not in seen,'delete lifetime');seen.add(k)
            old=index[k];final=k==('Q',1) or k==('P',n+1)
            need(r['phase']==('FINAL' if final else 'GAP') and int(r['cycle'])==(n if final else k[1]),'delete cycle/phase')
            need(r['participant']==old['participant'] and r['writer']==old['writer'] and r['input_open']=='0','delete identity/drain')
            need(r.get('entities_rc')==r.get('participant_rc')=='0' if device else r['deleted']=='1','actual delete success')
            need(r['app']==old['app'] and r['receiver']==old['receiver'],'delete application/receiver')
            need(r['egress_closed']==r['egress_drained']=='1' and r['tx_records']==r['tx_bytes']=='0','old view egress drain')
            if device:
                own_frames=[f for f in frames if f['_source_side']==str(side)]
                need('frames' in r,'target delete frame boundary missing');boundary=int(r['frames'])
                need(0<=boundary<=len(own_frames),'target delete frame boundary range')
                need(not any(f['stage']=='RX_RUNNING' and f['app']==old['app'] and f['receiver']==old['receiver'] and int(f['event'])>boundary for f in own_frames),'target old receiver after delete boundary')
        tables=[]
        expected=set()
        for c in range(1,n+1):
            for phase,g,life,ordinals in [('INITIAL','P',c,range(1,4)),('INITIAL','Q',1,range(9*c-8,9*c-5)),('GAP','Q',1,range(9*c-5,9*c-2)),('REATTACH','P',c+1,range(1,4)),('REATTACH','Q',1,range(9*c-2,9*c+1))]:
                expected.update((c,phase,g,life,ordinal) for ordinal in ordinals)
        for tag in ('SHARED_WRITE','SHARED_TAKE'):
            table={};sequences=set()
            for r in ownrows(tag):
                k=(int(r['cycle']),r['phase'],r['group'],int(r['life']),int(r['ordinal']))
                need(k in expected and k not in table,'per-cycle sample identity/phase/duplicate')
                seq=(r['writer'],int(r['sn']));need(seq[1]>0 and seq not in sequences,'old sample reused across cycle/phase');sequences.add(seq);table[k]=r
            need(set(table)==expected,'per-side per-cycle ledger incomplete');tables.append(table)
        if device:
            waits=ownrows('SHARED_WAIT');expected_waits={k[:4] for k in expected}
            need(len(waits)==5*n and {(int(r['cycle']),r['phase'],r['group'],int(r['life'])) for r in waits}==expected_waits and all(r['rc']=='0' for r in waits),'target DDS wait receipts')
        native=ownrows('SHARED_NATIVE');need(len(native)==3*n+1,'native cycle receipts')
        nk=[(int(r['cycle']),r['stage']) for r in native]
        need(nk==[(c,s) for c in range(1,n+1) for s in ('INITIAL','GAP','REATTACHED')]+[(n,'FINAL')],'native stage order/duplicate')
        baseline=native[0]
        native_baselines.append(baseline)
        invariant=('runtime','provider','listener_entry','outgoing_entry','listener','outgoing','accepted','listener_owner','outgoing_owner','accepted_owner','socket_calls','listen_calls','bind_calls','shutdown_calls')
        if device:
            need('listener_record' in baseline and 'accepted_record' in baseline,'target native record identity missing')
            need(int(baseline['listener_record'])==int(baseline['accepted_record'])>0,'target native record identity')
            invariant+=('listener_record','accepted_record')
        need(all(baseline[k]==v for k,v in dict(socket_calls='2',listen_calls='1',bind_calls='1',shutdown_calls='0').items()),'native initial topology')
        for r in native[:-1]:
            need(all(r.get(k)==baseline[k] for k in invariant),'native continuity')
            v=1 if r['stage']=='GAP' else 2
            need(int(r['views'])==int(r['active_apps'])==v,'gap/live memberships')
            need(int(r['retained_apps'])<=2 and int(r['view_controls'])<=3,'resource accumulation')
            need(int(r['records_limit'])==64 and int(r['bytes_limit'])==524288,'frozen runtime resource limits')
            need(0<=int(r['records'])<=64 and 0<=int(r['bytes'])<=524288,'runtime resource bound')
        end=native[-1];need(all(int(end[k])==0 for k in ('views','active_apps','records','bytes','retained_apps')),'final resources')
        send_counts=[int(r['send_calls']) for r in native]
        need(send_counts==sorted(send_counts) and send_counts[0]>=0 and send_counts[-1]>0,'actual send call progression')
        actual_sends+=send_counts[-1]
        shutdown=[r for r in (b.get('PROBE_API',[]) if device else frames) if (r.get('_source_side') if device else r.get('side'))==str(side) and (r.get('name')=='Shutdown_enter' if device else r.get('stage')=='FAKE_C_SHUTDOWN')]
        notified=[r for r in frames if r.get('side')==str(side) and r.get('stage')=='FAKE_TERMINAL_NOTIFICATION']
        # A peer notification already closes an owner; the adapter must not call
        # Shutdown again. Count actual calls and all three terminal identities.
        need(int(end['shutdown_calls'])==len(shutdown),'final actual shutdown accounting')
        if device:
            settle=ownrows('SHARED_SETTLE');need(len(settle)==1 and settle[0]['drained']=='1' and settle[0]['fallback']=='0','target final responsibilities')
            original={int(baseline[x]) for x in ('listener','outgoing','accepted')};local=[int(r['socket']) for r in shutdown]
            need(len(original)==3 and len(local)==len(set(local)) and set(local)<=original,'target duplicate/foreign Shutdown identity')
            peer={int(r['socket']) for r in b.get('PROBE_API',[]) if r['_source_side']==str(side) and r['name']=='OnShutdown'}
            # Callback repeats do not close another owner, and are not treated as
            # proof of SDK exactly-once delivery or physical quiescence.
            need(set(local)|(peer&original)==original,'target terminal identity coverage')
        else:
            terminal=[int(r['detail'].split('=',1)[1]) for r in shutdown+notified]
            need(len(terminal)==3 and set(terminal)=={100+100*side+i for i in range(3)},'all original native owners closed exactly once')
            links=ownrows('SHARED_LINK');need(len(links)==1,'actual Fake transport counter receipt');link=links[0]
            need(link['phase']=='FINAL' and int(link['cycle'])==n and int(link['sends'])==send_counts[-1] and 0<int(link['forwards'])<=int(link['sends']),'actual Fake send/forward counts')
            need(all(int(link[k])==0 for k in ('queued','bytes','invalid')) and 0<=int(link['record_peak'])<=64 and 0<=int(link['byte_peak'])<=524288,'Fake resource bound')
            need(int(link['shutdowns'])==len(shutdown) and int(link['notifications'])==len(notified),'actual Fake terminal counts')
        result=ownrows('SHARED_RESULT');need(len(result)==1,'per-side final result');result=result[0]
        need(result['success']=='1' and int(result['cycles'])==n,'completion cycle')
        need(int(result['p_created'])==int(result['p_deleted'])==n+1 and int(result['q_created'])==int(result['q_deleted'])==1,'rebuild accounting')
        need(int(result['writes'])==len(tables[0])==15*n and int(result['takes'])==len(tables[1])==15*n,'observed versus summary')
        ids.append(index);ledgers.append(tables)
    for side in range(2):
        matches=[r for r in b.get('SHARED_MATCH',[]) if r['side']==str(side)]
        need(len(matches)==4*n,'fresh match receipts')
        for c in range(1,n+1):
            subset=[r for r in matches if int(r['cycle'])==c]
            need([(r['phase'],r['group'],int(r['life'])) for r in subset]==[('INITIAL','P',c),('INITIAL','Q',1),('REATTACH','P',c+1),('REATTACH','Q',1)],'cycle match order/phase')
            for r in subset:
                k=(r['group'],int(r['life']));own=ids[side][k];peer=ids[1-side][k]
                need(r['writer']==own['writer'] and r['reader']==own['reader'] and r['remote_writer']==peer['writer'] and r['remote_reader']==peer['reader'],'fresh exact remote match')
                need(r['app']==own['app'] and r['receiver']==own['receiver'],'match application/receiver')
        for k,take in ledgers[side][1].items():
            own=ids[side][k[2:4]];peer=ids[1-side][k[2:4]];write=ledgers[1-side][0][k]
            need(take['writer']==write['writer']==peer['writer'] and take['reader']==own['reader'] and take['sn']==write['sn'],'cross-side Writer/SN identity')
            need(take['valid']=='1' and take['app']==own['app'] and take['receiver']==own['receiver'] and write['rc']=='0' and write['serialized']=='60','real payload/write/take')
            need(write['app']==peer['app'] and write['receiver']==peer['receiver'],'write application/receiver')
            if k[0]==1:
                incoming=[r for r in frames if r.get('kind')=='DATA' and r.get('writer')==take['writer'] and r.get('sn')==take['sn']]
                source=lambda r:r.get('_source_side') if device else r.get('side')
                owned=[r for r in incoming if r.get('stage')=='RX_OWNED' and source(r)==str(side) and r.get('app')==own['app'] and r.get('receiver')==own['receiver']]
                def linked(o):
                    same=lambda r:r.get('hash')==o['hash'] and r.get('length')==o['length']
                    if device:
                        native=native_baselines[side];remote=native_baselines[1-side]
                        return o['owner']==native['accepted_owner'] and any(r['stage']=='SDK_SEND' and source(r)==str(1-side) and r['owner']==remote['outgoing_owner'] and r['socket']==remote['outgoing'] and int(r['admitted'])>0 and int(r['entered'])>0 and same(r) for r in incoming) and any(r['stage']=='SDK_INGRESS' and source(r)==str(side) and r['packet']==o['ingress'] and r['socket']==native['accepted'] and same(r) for r in incoming) and any(r['stage']=='RX_RUNNING' and source(r)==str(side) and r['app']==own['app'] and r['receiver']==own['receiver'] and r['correlation']==o['correlation'] and same(r) for r in incoming)
                    return any(r.get('stage')==('SDK_SEND' if device else 'FAKE_SEND') and source(r)==str(1-side) and same(r) for r in incoming) and any(r.get('stage')==('SDK_INGRESS' if device else 'FAKE_INGRESS') and source(r)==str(side) and same(r) for r in incoming)
                need(any(linked(o) for o in owned),'sampled cycle actual Fake/owned path missing')
    return dict(pass_=True,case=device_case or 'HOST_CYCLE',run=run,cycles=n,reader_samples=sum(len(t[1]) for t in ledgers),writes=sum(len(t[0]) for t in ledgers),actual_send_calls=actual_sends,send_policy=b.get('SHARED_SAMPLING',[{}])[0].get('send_policy','legacy'),cancelled_before_sdk=sum(int(r['cancelled']) for r in b.get('SHARED_SEND_SUMMARY',[])),expected_reader_samples=30*n,expected_writes=30*n,detailed_cycles=1,omitted_observations=int(summary['omitted_observations']),continuity='OBJECT_AND_PUBLIC_CALL_SCOPE_ONLY',sdk_epoch='UNPROVEN',sdk_drain='UNPROVEN')

def analyze(case,texts,expected_cycles=None):
    if case=='HOST_CYCLE':
        need(len(texts)==1,'one actual Host process output');return analyze_cycles(texts[0],expected_cycles)
    if expected_cycles is not None:
        need(case in ('SHARED_W1','SHARED_W2') and len(texts)==2,'cycle target topology');return analyze_cycles(texts,expected_cycles,case)
    return analyze_documents(case,[parse(t) for t in texts])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--case',required=True,choices=['SHARED_W1','SHARED_W2','HOST_CYCLE']);p.add_argument('--a',type=Path,required=True);p.add_argument('--b',type=Path);p.add_argument('--cycles',type=int);p.add_argument('--output',type=Path,required=True);p.add_argument('--require-send-policy',choices=['selected_retirement_v1']);a=p.parse_args()
    try:
        result=analyze(a.case,[path.read_text(encoding='utf-8-sig') for path in (a.a,a.b) if path],a.cycles)
        if a.require_send_policy:need(result.get('send_policy')==a.require_send_policy,'required send policy missing')
        # JSON keys must remain explicit sample identities, not tuple coercions.
        if 'ledger' in result:result['ledger']=[[list(table.values()) for table in side] for side in result['ledger']]
    except (ValueError,KeyError,TypeError,OSError) as e:result=dict(pass_=False,case=a.case,first_failure=str(e))
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');raise SystemExit(0 if result['pass_'] else 1)
