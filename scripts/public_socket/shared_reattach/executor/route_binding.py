"""Narrow binding of a completed, guarded public directory-query receipt."""
import argparse
import hashlib
import importlib.util
import json
import re
from pathlib import Path

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def require(value,label):
    if not value:raise ValueError(label)

def load_route_binding(private_path):
    private_path=Path(private_path).resolve()
    task=Path(__file__).resolve().parent.parent
    require(private_path.is_relative_to(task),'Query must belong to current task')
    if json.loads(private_path.read_text(encoding='utf-8-sig')).get('recovered_identity_source') is not None:return load_recovered_identity_binding(private_path)
    if json.loads(private_path.read_text(encoding='utf-8-sig')).get('replay') is not None:return load_replayed_binding(private_path)
    require(private_path.parent.parent==task/'windows','Query must belong to current task')
    require(private_path.name=='PRIVATE_ROUTE_BINDINGS.json','Expected explicit private route receipt')
    directory=private_path.parent
    files={}
    def read(name):
        path=directory/name;data=path.read_bytes();files[str(path)]=hashlib.sha256(data).hexdigest()
        value=json.loads(data.decode('utf-8-sig'));require(isinstance(value,dict),'Receipt must be an object');return value
    private=read('PRIVATE_ROUTE_BINDINGS.json');public=read('ROUTE_QUERY_RESULT.json')
    config=read('window.json');summary=read('window-summary.json')
    run=private.get('run')
    require(isinstance(run,str) and re.fullmatch('[0-9a-f]{16}',run),'Invalid query run')
    require(public.get('run')==config.get('run')==run,'Query run mismatch')
    require(config.get('scenario')=='readonly-directory-query','Not a registered directory query')
    require(private.get('pass') is True and public.get('pass') is True and private.get('errors')==public.get('errors')==[],'Query not PASS')
    require(private.get('scope')=='CURRENT_PUBLIC_DIRECTORY_NO_SOCKET_DDS_NOT_RUN','Wrong query scope')
    require(summary.get('first_failure') is None and summary.get('cleanup_failures')==[] and summary.get('attempted_grants')==['A','B'],'Query cleanup not complete')
    for side in ['A','B']:
        guard=read(side+'/final-guard.json')
        require(all(guard.get(name) is True for name in ['Passed','byte_match','metadata_match','process_absent']) and guard.get('Phase')=='Revoked' and guard.get('service_permission')=='DENIED','Incomplete opposite/current board guard')
    parser_path=Path(__file__).resolve().parent.parent/'executor-route-query/route_query.py'
    parser_sha=digest(parser_path)
    spec=importlib.util.spec_from_file_location('frozen_public_query_parser',parser_path)
    parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
    text={};source_sha={}
    for side in ['A','B']:
        path=directory/side/'directory-query/stdout.log';data=path.read_bytes()
        sha=hashlib.sha256(data).hexdigest();source_sha[side]=sha;files[str(path)]=sha
        text[side]=data.decode('utf-16' if data.startswith((b'\xff\xfe',b'\xfe\xff')) else 'utf-8-sig').replace('\r\n','\n')
    require(private.get('source_sha256')==public.get('source_sha256')==source_sha,'Raw query hash mismatch')
    parsed=parser.correlate(text['A'],text['B'])
    require(parsed['pass'],'Raw directory cross-match failed')
    require(private.get('peer')==parsed['peer'] and private.get('boards')==parsed['boards'],'Private selection differs from raw public API records')
    expected_digests={side:hashlib.sha256(parsed['boards'][side]['local'].encode()).hexdigest() for side in ['A','B']}
    require(public.get('local_id_sha256')==expected_digests,'Public identity digest mismatch')
    files[str(parser_path.resolve())]=parser_sha
    return {'schema':1,'query_run':run,'private_receipt':str(private_path),'peer':parsed['peer'],
            'local_id_sha256':expected_digests,'proof_files_sha256':files,
            'selection':'OPPOSITE_ACTUAL_LOCAL_ID_EXACTLY_ONCE_IN_GUARDED_PUBLIC_QUERY'}

