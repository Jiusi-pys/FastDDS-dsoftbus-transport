"""Compile actual SDK23 consumer/record layouts; never execute a target binary."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path

SYMBOL='_ZTIN8eprosima7fastdds3dds16TopicDescriptionE'
def run(sdk,before,after,output):
    repo=Path(__file__).resolve().parents[2];out=Path(output).resolve()
    if not out.is_relative_to(repo/'build/ros2-target'):raise ValueError('Contained target check output required')
    out.mkdir(parents=True,exist_ok=False);sdk=Path(sdk).resolve()
    env=os.environ.copy();env.update(TEMP=str(out),TMP=str(out),TMPDIR=str(out))
    commands=[]
    def command(label,argv):
        argv=[str(x) for x in argv];p=subprocess.run(argv,cwd=repo,env=env,capture_output=True)
        (out/(label+'.stdout')).write_bytes(p.stdout);(out/(label+'.stderr')).write_bytes(p.stderr)
        row=dict(argv=argv,cwd=str(repo),exit_code=p.returncode)
        (out/(label+'.command.json')).write_text(json.dumps(row,indent=2));commands.append(row)
        if p.returncode:raise RuntimeError(label+' failed')
        return p.stdout.decode(errors='strict')
    source=out/'consumer.cpp';source.write_text('#include <fastdds/dds/topic/Topic.hpp>\nextern "C" const eprosima::fastdds::dds::Topic* original_cast(const eprosima::fastdds::dds::TopicDescription* p) {return dynamic_cast<const eprosima::fastdds::dds::Topic*>(p);}\n')
    layout=out/'layout.cpp';layout.write_text('#include <fastdds/dds/topic/Topic.hpp>\n#include <fastdds/dds/topic/ContentFilteredTopic.hpp>\n#include <type_traits>\nusing namespace eprosima::fastdds::dds;\nstatic_assert(!std::is_destructible<TopicDescription>::value,"protected destructor unchanged");\nstatic_assert(std::is_nothrow_destructible<Topic>::value,"Topic destructor noexcept unchanged");\nstatic_assert(std::is_nothrow_destructible<ContentFilteredTopic>::value,"CFT destructor noexcept unchanged");\nint a[sizeof(Topic)]; int b[sizeof(ContentFilteredTopic)];\n')
    layouts={};symbols={}
    for role,inc in [('before',Path(before).resolve()),('after',Path(after).resolve())]:
        flags=[sdk/'llvm/bin/clang++.exe','--target=aarch64-linux-ohos','--sysroot='+str(sdk/'sysroot'),'-std=c++14','-O0','-fPIC','-DFASTRTPS_DYN_LINK','-I'+str(inc)]
        command(role+'-compile',flags+['-c',source,'-o',out/(role+'.o')])
        table=command(role+'-symbols',[sdk/'llvm/bin/llvm-readelf.exe','--syms','--wide',out/(role+'.o')])
        symbols[role]=[x for x in table.splitlines() if SYMBOL in x]
        dump=command(role+'-layouts',flags+['-c',layout,'-o',out/(role+'-layout.o'),'-Xclang','-fdump-record-layouts'])
        # Compare semantic AST layouts, not IR dump source paths/locations.
        blocks=[]
        for block in dump.split('*** Dumping AST Record Layout')[1:]:
            block=block.split('*** Dumping IRgen Record Layout')[0].strip()
            if any('class eprosima::fastdds::dds::'+name+'\n' in block for name in ('TopicDescription','Topic','ContentFilteredTopic')):blocks.append(block)
        layouts[role]=blocks
    assert len(symbols['before'])==len(symbols['after'])==1,symbols
    assert ' UND ' not in symbols['before'][0] and ' WEAK ' in symbols['before'][0],symbols
    assert ' UND ' in symbols['after'][0],symbols
    assert len(layouts['before'])==3 and layouts['before']==layouts['after'],{k:len(v) for k,v in layouts.items()}
    result=dict(scope='SDK23_AARCH64_COMPILE_AND_LAYOUT_NOT_RUNTIME',result='PASS',
                before_symbols=symbols['before'],after_symbols=symbols['after'],layout_classes=3,
                unchanged_layouts=True,protected_and_noexcept_assertions=True,
                before_header_sha256=hashlib.sha256((Path(before)/'fastdds/dds/topic/TopicDescription.hpp').read_bytes()).hexdigest(),
                after_header_sha256=hashlib.sha256((Path(after)/'fastdds/dds/topic/TopicDescription.hpp').read_bytes()).hexdigest(),commands=commands)
    (out/'RESULT.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('sdk','before','after','output'):p.add_argument('--'+name,required=True)
    a=p.parse_args();print(json.dumps(run(a.sdk,a.before,a.after,a.output)))
