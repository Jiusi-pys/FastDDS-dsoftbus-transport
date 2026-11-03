"""Independent PRE/POST ledger; no SDK control, dispatch or permission authority."""
import argparse
import json
import re
from pathlib import Path
from analyze_ros2 import rows


def analyze(texts, run, order, source='SDK_PUBLIC_SOCKET', route_lifecycle=False):
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
        if not route_lifecycle:
            need(len(local)==1 and local[0].get('sdk_shutdown_calls')=='1' and local[0].get('sdk_shutdown_returns')=='1','actual_local_close')
        elif route_lifecycle=='native':
            result['route']=analyze_native_route(texts,run)
            errors.extend(result['route']['errors'])
        else:
            for side in 'AB':
                events=rows(texts[side],'HOST_NODE_EVENT');queries=rows(texts[side],'HOST_NODE_QUERY')
                need([x.get('state') for x in events]==['OFFLINE','ONLINE'] and all(x.get('run')==run and x.get('side')==side and x.get('source')=='HOST_PUBLIC_NODE_API_MODEL' for x in events),'actual_node_boundary:'+side)
                need(len(queries)==1 and queries[0].get('count')=='1' and queries[0].get('online')=='1','actual_directory_query:'+side)
                identities=rows(texts[side],'HOST_NODE_IDENTITY');sockets=rows(texts[side],'HOST_SOCKET_ROUTE')
                need(len(identities)==1 and identities[0].get('key')=='UDID' and identities[0].get('result')=='0','actual_identity_query:'+side)
                need(len(sockets)==2 and sockets[0].get('queries')=='0' and sockets[1].get('queries')=='1','actual_socket_route:'+side)
                if len(identities)==1 and len(sockets)==2:
                    need(sockets[1].get('network')==identities[0].get('network'),'resolved_socket_destination:'+side)
                    if route_lifecycle in ('new','same'):
                        need((sockets[1].get('network')!=sockets[0].get('network'))==(route_lifecycle=='new'),'selected_route_mode:'+side)
                need(all(x.get('run')==run and x.get('side')==side and x.get('source')=='HOST_PUBLIC_NODE_API_MODEL' for x in identities+sockets),'route_boundary_identity:'+side)
    except (ValueError,KeyError,TypeError,IndexError) as error:errors.append('MALFORMED:'+str(error))
    result.update(expected_pre=8,expected_post=12,expected_total=20,**{'pass':not errors});return result


