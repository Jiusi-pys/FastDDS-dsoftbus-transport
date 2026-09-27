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

def analyze(case,texts):return analyze_documents(case,[parse(t) for t in texts])

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--case',required=True,choices=['SHARED_W1','SHARED_W2']);p.add_argument('--a',type=Path,required=True);p.add_argument('--b',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    try:
        result=analyze(a.case,[a.a.read_text(encoding='utf-8-sig'),a.b.read_text(encoding='utf-8-sig')])
        # JSON keys must remain explicit sample identities, not tuple coercions.
        result['ledger']=[[list(table.values()) for table in side] for side in result['ledger']]
    except (ValueError,KeyError,TypeError) as e:result=dict(pass_=False,case=a.case,first_failure=str(e))
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');raise SystemExit(0 if result['pass_'] else 1)
