"""Actual direct DDS 2.14 processes using the installed factory integration; no HDC."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import socket
import subprocess
import time

import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/public_socket'))
from analyze_dds214 import records,analyze

def main():
    p=argparse.ArgumentParser()
    for key in ('exe','bridge','fastdds-prefix','fastcdr-prefix','dependency-bin','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--mode',choices=['normal','delete-refusal'],default='normal')
    p.add_argument('--forward',choices=['0','1'],default='1')
    p.add_argument('--config-fault',choices=['none','missing-bridge'],default='none')
    a=p.parse_args();a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=False)
    run=secrets.token_hex(8);ports=[]
    for _ in 'AB':
        with socket.socket() as s:s.bind(('127.0.0.1',0));ports.append(s.getsockname()[1])
    assert len(set(ports))==2
    inputs=[a.exe,a.bridge,a.fastdds_prefix/'bin/fastrtps-2.14.dll']
    hashes={str(x.resolve()):hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}
    children={};streams=[];commands={};released=[]
    for i,side in enumerate('AB'):
        conf=a.output/(side+'.conf')
        library=a.bridge.resolve() if a.config_fault=='none' else a.output/'missing-bridge.dll'
        conf.write_text('\n'.join(['bridge_library='+str(library),'fastdds_module='+str((a.fastdds_prefix/'bin/fastrtps-2.14.dll').resolve()),'fastcdr_module='+str((a.fastcdr_prefix/'bin/fastcdr-2.2.dll').resolve()),'backend=HOST_OPAQUE_BYTES_TEST_ONLY','run='+run,'side='+str(i),'domain=201','port='+str(ports[i]),'peer_port='+str(ports[1-i]),'forward='+a.forward])+'\n')
        env=os.environ.copy();env['PATH']=os.pathsep.join([str(a.fastdds_prefix/'bin'),str(a.fastcdr_prefix/'bin'),str(a.dependency_bin),env.get('PATH','')])
        env.update(FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(conf),FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX=str(a.output/(side+'-control')),FASTDDS_ENVIRONMENT_FILE='',SKIP_DEFAULT_XML='1')
        for key in ('FASTRTPS_DEFAULT_PROFILES_FILE','FASTDDS_DEFAULT_PROFILES_FILE','ROS_LOCALHOST_ONLY','RMW_IMPLEMENTATION'):env.pop(key,None)
        stream=(a.output/(side+'.raw.log')).open('wb');streams.append(stream)
        commands[side]=[str(a.exe.resolve()),side,run,a.mode]
        children[side]=subprocess.Popen(commands[side],env=env,stdout=stream,stderr=subprocess.STDOUT,shell=False)
    killed=[];deadline=time.monotonic()+110
    try:
        while any(c.poll() is None for c in children.values()) and time.monotonic()<deadline:
            texts={s:(a.output/(s+'.raw.log')).read_text(errors='replace') for s in 'AB'}
            for stage in ('MATCH','DATA_DONE'):
                if stage not in released and all(f'DDS214_STAGE run={run} side={s} pid={children[s].pid} stage={stage}' in texts[s] for s in 'AB'):
                    for s in 'AB':(a.output/(s+'-control.'+stage)).write_text(f'DDS214_RELEASE run={run} side={s} stage={stage}\n')
                    released.append(stage)
            time.sleep(.02)
    finally:
        for s,c in children.items():
            if c.poll() is None:c.kill();killed.append(s)
            c.wait(timeout=5)
        for stream in streams:stream.close()
    texts={s:(a.output/(s+'.raw.log')).read_text(errors='replace') for s in 'AB'}
    ledger=analyze(texts,run,a.mode)
    expected_failure=a.forward=='0' or a.config_fault!='none'
    if expected_failure:
        expected='PEER_MATCH_MISSING' if a.forward=='0' else 'PARTICIPANT_CREATE'
        test_pass=not killed and all(c.returncode!=0 for c in children.values()) and all(expected in texts[s] for s in 'AB') and ledger['observed_writes']==ledger['observed_takes']==0 and not released
    else:test_pass=not killed and all(c.returncode==0 for c in children.values()) and ledger['pass'] and released==['MATCH','DATA_DONE']
    result={'scope':'DIRECT_DDS214_HOST_OPAQUE_BYTES_NOT_DEVICE','run':run,'mode':a.mode,'inputs':hashes,'argv':commands,'exit_codes':{s:c.returncode for s,c in children.items()},'released':released,'killed':killed,'ledger':ledger,'expected_failure':expected_failure,'test_pass':test_pass,'device_operations':0}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));return 0 if test_pass else 1
if __name__=='__main__':raise SystemExit(main())
