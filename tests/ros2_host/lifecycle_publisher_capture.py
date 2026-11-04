"""Strict finite managed-publisher receipt oracle; no DDS sequence-number inference."""
import re


def analyze_publisher(texts, run, pids):
    if set(texts) != {'A', 'B'} or set(pids) != {'A', 'B'} or pids['A'] == pids['B']:
        raise ValueError('two process identities')
    if not re.fullmatch('[0-9a-f]{16}', run) or any(type(p) is not int or p <= 0 for p in pids.values()):
        raise ValueError('run/PID format')
    qos = dict(reliability='reliable', durability='volatile', history='keep_last', depth='10')
    common = {'MATCHED': {'nodes', 'samples', 'transitions'}, 'START': {'nodes'}, 'CLOSED': {'result'}}
    schemas = {'A': dict(common, READY={'namespace', 'gid', 'impl'} | set(qos),
        CALLBACK={'callback','ordinal','previous','current','return','activated'},
        PROBE={'phase','state','activated','matched','gid','impl'},
        ATTEMPT={'phase','attempt','overload','activated','payload_hex'}, RETURN={'phase','attempt','overload'},
        BUSINESS_DONE={'probes','attempts','callbacks','finalized','activated','gid'}),
        'B': dict(common, READY={'namespace'} | set(qos),
        CHANGE_REQUEST={'operation','service','transition'}, CHANGE_RESPONSE={'operation','success'},
        STATE={'operation','service','state','label'}, TRANSITION={'ordinal','transition','start','goal'},
        PROBE_REQUEST={'phase','service','matched'}, PROBE_RESPONSE={'phase','state','activated','first','last','gid','impl','success'},
        RECEIVE={'phase','attempt','overload','gid','impl','payload_hex'},
        BUSINESS_DONE={'changes','states','probes','events','received','pending'},
        OBSERVATION_END={'received','inactive_received','finite'})}
    rows = {}
    def require(ok, why):
        if not ok: raise ValueError(why)
    for side in 'AB':
        rows[side] = []
        for line in texts[side].splitlines():
            if not line.startswith('PUBLISHER_ACCEPT'): continue
            pairs = [part.split('=', 1) for part in line.split()[1:]]
            require(line.startswith('PUBLISHER_ACCEPT ') and all(len(p) == 2 for p in pairs), 'record syntax')
            row = dict(pairs); event = row.get('event')
            require(len(row) == len(pairs) and event in schemas[side], 'schema/duplicate key')
            require(set(row) == schemas[side][event] | {'run','side','pid','local','event'}, 'schema fields')
            require((row['run'],row['side'],row['pid'],row['local']) ==
                    (run,side,str(pids[side]),str(len(rows[side])+1)), 'record identity/order')
            rows[side].append(row)
    def select(side, event): return [r for r in rows[side] if r['event'] == event]
    def body(row): return {k:v for k,v in row.items() if k not in {'run','side','pid','local','event'}}
    def one(side, event, expected):
        found = select(side,event)
        require(len(found) == 1 and body(found[0]) == {k:str(v) for k,v in expected.items()}, event)
        return found[0]
    def ordered(*records):
        values = [int(r['local']) for r in records]
        require(all(a < b for a,b in zip(values, values[1:])), 'local causal order')
    ns = '/publisher_' + run
    require(rows['A'] and rows['B'], 'empty records')
    writer = rows['A'][0].get('gid',''); impl = rows['A'][0].get('impl','')
    require(re.fullmatch('[0-9a-f]{32}',writer) and writer != '0'*32 and re.fullmatch('[A-Za-z0-9_]+',impl), 'publisher GID')
    for side in 'AB':
        require([r['event'] for r in rows[side][:3]] == ['READY','MATCHED','START'], 'startup sequence')
        one(side,'READY',dict(namespace=ns,**qos,**(dict(gid=writer,impl=impl) if side=='A' else {})))
        one(side,'MATCHED',dict(nodes=1,samples=1,transitions=1)); one(side,'START',dict(nodes=1)); one(side,'CLOSED',dict(result=0))
    require([r['event'] for r in rows['A'][-2:]] == ['BUSINESS_DONE','CLOSED'], 'server close')
    require([r['event'] for r in rows['B'][-3:]] == ['BUSINESS_DONE','OBSERVATION_END','CLOSED'], 'observation end')
    one('A','BUSINESS_DONE',dict(probes=4,attempts=8,callbacks=6,finalized=1,activated=0,gid=writer))
    done = one('B','BUSINESS_DONE',dict(changes=6,states=7,probes=4,events=12,received=4,pending=0))
    end = one('B','OBSERVATION_END',dict(received=4,inactive_received=0,finite=1))
    ordered(done,end)

    def payload(phase,attempt,overload):
        return f'run={run};phase={phase};attempt={attempt};overload={overload}'.encode().hex()
    # Server order is deterministic: one executor, synchronous service bodies.
    expected_a = []
    def add(event, **fields): expected_a.append(dict(event=event,**{k:str(v) for k,v in fields.items()}))
    callbacks = [('configure',1,1,10,0),('activate',1,2,13,1),('deactivate',1,3,14,0),
                 ('activate',2,2,13,1),('deactivate',2,3,14,0),('shutdown',1,2,12,0)]
    for index,(callback,ordinal,previous,current,active) in enumerate(callbacks,1):
        add('CALLBACK',callback=callback,ordinal=ordinal,previous=previous,current=current,activated=active,**{'return':97})
        if index > 4: continue
        phase = index; state = 3 if active else 2
        add('PROBE',phase=phase,state=state,activated=active,matched=1,gid=writer,impl=impl)
        for attempt,overload in ((phase*2-1,'ref'),(phase*2,'unique')):
            add('ATTEMPT',phase=phase,attempt=attempt,overload=overload,activated=active,payload_hex=payload(phase,attempt,overload))
            add('RETURN',phase=phase,attempt=attempt,overload=overload)
    actual_a = [dict(event=r['event'],**body(r)) for r in rows['A'][3:-2]]
    require(actual_a == expected_a, 'server callback/probe/publish sequence')

    changes = select('B','CHANGE_REQUEST'); responses = select('B','CHANGE_RESPONSE'); states = select('B','STATE')
    events = select('B','TRANSITION'); probes = select('B','PROBE_REQUEST'); probe_responses = select('B','PROBE_RESPONSE')
    samples = select('B','RECEIVE')
    require(tuple(map(len,(changes,responses,states,events,probes,probe_responses,samples))) == (6,6,7,12,4,4,4), 'business cardinality')
    stable = [1,2,3,2,3,2,4]; labels = {1:'unconfigured',2:'inactive',3:'active',4:'finalized'}
    for operation,(row,state) in enumerate(zip(states,stable)):
        require(body(row) == dict(operation=str(operation),service=ns+'/managed/get_state',state=str(state),label=labels[state]), 'state snapshot')
    triples = [(1,10),(10,2),(2,13),(13,3),(3,14),(14,2),(2,13),(13,3),(3,14),(14,2),(2,12),(12,4)]
    for ordinal,(event,(start,goal)) in enumerate(zip(events,triples),1):
        require(body(event) == dict(ordinal=str(ordinal),transition='0',start=str(start),goal=str(goal)), 'transition event')
    for operation,(request,response,transition) in enumerate(zip(changes,responses,[1,3,4,3,4,6]),1):
        require(body(request) == dict(operation=str(operation),service=ns+'/managed/change_state',transition=str(transition)), 'change request')
        require(body(response) == dict(operation=str(operation),success='1'), 'change response')
        ordered(states[operation-1],request,response,states[operation])
        ordered(request,events[2*operation-2],events[2*operation-1],states[operation])
    for phase,(request,response) in enumerate(zip(probes,probe_responses),1):
        active = int(phase%2==0); state = 3 if active else 2
        require(body(request) == dict(phase=str(phase),service=ns+'/probe',matched='1'), 'probe request')
        require(body(response) == dict(phase=str(phase),state=str(state),activated=str(active),first=str(phase*2-1),last=str(phase*2),gid=writer,impl=impl,success='1'), 'probe response')
        ordered(states[phase],request,response,changes[phase])
    for sample,attempt in zip(samples,[3,4,7,8]):
        phase = 2 if attempt < 5 else 4; overload = 'ref' if attempt%2 else 'unique'
        require(body(sample) == dict(phase=str(phase),attempt=str(attempt),overload=overload,gid=writer,impl=impl,payload_hex=payload(phase,attempt,overload)), 'sample/GID/script')
        ordered(probes[phase-1],sample,changes[phase])
    ordered(states[-1],done,end)
    return dict(changes=6,states=7,probes=4,publish_attempts=8,received=4,transition_events=12,callbacks=6,
                active_attempts=4,inactive_attempts=4,inactive_received=0,publisher_gid=writer,gid_implementation=impl,
                negative_observation='FINITE_MATCHED_WINDOW_ONLY',rtps_sequence_numbers_observed=False,
                loaned_and_serialized_publishers_tested=False)
