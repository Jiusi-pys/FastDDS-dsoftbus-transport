"""Two fixed, newly granted slots; delegates original recovery and device rules."""
from pathlib import Path
import json,re,importlib.util

SCENARIO='DDS214_LAYER_BASELINE'
SCOPE='DDS214_LAYER_BASELINE_ACCEPTANCE'
AUTH='ONE_DDS214_AND_CONDITIONAL_ROS_DEVICE_VALIDATION'
ROS_AUTH='ONE_CONDITIONAL_ROS_AFTER_DDS214_DEVICE_VALIDATION'
LIMITS=dict(canonical_windows=1,repair_retries=0,total_attempts=1,query_pairs_per_attempt=1,total_query_pairs=1,max_record_bytes=8192,per_side_writes=5,per_side_takes=5,stages=4)
STAGES=['LISTENER_READY','MATCH','DATA_DONE','DDS_DELETED']
MAIN='fastdds_dsoftbus_dds214_baseline'
APPROVED_GRANT_RELATIVE='.artifacts/ros2-device-acceptance/layered-device-20261002/AUTHORIZATION.json'
APPROVED_GRANT_SHA256='8efd99ef7da4dc91b7b982ba825ff8ea698d5e6941b04b972154ed1305fd5d55'

def grant(c,f,layer):
    c.need(f.get('authorization')==(AUTH if layer=='DDS214' else ROS_AUTH),'Layered frozen authorization mismatch')
    b=f['layered_authorization'];p=Path(b['path']).resolve()
    c.need(p==(Path(c.PRIVATE['repo_root'])/APPROVED_GRANT_RELATIVE).resolve() and b.get('sha256')==APPROVED_GRANT_SHA256 and c.digest(p)==APPROVED_GRANT_SHA256,'Layered approved grant anchor mismatch')
    c.need(c.digest(p)==b['sha256'],'Layered original grant drift');g=json.loads(p.read_text(encoding='utf-8-sig'))
    c.need(g.get('authorization')==AUTH and g.get('human_approved') is True and g.get('quota_id')==f.get('layered_quota_id'),'Layered grant identity')
    exact={k:dict(query_pair=1,data_attempt=1,repair_retry=0) for k in ('DDS214','ROS2')}
    c.need(g.get('limits')==exact and all(type(v) is int for row in g['limits'].values() for v in row.values()),'Layered grant expansion')
    c.need(Path(g['task_roots'][layer]).resolve()==c.TASK_ROOT and len(set(g['task_roots'].values()))==2,'Layered fixed task roots')
    c.need(g.get('targets')==c.TARGETS and g.get('refund') is False,'Layered target/refund mismatch')
    old=g['historical_ledger'];c.need(c.digest(old['path'])==old['sha256'],'Layered historical ledger drift')
    c.need(f.get('local_contract_test') is not True and f.get('device_authorized') is True,'Layered grant is device-only')
    if layer=='ROS2':
        b=f['dds214_acceptance'];p=Path(b['path']).resolve();ddsroot=Path(g['task_roots']['DDS214']).resolve()
        c.need(p==ddsroot/'DDS214_ACCEPTANCE.json' and c.digest(p)==b['sha256'],'Conditional DDS acceptance drift')
        accepted=json.loads(p.read_text(encoding='utf-8-sig'));w=Path(accepted['window']).resolve()
        c.need(accepted.get('verdict')=='PASS' and accepted.get('scenario')==SCENARIO and w.parent==ddsroot/'windows','Conditional DDS acceptance scope')
        for name,sha in accepted['receipts'].items():
            relative=Path(name);c.need(not relative.is_absolute() and '..' not in relative.parts and c.digest(w/relative)==sha,'Conditional DDS evidence drift')
        c.need({'window.json','result.json','dds214-analysis.json','window-summary.json','load-collection-summary.json','DATA_ATTEMPT_STARTED','INDEPENDENT_ACCEPTANCE.json','A.raw.stdout.log','B.raw.stdout.log','A.raw.stderr.log','B.raw.stderr.log','A/actual-load-origin.json','B/actual-load-origin.json'}<=set(accepted['receipts']),'Conditional DDS receipts incomplete')
        cfg=json.loads((w/'window.json').read_text(encoding='utf-8-sig'));result=json.loads((w/'result.json').read_text(encoding='utf-8-sig'));a=json.loads((w/'dds214-analysis.json').read_text(encoding='utf-8-sig'));s=json.loads((w/'window-summary.json').read_text(encoding='utf-8-sig'));load=json.loads((w/'load-collection-summary.json').read_text(encoding='utf-8-sig'));review=json.loads((w/'INDEPENDENT_ACCEPTANCE.json').read_text(encoding='utf-8-sig'))
        c.need(cfg.get('scenario')==SCENARIO and cfg.get('run')==accepted.get('run') and cfg.get('simulated') is not True and result.get('pass') is True and result.get('timedOut') is False and result.get('activeCollectors')==[],'Conditional DDS execution incomplete')
        c.need(a.get('evidence_scope')=='device' and a.get('pass') is True and a.get('native',{}).get('pass_') is True and a.get('observed_writes')==10 and a.get('observed_takes')==10 and a.get('boards',{}).get('A',{}).get('takes')==5 and a.get('boards',{}).get('B',{}).get('takes')==5,'Conditional DDS functional evidence')
        c.need(s.get('first_failure') is None and s.get('cleanup_failures')==[] and load.get('first_failure') is None and load.get('failures')==[] and review.get('verdict')=='PASS' and review.get('run')==accepted['run'],'Conditional DDS recovery/review')
        analyzer=Path(c.PRIVATE['repo_root'])/'scripts/public_socket/analyze_dds214.py';c.need(c.digest(analyzer)==cfg['analyzer_sha256'],'Conditional DDS analyzer drift')
        spec=importlib.util.spec_from_file_location('conditional_dds_raw_analysis',analyzer);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        texts={side:(w/(side+'.raw.stdout.log')).read_text(encoding='utf-8-sig')+'\n'+(w/(side+'.raw.stderr.log')).read_text(encoding='utf-8-sig') for side in 'AB'}
        c.need(m.analyze(texts,accepted['run'],'normal','device')['pass'] and m.native_correlation(texts,accepted['run'])['pass_'],'Conditional DDS original text rejected')
        c.need(all(json.loads((w/side/'actual-load-origin.json').read_text(encoding='utf-8-sig')).get('complete') is True for side in 'AB'),'Conditional DDS live load incomplete')
        c._require_normal_restored(w)
    return g