def load_recovered_identity_binding(private_path):
    task=Path(__file__).resolve().parent.parent;private_path=Path(private_path).resolve()
    private=json.loads(private_path.read_text(encoding='utf-8-sig'));binding=private.get('recovered_identity_source',{})
    spec=importlib.util.spec_from_file_location('recovered_task_contract',task/'executor/task_contract.py');contract=importlib.util.module_from_spec(spec);spec.loader.exec_module(contract)
    window=Path(binding.get('source_query_window','')).resolve();accepted=contract.verify_recovered_identity_source(window)
    acceptance=(contract.repo_root()/contract.ROS_V1_RECOVERED_SOURCE_RELATIVE).resolve()
    require(binding==dict(source_query_window=str(window),acceptance_path=str(acceptance),acceptance_sha256=digest(acceptance)),'Recovered source binding drift')
    spec=importlib.util.spec_from_file_location('recovered_route_parser',task/'executor-route-query/route_query.py');parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
    parsed=parser.correlate(*(parser.decode(window/side/'directory-query/stdout.log') for side in 'AB'))
    require(parsed['pass'] and private.get('peer')==parsed['peer'],'Recovered source differs from original raw route crossmatch')
    config=json.loads((window/'window.json').read_text(encoding='utf-8-sig'))
    authority=Path(contract.PRIVATE['task_authority_root']).resolve();claim=authority/'ros2/task-ledger'/('query-'+str(config['ros_attempt'])+'.json')
    require(json.loads(claim.read_text(encoding='utf-8-sig'))==contract.ros_query_claim_row(config,False),'Original consumed query claim drift')
    files=dict(accepted['proof_files_sha256']);files[str(acceptance)]=digest(acceptance);files[str(claim)]=digest(claim);files[str(private_path)]=digest(private_path)
    for path in (Path(__file__).resolve(),task/'executor/task_contract.py',task/'executor-route-query/route_query.py'):files[str(path)]=digest(path)
    return dict(schema=1,query_run=config['run'],private_receipt=str(private_path),source_query_window=str(window),peer=parsed['peer'],local_id_sha256={side:hashlib.sha256(parsed['boards'][side]['local'].encode()).hexdigest() for side in 'AB'},proof_files_sha256=files,selection='ORIGINAL_CONSUMED_QUERY_RAW_CROSSMATCH_WITH_INDEPENDENT_APPENDED_EXPORT_RESTORATION')

def load_replayed_binding(private_path):
    task=Path(__file__).resolve().parent.parent;directory=Path(private_path).parent
    private=json.loads(Path(private_path).read_text(encoding='utf-8-sig'));public=json.loads((directory/'ROUTE_QUERY_RESULT.json').read_text(encoding='utf-8-sig'));provenance=json.loads((directory/'REPLAY_PROVENANCE.json').read_text(encoding='utf-8-sig'))
    require(private.get('replay')==public.get('replay')==provenance,'Replay provenance mismatch')
    spec=importlib.util.spec_from_file_location('replayed_query_receipt',task/'executor-route-query/query_receipt.py');receipt=importlib.util.module_from_spec(spec);spec.loader.exec_module(receipt)
    verified=receipt.verify_completed_query(provenance['source_query_window'],provenance.get('archive_manifest'))
    require(verified==provenance,'Original evidence or replay validator drift')
    spec=importlib.util.spec_from_file_location('replayed_parser',task/'executor-route-query/route_query.py');parser=importlib.util.module_from_spec(spec);spec.loader.exec_module(parser)
    window=Path(provenance['source_query_window']);parsed=parser.correlate(*(parser.decode(window/s/'directory-query/stdout.log') for s in 'AB'))
    require(parsed['pass'] and private.get('pass') is True and public.get('pass') is True and private.get('errors')==public.get('errors')==[],'Replay text failed')
    require(private.get('run')==public.get('run')==provenance['query_run'] and private.get('scope')==public.get('scope')=='CURRENT_PUBLIC_DIRECTORY_NO_SOCKET_DDS_NOT_RUN','Replay run/scope mismatch')
    require(private.get('peer')==parsed['peer'] and private.get('boards')==parsed['boards'],'Replay selection mismatch')
    expected={s:hashlib.sha256(parsed['boards'][s]['local'].encode()).hexdigest() for s in 'AB'}
    require(public.get('local_id_sha256')==expected,'Replay public identity mismatch')
    source_sha={s:digest(window/s/'directory-query/stdout.log') for s in 'AB'}
    require(private.get('source_sha256')==public.get('source_sha256')==source_sha,'Replay raw hash mismatch')
    files=dict(provenance['original_files_sha256']);files.update(provenance['replay_validator_sha256'])
    for p in (Path(private_path),directory/'ROUTE_QUERY_RESULT.json',directory/'REPLAY_PROVENANCE.json',Path(__file__).resolve()):files[str(p)]=digest(p)
    return dict(schema=1,query_run=provenance['query_run'],source_query_window=str(window),query_for_cycles=provenance['query_for_cycles'],private_receipt=str(Path(private_path).resolve()),peer=parsed['peer'],local_id_sha256=expected,proof_files_sha256=files,selection='OPPOSITE_ACTUAL_LOCAL_ID_EXACTLY_ONCE_IN_GUARDED_PUBLIC_QUERY')

def verify_frozen(proof,peer):
    require(isinstance(proof,dict) and proof.get('schema')==1,'Invalid frozen route proof')
    for path,sha in proof['proof_files_sha256'].items(): require(digest(path)==sha,'Frozen route proof changed')
    refreshed=load_route_binding(proof['private_receipt'])
    require(refreshed==proof and peer==proof['peer'],'Configured peer differs from frozen exact selection')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--check-window',required=True,type=Path);args=parser.parse_args()
    cfg=json.loads(args.check_window.read_text(encoding='utf-8-sig'))
    verify_frozen(cfg['route_binding'],cfg['peer'])
    print(json.dumps({'route_binding':'PASS','query_run':cfg['route_binding']['query_run'],'local_id_sha256':cfg['route_binding']['local_id_sha256']}))
