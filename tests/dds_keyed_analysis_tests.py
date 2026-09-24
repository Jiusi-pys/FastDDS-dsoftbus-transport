"""Mutations of actual keyed fixture logs exercise the shipped evidence oracle."""
import base64
import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
try:
    import analyze_dds_keyed as analyzer
except ModuleNotFoundError:
    analyzer = None

FIXTURE = json.loads(Path(__file__).with_name('keyed_trace_fixtures.json').read_text())
MATRIX_BYTES = base64.b64decode(FIXTURE['matrix_base64'])
MATRIX = json.loads(MATRIX_BYTES)


class KeyedAnalysisTests(unittest.TestCase):
    def renumber(self,c):
        for side in 'AB':
            order=0
            for line,row in enumerate(c['records'][side],1):
                row['_line']=line
                if row.get('_kind') in ('KEYED','MATRIX'):
                    order+=1;row['order']=str(order)
            phases={r['phase']:r for r in c['records'][side] if r.get('event')=='PHASE_DONE'}
            for event in c['control']:
                if event.get('event')=='CONTROL_PHASE_OBSERVED' and event['side']==side:event['order']=int(phases[event['phase']]['order'])
        return c

    def case(self, name):
        self.assertIsNotNone(analyzer, 'shipped keyed analyzer does not exist')
        item = copy.deepcopy(FIXTURE['cases'][name])
        item['records'] = {s: analyzer.parse_text(item['logs'][s], s) for s in 'AB'}
        return item

    def native_contract(self):
        """Synthetic trace-format contract atop real K1; NEVER device evidence."""
        c=self.case('K1');c['config']['mode']='BYTES_NATIVE';run=c['config']['run']
        keys={'A':'a'*32,'B':'b'*32};ports={'A':'7301','B':'7302'}
        write_rows=self.rows(c,'WRITE_RETURN','A')
        for side in 'AB':
            peer='B' if side=='A' else 'A';rows=c['records'][side]
            self.rows(c,'SCOPE',side)[0]['scope']='BYTES_NATIVE'
            pq=self.rows(c,'PARTICIPANT_QOS',side)[0]
            pq.update(native_descriptor_count='1',udp_descriptor_count='0',default_locators=f'167:{keys[side]}:{ports[side]}',metatraffic_locators=f'167:{keys[side]}:{ports[side]}',initial_peer_locators=f'167:{keys[peer]}:{ports[peer]}')
            def keyed(event,**kw):
                return {'_kind':'KEYED','_side':side,'run':run,'case':'K1','side':side,'pid':str(c['processes'][side]['pid']),'event':event,**{k:str(v) for k,v in kw.items()}}
            def native(event,inv='99',**kw):
                return {'_kind':'EXPERIMENT_UNQUALIFIED','_side':side,'run':run,'event':event,'instance_id':'1','invocation':inv,'socket':'5','owner':'accepted','rc':'0','len':'1024','hash':'0123456789abcdef','receiver_lease_id':'21','local_kind':'167','local_key':keys[side],'local_port':ports[side],'peer_kind':'167','peer_key':keys[peer],'peer_port':ports[peer],**{k:str(v) for k,v in kw.items()}}
            def frames(event):
                return [{'_kind':'EXPERIMENT_DATA','_side':side,'run':run,'event':event,'instance_id':'1','invocation':'99','writer_guid':w['writer_guid'],'sequence_hi':w['sn_hi'],'sequence_lo':w['sn_lo'],'native_len':'1024','native_hash':'0123456789abcdef'} for w in write_rows]
            def app(stage):
                return {'_kind':'EXPERIMENT_APPLICATION_RX','_side':side,'stage':str(stage),'runtime_id':'1','instance_id':'1','application_incarnation':'11','receiver_incarnation':'21','logical_token':'31','invocation':'99','bytes':'1024'}
            rows[2:2]=[keyed('NATIVE_IDENTITY',endpoint='A1' if side=='A' else 'B1',participant_guid=analyzer.guid(run,'W1' if side=='A' else 'R1',True),runtime=1,native_instance=1,application=11,receiver=21,token=31),native('SendBytes_return','1',spdp=1,sedp_pub=1 if side=='A' else 0,sedp_sub=1 if side=='B' else 0),native('FastDDS_receiver_return','2',spdp=1,sedp_pub=1 if side=='B' else 0,sedp_sub=1 if side=='A' else 0)]
            if side=='A':
                insert=max(i for i,r in enumerate(rows) if r.get('event')=='WRITE_RETURN')+1
                rows[insert:insert]=[{'_kind':'EXPERIMENT_APPLICATION_TX','_side':side,'run':run,'stage':'TX_ACCEPTED','instance_id':'1','application_incarnation':'11','logical_token':'31','invocation':'99'},native('SendBytes_return'),*frames('SendBytes_return')]
            else:
                insert=next(i for i,r in enumerate(rows) if r.get('event')=='DATA')
                rows[insert:insert]=[app(0),app(1),native('OnBytes_owned_copy'),*frames('OnBytes_owned_copy'),app(3),app(4),native('FastDDS_receiver_enter'),*frames('FastDDS_receiver_enter'),native('FastDDS_receiver_return'),*frames('FastDDS_receiver_return'),app(5)]
            rows.append({'_kind':'STAGE','_side':side,'run':run,'stage':'KEYED_NATIVE_FINALIZED','clean':'1','dds_clean':'1','runtime_clean':'1','process_budget_settled':'1'})
        return self.renumber(c)

    def run_case(self, case, native=False):
        return analyzer.analyze_records(case['config'], case['records'], case['control'], MATRIX,
            matrix_sha256=FIXTURE['matrix_sha256'], require_native=native,
            processes=case['processes'], consumed=case['consumed'])

    def rows(self, item, event, side='B'):
        return [r for r in item['records'][side] if r.get('event') == event]

    def reject(self, item):
        result = self.run_case(item)
        self.assertFalse(result['pass'], result)
        self.assertTrue(result['errors'] or result['native']['errors'])

    def test_all_actual_reference_cases(self):
        for name in FIXTURE['cases']:
            with self.subTest(case=name):
                result = self.run_case(self.case(name))
                self.assertTrue(result['pass'], result['errors'])
                self.assertFalse(result['native_proof_pass'])

    def test_reference_never_native(self):
        r = self.run_case(self.case('K1'), native=True)
        self.assertFalse(r['pass']); self.assertFalse(r['native_proof_pass'])

    def test_keyed_type_kind(self):
        c=self.case('K1');self.rows(c,'ENTITY_CREATED','A')[0]['entity_kind']='3';self.reject(c)

    def test_true_key_handle(self):
        c=self.case('K1');self.rows(c,'REGISTER_RETURN','A')[0]['computed_handle']='0'*32;self.reject(c)

    def test_missing_other_key_same_ordinal(self):
        c=self.case('K1');c['records']['B']=[r for r in c['records']['B'] if not(r.get('event')=='DATA' and r.get('key')=='34')];self.reject(c)

    def test_writer_conflation_same_key(self):
        c=self.case('K2');data=self.rows(c,'DATA');data[-1]['writer_guid']=data[0]['writer_guid'];self.reject(c)

    def test_same_key_alias_handle(self):
        c=self.case('K2');self.rows(c,'DATA')[-1]['instance_handle']='a'*32;self.reject(c)

    def test_wrong_reader(self):
        c=self.case('K1');self.rows(c,'DATA')[0]['reader_guid']='a'*32;self.reject(c)

    def test_invalid_data_payload_access(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['payload_accessed']='1';self.reject(c)

    def test_invalid_data_payload_fields(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['ordinal']='1';self.reject(c)

    def test_state_wrong_key(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['known_key']='34';self.reject(c)

    def test_state_wrong_writer(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['writer_guid']='b'*32;self.reject(c)

    def test_state_unknown_not_retroactively_known(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['mapping_source']='CURRENT_LOOKUP';self.reject(c)

    def test_state_policy_false_not_disposed(self):
        c=self.case('K4_FALSE');self.rows(c,'INSTANCE_STATE')[0]['instance_state']='2';self.reject(c)

    def test_revival_before_state_barrier(self):
        c=self.case('K3');self.rows(c,'WRITE_RETURN','A')[-1]['phase']='CHANGE';self.reject(c)

    def test_early_take_is_not_eviction(self):
        c=self.case('H2');self.rows(c,'API_RETURN')[0]['operation']='take';self.reject(c)

    def test_history_app_resend(self):
        c=self.case('H1');w=self.rows(c,'WRITE_RETURN','A')[-1];w.update(key='17',ordinal='3');self.reject(c)

    def test_history_missing_not_labeled_eviction(self):
        c=self.case('H1');c['records']['B']=[r for r in c['records']['B'] if not(r.get('event')=='DATA' and r.get('ordinal')=='3')];self.reject(c)

    def test_keep_all_read_does_not_count_network_duplicates(self):
        r=self.run_case(self.case('H3'));self.assertTrue(r['pass'],r['errors']);self.assertEqual(r['unique_data_identities'],8);self.assertGreater(r['data_operation_returns'],8)

    def test_keep_all_masks(self):
        c=self.case('H3');next(r for r in self.rows(c,'API_RETURN') if r.get('phase')=='CHANGE' and r.get('sample_mask')=='1')['sample_mask']='3';self.reject(c)

    def test_missing_loan(self):
        c=self.case('H3');loan=self.rows(c,'RETURN_LOAN')[0];c['records']['B'].remove(loan);self.reject(c)

    def test_send_and_wait_without_reader(self):
        c=self.case('R3');c['records']['B']=[r for r in c['records']['B'] if r.get('event')!='DATA'];self.reject(c)

    def test_max_instances_C_not_silent_success(self):
        c=self.case('R2');self.rows(c,'REGISTER_RETURN','A')[-1]['defined']='1';self.reject(c)

    def test_R1_inconsistent_rejection(self):
        c=self.case('R1');self.rows(c,'INVALID_SET_QOS','A')[0]['rc']='0';self.reject(c)

    def test_R1_no_device_pass(self):
        c=self.case('R1');c['config']['mode']='BYTES_NATIVE';self.reject(c)

    def test_no_app_future_adoption(self):
        c=self.case('K1');r=c['records']['B'][0];r.update(reason='NO_APPLICATION',instance_id='7',invocation='8');c['records']['B'][1].update(event='FastDDS_receiver_enter',instance_id='7',invocation='8');self.reject(c)

    def test_old_snapshot_not_current(self):
        c=self.case('K1');s=self.rows(c,'MATCH_SNAPSHOT')[-1];s['order']=self.rows(c,'MATCH_SNAPSHOT')[0]['order'];self.reject(c)

    def test_unknown_upstream_never_harmless(self):
        c=self.case('K4_TRUE');self.rows(c,'INSTANCE_STATE')[0]['support']='UPSTREAM_NOT_IMPLEMENTED';self.reject(c)

    def test_missing_control_positive(self):
        c=self.case('K1');c['control']=[];self.reject(c)

    def test_missing_phase_log(self):
        c=self.case('K1');c['records']['B']=[r for r in c['records']['B'] if not(r.get('event')=='PHASE_DONE' and r.get('phase')=='PRE')];self.reject(c)

    def test_wrong_payload(self):
        c=self.case('K1');self.rows(c,'DATA')[0]['payload_hex']='0'*64;self.reject(c)

    def test_wrong_qos(self):
        c=self.case('H2');self.rows(c,'LOCAL_QOS')[0]['depth']='3';self.reject(c)

    def test_repeated_identity_in_single_read_not_valid(self):
        c=self.case('H3');a=next(r for r in self.rows(c,'API_RETURN') if r.get('phase')=='CHANGE' and r.get('sample_mask')=='3')
        data=[r for r in self.rows(c,'DATA') if r['operation_id']==a['operation_id']]
        clone=copy.deepcopy(data[0]);c['records']['B'].insert(c['records']['B'].index(data[-1])+1,clone);a['count']='9'
        self.reject(self.renumber(c))

    def test_mask_does_not_match_actual_sample_state(self):
        c=self.case('H3');next(r for r in self.rows(c,'DATA') if r.get('sample_mask')=='1')['sample_state']='2';self.reject(c)

    def test_missing_initial_register_cannot_be_hidden_by_later_handle(self):
        c=self.case('K4_TRUE');c['records']['A'].remove(self.rows(c,'REGISTER_RETURN','A')[0]);self.reject(c)

    def test_state_known_SN_cannot_alias_business_data(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0]['sn_lo']='1';self.reject(c)

    def test_Reader_pre_delivery_moved_past_PRE_barrier(self):
        c=self.case('K1');rows=c['records']['B'];batch=[r for r in rows if r.get('event') in ('API_RETURN','DATA','RETURN_LOAN') and r.get('phase')=='PRE']
        for r in batch:rows.remove(r);r['phase']='CHANGE'
        index=next(i for i,r in enumerate(rows) if r.get('event')=='PHASE_DONE' and r.get('phase')=='CHANGE')
        rows[index:index]=batch;self.reject(self.renumber(c))

    def test_native_trace_contract_not_device_evidence(self):
        r=self.run_case(self.native_contract(),native=True);self.assertTrue(r['pass'],r)
        self.assertEqual(len(r['native']['sample_joins']),8)

    def test_native_missing_original_lease(self):
        c=self.native_contract();next(r for r in c['records']['B'] if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='1')['application_incarnation']='12';self.reject(c)

    def test_native_wrong_route(self):
        c=self.native_contract();next(r for r in c['records']['B'] if r.get('event')=='FastDDS_receiver_enter' and r.get('_kind')=='EXPERIMENT_UNQUALIFIED')['peer_key']='c'*32;self.reject(c)

    def test_native_missing_return(self):
        c=self.native_contract();c['records']['B']=[r for r in c['records']['B'] if not(r.get('event')=='FastDDS_receiver_return' and r.get('invocation')=='99' and r.get('_kind')=='EXPERIMENT_UNQUALIFIED')];self.reject(c)

    def test_native_guid_sn_not_ordinal_join(self):
        c=self.native_contract();next(r for r in c['records']['B'] if r.get('_kind')=='EXPERIMENT_DATA' and r.get('event')=='OnBytes_owned_copy')['sequence_lo']='999';self.reject(c)

    def test_native_unsuccessful_send_not_delivery_evidence(self):
        c=self.native_contract();next(r for r in c['records']['A'] if r.get('_kind')=='EXPERIMENT_UNQUALIFIED' and r.get('invocation')=='99')['rc']='-1';self.reject(c)

    def test_native_wrong_participant_identity(self):
        c=self.native_contract();self.rows(c,'NATIVE_IDENTITY')[0]['participant_guid']='a'*32;self.reject(c)

    def test_unknown_state_requires_joint_unknown_GUID_SN(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0].update(identity_known='0',writer_guid='UNKNOWN');self.reject(c)

    def test_genuine_unknown_state_is_not_fabricated(self):
        c=self.case('K3');self.rows(c,'INSTANCE_STATE')[0].update(identity_known='0',writer_guid='UNKNOWN',sn_hi='UNKNOWN',sn_lo='UNKNOWN')
        r=self.run_case(c);self.assertTrue(r['pass'],r);self.assertEqual(r['instance_state_ledger'][0]['sn_lo'],'UNKNOWN')

    def test_owned_copy_capture_cannot_follow_final_cleanup(self):
        c=self.native_contract();rows=c['records']['B'];late=next(r for r in rows if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='1')
        rows.remove(late);rows.append(late);self.reject(self.renumber(c))

    def test_receiver_cannot_run_before_admission(self):
        c=self.native_contract();rows=c['records']['B'];a=next(r for r in rows if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='3');b=next(r for r in rows if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='4')
        a['stage'],b['stage']='4','3';self.reject(c)

    def test_settlement_cannot_precede_receiver_return(self):
        c=self.native_contract();rows=c['records']['B'];settled=next(r for r in rows if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='5');rows.remove(settled)
        i=next(i for i,r in enumerate(rows) if r.get('_kind')=='EXPERIMENT_UNQUALIFIED' and r.get('event')=='FastDDS_receiver_return' and r.get('invocation')=='99');rows.insert(i,settled);self.reject(self.renumber(c))

    def test_actual_terminal_no_app_marker_cannot_be_adopted(self):
        c=self.native_contract();marker=analyzer.parse_text('EXPERIMENT_DROP_NO_APPLICATION run='+c['config']['run']+' runtime_id=1 instance_id=1 socket=5 invocation=99 disposition=TERMINAL_NOT_SAVED process_steady_us=1\n','B')[0]
        c['records']['B'].insert(0,marker);self.reject(self.renumber(c))

    def test_queued_diagnostic_may_lag_worker(self):
        c=self.native_contract();rows=c['records']['B'];queued=copy.deepcopy(next(r for r in rows if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='1'));queued['stage']='2'
        i=next(i for i,r in enumerate(rows) if r.get('_kind')=='EXPERIMENT_APPLICATION_RX' and r.get('stage')=='5');rows.insert(i+1,queued)
        r=self.run_case(self.renumber(c),native=True);self.assertTrue(r['pass'],r)

    def test_fixture_matrix_hash(self):
        self.assertEqual(hashlib.sha256(MATRIX_BYTES).hexdigest(),FIXTURE['matrix_sha256'])

    def test_current_matrix_replica_hash(self):
        current=base64.b64decode(FIXTURE['current_matrix_base64'])
        self.assertEqual(hashlib.sha256(current).hexdigest(),FIXTURE['current_matrix_sha256'])
        self.assertEqual([c['case_id'] for c in json.loads(current)['cases']],[c['case_id'] for c in MATRIX['cases']])


if __name__=='__main__':unittest.main()
