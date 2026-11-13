"""Real Java/JNI <-> independent B3 public-DDS C++ processes on Windows."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid
import psutil
from build_java_backend import snapshot
ROOT=Path(__file__).resolve().parents[2]

def environment(bundle):
    env=os.environ.copy();env['PATH']=os.pathsep.join(bundle['runtime_dirs'])+os.pathsep+env.get('PATH','');return env

def modules(pid,bundle):
    loaded=sorted({p.path for p in psutil.Process(pid).memory_maps() if p.path.lower().endswith('.dll')})
    dirs={Path(p).resolve() for p in bundle['runtime_dirs']};dll=Path(bundle['dll']).resolve()
    required='fastrtps-2.14.dll' if bundle['abi']=='2.14.5' else 'fastdds-3.6.dll';seen=set();records=[]
    for item in loaded:
        path=Path(item).resolve();name=path.name.lower()
        if name.startswith(('fastdds-','fastrtps-','fastcdr-','foonathan_memory-')) or name=='standards_b5_jni.dll':
            if path!=dll and path.parent not in dirs:raise RuntimeError('loaded native module outside declared closure: '+str(path))
            seen.add(name);records.append({'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    if required not in seen or 'standards_b5_jni.dll' not in seen or not any(n.startswith('fastcdr-') for n in seen):raise RuntimeError('required ABI modules not observed')
    if ('fastdds-3.6.dll' if required=='fastrtps-2.14.dll' else 'fastrtps-2.14.dll') in seen:raise RuntimeError('opposite DDS ABI is loaded')
    return {'pid':pid,'all_dll_paths':loaded,'verified_native_modules':records}

def run(args):
    out=args.out.resolve()
    if out.exists() or not out.is_relative_to((ROOT/'build').resolve()):raise ValueError('output must be a new workspace build directory')
    out.mkdir(parents=True);before=snapshot();summary={'status':'FAIL','source_snapshot':before,'pairs':[]}
    bundles=json.loads(args.bundles.read_text(encoding='utf-8-sig'))
    if set(bundles)!={'2.14.5','3.6.2'}:raise ValueError('both exact ABI bundles required')
    for abi,bundle in bundles.items():
        build=json.loads(Path(bundle['build_summary']).read_text(encoding='utf-8-sig'))
        if build['status']!='PASS' or build['fastdds']!=abi or build['bundle']!=bundle or build['source_snapshot']!=before:raise ValueError('bundle build/source provenance mismatch')
        for path,sha in {**build['artifact_sha256'],**build['jdk_sha256']}.items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=sha:raise ValueError('bundle artifact/JDK hash mismatch: '+path)
    artifacts={str(Path(bundle[key]).resolve()):hashlib.sha256(Path(bundle[key]).read_bytes()).hexdigest() for bundle in bundles.values() for key in ('java','dll','jar','api_jar','peer')}
    summary['artifact_sha256']=artifacts
    try:
        for jabi,java in bundles.items():
            for cabi,cpp in bundles.items():
                for role in ('writer','reader'):
                    for rep in ((0,2) if jabi==cabi else (0,)):
                        name=f'{jabi}-{cabi}-java-{role}-rep{rep}';case=out/name;case.mkdir()
                        topic='B5_'+uuid.uuid4().hex;gate=case/'go';opposite='reader' if role=='writer' else 'writer'
                        jcmd=[java['java'],'-Xcheck:jni','-cp',java['jar'],'org.fastdds.standards.b5.Client',java['dll'],jabi,role,str(args.domain),topic,str(rep),str(gate)]
                        ccmd=[cpp['peer'],'--dds',opposite,str(args.domain),topic,str(rep)]
                        processes=[];entry={'java_abi':jabi,'cpp_abi':cabi,'java_role':role,'representation':rep,'commands':[jcmd,ccmd],'status':'FAIL'}
                        summary['pairs'].append(entry)
                        try:
                            with (case/'java.stdout.log').open('w',encoding='utf-8') as jout,(case/'java.stderr.log').open('w',encoding='utf-8') as jerr,(case/'cpp.stdout.log').open('w',encoding='utf-8') as cout,(case/'cpp.stderr.log').open('w',encoding='utf-8') as cerr:
                                jp=subprocess.Popen(jcmd,cwd=ROOT,env=environment(java),stdout=jout,stderr=jerr,creationflags=subprocess.CREATE_NO_WINDOW);processes.append(jp)
                                deadline=time.monotonic()+8
                                while time.monotonic()<deadline:
                                    text=(case/'java.stdout.log').read_text(encoding='utf-8')
                                    if 'B5_READY role='+role in text:break
                                    if jp.poll() is not None:raise RuntimeError('Java startup failed: '+str(case))
                                    time.sleep(.02)
                                else:raise TimeoutError('Java endpoint readiness deadline')
                                if 'pid='+str(jp.pid)+' ' not in text:raise RuntimeError('Java PID receipt mismatch')
                                (case/'java-modules.json').write_text(json.dumps(modules(jp.pid,java),indent=2),encoding='utf-8')
                                cp=subprocess.Popen(ccmd,cwd=ROOT,env=environment(cpp),stdout=cout,stderr=cerr,creationflags=subprocess.CREATE_NO_WINDOW);processes.append(cp)
                                gate.touch();jcode=jp.wait(timeout=35);ccode=cp.wait(timeout=35)
                            entry.update(java_pid=jp.pid,cpp_pid=cp.pid,java_exit=jcode,cpp_exit=ccode)
                            jtext=(case/'java.stdout.log').read_text(encoding='utf-8');ctext=(case/'cpp.stdout.log').read_text(encoding='utf-8')
                            if jcode or ccode or f'B5_DDS PASS role={role} abi={jabi} rep={rep} samples=10 owners=0' not in jtext or f'B3_DDS PASS role={opposite} rep={rep}' not in ctext:raise RuntimeError('DDS value/exit/cleanup failure: '+str(case))
                            if re.search(r'WARNING in native method|FATAL ERROR in native method',jtext+(case/'java.stderr.log').read_text(encoding='utf-8')):raise RuntimeError('JNI checker warning')
                            entry['status']='PASS';print('B5_PAIR PASS '+name,flush=True)
                        finally:
                            for process in processes:
                                if process.poll() is None:process.kill();process.wait(timeout=5);entry['forced_cleanup']=True
                            (case/'receipt.json').write_text(json.dumps(entry,indent=2)+'\n',encoding='utf-8')
                            (out/'progress.json').write_text(json.dumps(summary['pairs'],indent=2),encoding='utf-8')
        if len(summary['pairs'])!=12 or Counter(p['status'] for p in summary['pairs'])!={'PASS':12}:raise RuntimeError('Java matrix census incomplete')
        if snapshot()!=before:raise RuntimeError('source changed during execution')
        if any(hashlib.sha256(Path(path).read_bytes()).hexdigest()!=expected for path,expected in artifacts.items()):raise RuntimeError('artifact changed during execution')
        summary.update(status='PASS',source_unchanged=True,artifact_unchanged=True)
    finally:(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--bundles',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);parser.add_argument('--domain',type=int,choices=[219,223],default=219)
    try:run(parser.parse_args())
    except Exception as error:parser.exit(1,str(error)+'\n')
