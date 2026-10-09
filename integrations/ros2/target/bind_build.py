"""Bind an existing actual target build/install/consumer to executor inputs.

This is a provenance projection from original command receipts, never a build,
device preflight, authorization, or success issuer.
"""
import argparse,hashlib,json,shutil,re
from pathlib import Path

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def read(path):return json.loads(Path(path).read_text(encoding='utf-8-sig'))
def put(path,value):path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')

def verify_build_identity(builds,members,version,security):
    rows=[];identities=[]
    def macro(header,name):
        values=re.findall(r'^#\s*define\s+'+name+r'\s+("[^"\r\n]+"|[0-9]+)\s*$',header.read_text(),re.M)
        if len(values)!=1:raise ValueError('Ambiguous dependency header macro: '+name)
        rows.append(dict(path=str(header.resolve()),sha256=sha(header)));return values[0].strip('"')
    def library(name,path):
        found=[x for x in members if x['name']==name]
        # Windows-installed Unix SONAME aliases can be separate identical files.
        # Permit that declared sibling alias only, never a different prefix.
        if len(found)!=1 or Path(found[0]['source']).resolve().parent!=path.resolve().parent or found[0]['sha256']!=sha(path):
            raise ValueError('Package differs from configured dependency: '+name)
        rows.append(dict(path=str(path.resolve()),sha256=sha(path),package_source=str(Path(found[0]['source']).resolve()),package_source_sha256=sha(found[0]['source'])))
    for build in builds:
        cache=build/'CMakeCache.txt';values={}
        for line in cache.read_text().splitlines():
            match=re.fullmatch(r'([^:#]+):[^=]+=(.*)',line)
            if match:values[match[1]]=match[2]
        rows.append(dict(path=str(cache.resolve()),sha256=sha(cache)))
        dds=Path(values['fastrtps_DIR']).resolve().parents[2];cdr=Path(values['fastcdr_DIR']).resolve().parents[2]
        header=dds/'include/fastrtps/config.h'
        if macro(header,'FASTRTPS_VERSION_STR')!=version or macro(header,'HAVE_SECURITY')!=str(int(security=='ON')):
            raise ValueError('Requested version/security differs from actual configured headers')
        expected_cdr='2.2.8' if version=='2.14.7' else '2.2.5'
        if macro(cdr/'include/fastcdr/config.h','FASTCDR_VERSION_STR')!=expected_cdr:raise ValueError('Fast CDR configured version mismatch')
        # Package origins are checked against the actual install prefix selected by CMake.
        for name,prefix in [('libfastrtps.so.2.14',dds),('libfastcdr.so.2',cdr)]:
            found=[x for x in members if x['name']==name]
            if len(found)!=1 or not Path(found[0]['source']).resolve().is_relative_to(prefix/'lib'):
                raise ValueError('Foreign DDS/CDR package origin')
            library(name,Path(found[0]['source']))
        ssl=None
        if security=='ON':
            include=Path(values['OPENSSL_INCLUDE_DIR']).resolve();ssl=include.parent
            if macro(include/'openssl/opensslv.h','OPENSSL_VERSION_STR')!='3.5.9':raise ValueError('OpenSSL candidate version mismatch')
            library('libssl.so.3',Path(values['OPENSSL_SSL_LIBRARY']))
            library('libcrypto.so.3',Path(values['OPENSSL_CRYPTO_LIBRARY']))
        identities.append((dds,cdr,ssl))
    if len(set(identities))!=1:raise ValueError('Assembly and consumer use different dependency prefixes')
    return rows

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--version',choices=['2.14.5','2.14.7'],default='2.14.5')
    p.add_argument('--security-mode',choices=['OFF','ON'],default='OFF')
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
    identity_inputs=verify_build_identity((a.assembly_build,a.consumer_build),members,a.version,a.security_mode)
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
    put(a.output/'BUILD_PROVENANCE.json',dict(scope='ACTUAL_SAVED_BUILD_COMMAND_PROJECTION_NOT_DEVICE_ADMISSION',receipts=receipts,dependency_identity_inputs=identity_inputs,package_selection_sha256=sha(a.package/'PACKAGE_INPUTS.json')))
    if a.security_mode=='ON' and a.version!='2.14.7':raise ValueError('Secure candidate requires exact 2.14.7')
    put(a.output/'build-manifest.json',dict(scenario='ROS2_DSOFTBUS',main='ordinary_ros2_transport_consumer',abi=dict(fastrtps=a.version,cpp_runtime='c++_shared'),security_mode=a.security_mode,bridgeExit=read(a.assembly_receipts/'build.json')['exit_code'],configureExit=read(a.consumer_receipts/'configure.json')['exit_code'],buildExit=read(a.consumer_receipts/'build.json')['exit_code'],sourceStateSha256=sha(a.output/'source-state.json'),artifacts=artifacts,original_command_provenance_sha256=sha(a.output/'BUILD_PROVENANCE.json')))
    print('ACTUAL_BUILD_BOUND members='+str(len(members)))

if __name__=='__main__':main()
