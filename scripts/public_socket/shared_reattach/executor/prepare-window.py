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
    if {e['name'] for e in artifacts}!={q.name for q in package.iterdir()}:raise ValueError('Package members differ')
    if len(artifacts)!=len({e['name'] for e in artifacts}):raise ValueError('Duplicate package member')
    installed=mode==contract.INSTALLED_SCENARIO
    ros=mode==contract.ROS_SCENARIO
    dds214=mode==contract.layered().SCENARIO
    if not installed and not ros and not dds214 and len(artifacts)!=6:raise ValueError('Package members differ')
    main_member=manifest.get('main') if installed or ros or dds214 else 'public_socket_dds_experiment'
    if dds214 and main_member!=contract.layered().MAIN:raise ValueError('DDS214 scene requires its installed direct DDS app')
    if ros and main_member!='ordinary_ros2_transport_consumer':raise ValueError('ROS scenario requires its installed ordinary app')
    if installed and (not isinstance(main_member,str) or main_member not in {e['name'] for e in artifacts}):raise ValueError('Installed package needs an explicit main member')
    if installed and main_member!=contract.INSTALLED_MAIN:raise ValueError('Installed scenario requires the installed consumer as main member')
    for e in artifacts:
        if digest(package/e['name'])!=e['sha256'] or digest(e['source'])!=e['sha256']:raise ValueError('Package hash mismatch')
    if digest(build/'source-state.json')!=manifest['sourceStateSha256']:raise ValueError('Source receipt changed')
    route_proof=None;peer={}
    if mode in contract.CASES:
        if not route or not Path(route).resolve().is_relative_to(root):raise ValueError('Current-task guarded route query required')
        spec=importlib.util.spec_from_file_location('runtime_route',here/'route_binding.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);route_proof=m.load_route_binding(route);peer=route_proof['peer']
    run=secrets.token_hex(8)
    cfg={'work_package':frozen['scope'],'task_contract':proof,'run':run,'scenario':mode,'cycles':cycles,'delete_first':'A' if mode=='SHARED_W2' else 'B','repo':str(repo),'window_root':str(destination),
         'package':str(package),'package_artifacts':artifacts,'main_sha256':digest(package/main_member),
         'build_manifest':str(manifest_path),'build_manifest_sha256':digest(manifest_path),
         'source_state':str(build/'source-state.json'),'source_state_sha256':digest(build/'source-state.json'),
         'preflight_config':str(preflight),'preflight_config_sha256':digest(preflight),'eligible_for_executor_gate':True,
         'permission_helper':pre['permission_helper'],'platform':pre['platform'],'required_platform_paths':pre['required_platform_paths'],
         'header_sha256':frozen['header_sha256'],
         'system_library_dirs':contract.LIBDIRS,'targets':contract.TARGETS,'token':contract.TOKENS,
         'remote':{s:f'/data/local/tmp/mdds_dds_{run}_{s}' for s in ['A','B']},'peer':peer,
         'hdc_path':contract.PRIVATE['hdc_path'],'private_config_sha256':digest(root/'PRIVATE_CONFIG.json'),'permission_baseline_sha256':contract.PRIVATE['permission_baseline_sha256'],
         'stdout_budget':25165824,
         'analyzer_sha256':digest(repo/'scripts/public_socket/analyze_shared.py'),'shared_script_sha256':{'NativePermissionGuard.ps1':digest(repo/'scripts/NativePermissionGuard.ps1')},
         'executor_sha256':{n:digest(here/n) for n in ['run-window.ps1','runner-template.cjs','shared-control.cjs','record-pair.cjs','bounded-output.cjs','capture-load.sh','observe-native.cjs','load_origin.py','route_binding.py','task_contract.py','private_config.py','prepare-window.py']}}
    if dds214:
        if cycles!=1:raise ValueError('DDS214 scene has no cycles')
        cfg.pop('cycles',None);cfg.pop('delete_first',None)
        cfg.update(main_member=main_member,main_prefix='DDS214_MAIN',main_mode='normal',stages=list(contract.layered().STAGES),
                   service_prefix=contract.PRIVATE['ros2_service_prefix'],package_name=contract.PRIVATE['ros2_package_name'],
                   expected_writes={'A':5,'B':5},record_count=10,payload_bytes=32,max_record_bytes=8192,timeout_seconds=240)
        cfg['analyzer_sha256']=digest(repo/'scripts/public_socket/analyze_dds214.py')
        if frozen.get('local_contract_test') is True:cfg['simulated']=True
    elif ros:
        if cycles!=1:raise ValueError('ROS scene has no cycle dimension')
        cfg.pop('cycles',None);cfg.pop('delete_first',None)
        cfg['analyzer_sha256']=digest(repo/'scripts/public_socket/analyze_ros2.py')
        cfg['native_analyzer_sha256']=digest(repo/'scripts/public_socket/analyze_ros2_native.py')
        cfg.update(scenario=mode,work_package=contract.ROS_SCOPE,main_member=main_member,main_prefix='ROS_MAIN',main_mode='dual',
                   ros_attempt=1 if frozen.get('local_contract_test') is True else contract.ros_next_attempt(),
                   stages=list(contract.ROS_STAGES),service_prefix=contract.PRIVATE['ros2_service_prefix'],
                   package_name=contract.PRIVATE['ros2_package_name'],expected_writes={'A':10,'B':10},record_count=20,
                   payload_bytes=128,max_record_bytes=8192,timeout_seconds=300)
        if frozen.get('bounded_native_reaccept'):
            cfg['bounded_native_reaccept']=frozen['bounded_native_reaccept'];cfg['stages']=list(contract.ROS_NATIVE_STAGES)
            cfg['analyzer_sha256']=digest(repo/'scripts/public_socket/analyze_bounded_native_reaccept.py')
            cfg['executor_sha256']['bounded-native-scene.cjs']=digest(here/'bounded-native-scene.cjs')
        if frozen.get('v1_successor_authorization'):
            v1=contract.PRIVATE.get('v1_deployment')
            if not v1 or frozen.get('identity_qualification') is True:raise ValueError('V1 ROS requires verified private identity/configuration, not query prerequisite contract')
            cfg['v1_deployment']=v1
            cfg['v1_locator_analyzer']=frozen['v1_locator_analyzer']
        if frozen.get('local_contract_test') is True:cfg['simulated']=True
        if contract.ros_task_mode(frozen):cfg.update(ros_task_id=frozen['task_id'],ros_authorization_mode=contract.ROS_TASK_AUTHORIZATION,task_authority_root=frozen['task_authority_root'])
        if frozen.get('paired_runtime_close') is True:cfg['paired_runtime_close']=True
        if frozen.get('rmw_debugger'):
            cfg['rmw_debugger']=frozen['rmw_debugger'];cfg['require_residual_snapshot']=True
            cfg['executor_sha256'].update({n:digest(here/n) for n in ('rmw-debugger.cjs','rmw_debugger.py','rmw_lldb_trace.py')})
    elif installed:
        if cycles!=1:raise ValueError('Installed runtime window takes no cycles')
        if frozen.get('local_contract_test') is not True:cfg['installed_attempt']=contract.installed_next_attempt()
        cfg.pop('cycles',None);cfg.pop('delete_first',None)
        cfg.update(scenario=mode,work_package=frozen['scope'],main_member=main_member,stages=list(contract.INSTALLED_STAGES),
                   service_prefix=contract.PRIVATE['installed_service_prefix'],package_name=contract.PRIVATE['installed_package_name'],
                   expected_writes={'A':contract.INSTALLED_LIMITS['per_side_writes'],'B':contract.INSTALLED_LIMITS['per_side_writes']},
                   record_count=2*contract.INSTALLED_LIMITS['per_side_writes'],payload_bytes=contract.INSTALLED_LIMITS['payload_bytes'],
                   max_record_bytes=contract.INSTALLED_LIMITS['max_record_bytes'],timeout_seconds=180,
                   main_prefix='INST_MAIN',main_mode='installed_runtime')
        # Mode marker. The validator compares it against the frozen contract mode, so
        # it must be written exactly when the contract is a local contract test and
        # never otherwise: a device window that carried simulated=true would be
        # refused, and a local window that omitted it could not be told apart from a
        # device one. This is the only producer of the field.
        if frozen.get('local_contract_test') is True:
            cfg['simulated']=True
        elif 'simulated' in cfg:
            raise ValueError('Device window may not carry the local simulation marker')
    else:
        cfg.update({'expected_writes':{'A':3,'B':0} if mode=='F1M' else {'A':15*cycles,'B':15*cycles},'record_count':3 if mode=='F1M' else 30*cycles,'payload_bytes':1048576 if mode=='F1M' else 32,'max_record_bytes':8192,'timeout_seconds':180+330*cycles if cycles>1 else 180})
    if frozen['scope']==contract.OBSERVED_SCOPE and cycles==100:
        acceptance=Path(frozen['n10_acceptance_path']);cfg['n10_acceptance']={'path':str(acceptance.resolve()),'sha256':digest(acceptance)}
    if route_proof:cfg['route_binding']=route_proof
    if retry:cfg['retry_evidence']={'path':str(Path(retry).resolve()),'sha256':digest(retry)}
    cfg['executor_sha256'].update({n:digest(here/n) for n in ('dds214-control.cjs','layered_contract.py')})
    contract.verify_window(cfg)
    destination.mkdir(parents=True,exist_ok=False)
    if cfg.get('bounded_native_reaccept'):shutil.copyfile(here/'bounded-native-scene.cjs',destination/'bounded-native-scene.cjs')
    for source,name in [('runner-template.cjs','runner.cjs'),('dds214-control.cjs','dds214-control.cjs'),('ros-control.cjs','ros-control.cjs'),('shared-control.cjs','shared-control.cjs'),('observe-native.cjs','observe-native.cjs'),('record-pair.cjs','record-pair.cjs'),('bounded-output.cjs','bounded-output.cjs'),('rmw-debugger.cjs','rmw-debugger.cjs')]:shutil.copyfile(here/source,destination/name)
    for side in ['A','B']:
        aux=destination/side/'aux';aux.mkdir(parents=True)
        shutil.copyfile(here/'capture-load.sh',aux/'capture-load.sh');shutil.copyfile(pre['permission_helper']['path'],aux/'native_permission_probe')
        if cfg.get('rmw_debugger'):shutil.copyfile(cfg['rmw_debugger']['server']['path'],aux/'lldb-server')
        if ros or dds214:
            platform=[x for x in cfg['required_platform_paths'] if Path(x).name=='libsoftbus_client.z.so']
            if len(platform)!=1:raise ValueError('ROS requires exactly one declared real Socket library source')
            # Assemble the full approved deployment prefix once. V1 TargetBridge
            # derives the canonical listener/peer body and consumes the separately
            # approved concrete initiator inputs; Socket.pkgName adds no prefix.
            values={'bridge_library':cfg['remote'][side]+'/libfastdds_dsoftbus_ros2_target_bridge.so',
                    'fastdds_module':cfg['remote'][side]+'/libfastrtps.so.2.14',
                    'fastcdr_module':cfg['remote'][side]+'/libfastcdr.so.2',
                    'softbus_module':platform[0],'backend':'OHOS_PUBLIC_SOCKET_EXPERIMENTAL',
                    'run':run,'side':'0' if side=='A' else '1','domain':'201',
                    'authorized_package':cfg['package_name'],'peer_network_id':cfg['peer'][side],
                    'service_prefix':cfg['package_name']+'.'+cfg['service_prefix'],'connect_release':cfg['remote'][side]+'/control.LISTENER_READY'}
            if ros and cfg.get('v1_deployment'):
                v=cfg['v1_deployment'];other='B' if side=='A' else 'A'
                values.update(locator_version='V1',native_name_contract='PACKAGE_SCOPED_V1',namespace=v['namespace'],domain=str(v['domain']),profile=v['profile'],local_udid=v['udids'][side],peer_udid=v['udids'][other],service_slot=str(v['slots'][side]),peer_slot=str(v['slots'][other]),local_initiator_name=v['initiator_names'][side],peer_initiator_name=v['initiator_names'][other])
            if ros and cfg.get('paired_runtime_close') is True:
                values['runtime_close_release']=cfg['remote'][side]+'/control.RUNTIME_CLOSE'
            if ros and cfg.get('bounded_native_reaccept'):
                values.update(backend='OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL',native_reaccept_order=cfg['bounded_native_reaccept']['order'],native_reaccept_control=cfg['remote'][side]+'/control.NATIVE_REACCEPT')
            (aux/('dds214.conf' if dds214 else 'ros.conf')).write_text(''.join(k+'='+v+'\n' for k,v in values.items()),encoding='utf-8',newline='\n')
        (aux/'required-load-paths.txt').write_text('\n'.join([cfg['remote'][side]+'/'+e['name'] for e in artifacts]+cfg['required_platform_paths'])+'\n',newline='\n')
    cfg['auxiliary_sha256']={s:{p.name:digest(p) for p in (destination/s/'aux').iterdir()} for s in ['A','B']}
    contract.verify_window(cfg)
    (destination/'window.json').write_text(json.dumps(cfg,indent=2));return {'run':run,'window':str(destination),'status':'PREPARED_NOT_EXECUTED'}
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=contract.CASES);p.add_argument('--build',required=True);p.add_argument('--destination',required=True);p.add_argument('--preflight',required=True);p.add_argument('--route');p.add_argument('--retry');p.add_argument('--contract');p.add_argument('--cycles',type=int,default=1);a=p.parse_args();print(json.dumps(prepare(a.mode,a.build,a.destination,a.preflight,a.route,a.retry,a.contract,a.cycles)))
