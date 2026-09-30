"""Exercise real CMake configuration at the renamed public option boundary."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

LEGACY_OPTIONS = (
    'MDDS_BUILD_EXPERIMENTAL_RUNTIME', 'MDDS_BUILD_EXPERIMENTAL_TESTS',
    'MDDS_EXPERIMENT_NATIVE', 'MDDS_EXPERIMENT_RX_DROP',
    'MDDS_EXPERIMENT_TOKEN_BRIDGE', 'MDDS_FASTDDS_MODE',
    'MDDS_INSTALL_PACKAGE', 'MDDS_PUBLIC_SOCKET_LIBRARY',
)

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
    a.work=Path(tempfile.mkdtemp(prefix='run-',dir=a.work))
    results=[]
    common=['-DBUILD_TESTING=OFF','-DBUILD_INTERFACE_PROBE=OFF',
            '-DBUILD_SOFTBUS_CAPABILITY_PROBE=OFF',
            '-DBUILD_DSOFTBUS_TRANSPORT_FACADE=OFF']
    for index,option in enumerate((*LEGACY_OPTIONS,'MDDS_UNRELATED_PARENT_SENTINEL')):
        value='CONFIG' if option=='MDDS_FASTDDS_MODE' else 'ON'
        argv=['cmake','-S',str(a.source.resolve()),'-B',str(a.work/('case-'+str(index))),*common,
              '-DFASTDDS_DSOFTBUS_FASTDDS_MODE=SOURCE_PROBE','-D'+option+'='+value]
        r=subprocess.run(argv,capture_output=True,timeout=120)
        (a.work/(option+'.stdout')).write_bytes(r.stdout)
        (a.work/(option+'.stderr')).write_bytes(r.stderr)
        legacy=option in LEGACY_OPTIONS
        ok=(r.returncode!=0 and ('OBSOLETE_TRANSPORT_OPTION: '+option).encode() in r.stderr
            and option.replace('MDDS_','FASTDDS_DSOFTBUS_',1).encode() in r.stderr) if legacy else r.returncode==0
        results.append(dict(option=option,argv=argv,exit_code=r.returncode,pass_=ok))
    (a.work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    assert all(x['pass_'] for x in results), results
    print('PUBLIC_OPTION_MIGRATION_BOUNDARY_PASS: 8 obsolete options rejected, unrelated variable accepted')

if __name__=='__main__':main()
