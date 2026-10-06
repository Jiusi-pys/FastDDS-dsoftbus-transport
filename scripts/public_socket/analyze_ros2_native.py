"""Same-run logical DDS sample correlation with structural SDK observations.

Does not establish SDK event epochs, cancellation, drain, or global resource HWM.
Local modeled output is explicitly ineligible for this device evidence check.
"""
import argparse
import json
from pathlib import Path
import re
from analyze_ros2 import analyze,rows


def correlate(texts,run,ledger=None,route_lifecycle_native=False):
    ledger=analyze(texts,run,['reliable','best_effort']) if ledger is None else ledger
    errors=list(ledger['errors']);frames={};sockets={};matched={};offline_matched={s:0 for s in 'AB'}
    for side in 'AB':
        frames[side]={'SDK_SEND':set(),'SDK_INGRESS':set()};matched[side]=0
        sockets[side]={'SDK_SEND':{},'SDK_INGRESS':{}}
        try:
            records=rows(texts[side],'ROS_NATIVE_DATA');summaries=rows(texts[side],'ROS_NATIVE_SUMMARY')
            if len(summaries)!=1:raise ValueError('NATIVE_SUMMARY_REQUIRED')
            summary=summaries[0]
            if summary.get('run')!=run or summary.get('side')!=side or summary.get('sdk_epoch')!='NOT_PROVEN':raise ValueError('SUMMARY_IDENTITY')
            for field in ('send_enter','send_return','send_error','ingress','detailed_stored','metadata_limit','overflow','malformed'):
                if not re.fullmatch(r'[0-9]+',summary.get(field,'')):raise ValueError('SUMMARY_COUNTER:'+field)
            if (int(summary['send_enter'])<1 or summary['send_enter']!=summary['send_return'] or
                    summary['send_error']!='0' or int(summary['ingress'])<1 or summary['overflow']!='0' or
                    summary['malformed']!='0' or summary['metadata_limit']!='8192' or
                    int(summary['detailed_stored'])!=len(records) or len(records)>8192):raise ValueError('SUMMARY_INCOMPLETE_OR_ERROR')
            for record in records:
                if (record.get('run')!=run or record.get('side')!=side or
                        record.get('scope')!='STRUCTURAL_APP_OBSERVER' or record.get('direction') not in frames[side] or
                        not re.fullmatch(r'[0-9a-f]{32}',record.get('writer','')) or
                        not re.fullmatch(r'[1-9][0-9]*',record.get('observation','')) or
                        not re.fullmatch(r'[0-9]+',record.get('socket','')) or int(record['socket'])>2147483647 or
                        not re.fullmatch(r'-?[0-9]+',record.get('sequence_hi','')) or
                        not re.fullmatch(r'[0-9]+',record.get('sequence_lo',''))):raise ValueError('NATIVE_RECORD_IDENTITY')
                high=int(record['sequence_hi']);low=int(record['sequence_lo'])
                if not -2147483648<=high<=2147483647 or not 0<=low<=4294967295:raise ValueError('NATIVE_SEQUENCE_RANGE')
                frames[side][record['direction']].add((record['writer'],high*4294967296+low))
                sockets[side][record['direction']].setdefault((record['writer'],high*4294967296+low),set()).add(int(record['socket']))
            real=rows(texts[side],'FDDST_REAL_SOCKET')
            if len(real)!=1 or real[0].get('run')!=run or real[0].get('side')!=('0' if side=='A' else '1') or real[0].get('fake_sdk')!='0':raise ValueError('REAL_BACKEND_REQUIRED')
            if rows(texts[side],'ROS_LOCAL_MODEL'):raise ValueError('LOCAL_MODEL_NOT_DEVICE_EVIDENCE')
            identity=rows(texts[side],'ROS_IDENTITY_FINAL')
            if len(identity)!=1 or identity[0].get('restored')!='1' or identity[0].get('business_rc')!='0':raise ValueError('IDENTITY_RESTORE_NOT_CONFIRMED')
        except (ValueError,KeyError,TypeError) as error:
            errors.append('native:'+side+':'+str(error))
    for side,peer in [('A','B'),('B','A')]:
        seen=set()
        try:
            for sample in rows(texts[side],'ROS_TAKE'):
                is_offline=route_lifecycle_native and sample.get('segment')=='OFFLINE'
                if not re.fullmatch(r'[1-9][0-9]*',sample.get('publication_sequence','')):raise ValueError('DDS_SEQUENCE_NOT_OBSERVED')
                gid=sample.get('gid','')
                if not re.fullmatch(r'(?:[0-9a-f]{32}|[0-9a-f]{48}|[0-9a-f]{64})',gid):raise ValueError('RMW_GID_FORMAT')
                key=(gid[:32],int(sample['publication_sequence']))
                if key in seen:raise ValueError('DUPLICATE_TOPIC_NATIVE_IDENTITY')
                seen.add(key)
                if key not in frames[peer]['SDK_SEND'] or key not in frames[side]['SDK_INGRESS']:raise ValueError('TOPIC_NATIVE_CHAIN_MISSING:'+sample.get('phase','')+':'+sample.get('ordinal',''))
                if sample.get('segment')=='POST':
                    before={s:next(x for x in rows(texts[s],'NATIVE_REACCEPT_EVENT') if x.get('stage')=='ARMED') for s in 'AB'}
                    after={s:next(x for x in rows(texts[s],'NATIVE_REACCEPT_EVENT') if x.get('stage')=='OWNER_PUBLISHED') for s in 'AB'}
                    expected_send=int(after['A']['new_id']) if peer=='A' else int(before['B']['outgoing_id'])
                    expected_receive=int(after['B']['new_id']) if side=='B' else int(before['A']['accepted_id'])
                    if route_lifecycle_native:
                        expected_send=int(after[peer]['outgoing_id']);expected_receive=int(after[side]['accepted_id'])
                    if expected_send not in sockets[peer]['SDK_SEND'].get(key,set()) or expected_receive not in sockets[side]['SDK_INGRESS'].get(key,set()):raise ValueError('POST_REPLACEMENT_SOCKET_CHAIN_MISSING')
                if is_offline:offline_matched[side]+=1
                else:matched[side]+=1
        except (ValueError,KeyError,TypeError,StopIteration) as error:errors.append('correlation:'+side+':'+str(error))
    if matched!={'A':10,'B':10}:errors.append('DIRECTIONAL_NATIVE_TOPIC_SET_INCOMPLETE')
    return dict(run=run,**{'pass':not errors},errors=errors,matched_topic_takes=matched,offline_history_takes=offline_matched,
                ros=ledger,scope='LOGICAL_DDS_SAMPLE_TO_STRUCTURAL_SDK_SEND_INGRESS_NOT_PHYSICAL_EPOCH',
                sdk_epoch='NOT_PROVEN',sdk_drain='NOT_PROVEN')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--a',type=Path,required=True);p.add_argument('--b',type=Path,required=True);p.add_argument('--a-stderr',type=Path,required=True);p.add_argument('--b-stderr',type=Path,required=True);p.add_argument('--route-lifecycle-native',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    texts={s:Path(getattr(a,s.lower())).read_text(encoding='utf-8-sig')+'\n'+Path(getattr(a,s.lower()+'_stderr')).read_text(encoding='utf-8-sig') for s in 'AB'}
    native=[x for x in rows(texts['A'],'NATIVE_REACCEPT_EVENT') if x.get('stage')=='ARMED']
    if native:
        from analyze_bounded_native_reaccept import analyze as bounded_analyze
        origin_a=any(x.get('stage')=='LOCAL_CLOSE_RETURNED' for x in rows(texts['A'],'NATIVE_REACCEPT_EVENT'))
        result=correlate(texts,a.run,bounded_analyze(texts,a.run,'outgoing-first' if origin_a or a.route_lifecycle_native else 'accepted-first',route_lifecycle='native' if a.route_lifecycle_native else False),route_lifecycle_native=a.route_lifecycle_native)
    else:result=correlate(texts,a.run)
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');raise SystemExit(0 if result['pass'] else 1)
