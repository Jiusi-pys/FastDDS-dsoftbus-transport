"""Exact public API route cross-match; no first-online heuristic or device calls."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

def parse(text, side):
    errors=[]
    def need(value,label):
        if not value: errors.append(side+':'+label)
    local=re.findall(r'^route_kind=local network_id=([^\s]+)$',text,re.M)
    rows=re.findall(r'^route_kind=remote index=(\d+) network_id=([^\s]+)$',text,re.M)
    peers=[value for _,value in rows]
    need(len(local)==1 and all(re.fullmatch('[0-9a-f]{64}',x) for x in local),'EXACT_LOCAL_ID')
    need(all(re.fullmatch('[0-9a-f]{64}',x) for x in peers),'VALID_REMOTE_IDS')
    need(len(set(peers))==len(peers),'DUPLICATE_REMOTE')
    need([int(i) for i,_ in rows]==list(range(len(rows))),'REMOTE_INDEX_SEQUENCE')
    need(not local or local[0] not in peers,'NO_SELF_ROUTE')
    need(len(re.findall(r'^scope=READONLY_ROUTE_QUERY role=list-nodes\b',text,re.M))==1,'READONLY_MODE')
    need(len(re.findall(r'^stage=GetLocalNodeDeviceInfo action=RETURN rc=0\b',text,re.M))==1,'LOCAL_API_RETURN')
    returned=re.findall(r'^stage=GetAllNodeDeviceInfo action=RETURN rc=0 count=(\d+) data_present=1\b',text,re.M)
    need(len(returned)==1 and int(returned[0])==len(peers) and 0<len(peers)<=32,'REMOTE_API_COUNT')
    need(len(re.findall(r'^stage=FreeNodeInfo action=RETURN$',text,re.M))==1,'FREE_RETURN')
    need(len(re.findall(r'^stage=route_summary query_valid=true remote_count='+str(len(peers))+r' route_available=1 free_returned=1 allocation_present=1 node_observation_cap=32 communication=NOT_RUN$',text,re.M))==1,'ROUTE_SUMMARY')
    need(len(re.findall(r'^result=PASS scope=READONLY_ROUTE_QUERY fatal=0 untracked_first_id=0 untracked_count=0 cleanup=NO_SOCKET_RESOURCES token_restored=1 quiescence=NOT_VERIFIED dds=NOT_RUN transport=NOT_IMPLEMENTED$',text,re.M))==1,'FINAL_QUERY_RESTORED')
    need(re.findall(r'^MDDS_REMOTE_EXIT=(\d+)$',text,re.M)==['0'],'REMOTE_EXIT')
    need(not re.search(r'(?:scope=SOCKET|^stage=(?:Socket|Listen|Bind|SendBytes|SendStream|Shutdown)\b)',text,re.M),'NO_SOCKET_TRACE')
    return {'local':local[0] if len(local)==1 else None,'online':peers,'errors':errors}

def correlate(a,b):
    boards={'A':parse(a,'A'),'B':parse(b,'B')}
    errors=boards['A']['errors']+boards['B']['errors']
    if boards['A']['local']==boards['B']['local']: errors.append('DISTINCT_BOARD_LOCAL_IDS')
    for side,other in [('A','B'),('B','A')]:
        if boards[side]['online'].count(boards[other]['local'])!=1: errors.append(side+':OPPOSITE_ACTUAL_LOCAL_NOT_ONLINE_EXACTLY_ONCE')
    return {'pass':not errors,'errors':errors,'boards':boards,
            'peer':{'A':boards['B']['local'],'B':boards['A']['local']}}

def decode(path):
    data=Path(path).read_bytes()
    return data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig').replace('\r\n','\n')

def analyze_completed(window, output=None, archive_manifest=None):
    window=Path(window).resolve();destination=Path(output).resolve() if output else window
    task_root=Path(__file__).resolve().parent.parent
    if destination!=window:
        if not destination.is_relative_to(task_root) or destination.exists():raise ValueError('Fresh task-owned replay output required')
    elif any((destination/n).exists() for n in ('PRIVATE_ROUTE_BINDINGS.json','ROUTE_QUERY_RESULT.json','REPLAY_PROVENANCE.json')):raise ValueError('Never overwrite prior query results')
    spec=importlib.util.spec_from_file_location('readonly_query_receipt',Path(__file__).with_name('query_receipt.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    provenance=m.verify_completed_query(window,archive_manifest)
    source={side:window/side/'directory-query/stdout.log' for side in 'AB'}
    result=correlate(decode(source['A']),decode(source['B']))
    if not result['pass']:raise ValueError('Query text rejected: '+','.join(result['errors']))
    result.update(run=provenance['query_run'],scope='CURRENT_PUBLIC_DIRECTORY_NO_SOCKET_DDS_NOT_RUN',source_sha256={s:hashlib.sha256(p.read_bytes()).hexdigest() for s,p in source.items()},replay=provenance)
    public={key:result[key] for key in ['pass','errors','run','scope','source_sha256','replay']}
    public['local_id_sha256']={s:hashlib.sha256(result['boards'][s]['local'].encode()).hexdigest() for s in 'AB'}
    public['selection']='OPPOSITE_ACTUAL_LOCAL_ID_EXACTLY_ONCE_IN_THIS_ONLINE_LIST'
    if destination!=window:destination.mkdir(parents=True,exist_ok=False)
    for name,obj in [('PRIVATE_ROUTE_BINDINGS.json',result),('ROUTE_QUERY_RESULT.json',public),('REPLAY_PROVENANCE.json',provenance)]:
        with (destination/name).open('x',encoding='utf-8') as stream:json.dump(obj,stream,indent=2)
    return public

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('window',type=Path);parser.add_argument('--output',type=Path);parser.add_argument('--archive-manifest',type=Path);args=parser.parse_args()
    print(json.dumps(analyze_completed(args.window,args.output,args.archive_manifest)))
