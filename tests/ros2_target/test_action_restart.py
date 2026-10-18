"""Host-only oracle/control tests; these fixtures are not DSoftBus execution."""
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import action_restart_oracle as O
import run_action_restart as H

spec = importlib.util.spec_from_file_location('action_restart_probe', H.PROBE)
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
SCENE = '1234567890abcdef'
RUNS = {'A': 'aaaaaaaaaaaaaaaa', 'B0': 'bbbbbbbbbbbbbbbb', 'B1': 'cccccccccccccccc'}
FILES = {member: hashlib.sha256(member.encode()).hexdigest() for member in O.MODULES.values()}


def payload(event, phase, index=1):
    uuid, order = O.identity(SCENE, phase)
    values = [order] if event.endswith('_GOAL') else O.sequence(order)
    if event.endswith('_FEEDBACK'):
        values = values[:index + 2]
    if event.endswith('_RESULT') and phase == 'OLD':
        values = []
    raw = b'\0\1\0\0' + (b'' if event.endswith('_GOAL') else struct.pack('<I', len(values)))
    raw += struct.pack('<' + 'i' * len(values), *values)
    fields = dict(phase=phase, uuid=uuid, order=order, values=values, cdr_base64=base64.b64encode(raw).decode(),
                  cdr_sha256=hashlib.sha256(raw).hexdigest(), cdr_bytes=len(raw))
    if event.endswith('_FEEDBACK'):
        fields['index'] = index
    if event.endswith('_RESULT'):
        fields['status'] = 0 if phase == 'OLD' else 4
    if event == 'SERVER_GOAL':
        fields['status'] = 1 if phase == 'OLD' else 2
    return fields


def type_fixture(instance):
    cdr = []
    for label in ('Goal', 'Feedback', 'Result'):
        p = payload('CLIENT_' + label.upper(), 'PRE')
        cdr.append(dict(type=label, values=p['values'], bytes=p['cdr_bytes'], sha256=p['cdr_sha256'], base64=p['cdr_base64']))
    return dict(interface='example_interfaces/action/Fibonacci', soabi='cpython-312-aarch64-linux-ohos', node_created=False,
                origins={name: {'path': '/data/local/tmp/mdds_dds_' + RUNS[instance] + '_' + instance[0] + '/package/' + member,
                                'member': member, 'sha256': FILES[member]} for name, member in O.MODULES.items()},
                capsules=copy.deepcopy(O.CAPSULES), cdr=cdr)


