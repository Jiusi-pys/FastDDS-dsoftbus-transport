"""Repository-contained OHOS rebuild. No device, claim or admission operations.

Composes the existing source patches, ROS slice builder, installer and ELF packager.
SDK and Host tools are read-only inputs; all child temporary/output paths are local.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

REPO=Path(__file__).resolve().parents[3]
FDDS='ebcb5c05e9da9e262041e8616bbaff61ab40a296'
PINS={'rmw_fastrtps':'bbbb90a98b65f1f678945ef69273edf8b09200ae',
      'rcl':'22c0b957140035c675f0a061daaab5fd6b2080e7',
      'rclcpp':'3aa906a2c7ad13d1623b31a55731993f56538e72'}
ROS_REPOS=('common_interfaces example_interfaces libyaml_vendor rcl rcl_interfaces '
 'rcl_logging rclcpp rcpputils rcutils rmw rmw_dds_common rmw_fastrtps '
 'rmw_implementation ros2_tracing rosidl rosidl_core rosidl_defaults '
 'rosidl_dynamic_typesupport rosidl_dynamic_typesupport_fastrtps '
 'rosidl_typesupport rosidl_typesupport_fastrtps spdlog_vendor '
 'unique_identifier_msgs').split()

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def write(path,value):
    Path(path).write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')
def build_root(repo,output):
    repo=Path(repo).resolve();root=(Path(output) if output else repo/'build/ros2-target').resolve()
    if not root.is_relative_to(repo/'build') or root==repo/'build':
        raise ValueError('Output must be a contained repository build subdirectory')
    return root
def validate_selection(row,repo,root):
    if row.get('repo')!=str(repo.resolve()) or row.get('root')!=str(root.resolve()):
        raise ValueError('Foreign build selection')
def exact_eol(path,expected):
    path=Path(path);before=path.read_bytes();result=before
    if hashlib.sha256(before).hexdigest()!=expected:
        lf=before.replace(b'\r\n',b'\n')
        matches=[x for x in (lf,lf.replace(b'\n',b'\r\n')) if hashlib.sha256(x).hexdigest()==expected]
        if len(matches)!=1:raise ValueError('Exact source prerequisite mismatch: '+str(path))
        result=matches[0];path.write_bytes(result)
    return dict(path=str(path),before_sha256=hashlib.sha256(before).hexdigest(),after_sha256=expected,changed=before!=result)
def tree_hash(root):
    digest=hashlib.sha256()
    for p in sorted(Path(root).rglob('*')):
        if p.is_file():digest.update((p.relative_to(root).as_posix()+'\0'+sha(p)+'\n').encode())
    return digest.hexdigest()
def verify_archive_tree(archive,root):
    root=Path(root).resolve();expected=set()
    with tarfile.open(archive) as source:
        for entry in source.getmembers():
            if entry.isdir():continue
            path=(root/entry.name).resolve()
            if not entry.isfile() or not path.is_relative_to(root):raise ValueError('Unexpected archive entry')
            expected.add(path.relative_to(root).as_posix())
            if not path.is_file() or hashlib.sha256(source.extractfile(entry).read()).hexdigest()!=sha(path):
                raise ValueError('Archive source bytes changed: '+entry.name)
    if expected!={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}:
        raise ValueError('Archive source membership changed')
def verify_transform_inputs(record,current):
    if record!=current:raise ValueError('Fast DDS transform input drift; choose a fresh contained build root')

class Build:
    def __init__(self,args):
        self.rebuild_packages=args.rebuild_package
        self.root=build_root(REPO,args.output);self.root.mkdir(parents=True,exist_ok=True)
        for leaf in ('tmp','receipts','src','downloads'):
            p=self.root/leaf
            if not p.resolve().is_relative_to(self.root):raise ValueError('Output path escape')
            p.mkdir(exist_ok=True)
        for leaf in ('src/fastdds','src/ros','src/asio','ros','fastdds-build','fastdds-install','transport-build',
                     'transport-install','consumer-source','consumer-build','package','identity','bound-build'):
            if not (self.root/leaf).resolve().is_relative_to(self.root):raise ValueError('Output path escape: '+leaf)
        if any(not p.resolve().is_relative_to(self.root) for p in self.root.rglob('*')):
            raise ValueError('Existing output contains an escaping link')
        self.env=os.environ.copy();tmp=str(self.root/'tmp')
        self.env.update(TEMP=tmp,TMP=tmp,TMPDIR=tmp,PYTHONDONTWRITEBYTECODE='1',CMAKE_BUILD_PARALLEL_LEVEL='4')
        self.cmake=Path(args.cmake).resolve();self.python=Path(args.python).resolve()
        self.sdk=Path(args.sdk).resolve();self.ninja=Path(args.ninja).resolve();self.host=Path(args.host_tools).resolve()
        self.env['PATH']=os.pathsep.join([str(self.python.parent),str(self.python.parent/'Scripts'),str(self.python.parent/'Library/bin'),self.env.get('PATH','')])
        self.env['PYTHONPATH']=str(self.host/'Lib/site-packages')
        self.base=REPO/'.artifacts/ros2-device-acceptance/run-20260930-local/target-environment'
        self.deps=[self.base/('sdk23-shared-prefix-'+name) for name in ('fastcdr-225','tinyxml2-6','foonathan-074','ros-bootstrap')]
        self.fast=self.root/'fastdds-install';self.ros=self.root/'ros/install';self.transport=self.root/'transport-install'
        self.selection=self.root/'BUILD_SELECTION.json'
        current=dict(repo=str(REPO),root=str(self.root),sdk=str(self.sdk),cmake=str(self.cmake),python=str(self.python),ninja=str(self.ninja),host_tools=str(self.host),
                     dependency_prefixes=[dict(path=str(p),tree_sha256=tree_hash(p)) for p in self.deps],
                     tool_sha256={str(p):sha(p) for p in (self.cmake,self.python,self.ninja,self.sdk/'build/cmake/ohos.toolchain.cmake',self.sdk/'llvm/bin/clang++.exe')})
        for p in (self.cmake,self.python,self.ninja,self.sdk/'build/cmake/ohos.toolchain.cmake'):
            if not p.is_file():raise ValueError('Required installed tool missing: '+str(p))
        if any(not p.is_dir() for p in self.deps):raise ValueError('Declared target dependency prefix missing')
        if self.selection.exists():
            old=read(self.selection);validate_selection(old,REPO,self.root)
            if old!=current:raise ValueError('Selected tools or reused dependency prefix changed')
        else:write(self.selection,current)
    def command(self,label,argv):
        argv=[str(x) for x in argv];folder=self.root/'receipts'
        number=max([int(p.name.split('-')[0]) for p in folder.glob('*.json')]+[0])+1
        base=folder/f'{number:04d}-{label}'
        write(base.with_suffix('.intent.json'),dict(argv=argv,cwd=str(REPO),driver_sha256=sha(__file__),env=dict(TEMP=self.env['TEMP'],TMP=self.env['TMP'],TMPDIR=self.env['TMPDIR'],PYTHONDONTWRITEBYTECODE='1'),scope='LOCAL_TARGET_BUILD_NO_DEVICE'))
        with base.with_suffix('.stdout').open('wb') as out,base.with_suffix('.stderr').open('wb') as err:
            done=subprocess.run(argv,cwd=REPO,env=self.env,stdout=out,stderr=err,shell=False)
        write(base.with_suffix('.json'),dict(argv=argv,cwd=str(REPO),exit_code=done.returncode))
        print(label,done.returncode,flush=True)
        if done.returncode:raise RuntimeError(f'{label} failed; receipt {base}')
        return base.with_suffix('.json')
    def archive(self,name,source,ref):
        destination=self.root/'src'/name;record=self.root/'src'/(name.replace('/','_')+'.source.json')
        commit=subprocess.check_output(['git','-C',str(source),'rev-parse',ref+'^{commit}'],text=True,env=self.env).strip()
        if record.exists():
            row=read(record)
            if row['commit']!=commit or tree_hash(destination)!=row['tree_sha256']:raise ValueError('Source snapshot drift: '+name)
            return destination
        if destination.exists():raise ValueError('Unreceipted source directory: '+str(destination))
        destination.mkdir(parents=True);archive=self.root/'downloads'/(name.replace('/','_')+'.tar')
        self.command('archive-'+name.replace('/','_'),['git','-c','core.autocrlf=false','-C',source,'archive','--format=tar','--output='+str(archive),commit])
        with tarfile.open(archive) as source_tar:source_tar.extractall(destination,filter='data')
        write(record,dict(source_repository=str(source),commit=commit,archive_sha256=sha(archive),tree_sha256=tree_hash(destination)))
        return destination
    def apply_patches(self,src):
        here=REPO/'integrations/ros2';changes=[]
        for row in read(here/'UPSTREAM.json')['files']:changes.append(exact_eol(src/row['path'],row['base_sha256']))
        self.command('factory-patch',[self.python,here/'apply_fastdds_hook.py','--source',src])
        self.command('endpoint-patch',[self.python,here/'apply_endpoint_guard.py','--source',src])
        meta=read(here/'TEARDOWN_UPSTREAM.json')
        for row in meta['files']:changes.append(exact_eol(src/row['path'],row['base_sha256']))
        changes.append(exact_eol(src/'src/cpp/fastdds/domain/DomainParticipantFactory.cpp',meta['factory_after_existing_hook_sha256']))
        self.command('teardown-patch',[self.python,here/'apply_teardown_observation.py','--source',src])
        for row in read(here/'RESIDUAL_UPSTREAM.json')['files']:changes.append(exact_eol(src/row['path'],row['base_sha256']))
        self.command('residual-patch',[self.python,here/'apply_residual_observation.py','--source',src])
        for row in read(here/'TOPIC_RTTI_UPSTREAM.json')['files']:changes.append(exact_eol(src/row['path'],row['base_sha256']))
        self.command('topic-rtti-patch',[self.python,here/'apply_topic_rtti_key.py','--source',src])
        return changes
    def prepare(self):
        patched=self.root/'FAST_DDS_PATCHED.json';here=REPO/'integrations/ros2'
        names=['UPSTREAM.json','TEARDOWN_UPSTREAM.json','RESIDUAL_UPSTREAM.json','TOPIC_RTTI_UPSTREAM.json','apply_fastdds_hook.py','apply_endpoint_guard.py','apply_teardown_observation.py','apply_residual_observation.py','apply_topic_rtti_key.py','fastdds-2.14.5-teardown.patch','FastddsHook.hpp','PlatformModule.hpp','StartupDiagnostics.hpp','TeardownDiagnostics.hpp','ResidualDiagnostics.hpp','NativePrefix.hpp']
        current={n:sha(here/n) for n in names};inputs=self.root/'FAST_DDS_TRANSFORM_INPUTS.json'
        if inputs.exists():verify_transform_inputs(read(inputs)['inputs'],current)
        if not patched.exists():
            src=self.archive('fastdds',REPO/'Fast-DDS',FDDS);changes=self.apply_patches(src)
            write(patched,dict(commit=FDDS,tree_sha256=tree_hash(src),exact_eol_mapping=changes))
            write(inputs,dict(scope='CAPTURED_DURING_SOURCE_PREPARATION',inputs=current))
        elif tree_hash(self.root/'src/fastdds')!=read(patched)['tree_sha256']:raise ValueError('Patched source drift')
        elif not inputs.exists():
            # Existing receipt remains immutable. Prove current transform inputs
            # reproduce its exact tree; this is an appended replay, not a claim
            # that the old build recorded these input hashes at its start.
            with tempfile.TemporaryDirectory(prefix='patch-verify-',dir=self.root/'tmp') as folder:
                with tarfile.open(self.root/'downloads/fastdds.tar') as source:source.extractall(folder,filter='data')
                self.apply_patches(Path(folder))
                if tree_hash(Path(folder))!=read(patched)['tree_sha256']:raise ValueError('Current transforms do not reproduce selected source')
            write(inputs,dict(scope='POST_BUILD_EXACT_SOURCE_REPLAY_NOT_ORIGINAL_INPUT_RECEIPT',inputs=current,prior_source_sha256=sha(patched)))
        for name in ROS_REPOS:self.archive('ros/'+name,REPO/'ros2/src/ros2'/name,PINS.get(name,'HEAD'))
        for group,name in [('ament','ament_index'),('ros-tooling','libstatistics_collector'),('ros','ros_environment')]:
            self.archive('ros/'+name,REPO/'ros2/src'/group/name,'HEAD')
        asio=self.root/'src/asio';identity=read(REPO/'.artifacts/ros2-device-acceptance/run-20260930-local/ASIO_SOURCE_INPUT.json')
        archive=self.root/'downloads/asio.tar.gz'
        if not archive.exists():
            with urllib.request.urlopen(identity['url'],timeout=60) as source,archive.open('xb') as target:shutil.copyfileobj(source,target)
        if sha(archive)!=identity['archive_sha256']:raise ValueError('Pinned Asio archive mismatch')
        if not asio.exists():
            asio.mkdir()
            with tarfile.open(archive) as source:source.extractall(asio,filter='data')
        verify_archive_tree(archive,asio)
        proof=self.root/'ASIO_VERIFICATION.json'
        current_asio=dict(commit=identity['commit'],archive_sha256=sha(archive),tree_sha256=tree_hash(asio))
        if proof.exists() and read(proof)!=current_asio:raise ValueError('Asio selected source changed')
        if not proof.exists():write(proof,current_asio)
        self.asio=asio/('asio-'+identity['commit'])/'asio/include'
        token=REPO/'.artifacts/ros2-device-acceptance/offline-teardown-20261002/target-package/libmdds_runtime_token_bridge.so'
        if sha(token)!='b2ef1d732b72640d91e656b1e138ba7dcce6b700a966d875d72d6992f26cdceb':raise ValueError('Existing identity bridge changed')
        (self.root/'identity').mkdir(exist_ok=True);shutil.copyfile(token,self.root/'identity'/token.name)
    def options(self):
        return ['-G','Ninja','-DCMAKE_MAKE_PROGRAM='+self.ninja.as_posix(),'-DCMAKE_TOOLCHAIN_FILE='+str(self.sdk/'build/cmake/ohos.toolchain.cmake'),
                '-DOHOS_ARCH=arm64-v8a','-DOHOS_STL=c++_shared','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH','-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF',
                '-DCMAKE_PROJECT_INCLUDE='+str(REPO/'integrations/ros2/target/RosTargetBuildOptions.cmake')]
    def configure_build(self,label,source,build,options,install=False):
        for p in (build,):
            if not Path(p).resolve().is_relative_to(self.root):raise ValueError('Build output escape')
        records={}
        records['configure']=self.command(label+'-configure',[self.cmake,'-S',source,'-B',build,*self.options(),*options])
        records['build']=self.command(label+'-build',[self.cmake,'--build',build,'--parallel','4','--verbose'])
        if install:records['install']=self.command(label+'-install',[self.cmake,'--install',build])
        dest=self.root/(label+'-receipts');dest.mkdir(exist_ok=True)
        # Mutable current-selection copies refer to immutable numbered originals.
        for phase,path in records.items():write(dest/(phase+'.json'),dict(read(path),original_receipt=str(path),original_sha256=sha(path)))
    def fastdds(self):
        self.prepare();src=self.root/'src/fastdds'
        self.configure_build('fastdds',src,self.root/'fastdds-build',[
            '-DCMAKE_INSTALL_PREFIX='+str(self.fast),'-DBUILD_SHARED_LIBS=ON','-DSECURITY=OFF','-DFASTDDS_STATISTICS=OFF',
            '-DTHIRDPARTY=OFF','-DTHIRDPARTY_UPDATE=OFF','-DAsio_INCLUDE_DIR='+str(self.asio),
            '-DTHIRDPARTY_BOOST_INCLUDE_DIR='+str(src/'thirdparty/boost/include'),'-DTINYXML2_FROM_SOURCE=OFF',
            '-Dfastcdr_DIR='+str(self.deps[0]/'lib/cmake/fastcdr'),'-DTinyXML2_DIR='+str(self.deps[1]/'lib/cmake/tinyxml2'),
            '-Dfoonathan_memory_DIR='+str(self.deps[2]/'lib/foonathan_memory/cmake'),'-DCOMPILE_TOOLS=ON','-DCOMPILE_EXAMPLES=OFF','-DBUILD_TESTING=OFF'],True)
    def ros_build(self):
        self.prepare()
        args=[self.python,REPO/'integrations/ros2/target/build_ros_slice.py','--source',self.root/'src/ros','--output',self.root/'ros',
              '--target-prefix',self.fast,'--host-tools',self.host,'--sdk',self.sdk,'--cmake',self.cmake,'--python',self.python,'--ninja',self.ninja]
        for p in self.deps:args+=['--dependency-prefix',p]
        for name in self.rebuild_packages:args+=['--package',name,'--rebuild-package',name]
        # rosidl probes these implicit interface dependencies with QUIET;
        # build them explicitly before packages containing srv/action files.
        for name in ('rosidl_typesupport_introspection_c','rosidl_typesupport_introspection_cpp','rosidl_typesupport_c','rosidl_typesupport_cpp',
                     'builtin_interfaces','unique_identifier_msgs','service_msgs','action_msgs','rcl_interfaces',
                     'rmw_fastrtps_cpp','rmw_implementation','rclcpp','std_msgs','example_interfaces'):args+=['--package',name]
        # These interface packages may have been installed before the C/C++
        # dispatch generators existed. Rebuild only when that output is absent.
        for name in ('builtin_interfaces','unique_identifier_msgs','service_msgs','action_msgs','rcl_interfaces'):
            if (self.ros/'share'/name).exists() and not (self.ros/'lib'/('lib'+name+'__rosidl_typesupport_cpp.so')).is_file():
                args+=['--rebuild-package',name]
        if (self.root/'ros').exists():args+=['--resume']
        self.command('ros-slice',args)
    def package_dirs(self):
        spec=importlib.util.spec_from_file_location('ros_slice',Path(__file__).with_name('build_ros_slice.py'));module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        dirs={k:v for k,v in module.package_dirs(self.host).items() if k.startswith('ament_') or k in module.PURE_TOOLS}
        for prefix in [*self.deps,self.fast,self.ros,self.transport]:dirs.update(module.package_dirs(prefix))
        return ['-D'+k+'_DIR:PATH='+v.as_posix() for k,v in sorted(dirs.items())]
    def assembly(self):
        socket=REPO/'.artifacts/breakthrough-20260920/native-path/A-libsoftbus_client.z.so'
        self.configure_build('assembly',REPO/'integrations/ros2/target',self.root/'transport-build',self.package_dirs()+[
            '-DCMAKE_INSTALL_PREFIX='+str(self.transport),'-DDSOFTBUS_SOURCE_DIR='+str(REPO/'communication_dsoftbus'),
            '-DFASTDDS_DSOFTBUS_SOCKET_LIBRARY='+str(socket)],True)
    def consumer(self):
        source=self.root/'consumer-source';installed=self.transport/'share/FastDDSDSoftBusTransportRos2Target/examples/ros2_transport'
        if source.exists():
            if tree_hash(source)!=tree_hash(installed):raise ValueError('Installed consumer source changed; select fresh build root')
        else:shutil.copytree(installed,source)
        prefixes=[self.ros,self.transport,self.fast,*self.deps]
        self.env['AMENT_PREFIX_PATH']=os.pathsep.join(str(p) for p in prefixes)
        self.configure_build('consumer',source,self.root/'consumer-build',self.package_dirs()+[
            '-DPython3_EXECUTABLE='+str(self.python),'-DCMAKE_PREFIX_PATH='+';'.join(str(p) for p in prefixes),
            '-DFastRTPS_INCLUDE_DIR='+str(self.fast/'include'),'-DFastRTPS_LIBRARY_RELEASE='+str(self.fast/'lib/libfastrtps.so'),
            '-DFastCDR_LIBRARY_RELEASE='+str(self.deps[0]/'lib/libfastcdr.so'),
            '-DFASTDDS_DSOFTBUS_TOKEN_BRIDGE_LIBRARY='+str(self.root/'identity/libmdds_runtime_token_bridge.so'),
            '-DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map,'+str(self.root/'consumer-build/consumer.map'),
            '-DAMENT_TEST_RESULTS_DIR='+str(self.root/'consumer-build/test_results')])
    def package(self):
        output=self.root/'package'
        args=[self.python,Path(__file__).with_name('package_elf.py'),'--readelf',self.sdk/'llvm/bin/llvm-readelf.exe',
              '--main',self.root/'consumer-build/ordinary_ros2_transport_consumer','--bridge',self.transport/'lib/libfastdds_dsoftbus_ros2_target_bridge.so','--output',output]
        for p in [self.ros,self.transport,self.fast,*self.deps,self.root/'identity']:args+=['--library-root',p]
        args+=['--library',self.sdk/'llvm/lib/aarch64-linux-ohos/libc++_shared.so']
        for name in ('libaccesstoken_sdk.z.so','libc++.so','libc.so','libsoftbus_client.z.so','libtokensetproc_shared.z.so'):args+=['--external',name]
        self.command('package',args)
        members=read(output/'PACKAGE_INPUTS.json')['members']
        rows=[dict(name=r['name'],path=str((output/r['name']).resolve()),characters=len(str((output/r['name']).resolve())),sha256=sha(output/r['name'])) for r in members]
        write(self.root/'PACKAGE_SEND_PATHS.json',dict(scope='HOST_SOURCE_PATH_LENGTH_NOT_DEVICE_ADMISSION',members=rows,max_characters=max(r['characters'] for r in rows)))
        self.command('bind',[self.python,Path(__file__).with_name('bind_build.py'),'--repo',REPO,'--package',output,
            '--assembly-receipts',self.root/'assembly-receipts','--consumer-receipts',self.root/'consumer-receipts',
            '--assembly-build',self.root/'transport-build','--consumer-build',self.root/'consumer-build','--output',self.root/'bound-build'])

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path)
    p.add_argument('--phase',choices=['prepare','fastdds','ros','assembly','consumer','package','all'],required=True)
    p.add_argument('--rebuild-package',action='append',default=[],help='Explicit local ROS relink after an audited dependency install correction')
    p.add_argument('--cmake',default='C:/pixi_ws/.pixi/envs/default/Library/bin/cmake.exe')
    p.add_argument('--python',default='C:/pixi_ws/.pixi/envs/default/python.exe')
    p.add_argument('--ninja',default='C:/Users/17715/scoop/shims/ninja.exe')
    p.add_argument('--sdk',default='C:/Users/17715/AppData/Local/OpenHarmony/Sdk/23/native')
    p.add_argument('--host-tools',default='C:/pixi_ws/ros2-windows')
    a=p.parse_args();b=Build(a)
    for phase in (['prepare','fastdds','ros','assembly','consumer','package'] if a.phase=='all' else [a.phase]):
        getattr(b,'ros_build' if phase=='ros' else phase)()

if __name__=='__main__':main()
