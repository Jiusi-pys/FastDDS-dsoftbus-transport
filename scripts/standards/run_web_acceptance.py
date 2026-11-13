"""Build one exact C1 ABI and run the independent local requests/DDS client."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]

def run(args):
    out=args.out.resolve()
    if out.exists():raise ValueError('evidence directory must be new')
    out.mkdir(parents=True)
    env=os.environ.copy()
    env['PATH']=os.pathsep.join([str(p.resolve()) for p in args.runtime_dir]+[env.get('PATH','')])
    env['IDL_CLANG']=str(args.clang.resolve())
    records=[]
    def execute(name,command,extra=None):
        child=env.copy();child.update(extra or {})
        p=subprocess.run([str(x) for x in command],cwd=ROOT,env=child,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
        (out/(name+'.log')).write_text(p.stdout,encoding='utf-8')
        records.append({'name':name,'command':[str(x) for x in command],'exit_code':p.returncode,'log':name+'.log'})
        (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
        if p.returncode:raise RuntimeError(name+' failed; '+str(out/(name+'.log')))
    sources=[p for p in (ROOT/'integrations'/'standards'/'web_gateway').rglob('*') if p.is_file()]
    sources += [ROOT/'scripts'/'standards'/n for n in ['prepare_web_schema.py','run_web_acceptance.py']]
    sources += [ROOT/'tests'/n for n in ['standards_web_gateway_tests.py','standards_web_schema_tests.py']]
    before={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    summary={'status':'FAIL','fastdds':args.fastdds,'source_snapshot':before}
    try:
        execute('generate',[sys.executable,'-B',ROOT/'scripts/standards/generate_cpp.py',ROOT/'integrations/standards/idl/BoundedRecord.idl',
             '--clang',args.clang,'--fastdds',args.fastdds,'--out',out/'generated'])
        execute('configure',['cmake','-S',ROOT/'integrations/standards/web_gateway','-B',out/'build',
             '-DDDS_STANDARDS_FASTDDS='+args.fastdds,'-DB3_GENERATED_DIR='+str(out/'generated'),
             '-DB3_IDL='+str(ROOT/'integrations/standards/idl/BoundedRecord.idl'),
             '-DC1_BOOST_INCLUDE_DIR='+str(args.boost_include.resolve()),'-DPython3_EXECUTABLE='+sys.executable,*args.cmake_arg])
        execute('build',['cmake','--build',out/'build','--config','Release'])
        suffix='.exe' if os.name=='nt' else ''
        directory=out/'build'/'Release' if (out/'build'/'Release').exists() else out/'build'
        gateway=directory/('webdds_gateway'+suffix);peer=directory/('c1_peer'+suffix)
        execute('schema-tests',[sys.executable,'-B',ROOT/'tests/standards_web_schema_tests.py','-v'])
        for rep in (0,2):
            execute('http-dds-rep'+str(rep),[sys.executable,'-B',ROOT/'tests/standards_web_gateway_tests.py','-v'],{
                'C1_GATEWAY':str(gateway),'C1_PEER':str(peer),'C1_SCHEMAS':str(out/'build'/'schemas'),
                'C1_DOMAIN':str(args.domain),'C1_REPRESENTATION':str(rep),'C1_EVIDENCE_DIR':str(out/('rep'+str(rep)))})
        after={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        if before!=after:raise RuntimeError('source snapshot changed during execution')
        summary.update(status='PASS',artifact_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (gateway,peer)},source_unchanged=True)
    finally:
        summary['commands']=records
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('C1_ACCEPTANCE PASS',args.fastdds)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fastdds',choices=['2.14.5','3.6.2'],required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--boost-include',type=Path,required=True)
    p.add_argument('--clang',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',default=[])
    p.add_argument('--cmake-arg',action='append',default=[])
    p.add_argument('--domain',type=int,choices=[219,223],required=True)
    args=p.parse_args()
    try:run(args)
    except (OSError,ValueError,RuntimeError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
