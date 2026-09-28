"""Freeze a fresh registered public directory query window; no device access."""
import argparse,hashlib,json,secrets,shutil,importlib.util
from pathlib import Path

QUERY_SHA256='d01365fc20d70206a1ddeb32c5c278642125f5c1eb49f80754b2d1c8350a172b'
PERMISSION_SHA256='c99bd1641c7802acd11e70bdd6da6cc86fd08380b19fe56694ca37837de5a0f2'
def digest(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def prepare(destination,preflight_path,provenance,contract_path=None,query_cycles=None):
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
    if digest(query)!=QUERY_SHA256 or query.stat().st_size!=257536: raise ValueError('Query helper identity mismatch')
    if digest(helper)!=PERMISSION_SHA256 or preflight['permission_helper']['sha256']!=PERMISSION_SHA256: raise ValueError('Permission helper identity mismatch')
    provenance_sha=digest(provenance)
    run=secrets.token_hex(8)
    contract.assert_fresh_run(run)
    config={'task_contract':task_contract,'run':run,'scenario':'readonly-directory-query','repo':str(repo),
      'window_root':str(destination),
      'package':str(destination/'helpers'),'eligible_for_executor_gate':True,
      'preflight_config':str(preflight_path),'preflight_config_sha256':digest(preflight_path),
      'permission_helper':{'path':str(helper),'sha256':PERMISSION_SHA256},
      'query_helper':{'path':str(query),'sha256':QUERY_SHA256},
      'helper_provenance':str(provenance),'helper_provenance_sha256':provenance_sha,
      'package_artifacts':[{'name':'native_permission_probe','sha256':PERMISSION_SHA256},{'name':'softbus_record_probe','sha256':QUERY_SHA256}],
      'platform':preflight['platform'],'system_library_dirs':contract.LIBDIRS,
      'targets':contract.TARGETS,'hdc_path':contract.PRIVATE['hdc_path'],'private_config_sha256':digest(here.parent/'PRIVATE_CONFIG.json'),'permission_baseline_sha256':contract.PRIVATE['permission_baseline_sha256'],
      'remote':{side:f'/data/local/tmp/mdds_dds_{run}_{side}' for side in ['A','B']},
      'token':contract.TOKENS,
      'analyzer_sha256':digest(repo/'scripts/public_socket/analyze_shared.py'),'shared_script_sha256':{'NativePermissionGuard.ps1':digest(repo/'scripts/NativePermissionGuard.ps1')},
      'contract_validator_sha256':digest(here.parent/'executor/task_contract.py'),
      'executor_sha256':{name:digest(here/name) for name in ['run-window.ps1','route_query.py','prepare-query.py','launch.ps1']},
      'command_contract':'--list-nodes --expected-token <board-token> --preflight-confirmed',
      'scope':'CURRENT_PUBLIC_DIRECTORY_ONLY; NO_SOCKET; DDS_NOT_RUN'}
    frozen=json.loads(Path(task_contract['freeze_path']).read_text())
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
    p=argparse.ArgumentParser();p.add_argument('destination',type=Path);p.add_argument('--preflight-config',type=Path,required=True);p.add_argument('--helper-provenance',type=Path,required=True);p.add_argument('--contract',type=Path);p.add_argument('--for-cycles',type=int,choices=[10,100])
    a=p.parse_args();print(json.dumps(prepare(a.destination,a.preflight_config,a.helper_provenance,a.contract,a.for_cycles)))
