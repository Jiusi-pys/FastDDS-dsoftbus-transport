"""Fresh exact-ABI OpenHarmony builds of the bounded D1-D4 applications."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import build_ua_complete as d4
import build_dds_ua_gateway as d2

ROOT=d4.ROOT
sha=d4.sha
TARGETS=('d1_gateway','d1_peer','d1_fixture','d2_gateway','d2_peer','d3_actor','d3_peer','d4_service','d4_history','d4_peer','d5_query')


def snapshot():
    files=[p for p in (ROOT/'integrations/standards/opcua_device').rglob('*') if p.is_file()]
    files += [ROOT/'scripts/standards/build_ua_device.py']
    files += [ROOT/'integrations/standards/web_gateway'/name for name in ('c4_transport.hpp','c4_shutdown.hpp','c4_query.cpp')]
    files += [ROOT/'integrations/ros2/autoload/DeviceDeployment.hpp',ROOT/'scripts/standards/web_device_support.py']
    return {**d4.snapshot(),**d2.snapshot(),**{str(p.relative_to(ROOT)):sha(p) for p in files}}


def validate(bundle):
    record=json.loads(Path(bundle['summary']).read_text(encoding='utf-8'))
    if record['status']!='PASS' or record['bundle']!=bundle or record['source_snapshot']!=snapshot():raise ValueError('D5 source/build identity changed')
    if len(record['commands'])!=7 or any(c['exit_code'] for c in record['commands']):raise ValueError('D5 build command failure')
    for group in ('upstream_sha256','input_sha256','artifact_sha256','generated_sha256'):
        if not record[group] or any(sha(p)!=h for p,h in record[group].items()):raise ValueError('D5 provenance changed: '+group)
    d2.verify_client(bundle['client_manifest'])
    return record


def build(args):
    out=args.out.resolve()
    if out.exists() or not out.is_relative_to((ROOT/'build').resolve()):raise ValueError('new workspace build directory required')
    upstream=d4.verify_upstream(args.ua_archive.resolve(),args.ua_source.resolve())
    d2.verify_client(args.client_manifest)
    before=snapshot();out.mkdir(parents=True);commands=[]
    prefix,stock=args.dds_prefix.resolve(),args.stock_prefix.resolve()
    inputs=[args.ua_archive,args.clang,args.toolchain,args.client_manifest]
    for directory in (prefix/'include',prefix/'lib',prefix/'share',stock/'include',stock/'lib'):
        inputs += [p for p in directory.rglob('*') if p.is_file()]
    inputs += [p for p in args.toolchain.resolve().parents[2].rglob('ohos*.cmake') if p.is_file()]
    input_hashes={str(p.resolve()):sha(p) for p in inputs}
    summary=dict(status='FAIL',source_snapshot=before,upstream_sha256=upstream,input_sha256=input_hashes,commands=commands)
    def execute(name,argv):
        argv=[str(x).replace('\\','/') for x in argv]
        with (out/(name+'.log')).open('wb') as log:
            result=subprocess.run(argv,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,timeout=900)
        commands.append(dict(name=name,argv=argv,exit_code=result.returncode))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if result.returncode:raise RuntimeError(name+' failed: '+str(out/(name+'.log')))
    try:
        common=['-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(args.toolchain.resolve()),'-DOHOS_ARCH=arm64-v8a','-DOHOS_PLATFORM=OHOS','-DCMAKE_BUILD_TYPE=Release']
        execute('ua-configure',['cmake','-S',ROOT/'integrations/standards/opcua_device/ua_native','-DD5_UA_SOURCE='+str(args.ua_source.resolve()),'-B',out/'ua-build',*common,'-DCMAKE_INSTALL_PREFIX='+str(out/'ua-install'),'-DPython3_EXECUTABLE='+sys.executable,'-DUA_ENABLE_ENCRYPTION=OFF','-DUA_ENABLE_QUERY=ON','-DUA_ENABLE_HISTORIZING=ON','-DUA_BUILD_EXAMPLES=OFF','-DUA_BUILD_UNIT_TESTS=OFF','-DUA_NAMESPACE_ZERO=REDUCED','-DBUILD_SHARED_LIBS=OFF'])
        execute('ua-build',['cmake','--build',out/'ua-build','--parallel',str(args.jobs)])
        execute('ua-install',['cmake','--install',out/'ua-build'])
        for name,idl in [('records','BoundedRecord'),('rpc','RpcMessage')]:
            execute(name+'-generate',[sys.executable,'-B',ROOT/'scripts/standards/generate_cpp.py',ROOT/'integrations/standards/idl'/(idl+'.idl'),'--clang',args.clang,'--out',out/name,'--fastdds',args.version])
        options={'DDS_STANDARDS_FASTDDS':args.version,'B3_GENERATED_DIR':out/'records','B3_IDL':ROOT/'integrations/standards/idl/BoundedRecord.idl','D3_GENERATED_DIR':out/'rpc','D3_IDL':ROOT/'integrations/standards/idl/RpcMessage.idl','CMAKE_PREFIX_PATH':prefix,'Python3_EXECUTABLE':sys.executable,'open62541_DIR':out/'ua-install/lib/cmake/open62541','FastDDSDSoftBusStock_DIR':stock/'lib/cmake/FastDDSDSoftBusStock','fastcdr_DIR':prefix/'lib/cmake/fastcdr','foonathan_memory_DIR':prefix/'lib/foonathan_memory/cmake'}
        options.update(OPENSSL_ROOT_DIR=prefix,OPENSSL_INCLUDE_DIR=prefix/'include',OPENSSL_SSL_LIBRARY=prefix/'lib/libssl.so',OPENSSL_CRYPTO_LIBRARY=prefix/'lib/libcrypto.so')
        package='fastrtps' if args.version=='2.14.5' else 'fastdds';options[package+'_DIR']=prefix/'share'/package/'cmake'
        options['CMAKE_EXE_LINKER_FLAGS']='-L'+(prefix/'lib').as_posix()+' -Wl,-rpath-link,'+(prefix/'lib').as_posix()+' -Wl,-rpath-link,'+args.platform_dir.resolve().as_posix()
        execute('configure',['cmake','-S',ROOT/'integrations/standards/opcua_device','-B',out/'build',*common,*['-D'+k+'='+str(v) for k,v in options.items()]])
        execute('build',['cmake','--build',out/'build','--target',*TARGETS,'-j',str(args.jobs)])
        generated={str(out/area/name):digest for area in ('records','rpc') for name,digest in json.loads((out/area/'generation.json').read_text(encoding='utf-8'))['generated'].items()}
        artifacts={str(out/'build'/name):sha(out/'build'/name) for name in TARGETS}
        artifacts[str(out/'ua-install/lib/libopen62541.a')]=sha(out/'ua-install/lib/libopen62541.a')
        if snapshot()!=before or any(sha(p)!=h for p,h in {**input_hashes,**upstream,**generated}.items()):raise RuntimeError('D5 inputs changed during build')
        summary.update(status='PASS',source_unchanged=True,generated_sha256=generated,artifact_sha256=artifacts,
                       bundle=dict(abi=args.version,build=str(out/'build'),prefix=str(prefix),stock=str(stock),summary=str(out/'summary.json'),client_manifest=str(args.client_manifest.resolve())))
    finally:
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('D5_BUILD PASS '+args.version,flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--version',choices=['2.14.5','3.6.2'],required=True)
    for name in ('out','ua-source','ua-archive','dds-prefix','stock-prefix','toolchain','clang','platform-dir','client-manifest'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--jobs',type=int,choices=range(1,17),default=6)
    try:build(p.parse_args())
    except Exception as error:p.exit(1,str(error)+'\n')
