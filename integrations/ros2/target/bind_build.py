"""Bind an existing actual target build/install/consumer to executor inputs.

This is a provenance projection from original command receipts, never a build,
device preflight, authorization, or success issuer.
"""
import argparse,hashlib,json,shutil
from pathlib import Path

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def put(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def main():
    p=argparse.ArgumentParser()
    for name in ('repo','package','assembly_receipts','consumer_receipts','assembly_build','consumer_build','output'):
        p.add_argument('--'+name.replace('_','-'),type=Path,required=True)
    a=p.parse_args();repo=a.repo.resolve();receipts=[]
    if a.output.exists():raise ValueError('Fresh binding output required')
    for group in (a.assembly_receipts,a.consumer_receipts):
        for phase in ('configure','build'):
            path=group/(phase+'.json');row=read(path)
            if row.get('exit_code')!=0 or Path(row['cwd']).resolve()!=repo:raise ValueError('Actual successful build command required')
            receipts.append(dict(path=str(path.resolve()),sha256=sha(path),argv=row['argv'],exit_code=row['exit_code']))
    installed=a.assembly_receipts/'install.json'
    if read(installed).get('exit_code')!=0:raise ValueError('Actual install command failed')
    receipts.append(dict(path=str(installed.resolve()),sha256=sha(installed)))
    compiled=[];project_files=set()
    for folder in (a.assembly_build,a.consumer_build):
        database=folder/'compile_commands.json'
        for command in read(database):
            file=Path(command['file']).resolve()
            compiled.append(dict(path=str(file),sha256=sha(file),command=command.get('command'),directory=command['directory']))
            if file.is_relative_to(repo):project_files.add(file)
        receipts.append(dict(path=str(database.resolve()),sha256=sha(database)))
    for path in (repo/'include/fastdds_dsoftbus').rglob('*.hpp'):project_files.add(path.resolve())
    for folder in (repo/'integrations/ros2',repo/'examples/ros2_transport'):
        for path in folder.rglob('*'):
            if path.is_file() and path.suffix in ('.cpp','.hpp','.h','.cmake','.in','.json','.py','.patch'):project_files.add(path.resolve())
    for path in (repo/'integrations/ros2/CMakeLists.txt',repo/'integrations/ros2/target/CMakeLists.txt',repo/'examples/ros2_transport/CMakeLists.txt',repo/'communication_dsoftbus/interfaces/kits/transport/socket.h',repo/'communication_dsoftbus/interfaces/kits/common/softbus_common.h',repo/'communication_dsoftbus/interfaces/kits/transport/trans_type.h'):project_files.add(path.resolve())
    selection=read(a.package/'PACKAGE_INPUTS.json')
    members=selection['members']
    if len({x['name'] for x in members})!=len(members):raise ValueError('Package membership duplicate')
    for row in members:
        if sha(a.package/row['name'])!=row['sha256']:raise ValueError('Package input drift')
    # Record selected actual link map; archives do not appear in live maps.
    link=a.consumer_build/'consumer.map'
    if not link.is_file() or not link.stat().st_size:raise ValueError('Actual consumer link map missing')
    receipts.append(dict(path=str(link.resolve()),sha256=sha(link)))
    a.output.mkdir(parents=True);(a.output/'package').mkdir()
    artifacts=[]
    for row in members:
        source=(a.package/row['name']).resolve();shutil.copyfile(source,a.output/'package'/row['name'])
        artifacts.append(dict(name=row['name'],sha256=row['sha256'],source=str(source),path=str(source)))
    sources=[dict(path=file.relative_to(repo).as_posix(),sha256=sha(file)) for file in sorted(project_files)]
    put(a.output/'source-state.json',sources);put(a.output/'compiled-inputs.json',compiled)
    put(a.output/'BUILD_PROVENANCE.json',dict(scope='ACTUAL_SAVED_BUILD_COMMAND_PROJECTION_NOT_DEVICE_ADMISSION',receipts=receipts,package_selection_sha256=sha(a.package/'PACKAGE_INPUTS.json')))
    put(a.output/'build-manifest.json',dict(scenario='ROS2_DSOFTBUS',main='ordinary_ros2_transport_consumer',abi=dict(fastrtps='2.14.5',cpp_runtime='c++_shared'),bridgeExit=read(a.assembly_receipts/'build.json')['exit_code'],configureExit=read(a.consumer_receipts/'configure.json')['exit_code'],buildExit=read(a.consumer_receipts/'build.json')['exit_code'],sourceStateSha256=sha(a.output/'source-state.json'),artifacts=artifacts,original_command_provenance_sha256=sha(a.output/'BUILD_PROVENANCE.json')))
    print('ACTUAL_BUILD_BOUND members='+str(len(members)))

if __name__=='__main__':main()
