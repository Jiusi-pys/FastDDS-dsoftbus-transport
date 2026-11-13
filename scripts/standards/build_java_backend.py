"""Fresh exact-ABI Windows Java/JNI build and executable ownership contracts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]

def snapshot():
    paths=[p for area in ('java_backend','c_backend','cpp_backend') for p in (ROOT/'integrations/standards'/area).rglob('*') if p.is_file()]
    paths += [ROOT/'integrations/standards/idl/BoundedRecord.idl',Path(__file__),ROOT/'scripts/standards/run_java_acceptance.py',ROOT/'scripts/standards/generate_cpp.py',ROOT/'scripts/standards/idl_cpp.py',ROOT/'scripts/standards/idl_cpp_typeobjects.py',ROOT/'scripts/standards/templates/b3_support.hpp']
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}

def run(args):
    out=args.out.resolve()
    if out.exists() or not out.is_relative_to((ROOT/'build').resolve()):raise ValueError('output must be a new workspace build directory')
    if os.name!='nt':raise ValueError('B5 execution target is Windows')
    out.mkdir(parents=True);before=snapshot();commands=[];summary={'status':'FAIL','fastdds':args.fastdds,'source_snapshot':before}
    jdk=args.jdk.resolve();runtime=[p.resolve() for p in args.runtime_dir]
    env=os.environ.copy();env['JAVA_HOME']=jdk.as_posix();env['IDL_CLANG']=str(args.clang.resolve())
    env['PATH']=os.pathsep.join(map(str,runtime+[jdk/'bin']))+os.pathsep+env.get('PATH','')
    def execute(name,command):
        command=[str(p).replace('\\','/') if name=='configure' else str(p) for p in command]
        try:
            result=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=900)
            data=result.stdout;code=result.returncode
        except subprocess.TimeoutExpired as error:
            data=error.stdout or b'';code='TIMEOUT'
        (out/(name+'.log')).write_bytes(data)
        commands.append({'name':name,'command':command,'exit_code':code,'log':name+'.log'})
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
        if code!=0:raise RuntimeError(name+' failed; '+str(out/(name+'.log')))
    try:
        execute('cmake-version',['cmake','--version'])
        for tool in ('java','javac','jar'):execute(tool+'-version',[jdk/'bin'/(tool+'.exe'),'--version'])
        tools=[jdk/'release',jdk/'include/jni.h',jdk/'include/win32/jni_md.h',*[jdk/'bin'/(n+'.exe') for n in ('java','javac','jar')]]
        summary['jdk_sha256']={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in tools}
        execute('generate',[sys.executable,'-B',ROOT/'scripts/standards/generate_cpp.py',ROOT/'integrations/standards/idl/BoundedRecord.idl','--clang',args.clang,'--out',out/'generated','--fastdds',args.fastdds])
        execute('configure',['cmake','-S',ROOT/'integrations/standards/java_backend','-B',out/'build','-DDDS_STANDARDS_FASTDDS='+args.fastdds,'-DB3_GENERATED_DIR='+str(out/'generated'),'-DB3_IDL='+str(ROOT/'integrations/standards/idl/BoundedRecord.idl'),'-DJAVA_HOME='+str(jdk),'-DJava_JAVA_EXECUTABLE='+str(jdk/'bin/java.exe'),'-DJava_JAVAC_EXECUTABLE='+str(jdk/'bin/javac.exe'),'-DJava_JAR_EXECUTABLE='+str(jdk/'bin/jar.exe'),'-DB5_TEST_DOMAIN='+str(args.domain),*args.cmake_arg])
        execute('build',['cmake','--build',out/'build','--config','Release','--parallel','4'])
        execute('ctest',['ctest','--test-dir',out/'build','-C','Release','--output-on-failure','--no-tests=error','--output-junit',out/'ctest.xml'])
        diagnostic=(out/'build/Testing/Temporary/LastTest.log').read_text(encoding='utf-8',errors='replace')
        if 'WARNING in native method' in diagnostic or 'FATAL ERROR in native method' in diagnostic:raise RuntimeError('JNI checker diagnostic in contract tests')
        directory=out/'build/Release' if (out/'build/Release').exists() else out/'build'
        peer=out/'build/b4/b3/Release/b3_contract.exe'
        if not peer.exists():peer=out/'build/b4/b3/b3_contract.exe'
        artifacts=[directory/'standards_b5_jni.dll',directory/'standards_b5_fault_jni.dll',out/'build/standards-b5.jar',out/'build/standards-b5-tests.jar',peer]
        if snapshot()!=before:raise RuntimeError('source changed during build')
        summary.update(status='PASS',source_unchanged=True,artifact_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts},bundle={'abi':args.fastdds,'java':str(jdk/'bin/java.exe'),'dll':str(artifacts[0]),'jar':str(out/'build/standards-b5-tests.jar'),'api_jar':str(out/'build/standards-b5.jar'),'peer':str(peer),'runtime_dirs':list(map(str,runtime)),'build_summary':str(out/'summary.json')})
    finally:
        summary['commands']=commands;(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('B5_BUILD PASS abi='+args.fastdds,flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fastdds',choices=['2.14.5','3.6.2'],required=True)
    for name in ('out','jdk','clang'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--runtime-dir',type=Path,action='append',default=[])
    parser.add_argument('--cmake-arg',action='append',default=[])
    parser.add_argument('--domain',type=int,choices=[219,223],required=True)
    try:run(parser.parse_args())
    except Exception as error:parser.exit(1,str(error)+'\n')
