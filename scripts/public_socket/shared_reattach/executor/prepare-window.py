"""Offline preparation of one exact new probe window; never deploys or executes."""
from pathlib import Path
import argparse,json,hashlib,secrets,shutil,importlib.util
import task_contract as contract
def digest(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def prepare(mode,build,destination,preflight,route=None,retry=None,contract_path=None,cycles=1):
    build=Path(build).resolve();destination=Path(destination).resolve();preflight=Path(preflight).resolve();here=Path(__file__).resolve().parent;root=here.parent;repo=Path(contract.PRIVATE['repo_root']).resolve()
    if destination.exists() or not destination.is_relative_to(root/'windows'):raise ValueError('Fresh current-task window required')
    if not preflight.is_relative_to(root):raise ValueError('Current task preflight required')
    proof=contract.load(contract_path);frozen=json.loads(Path(proof['freeze_path']).read_text());pre=json.loads(preflight.read_text());manifest_path=build/'build-manifest.json';manifest=json.loads(manifest_path.read_text());package=build/'package'
    if not pre['eligible_for_executor_gate'] or any(manifest[k]!=0 for k in ['bridgeExit','configureExit','buildExit']):raise ValueError('Preflight/build not eligible')
    artifacts=[{'name':e['name'],'sha256':e['sha256'],'source':e['path']} for e in manifest['artifacts']]
    if len(artifacts)!=6 or {e['name'] for e in artifacts}!={p.name for p in package.iterdir()}:raise ValueError('Package members differ')
    for e in artifacts:
        if digest(package/e['name'])!=e['sha256'] or digest(e['source'])!=e['sha256']:raise ValueError('Package hash mismatch')
    if digest(build/'source-state.json')!=manifest['sourceStateSha256']:raise ValueError('Source receipt changed')
    route_proof=None;peer={}
    if mode in contract.CASES:
        if not route or not Path(route).resolve().is_relative_to(root):raise ValueError('Current-task guarded route query required')
        spec=importlib.util.spec_from_file_location('runtime_route',here/'route_binding.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);route_proof=m.load_route_binding(route);peer=route_proof['peer']
    run=secrets.token_hex(8)
    cfg={'work_package':frozen['scope'],'task_contract':proof,'run':run,'scenario':mode,'cycles':cycles,'delete_first':'A' if mode=='SHARED_W2' else 'B','repo':str(repo),'window_root':str(destination),
         'package':str(package),'package_artifacts':artifacts,'main_sha256':digest(package/'public_socket_dds_experiment'),
         'build_manifest':str(manifest_path),'build_manifest_sha256':digest(manifest_path),
         'source_state':str(build/'source-state.json'),'source_state_sha256':digest(build/'source-state.json'),
         'preflight_config':str(preflight),'preflight_config_sha256':digest(preflight),'eligible_for_executor_gate':True,
         'permission_helper':pre['permission_helper'],'platform':pre['platform'],'required_platform_paths':pre['required_platform_paths'],
         'header_sha256':frozen['header_sha256'],
         'system_library_dirs':contract.LIBDIRS,'targets':contract.TARGETS,'token':contract.TOKENS,
         'remote':{s:f'/data/local/tmp/mdds_dds_{run}_{s}' for s in ['A','B']},'peer':peer,
         'hdc_path':contract.PRIVATE['hdc_path'],'private_config_sha256':digest(root/'PRIVATE_CONFIG.json'),'permission_baseline_sha256':contract.PRIVATE['permission_baseline_sha256'],
         'expected_writes':{'A':3,'B':0} if mode=='F1M' else {'A':15*cycles,'B':15*cycles},'record_count':3 if mode=='F1M' else 30*cycles,'payload_bytes':1048576 if mode=='F1M' else 32,'max_record_bytes':8192,'timeout_seconds':180+330*cycles if cycles>1 else 180,'stdout_budget':25165824,
         'analyzer_sha256':digest(repo/'scripts/public_socket/analyze_shared.py'),'shared_script_sha256':{'NativePermissionGuard.ps1':digest(repo/'scripts/NativePermissionGuard.ps1')},
         'executor_sha256':{n:digest(here/n) for n in ['run-window.ps1','runner-template.cjs','shared-control.cjs','record-pair.cjs','bounded-output.cjs','capture-load.sh','observe-native.cjs','load_origin.py','route_binding.py','task_contract.py','private_config.py','prepare-window.py']}}
    if frozen['scope']==contract.OBSERVED_SCOPE and cycles==100:
        acceptance=Path(frozen['n10_acceptance_path']);cfg['n10_acceptance']={'path':str(acceptance.resolve()),'sha256':digest(acceptance)}
    if route_proof:cfg['route_binding']=route_proof
    if retry:cfg['retry_evidence']={'path':str(Path(retry).resolve()),'sha256':digest(retry)}
    contract.verify_window(cfg)
    destination.mkdir(parents=True,exist_ok=False)
    for source,name in [('runner-template.cjs','runner.cjs'),('shared-control.cjs','shared-control.cjs'),('observe-native.cjs','observe-native.cjs'),('record-pair.cjs','record-pair.cjs'),('bounded-output.cjs','bounded-output.cjs')]:shutil.copyfile(here/source,destination/name)
    for side in ['A','B']:
        aux=destination/side/'aux';aux.mkdir(parents=True)
        shutil.copyfile(here/'capture-load.sh',aux/'capture-load.sh');shutil.copyfile(pre['permission_helper']['path'],aux/'native_permission_probe')
        (aux/'required-load-paths.txt').write_text('\n'.join([cfg['remote'][side]+'/'+e['name'] for e in artifacts]+cfg['required_platform_paths'])+'\n',newline='\n')
    cfg['auxiliary_sha256']={s:{p.name:digest(p) for p in (destination/s/'aux').iterdir()} for s in ['A','B']}
    (destination/'window.json').write_text(json.dumps(cfg,indent=2));return {'run':run,'window':str(destination),'status':'PREPARED_NOT_EXECUTED'}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=contract.CASES);p.add_argument('--build',required=True);p.add_argument('--destination',required=True);p.add_argument('--preflight',required=True);p.add_argument('--route');p.add_argument('--retry');p.add_argument('--contract');p.add_argument('--cycles',type=int,default=1);a=p.parse_args();print(json.dumps(prepare(a.mode,a.build,a.destination,a.preflight,a.route,a.retry,a.contract,a.cycles)))
