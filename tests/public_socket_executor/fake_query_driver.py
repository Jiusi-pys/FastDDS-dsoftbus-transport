"""Local subprocess fixture ONLY. Never invokes HDC or simulates DDS delivery."""
import json,sys
side=sys.argv[1];local=('a' if side=='A' else 'b')*64;peer=('b' if side=='A' else 'a')*64
text=f"""scope=READONLY_ROUTE_QUERY role=list-nodes
stage=GetLocalNodeDeviceInfo action=RETURN rc=0
route_kind=local network_id={local}
stage=GetAllNodeDeviceInfo action=RETURN rc=0 count=1 data_present=1
route_kind=remote index=0 network_id={peer}
stage=FreeNodeInfo action=RETURN
stage=route_summary query_valid=true remote_count=1 route_available=1 free_returned=1 allocation_present=1 node_observation_cap=32 communication=NOT_RUN
result=PASS scope=READONLY_ROUTE_QUERY fatal=0 untracked_first_id=0 untracked_count=0 cleanup=NO_SOCKET_RESOURCES token_restored=1 quiescence=NOT_VERIFIED dds=NOT_RUN transport=NOT_IMPLEMENTED
MDDS_REMOTE_EXIT=0
"""
print(json.dumps(dict(scope='LOCAL_SYNTHETIC_NO_DEVICE',stdout=text,local_exit=0,restored=True)))
