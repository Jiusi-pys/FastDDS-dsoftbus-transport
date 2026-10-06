"""Local decision accounting; never infers SDK loss, historical cause or harmlessness."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path

BINDING={'BINDING_PHASE','NO_REGISTRATION','NATIVE_ADMISSION','NATIVE_IDENTITY','LOCAL_MISMATCH','BRIDGE_ACCEPTED','BRIDGE_REFUSED'}
ADAPTER={'ADAPTER_STOPPED','ADAPTER_PHASE','CHANNEL_CLOSED','FOREIGN_SOCKET','INVALID_BUFFER','ADAPTER_CAPACITY','FORWARDED'}
ADAPTER_STATUS={'FORWARDED':0,'ADAPTER_STOPPED':6,'ADAPTER_PHASE':5,'CHANNEL_CLOSED':15,'FOREIGN_SOCKET':10,'INVALID_BUFFER':9,'ADAPTER_CAPACITY':13}
BRIDGE_STATUS={0:0,1:12,2:12,3:29,4:3,5:4,6:37,7:29,8:2,9:20,10:3,11:49}
STAGES=set('NATIVE_ENTRY ADAPTER NATIVE_CALLBACK NATIVE_IDENTITY COORDINATOR CAPTURE_GATE CAPTURE COPY COMMIT_GATE COMMIT'.split())
REASONS=set('RETIRED RUNTIME_MISSING OWNER_MISSING WRONG_ROLE CLOSE_CLAIMED NOT_READY CLOSING NULL_BUFFER ZERO_SIZE TOO_LARGE NATIVE_CALLBACK_REJECTED NATIVE_IDENTITY_REJECTED INVALID_PIN NO_APPLICATION CLOCK_OVERFLOW NATIVE_CAPTURE_REJECTED INVALID_DEADLINE INVALID_CLAIM INVALID_LOCAL STOPPED APPLICATION_INACTIVE RECEIVER_NOT_OPEN INVALID_ROUTE EXPIRED GLOBAL_RECORDS GLOBAL_BYTES LOCAL_RECORDS LOCAL_BYTES COPYING_LIMIT APPLICATION_COPYING_EXHAUSTED CAPTURE_ALLOCATION INVALID_COPY COPY_ALLOCATION NATIVE_COMMIT_REJECTED INVALID_COMMIT COMMIT_APPLICATION_INACTIVE COMMIT_RECEIVER_NOT_OPEN COMMIT_RECEIVER_CHANGED ACCEPTED_QUEUE CALLBACK_EXCEPTION'.split())

def need(ok, message):
    if not ok:raise ValueError(message)

def analyze(document, expected_run):
    need(document.get('run')==expected_run,'run identity mismatch; no historical backfill')
    snapshots=document.get('snapshots',[]);need(snapshots,'missing decision snapshot')
    previous=[];last=None
    for snapshot in snapshots:
        rows=snapshot['rows'];s=snapshot['summary'];counts=Counter(snapshot['branches']);reasons=Counter(snapshot['reasons'])
        need(len(rows)==s['count'] and 0<=s['count']<=s['limit'],'missing detail or capacity mismatch')
        need(s['total']==s['count']+s['overflow'] and s['overflow']>=0,'overflow accounting mismatch')
        need([int(r['event']) for r in rows]==list(range(1,len(rows)+1)),'duplicate/missing event')
        need(rows[:len(previous)]==previous,'cumulative snapshot changed or regressed')
        unknown=bool(s['unknown']);seen=set();observed=Counter();observed_reasons=Counter();refused=0;adapter=0
        increments=defaultdict(list)
        for r in rows:
            branch=r['branch'];layer=r['layer'];called=int(r['bridge_called']);refusal=int(r['refused_receive'])
            need(int(r['side']) in (0,1) and ('side' not in document or int(r['side'])==document['side']),'wrong board identity')
            need(layer in ('BINDING','ADAPTER'),'unknown layer')
            key=(r['side'],layer,r['record'],r['role'],r['socket'],r['sequence'])
            need(key not in seen,'duplicate decision identity');seen.add(key)
            if branch not in BINDING|ADAPTER:unknown=True;continue
            need(branch in (BINDING if layer=='BINDING' else ADAPTER),'wrong branch layer')
            if int(r.get('role',-1)) not in (0,1) or int(r.get('phase',-1)) not in range(7):unknown=True
            if layer=='ADAPTER':
                if int(r.get('status_known',0))!=1 or int(r.get('status',-1))!=ADAPTER_STATUS[branch]:unknown=True
                if branch=='ADAPTER_PHASE' and int(r.get('phase',-1))==3:unknown=True
                if branch not in ('ADAPTER_PHASE','ADAPTER_STOPPED') and int(r.get('phase',-1))!=3:unknown=True
            elif branch=='BINDING_PHASE':
                if int(r.get('status_known',0))!=0 or int(r.get('phase',-1))==3:unknown=True
            else:
                if int(r.get('status_known',0))!=1 or int(r.get('status',-1)) not in range(51):unknown=True
                if branch=='NO_REGISTRATION' and int(r.get('status',-1))!=43:unknown=True
            observed[branch]+=1
            need(called==int(branch in ('BRIDGE_ACCEPTED','BRIDGE_REFUSED')),'invented/skipped bridge call')
            need(refusal==int(branch in ('NO_REGISTRATION','BRIDGE_REFUSED')),'accepted/cancelled work counted as refusal')
            if called:
                observed_reasons[r['reason']]+=1
                if int(r['result_known'])!=1 or r['reason'] not in REASONS or int(r['result']) not in BRIDGE_STATUS or r.get('stage') not in STAGES:unknown=True
                elif branch=='BRIDGE_ACCEPTED':need(int(r['result'])==0 and r['reason']=='ACCEPTED_QUEUE','accepted result mismatch')
                else:need(int(r['result'])!=0 and r['reason']!='ACCEPTED_QUEUE','refused result mismatch')
                if int(r['result']) in BRIDGE_STATUS and int(r.get('status',-1))!=BRIDGE_STATUS[int(r['result'])]:unknown=True
            else:need(not int(r['result_known']) and r['reason']=='UNOBSERVED','uncalled bridge fabricated result/reason')
            if refusal:
                refused+=1;increments[key[:5]].append(int(r['refused_receive_total']))
            if layer=='ADAPTER' and branch!='FORWARDED':adapter+=1
        if not unknown:
            need(sum(counts.values())==s['total'],'aggregate totals mismatch')
            for key,value in observed.items():need(value<=counts[key],'detail exceeds reason aggregate')
            for key,value in observed_reasons.items():need(value<=reasons[key],'detail exceeds bridge aggregate')
            if not s['overflow']:
                need(+observed==+counts and +observed_reasons==+reasons,'aggregate/detail mismatch')
                for values in increments.values():need(sorted(values)==list(range(1,len(values)+1)),'original refused_receive increments missing/repeated')
        sdk_accounting='NOT_REQUESTED_UNIT_OR_HOST_INPUT'
        if document.get('sdk_trace_required'):
            trace=document.get('sdk_trace');api=document.get('sdk_api',[])
            complete=bool(trace) and not int(trace['overflow']) and [int(r['event']) for r in api]==list(range(1,int(trace['count'])+1))
            sdk=Counter((int(r['role']),int(r['socket']),int(r['value']),int(r['length'])) for r in api if r['name']=='OnBytesRejected')
            adapter_rows=Counter((int(r['role']),int(r['socket']),int(r['status']),int(r['size'])) for r in rows if r['layer']=='ADAPTER' and r['branch'] in ADAPTER-{'FORWARDED'})
            sdk_accounting='COUNT_AND_SIGNATURE_RECONCILED' if complete and sdk==adapter_rows else 'INCOMPLETE_OR_UNMATCHED'
            if sdk_accounting!='COUNT_AND_SIGNATURE_RECONCILED':unknown=True
        status='UNKNOWN_REASON' if unknown else 'DETAIL_TRUNCATED' if s['overflow'] else 'COMPLETE_DECISION_ACCOUNTING'
        last=dict(status=status,run=expected_run,total_decisions=s['total'],detail_count=s['count'],overflow=s['overflow'],
            binding_refused_receive=refused,adapter_rejected=adapter,branch_totals=dict(counts),bridge_reason_totals=dict(reasons),
            unique_lost_messages=None,detail_scope='RECORDED_PREFIX' if s['overflow'] else 'SNAPSHOT_PREFIX',
            historical_cause='HISTORICAL_CAUSE_UNRESOLVED',reproduction='OBSERVED' if refused else 'NOT_REPRODUCED',
            sdk_rejection_accounting=sdk_accounting)
        previous=rows
    return last

def parse(text):
    result={'run':None,'snapshots':[],'sdk_api':[]};rows=[];branches={};reasons={}
    for line in text.splitlines():
        parts=line.split()
        if not parts:continue
        fields=dict(x.split('=',1) for x in parts[1:] if '=' in x)
        if parts[0]=='DDS_MAIN':
            need(result['run'] in (None,fields['run']),'mixed run');result['run']=fields['run']
            need(fields.get('side') in ('A','B'),'missing/wrong main board');side=0 if fields['side']=='A' else 1
            need(result.get('side',side)==side,'mixed board');result['side']=side;result['sdk_trace_required']=True
        elif parts[0]=='PROBE_API':result['sdk_api'].append(fields)
        elif parts[0]=='PROBE_TRACE':
            need('sdk_trace' not in result,'duplicate SDK trace summary');result['sdk_trace']=fields
        elif parts[0]=='RX_DECISION':
            rows.append({k:(int(v) if v.lstrip('-').isdigit() else v) for k,v in fields.items()})
        elif parts[0]=='RX_DECISION_BRANCH':
            need(fields['branch'] not in branches,'duplicate aggregate');branches[fields['branch']]=int(fields['count'])
        elif parts[0]=='RX_DECISION_REASON':
            need(fields['reason'] not in reasons,'duplicate bridge aggregate');reasons[fields['reason']]=int(fields['count'])
        elif parts[0]=='RX_DECISION_SUMMARY':
            result['snapshots'].append(dict(rows=rows,branches=branches,reasons=reasons,summary={k:int(v) for k,v in fields.items()}));rows=[];branches={};reasons={}
    need(not rows and not branches and not reasons,'incomplete final diagnostic export')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--run',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=parse(a.log.read_text(encoding='utf-8-sig'));report=analyze(data,a.run)
    a.output.write_text(json.dumps(dict(report=report,evidence=data),indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report));raise SystemExit(0 if report['status']=='COMPLETE_DECISION_ACCOUNTING' else 2)
