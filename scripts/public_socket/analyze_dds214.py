"""Version-bound direct DDS ledger; Host/local-model/device evidence stay separate."""
import re,json,argparse
from pathlib import Path
def records(text, prefix):
    result=[]
    for line in text.splitlines():
        if not line.startswith(prefix+' '):continue
        row={}
        for token in line.split()[1:]:
            if '=' not in token:raise ValueError('Malformed '+prefix)
            key,value=token.split('=',1)
            if key in row:raise ValueError('Duplicate '+prefix)
            row[key]=value
        result.append(row)
    return result

def analyze(texts,run,mode,scope="host"):
    errors=[];boards={};parsed={}
    events=('MAIN','ID','MATCH','DELETE_REFUSAL','WRITE','TAKE','WAIT','DATA_DONE','DELETE_ENTITIES','DELETE_PARTICIPANT_ENTER','DELETE_PARTICIPANT_RETURN','EXIT')
    for side in 'AB':
        try:parsed[side]={e:records(texts[side],'DDS214_'+e) for e in events}
        except ValueError as error:errors.append(side+':'+str(error));parsed[side]={e:[] for e in events}
        rows=parsed[side]
        for event in events:
            if any(r.get('run')!=run or r.get('side')!=side for r in rows[event]):errors.append('identity:'+side+':'+event)
        main=rows['MAIN'];identity=rows['ID'];match=rows['MATCH']
        if len(main)!=1 or main[0].get('scenario')!='DDS214_LAYER_BASELINE' or main[0].get('mode')!=mode or not re.fullmatch('[1-9][0-9]*',main[0].get('pid','')):errors.append('main:'+side)
        if len(identity)!=1 or not all(re.fullmatch('[0-9a-f]{32}',identity[0].get(k,'')) for k in ('participant','writer','reader')):errors.append('DDS_entities:'+side)
        elif identity[0].get('writer_topic')!='dds214_'+run+'_from_'+side or identity[0].get('reader_topic')!='dds214_'+run+'_from_'+('B' if side=='A' else 'A'):errors.append('directional_topics:'+side)
        if len(match)!=1 or match[0].get('writers')!='1' or match[0].get('readers')!='1':errors.append('match:'+side)
        boards[side]={'writes':len(rows['WRITE']),'takes':len(rows['TAKE']),'pid':main[0].get('pid') if len(main)==1 else None}
        for event in ('WRITE','TAKE'):
            samples=rows[event]
            if len(samples)!=5 or {r.get('ordinal') for r in samples}!=set('12345'):errors.append('samples:'+side+':'+event)
            if any(not re.fullmatch('[1-9][0-9]*',r.get('sn','')) for r in samples):errors.append('DDS_SN:'+side+':'+event)
            else:
                sequences=[int(r['sn']) for r in samples]
                if len(set(sequences))!=len(sequences):errors.append('DDS_SN_reused:'+side+':'+event)
                if event=='WRITE' and any(b<=a for a,b in zip(sequences,sequences[1:])):errors.append('DDS_SN_write_order:'+side)
        for event in ('WAIT','DELETE_ENTITIES','DELETE_PARTICIPANT_RETURN'):
            if len(rows[event])!=1 or rows[event][0].get('rc')!='0':errors.append('public_return:'+side+':'+event)
        if len(rows['DELETE_PARTICIPANT_ENTER'])!=1 or len(rows['EXIT'])!=1 or rows['EXIT'][0].get('result')!='0':errors.append('exit:'+side)
        if len(rows['DATA_DONE'])!=1 or rows['DATA_DONE'][0].get('writes')!='5' or rows['DATA_DONE'][0].get('takes')!='5':errors.append('data_done:'+side)
        if mode=='delete-refusal':
            refused=rows['DELETE_REFUSAL']
            if len(refused)!=1 or refused[0].get('rc')!='4' or refused[0].get('active')!='1' or refused[0].get('active_source')!='PUBLIC_OWNED_ENDPOINT_LISTS' or refused[0].get('writers')!='1' or refused[0].get('readers')!='1' or not identity or any(refused[0].get(k)!=identity[0].get(k) for k in ('participant','writer','reader')):errors.append('refusal:'+side)
            first_write=texts[side].find('DDS214_WRITE ')
            if first_write<texts[side].find('DDS214_DELETE_REFUSAL '):errors.append('progress_after_refusal:'+side)
        elif rows['DELETE_REFUSAL']:errors.append('unexpected_refusal:'+side)
        close='FDDST_ROS_CLOSED run='+run
        if texts[side].count(close)!=1 or texts[side].find(close)<texts[side].find('DDS214_DELETE_PARTICIPANT_ENTER '):errors.append('owner_close_order:'+side)
        if scope=='host':
            if not re.search(r'FDDST_HOST_IO run='+run+r' side=\d sends=[1-9][0-9]* ingress=[1-9][0-9]* send_errors=0 fake_sdk=1',texts[side]):errors.append('opaque_transport_path:'+side)
            if 'builtin=0 intraprocess=0 mode=HOST_OPAQUE_BYTES_TEST_ONLY' not in texts[side]:errors.append('transport_selection:'+side)
        elif scope=='device':
            if 'builtin=0 intraprocess=0 mode=OHOS_PUBLIC_SOCKET_EXPERIMENTAL' not in texts[side]:errors.append('transport_selection:'+side)
            if 'DDS214_LOCAL_MODEL' in texts[side] or 'HOST_OPAQUE_BYTES_TEST_ONLY' in texts[side]:errors.append('not_device:'+side)
        elif scope=='local_model':
            if 'DDS214_LOCAL_MODEL' not in texts[side]:errors.append('local_model_marker:'+side)
        else:errors.append('unknown_scope:'+side)
        if 'FDDST_FACTORY_DESTRUCTOR_INCOMPLETE_NO_NORMAL_CLOSE' in texts[side] or 'DDS214_FAILURE ' in texts[side]:errors.append('failure:'+side)
    for side,other in [('A','B'),('B','A')]:
        own=parsed[side]['ID'];peer=parsed[other]['ID']
        if len(own)!=1 or len(peer)!=1:continue
        if own[0]['participant']==peer[0]['participant'] or own[0]['writer']==peer[0]['writer']:errors.append('distinct_entities:'+side)
        writes={r.get('ordinal'):r for r in parsed[other]['WRITE']}
        for write in parsed[side]['WRITE']:
            if write.get('ok')!='1' or write.get('writer')!=own[0]['writer']:errors.append('write_identity:'+side)
        for take in parsed[side]['TAKE']:
            source=writes.get(take.get('ordinal'),{})
            if take.get('writer')!=peer[0]['writer'] or take.get('reader')!=own[0]['reader'] or take.get('payload_ok')!='1' or take.get('sn')!=source.get('sn'):errors.append('peer_sample:'+side)
    return {'run':run,'mode':mode,'boards':boards,'observed_writes':sum(b['writes'] for b in boards.values()),'observed_takes':sum(b['takes'] for b in boards.values()),'errors':errors,'pass':not errors}

