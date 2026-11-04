"""Fixed two-exchange serialized API oracle; synthetic receipts alone are not ROS proof."""
import re


def analyze_serialized(texts, run, pids):
    def require(ok, why):
        if not ok: raise ValueError(why)
    require(set(texts)=={'A','B'} and set(pids)=={'A','B'} and pids['A']!=pids['B'], 'two identities')
    require(re.fullmatch('[0-9a-f]{16}',run) and all(type(p) is int and p>0 for p in pids.values()), 'run/PID format')
    schemas={
        'READY':{'namespace','pub_topic','sub_topic','gid','impl','reliability','durability','history','depth'},
        'MATCHED':{'publishers','subscriptions'}, 'START':{'peers'},
        'SERIALIZE':{'role','id','overload','payload_hex','length','capacity_before','capacity_after'},
        'PUBLISH_ATTEMPT':{'role','id','overload'}, 'RETURN':{'role','id','overload'},
        'RECEIVE':{'role','id','payload_hex','gid','impl'},
        'BUSINESS_DONE':{'serialized','published','received','pending'},
        'OBSERVATION_END':{'received','finite'}, 'CLOSED':{'result'}}
    rows={}
    for side in 'AB':
        rows[side]=[]
        for line in texts[side].splitlines():
            if not line.startswith('SERIALIZED_ACCEPT'): continue
            pairs=[token.split('=',1) for token in line.split()[1:]]
            require(line.startswith('SERIALIZED_ACCEPT ') and all(len(p)==2 for p in pairs),'record syntax')
            row=dict(pairs); event=row.get('event')
            require(len(row)==len(pairs) and event in schemas,'event/schema')
            require(set(row)==schemas[event]|{'run','side','pid','local','event'},'fields')
            require((row['run'],row['side'],row['pid'],row['local'])==(run,side,str(pids[side]),str(len(rows[side])+1)), 'record identity/order')
            rows[side].append(row)
    def body(row): return {k:v for k,v in row.items() if k not in {'run','side','pid','local','event'}}
    def select(side,event): return [r for r in rows[side] if r['event']==event]
    def one(side,event,expected):
        found=select(side,event)
        require(len(found)==1 and body(found[0])==expected,event)
    writers={}
    for side in 'AB':
        ready=select(side,'READY');require(len(ready)==1,'READY count')
        writer,impl=ready[0]['gid'],ready[0]['impl']
        require(re.fullmatch('[0-9a-f]{32}',writer) and writer!='0'*32 and impl=='rmw_fastrtps_cpp','GID format/implementation')
        writers[side]=(writer,impl)
    require(writers['A'][0]!=writers['B'][0] and writers['A'][1]==writers['B'][1],'distinct compatible publishers')
    ns='/serialized_'+run
    def payload(role,identifier):
        # Independent fixed contract; no child-supplied body or length determines the expected bytes.
        value='0123456789abcdef0' if identifier==1 else '0123456789abcdef'*256
        return f'run={run};role={role};id={identifier};body={value}'.encode()
    sizes={}
    for side in 'AB':
        peer='B' if side=='A' else 'A'; role='request' if side=='A' else 'reply'; incoming='reply' if side=='A' else 'request'
        one(side,'READY',dict(namespace=ns,pub_topic=ns+'/'+role,sub_topic=ns+'/'+incoming,gid=writers[side][0],impl=writers[side][1],
            reliability='reliable',durability='volatile',history='keep_last',depth='10'))
        one(side,'MATCHED',dict(publishers='1',subscriptions='1'));one(side,'START',dict(peers='1'))
        one(side,'BUSINESS_DONE',dict(serialized='2',published='2',received='2',pending='0'))
        one(side,'OBSERVATION_END',dict(received='2',finite='1'));one(side,'CLOSED',dict(result='0'))
        require([r['event'] for r in rows[side][:3]]==['READY','MATCHED','START'],'startup order')
        require([r['event'] for r in rows[side][-3:]]==['BUSINESS_DONE','OBSERVATION_END','CLOSED'],'closure order')
        expected=[];sizes[side]=[]
        for identifier in (1,2):
            overload='wrapper' if identifier==1 else 'raw'
            identity=dict(role=role,id=str(identifier),overload=overload)
            serialized=[r for r in select(side,'SERIALIZE') if r['id']==str(identifier)]
            require(len(serialized)==1,'serialization per exchange')
            length=serialized[0]['length']; require(re.fullmatch('[1-9][0-9]*',length),'serialized length format')
            require(len(payload(role,identifier))<int(length)<=8192,'serialized length bounds')
            sizes[side].append(int(length))
            sent=[dict(event='SERIALIZE',**identity,payload_hex=payload(role,identifier).hex(),length=length,
                       capacity_before='8192',capacity_after=length),
                  dict(event='PUBLISH_ATTEMPT',**identity),dict(event='RETURN',**identity)]
            received=dict(event='RECEIVE',role=incoming,id=str(identifier),payload_hex=payload(incoming,identifier).hex(),
                          gid=writers[peer][0],impl=writers[peer][1])
            # Each child is single-threaded. B replies inside its receive callback;
            # A spins for a reply only after its synchronous publish returns.
            expected.extend(sent+[received] if side=='A' else [received]+sent)
        require([dict(event=r['event'],**body(r)) for r in rows[side][3:-3]]==expected,'exchange/payload/overload/GID sequence')
    return dict(exchanges=2,serializations=4,publish_attempts=4,publish_returns=4,received=4,
                wrapper_publishes=2,raw_publishes=2,body_lengths=[17,4096],initial_capacity=8192,serialized_lengths=sizes,
                post_capacity_contract='EQUALS_SERIALIZED_LENGTH_FOR_RMW_FASTRTPS_CPP_8_4_3',
                publisher_gids={s:dict(gid=g,implementation=i) for s,(g,i) in writers.items()},
                interpretation='FINITE_TYPED_SERIALIZED_SEMANTIC_INTEROPERABILITY',
                generic_tested=False,loaned_tested=False,zero_copy_tested=False,wire_byte_identity_claimed=False)