def shape(c,x):
    c.verify_private_binding(x);c.assert_fresh_run(x['run'])
    c.need(x.get('work_package')==SCOPE and x.get('main_member')==MAIN and x.get('main_prefix')=='DDS214_MAIN' and x.get('main_mode')=='normal','DDS214 scope/main')
    c.need(not any(k in x for k in ('cycles','delete_first','retry_evidence','shared')),'DDS214 fixed normal window')
    c.need(x['targets']==c.TARGETS and x['token']==c.TOKENS and x['remote']=={s:f"/data/local/tmp/mdds_dds_{x['run']}_{s}" for s in 'AB'},'DDS214 run/target/paths')
    c.need(x.get('expected_writes')=={'A':5,'B':5} and x.get('record_count')==10 and x.get('stages')==STAGES and x.get('timeout_seconds')==240 and x.get('stdout_budget')==25165824 and x.get('max_record_bytes')==8192,'DDS214 sample/budget expansion')
    c.need(x['system_library_dirs']==c.LIBDIRS and set(x['peer'])=={'A','B'} and all(re.fullmatch('[0-9a-f]{64}',v) for v in x['peer'].values()),'DDS214 route/library paths')
    names=[e['name'] for e in x['package_artifacts']];c.need(len(names)==len(set(names)) and MAIN in names and len(names)<=16,'DDS214 exact selected package')
    c.need(not any(any(k in n.lower() for k in ('rclcpp','rmw','3.6','fake')) for n in names),'DDS214 forbidden package variant')
    c.need(all(Path(e['source']).name==e['name'] and c.digest(e['source'])==e['sha256'] for e in x['package_artifacts']),'DDS214 package drift')

def path(c,kind):return c.TASK_ROOT/'dds214/attempt-ledger'/(kind+'-1.json')
def row(c,x):
    f=json.loads(Path(x['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'))
    return dict(scope=SCOPE,kind='DDS214_QUERY_DISPATCH_INTENT',purpose=SCENARIO,run=x['run'],window=str(Path(x['window_root']).resolve()),targets=x['targets'],contract_sha256=x['task_contract']['freeze_sha256'],materialization_sha256=f['materialization_sha256'],source_state_sha256=f['build_binding']['source_state_sha256'],quota_id=f.get('layered_quota_id'),local_contract_test=f.get('local_contract_test') is True)
def budget(c,x,query=False):
    f=json.loads(Path(x['task_contract']['freeze_path']).read_text(encoding='utf-8-sig'))
    if f.get('local_contract_test') is True:
        c.verify_local_driver(f)
        if not query:c.need(x.get('simulated') is True,'DDS214 local window mode mismatch')
    else:grant(c,f,'DDS214');c.verify_installed_device_mode(f,x)
    c.need(not path(c,'query' if query else 'data').exists(),'DDS214 quota already consumed')
    if not query:
        q=json.loads(path(c,'query').read_text(encoding='utf-8-sig'));proof=x.get('route_binding',{})
        c.need(q.get('purpose')==SCENARIO and proof.get('query_run')==q.get('run') and Path(proof['private_receipt']).resolve().parent==Path(q['window']).resolve(),'DDS214 current query required')
        completed_query(c,json.loads((Path(q['window'])/'window.json').read_text(encoding='utf-8-sig')))
        c.require_restored(Path(q['window']).resolve())
def completed_query(c,x):c.need(json.loads(path(c,'query').read_text(encoding='utf-8-sig'))==row(c,x),'DDS214 completed claim mismatch')
def claim(c,x):
    p=path(c,'query');p.parent.mkdir(parents=True,exist_ok=True)
    with p.open('x',encoding='utf-8') as stream:json.dump(row(c,x),stream)
def dispatch(c,x,side):
    c.verify_query_context(x);completed_query(c,x);c.need(side in ('A','B'),'DDS214 query side')
    p=Path(x['window_root'])/('QUERY_DISPATCH_INTENT_'+side+'.json')
    receipt=dict(run=x['run'],side=side,target=x['targets'][side],purpose=SCENARIO,claim_path=str(path(c,'query')),claim_sha256=c.digest(path(c,'query')),contract_sha256=x['task_contract']['freeze_sha256'],driver_path=x['hdc_path'],remote_execution='UNOBSERVED_AT_INTENT')
    with p.open('x',encoding='utf-8') as stream:json.dump(receipt,stream)
