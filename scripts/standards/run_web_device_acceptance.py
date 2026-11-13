"""Replay the unmodified C1/C2 client oracles against native dual-board gateways."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'))
from web_device_support import Deployment,Facade
import standards_web_gateway_tests as rest
import standards_websocket_tests as websocket_tests

def snapshot():
    files=list((ROOT/'integrations/standards/web_gateway').rglob('*'))
    files+=list((ROOT/'scripts/standards').glob('*web*py'))
    files+=[ROOT/'tests/standards_web_gateway_tests.py',ROOT/'tests/standards_websocket_tests.py']
    return {str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()}

def run(args):
    out=args.out.resolve();before=snapshot();deployment=None;summary={'status':'FAIL','scope':'targeted native replay' if args.targeted else 'full selected native replay','source_snapshot':before,'lanes':[]}
    try:
        bundles=json.loads(args.bundles.read_text(encoding='utf-8-sig'))
        ssh_map=json.loads(args.ssh_map.read_text(encoding='utf-8-sig')) if args.ssh_map else None
        deployment=Deployment(out,bundles,args.hdc,ssh_map)
        versions=[args.version] if args.version else list(bundles)
        matrix=[(g,p,board,rep) for g in versions for p in versions
                for board in ([args.gateway_board] if args.gateway_board else deployment.boards)
                for rep in ([args.representation] if args.representation is not None else ([0,2] if g==p else [0]))
                if not args.same_only or g==p]
        for gateway_version,peer_version,board,rep in matrix:
            name=f'{gateway_version}-{peer_version}-{board}-rep{rep}';lane=out/name;lane.mkdir()
            facade=Facade(deployment,board,gateway_version,peer_version)
            rest.subprocess=facade;websocket_tests.subprocess=facade
            os.environ.update(C1_GATEWAY=facade.gateway,C2_GATEWAY=facade.gateway,C1_PEER=facade.peer,C1_DOMAIN='219',C1_REPRESENTATION=str(rep),C1_SCHEMAS=str(Path(bundles[gateway_version]['build'])/'schemas'))
            entry={'gateway_version':gateway_version,'peer_version':peer_version,'gateway_board':board,'peer_board':next(b for b in deployment.boards if b!=board),'representation':rep,'suites':[]}
            summary['lanes'].append(entry)
            for label,module in [('rest',rest),('ws',websocket_tests),('wss',websocket_tests)]:
                facade.profile='rest' if label=='rest' else 'websocket'
                directory=lane/label;directory.mkdir();os.environ.update(C1_EVIDENCE_DIR=str(directory),C2_EVIDENCE_DIR=str(directory),C2_TRANSPORT='wss' if label=='wss' else 'ws')
                loader=unittest.TestLoader()
                if args.targeted:
                    names=(['GatewayTests.test_independent_dds_reader_receives_http_writes','GatewayTests.test_independent_dds_writer_read_take_waitset'] if label=='rest' else ['WebSocketTests.test_handshake_request_and_authorization','WebSocketTests.test_bound_writer_to_native_dds_reader','WebSocketTests.test_native_dds_unsolicited_push_and_cancellation'])
                    # Find the actual C1 test class rather than invent a new oracle.
                    if label=='rest':
                        klass=next(n for n,v in vars(rest).items() if isinstance(v,type) and issubclass(v,unittest.TestCase) and hasattr(v,'test_independent_dds_reader_receives_http_writes'))
                        names=[n.replace('GatewayTests.',klass+'.') for n in names]
                    tests=loader.loadTestsFromNames(names,module)
                else:tests=loader.loadTestsFromModule(module)
                expected_tests=tests.countTestCases()
                if expected_tests==0:raise RuntimeError('native suite contains no client oracles')
                with (lane/(label+'.log')).open('w',encoding='utf-8') as log:
                    result=unittest.TextTestRunner(stream=log,verbosity=2,failfast=True).run(tests)
                record={'name':label,'tests':result.testsRun,'expected_tests':expected_tests,'failures':len(result.failures),'errors':len(result.errors),'skipped':len(result.skipped),'status':'PASS' if result.wasSuccessful() and not result.skipped and result.testsRun==expected_tests else 'FAIL'}
                entry['suites'].append(record);print('C4_SUITE',name,label,record,flush=True)
                (out/'progress.json').write_text(json.dumps(summary['lanes'],indent=2),encoding='utf-8')
                for session in facade.sessions:
                    if not session.finished:
                        try:session.stop();session.finish(15)
                        except Exception as e:record['cleanup_error']=str(e);record['status']='FAIL'
                if record['status']!='PASS':raise RuntimeError('native replay failed: '+str(lane/(label+'.log')))
        deployment.posthash()
        if snapshot()!=before:raise RuntimeError('source changed during device replay')
        summary.update(status='PASS',source_unchanged=True)
    except Exception as e:
        summary['error']=str(e);summary['traceback']=traceback.format_exc();raise
    finally:
        if deployment:deployment.close()
        if out.exists():(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    return 0

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bundles',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--hdc',type=Path,default=ROOT/'build/dsb-exec-20261110/hdc_ssh.bat');p.add_argument('--targeted',action='store_true')
    p.add_argument('--same-only',action='store_true');p.add_argument('--version',choices=['2.14.5','3.6.2']);p.add_argument('--gateway-board',choices=['100.97.6.8','100.97.143.74']);p.add_argument('--representation',type=int,choices=[0,2])
    p.add_argument('--ssh-map',type=Path,help='Explicit logical-board to private LAN SSH address map; host keys stay pinned to the logical board')
    a=p.parse_args()
    try:sys.exit(run(a))
    except Exception as e:p.exit(1,str(e)+'\n')
