"""Real installed-package test. Never imports the source implementation or calls HDC."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--dependency-prefix',type=Path,required=True)
p.add_argument('--cmake',default='cmake')
a=p.parse_args()
a.build=a.build.resolve()
a.dependency_prefix=a.dependency_prefix.resolve()
work=a.work.resolve()
work.mkdir(parents=True,exist_ok=False)
prefix=work/'original-prefix'
moved=work/'relocated-prefix'
results=[]
env=os.environ.copy()
# Explicit dependency runtime, never a substitute SDK/Fake implementation.
env['PATH']=os.pathsep.join([str(a.dependency_prefix/'bin'),str(a.dependency_prefix/'lib'),env.get('PATH','')])
def run(name,argv,expected=0):
    result=subprocess.run([str(x) for x in argv],cwd=work,env=env,capture_output=True,timeout=240)
    (work/(name+'.stdout')).write_bytes(result.stdout)
    (work/(name+'.stderr')).write_bytes(result.stderr)
    results.append(dict(name=name,argv=[str(x) for x in argv],cwd=str(work),exit=result.returncode,expected=expected))
    (work/'results.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    if expected=='nonzero': assert result.returncode!=0,name
    else: assert result.returncode==expected,name
    return result
run('install',[a.cmake,'--install',a.build,'--config','Release','--prefix',prefix])
cache=(a.build/'CMakeCache.txt').read_text(encoding='utf-8')
source=next(x.split('=',1)[1] for x in cache.splitlines() if x.startswith('CMAKE_HOME_DIRECTORY:'))
for file in prefix.rglob('*.cmake'):
    text=file.read_text(encoding='utf-8').replace('\\','/').lower()
    assert source.replace('\\','/').lower() not in text,file
    assert str(a.build).replace('\\','/').lower() not in text,file
    assert '.artifacts' not in text,file
assert not list(prefix.rglob('*Fake*'))
assert not (prefix/'include/mdds/dsoftbus/HostProviderLoop.hpp').exists()
assert not (prefix/'include/mdds/dsoftbus/DSoftBusTransport.hpp').exists()
assert not (prefix/'include/mdds/dsoftbus/RuntimeProbeAccess.hpp').exists()
assert not (prefix/'include/mdds/dsoftbus/ProviderAdapter.hpp').exists()
for file in prefix.rglob('*'):
    if file.is_file():
        content=file.read_bytes().lower().replace(b'\\',b'/')
        assert source.lower().replace('\\','/').encode() not in content,file
        assert str(a.build).lower().replace('\\','/').encode() not in content,file
consumer=work/'consumer'
shutil.copytree(prefix/'share/MDDS/examples/installed_consumer',consumer)
prefix.rename(moved) # Own freshly installed prefix only; old location no longer exists.
headers=[x.relative_to(moved/'include').as_posix() for x in (moved/'include').rglob('*.hpp')]
(consumer/'all_headers.cpp').write_text(''.join('#include <'+x+'>\n' for x in headers),encoding='utf-8')
with (consumer/'CMakeLists.txt').open('a',encoding='utf-8') as f:
    f.write('\nadd_library(all_headers OBJECT all_headers.cpp)\ntarget_link_libraries(all_headers PRIVATE MDDS::mdds_dsoftbus_transport_facade MDDS::mdds_example_types)\ntarget_compile_features(all_headers PRIVATE cxx_std_17)\n')
gen=['-G','Visual Studio 17 2022','-A','x64'] if os.name=='nt' else ['-G','Ninja']
configure=[a.cmake,'-S',consumer,'-B',work/'consumer-build',*gen,'-DCMAKE_CONFIGURATION_TYPES=Release','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF','-DCMAKE_PREFIX_PATH='+str(moved)+';'+str(a.dependency_prefix)]
run('configure',configure)
run('build',[a.cmake,'--build',work/'consumer-build','--config','Release','--parallel','4'])
exe=work/'consumer-build'/('Release/mdds_consumer.exe' if os.name=='nt' else 'mdds_consumer')
result=run('execute',[exe])
assert b'NativeBackendUnavailable' in result.stderr
assert b'HOST_FASTDDS_WRITE_TAKE_PASS' in result.stdout
negative=work/'negative';negative.mkdir()
(negative/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.22)\nproject(no_fake LANGUAGES CXX)\nfind_package(MDDS CONFIG REQUIRED COMPONENTS Experimental)\n',encoding='utf-8')
result=run('experimental_not_in_host',[a.cmake,'-S',negative,'-B',work/'negative-build',*gen,'-DCMAKE_CONFIGURATION_TYPES=Release','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(moved)+';'+str(a.dependency_prefix)],'nonzero')
assert b'does not contain' in result.stderr
result=run('debug_rejected',[a.cmake,'-S',consumer,'-B',work/'debug-build',*gen,'-DCMAKE_CONFIGURATION_TYPES=Debug','-DCMAKE_BUILD_TYPE=Debug','-DCMAKE_PREFIX_PATH='+str(moved)+';'+str(a.dependency_prefix)],'nonzero')
assert b'Release CONFIG' in result.stderr
preloaded=work/'preloaded';preloaded.mkdir()
(preloaded/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.22)\nproject(preloaded LANGUAGES CXX)\nfind_package(fastdds 3.6.2.0 EXACT CONFIG REQUIRED COMPONENTS shared)\nforeach(t fastdds fastcdr foonathan_memory)\nset_property(TARGET ${t} PROPERTY IMPORTED_GLOBAL TRUE)\nendforeach()\nset_property(TARGET fastdds PROPERTY MAP_IMPORTED_CONFIG_RELEASE "")\nfind_package(MDDS CONFIG REQUIRED)\n',encoding='utf-8')
result=run('empty_release_map_rejected',[a.cmake,'-S',preloaded,'-B',work/'preloaded-build',*gen,'-DCMAKE_CONFIGURATION_TYPES=Release','-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(moved)+';'+str(a.dependency_prefix)],'nonzero')
assert b'MAP_IMPORTED_CONFIG_RELEASE' in result.stderr
print('INSTALL_RELOCATION_EXTERNAL_HEADERS_DEFAULT_AND_HOST_DDS_PASS')
