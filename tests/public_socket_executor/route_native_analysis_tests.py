"""Synthetic evidence grammar tests; none of these rows are SDK/device proof."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts/public_socket'))
from analyze_bounded_native_reaccept import analyze_native_route

RUN='0123456789abcdef'


def fixture(side):
    operations=[('RegNode_return','SDK_RC'),('OnNodeOffline_enter','CALLBACK_STATUS'),('NodeOffline_processed','LOCAL_STATUS'),('OnNodeOnline_enter','CALLBACK_STATUS'),('NodeOnline_processed','LOCAL_STATUS'),('GetAllNodes_return','SDK_RC'),('GetNodeUdid_return','SDK_RC')]
    lines=[f'ROS_STARTUP_EVENT run={RUN} side={side} observation_seq={i} operation={op} value_type={kind} value=0 source=REAL_TARGET sdk_return_observed={int(kind=="SDK_RC")} callback_observed={int(kind=="CALLBACK_STATUS")} local_result_observed={int(kind=="LOCAL_STATUS")}' for i,(op,kind) in enumerate(operations,1)]
    lines += [f'ROS_STARTUP_EVENT run={RUN} side={side} observation_seq=8 operation=NodeIdentity_match value_type=LOCAL_STATUS value=1 local_result_observed=1 source=REAL_TARGET',f'ROS_STARTUP_EVENT run={RUN} side={side} observation_seq=9 operation=NodeSocket_route_binding value_type=LOCAL_STATUS value=0 detail_value=3 entry=4 local_result_observed=1 source=REAL_TARGET',f'ROS_STARTUP_EVENT run={RUN} side={side} observation_seq=10 operation=Socket_enter entry=4 sdk_entered=1 source=REAL_TARGET',f'ROS_STARTUP_EVENT run={RUN} side={side} observation_seq=11 operation=Socket_return entry=4 value_type=SOCKET_HANDLE value=4 sdk_return_observed=1 source=REAL_TARGET',f'NATIVE_REACCEPT_EVENT run={RUN} side={side} stage=OWNER_PUBLISHED outgoing_id=4 client_entry=4']
    lines += [f'NATIVE_ROUTE_EVENT run={RUN} side={side} seq=1 stage=OFFLINE revision=2 available=0 source=SDK_PUBLIC_NODE sdk_epoch=NOT_PROVEN',f'NATIVE_ROUTE_EVENT run={RUN} side={side} seq=2 stage=RESOLVED revision=3 available=1 source=SDK_PUBLIC_NODE sdk_epoch=NOT_PROVEN']
    for qos,guid in [('reliable','a'*32),('best_effort','b'*32)]:
        lines.append(f'ROS_PUBLISHER run={RUN} side={side} phase={qos} gid={guid}')
        for sequence in (1,2):
            lines.append(f'NATIVE_SENDER_SUBMISSION run={RUN} side={side} observation={sequence} sender=17 writer={guid} sequence_hi=0 sequence_lo={sequence} status=0 submission_calls=1 queue_admitted=1 route_known=0 available=0 route_revision=0 route_sample=UNKNOWN native_work=NOT_INFERRED segment=PRE source=SDK_PUBLIC_NODE')
        lines.append(f'NATIVE_SENDER_SUBMISSION run={RUN} side={side} observation=3 sender=17 writer={guid} sequence_hi=0 sequence_lo=3 status=7 submission_calls=1 queue_admitted=0 route_known=1 available=0 route_revision=2 route_sample=ADMISSION_GATE native_work=NOT_ENTERED_BY_SUBMISSION segment=OFFLINE source=SDK_PUBLIC_NODE')
    lines += [f'ROS_WRITE run={RUN} side={side} phase={q} segment=OFFLINE ordinal=1001 payload={RUN}:{q}:{side}:1001' for q in ('reliable','best_effort')]
    return '\n'.join(lines)+'\n'


class RouteEvidence(unittest.TestCase):
    def setUp(self):self.texts={s:fixture(s) for s in 'AB'}
    def test_complete_local_grammar(self):self.assertTrue(analyze_native_route(self.texts,RUN)['pass'])
    def test_optional_late_offline_history_separate(self):
        texts=copy.deepcopy(self.texts);texts['A']+=f'ROS_TAKE phase=reliable segment=OFFLINE ordinal=1001 payload={RUN}:reliable:B:1001\n'
        value=analyze_native_route(texts,RUN)
        self.assertTrue(value['pass']);self.assertEqual(value['offline_takes']['A'],1)
    def test_missing_real_chain_cannot_pass(self):
        for operation in ('RegNode_return','OnNodeOffline_enter','NodeOffline_processed','OnNodeOnline_enter','NodeOnline_processed','GetAllNodes_return','GetNodeUdid_return'):
            texts=dict(self.texts,A='\n'.join(x for x in self.texts['A'].splitlines() if 'operation='+operation+' ' not in x))
            with self.subTest(operation=operation):self.assertFalse(analyze_native_route(texts,RUN)['pass'])
    def test_false_sender_evidence_refused(self):
        for old,new in [('status=7','status=0'),('queue_admitted=0','queue_admitted=1'),('route_revision=2','route_revision=1'),('route_sample=ADMISSION_GATE','route_sample=LATER_EXPORT'),('source=REAL_TARGET','source=HOST_FAKE_INJECTION'),('source=SDK_PUBLIC_NODE','source=HOST_PUBLIC_NODE_MODEL'),('ordinal=1001','ordinal=1'),('sequence_lo=3','sequence_lo=2')]:
            texts=dict(self.texts,A=self.texts['A'].replace(old,new))
            with self.subTest(old=old):self.assertFalse(analyze_native_route(texts,RUN)['pass'])
    def test_both_qos_new_samples_required(self):
        texts=dict(self.texts,A='\n'.join(line for line in self.texts['A'].splitlines() if not ('segment=OFFLINE' in line and 'writer='+'b'*32 in line)))
        self.assertFalse(analyze_native_route(texts,RUN)['pass'])
    def test_pre_retry_cannot_be_offline_new_sample(self):
        texts=dict(self.texts,A=self.texts['A'].replace('sequence_lo=3 status=7','sequence_lo=2 status=7'))
        self.assertFalse(analyze_native_route(texts,RUN)['pass'])
    def test_same_or_changed_network_from_sdk_input_only(self):
        self.assertEqual(analyze_native_route(self.texts,RUN)['network_id_changed'],dict(A=False,B=False))
        texts=dict(self.texts,A=self.texts['A'].replace('operation=NodeSocket_route_binding value_type=LOCAL_STATUS value=0','operation=NodeSocket_route_binding value_type=LOCAL_STATUS value=1'))
        value=analyze_native_route(texts,RUN);self.assertTrue(value['pass']);self.assertTrue(value['network_id_changed']['A'])
        texts=dict(self.texts,A=self.texts['A'].replace('detail_value=3','detail_value=2'))
        self.assertFalse(analyze_native_route(texts,RUN)['pass'])


if __name__=='__main__':unittest.main()
