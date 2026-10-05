"""Independent PRE/POST ledger; no SDK control, dispatch or permission authority."""
import argparse
import json
import re
from pathlib import Path
from analyze_ros2 import rows


def analyze(texts, run, order, source='SDK_PUBLIC_SOCKET'):
    errors=[]; result={'run':run,'order':order,'source':source,'errors':errors,'pre':{},'post':{}}
    def need(condition, reason):
        if not condition: errors.append(reason)
    records={}
    try:
        for side in 'AB':
            records[side]={event:rows(texts[side],event) for event in ('ROS_MAIN','ROS_PUBLISHER','ROS_WRITE','ROS_TAKE','ROS_GRAPH','ROS_SERVICE_SENT','ROS_SERVICE_REQUEST','ROS_SERVICE_RESPONSE','ROS_CONTEXT_CLOSED','ROS_REACCEPT_ANCHOR','NATIVE_ENTITY_ANCHOR','NATIVE_ENDPOINT_ANCHOR','NATIVE_REACCEPT_EVENT','NATIVE_REACCEPT_REFUSAL')}
            r=records[side]; main=r['ROS_MAIN']; need(len(main)==1 and main[0].get('run')==run and main[0].get('side')==side,'main:'+side)
            closed=r['ROS_CONTEXT_CLOSED'];need(len(closed)==1 and closed[0].get('result')=='0' and closed[0].get('shutdown_return')=='1','close:'+side)
            need(not r['NATIVE_REACCEPT_REFUSAL'],'admission_refused:'+side)
            events=r['NATIVE_REACCEPT_EVENT']; states={e.get('stage'):e for e in events}
            need(len(states)==len(events) and len(events)<=8 and all(e.get('run')==run and e.get('side')==side and e.get('source')==source for e in events),'native_identity:'+side)
            need(all(s in states for s in ('ARMED','OLD_OWNER_SETTLED','OWNER_PUBLISHED')),'native_stage:'+side)
            if not all(s in states for s in ('ARMED','OLD_OWNER_SETTLED','OWNER_PUBLISHED')):continue
            old,new,settled=(states[s] for s in ('ARMED','OWNER_PUBLISHED','OLD_OWNER_SETTLED'))
            need(new.get('old_id')==old.get('old_id') and new.get('old_owner')==old.get('old_owner') and new.get('new_id')!=old.get('old_id') and new.get('new_owner')!=old.get('old_owner') and int(new['new_id'])>0,'new_owner:'+side)
            need(all(new.get(k)==old.get(k) for k in ('listener','listener_entry','client_entry')),'listener:'+side)
            need(settled.get('callbacks')=='0' and settled.get('receivers')=='0' and (settled.get('shutdown_returned')=='1' or settled.get('on_shutdown_seen')=='1'),'old_settlement:'+side)
            need(new.get('sdk_epoch')=='NOT_PROVEN' and new.get('sdk_drain')=='NOT_PROVEN','scope:'+side)
            need(all(int(b['seq'])>int(a['seq']) and int(b['wall_ms'])>=int(a['wall_ms']) for a,b in zip(events,events[1:])),'native_order_clock:'+side)
            entity={x.get('point'):x for x in r['NATIVE_ENTITY_ANCHOR']};need(len(entity)==2 and all(k in entity for k in ('BEFORE','AFTER')),'participant_anchor:'+side)
            if len(entity)==2:need(all(entity['BEFORE'].get(k)==entity['AFTER'].get(k) for k in ('runtime','participant','guid')),'participant_changed:'+side)
            need(all(x.get('identity_source')=='SDK_SEND_SPDP_PREFIX' and x.get('participant')=='WIRE_GUID' and re.fullmatch('[0-9a-f]{24}000001c1',x.get('guid','')) for x in entity.values()),'participant_wire_identity:'+side)
            endpoint={point:{(x['role'],x['guid'],x['topic']) for x in r['NATIVE_ENDPOINT_ANCHOR'] if x.get('point')==point} for point in ('BEFORE','AFTER')}
            need(bool(endpoint['BEFORE']) and endpoint['BEFORE']==endpoint['AFTER'],'endpoint_guids:'+side)
            need('BEFORE' in entity and all(re.fullmatch('[0-9a-f]{32}',guid) and guid[:24]==entity['BEFORE'].get('guid','')[:24] for role,guid,topic in endpoint['BEFORE']),'endpoint_participant_prefix:'+side)
            need(any(x[0]=='reader' and '/from_' in x[2] for x in endpoint['BEFORE']),'reader_wire_guid:'+side)
            prefix=f'rt/fdds_{run}/'; peer='B' if side=='A' else 'A'
            for qos in ('reliable','best_effort'):
                own=[x for x in r['ROS_PUBLISHER'] if x.get('phase')==qos]
                need(len(own)==1,'own_publisher:'+side+qos)
                if len(own)==1:need(('writer',own[0]['gid'][:32],prefix+qos+'/from_'+side) in endpoint['BEFORE'],'writer_wire_guid:'+side+qos)
                need(any(role=='reader' and topic==prefix+qos+'/from_'+peer for role,guid,topic in endpoint['BEFORE']),'expected_reader_topic:'+side+qos)
            service_prefix=f'/fdds_{run}/bounded_add'
            need(any(role==('writer' if side=='A' else 'reader') and topic=='rq'+service_prefix+'Request' for role,guid,topic in endpoint['BEFORE']),'service_request_guid:'+side)
            need(any(role==('reader' if side=='A' else 'writer') and topic=='rr'+service_prefix+'Reply' for role,guid,topic in endpoint['BEFORE']),'service_reply_guid:'+side)
            for qos in ('reliable','best_effort'):
                anchors={x.get('point'):x for x in r['ROS_REACCEPT_ANCHOR'] if x.get('phase')==qos}
                need(len(anchors)==2 and all(k in anchors for k in ('BEFORE','AFTER')),'app_anchor:'+side+qos)
                if len(anchors)==2:
                    need(all(anchors['BEFORE'].get(k)==anchors['AFTER'].get(k) for k in ('pid','node','publisher','subscriber','context','service','client','gid')),'app_changed:'+side+qos)
                    need(len(main)==1 and anchors['BEFORE'].get('pid')==main[0].get('pid'),'app_main_pid:'+side+qos)
            publication=texts[side].find('stage=OWNER_PUBLISHED ')
            for line in texts[side].splitlines():
                if line.startswith('ROS_WRITE ') and 'segment=POST ' in line:need(publication>=0 and texts[side].find(line)>publication,'post_before_publication:'+side)
        for side,peer in (('A','B'),('B','A')):
            r=records[side]
            for segment,ordinal_set in (('PRE',{1,2}),('POST',{3,4,5})):
                counts={'writes':0,'takes':0}
                for qos in ('reliable','best_effort'):
                    writers=[x for x in records[peer]['ROS_PUBLISHER'] if x.get('phase')==qos]; need(len(writers)==1,'publisher:'+peer+qos)
                    for event,who in (('ROS_WRITE',side),('ROS_TAKE',peer)):
                        values=[x for x in r[event] if x.get('phase')==qos and x.get('segment')==segment]
                        counts['writes' if event=='ROS_WRITE' else 'takes']+=len(values)
                        need(len(values)==len(ordinal_set) and {int(x['ordinal']) for x in values}==ordinal_set,'ledger:'+side+qos+segment+event)
                        need(all(x.get('payload')==f'{run}:{qos}:{who}:{x["ordinal"]}' for x in values),'payload:'+side+segment)
                        if event=='ROS_TAKE' and len(writers)==1:need(all(x.get('gid')==writers[0]['gid'] for x in values),'remote_writer:'+side+qos)
                    graphs=[x for x in r['ROS_GRAPH'] if x.get('phase')==qos and x.get('segment')==segment];need(len(graphs)==1 and graphs[0].get('visible')=='1' and graphs[0].get('subscription_count')=='1','graph:'+side+segment+qos)
                result[segment.lower()][side]=counts
        for segment,total in (('PRE','7'),('POST','11')):
            sent=[x for x in records['A']['ROS_SERVICE_SENT'] if x.get('phase')==segment];handled=[x for x in records['B']['ROS_SERVICE_REQUEST'] if x.get('phase')==segment];response=[x for x in records['A']['ROS_SERVICE_RESPONSE'] if x.get('phase')==segment]
            need(len(sent)==len(handled)==len(response)==1,'service:'+segment)
            if len(sent)==len(handled)==len(response)==1:need(sent[0].get('request_id')==handled[0].get('request_id')==response[0].get('request_id') and handled[0].get('sum')==response[0].get('sum')==total,'service_result:'+segment)
        origin='A' if order=='outgoing-first' else 'B';local=[x for x in records[origin]['NATIVE_REACCEPT_EVENT'] if x.get('stage')=='LOCAL_CLOSE_RETURNED']
        need(len(local)==1 and local[0].get('sdk_shutdown_calls')=='1' and local[0].get('sdk_shutdown_returns')=='1','actual_local_close')
    except (ValueError,KeyError,TypeError,IndexError) as error:errors.append('MALFORMED:'+str(error))
    result.update(expected_pre=8,expected_post=12,expected_total=20,**{'pass':not errors});return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--a',type=Path,required=True);parser.add_argument('--b',type=Path,required=True);parser.add_argument('--run',required=True);parser.add_argument('--order',choices=('outgoing-first','accepted-first'),required=True);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=analyze({'A':args.a.read_text(errors='replace'),'B':args.b.read_text(errors='replace')},args.run,args.order);args.output.write_text(json.dumps(result,indent=2)+'\n');raise SystemExit(0 if result['pass'] else 1)
