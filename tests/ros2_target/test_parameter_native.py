"""Host decoder/admission mocks only; these tests cannot prove DSoftBus traffic."""
import base64
import copy
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import parameter_native_oracle as O
import run_parameter_native as H

SCENARIO, RUN = '0123456789abcdef', 'abcdef0123456789'


def encode(kind, value, endian='<'):
    """Synthetic XCDR fixtures, deliberately not native ROS serialization."""
    out = bytearray()

    def put(t, v):
        if isinstance(t, list):
            put('I', len(v))
            for item in v:
                put(t[0], item)
        elif t in O.SCHEMAS:
            for name, field in O.SCHEMAS[t]:
                put(field, v[name])
        elif t == 's':
            raw = v.encode('utf-8') + b'\0'
            put('I', len(raw))
            out.extend(raw)
        else:
            out.extend(b'\0' * ((-len(out)) % struct.calcsize(t)))
            out.extend(struct.pack(endian + t, v))
    put(kind, value)
    return (b'\0\1\0\0' if endian == '<' else b'\0\0\0\0') + out


def capture(kind, value):
    raw = encode(kind, value)
    return dict(value=copy.deepcopy(value), cdr_base64=base64.b64encode(raw).decode(),
                cdr_sha256=hashlib.sha256(raw).hexdigest(), cdr_bytes=len(raw))


def fixtures():
    a, b = [], []
    initial, changed, atomic, final = (O.state(SCENARIO, i) for i in range(4))
    a.append(dict(event='DISCOVERY_OBSERVATION', ready_services=list(O.SERVICES), matched_publishers=2,
                  publishers=[dict(name='parameter_' + SCENARIO + '_' + side, namespace='/') for side in 'AB']))
    a.append(dict(event='CLIENT_READY', services=list(O.SERVICES), matched_publishers=2,
                  publisher_fqns=['/parameter_' + SCENARIO + '_' + side for side in 'AB'], discovery_observations=1,
                  event_qos={'history': 1, 'depth': 1000, 'reliability': 1, 'durability': 2}))
    success = dict(successful=True, reason='')
    for index, (service, request) in enumerate(O.expected_calls(SCENARIO), 1):
        a.append(dict(event='REQUEST', call=index, service=service, **capture(service + '.Request', request)))
        if index in (1, 13):
            response = {'result': {'names': [p['name'] for p in (initial if index == 1 else final)], 'prefixes': ['p' + SCENARIO]}}
        elif index == 2:
            response = {'types': [p['value']['type'] for p in initial]}
        elif index == 3:
            response = {'descriptors': [dict(name=p['name'], type=p['value']['type'], description='', additional_constraints='',
                        read_only=i == 9, dynamic_typing=i == 10, floating_point_range=[], integer_range=[]) for i, p in enumerate(initial)]}
        elif index in (4, 6, 11, 14):
            response = {'values': [p['value'] for p in {4: initial, 6: changed, 11: atomic, 14: final}[index]]}
        elif index in (5, 12):
            response = {'results': [success] * (10 if index == 5 else 1)}
        elif index == 7:
            response = {'result': success}
        else:
            response = {'result': dict(successful=False, reason={8: 'r33 callback rejects -99',
                                     9: 'Trying to set a read-only parameter.', 10: 'Wrong parameter type.'}[index])}
        a.append(dict(event='RESPONSE', call=index, service=service, **capture(service + '.Response', response)))
        if index not in (5, 7, 8, 12):
            continue
        groups = [[p] for p in request['parameters']] if index == 5 else [request['parameters']]
        for group in groups:
            b.append(dict(event='SERVER_VALIDATE', parameters=group, accepted=index != 8))
            if index == 8:
                continue
            b.append(dict(event='SERVER_COMMIT', parameters=group))
            kind = 'deleted_parameters' if index == 12 else 'new_parameters' if group[0]['name'].endswith('.dynamic') else 'changed_parameters'
            event = dict(stamp=dict(sec=123, nanosec=456), node='/parameter_' + SCENARIO + '_B',
                         new_parameters=[], changed_parameters=[], deleted_parameters=[])
            event[kind] = group
            a.append(dict(event='PARAMETER_EVENT', **capture('ParameterEvent', event)))
    a.append(dict(event='CLIENT_COMPLETE', calls=14, observed_events=12, quiet_seconds=0.25))
    b.extend([dict(event='SERVER_STATE', parameters=final), dict(event='SERVER_COMPLETE', commits=12)])
    for rows in (a, b):
        for index, row in enumerate(rows, 1):
            row['seq'] = index
    return a, b


