"""Interpret the declared A-outgoing terminal scene; preserve its raw verdict."""
import argparse
import json
import re
from pathlib import Path


def check(root, side):
    scene=json.loads((root/'RESULT.json').read_text())
    err=(root/(side+'.stderr')).read_text(errors='replace')
    out=(root/(side+'.stdout')).read_text(errors='replace')
    cleanup=re.search(r'FDDST_HOST_CLEANUP threads_joined=(\d+) retained_owner_metadata=(\d+) retained_id_metadata=(\d+) callbacks=(\d+) receives=(\d+) send_entered=(\d+) attempts=(\d+) calls=(\d+) returns=(\d+)',err)
    checks={
        'business_oracle_executed': bool(scene.get('oracle_line')),
        'required_delivery_failed': bool(re.search(r'RESOURCE_ORACLE scene=huge result=FAIL P=0\b',scene.get('oracle_line') or '')),
        'raw_scene_failed': scene.get('passed') is False,
        'raw_children_normal': scene.get('exit')=={'A':0,'B':5},
        'original_connection_terminal_observed': bool(re.search(r'FDDST_HOST_SEND_TERMINAL_BARRIER original_socket=\d+ terminal_observed=1\b',err)),
        'actual_backend_terminal_refusal': bool(re.search(r'FDDST_BACKEND_SEND_REFUSAL layer=PUBLIC_SOCKET_BACKEND branch=InvalidState source=LOCAL_BACKEND_GATE reason_mask=8\b.*api_called=0 api_returned=0 result_present=0 status=5\b',err)),
        'adapter_reported_pre_entry': bool(re.search(r'FDDST_ADAPTER_SEND_REJECT .*api_called=0\b',err)),
        'operational_failure_preserved': 'FDDST_PUMP_ERROR_OBSERVED' in err and 'FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR' in err,
        'caller_original_ticket_recorded': bool(re.search(r'FDDST_PUMP_SEND_FAILURE outcome=3 status=50 result_present=0 original_owner_known=1\b.*close_intent=1 provider_fault=2 source=ORIGINAL_TICKET',err)),
        'original_control_cleanup_complete': bool(cleanup) and cleanup.group(1)=='1' and tuple(map(int,cleanup.groups()[3:6]))==(0,0,0),
        'native_entry_count_independent': bool(cleanup) and int(cleanup.group(7))==int(cleanup.group(8))+1 and cleanup.group(8)==cleanup.group(9),
        'owner_released_with_failure': scene.get('close_contract',{}).get(side,{}).get('released') is True and not scene['close_contract'][side]['incomplete'] and scene['close_contract'][side]['runtime_error'],
        'module_unmapped_after_delete': 'RESOURCE_MODULE_AFTER_DELETE mapped=0 observation_extra_loader_refs=0' in out,
        'peer_cleanup_and_module_complete': scene.get('close_contract',{}).get('B',{}).get('ok') is True and 'RESOURCE_MODULE_AFTER_DELETE mapped=0 observation_extra_loader_refs=0' in (root/'B.stdout').read_text(errors='replace'),
        'no_sanitizer_or_timeout': not scene.get('asan') and not any(x in err for x in ('AddressSanitizer','runtime error:','terminate')),
    }
    result=dict(checks=checks,passed=all(checks.values()),raw_scene_passed=scene.get('passed'),raw_exit=scene.get('exit'),business_oracle=scene.get('oracle_line'))
    (root/'EXPECTED_NEGATIVE.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=Path);parser.add_argument('--side',choices=('A',),default='A')
    args=parser.parse_args();result=check(args.root,args.side);print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