def analyze_native_route(texts,run):
    """SDK boundary evidence and rejected new work, separate from DDS write RC."""
    errors=[];result={'source':'SDK_PUBLIC_NODE','offline_writes':{},'offline_takes':{},'network_id_changed':{},'errors':errors}
    def need(value,reason):
        if not value:errors.append(reason)
    try:
        for side in 'AB':
            raw_sdk=rows(texts[side],'ROS_STARTUP_EVENT');by_observation={}
            for item in raw_sdk:
                key=item.get('observation_seq');old=by_observation.get(key)
                # The bounded control exporter and final diagnostic flush may
                # expose the same immutable capture. Contradictions are errors.
                if old is not None:
                    need(all(old.get(k)==item.get(k) for k in ('operation','entry','value','value_type','detail_value','source','run','side','sdk_return_observed','callback_observed','local_result_observed')),'route_conflicting_sdk_capture:'+side)
                else:by_observation[key]=item
            sdk=list(by_observation.values())
            need(all(x.get('source')=='REAL_TARGET' for x in sdk),'route_non_target_sdk:'+side)
            def successful(operation,kind):
                flag={'SDK_RC':'sdk_return_observed','CALLBACK_STATUS':'callback_observed','LOCAL_STATUS':'local_result_observed'}[kind]
                return [x for x in sdk if x.get('operation')==operation and x.get('value_type')==kind and x.get('value')=='0' and x.get(flag)=='1' and x.get('run')==run and x.get('side')==side]
            need(bool(successful('RegNode_return','SDK_RC')),'route_registration:'+side)
            offline=successful('OnNodeOffline_enter','CALLBACK_STATUS');online=successful('OnNodeOnline_enter','CALLBACK_STATUS')
            processed_off=successful('NodeOffline_processed','LOCAL_STATUS');processed_on=successful('NodeOnline_processed','LOCAL_STATUS')
            need(bool(offline) and bool(online) and bool(processed_off) and bool(processed_on),'route_platform_callback_chain:'+side)
            queries=successful('GetAllNodes_return','SDK_RC');identities=successful('GetNodeUdid_return','SDK_RC')
            need(bool(queries) and bool(identities),'route_public_resolver:'+side)
            if offline and online and processed_off and processed_on and queries and identities:
                seq=lambda x:int(x['observation_seq'])
                need(min(map(seq,offline))<min(map(seq,processed_off))<min(map(seq,online))<=min(map(seq,processed_on))<max(map(seq,queries))<max(map(seq,identities)),'route_sdk_causality:'+side)
            events=rows(texts[side],'NATIVE_ROUTE_EVENT')
            need([x.get('stage') for x in events]==['OFFLINE','RESOLVED'] and all(x.get('run')==run and x.get('side')==side and x.get('source')=='SDK_PUBLIC_NODE' and x.get('sdk_epoch')=='NOT_PROVEN' for x in events),'route_product_events:'+side)
            if len(events)!=2:continue
            off,on=events
            need(off.get('available')=='0' and on.get('available')=='1' and int(on['revision'])>int(off['revision']),'route_publication:'+side)
            matched_identities=[x for x in sdk if x.get('operation')=='NodeIdentity_match' and x.get('value_type')=='LOCAL_STATUS' and x.get('local_result_observed')=='1' and x.get('value')=='1']
            bindings=[x for x in sdk if x.get('operation')=='NodeSocket_route_binding' and x.get('value_type')=='LOCAL_STATUS' and x.get('local_result_observed')=='1' and x.get('detail_value')==on['revision'] and x.get('value') in ('0','1')]
            owners=[x for x in rows(texts[side],'NATIVE_REACCEPT_EVENT') if x.get('stage')=='OWNER_PUBLISHED']
            need(bool(matched_identities) and len(bindings)==1 and len(owners)==1,'route_resolved_sdk_socket_binding:'+side)
            if matched_identities and len(bindings)==1 and len(owners)==1:
                binding=bindings[0];owner=owners[0];entry=binding.get('entry')
                calls=[x for x in sdk if x.get('operation')=='Socket_enter' and x.get('sdk_entered')=='1' and x.get('entry')==entry and int(x['observation_seq'])>int(binding['observation_seq'])]
                returned=[x for x in sdk if x.get('operation')=='Socket_return' and x.get('value_type')=='SOCKET_HANDLE' and x.get('sdk_return_observed')=='1' and x.get('entry')==entry and x.get('value')==owner.get('outgoing_id')]
                need(entry==owner.get('client_entry') and bool(calls) and bool(returned) and max(int(x['observation_seq']) for x in matched_identities)<int(binding['observation_seq'])<min(int(x['observation_seq']) for x in calls)<max(int(x['observation_seq']) for x in returned),'route_real_replacement_socket:'+side)
                result['network_id_changed'][side]=binding['value']=='1'
            submissions=rows(texts[side],'NATIVE_SENDER_SUBMISSION')
            rejected=[x for x in submissions if x.get('run')==run and x.get('side')==side and x.get('segment')=='OFFLINE' and x.get('route_known')=='1' and x.get('route_sample')=='ADMISSION_GATE' and x.get('available')=='0' and x.get('route_revision')==off['revision'] and x.get('status')=='7' and x.get('queue_admitted')=='0' and x.get('native_work')=='NOT_ENTERED_BY_SUBMISSION' and int(x.get('submission_calls','0'))>0 and x.get('source')=='SDK_PUBLIC_NODE']
            need(bool(rejected) and all(re.fullmatch('[0-9a-f]{32}',x.get('writer','')) and x.get('sender') and int(x['sequence_lo'])>0 for x in rejected),'route_actual_sender_refusal:'+side)
            publishers=rows(texts[side],'ROS_PUBLISHER')
            for qos in ('reliable','best_effort'):
                pubs=[x for x in publishers if x.get('phase')==qos]
                need(len(pubs)==1,'route_offline_writer:'+side+qos)
                if len(pubs)!=1:continue
                writer=pubs[0].get('gid','')[:32]
                pre=[x for x in submissions if x.get('segment')=='PRE' and x.get('writer')==writer and x.get('status')=='0' and x.get('queue_admitted')=='1' and x.get('run')==run and x.get('side')==side and x.get('source')=='SDK_PUBLIC_NODE']
                sequences={int(x['sequence_hi'])*4294967296+int(x['sequence_lo']) for x in pre}
                original_senders={x.get('sender') for x in pre}
                # Exactly the two frozen PRE writes precede OFF1001 on the same
                # original Writer. Retransmission repeats its old sequence;
                # it cannot satisfy this next-new-write correlation.
                need(sequences=={1,2},'route_pre_sequence_provenance:'+side+qos)
                expected=max(sequences)+1 if sequences else -1
                need(expected==3 and any(x.get('writer')==writer and x.get('sender') in original_senders and int(x['sequence_hi'])*4294967296+int(x['sequence_lo'])==expected for x in rejected),'route_exact_offline_topic_refusal:'+side+qos)
            writes=[x for x in rows(texts[side],'ROS_WRITE') if x.get('segment')=='OFFLINE']
            takes=[x for x in rows(texts[side],'ROS_TAKE') if x.get('segment')=='OFFLINE']
            result['offline_writes'][side]=len(writes);result['offline_takes'][side]=len(takes)
            need(len(writes)==2 and {x.get('phase') for x in writes}=={'reliable','best_effort'} and all(x.get('ordinal')=='1001' and x.get('payload')==f'{run}:{x["phase"]}:{side}:1001' for x in writes),'route_offline_write_ledger:'+side)
            peer='B' if side=='A' else 'A'
            need(len(takes)<=2 and len({x.get('phase') for x in takes})==len(takes) and all(x.get('ordinal')=='1001' and x.get('phase') in ('reliable','best_effort') and x.get('payload')==f'{run}:{x["phase"]}:{peer}:1001' for x in takes),'route_offline_history_ledger:'+side)
    except (ValueError,KeyError,TypeError,IndexError) as error:errors.append('ROUTE_MALFORMED:'+str(error))
    result['pass']=not errors;return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--a',type=Path,required=True);parser.add_argument('--b',type=Path,required=True);parser.add_argument('--run',required=True);parser.add_argument('--order',choices=('outgoing-first','accepted-first'),required=True);parser.add_argument('--route-lifecycle-native',action='store_true');parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=analyze({'A':args.a.read_text(errors='replace'),'B':args.b.read_text(errors='replace')},args.run,args.order,route_lifecycle='native' if args.route_lifecycle_native else False);args.output.write_text(json.dumps(result,indent=2)+'\n');raise SystemExit(0 if result['pass'] else 1)
