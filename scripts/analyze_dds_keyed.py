"""Version-bound keyed DDS evidence oracle; host reference is never native proof.

Read/take operation returns are distinct from cache identities and native messages.
All ordering uses one process's log order or metadata-only release receipts.
This tool neither parses invalid-data payloads nor infers SDK-internal settlement.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

from analyze_dds_matrix import integer, parse_text as parse_common, PHASES, RX_EVENTS


def parse_text(text, side):
    # Reuse the strict existing token parser while retaining the actual prefix.
    translated = '\n'.join('MATRIX '+line[6:] if line.startswith('KEYED ') else line for line in text.splitlines())
    rows = parse_common(translated, side)
    lines = text.splitlines()
    for row in rows:
        if lines[row['_line']-1].startswith('KEYED '):row['_kind']='KEYED'
    return rows


def guid(run, endpoint, participant=False):
    side = 'B' if endpoint=='R1' else 'A'
    prefix=bytearray.fromhex(run);prefix[0]^=0xA1 if side=='A' else 0xB2
    prefix.extend((0x44,0x55,0x41,1))
    entity = bytes((0,0,1,0xC1)) if participant else bytes((0,0,11,7)) if side=='B' else bytes((0,0,int(endpoint[1:]),2))
    return (prefix+entity).hex()


def payload(run,key,writer,ordinal):
    run_value=int(run,16)
    data=bytes((key*3+writer*17+ordinal*7+((run_value>>((i%8)*8))&255)+i)&255 for i in range(32))
    digest=14695981039346656037
    for byte in data:digest=((digest^byte)*1099511628211)&((1<<64)-1)
    return data.hex(),f'{digest:016x}'


def business(row):
    return integer(row,'writer_id'),integer(row,'key'),integer(row,'ordinal')


def identity(row):
    return row.get('writer_guid'),integer(row,'sn_hi'),integer(row,'sn_lo')


def expected_writes(case):
    """Mechanical per-case phase projection of the canonical small scenarios."""
    out={}
    def add(phase,writers,keys,ordinals):
        for w in writers:
            for k in keys:
                for o in ordinals:out[w,k,o]=phase
    if case=='K2':add('PRE',[1,2],[17],range(1,4))
    elif case in ('K3','K4_TRUE','K4_FALSE'):
        add('PRE',[1],[17,34],[1]);add('POST',[1],[34],[2]);add('OBSERVE',[1],[17],[2])
    elif case=='H1':add('PRE',[1],[17,34],range(1,5));add('POST',[1],[17,34],[5])
    elif case=='H2':
        for o,p in enumerate(('PRE','CHANGE','POST','OBSERVE'),1):add(p,[1],[17,34],[o])
    elif case=='R1':add('POST',[1],[17,34],[1])
    elif case in ('R2','R3'):
        keys=[17,34,51] if case=='R3' else [17,34]
        add('PRE',[1],keys,[1]);add('POST',[1],keys,[2])
    else:add('PRE',[1],[17,34],range(1,5))
    return out


def _native(config,rows,own,unique,states,done,created):
    errors,joins,state_joins=[],[],[]
    def need(ok,reason):
        if not ok:errors.append(reason)
        return ok
    if config.get('mode')!='BYTES_NATIVE':
        return {'pass':False,'errors':['host_reference_is_not_native'],'sample_joins':[],'state_joins':[],'status':'UNVERIFIED_NATIVE_PROOF'}
    identities={};qoses={};native={s:[r for r in rows[s] if r.get('_kind')=='EXPERIMENT_UNQUALIFIED'] for s in 'AB'}
    for side in 'AB':
        found=[r for r in own[side] if r.get('event')=='NATIVE_IDENTITY']
        if need(len(found)==1,'native_identity:'+side):
            identities[side]=found[0]
            need(found[0].get('participant_guid')==guid(config['run'],'W1' if side=='A' else 'R1',True),'native_participant_identity:'+side)
            for key in ('runtime','native_instance','application','receiver','token'):
                need(integer(found[0],key)>0,'native_identity_field:'+side+':'+key)
        pq=[r for r in own[side] if r.get('event')=='PARTICIPANT_QOS']
        if need(len(pq)==1,'native_participant_qos:'+side):
            qoses[side]=pq[0]
            for k,v in {'domain':73,'use_builtin':0,'native_descriptor_count':1,'udp_descriptor_count':0,'intraprocess':0,'multicast_count':0}.items():
                need(integer(pq[0],k)==v,'native_only:'+side+':'+k)
            need(pq[0].get('default_locators')==pq[0].get('metatraffic_locators'),'native_local_locator:'+side)
        finals=[r for r in rows[side] if r.get('stage') in ('KEYED_NATIVE_FINALIZED','MATRIX_NATIVE_FINALIZED')]
        need(len(finals)==1 and all(integer(finals[0],k)==1 for k in ('clean','dds_clean','runtime_clean','process_budget_settled')),'native_finalization:'+side)
        if side in identities:
            traffic=[r for r in native[side] if r.get('instance_id')==identities[side]['native_instance'] and r.get('rc')=='0']
            for event,kind in (('SendBytes_return','spdp'),('FastDDS_receiver_return','spdp'),('SendBytes_return','sedp_pub' if side=='A' else 'sedp_sub'),('FastDDS_receiver_return','sedp_sub' if side=='A' else 'sedp_pub')):
                need(any(r.get('event')==event and integer(r,kind)>0 for r in traffic),'native_discovery:'+side+':'+event+':'+kind)
    if len(qoses)==2:
        for side,other in (('A','B'),('B','A')):need(qoses[side].get('initial_peer_locators')==qoses[other].get('default_locators'),'native_peer_locator:'+side)
    def frame(side,event,inst,inv,record,length,digest):
        return any(r.get('_kind')=='EXPERIMENT_DATA' and r.get('event')==event and r.get('instance_id')==inst and r.get('invocation')==inv and r.get('writer_guid')==record.get('writer_guid') and integer(r,'sequence_hi')==integer(record,'sn_hi') and integer(r,'sequence_lo')==integer(record,'sn_lo') and integer(r,'native_len')==length and r.get('native_hash')==digest for r in rows[side])
    for sample,is_state in [(r,False) for r in unique.values()]+[(r,True) for r in states]:
        if is_state and integer(sample,'identity_known')!=1:
            state_joins.append({'identity_known':False,'instance_handle':sample.get('instance_handle'),'status':'STATE_NATIVE_ORIGINAL_IDENTITY_UNOBSERVED'});continue
        if not all(s in identities for s in 'AB'):continue
        ri,wi=identities['B'],identities['A'];candidates=[]
        historical=config.get('case_id')=='H1' and integer(sample,'ordinal') in (3,4)
        for enter in native['B']:
            if not(enter.get('event')=='FastDDS_receiver_enter' and enter.get('instance_id')==ri['native_instance'] and enter.get('receiver_lease_id')==ri['receiver'] and enter.get('rc')=='0' and integer(enter,'_line')<integer(sample,'_line')):continue
            inv=enter.get('invocation');length=integer(enter,'len');digest=enter.get('hash')
            chain={e:[r for r in native['B'] if r.get('event')==e and r.get('instance_id')==ri['native_instance'] and r.get('invocation')==inv] for e in RX_EVENTS}
            if not all(len(v)==1 for v in chain.values()):continue
            copied,_,returned=(chain[e][0] for e in RX_EVENTS)
            fields=('socket','owner','len','hash','local_kind','local_key','local_port','peer_kind','peer_key','peer_port','receiver_lease_id')
            if not all(all(r.get(k)==enter.get(k) for k in fields) for r in (copied,returned)):continue
            if not(integer(copied,'_line')<integer(enter,'_line')<integer(returned,'_line')):continue
            if historical and integer(enter,'_line')<=integer(created.get('R1',{}),'_line'):continue
            if ':'.join(enter.get('local_'+k,'') for k in ('kind','key','port'))!=qoses.get('B',{}).get('default_locators'):continue
            if not all(frame('B',e,ri['native_instance'],inv,sample,length,digest) for e in RX_EVENTS):continue
            app=[r for r in rows['B'] if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('instance_id')==ri['native_instance'] and r.get('invocation')==inv]
            if not all(sum(r.get('stage')==str(s) for r in app)==1 for s in (0,1,3,4,5)):continue
            if not all(r.get('runtime_id')==ri['runtime'] and r.get('application_incarnation')==ri['application'] and r.get('receiver_incarnation')==ri['receiver'] and r.get('logical_token')==ri['token'] and integer(r,'bytes')==length for r in app):continue
            stage={int(r['stage']):integer(r,'_line') for r in app if r.get('stage') in ('0','1','3','4','5')}
            # ProcessReceivePath::Admit invokes Captured then OwnedCopy before
            # CommitReceive. RuntimeObserver prints OwnedCopy before LogSpan.
            # TryEnter/Invoke and the execution destructor bracket delivery.
            # Queued is printed AFTER publication and may lag the worker;
            # TX_ACCEPTED is also a diagnostic, not a linearization marker.
            if not(stage[0]<stage[1]<integer(copied,'_line')<stage[3]<stage[4]<integer(enter,'_line')<integer(returned,'_line')<stage[5]):continue
            txs=[]
            for tx in native['A']:
                if not(tx.get('event')=='SendBytes_return' and tx.get('rc')=='0' and tx.get('instance_id')==wi['native_instance'] and integer(tx,'len')==length and tx.get('hash')==digest):continue
                if not all(tx.get('peer_'+k)==enter.get('local_'+k) and tx.get('local_'+k)==enter.get('peer_'+k) for k in ('kind','key','port')):continue
                if not frame('A','SendBytes_return',wi['native_instance'],tx.get('invocation'),sample,length,digest):continue
                upper=[r for r in rows['A'] if r.get('_kind')=='EXPERIMENT_APPLICATION_TX' and r.get('instance_id')==wi['native_instance'] and r.get('invocation')==tx.get('invocation') and r.get('stage')=='TX_ACCEPTED' and r.get('application_incarnation')==wi['application'] and r.get('logical_token')==wi['token']]
                if len(upper)!=1:continue
                if historical and (integer(tx,'_line')<=integer(done['A','PRE'],'_line') or integer(upper[0],'_line')<=integer(done['A','PRE'],'_line')):continue
                txs.append(tx.get('invocation'))
            if txs:candidates.append({'rx_invocation':inv,'native_len':length,'native_hash':digest,'tx_invocations':txs,'receiver':ri['receiver'],'application':ri['application']})
        label=':'.join(map(str,identity(sample)))
        if need(bool(candidates),'native_record_chain:'+label):
            record={'reader':'R1','writer_guid':sample['writer_guid'],'sn_hi':integer(sample,'sn_hi'),'sn_lo':integer(sample,'sn_lo'),'key':integer(sample,'known_key' if is_state else 'key'),'ordinal':None if is_state else integer(sample,'ordinal'),'instance_handle':sample.get('instance_handle'),'candidates':candidates,'scope':'GUID_SN_FRAME_CAPTURED_LEASE_ASSOCIATION_NOT_EXCLUSIVE_INTERNAL_CAUSE'}
            (state_joins if is_state else joins).append(record)
    return {'pass':not errors,'errors':errors,'sample_joins':joins,'state_joins':state_joins,'status':'OBSERVED_NATIVE_CHAIN' if not errors else 'UNVERIFIED_NATIVE_PROOF','sdk_internal_settlement':'UNOBSERVED'}


def analyze_records(config,records_by_side,control_events,matrix_document,*,matrix_sha256,require_native=True,processes=None,consumed=None):
    errors=[]
    def need(ok,reason):
        if not ok:errors.append(reason)
        return ok
    run=config.get('run','');case=config.get('case_id',config.get('case'))
    spec=next((s for s in matrix_document.get('cases',[]) if s.get('case_id')==case),None)
    if not need(bool(re.fullmatch('[0-9a-f]{16}',run)),'run_identity') or not need(spec is not None,'unknown_case'):
        return {'pass':False,'errors':errors,'native':{'errors':['core_failed']},'core_pass':False,'native_proof_pass':False,'status':'FAIL'}
    need(config.get('matrix_sha256',matrix_sha256)==matrix_sha256,'matrix_identity')
    mode=config.get('mode');need(mode in ('HOST_REFERENCE_UDP','BYTES_NATIVE'),'unknown_mode')
    need(case!='R1' or mode=='HOST_REFERENCE_UDP','R1_host_only_not_device_pass')
    rows={s:list(records_by_side.get(s,[])) for s in 'AB'}
    own={s:[r for r in rows[s] if r.get('_kind') in ('KEYED','MATRIX')] for s in 'AB'}
    done,gates={},{}
    def phase_contains(side,phase,row):
        lo=0 if phase=='INITIAL' else integer(gates.get((side,phase),{}),'order')
        return lo<integer(row,'order')<integer(done.get((side,phase),{}),'order')
    for side in 'AB':
        stream=own[side];need(bool(stream),'missing_records:'+side);pid=None;previous=0;line=0
        for row in rows[side]:
            need(integer(row,'_line')>line,'raw_order:'+side);line=integer(row,'_line')
            if 'run' in row:need(row['run']==run,'mixed_run:'+side)
        for row in stream:
            need(row.get('run')==run and row.get('case')==case and row.get('side')==side,'record_identity:'+side)
            p=integer(row,'pid');need(p>0 and (pid is None or pid==p),'pid_continuity:'+side);pid=p
            n=integer(row,'order');need(n>previous,'event_order:'+side);previous=n
            need(row.get('event') not in ('FIRST_FAILURE','CLEANUP_FAILURE'),'reported_failure:'+side)
            need('UPSTREAM_NOT_IMPLEMENTED' not in row.values(),'unsupported_is_not_pass:'+side)
            for k in ('overflow','diagnostic_overflow','dropped_diagnostics'):
                if k in row:need(integer(row,k)==0,'incomplete_diagnostics:'+side)
        phases=[r for r in stream if r.get('event')=='PHASE_DONE'];releases=[r for r in stream if r.get('event')=='CONTROL_CONSUMED']
        need([r.get('phase') for r in phases]==list(PHASES),'phase_sequence:'+side)
        need([r.get('phase') for r in releases]==list(PHASES[1:]),'release_sequence:'+side)
        done.update({(side,r['phase']):r for r in phases});gates.update({(side,r['phase']):r for r in releases})
        for i,p in enumerate(PHASES[1:],1):need(integer(done.get((side,PHASES[i-1]),{}),'order')<integer(gates.get((side,p),{}),'order')<integer(done.get((side,p),{}),'order'),'phase_barrier:'+side+':'+p)
        proc=(processes or {}).get(side,{})
        need(proc.get('exit_code')==0 and not proc.get('signal') and not proc.get('error') and integer(proc,'pid')==pid,'process_receipt:'+side)
        for phase in PHASES[1:]:need((consumed or {}).get(side,{}).get(phase)==f'MATRIX_RELEASE {run} {case} {phase}\n','consumed_receipt:'+side+':'+phase)
        scope=[r for r in stream if r.get('event')=='SCOPE']
        need(len(scope)==1 and scope[0].get('scope')==mode and scope[0].get('table_sha256')==matrix_sha256,'scope:'+side)
        observation=[r for r in stream if r.get('event')=='OBSERVATION']
        need(len(observation)==1 and integer(observation[0],'duration_ms')>=matrix_document['common']['observation_ms'] and phase_contains(side,'OBSERVE',observation[0]),'observation:'+side)
        for phase in PHASES:
            loans=[r for r in stream if r.get('event')=='LOAN_ACCOUNTING' and r.get('phase')==phase]
            need(len(loans)==1 and integer(loans[0],'loans_outstanding')==0 and phase_contains(side,phase,loans[0]),'phase_loans:'+side+':'+phase)
        terminal={(r.get('instance_id'),r.get('invocation')) for r in rows[side] if r.get('reason')=='NO_APPLICATION' or (r.get('_kind')=='EXPERIMENT_DROP_NO_APPLICATION' and r.get('disposition')=='TERMINAL_NOT_SAVED')}
        for row in rows[side]:
            if row.get('event') in RX_EVENTS or (row.get('_kind')=='EXPERIMENT_APPLICATION_RX' and integer(row,'stage')>=1):need((row.get('instance_id'),row.get('invocation')) not in terminal,'no_app_future_adoption:'+side)
        cleanup=[r for r in stream if r.get('event')=='PARTICIPANT_DELETE_RETURN']
        need(len(cleanup)==1 and integer(cleanup[0],'rc')==0 and integer(cleanup[0],'contained_rc')==0 and phase_contains(side,'CLEANUP',cleanup[0]),'dds_cleanup:'+side)
    seen={'A':-1,'B':-1};begun=[];returned=Counter()
    for event in control_events:
        need(event.get('run')==run and event.get('case')==case,'controller_identity')
        kind=event.get('event');side=event.get('side');phase=event.get('phase')
        if kind=='CONTROL_PHASE_OBSERVED':
            if not need(side in seen and phase in PHASES,'controller_phase'):continue
            idx=PHASES.index(phase);local=done.get((side,phase),{})
            need(idx==seen[side]+1 and integer(event,'order')==integer(local,'order') and str(event.get('pid'))==str(local.get('pid')),'controller_actual_phase')
            seen[side]=idx
        elif kind=='CONTROL_RELEASE_BEGIN':
            idx=len(begun)+1
            need(idx<len(PHASES) and phase==PHASES[idx] and event.get('prerequisite')==PHASES[idx-1] and all(v>=idx-1 for v in seen.values()),'release_without_both_prerequisites');begun.append(phase)
        elif kind=='CONTROL_RELEASE_RETURN':need(side in seen and phase in begun and event.get('result')=='PASS','release_return');returned[side,phase]+=1
        else:need(False,'unknown_controller_event:'+str(kind))
    need(begun==list(PHASES[1:]) and all(v==5 for v in seen.values()) and all(returned[s,p]==1 for s in 'AB' for p in PHASES[1:]),'controller_complete')
    endpoints=['W1','W2','R1'] if case=='K2' else ['W1','R1'];guids={e:guid(run,e) for e in endpoints};created={}
    for endpoint in endpoints:
        side='B' if endpoint=='R1' else 'A';stream=own[side];kind='reader' if side=='B' else 'writer'
        creation=[r for r in stream if r.get('event')=='ENTITY_CREATED' and r.get('endpoint')==endpoint]
        cp='CHANGE' if case=='H1' and side=='B' else 'INITIAL'
        if need(len(creation)==1,'entity_count:'+endpoint):
            created[endpoint]=creation[0]
            need(creation[0].get('guid')==guids[endpoint] and creation[0].get('kind')==kind and integer(creation[0],'entity_kind')==(7 if side=='B' else 2) and creation[0].get('phase')==cp and phase_contains(side,cp,creation[0]),'keyed_entity_identity:'+endpoint)
        qos=[r for r in stream if r.get('event')=='LOCAL_QOS' and r.get('endpoint')==endpoint]
        wanted={'reliability':2,'durability':1 if case=='H1' else 0,'history':0 if (case=='H1' and side=='A') or (case=='H2' and side=='B') or case=='R1' else 1,'depth':2,'max_samples':64,'max_instances':4,'max_samples_per_instance':16,'allocated_samples':64,'ownership':0,'data_sharing':3,'max_blocking_seconds':1}
        if case=='H3':wanted.update(max_samples=16,max_samples_per_instance=4,allocated_samples=16)
        if case=='R1':wanted.update(max_samples=8,max_instances=2,max_samples_per_instance=2,allocated_samples=8)
        if case=='R2' and side=='A':wanted.update(max_samples=8,max_instances=2,max_samples_per_instance=4,allocated_samples=8)
        if case=='R3':wanted.update(max_samples=12,max_instances=3,max_samples_per_instance=4,allocated_samples=12)
        if need(len(qos)==1,'local_qos_count:'+endpoint):
            for key,value in wanted.items():need(integer(qos[0],key)==value,'actual_qos:'+endpoint+':'+key)
        if side=='A':
            life=[r for r in stream if r.get('event')=='WRITER_LIFECYCLE_QOS' and r.get('endpoint')==endpoint]
            need(len(life)==1 and integer(life[0],'autodispose')==(0 if case=='K4_FALSE' else 1) and integer(life[0],'publish_mode')==0,'writer_lifecycle_qos:'+endpoint)
        peers={guids['R1']} if side=='A' else {guids[e] for e in endpoints if e!='R1'}
        phases=('INITIAL','CHANGE','OBSERVE') if case=='H1' and side=='A' else ('CHANGE','OBSERVE') if case=='H1' else ('INITIAL','OBSERVE')
        for phase in phases:
            snapshots=[r for r in stream if r.get('event')=='MATCH_SNAPSHOT' and r.get('endpoint')==endpoint and r.get('phase')==phase]
            expected=set() if case=='H1' and phase=='INITIAL' else peers
            if need(len(snapshots)==1,'snapshot_count:'+endpoint+':'+phase):
                snap=snapshots[0];actual=[] if snap.get('peers')=='none' else snap.get('peers','').split(',')
                need(set(actual)==expected and len(actual)==len(set(actual)) and snap.get('guid')==guids[endpoint] and phase_contains(side,phase,snap),'current_match_graph:'+endpoint+':'+phase)
                if phase=='OBSERVE':need(any(r.get('event')=='OBSERVATION' and integer(r,'order')<integer(snap,'order') for r in stream),'snapshot_after_observation:'+endpoint)
                for peer in expected:need(any(r.get('event')=='REMOTE_QOS' and r.get('endpoint')==endpoint and r.get('peer_guid')==peer and integer(r,'reliability')==2 and integer(r,'durability')==(1 if case=='H1' else 0) and integer(r,'order')<integer(snap,'order') for r in stream),'remote_actual_qos:'+endpoint)
        matches=[r for r in stream if r.get('event')=='MATCH_EVENT' and r.get('endpoint')==endpoint]
        need(any(integer(r,'change')>0 and r.get('peer_guid') in peers for r in matches),'fresh_match:'+endpoint)
        for match in matches:need(match.get('peer_guid') in peers and (integer(match,'change')>0 or integer(match,'order')>integer(done.get((side,'OBSERVE'),{}),'order')),'unexpected_match:'+endpoint)
    for side in 'AB':
        for row in [r for r in own[side] if r.get('event')=='DISCOVERY']:
            need(row.get('guid') in guids.values() and row.get('type')=='MddsKeyedSample' and row.get('topic')==f'mdds-keyed-{run}-T','discovery_type_identity:'+side)
    planned=expected_writes(case);write_rows=[r for r in own['A'] if r.get('event')=='WRITE_RETURN'];writes={};sequences=set()
    registrations=[r for r in own['A'] if r.get('event')=='REGISTER_RETURN'];handles={}
    for row in registrations:
        key=integer(row,'key');endpoint=row.get('endpoint');expected_handle=f'{key:08x}'+'0'*24
        need(endpoint in endpoints and endpoint!='R1' and key in (17,34,51),'register_identity')
        nil=case=='R2' and key==51
        need(integer(row,'defined')==(0 if nil else 1) and row.get('computed_handle')==expected_handle and row.get('instance_handle')==('0'*32 if nil else expected_handle),'key_registration:'+str(key))
        if not nil:handles[endpoint,key]=row['instance_handle']
        need(phase_contains('A',row.get('phase'),row),'register_phase')
    required_registers=Counter((endpoint,key,'INITIAL') for endpoint in endpoints if endpoint!='R1' for key in ([17] if case=='K2' else [17,34,51] if case=='R3' else [17,34]))
    if case=='R2':required_registers['W1',51,'CHANGE']+=1
    if case in ('K4_TRUE','K4_FALSE'):required_registers['W1',17,'OBSERVE']+=1
    need(Counter((r.get('endpoint'),integer(r,'key'),r.get('phase')) for r in registrations)==required_registers,'exact_registration_lifecycle')
    for row in write_rows:
        b=business(row);endpoint='W'+str(b[0]);seq=identity(row);phase=planned.get(b)
        need(b not in writes and phase is not None and row.get('phase')==phase and phase_contains('A',phase,row),'original_write_once_and_phase:'+str(b));writes[b]=row
        need(row.get('endpoint')==endpoint and row.get('writer_guid')==guids.get(endpoint) and seq not in sequences and seq[1]>=0 and (seq[1]>0 or seq[2]>0),'write_GUID_SN:'+str(b));sequences.add(seq)
        expected_hex,expected_hash=payload(run,b[1],b[0],b[2])
        need(row.get('payload_hex')==expected_hex and row.get('payload_hash')==expected_hash and row.get('data_run')==run and integer(row,'rc')==0 and row.get('registered_handle')==handles.get((endpoint,b[1])) and row.get('mode')=='DEDUCED_REGISTERED_KEY','write_content_and_handle:'+str(b))
        need(any(r.get('endpoint')==endpoint and integer(r,'key')==b[1] and integer(r,'defined')==1 and integer(r,'order')<integer(row,'order') for r in registrations),'write_prior_registration:'+str(b))
    need(set(writes)==set(planned) and len(write_rows)==len(planned),'exact_original_write_set')
    for (writer,key,ordinal),phase in planned.items():
        waits=[r for r in own['A'] if r.get('event')=='DDS_WAIT' and r.get('endpoint')=='W'+str(writer) and r.get('phase')==phase]
        required=not(case=='H1' and phase=='PRE')
        need(bool(waits) and all(r.get('state')==('Called' if required else 'NotCalled') and (integer(r,'rc')==0 and integer(r,'matched_reliable')>0 if required else 'rc' not in r) for r in waits),'DDS_wait_operation:'+str((writer,phase)))
    data=[r for r in own['B'] if r.get('event')=='DATA'];states=[r for r in own['B'] if r.get('event')=='INSTANCE_STATE'];apis=[r for r in own['B'] if r.get('event')=='API_RETURN'];loans=[r for r in own['B'] if r.get('event')=='RETURN_LOAN']
    ops={};op_rows=defaultdict(list)
    for row in data+states:op_rows[integer(row,'operation_id')].append(row)
    for api in apis:
        op=integer(api,'operation_id');need(op>0 and op not in ops,'operation_unique');ops[op]=api
        result=op_rows[op];rc=integer(api,'rc');n=integer(api,'count')
        need(api.get('operation') in ('read','take') and rc in (0,11) and n==len(result) and (n>0 if rc==0 else n==0),'API_return_records:'+str(op))
        need(integer(api,'sample_mask') in (1,2,3) and integer(api,'view_mask')==3 and integer(api,'instance_mask')==7 and phase_contains('B',api.get('phase'),api),'API_masks_phase:'+str(op))
        returned=[r for r in loans if integer(r,'operation_id')==op]
        need(len(returned)==(1 if rc==0 else 0),'loan_exact_return:'+str(op))
        if returned:need(integer(returned[0],'rc')==0 and integer(returned[0],'loans_outstanding')==0 and integer(returned[0],'order')>max([integer(api,'order')]+[integer(r,'order') for r in result]) and phase_contains('B',api.get('phase'),returned[0]),'loan_settlement:'+str(op))
        need(len({identity(r) for r in result if r.get('event')=='DATA'})==sum(r.get('event')=='DATA' for r in result),'duplicate_cache_identity_within_operation:'+str(op))
        for row in result:
            need(integer(row,'order')>integer(api,'order') and all(row.get(k)==api.get(k) for k in ('phase','operation','sample_mask','view_mask','instance_mask')),'record_API_context:'+str(op))
            need(integer(row,'sample_state') in (1,2) and bool(integer(row,'sample_state')&integer(api,'sample_mask')) and integer(row,'view_state') in (1,2) and integer(row,'instance_state') in (1,2,4),'SampleInfo_masks:'+str(op))
    need(all(op in ops for op in op_rows),'orphan_operation_records')
    need(all(integer(r,'operation_id') in ops for r in loans),'orphan_loan')
    unique={};seen_business={};key_to_handle={};handle_to_key={};taken=set();state_ledger=[]
    for row in sorted(data+states,key=lambda r:integer(r,'order')):
        handle=row.get('instance_handle');seq=identity(row);phase=row.get('phase')
        need(row.get('endpoint')=='R1' and row.get('reader_guid')==guids['R1'] and phase_contains('B',phase,row),'Reader_identity_phase')
        if row.get('event')=='INSTANCE_STATE':
            need(integer(row,'valid_data')==0 and integer(row,'payload_accessed')==0 and not any(k in row for k in ('key','writer_id','ordinal','data_run','payload_hex','payload_hash','payload_valid')),'invalid_data_must_not_access_payload')
            key=integer(row,'known_key');need(handle_to_key.get(handle)==key and row.get('mapping_source')=='PRIOR_VALID_DATA','state_prior_valid_mapping')
            wanted=4 if case=='K4_FALSE' else 2
            need(case in ('K3','K4_TRUE','K4_FALSE') and key==17 and phase=='CHANGE' and integer(row,'instance_state')==wanted,'version_bound_instance_state')
            known=integer(row,'identity_known')
            need(known in (0,1) and ((seq[0]==guids['W1'] and seq[1]>=0 and (seq[1]>0 or seq[2]>0) and seq not in sequences) if known==1 else all(row.get(k)=='UNKNOWN' for k in ('writer_guid','sn_hi','sn_lo'))),'state_original_identity')
            state_ledger.append({k:row.get(k) for k in ('instance_handle','known_key','instance_state','disposed_generation','no_writers_generation','writer_guid','sn_hi','sn_lo','identity_known','phase')});continue
        b=business(row);original=writes.get(b);key=b[1]
        if case not in ('H2','H3'):
            expected_phase='CHANGE' if case=='H1' and b[2]<5 else planned.get(b)
            need(phase==expected_phase,'Reader_delivery_phase:'+str(b))
        need(integer(row,'valid_data')==1 and integer(row,'instance_state')==1 and integer(row,'payload_valid')==1,'valid_DATA_state')
        need(original is not None and seq==identity(original or {}) and row.get('writer_guid')==guids.get('W'+str(b[0])),'Reader_original_writer_SN')
        expected_hex,expected_hash=payload(run,key,b[0],b[2]);need(row.get('payload_hex')==expected_hex and row.get('payload_hash')==expected_hash and row.get('data_run')==run,'Reader_payload')
        need(bool(re.fullmatch('[0-9a-f]{32}',handle or '')) and handle!='0'*32 and key_to_handle.get(key,handle)==handle and handle_to_key.get(handle,key)==key,'Reader_handle_key_bimap')
        key_to_handle[key]=handle;handle_to_key[handle]=key
        need(seq not in taken,'record_after_take');need(seq not in unique or business(unique[seq])==b,'same_SN_different_data');need(b not in seen_business or seen_business[b]==seq,'app_resend_not_history')
        unique.setdefault(seq,row);seen_business[b]=seq
        if row.get('operation')=='take':taken.add(seq)
    expected_data=set(planned)
    if case=='H1':expected_data={b for b in expected_data if b[2]>=3}
    need(set(seen_business)==expected_data,'exact_per_Reader_key_Writer_data_set')
    ledgers=[r for r in own['B'] if r.get('event')=='LEDGER']
    need(len(ledgers)==1 and integer(ledgers[0],'unique')==len(unique) and ledgers[0].get('failure')=='none','ledger_reconciliation')
    for row in data:
        if case=='H1':need(row.get('phase')==('CHANGE' if integer(row,'ordinal')<5 else 'POST'),'late_history_phase')
    if case=='H1':
        need(created.get('R1',{}).get('phase')=='CHANGE','late_reader_after_PRE')
        need(not [r for r in data if r.get('phase')=='PRE'],'no_historical_receipt_before_reader')
    def op_set(op):return {business(r) for r in op_rows[op] if r.get('event')=='DATA'}
    if case=='H2':
        for ordinal,phase in enumerate(('PRE','CHANGE','POST','OBSERVE'),1):
            snapshots=[r for r in own['B'] if r.get('event')=='SNAPSHOT' and integer(r,'round')==ordinal and r.get('phase')==phase]
            need(len(snapshots)==1,'round_snapshot:'+phase)
            if snapshots:
                op=integer(snapshots[0],'operation_id');api=ops.get(op,{})
                need(api.get('operation')=='read' and api.get('phase')==phase and integer(api,'sample_mask')==3 and {(1,17,ordinal),(1,34,ordinal)}<=op_set(op) and phase_contains('B',phase,snapshots[0]),'nonremoving_round_pair:'+phase)
        take=[r for r in apis if r.get('operation')=='take'];wanted={(1,k,o) for k in (17,34) for o in (3,4)}
        need(all(r.get('phase')=='OBSERVE' for r in take),'early_take_cannot_prove_eviction')
        need(any(integer(r,'rc')==0 and integer(r,'sample_mask')==3 and op_set(integer(r,'operation_id'))==wanted for r in take),'KEEP_LAST_final_take_exact')
        need(any(integer(r,'rc')==11 and integer(r,'sample_mask')==3 for r in take),'KEEP_LAST_after_take_empty')
    if case=='H3':
        required=[('CHANGE','read',3,0,8),('CHANGE','read',1,0,8),('CHANGE','read',2,11,0),('POST','take',3,0,8),('POST','read',3,11,0),('POST','take',3,11,0)]
        last=0
        for phase,method,mask,rc,count in required:
            found=[r for r in apis if r.get('phase')==phase and r.get('operation')==method and integer(r,'sample_mask')==mask and integer(r,'rc')==rc and integer(r,'count')==count and integer(r,'order')>last]
            if need(bool(found),'KEEP_ALL_read_take_contract:'+str((phase,method,mask,rc,count))):
                last=integer(found[0],'order')
                if count:need(op_set(integer(found[0],'operation_id'))==expected_data,'KEEP_ALL_exact_operation_set')
        need(not any(r.get('operation')=='take' and r.get('phase') not in ('POST','OBSERVE') for r in apis),'KEEP_ALL_no_early_take')
    if case in ('K3','K4_TRUE','K4_FALSE'):
        need(bool(states),'state_notice_required');need(all(r.get('operation')=='read' for r in apis),'retain_lifecycle_history')
        state_ops=[r for r in own['A'] if r.get('event')=='STATE_OPERATION']
        need(len(state_ops)==1 and state_ops[0].get('operation')==('dispose' if case=='K3' else 'unregister') and integer(state_ops[0],'key')==17 and integer(state_ops[0],'rc')==0 and phase_contains('A','CHANGE',state_ops[0]),'state_public_operation')
        revival=[r for r in data if integer(r,'key')==17 and integer(r,'ordinal')==2]
        need(bool(revival) and all(r.get('phase')=='OBSERVE' for r in revival),'revival_after_state_and_B_progress')
        if revival:
            need(all(integer(r,'disposed_generation')==(0 if case=='K4_FALSE' else 1) and integer(r,'no_writers_generation')==(1 if case=='K4_FALSE' else 0) for r in revival),'retained_generation_counts')
        need(any(r.get('event')=='LIFECYCLE_BOUNDARY' and r.get('phase')=='POST' and integer(r,'A_state')==(4 if case=='K4_FALSE' else 2) and integer(r,'B_state')==1 for r in own['B']),'B_alive_while_A_nonalive')
    if case=='R2':
        c=[r for r in registrations if integer(r,'key')==51]
        need(len(c)==1 and c[0].get('phase')=='CHANGE' and integer(c[0],'defined')==0,'one_capacity_rejection_C')
        need(not any(r.get('event')=='STATE_OPERATION' for r in own['A']),'AB_not_reclaimed_for_C')
    if case=='R1':
        for side in 'AB':
            invalid=[r for r in own[side] if r.get('event')=='INVALID_CREATE'];setq=[r for r in own[side] if r.get('event')=='INVALID_SET_QOS']
            need(len(invalid)==1 and invalid[0].get('result')=='NULL' and integer(invalid[0],'depth')==3 and integer(invalid[0],'max_samples_per_instance')==2 and phase_contains(side,'PRE',invalid[0]),'invalid_create:'+side)
            need(len(setq)==1 and integer(setq[0],'rc')==8 and integer(setq[0],'unchanged')==1 and phase_contains(side,'CHANGE',setq[0]),'inconsistent_set_qos:'+side)
    core=not errors
    native=_native(config,rows,own,unique,states,done,created) if core else {'pass':False,'errors':['core_failed'],'sample_joins':[],'state_joins':[]}
    native_pass=core and native['pass'];passed=core and (native_pass if require_native else mode=='HOST_REFERENCE_UDP' or native_pass)
    return {'schema':1,'run':run,'case':case,'pass':passed,'core_pass':core,'native_proof_pass':native_pass,'status':'FAIL' if not core else 'NATIVE_CHAIN_OBSERVED_EXTERNAL_GATES_REQUIRED' if native_pass else 'UNVERIFIED_NATIVE_PROOF' if require_native else 'HOST_REFERENCE_CORE_PASS','errors':errors,'actual_write_operations':len(write_rows),'data_operation_returns':len(data),'unique_data_identities':len(unique),'take_identities':len(taken),'instance_state_records':len(states),'data_ledger':[{'writer_guid':s[0],'sn_hi':s[1],'sn_lo':s[2],'key':integer(r,'key'),'ordinal':integer(r,'ordinal'),'instance_handle':r.get('instance_handle')} for s,r in unique.items()],'instance_state_ledger':state_ledger,'read_take_semantics':'Repeated non-removing read is not a native duplicate; successful take consumes a cache identity','history_interpretation':'DDS per-instance bounded retention/eviction, not transport loss accounting','native':native,'device_tested':native_pass,'qualification':'NOT_QUALIFIED','sdk_internal_settlement':'UNOBSERVED','external_device_guards_and_load':'NOT_ASSESSED_BY_THIS_ANALYZER'}


def analyze_window(window,matrix_path,require_native=True):
    window=Path(window);matrix_bytes=Path(matrix_path).read_bytes();sha=hashlib.sha256(matrix_bytes).hexdigest()
    config=json.loads((window/'window.json').read_text(encoding='utf-8-sig'));records={};hashes={}
    for side in 'AB':
        source=window/(side+'.raw.stdout.log');raw=source.read_bytes();hashes[source.name]=hashlib.sha256(raw).hexdigest();records[side]=parse_text(raw.decode('utf-8'),side)
    control=[json.loads(line) for line in (window/'control.jsonl').read_text().splitlines() if line.strip()]
    result=json.loads((window/'result.json').read_text());consumed={s:{p.stem:p.read_text(encoding='utf-8') for p in (window/(s+'-control')).glob('*.consumed')} for s in 'AB'}
    report=analyze_records(config,records,control,json.loads(matrix_bytes),matrix_sha256=sha,require_native=require_native,processes=result.get('processes'),consumed=consumed)
    report['source_sha256']=hashes;report['matrix_sha256']=sha
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('window',type=Path);parser.add_argument('--matrix',required=True,type=Path);parser.add_argument('--output',required=True,type=Path);parser.add_argument('--allow-host-reference',action='store_true')
    args=parser.parse_args()
    try:result=analyze_window(args.window,args.matrix,not args.allow_host_reference)
    except (OSError,ValueError,KeyError,TypeError) as error:result={'pass':False,'core_pass':False,'native_proof_pass':False,'status':'FAIL','errors':[str(error)]}
    with args.output.open('x',encoding='utf-8') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps({'pass':result['pass'],'status':result['status'],'errors':result.get('errors',[])[:12]}));return 0 if result['pass'] else 1


if __name__=='__main__':raise SystemExit(main())