class ParameterTests(unittest.TestCase):
    def test_finite_synthetic_positive(self):
        a, b = fixtures()
        result = O.business(a, b, SCENARIO)
        self.assertEqual(result['rpc_count'], 14)
        self.assertEqual(result['mutation_events'], 12)

    def test_actual_discovery_wait_requires_both_views_without_resetting_deadline(self):
        spec = importlib.util.spec_from_file_location('parameter_probe_discovery_host', H.PROBE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        names = ['parameter_' + SCENARIO + '_' + side for side in 'AB']
        good = [SimpleNamespace(node_name=name, node_namespace='/') for name in names]
        unknown = [good[0], SimpleNamespace(node_name='_NODE_NAME_UNKNOWN_', node_namespace='_NODE_NAMESPACE_UNKNOWN_')]
        for mode in ('converge', 'unknown', 'missing', 'duplicate', 'foreign', 'extra', 'unmatched',
                     'service_missing', 'read_crosses_deadline', 'finish', 'flapping',
                     'ready_with_finish', 'ready_with_bad_context', 'finish_during_read', 'context_during_read'):
            clock, position, captured, active, finish = [0.0], [0], [], [True], [False]
            probe = object.__new__(module.ParameterProbe)
            probe.node_name, probe.server_name = names[0], '/' + names[1]
            probe.deadline = 100.0 if mode == 'flapping' else 5.0
            probe.done = False
            probe.finish_requested = lambda: finish[0]
            active[0] = mode != 'ready_with_bad_context'
            finish[0] = mode == 'ready_with_finish'
            probe.clients = {name: SimpleNamespace(service_is_ready=lambda: mode != 'service_missing') for name in O.SERVICES}
            probe.subscription = SimpleNamespace(get_publisher_count=lambda: position[0] if mode == 'flapping' else 1 if mode == 'unmatched' else 2)

            def info(topic):
                self.assertEqual(topic, '/parameter_events')
                if mode == 'read_crosses_deadline':
                    clock[0] = probe.deadline
                if mode == 'finish_during_read': finish[0] = True
                if mode == 'context_during_read': active[0] = False
                if mode == 'converge':
                    return [unknown, [good[0]], [good[0]], good][min(position[0], 3)]
                return {'unknown': unknown, 'missing': [], 'duplicate': [good[0], good[0]],
                        'foreign': [good[0], SimpleNamespace(node_name='foreign', node_namespace='/')],
                        'extra': good + [good[0]], 'flapping': []}.get(mode, good)

            def tick():
                clock[0] += 1
                position[0] += 1
                return mode != 'finish'

            # The finish model must remain unready before its tick is observed.
            if mode == 'finish':
                probe.subscription.get_publisher_count = lambda: 0
            probe.node = SimpleNamespace(get_publishers_info_by_topic=info)
            probe.tick = tick
            probe.emit = lambda event, **fields: captured.append((event, fields))
            original_deadline = probe.deadline
            with self.subTest(mode=mode), patch.object(module.time, 'monotonic', lambda: clock[0]), \
                    patch.dict(sys.modules, {'rclpy': SimpleNamespace(ok=lambda: active[0])}):
                if mode == 'converge':
                    snapshot, observations = probe.wait_ready()
                    self.assertEqual(snapshot['matched_publishers'], 2)
                    self.assertEqual(observations, 3)
                    self.assertEqual(position[0], 3)
                else:
                    with self.assertRaises(RuntimeError):
                        probe.wait_ready()
                self.assertEqual(probe.deadline, original_deadline)
                self.assertTrue(all(event == 'DISCOVERY_OBSERVATION' for event, _ in captured))
                self.assertLessEqual(len(captured), 64)

    def test_discovery_oracle_requires_exact_final_snapshot(self):
        a, b = fixtures()
        initial = copy.deepcopy(a[0])
        initial['publishers'] = []
        a.insert(0, initial)
        O.one(a, 'CLIENT_READY')['discovery_observations'] = 2
        for seq, row in enumerate(a, 1):
            row['seq'] = seq
        self.assertEqual(O.business(a, b, SCENARIO)['discovery_observations'], 2)
        for mode in ('missing', 'duplicate_publisher', 'unknown_name', 'matched_bool', 'unmatched',
                     'service_missing', 'after_ready', 'wrong_fqns', 'wrong_count', 'duplicate_snapshot', 'wrong_side'):
            a, b = fixtures()
            row, ready = a[0], O.one(a, 'CLIENT_READY')
            if mode == 'missing': a.pop(0)
            elif mode == 'duplicate_publisher': row['publishers'][1] = row['publishers'][0]
            elif mode == 'unknown_name': row['publishers'][1]['name'] = '_NODE_NAME_UNKNOWN_'
            elif mode == 'matched_bool': row['matched_publishers'] = True
            elif mode == 'unmatched': row['matched_publishers'] = 1
            elif mode == 'service_missing': row['ready_services'].pop()
            elif mode == 'after_ready': row['seq'] = ready['seq'] + 1
            elif mode == 'wrong_fqns': ready['publisher_fqns'].pop()
            elif mode == 'wrong_count': ready['discovery_observations'] = 2
            elif mode == 'duplicate_snapshot':
                a.insert(0, copy.deepcopy(row))
                ready['discovery_observations'] = 2
            else: b.append(copy.deepcopy(row))
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                O.business(a, b, SCENARIO)

    def test_known_standard_startup_is_separate_and_exact(self):
        for name, default, mode in ((name, default, mode) for name, default in
                                   (('use_sim_time', False), ('start_type_description_service', True))
                                   for mode in ('valid', 'duplicate', 'wrong_value', 'mixed', 'foreign', 'unknown')):
            a, b = fixtures()
            event = dict(stamp=dict(sec=123, nanosec=456), node='/parameter_' + SCENARIO + '_B',
                         new_parameters=[{'name': name, 'value': O.value(1, default)}],
                         changed_parameters=[], deleted_parameters=[])
            if mode == 'wrong_value': event['new_parameters'][0]['value'] = O.value(1, not default)
            if mode == 'mixed': event['new_parameters'].append(O.state(SCENARIO, 0)[0])
            if mode == 'foreign': event['node'] += '_foreign'
            if mode == 'unknown': event['new_parameters'][0]['name'] = 'unknown_standard'
            row = dict(event='STANDARD_STARTUP_EVENT', seq=1000, **capture('ParameterEvent', event))
            a.append(row)
            if mode == 'duplicate': a.append(copy.deepcopy(row))
            with self.subTest(name=name, mode=mode):
                if mode == 'valid':
                    result = O.business(a, b, SCENARIO)
                    self.assertEqual(result['mutation_events'], 12)
                    self.assertEqual(result['standard_startup_events_observed'], 1)
                else:
                    with self.assertRaises(ValueError):
                        O.business(a, b, SCENARIO)

    def test_both_standard_startup_events_are_optional_and_order_independent(self):
        for names in (('use_sim_time', 'start_type_description_service'),
                      ('start_type_description_service', 'use_sim_time')):
            a, b = fixtures()
            for index, name in enumerate(names):
                event = dict(stamp=dict(sec=123, nanosec=456), node='/parameter_' + SCENARIO + '_B',
                             new_parameters=[{'name': name, 'value': O.value(1, name != 'use_sim_time')}],
                             changed_parameters=[], deleted_parameters=[])
                a.append(dict(event='STANDARD_STARTUP_EVENT', seq=1000 + index, **capture('ParameterEvent', event)))
            self.assertEqual(O.business(a, b, SCENARIO)['standard_startup_events_observed'], 2)

    def test_hardcoded_standard_names_request(self):
        # encapsulation + sequence length1 + string length2 + a/NUL.
        for raw in (bytes.fromhex('0001000001000000020000006100'), bytes.fromhex('0000000000000001000000026100')):
            reader = O.CdrReader(raw)
            self.assertEqual(reader.read('GetParameters.Request'), {'names': ['a']})
            self.assertEqual(reader.offset, len(reader.data))

    def test_exact_parameter_endpoint_declarations_required(self):
        lines = {}
        for side in 'AB':
            lines[side] = []
            for name in ('list_parameters', 'get_parameter_types', 'describe_parameters', 'get_parameters',
                         'set_parameters', 'set_parameters_atomically'):
                for direction, role, suffix in (('rq', 'writer' if side == 'A' else 'reader', 'Request'),
                                                 ('rr', 'reader' if side == 'A' else 'writer', 'Reply')):
                    lines[side].append('FDDST_ENDPOINT role=' + role + ' data_sharing=OFF topic=' + direction +
                                       '/parameter_' + SCENARIO + '_B/' + name + suffix)
            for direction, role, suffix in (('rq', 'reader', 'Request'), ('rr', 'writer', 'Reply')):
                lines[side].append('FDDST_ENDPOINT role=' + role + ' data_sharing=OFF topic=' + direction +
                                   '/parameter_' + SCENARIO + '_' + side + '/get_type_description' + suffix)
            for role in (('writer', 'reader') if side == 'A' else ('writer',)):
                lines[side].append('FDDST_ENDPOINT role=' + role + ' data_sharing=OFF topic=rt/parameter_events')
        texts = {k: '\n'.join(v) for k, v in lines.items()}
        self.assertEqual(len(O.parameter_endpoints(texts, SCENARIO)['declarations']['A']['services']), 12)
        for mode in ('missing_service', 'duplicate_service', 'foreign_scene', 'wrong_role', 'shared_memory',
                     'missing_event', 'extra_event_reader', 'action_only', 'unknown_type_field',
                     'missing_standard', 'duplicate_standard', 'wrong_standard_side', 'wrong_standard_role'):
            changed = copy.deepcopy(lines)
            if mode == 'missing_service': changed['B'].pop(0)
            elif mode == 'duplicate_service': changed['A'].append(changed['A'][0])
            elif mode == 'foreign_scene': changed['B'][0] = changed['B'][0].replace(SCENARIO, RUN)
            elif mode == 'wrong_role': changed['B'][0] = changed['B'][0].replace('role=reader', 'role=writer')
            elif mode == 'shared_memory': changed['A'][0] = changed['A'][0].replace('sharing=OFF', 'sharing=ON')
            elif mode == 'missing_event': changed['A'].pop()
            elif mode == 'extra_event_reader': changed['B'].append(changed['A'][-1])
            elif mode == 'action_only':
                changed = {k: ['FDDST_ENDPOINT role=writer data_sharing=OFF topic=rt/fibonacci/status'] for k in 'AB'}
            elif mode == 'missing_standard': changed['A'].pop(12)
            elif mode == 'duplicate_standard': changed['B'].append(changed['B'][12])
            elif mode == 'wrong_standard_side': changed['A'][12] = changed['A'][12].replace('_A/', '_B/')
            elif mode == 'wrong_standard_role': changed['B'][12] = changed['B'][12].replace('role=reader', 'role=writer')
            else: changed['A'][0] += ' type=unobserved'
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                O.parameter_endpoints({k: '\n'.join(v) for k, v in changed.items()}, SCENARIO)

    def test_cdr_boundaries_and_booleans(self):
        for raw in (b'', b'\0\7\0\0', bytes.fromhex('0001000001000000ffffffff'), bytes.fromhex('00010000e9030000')):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                O.CdrReader(raw).read('GetParameters.Request')
        with self.assertRaises(ValueError):
            O.CdrReader(bytes.fromhex('0001000002')).read('?')

    def test_missing_or_duplicate_event_rejected(self):
        for mode in ('missing', 'duplicate', 'foreign', 'old_deleted_value', 'wrong_kind'):
            a, b = fixtures()
            events = O.select(a, 'PARAMETER_EVENT')
            if mode == 'missing':
                a.remove(events[-1])
            elif mode == 'duplicate':
                a.append(copy.deepcopy(events[-1]))
            else:
                row = events[-1]
                value = copy.deepcopy(row['value'])
                if mode == 'foreign':
                    value['node'] += '_foreign'
                elif mode == 'old_deleted_value':
                    value['deleted_parameters'][0]['value'] = O.value(4, 'temporary')
                else:
                    value['changed_parameters'], value['deleted_parameters'] = value['deleted_parameters'], []
                row.update(capture('ParameterEvent', value))
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                O.business(a, b, SCENARIO)

    def test_partial_atomic_write_rejected(self):
        a, b = fixtures()
        row = O.select(a, 'RESPONSE')[10]
        value = copy.deepcopy(row['value'])
        value['values'][1] = O.value(2, 123)
        row.update(capture('GetParameters.Response', value))
        with self.assertRaises(ValueError):
            O.business(a, b, SCENARIO)

    def test_failed_response_and_cdr_json_mismatch(self):
        for mode in ('rpc', 'cdr', 'json_type', 'order', 'duplicate_rpc', 'mutated_request', 'server_commit'):
            a, b = fixtures()
            row = O.select(a, 'RESPONSE')[4]
            if mode == 'rpc':
                value = copy.deepcopy(row['value'])
                value['results'][0] = dict(successful=False, reason='failure')
                row.update(capture('SetParameters.Response', value))
            elif mode == 'cdr':
                row['cdr_sha256'] = '0' * 64
            elif mode == 'json_type':
                row['value']['results'][0]['successful'] = 1
            elif mode == 'order':
                row['seq'] = 0
            elif mode == 'duplicate_rpc':
                a.append(copy.deepcopy(row))
            elif mode == 'mutated_request':
                row = O.select(a, 'REQUEST')[4]
                value = copy.deepcopy(row['value'])
                value['parameters'][0]['value'] = O.value(1, True)
                row.update(capture('SetParameters.Request', value))
            else:
                O.select(b, 'SERVER_COMMIT')[0]['parameters'][0]['value'] = O.value(1, True)
            with self.subTest(mode=mode), self.assertRaises(ValueError):
                O.business(a, b, SCENARIO)

    def test_identity_duplicate_json_and_failure(self):
        base = dict(schema=1, scenario=SCENARIO, instance='A', run=RUN, side='A', pid=123,
                    seq=1, role='requester', monotonic_ns=1, event='NODE_READY')
        self.assertEqual(len(O.rows('R25_PROBE ' + json.dumps(base), SCENARIO, 'A', RUN, 123)), 1)
        for changed in (dict(base, pid=124), dict(base, seq=True), dict(base, event='FAIL'), dict(base, monotonic_ns=0)):
            with self.assertRaises(ValueError):
                O.rows('R25_PROBE ' + json.dumps(changed), SCENARIO, 'A', RUN, 123)
        with self.assertRaises(ValueError):
            O.rows('R25_PROBE ' + json.dumps(base)[:-1] + ',"seq":1}', SCENARIO, 'A', RUN, 123)

    def test_no_replacement_control_and_original_time_bounds(self):
        p = H.ParameterProcess.__new__(H.ParameterProcess)
        with self.assertRaises(ValueError):
            p.extra_control('ARM')
        self.assertEqual(H.LIMITS, {'business_seconds': 35, 'probe_seconds': 40, 'normal_close_release_seconds': 50,
                         'final_exit_seconds': 20, 'analysis_seconds': 60, 'query_construct_seconds': 300, 'query_dispatch_seconds': 240})

    def test_qualified_package_rejects_extra_artifact(self):
        with patch.object(H, 'current', return_value=(Path('parent'), {'a': 'hash'})), \
             patch.object(H.BASE, 'runtime_path', side_effect=lambda p, *args, **kw: Path(p)), \
             patch.object(H, 'read', return_value={'schema': 1, 'scope': H.SCOPE, 'malicious': True}):
            with self.assertRaises(ValueError):
                H.checked_package('fake/DERIVATION.json', {})

    def test_admission_rejects_cross_run_and_widened_limit(self):
        plan = dict(schema=1, scope=H.SCOPE, scenario=SCENARIO, runs={'A': RUN, 'B': RUN}, limits=dict(H.LIMITS),
                    derivation='', qualification='', package='', files={}, bindings={}, routes='', external='', identity='')
        for key in ('run', 'limit', 'schema', 'extra'):
            candidate = copy.deepcopy(plan)
            if key == 'run': candidate['runs']['B'] = '1' * 16
            elif key == 'limit': candidate['limits']['business_seconds'] = 36
            elif key == 'schema': candidate['schema'] = True
            else: candidate['extra'] = True
            with self.subTest(key=key), self.assertRaises(ValueError):
                H.validate_plan(candidate)

    def test_probe_plain_preserves_binary_and_typed_values(self):
        spec = importlib.util.spec_from_file_location('parameter_probe_host', H.PROBE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.assertEqual(module.plain([b'\x00', b'\xff']), [0, 255])
        self.assertIs(module.plain(True), True)
        self.assertEqual(module.values(1)[1], -1099511627777)

    def test_actual_workflow_rejects_expired_stage_before_reset(self):
        # Independent tester R33-T01: local receipt writes and final PID checks
        # can consume a deadline too; a fresh next-stage budget must not hide it.
        for point in ('NONE', 'BUSINESS', 'NORMAL_CLOSE_RELEASED', 'CONFIRM_ABSENT', 'FINAL_EXIT_CONFIRMED', 'ANALYSIS'):
            for delta in (-0.001, 0, 0.001):
                scene = H.ParameterScene.__new__(H.ParameterScene)
                clock = [100.0]
                records = dict(zip(('A', 'B'), fixtures()))
                scene.plan, scene.observed = {'scenario': SCENARIO}, datetime.now(timezone.utc)
                scene.deadline, scene.processes = None, {}
                for side in ('A', 'B'):
                    process = SimpleNamespace(side=side, run=RUN)
                    for name in ('deploy', 'launch', 'maps', 'publish', 'finish_fixture', 'close_release', 'confirm_absent'):
                        def operation(*args, _side=side, _name=name):
                            if point == 'CONFIRM_ABSENT' and _name == 'confirm_absent' and _side == 'B':
                                clock[0] = scene.deadline + delta
                        setattr(process, name, operation)
                    process.collect = lambda _side=side: {_side: _side}
                    scene.processes[side] = process
                scene.checkpoint = lambda *args: True
                scene.records = lambda text, key: records[key]

                def wait(keys, predicate, **kwargs):
                    texts = {key: key for key in keys}
                    self.assertTrue(predicate(texts, {key: True for key in keys}))
                    return texts

                def write(name, value):
                    if name == point + '.json':
                        clock[0] = scene.deadline + delta

                scene.wait, scene.write = wait, write
                scene.analyze = lambda texts: scene.write('ANALYSIS.json', {})
                with self.subTest(point=point, delta=delta), patch.object(H.time, 'monotonic', lambda: clock[0]):
                    if point != 'NONE' and delta >= 0:
                        with self.assertRaises(ValueError):
                            scene.workflow()
                    else:
                        scene.workflow()


if __name__ == '__main__':
    unittest.main()
