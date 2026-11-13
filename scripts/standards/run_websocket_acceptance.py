"""Fresh exact-ABI C1 regression plus distinct WS/WSS native DDS acceptance."""
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
    sources=[p for p in (ROOT/'integrations/standards/web_gateway').rglob('*') if p.is_file()]
    sources += [ROOT/'.gitattributes']+[ROOT/'scripts/standards'/name for name in
        ['run_websocket_acceptance.py','run_web_acceptance.py','prepare_web_schema.py','prepare_websocket_schema.py','generate_web_test_certs.py']]
    sources += [ROOT/'tests'/name for name in ['standards_websocket_tests.py','standards_web_gateway_tests.py','standards_web_schema_tests.py']]
    before={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
    env=os.environ.copy();env['PATH']=os.pathsep.join([str(p.resolve()) for p in args.runtime_dir]+[env.get('PATH','')])
    records=[];summary={'status':'FAIL','fastdds':args.fastdds,'source_snapshot':before}
    def execute(name,command,extra=None):
        child=env.copy();child.update(extra or {})
        result=subprocess.run([str(x) for x in command],cwd=ROOT,env=child,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=900)
        (out/(name+'.log')).write_text(result.stdout,encoding='utf-8')
        records.append({'name':name,'command':[str(x) for x in command],'exit_code':result.returncode})
        (out/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if result.returncode:raise RuntimeError(name+' failed; '+str(out/(name+'.log')))
    try:
        command=[sys.executable,'-B',ROOT/'scripts/standards/run_web_acceptance.py','--fastdds',args.fastdds,'--out',out/'c1-core','--clang',args.clang,'--boost-include',args.boost_include,'--domain',args.domain]
        for p in args.runtime_dir:command.extend(['--runtime-dir',p])
        command += ['--cmake-arg='+arg for arg in args.cmake_arg]
        execute('c1-build-and-regression',command)
        build=out/'c1-core/build';directory=build/'Release' if (build/'Release').exists() else build
        suffix='.exe' if os.name=='nt' else ''
        gateway=directory/('webdds_gateway_c2'+suffix);peer=directory/('c1_peer'+suffix)
        for rep in (0,2):
            for transport in ('ws','wss'):
                name=transport+'-rep'+str(rep)
                execute(name,[sys.executable,'-B',ROOT/'tests/standards_websocket_tests.py','-v'],{
                    'C2_GATEWAY':str(gateway),'C1_PEER':str(peer),'C1_SCHEMAS':str(build/'schemas'),
                    'C1_DOMAIN':str(args.domain),'C1_REPRESENTATION':str(rep),'C2_TRANSPORT':transport,'C2_EVIDENCE_DIR':str(out/name)})
        after={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
        if before!=after:raise RuntimeError('source snapshot changed during acceptance')
        summary.update(status='PASS',source_unchanged=True,artifact_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (gateway,peer)})
    finally:
        summary['commands']=records
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('C2_ACCEPTANCE PASS',args.fastdds)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fastdds',choices=['2.14.5','3.6.2'],required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--clang',type=Path,required=True);p.add_argument('--boost-include',type=Path,required=True)
    p.add_argument('--runtime-dir',type=Path,action='append',default=[]);p.add_argument('--cmake-arg',action='append',default=[])
    p.add_argument('--domain',choices=['219','223'],required=True);a=p.parse_args()
    try:run(a)
    except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:p.exit(1,str(error)+'\n')
