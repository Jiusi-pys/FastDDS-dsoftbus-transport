"""Freeze a fresh registered public directory query window; no device access."""
import argparse,hashlib,json,secrets,shutil,importlib.util
from pathlib import Path

QUERY_SHA256='d01365fc20d70206a1ddeb32c5c278642125f5c1eb49f80754b2d1c8350a172b'
PERMISSION_SHA256='c99bd1641c7802acd11e70bdd6da6cc86fd08380b19fe56694ca37837de5a0f2'
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def prepare(destination,preflight_path,provenance,contract_path=None,query_cycles=None,retry=None):
    if query_cycles is not None and (type(query_cycles) is not int or query_cycles not in (10,100)):raise ValueError('Invalid explicit query purpose')
    repo=Path.cwd().resolve();here=Path(__file__).resolve().parent
    destination=Path(destination).resolve();preflight_path=Path(preflight_path).resolve();provenance=Path(provenance).resolve()
    if destination==here.parent or not destination.is_relative_to(here.parent):raise ValueError('Query destination must be a fresh directory within this task')
    if destination.exists():raise ValueError('Query destination already exists; no window reuse')
    spec=importlib.util.spec_from_file_location('runtime_query_contract',here.parent/'executor/task_contract.py');contract=importlib.util.module_from_spec(spec);spec.loader.exec_module(contract)
    task_contract=contract.load(contract_path)
    if repo!=Path(contract.PRIVATE['repo_root']).resolve():raise ValueError('Repository cwd differs from explicit task binding')
    preflight=json.loads(preflight_path.read_text(encoding='utf-8-sig'))
    if preflight.get('eligible_for_executor_gate') is not True: raise ValueError('Current two-board preflight not eligible')
    if not preflight_path.is_relative_to(here.parent):raise ValueError('Fresh preflight must belong to this task')
    query=Path(contract.PRIVATE['query_helper_path']);helper=Path(preflight['permission_helper']['path'])
    frozen=json.loads(Path(task_contract['freeze_path']).read_text())
    identity=frozen.get('identity_qualification') is True
    query_sha=QUERY_SHA256;query_size=257536
    if identity:
        successor=contract.verify_v1_successor(frozen,contract.verify_ros_task_grant(frozen))
        if successor is None:raise ValueError('Identity qualification requires explicit V1 successor')
        bound=frozen['identity_query_helper'];query=Path(bound['path']);query_sha=bound['sha256'];query_size=bound['size']
    if digest(query)!=query_sha or query.stat().st_size!=query_size: raise ValueError('Query helper identity mismatch')
    if digest(helper)!=PERMISSION_SHA256 or preflight['permission_helper']['sha256']!=PERMISSION_SHA256: raise ValueError('Permission helper identity mismatch')
    provenance_sha=digest(provenance)
    run=secrets.token_hex(8)
    contract.assert_fresh_run(run)
    config={'task_contract':task_contract,'run':run,'scenario':'readonly-directory-query','repo':str(repo),
      'window_root':str(destination),
      'package':str(destination/'helpers'),'eligible_for_executor_gate':True,
      'preflight_config':str(preflight_path),'preflight_config_sha256':digest(preflight_path),
      'permission_helper':{'path':str(helper),'sha256':PERMISSION_SHA256},
      'query_helper':{'path':str(query),'sha256':query_sha},
      'helper_provenance':str(provenance),'helper_provenance_sha256':provenance_sha,
      'package_artifacts':[{'name':'native_permission_probe','sha256':PERMISSION_SHA256},{'name':'softbus_record_probe','sha256':query_sha}],
      'platform':preflight['platform'],'system_library_dirs':contract.LIBDIRS,
      'targets':contract.TARGETS,'hdc_path':contract.PRIVATE['hdc_path'],'private_config_sha256':digest(here.parent/'PRIVATE_CONFIG.json'),'permission_baseline_sha256':contract.PRIVATE['permission_baseline_sha256'],
      'remote':{side:f'/data/local/tmp/mdds_dds_{run}_{side}' for side in ['A','B']},
      'token':contract.TOKENS,
      'analyzer_sha256':digest(repo/'scripts/public_socket/analyze_shared.py'),'shared_script_sha256':{'NativePermissionGuard.ps1':digest(repo/'scripts/NativePermissionGuard.ps1')},
      'contract_validator_sha256':digest(here.parent/'executor/task_contract.py'),
      'executor_sha256':{name:digest(here/name) for name in ['run-window.ps1','route_query.py','prepare-query.py','launch.ps1']},
      'command_contract':'--list-nodes --expected-token <board-token> --preflight-confirmed',
      'scope':'CURRENT_PUBLIC_DIRECTORY_ONLY; NO_SOCKET; DDS_NOT_RUN'}
    if identity:
        config['identity_qualification']=True
        config['private_identity_output']={side:config['remote'][side]+'/local-udid.bin' for side in 'AB'}
        config['command_contract']='--list-nodes --check-identities --identity-output <exact-window>/local-udid.bin --expected-token <board-token> --preflight-confirmed'
    frozen=json.loads(Path(task_contract['freeze_path']).read_text())
    if frozen['scope']==contract.layered().SCOPE:
        config['work_package']=contract.layered().SCOPE;config['query_for_scenario']=contract.layered().SCENARIO
    if frozen['scope']==contract.ROS_SCOPE:
        config['work_package']=contract.ROS_SCOPE;config['query_for_scenario']=contract.ROS_SCENARIO
        config['ros_attempt']=1 if frozen.get('local_contract_test') is True else contract.ros_next_attempt()
        if contract.ros_task_mode(frozen):config.update(ros_task_id=frozen['task_id'],ros_authorization_mode=contract.ROS_TASK_AUTHORIZATION,task_authority_root=frozen['task_authority_root'])
        if retry:config['retry_evidence']={'path':str(Path(retry).resolve()),'sha256':digest(retry)}
    if frozen['scope']==contract.INSTALLED_SCOPE and frozen.get('local_contract_test') is not True:
        config['installed_attempt']=contract.installed_next_attempt()
        if retry:config['retry_evidence']={'path':str(Path(retry).resolve()),'sha256':digest(retry)}
    if frozen['scope']==contract.OBSERVED_SCOPE:
        config['work_package']=frozen['scope'];config['query_for_cycles']=query_cycles
        if query_cycles==100:
            acceptance=Path(frozen['n10_acceptance_path']);config['n10_acceptance']={'path':str(acceptance.resolve()),'sha256':digest(acceptance)}
    contract.verify_query(config)
    destination.mkdir(parents=True,exist_ok=False)
    (destination/'helpers').mkdir()
    for side in ['A','B']: (destination/side).mkdir()
    shutil.copyfile(query,destination/'helpers/softbus_record_probe')
    shutil.copyfile(helper,destination/'helpers/native_permission_probe')
    with (destination/'window.json').open('x',encoding='utf-8') as stream: json.dump(config,stream,indent=2)
    return {'run':run,'window':str(destination),'status':'FROZEN_NOT_EXECUTED','scope':config['scope']}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('destination',type=Path);p.add_argument('--preflight-config',type=Path,required=True);p.add_argument('--helper-provenance',type=Path,required=True);p.add_argument('--contract',type=Path);p.add_argument('--for-cycles',type=int,choices=[10,100]);p.add_argument('--retry',type=Path)
    a=p.parse_args();print(json.dumps(prepare(a.destination,a.preflight_config,a.helper_provenance,a.contract,a.for_cycles,a.retry)))
