"""Explicit host fixtures only: no native ROS or DSoftBus verification."""
import base64
import copy
from dataclasses import FrozenInstanceError
import hashlib
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parameter_functional_oracle as F
import parameter_native_oracle as V1
import run_parameter_native as H
from test_parameter_native import fixtures, capture, SCENARIO, RUN


def marked():
    a, b = fixtures()
    for records in (a, b):
        for row, unused in V1._captures(records):
            row.update(native_roundtrip_equal=True, cdr_origin='LOCAL_RCLPY_SERIALIZE_MESSAGE')
    return a, b


def raw_capture(row, raw):
    row.update(cdr_base64=base64.b64encode(raw).decode('ascii'), cdr_bytes=len(raw),
               cdr_sha256=hashlib.sha256(raw).hexdigest())


class FunctionalTests(unittest.TestCase):
    def test_exact_and_excess_bytes_remain_distinct_and_immutable(self):
        for suffix in (b'', b'\0' * 8, b'nonzero\xff\0tail'):
            a, b = marked()
            row = V1.select(a, 'RESPONSE')[3]
            raw = base64.b64decode(row['cdr_base64'])
            raw_capture(row, raw + suffix)
            before = copy.deepcopy((a, b))
            result = F.business(a, b, SCENARIO)
            self.assertEqual(result, F.business(a, b, SCENARIO))
            self.assertEqual((a, b), before)
            self.assertEqual(result['rpc_count'], 14)
            self.assertEqual(result['mutation_events'], 12)
            self.assertEqual(result['standalone_serialization_compatibility'], 'OPEN')
            audit = next(c for c in result['captures'] if c['identity'].get('event') == 'RESPONSE' and
                         c['identity'].get('call') == 4)
            self.assertEqual(audit['schema_consumed_bytes'], len(raw))
            self.assertEqual(base64.b64decode(audit['suffix_base64']), suffix)
            self.assertEqual(audit['suffix_sha256'], hashlib.sha256(suffix).hexdigest())
            if suffix:
                with self.assertRaisesRegex(ValueError, 'Trailing CDR bytes'):
                    V1.business(a, b, SCENARIO)
            else:
                V1.business(a, b, SCENARIO)

    def test_marker_and_raw_corruption_fail(self):
        for mode in ('missing', 'false', 'integer_true', 'origin', 'hash', 'size', 'size_bool',
                     'base64', 'header', 'truncated', 'value', 'value_type', 'count'):
            a, b = marked()
            row = V1.select(a, 'REQUEST')[1]
            if mode == 'missing': del row['native_roundtrip_equal']
            elif mode == 'false': row['native_roundtrip_equal'] = False
            elif mode == 'integer_true': row['native_roundtrip_equal'] = 1
            elif mode == 'origin': row['cdr_origin'] = 'WIRE'
            elif mode == 'hash': row['cdr_sha256'] = '0' * 64
            elif mode == 'size': row['cdr_bytes'] += 1
            elif mode == 'size_bool': row['cdr_bytes'] = True
            elif mode == 'base64': row['cdr_base64'] = '!'
            elif mode == 'value': row['value']['names'][0] += 'x'
            elif mode == 'value_type': row['value']['names'][0] = 1
            else:
                raw = base64.b64decode(row['cdr_base64'])
                if mode == 'header': raw = b'\0\7\0\0' + raw[4:]
                elif mode == 'truncated': raw = raw[:-1]
                else: raw = raw[:4] + b'\xff' * 4 + raw[8:]
                raw_capture(row, raw)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                F.business(a, b, SCENARIO)

    def test_bad_boolean_utf8_and_string_terminator(self):
        for kind, value, raw in (
                ('SetParametersAtomically.Response', {'result': {'successful': True, 'reason': ''}},
                 bytes.fromhex('00010000020000000100000000')),
                ('GetParameters.Request', {'names': ['a']}, bytes.fromhex('000100000100000002000000ff00')),
                ('GetParameters.Request', {'names': ['a']}, bytes.fromhex('0001000001000000020000006161'))):
            row = dict(capture(kind, value), native_roundtrip_equal=True, cdr_origin='LOCAL_RCLPY_SERIALIZE_MESSAGE')
            raw_capture(row, raw)
            with self.subTest(kind=kind, raw=raw), self.assertRaises(ValueError):
                F.audit_capture(row, kind)

    def test_structural_padding_is_not_assumed_zero(self):
        kind = 'SetParametersAtomically.Response'
        row = dict(capture(kind, {'result': {'successful': True, 'reason': ''}}),
                   native_roundtrip_equal=True, cdr_origin='LOCAL_RCLPY_SERIALIZE_MESSAGE')
        raw = bytearray(base64.b64decode(row['cdr_base64']))
        raw[5:8] = b'\xfe\xfd\xfc'
        raw_capture(row, raw)
        self.assertEqual(F.audit_capture(row, kind)['suffix_bytes'], 0)

    def test_functional_errors_cannot_become_compatibility_warnings(self):
        for mode in ('call', 'service', 'order', 'missing_event', 'duplicate_event', 'foreign_node',
                     'wrong_atomic_readback', 'server_commit', 'discovery'):
            a, b = marked()
            row = V1.select(a, 'RESPONSE')[10]
            if mode == 'call': row['call'] = 10
            elif mode == 'service': row['service'] = 'Unknown'
            elif mode == 'order': row['seq'] = 0
            elif mode == 'missing_event': a.remove(V1.select(a, 'PARAMETER_EVENT')[-1])
            elif mode == 'duplicate_event': a.append(copy.deepcopy(V1.select(a, 'PARAMETER_EVENT')[-1]))
            elif mode == 'server_commit': b.remove(V1.select(b, 'SERVER_COMMIT')[0])
            elif mode == 'discovery': V1.one(a, 'CLIENT_READY')['matched_publishers'] = 1
            else:
                if mode == 'foreign_node':
                    row = V1.select(a, 'PARAMETER_EVENT')[0]
                    value = copy.deepcopy(row['value'])
                    value['node'] += '_foreign'
                    row.update(capture('ParameterEvent', value))
                else:
                    value = copy.deepcopy(row['value'])
                    value['values'][1] = V1.value(2, 123)
                    row.update(capture('GetParameters.Response', value))
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                F.business(a, b, SCENARIO)

    def test_old_unmarked_rows_fail_v2(self):
        a, b = fixtures()
        V1.business(a, b, SCENARIO)
        with self.assertRaisesRegex(ValueError, 'roundtrip'):
            F.business(a, b, SCENARIO)

    def test_wrong_role_records_reject_in_rows_and_both_wrappers(self):
        import json
        for side, events in (('A', V1.ROLE_EVENTS['B'] - V1.COMMON_EVENTS),
                             ('B', V1.ROLE_EVENTS['A'] - V1.COMMON_EVENTS)):
            for event in events:
                a, b = marked()
                source = a if side == 'B' else b
                row = copy.deepcopy(next((r for r in source if r['event'] == event), {'event': event}))
                (a if side == 'A' else b).append(row)
                for oracle in (V1, F):
                    with self.subTest(side=side, event=event, oracle=oracle.__name__), self.assertRaises(ValueError):
                        oracle.business(a, b, SCENARIO)
                row.update(schema=1, scenario=SCENARIO, instance=side, run=RUN, side=side, pid=123,
                           seq=1, role='requester' if side == 'A' else 'responder', monotonic_ns=1)
                with self.subTest(side=side, event=event), self.assertRaises(ValueError):
                    V1.rows('R25_PROBE ' + json.dumps(row), SCENARIO, side, RUN, 123)

    def test_release_schema_boolean_rejected_by_constructor(self):
        args = SimpleNamespace(output=Path('unused'), prepared=Path('plan'), release=Path('release'), hdc=Path('hdc'))
        plan = {'scenario': SCENARIO}
        for selected in (H.V1, H.V2):
            with self.subTest(profile=selected.scope), \
                    patch.object(H, 'fresh', return_value=Path('unused')), \
                    patch.object(H, 'checked_plan', return_value=(plan, {}, {}, {}, None)), \
                    patch.object(H, 'sha', return_value='hash'), \
                    patch.object(H, 'read', return_value={'schema': True, 'scope': selected.release_scope,
                        'scenario': SCENARIO, 'prepared_sha256': 'hash', 'runner_sha256': 'hash'}), \
                    patch.object(H.BAG, 'bag_selected_hdc') as hdc, patch.object(Path, 'mkdir') as mkdir:
                with self.assertRaises(ValueError):
                    H.ParameterScene(args, selected)
                hdc.assert_not_called()
                mkdir.assert_not_called()

    def test_fixed_profiles_and_crossed_plan_reject_before_admission(self):
        with self.assertRaises(FrozenInstanceError):
            H.V2.scope = H.V1.scope
        with self.assertRaises(ValueError):
            H.checked_acceptance(H.Acceptance(H.V2.scope, H.V2.release_scope, F))
        for selected, other in ((H.V1, H.V2), (H.V2, H.V1)):
            plan = dict(schema=1, scope=other.scope, scenario=SCENARIO, runs={'A': RUN, 'B': RUN},
                        limits=dict(H.LIMITS), derivation='', qualification='', package='', files={},
                        bindings={}, routes='', external='', identity='')
            with patch.object(H, 'admission') as admitted, self.assertRaises(ValueError):
                H.validate_plan(plan, selected)
            admitted.assert_not_called()

    def test_cross_release_rejects_before_dispatch_and_output(self):
        args = SimpleNamespace(output=Path('unused'), prepared=Path('plan'), release=Path('release'), hdc=Path('hdc'))
        plan = {'scenario': SCENARIO}
        with patch.object(H, 'fresh', return_value=Path('unused')), \
                patch.object(H, 'checked_plan', return_value=(plan, {}, {}, {}, None)), \
                patch.object(H, 'sha', return_value='hash'), \
                patch.object(H, 'read', return_value={'schema': 1, 'scope': H.V1.release_scope,
                    'scenario': SCENARIO, 'prepared_sha256': 'hash', 'runner_sha256': 'hash'}), \
                patch.object(H.BAG, 'bag_selected_hdc') as hdc, patch.object(Path, 'mkdir') as mkdir:
            with self.assertRaises(ValueError):
                H.ParameterScene(args, H.V2)
            hdc.assert_not_called()
            mkdir.assert_not_called()

    def test_probe_passes_full_bytes_before_success_marker(self):
        spec = importlib.util.spec_from_file_location('parameter_probe_functional_host', H.PROBE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        raw = b'full\0native\xffbytes'
        for mode in ('pass', 'serialize_error', 'deserialize_error', 'mismatch'):
            emitted, received = [], []
            def serialize(message):
                if mode == 'serialize_error': raise RuntimeError('serialize failed')
                return raw
            def deserialize(data, kind):
                received.append(data)
                if mode == 'deserialize_error': raise RuntimeError('deserialize failed')
                return 8 if mode == 'mismatch' else 7
            probe = SimpleNamespace(emit=lambda event, **fields: emitted.append(fields))
            with self.subTest(mode=mode), patch.dict(sys.modules, {'rclpy.serialization': SimpleNamespace(
                    serialize_message=serialize, deserialize_message=deserialize)}):
                if mode == 'pass':
                    module.ParameterProbe.capture(probe, 'REQUEST', 7)
                    self.assertIs(received[0], raw)
                    self.assertIs(emitted[0]['native_roundtrip_equal'], True)
                    self.assertEqual(base64.b64decode(emitted[0]['cdr_base64']), raw)
                else:
                    with self.assertRaises(RuntimeError):
                        module.ParameterProbe.capture(probe, 'REQUEST', 7)
                    self.assertFalse(emitted)


if __name__ == '__main__':
    unittest.main()
