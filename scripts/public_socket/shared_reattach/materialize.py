"""Copy reviewed tools into a fresh explicit local task; never grants or runs a device."""
from pathlib import Path
import argparse,hashlib,json,shutil,importlib.util
CANONICAL=('bounded-output.cjs','record-pair.cjs','observe-native.cjs','load_origin.py','capture-load.sh','rmw_lldb_trace.py')
def materialize(repo,task,profile):
 repo=Path(repo).resolve();task=Path(task).resolve();here=Path(__file__).resolve().parent
 if repo!=here.parents[2]:raise ValueError('Repository must own this exact materializer')
 authority=repo/'.artifacts/ros2-device-acceptance/task-scoped-teardown-20261002'
 task_child=task.parent==authority/'iterations' and task.name.startswith('iteration-') and task.name[10:].isdigit() and task.name[10:]==str(int(task.name[10:])) and int(task.name[10:])>=1
 if (task.parent.parent!=repo/'.artifacts' and not task_child) or task.exists():raise ValueError('Fresh declared task/immutable iteration root required')
 if task_child:
  grant=authority/'AUTHORIZATION.json'
  if hashlib.sha256(grant.read_bytes()).hexdigest()!='1dbadad0591bad3f76ef98a6bab09ff80ee6b75491e54c447410c841b39bea51':raise ValueError('Task materialization original grant anchor')
 config=json.loads(Path(profile).read_text(encoding='utf-8'))
 if not isinstance(config,dict):raise ValueError('Private configuration object required')
 config.update(schema=1,repo_root=str(repo),task_root=str(task))
 if task_child:config.update(task_authority_root=str(authority),execution_iteration_id=int(task.name[10:]))
 spec=importlib.util.spec_from_file_location('private_input_validation',here/'executor/private_config.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.validate_profile(config,task,repo)
 external_files=[dict(path=name,sha256=hashlib.sha256((repo/name).read_bytes()).hexdigest()) for name in ('scripts/NativePermissionGuard.ps1','scripts/public_socket/analyze_shared.py','scripts/public_socket/analyze_ros2.py','scripts/public_socket/analyze_ros2_native.py','scripts/public_socket/analyze_dds214.py')]
 if config.get('bounded_native_authorization'):external_files.append(dict(path='scripts/public_socket/analyze_bounded_native_reaccept.py',sha256=hashlib.sha256((repo/'scripts/public_socket/analyze_bounded_native_reaccept.py').read_bytes()).hexdigest()))
 task.mkdir(parents=True,exist_ok=False)
 manifest=[]
 for folder in ('executor','executor-route-query'):
  (task/folder).mkdir()
  for src in sorted((here/folder).iterdir()):
   if src.is_file() and src.suffix in ('.py','.cjs','.ps1'):
    if src.name=='bounded-native-scene.cjs' and not config.get('bounded_native_authorization'):continue
    dst=task/folder/src.name;shutil.copyfile(src,dst);manifest.append(dict(destination=dst.relative_to(task).as_posix(),source=src.relative_to(repo).as_posix(),sha256=hashlib.sha256(dst.read_bytes()).hexdigest()))
 for name in CANONICAL:
  src=here.parent/name;dst=task/'executor'/name;shutil.copyfile(src,dst);manifest.append(dict(destination=dst.relative_to(task).as_posix(),source=src.relative_to(repo).as_posix(),sha256=hashlib.sha256(dst.read_bytes()).hexdigest()))
 (task/'recovery').mkdir()
 for name in ('pregrant_recovery.py','recovery-guard.ps1','readonly_preflight.py'):
  src=here.parent/name;dst=task/'recovery'/name;shutil.copyfile(src,dst);manifest.append(dict(destination=dst.relative_to(task).as_posix(),source=src.relative_to(repo).as_posix(),sha256=hashlib.sha256(dst.read_bytes()).hexdigest()))
 (task/'windows').mkdir();(task/'PRIVATE_CONFIG.json').write_text(json.dumps(config,indent=2)+'\n',encoding='utf-8')
 receipt=dict(scope='LOCAL_MATERIALIZATION_ONLY_NO_EXECUTION_AUTHORITY',repo=str(repo),task=str(task),files=manifest,external_files=external_files,private_config_sha256=hashlib.sha256((task/'PRIVATE_CONFIG.json').read_bytes()).hexdigest(),device_operations=0)
 (task/'MATERIALIZATION.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');return receipt
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--repo',required=True);p.add_argument('--task',required=True);p.add_argument('--private-config',required=True);a=p.parse_args();print(json.dumps(materialize(a.repo,a.task,a.private_config)))