def native_correlation(texts,run):
    """Reuse the target's bounded structural observation format, not physical epochs."""
    errors=[];frames={s:{d:set() for d in ('SDK_SEND','SDK_INGRESS')} for s in 'AB'};matched={s:0 for s in 'AB'}
    for s in 'AB':
        try:
            if 'DDS214_LOCAL_MODEL' in texts[s] or 'HOST_OPAQUE_BYTES_TEST_ONLY' in texts[s]:raise ValueError('LOCAL_MODEL_NOT_DEVICE_EVIDENCE')
            summaries=records(texts[s],'ROS_NATIVE_SUMMARY');data=records(texts[s],'ROS_NATIVE_DATA')
            if len(summaries)!=1:raise ValueError('NATIVE_SUMMARY_REQUIRED')
            r=summaries[0]
            if r.get('run')!=run or r.get('side')!=s or r.get('sdk_epoch')!='NOT_PROVEN':raise ValueError('NATIVE_SUMMARY_IDENTITY')
            for k in ('send_enter','send_return','send_error','ingress','detailed_stored','metadata_limit','overflow','malformed'):
                if not re.fullmatch('[0-9]+',r.get(k,'')):raise ValueError('COUNTER:'+k)
            if int(r['send_enter'])<1 or r['send_enter']!=r['send_return'] or r['send_error']!='0' or int(r['ingress'])<1 or r['overflow']!='0' or r['malformed']!='0' or r['metadata_limit']!='8192' or int(r['detailed_stored'])!=len(data) or len(data)>8192:raise ValueError('NATIVE_INCOMPLETE')
            for r in data:
                if r.get('run')!=run or r.get('side')!=s or r.get('scope')!='STRUCTURAL_APP_OBSERVER' or r.get('direction') not in frames[s] or not re.fullmatch('[0-9a-f]{32}',r.get('writer','')) or not re.fullmatch('[1-9][0-9]*',r.get('observation','')) or not re.fullmatch('[0-9]+',r.get('socket','')) or int(r['socket'])>2147483647 or not re.fullmatch('-?[0-9]+',r.get('sequence_hi','')) or not re.fullmatch('[0-9]+',r.get('sequence_lo','')):raise ValueError('NATIVE_RECORD_IDENTITY')
                hi,lo=int(r['sequence_hi']),int(r['sequence_lo'])
                if not -2147483648<=hi<=2147483647 or not 0<=lo<=4294967295:raise ValueError('NATIVE_SN_RANGE')
                frames[s][r['direction']].add((r['writer'],hi*4294967296+lo))
            real=records(texts[s],'FDDST_REAL_SOCKET');identity=records(texts[s],'ROS_IDENTITY_FINAL');components=records(texts[s],'ROS_DDS_DELETED')
            if len(real)!=1 or real[0].get('run')!=run or real[0].get('side')!=('0' if s=='A' else '1') or real[0].get('fake_sdk')!='0':raise ValueError('REAL_BACKEND_REQUIRED')
            if len(identity)!=1 or identity[0].get('restored')!='1' or identity[0].get('business_rc')!='0':raise ValueError('IDENTITY_RESTORE_REQUIRED')
            if len(components)!=1 or components[0].get('run')!=run or components[0].get('side')!=s or components[0].get('dds_component_result')!='0' or components[0].get('api_return')!='NOT_OBSERVED':raise ValueError('COMPONENT_CLOSE_REQUIRED')
        except (ValueError,KeyError,TypeError) as e:errors.append('native:'+s+':'+str(e))
    for s,peer in [('A','B'),('B','A')]:
        try:
            for r in records(texts[s],'DDS214_TAKE'):
                key=(r['writer'],int(r['sn']))
                if key not in frames[peer]['SDK_SEND'] or key not in frames[s]['SDK_INGRESS']:raise ValueError('SAMPLE_NATIVE_CHAIN_MISSING:'+r.get('ordinal',''))
                matched[s]+=1
        except (ValueError,KeyError,TypeError) as e:errors.append('correlation:'+s+':'+str(e))
    if matched!={'A':5,'B':5}:errors.append('NATIVE_DIRECTIONAL_SET_INCOMPLETE')
    return dict(pass_=not errors,errors=errors,matched=matched,scope='LOGICAL_DDS_SAMPLE_TO_STRUCTURAL_SDK_NOT_PHYSICAL_EPOCH',sdk_drain='NOT_PROVEN',sdk_epoch='NOT_PROVEN')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--run',required=True);p.add_argument('--scope',choices=['host','device','local_model'],required=True)
    for k in ('a','b','a-stderr','b-stderr','output'):p.add_argument('--'+k,type=Path,required=True)
    a=p.parse_args();texts={s:getattr(a,s.lower()).read_text(encoding='utf-8-sig')+'\n'+getattr(a,s.lower()+'_stderr').read_text(encoding='utf-8-sig') for s in 'AB'}
    result=analyze(texts,a.run,'normal',a.scope)
    result['evidence_scope']=a.scope
    if a.scope=='device':
        result['native']=native_correlation(texts,a.run);result['pass']=result['pass'] and result['native']['pass_']
    else:result['native']={'status':'NOT_RUN_LOCAL_MODEL' if a.scope=='local_model' else 'NOT_RUN_HOST','device_evidence':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');raise SystemExit(0 if result['pass'] else 1)
