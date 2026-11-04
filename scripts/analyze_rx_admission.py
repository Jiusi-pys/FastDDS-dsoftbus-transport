"""Strict offline accounting of schema-1 callback admission observations.

This never infers historic causes, reads payload bytes, or classifies an unknown
outcome as harmless. Complete means records observed through the last snapshot,
not SDK quiescence, sample delivery, or production qualification.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

STAGES = set('NATIVE_ENTRY ADAPTER NATIVE_CALLBACK NATIVE_IDENTITY COORDINATOR CAPTURE_GATE CAPTURE COPY COMMIT_GATE COMMIT'.split())
REASONS = set('RETIRED RUNTIME_MISSING OWNER_MISSING WRONG_ROLE CLOSE_CLAIMED NOT_READY CLOSING NULL_BUFFER ZERO_SIZE TOO_LARGE NATIVE_CALLBACK_REJECTED NATIVE_IDENTITY_REJECTED INVALID_PIN NO_APPLICATION CLOCK_OVERFLOW NATIVE_CAPTURE_REJECTED INVALID_DEADLINE INVALID_CLAIM INVALID_LOCAL STOPPED APPLICATION_INACTIVE RECEIVER_NOT_OPEN INVALID_ROUTE EXPIRED GLOBAL_RECORDS GLOBAL_BYTES LOCAL_RECORDS LOCAL_BYTES COPYING_LIMIT APPLICATION_COPYING_EXHAUSTED CAPTURE_ALLOCATION INVALID_COPY COPY_ALLOCATION NATIVE_COMMIT_REJECTED INVALID_COMMIT COMMIT_APPLICATION_INACTIVE COMMIT_RECEIVER_NOT_OPEN COMMIT_RECEIVER_CHANGED ACCEPTED_QUEUE CALLBACK_EXCEPTION'.split())
CAPACITY_REASONS = ('GLOBAL_RECORDS','GLOBAL_BYTES','LOCAL_RECORDS','LOCAL_BYTES','COPYING_LIMIT','APPLICATION_COPYING_EXHAUSTED')
DIAGNOSTIC_TAGS = {'EXPERIMENT_RX_ADMISSION','EXPERIMENT_RX_ADMISSION_SUMMARY','EXPERIMENT_RX_ADMISSION_REASON_COUNT','EXPERIMENT_RX_ENTRY_UNCORRELATED','EXPERIMENT_RX_ENTRY_UNCORRELATED_SUMMARY'}
BOOL_FIELDS = 'result_known native_status_known native_known application_known receiver_known entry_state_known adapter_state_known lifecycle_known capacity_known stopped endpoint_limit_known record_limit_known'.split()
NUMBER_FIELDS = 'runtime_id callback_slot socket candidate_count size endpoint_limit record_limit native_slot native_incarnation native_generation native_route_version application_incarnation logical_token receiver_incarnation entry_state adapter_state ingress_receiver_marker capacity_mask records_used records_limit bytes_used bytes_limit local_records_used local_records_limit local_bytes_used local_bytes_limit copying_used copying_limit application_copying application_copying_limit'.split()
# Schema-1 emitted branch/result pairs. None means the model was not called;
# the numbers are ReceiveAdmissionResult in ApplicationAttachments.hpp, not SDK rc.
CONTRACT = {
    'RETIRED': {'NATIVE_ENTRY':None},
    **{r:{'ADAPTER':None} for r in ('RUNTIME_MISSING','OWNER_MISSING','WRONG_ROLE','CLOSE_CLAIMED','NOT_READY','CLOSING')},
    **{r:{'ADAPTER':None,'CAPTURE_GATE':4,'CAPTURE':4} for r in ('NULL_BUFFER','ZERO_SIZE')},
    'TOO_LARGE':{'ADAPTER':None,'CAPTURE':5},
    'NATIVE_CALLBACK_REJECTED':{'NATIVE_CALLBACK':None}, 'NATIVE_IDENTITY_REJECTED':{'NATIVE_IDENTITY':None},
    'INVALID_PIN':{'COORDINATOR':7}, 'NO_APPLICATION':{'COORDINATOR':3,'CAPTURE':3},
    'CLOCK_OVERFLOW':{'COORDINATOR':11}, 'NATIVE_CAPTURE_REJECTED':{'CAPTURE_GATE':7},
    'INVALID_DEADLINE':{'CAPTURE_GATE':10}, 'INVALID_CLAIM':{'CAPTURE':7},
    'INVALID_LOCAL':{'CAPTURE':1}, 'STOPPED':{'CAPTURE':9},
    'APPLICATION_INACTIVE':{'CAPTURE':3}, 'RECEIVER_NOT_OPEN':{'CAPTURE':3},
    'INVALID_ROUTE':{'CAPTURE':2}, 'EXPIRED':{'CAPTURE':11,'COMMIT':11},
    **{r:{'CAPTURE':6} for r in CAPACITY_REASONS},
    'CAPTURE_ALLOCATION':{'CAPTURE':8}, 'INVALID_COPY':{'COPY':4}, 'COPY_ALLOCATION':{'COPY':8},
    'NATIVE_COMMIT_REJECTED':{'COMMIT_GATE':7},
    **{r:{'COMMIT':7} for r in ('INVALID_COMMIT','COMMIT_APPLICATION_INACTIVE','COMMIT_RECEIVER_NOT_OPEN','COMMIT_RECEIVER_CHANGED')},
    'ACCEPTED_QUEUE':{'COMMIT':0},
}


def analyze_text(text, expected_run):
    errors, rows = [], []
    def fail(message):
        errors.append(message)
    def integer(row, name, minimum=0):
        value = row.get(name, '')
        if not re.fullmatch(r'-?\d+', value) or int(value) < minimum:
            raise ValueError(f'line {row["line"]}: invalid/missing {name}')
        return int(value)
    if not re.fullmatch(r'[0-9a-f]{16}', expected_run or ''):
        return {'complete':False,'errors':['invalid expected run'],'units':{}}
    for number, raw in enumerate(text.splitlines(), 1):
        words = raw.lstrip('\ufeff').split()
        if not words:
            continue
        fields = re.findall(r'(\w+)=([^\s]+)', raw)
        row = dict(fields)
        row.update(tag=words[0], line=number)
        if any(row.get(k,expected_run) != expected_run for k in ('run','run_id')):
            fail(f'line {number}: mixed run')
        if words[0] in DIAGNOSTIC_TAGS:
            if len(fields) != len(dict(fields)):
                fail(f'line {number}: duplicate diagnostic field')
            if row.get('schema') != '1':
                fail(f'line {number}: unsupported schema')
        rows.append(row)
    records, ingress, summaries, reason_rows = {}, {}, defaultdict(list), {}
    unknown_slots, unknown_total = {}, None
    raw_ingress = 0
    for row in rows:
        try:
            tag = row['tag']
            if tag == 'EXPERIMENT_NATIVE_INGRESS' and row.get('native_event') == 'OnBytes':
                raw_ingress += 1
                if row.get('run')!=expected_run:fail(f'line {row["line"]}: native ingress missing run identity')
                key = (integer(row,'instance_id',1), integer(row,'invocation',1))
                if key in ingress:fail(f'duplicate native ingress {key}')
                ingress[key] = row
            elif tag == 'EXPERIMENT_RX_ADMISSION':
                key = (integer(row,'instance_id',1), integer(row,'invocation',1))
                if row.get('run') != expected_run:fail(f'line {row["line"]}: missing run identity')
                if key in records:fail(f'duplicate terminal admission {key}')
                records[key] = row
                if row.get('reason') not in REASONS or row.get('stage') not in STAGES:
                    fail(f'{key}: unknown reason/stage is not classified')
                for field in BOOL_FIELDS:
                    if row.get(field) not in ('0','1'):fail(f'{key}: invalid/missing {field}')
                numbers = {f:integer(row,f,-1 if f in ('callback_slot','socket') else 0) for f in NUMBER_FIELDS}
                integer(row,'native_status',-1000000000)
                integer(row,'application_phase',-1);integer(row,'receiver_phase',-1)
                value=integer(row,'result',-1)
                if numbers['candidate_count'] not in (0,1):fail(f'{key}: unsupported candidate fanout')
                if row.get('identity_origin') != 'ADAPTER_CAPTURE_NOT_SDK_EVENT':fail(f'{key}: false native identity origin')
                if row.get('receiver_identity_kind') != 'ENTRY_SNAPSHOT_NOT_EXECUTION_LEASE':fail(f'{key}: false receiver lease claim')
                if row.get('payload_identity') != 'UNOBSERVED':fail(f'{key}: admission diagnostic must not infer payload identity')
                if row.get('result_known') == '0' and value != -1:fail(f'{key}: fabricated result for uncalled model')
                if row.get('result_known') == '1' and value not in range(12):fail(f'{key}: unknown model result')
                reason,stage=row.get('reason'),row.get('stage')
                allowed=CONTRACT.get(reason,{})
                if stage not in allowed:
                    fail(f'{key}: unsupported reason/stage pair')
                else:
                    expected=allowed[stage]
                    if row.get('result_known')!=('0' if expected is None else '1') or value!=(-1 if expected is None else expected):
                        fail(f'{key}: reason/result contract mismatch')
                if reason=='NO_APPLICATION' and (numbers['candidate_count']!=0 or row.get('application_known')!='0'):
                    fail(f'{key}: no-application decision claims a candidate')
                if reason=='RETIRED' and (row.get('entry_state_known')!='1' or not numbers['entry_state']&64):
                    fail(f'{key}: retirement lacks original retired snapshot')
                if reason=='ZERO_SIZE' and numbers['size']!=0:fail(f'{key}: zero-size predicate false')
                if reason=='TOO_LARGE':
                    limit='endpoint_limit' if stage=='ADAPTER' else 'record_limit'
                    if row.get(limit+'_known')!='1' or numbers[limit]==0 or numbers['size']<=numbers[limit]:
                        fail(f'{key}: oversize lacks original true limit predicate')
                state=numbers['adapter_state']; ready=bool(state&8 or (numbers['callback_slot']>=8 and state&16))
                state_predicates={'OWNER_MISSING':not state&1,'WRONG_ROLE':bool(state&1 and not state&2),
                    'CLOSE_CLAIMED':state&7==7,'NOT_READY':state&3==3 and not state&4 and not ready,
                    'CLOSING':state&3==3 and not state&4 and ready and bool(state&32)}
                if reason in state_predicates and (row.get('adapter_state_known')!='1' or not state_predicates[reason]):
                    fail(f'{key}: owner/closing predicate contradicts original snapshot')
                if row.get('reason') == 'ACCEPTED_QUEUE':
                    if row.get('stage')!='COMMIT' or row.get('result_known')!='1' or value!=0 or numbers['candidate_count']!=1:
                        fail(f'{key}: inconsistent accepted outcome')
                    mask=numbers['adapter_state']
                    if row.get('adapter_state_known')!='1' or mask&3!=3 or mask&(4|32|64) or not (mask&8 or (numbers['callback_slot']>=8 and mask&16)):
                        fail(f'{key}: accepted known bad owner/state')
                    if row.get('application_known')!='1' or row.get('receiver_known')!='1':fail(f'{key}: accepted without original candidate identity')
                    if numbers['size']==0 or any(row.get(n+'_known')!='1' or numbers['size']>numbers[n] for n in ('endpoint_limit','record_limit')):
                        fail(f'{key}: accepted size not proved within original limits')
                    if row.get('lifecycle_known')!='1' or row.get('stopped')!='0' or integer(row,'application_phase')!=0 or integer(row,'receiver_phase')!=2:
                        fail(f'{key}: accepted without original active/open lifecycle')
                elif row.get('result_known')=='1' and value==0:
                    fail(f'{key}: rejected reason with Accepted result')
                if row.get('capacity_known')=='1':
                    pairs=(('records_used','records_limit'),('bytes_used','bytes_limit'),('local_records_used','local_records_limit'),('local_bytes_used','local_bytes_limit'),('copying_used','copying_limit'),('application_copying','application_copying_limit'))
                    if any(numbers[u]>numbers[l] or numbers[l]==0 for u,l in pairs):
                        fail(f'{key}: inconsistent budget snapshot')
                    conditions=(numbers['records_used']==numbers['records_limit'],numbers['size']>numbers['bytes_limit']-numbers['bytes_used'],numbers['local_records_used']==numbers['local_records_limit'],numbers['size']>numbers['local_bytes_limit']-numbers['local_bytes_used'],numbers['copying_used']==numbers['copying_limit'],numbers['application_copying']==numbers['application_copying_limit'])
                    actual=sum(1<<i for i,c in enumerate(conditions) if c)
                    if numbers['capacity_mask']!=actual:fail(f'{key}: capacity predicate mask mismatch')
                    if row.get('reason') in CAPACITY_REASONS:
                        first=next((CAPACITY_REASONS[i] for i,c in enumerate(conditions) if c),None)
                        if first!=row['reason'] or value!=6 or row.get('stage')!='CAPTURE':fail(f'{key}: capacity first-branch mismatch')
                    elif actual:fail(f'{key}: true capacity predicate not reflected in decision')
                elif row.get('reason') in CAPACITY_REASONS:
                    fail(f'{key}: capacity reason lacks original budget snapshot')
            elif tag == 'EXPERIMENT_RX_ADMISSION_SUMMARY':
                instance=integer(row,'instance_id',1); seq=integer(row,'snapshot_seq',1)
                if row.get('run')!=expected_run:fail(f'line {row["line"]}: summary run missing')
                if summaries[instance] and seq<=int(summaries[instance][-1]['snapshot_seq']):fail(f'instance {instance}: snapshot regression/duplication')
                summaries[instance].append(row)
            elif tag == 'EXPERIMENT_RX_ADMISSION_REASON_COUNT':
                key=(integer(row,'instance_id',1),integer(row,'snapshot_seq',1),row.get('reason'))
                if key in reason_rows:fail(f'duplicate reason counter {key}')
                if row.get('unit')!='CALLBACK_ADMISSION' or row.get('run')!=expected_run:fail(f'{key}: wrong counter unit/run')
                if row.get('reason') not in REASONS:fail(f'{key}: unclassified reason counter')
                reason_rows[key]=integer(row,'count',1)
            elif tag == 'EXPERIMENT_RX_ENTRY_UNCORRELATED':
                slot=integer(row,'callback_slot'); count=integer(row,'count')
                if slot>=16 or row.get('unit')!='UNCORRELATED_CALLBACK_ENTRY' or row.get('reason')!='MISSING_STATE':fail('bad uncorrelated entry scope')
                if count<unknown_slots.get(slot,0):fail('uncorrelated counter regressed')
                unknown_slots[slot]=count
            elif tag == 'EXPERIMENT_RX_ENTRY_UNCORRELATED_SUMMARY':
                count=integer(row,'total')
                if row.get('unit')!='UNCORRELATED_CALLBACK_ENTRY':fail('bad uncorrelated summary unit')
                if unknown_total is not None and count<unknown_total:fail('uncorrelated total regressed')
                unknown_total=count
        except (ValueError,KeyError) as exc:
            fail(str(exc))
    if not summaries:fail('missing admission summary')
    if unknown_total is None:fail('missing uncorrelated-entry summary')
    elif sum(unknown_slots.values())!=unknown_total:fail('uncorrelated per-slot/total mismatch')
    if unknown_total:fail('uncorrelated callback entry lacks message/application identity')
    for key, row in records.items():
        original=ingress.get(key)
        if original is None:fail(f'{key}: no native ingress counterpart')
        elif any(row.get(f)!=original.get(f) for f in ('callback_slot','socket')):fail(f'{key}: ingress identity mismatch')
    for key in ingress:
        if key not in records:fail(f'{key}: missing terminal admission record')
    snapshot_keys={(i,int(s['snapshot_seq'])) for i,values in summaries.items() for s in values}
    for instance,sequence,reason in reason_rows:
        if (instance,sequence) not in snapshot_keys:
            fail(f'instance {instance}: orphan reason counter snapshot {sequence}')
    by_instance={}
    for instance in set(summaries)|{k[0] for k in records}|{k[0] for k in ingress}:
        if not summaries.get(instance):fail(f'instance {instance}: missing final summary');continue
        summary=summaries[instance][-1]
        try:
            seq=int(summary['snapshot_seq']); total=integer(summary,'total');stored=integer(summary,'stored'); overflow=integer(summary,'overflow');capacity=integer(summary,'capacity',1)
            emitted=integer(summary,'emitted_records'); count=sum(k[0]==instance for k in records); natives=sum(k[0]==instance for k in ingress)
            if total!=stored+overflow or stored>capacity or count!=emitted or count!=stored:fail(f'instance {instance}: retained/emitted/count mismatch')
            if total!=natives:fail(f'instance {instance}: observed callback total differs from ingress')
            if integer(summary,'store_bytes')!=capacity*integer(summary,'record_bytes',1):fail(f'instance {instance}: diagnostic storage bound mismatch')
            if summary.get('coverage')!='OBSERVED_UNTIL_SNAPSHOT':fail(f'instance {instance}: false quiescence coverage')
            if overflow or summary.get('complete_records')!='1':fail(f'instance {instance}: diagnostic overflow/incomplete output')
            reasons={reason:n for (i,s,reason),n in reason_rows.items() if i==instance and s==seq}
            actual=Counter(r['reason'] for k,r in records.items() if k[0]==instance)
            if sum(reasons.values())!=total:fail(f'instance {instance}: reason counter total mismatch')
            if not overflow and reasons!=dict(actual):fail(f'instance {instance}: records/reason counters differ')
            by_instance[str(instance)]={'summary':summary,'reason_counts':reasons,'record_count':count,'native_callback_count':natives}
        except (ValueError,KeyError) as exc:fail(str(exc))
    accepted=sum(r.get('reason')=='ACCEPTED_QUEUE' for r in records.values())
    return {'schema':1,'run':expected_run,'complete':not errors,'errors':errors,
            'coverage':'OBSERVED_UNTIL_SNAPSHOT_NOT_SDK_QUIESCENCE','historical_cause_assignment':'NOT_PERMITTED',
            'units':{'native_callbacks':len(ingress),'raw_ingress_records':raw_ingress,'terminal_admission_records':len(records),
                     'candidate_attempts':sum(int(r.get('candidate_count','0')) for r in records.values() if re.fullmatch(r'\d+',r.get('candidate_count',''))),
                     'accepted_to_queue':accepted,'rejected_callbacks':len(records)-accepted,'uncorrelated_entries':unknown_total},
            'instances':by_instance,'ledger':[dict(r,board=None) for r in records.values()]}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('window',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.exists():raise SystemExit('refusing to overwrite evidence output')
    config=json.loads((args.window/'window.json').read_text(encoding='utf-8-sig'))
    sides={}
    for side in ('A','B'):
        source=args.window/f'{side}.raw.stdout.log'
        sides[side]=analyze_text(source.read_text(encoding='utf-8'),config['run'])
        sides[side]['source']={'path':str(source.resolve()),'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
        for record in sides[side]['ledger']:record['board']=side
    result={'scope':'CURRENT_RUN_CALLBACK_ADMISSION_ACCOUNTING_ONLY','run':config['run'],'window':str(args.window.resolve()),
            'complete':all(s['complete'] for s in sides.values()),'sides':sides}
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({s:{'complete':r['complete'],'units':r['units'],'errors':r['errors']} for s,r in sides.items()}))
    return 0 if result['complete'] else 1


if __name__=='__main__':
    raise SystemExit(main())