def fixtures(unknown=False):
    records = {k: [] for k in O.INSTANCES}
    for key, out in records.items():
        def emit(event, **fields):
            out.append(dict(schema=1, scenario=SCENE, instance=key, run=RUNS[key], side=key[0],
                            pid=100 + O.INSTANCES.index(key), seq=len(out) + 1, monotonic_ns=len(out) + 1,
                            role='requester' if key == 'A' else 'responder', event=event, **fields))
        def business(phase):
            prefix = 'CLIENT' if key == 'A' else 'SERVER'
            emit(prefix + '_GOAL', **payload(prefix + '_GOAL', phase))
            if key == 'B0' and phase == 'OLD':
                emit('CONTROL_ACCEPTED', phase='START_OLD')
                emit('OLD_EXECUTING', phase='OLD', uuid=O.identity(SCENE, phase)[0], status=2)
            if key == 'A':
                uuid, order = O.identity(SCENE, phase)
                emit('CLIENT_ACCEPTED', phase=phase, uuid=uuid, order=order, accepted=True)
                emit('CLIENT_RESULT_REQUEST', phase=phase, uuid=uuid)
                emit('CLIENT_STATUS', phase=phase, uuid=uuid, status=2)
            count = 1 if phase == 'OLD' else O.identity(SCENE, phase)[1] - 1
            for index in range(1, count + 1):
                emit(prefix + '_FEEDBACK', **payload(prefix + '_FEEDBACK', phase, index))
            if key == 'A':
                emit('CLIENT_FEEDBACK_COMPLETE', phase=phase, count=count)
            if phase != 'OLD':
                if key != 'A':
                    emit('CONTROL_ACCEPTED', phase='COMPLETE_' + phase)
                emit(prefix + '_RESULT', **payload(prefix + '_RESULT', phase))
                if key == 'A':
                    emit('CLIENT_STATUS', phase=phase, uuid=O.identity(SCENE, phase)[0], status=4)
        emit('TYPE_SUPPORT_READY', **type_fixture(key))
        emit('NODE_READY')
        emit('ENTITY_READY', name='/action_restart_' + SCENE, interface='example_interfaces/action/Fibonacci',
             entity='client' if key == 'A' else 'server', qos='SELECTED_PUBLIC_DEFAULTS')
        if key == 'A':
            emit('ACTION_READY', phase='PRE', ready=True)
            business('PRE')
            emit('CONTROL_ACCEPTED', phase='OLD')
            business('OLD')
            emit('OLD_PENDING', checkpoint='BEFORE_B0_STOP', accepted=True, feedback_count=1, done=False, cancelled=False)
            emit('ACTION_UNAVAILABLE', ready=False, old_done=False)
            if unknown:
                emit('CLIENT_RESULT', **payload('CLIENT_RESULT', 'OLD'))
            emit('CONTROL_ACCEPTED', phase='POST')
            emit('ACTION_READY', phase='NEW', ready=True)
            business('NEW')
        elif key == 'B0':
            business('PRE')
            business('OLD')
            emit('OLD_HELD', status=2, result_set=False, callback_active=False)
        else:
            business('NEW')
        emit('PHASE_COMPLETE', phase='OLD_HELD' if key == 'B0' else 'NEW')
        emit('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
        if key == 'A':
            emit('OLD_FINAL_STATE', done=unknown, cancelled=False, response_observed=unknown)
            if not unknown:
                emit('OLD_LOCAL_DISPOSE', cancelled=True, remote_cancel=False)
        elif key == 'B0':
            emit('OLD_SERVER_ABANDON', status=2, result_set=False, remote_cancel=False)
        emit('ENTITY_DESTROY_RETURN', entity='client' if key == 'A' else 'server', returned=True)
        emit('NODE_DESTROY_RETURN', returned=True)
        emit('SHUTDOWN_RETURN', returned=True)
        emit('FINAL', success=True, node_created=True)
    return records


def parse(records, key):
    return O.rows('\n'.join('R25_PROBE ' + json.dumps(r) for r in records), SCENE, key, RUNS[key], 100 + O.INSTANCES.index(key))


class OracleTests(unittest.TestCase):
    def test_pending_and_unknown_old_never_count_as_new_recovery(self):
        for unknown in (False, True):
            records = fixtures(unknown)
            parsed = {k: parse(v, k) for k, v in records.items()}
            result = O.all_content(parsed, SCENE, FILES)
            self.assertTrue(result['new_goal_recovery'])
            self.assertEqual(result['application_goals'], 3)
            self.assertEqual(result['new']['feedback_count'], 7)
            self.assertEqual(result['instances']['A']['old_outcome'], 'STATUS_UNKNOWN_EMPTY_RESULT' if unknown else 'PENDING_LOCALLY_CANCELLED')

    def test_uuid_feedback_result_and_status_corruption_rejected(self):
        changes = [('CLIENT_GOAL', 'NEW', 'uuid', O.identity(SCENE, 'OLD')[0]),
                   ('CLIENT_ACCEPTED', 'OLD', 'accepted', False), ('CLIENT_FEEDBACK', 'NEW', 'index', 2),
                   ('CLIENT_FEEDBACK', 'PRE', 'values', [0, 1, 9]), ('CLIENT_RESULT', 'NEW', 'status', 5),
                   ('CLIENT_RESULT', 'NEW', 'cdr_base64', ''), ('CLIENT_RESULT', 'PRE', 'cdr_bytes', True),
                   ('OLD_PENDING', None, 'done', True), ('OLD_LOCAL_DISPOSE', None, 'remote_cancel', True)]
        for event, phase, field, value in changes:
            records = fixtures()
            O.select(records['A'], event, phase)[0][field] = value
            with self.subTest(event=event, field=field), self.assertRaises(ValueError):
                O.all_content(records, SCENE, FILES)
        records = fixtures(True)
        O.one(records['A'], 'CLIENT_RESULT', 'OLD')['status'] = 4
        with self.assertRaises(ValueError):
            O.all_content(records, SCENE, FILES)

    def test_missing_duplicate_and_wrong_generation_events_rejected(self):
        for event, phase in [('CLIENT_FEEDBACK', 'NEW'), ('CLIENT_ACCEPTED', 'OLD'), ('CLIENT_RESULT', 'NEW'),
                             ('CLIENT_STATUS', 'NEW'), ('ACTION_UNAVAILABLE', None)]:
            for duplicate in (False, True):
                records = fixtures()
                found = O.select(records['A'], event, phase)
                if duplicate:
                    records['A'].append(copy.deepcopy(found[0]))
                else:
                    records['A'] = [r for r in records['A'] if r not in found]
                with self.subTest(event=event, duplicate=duplicate), self.assertRaises(ValueError):
                    # Parse protects sequence/cardinality in addition to the semantic oracle.
                    parsed = {k: parse(v, k) for k, v in records.items()}
                    O.all_content(parsed, SCENE, FILES)

    def test_selected_abi_origins_capsules_native_cdr_required(self):
        mutations = [lambda r: r['origins']['rclpy.action.client'].update(path='/wrong/client.py'),
                     lambda r: r['origins']['rclpy.action.server'].update(sha256='0' * 64),
                     lambda r: r['capsules'].pop('Action'), lambda r: r['cdr'][2].update(values=[0]),
                     lambda r: r.update(node_created=True), lambda r: r.update(soabi='host')]
        for mutate in mutations:
            records = fixtures()
            mutate(records['A'][0])
            with self.assertRaises(ValueError):
                O.all_content(records, SCENE, FILES)

    def test_completion_requires_barrier_and_normal_destroy(self):
        for key, event, field, value in [('B0', 'CONTROL_ACCEPTED', 'phase', 'COMPLETE_NEW'),
                ('B0', 'OLD_HELD', 'callback_active', True), ('B0', 'OLD_SERVER_ABANDON', 'status', 5),
                ('B1', 'ENTITY_DESTROY_RETURN', 'returned', False), ('A', 'FINISH_ACCEPTED', 'reason', 'WINDOW_FAILED')]:
            records = fixtures()
            O.one(records[key], event, 'COMPLETE_PRE' if event == 'CONTROL_ACCEPTED' else None)[field] = value
            with self.assertRaises(ValueError):
                O.all_content(records, SCENE, FILES)

    def test_all_eight_real_endpoints_required_and_persistent(self):
        name = 'action_restart_' + SCENE + '/_action/'
        pairs = [('writer', 'rq/' + name + n + 'Request') for n in ('send_goal', 'get_result', 'cancel_goal')]
        pairs += [('reader', 'rr/' + name + n + 'Reply') for n in ('send_goal', 'get_result', 'cancel_goal')]
        pairs += [('reader', 'rt/' + name + n) for n in ('feedback', 'status')]
        texts = {}
        for key in O.INSTANCES:
            lines = []
            for i, (role, topic) in enumerate(pairs):
                if key == 'A':
                    for point in ('BEFORE', 'PROCESS_AFTER'):
                        lines.append('NATIVE_ENDPOINT_ANCHOR run=' + RUNS['A'] + ' side=A point=' + point +
                                     ' role=' + role + ' topic=' + topic + ' identity_source=SDK_SEND_SEDP guid=' + format(i + 1, '032x'))
                else:
                    role = 'reader' if role == 'writer' else 'writer'
                lines.append('FDDST_ENDPOINT role=' + role + ' topic=' + topic + ' data_sharing=OFF')
            texts[key] = '\n'.join(lines)
        self.assertEqual(len(O.action_endpoints(texts, SCENE, RUNS['A'])), 8)
        broken = dict(texts, A=texts['A'].replace('topic=rt/' + name + 'status', 'topic=rt/wrong'))
        with self.assertRaises(ValueError):
            O.action_endpoints(broken, SCENE, RUNS['A'])
        broken = dict(texts, B1=texts['B1'].replace('data_sharing=OFF', 'data_sharing=ON', 1))
        with self.assertRaises(ValueError):
            O.action_endpoints(broken, SCENE, RUNS['A'])


class ProbeTests(unittest.TestCase):
    def probe(self, key='A'):
        args = SimpleNamespace(run=RUNS[key], scenario=SCENE, instance=key, side=key[0],
                               role='requester' if key == 'A' else 'responder')
        with patch.object(P.OrdinaryRuntimeProbe, '__init__', lambda obj, a: setattr(obj, 'args', a)):
            probe = P.ActionRestartProbe(args)
        probe.emit = lambda *args, **kwargs: None
        probe.content = lambda *args, **kwargs: None
        return probe

    def test_distinct_nonzero_uuid_and_fibonacci_generations(self):
        self.assertEqual(len({P.identity(SCENE, p)[0] for p in ('PRE', 'OLD', 'NEW')}), 3)
        for phase in ('PRE', 'OLD', 'NEW'):
            uuid, order = P.identity(SCENE, phase)
            self.assertNotEqual(uuid, bytes(16))
            self.assertEqual((uuid.hex(), order), O.identity(SCENE, phase))
            self.assertEqual(P.sequence(order), O.sequence(order))

    def test_server_rejects_foreign_duplicate_or_wrong_generation(self):
        for key, phase in [('B0', 'NEW'), ('B1', 'OLD')]:
            probe = self.probe(key)
            uuid, order = P.identity(SCENE, phase)
            handle = SimpleNamespace(goal_id=SimpleNamespace(uuid=uuid), request=SimpleNamespace(order=order))
            with self.assertRaises(RuntimeError):
                probe.accepted(handle)
        probe = self.probe('B0')
        uuid, order = P.identity(SCENE, 'OLD')
        handle = SimpleNamespace(goal_id=SimpleNamespace(uuid=uuid), request=SimpleNamespace(order=order), status=2, executing=lambda: None)
        probe.accepted(handle)
        with self.assertRaises(RuntimeError):
            probe.accepted(handle)

    def test_old_execution_waits_for_permission_and_starts_once(self):
        probe = self.probe('B0')
        uuid, order = P.identity(SCENE, 'OLD')
        calls = []
        handle = SimpleNamespace(goal_id=SimpleNamespace(uuid=uuid), request=SimpleNamespace(order=order), status=1,
                                 publish_feedback=lambda msg: calls.append('feedback'))
        def execute():
            handle.status = 2
            calls.append('executing')
        handle.executing = execute
        probe.accepted(handle)
        self.assertEqual(calls, [])
        probe.control = lambda phase: False
        fibonacci = SimpleNamespace(Feedback=lambda **kw: SimpleNamespace(**kw))
        with patch.dict(sys.modules, {'example_interfaces.action': SimpleNamespace(Fibonacci=fibonacci)}):
            probe.server_step()
            self.assertEqual(calls, [])
            probe.control = lambda phase: phase == 'START_OLD'
            probe.server_step()
            probe.server_step()
        self.assertEqual(calls, ['executing', 'feedback'])

    def test_new_requires_observed_disappearance(self):
        probe = self.probe()
        probe.goals = {'OLD': object()}
        probe.results = {'OLD': SimpleNamespace(done=lambda: False)}
        probe.feedback = {'OLD': 1}
        probe.old_pending = True
        probe.responses = lambda: None
        probe.control = lambda phase: True
        probe.client = SimpleNamespace(server_is_ready=lambda: True)
        with self.assertRaises(RuntimeError):
            probe.client_step()

    def test_foreign_duplicate_feedback_rejected(self):
        probe = self.probe()
        probe.goals = {'NEW': object()}
        probe.feedback = {'NEW': 0}
        uuid, order = P.identity(SCENE, 'NEW')
        msg = SimpleNamespace(goal_id=SimpleNamespace(uuid=uuid), feedback=SimpleNamespace(sequence=[0, 1, 1]))
        probe.on_feedback('NEW', msg)
        with self.assertRaises(RuntimeError):
            probe.on_feedback('NEW', msg)
        msg.goal_id.uuid = bytes(16)
        with self.assertRaises(RuntimeError):
            probe.on_feedback('NEW', msg)


class RunnerTests(unittest.TestCase):
    def test_peer_log_capture_must_catch_up_before_feedback_oracle(self):
        scene = H.ActionScene.__new__(H.ActionScene)
        scene.plan = {'scenario': SCENE}
        scene.records = lambda rows, key: rows
        for phase, server in (('PRE', 'B0'), ('NEW', 'B1')):
            records = fixtures()
            self.assertTrue(scene.feedback_ready(records, server, phase))
            missing = O.select(records[server], 'SERVER_FEEDBACK', phase)[-1]
            records[server].remove(missing)
            self.assertFalse(scene.feedback_ready(records, server, phase))
            records[server].append(missing)
            self.assertTrue(scene.feedback_ready(records, server, phase))
            # Cardinality/content validation remains strict after readiness.
            records[server].append(copy.deepcopy(missing))
            self.assertTrue(scene.feedback_ready(records, server, phase))
            with self.assertRaises(ValueError):
                O.feedback_pair(records['A'], records[server], SCENE, phase)

    def test_peer_result_log_must_catch_up_before_pre_oracle(self):
        scene = H.ActionScene.__new__(H.ActionScene)
        scene.records = lambda rows, key: rows
        records = fixtures()
        self.assertTrue(scene.pre_result_ready(records))
        result = O.one(records['B0'], 'SERVER_RESULT', 'PRE')
        records['B0'].remove(result)
        self.assertFalse(scene.pre_result_ready(records))
        records['B0'].append(result)
        self.assertTrue(scene.pre_result_ready(records))

    def test_independent_deadlines_and_goal_count(self):
        self.assertEqual(H.LIMITS, {'business_seconds': 35, 'probe_seconds': 40, 'normal_close_release_seconds': 50,
            'final_exit_seconds': 20, 'analysis_seconds': 60, 'query_construct_seconds': 300,
            'query_dispatch_seconds': 240, 'replacements': 1, 'application_goals': 3})

    def test_no_extra_replacement_and_action_argv(self):
        process = H.ActionProcess.__new__(H.ActionProcess)
        process.prefix = {'A': '/data/local/tmp/mdds_dds_' + RUNS['A'] + '_A'}
        process.roles = {'A': 'requester'}
        process.run, process.scenario, process.instance = RUNS['A'], SCENE, 'A'
        self.assertIn('/package/scripts/action_restart_probe.py', process.argv('A')[2])
        fields = process.configuration_fields('A', {'backend': 'OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL'})
        self.assertNotIn('process_restart_cycles', fields)
        self.assertEqual(fields['service_prefix'], 'com.kaihong.mdds.r30.' + SCENE)

    def test_controls_cannot_cross_process_generations(self):
        for instance, name in [('A', 'COMPLETE_PRE'), ('B0', 'COMPLETE_NEW'), ('B1', 'OLD'), ('B1', 'POST')]:
            process = H.ActionProcess.__new__(H.ActionProcess)
            process.instance = instance
            with self.assertRaises(ValueError):
                process.extra_control(name)

    def test_evidence_write_cannot_cross_deadline(self):
        scene = H.ActionScene.__new__(H.ActionScene)
        scene.deadline, scene.write = 35, lambda *args: None
        with patch.object(H.time, 'monotonic', side_effect=[34, 34.5, 35.01]), self.assertRaises(ValueError):
            scene.stamp('BUSINESS')


if __name__ == '__main__':
    unittest.main()
