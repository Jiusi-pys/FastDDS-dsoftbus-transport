"""Build a fresh, exact-ABI OpenHarmony DDS-WEB gateway and independent peer."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]

def run(args):
    out=args.out.resolve()
    if out.exists() or not out.is_relative_to((ROOT/'build').resolve()):raise ValueError('output must be a new workspace build directory')
    out.mkdir(parents=True)
    sources=[p for p in (ROOT/'integrations/standards/web_gateway').rglob('*') if p.is_file()]
    sources += [Path(__file__).resolve(),ROOT/'scripts/standards/generate_cpp.py']
    hashes=lambda:{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    before=hashes();records=[];summary={'status':'FAIL','version':args.version,'source_snapshot':before}
    def execute(name,command):
        result=subprocess.run([str(x) for x in command],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=900)
        (out/(name+'.log')).write_bytes(result.stdout);records.append({'name':name,'command':[str(x) for x in command],'exit_code':result.returncode})
        (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if result.returncode:raise RuntimeError(name+' failed; '+str(out/(name+'.log')))
    try:
        execute('cmake-version',['cmake','--version']);execute('clang-version',[args.clang,'--version'])
        execute('generate',[sys.executable,'-B',ROOT/'scripts/standards/generate_cpp.py',ROOT/'integrations/standards/idl/BoundedRecord.idl','--clang',args.clang,'--out',out/'generated','--fastdds',args.version])
        prefix=args.dds_prefix.resolve();stock=args.stock_prefix.resolve();xml=args.xml_prefix.resolve();build=out/'build'
        options={'CMAKE_TOOLCHAIN_FILE':args.toolchain.resolve(),'OHOS_ARCH':'arm64-v8a','OHOS_PLATFORM':'OHOS','CMAKE_BUILD_TYPE':'Release','DDS_STANDARDS_FASTDDS':args.version,'B3_GENERATED_DIR':out/'generated','B3_IDL':ROOT/'integrations/standards/idl/BoundedRecord.idl','CMAKE_PREFIX_PATH':prefix,'C1_BOOST_INCLUDE_DIR':args.boost_include.resolve(),'Python3_EXECUTABLE':sys.executable,'OPENSSL_ROOT_DIR':prefix,'OPENSSL_INCLUDE_DIR':prefix/'include','OPENSSL_SSL_LIBRARY':prefix/'lib/libssl.so','OPENSSL_CRYPTO_LIBRARY':prefix/'lib/libcrypto.so','LIBXML2_INCLUDE_DIR':xml/'include/libxml2','LIBXML2_LIBRARY':xml/'lib/libxml2.so','C4_DSOFTBUS':'ON','FastDDSDSoftBusStock_DIR':stock/'lib/cmake/FastDDSDSoftBusStock','fastcdr_DIR':prefix/'lib/cmake/fastcdr','foonathan_memory_DIR':prefix/'lib/foonathan_memory/cmake'}
        package='fastrtps' if args.version=='2.14.5' else 'fastdds';options[package+'_DIR']=prefix/'share'/package/'cmake'
        options['CMAKE_EXE_LINKER_FLAGS']='-L'+(prefix/'lib').as_posix()+' -Wl,-rpath-link,'+(prefix/'lib').as_posix()
        if args.platform_dir:options['CMAKE_EXE_LINKER_FLAGS']+=' -Wl,-rpath-link,'+args.platform_dir.resolve().as_posix()
        execute('configure',['cmake','-S',ROOT/'integrations/standards/web_gateway','-B',build,'-G','Ninja',*['-D'+k+'='+str(v).replace('\\','/') for k,v in options.items()]])
        execute('build',['cmake','--build',build,'--target','webdds_gateway_c4','c4_peer','c4_query','-j',str(args.jobs)])
        if hashes()!=before:raise RuntimeError('source changed during native build')
        summary.update(status='PASS',source_unchanged=True,artifact_sha256={name:hashlib.sha256((build/name).read_bytes()).hexdigest() for name in ['webdds_gateway_c4','c4_peer','c4_query']},bundle={'build':str(build),'prefix':str(prefix),'stock':str(stock),'xml':str(xml)})
    finally:
        summary['commands']=records;(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('C4_BUILD PASS',args.version,flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--version',choices=['2.14.5','3.6.2'],required=True)
    for name in ['out','dds-prefix','stock-prefix','xml-prefix','toolchain','boost-include','clang']:p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--platform-dir',type=Path);p.add_argument('--jobs',type=int,choices=range(1,17),default=6);a=p.parse_args()
    try:run(a)
    except Exception as e:p.exit(1,str(e)+'\n')
