"""Host-only admission-ledger contracts; never SDK/device execution evidence."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('rx_accounting', Path(__file__).resolve().parents[1] / 'scripts/analyze_rx_admission.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
RUN = '1234567890abcdef'


def line(tag, **values):
    return tag + ' ' + ' '.join(f'{key}={value}' for key, value in values.items())


def record(**updates):
    value = dict(schema=1, run=RUN, instance_id=1, runtime_id=1, callback_slot=8,
        socket=2, invocation=5, stage='COMMIT', reason='ACCEPTED_QUEUE', result_known=1,
        result=0, candidate_count=1, size=12, endpoint_limit_known=1, endpoint_limit=60000,
        record_limit_known=1, record_limit=60000, native_status_known=1, native_status=0,
        native_known=1, native_slot=0, native_incarnation=1, native_generation=1,
        native_route_version=1, identity_origin='ADAPTER_CAPTURE_NOT_SDK_EVENT',
        application_known=1, application_incarnation=1, logical_token=2,
        receiver_known=1, receiver_incarnation=3,
        receiver_identity_kind='ENTRY_SNAPSHOT_NOT_EXECUTION_LEASE', entry_state_known=1,
        entry_state=11, adapter_state_known=1, adapter_state=11, ingress_receiver_marker=3,
        lifecycle_known=1, stopped=0, application_phase=0, receiver_phase=2,
        capacity_known=1, capacity_mask=0, records_used=0, records_limit=8,
        bytes_used=0, bytes_limit=1024, local_records_used=0, local_records_limit=4,
        local_bytes_used=0, local_bytes_limit=512, copying_used=0, copying_limit=2,
        application_copying=0, application_copying_limit=18446744073709551615,
        payload_identity='UNOBSERVED')
    value.update(updates)
    return value


def fixture(records=None, summaries=None, counts=None):
    records = records if records is not None else [record()]
    native = [line('EXPERIMENT_NATIVE_INGRESS', run=RUN, instance_id=r['instance_id'],
                   invocation=r['invocation'], native_event='OnBytes', callback_slot=r['callback_slot'], socket=r['socket']) for r in records]
    rows = [line('scope=REAL_FASTDDS_DSOFTBUS_ENGINEERING_EXPERIMENT', run=RUN)] + native
    rows += [line('EXPERIMENT_RX_ADMISSION', **r) for r in records]
    if summaries is None:
        summaries = [dict(schema=1, run=RUN, instance_id=1, snapshot_seq=1, total=len(records),
            stored=len(records), overflow=0, capacity=1024, emitted_records=len(records),
            complete_records=1, coverage='OBSERVED_UNTIL_SNAPSHOT', record_bytes=320, store_bytes=327680)]
    for summary in summaries:
        rows.append(line('EXPERIMENT_RX_ADMISSION_SUMMARY', **summary))
        grouped = counts if counts is not None else {reason:sum(r['reason']==reason for r in records) for reason in {r['reason'] for r in records}}
        for reason, count in grouped.items():
            rows.append(line('EXPERIMENT_RX_ADMISSION_REASON_COUNT', schema=1, run=RUN,
                instance_id=summary['instance_id'], snapshot_seq=summary['snapshot_seq'],
                reason=reason, count=count, unit='CALLBACK_ADMISSION'))
    rows.append(line('EXPERIMENT_RX_ENTRY_UNCORRELATED_SUMMARY', schema=1,total=0,
                     unit='UNCORRELATED_CALLBACK_ENTRY',coverage='OBSERVED_UNTIL_SNAPSHOT'))
    return '\n'.join(rows)


class AdmissionAnalysisTests(unittest.TestCase):
    def analyse(self, text):
        return MODULE.analyze_text(text, RUN)

    def reject(self, text):
        self.assertFalse(self.analyse(text)['complete'])

    def test_accepted(self):
        value = self.analyse(fixture())
        self.assertTrue(value['complete'], value)
        self.assertEqual(value['units']['native_callbacks'], 1)
        self.assertEqual(value['units']['candidate_attempts'], 1)

    def test_capacity_multiple_predicates_is_one_decision(self):
        r = record(stage='CAPTURE',reason='GLOBAL_RECORDS',result=6,capacity_mask=5,
                   records_used=8,local_records_used=4)
        value=self.analyse(fixture([r]))
        self.assertTrue(value['complete'],value)
        self.assertEqual(value['units']['rejected_callbacks'],1)
        self.assertEqual(value['units']['native_callbacks'],1)

    def test_capacity_first_reason_order(self):
        self.reject(fixture([record(stage='CAPTURE',reason='LOCAL_RECORDS',result=6,
            capacity_mask=5,records_used=8,local_records_used=4)]))

    def test_inconsistent_budget_mask(self):
        self.reject(fixture([record(stage='CAPTURE',reason='GLOBAL_RECORDS',result=6,capacity_mask=1)]))

    def test_unknown_is_not_harmless(self):
        self.reject(fixture([record(stage='UNOBSERVED',reason='UNOBSERVED',result_known=0,result=-1)]))

    def test_missing_terminal_record(self):
        self.reject('\n'.join(s for s in fixture().splitlines() if not s.startswith('EXPERIMENT_RX_ADMISSION ')))

    def test_missing_ingress(self):
        self.reject('\n'.join(s for s in fixture().splitlines() if not s.startswith('EXPERIMENT_NATIVE_INGRESS ')))

    def test_duplicate_record(self):
        self.reject(fixture()+ '\n'+line('EXPERIMENT_RX_ADMISSION', **record()))

    def test_mixed_run(self):
        self.reject(fixture().replace('EXPERIMENT_RX_ADMISSION schema=1 run='+RUN,
                                    'EXPERIMENT_RX_ADMISSION schema=1 run=ffffffffffffffff'))

    def test_candidate_fanout_not_assumed(self):
        self.reject(fixture([record(candidate_count=2)]))

    def test_no_application_zero_candidate(self):
        value=self.analyse(fixture([record(stage='COORDINATOR',reason='NO_APPLICATION',result=3,
             candidate_count=0,application_known=0,application_incarnation=0,logical_token=0,
             receiver_known=0,receiver_incarnation=0,lifecycle_known=0,capacity_known=0)]))
        self.assertTrue(value['complete'],value)
        self.assertEqual(value['units']['candidate_attempts'],0)

    def test_accepted_bad_owner(self):
        self.reject(fixture([record(adapter_state=0)]))

    def test_invalid_input_not_parsed(self):
        value=self.analyse(fixture([record(stage='ADAPTER',reason='NULL_BUFFER',result_known=0,
            result=-1,size=12,candidate_count=0,capacity_known=0,payload_identity='UNOBSERVED')]))
        self.assertTrue(value['complete'],value)
        self.reject(fixture([record(stage='ADAPTER',reason='NULL_BUFFER',result_known=0,
            result=-1,capacity_known=0,payload_identity='HASHED')]))

    def test_result_not_fabricated(self):
        self.reject(fixture([record(stage='ADAPTER',reason='CLOSING',result_known=0,result=0,capacity_known=0)]))

    def test_cumulative_summaries_not_added(self):
        text=fixture();summary=[s for s in text.splitlines() if s.startswith('EXPERIMENT_RX_ADMISSION_SUMMARY ')][0]
        count=[s for s in text.splitlines() if s.startswith('EXPERIMENT_RX_ADMISSION_REASON_COUNT ')][0]
        value=self.analyse(text+'\n'+summary.replace('snapshot_seq=1','snapshot_seq=2')+'\n'+count.replace('snapshot_seq=1','snapshot_seq=2'))
        self.assertTrue(value['complete'],value)
        self.assertEqual(value['units']['native_callbacks'],1)

    def test_reason_total_mismatch(self):
        self.reject(fixture(counts={'ACCEPTED_QUEUE':2}))

    def test_overflow_is_incomplete(self):
        self.reject(fixture().replace('overflow=0','overflow=1').replace('total=1','total=2').replace('complete_records=1','complete_records=0'))

    def test_uncorrelated_entry_is_separate_incomplete_unit(self):
        text=fixture().replace('SUMMARY schema=1 total=0','SUMMARY schema=1 total=1')
        text+='\n'+line('EXPERIMENT_RX_ENTRY_UNCORRELATED',schema=1,callback_slot=7,
             reason='MISSING_STATE',count=1,unit='UNCORRELATED_CALLBACK_ENTRY',coverage='OBSERVED_UNTIL_SNAPSHOT')
        value=self.analyse(text)
        self.assertFalse(value['complete'])
        self.assertEqual(value['units']['native_callbacks'],1)
        self.assertEqual(value['units']['uncorrelated_entries'],1)

    def test_missing_summary(self):
        self.reject('\n'.join(s for s in fixture().splitlines() if not s.startswith('EXPERIMENT_RX_ADMISSION_SUMMARY ')))

    def test_wrong_unit(self):
        self.reject(fixture().replace('unit=CALLBACK_ADMISSION','unit=DDS_SAMPLE'))

    def test_snapshot_regression(self):
        self.reject(fixture().replace('snapshot_seq=1','snapshot_seq=2')+'\n'+
                    '\n'.join(s for s in fixture().splitlines() if s.startswith('EXPERIMENT_RX_ADMISSION_SUMMARY ')))

    def test_orphan_reason_snapshot_not_ignored(self):
        self.reject(fixture()+'\n'+line('EXPERIMENT_RX_ADMISSION_REASON_COUNT',schema=1,
            run=RUN,instance_id=1,snapshot_seq=99,reason='ACCEPTED_QUEUE',count=1,unit='CALLBACK_ADMISSION'))

    def test_same_invocation_different_instances(self):
        first=fixture()
        second='\n'.join(s.replace('instance_id=1','instance_id=2') for s in fixture().splitlines()
                         if not s.startswith('EXPERIMENT_RX_ENTRY_UNCORRELATED_SUMMARY'))
        result=self.analyse(first+'\n'+second)
        self.assertTrue(result['complete'],result)
        self.assertEqual(result['units']['native_callbacks'],2)
        self.assertEqual(len(result['instances']),2)

    def test_ingress_must_carry_same_run(self):
        self.reject(fixture().replace('EXPERIMENT_NATIVE_INGRESS run='+RUN,
                                     'EXPERIMENT_NATIVE_INGRESS'))

    def test_no_app_cannot_be_commit_or_have_candidate(self):
        self.reject(fixture([record(stage='COMMIT',reason='NO_APPLICATION',result=3)]))
        self.reject(fixture([record(stage='COORDINATOR',reason='NO_APPLICATION',result=3)]))

    def test_oversize_requires_original_limit_fields(self):
        r=record(stage='ADAPTER',reason='TOO_LARGE',result_known=0,result=-1,size=60001,
                 capacity_known=0,candidate_count=0)
        for key in ('endpoint_limit_known','endpoint_limit','record_limit_known','record_limit'):
            del r[key]
        self.reject(fixture([r]))

    def test_retired_requires_original_retired_snapshot(self):
        self.reject(fixture([record(stage='NATIVE_ENTRY',reason='RETIRED',result_known=0,
                                   result=-1,candidate_count=0,capacity_known=0)]))

    def test_reason_must_match_sdk_model_result(self):
        self.reject(fixture([record(stage='CAPTURE',reason='INVALID_ROUTE',result=5,capacity_known=0)]))

    def test_oversize_original_predicate(self):
        r=record(stage='ADAPTER',reason='TOO_LARGE',result_known=0,result=-1,
                 size=513,endpoint_limit=512,candidate_count=0,capacity_known=0)
        self.assertTrue(self.analyse(fixture([r]))['complete'])
        self.reject(fixture([dict(r,size=512)]))

    def test_accepted_requires_legal_original_size(self):
        self.reject(fixture([record(size=0)]))
        self.reject(fixture([record(size=60001,capacity_known=0)]))
        self.reject(fixture([record(record_limit_known=0)]))

    def test_accepted_requires_active_captured_receiver(self):
        self.reject(fixture([record(application_phase=1)]))
        self.reject(fixture([record(receiver_phase=3)]))
        self.reject(fixture([record(lifecycle_known=0)]))


if __name__=='__main__':
    unittest.main()
