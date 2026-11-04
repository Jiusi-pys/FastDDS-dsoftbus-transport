import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import service_restart_oracle as O
import run_service_restart as H

spec = importlib.util.spec_from_file_location('service_probe', H.PROBE)
P = importlib.util.module_from_spec(spec)
spec.loader.exec_module(P)
SCENE = '1234567890abcdef'
RUNS = {'A': 'aaaaaaaaaaaaaaaa', 'B0': 'bbbbbbbbbbbbbbbb', 'B1': 'cccccccccccccccc'}
SUFFIX = '.cpython-312-aarch64-linux-ohos.so'
MODULES = {'rclpy._rclpy_pybind11': 'python/rclpy/_rclpy_pybind11' + SUFFIX,
           'rclpy.client': 'python/rclpy/client.py', 'rclpy.service': 'python/rclpy/service.py',
           'example_interfaces.srv._add_two_ints': 'python/example_interfaces/srv/_add_two_ints.py',
           'example_interfaces.example_interfaces_s__rosidl_typesupport_c':
               'python/example_interfaces/example_interfaces_s__rosidl_typesupport_c' + SUFFIX}
FILES = {member: hashlib.sha256(member.encode()).hexdigest() for member in MODULES.values()}


def type_fixture(instance):
    fields = ['_TYPE_SUPPORT', '_CREATE_ROS_MESSAGE', '_CONVERT_FROM_PY', '_CONVERT_TO_PY', '_DESTROY_ROS_MESSAGE']
    a, b = O.operands(SCENE, 'PRE')
    cdr = []
    for label, values in (('Request', [a,b]), ('Response', [a+b])):
        raw=b'\0\1\0\0'+struct.pack('<'+'q'*len(values),*values)
        cdr.append(dict(type=label,values=values,bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest(),
                        base64=base64.b64encode(raw).decode()))
    return dict(interface='example_interfaces/srv/AddTwoInts',soabi='cpython-312-aarch64-linux-ohos',node_created=False,
        origins={name: {'path':'/data/local/tmp/mdds_dds_'+RUNS[instance]+'_'+instance[0]+'/package/'+member,
                        'member':member,'sha256':FILES[member]} for name,member in MODULES.items()},
        capsules={'Service':['_TYPE_SUPPORT'],'Request':fields,'Response':fields},cdr=cdr)


def fixtures(late=False):
    records = {k: [] for k in O.INSTANCES}
    for instance, out in records.items():
        def emit(event, **fields):
            out.append(dict(schema=1, scenario=SCENE, instance=instance, run=RUNS[instance], side=instance[0],
                pid=100 + O.INSTANCES.index(instance), seq=len(out) + 1, monotonic_ns=len(out) + 1,
                role='requester' if instance == 'A' else 'responder', event=event, **fields))
        def data(event, phase):
            a, b = O.operands(SCENE, phase)
            response = event.endswith('RESPONSE')
            raw = b'\0\1\0\0' + struct.pack('<q' if response else '<qq', *((a+b,) if response else (a, b)))
            emit(event, phase=phase, a=a, b=b, **({'sum': a+b} if response else {}),
                 cdr_base64=base64.b64encode(raw).decode(), cdr_sha256=hashlib.sha256(raw).hexdigest(), cdr_bytes=len(raw))
        emit('TYPE_SUPPORT_READY', **type_fixture(instance))
        emit('NODE_READY')
        emit('ENTITY_READY', name='/service_restart_' + SCENE, interface='example_interfaces/srv/AddTwoInts',
             entity='client' if instance == 'A' else 'service', depth=10, reliability='RELIABLE', durability='VOLATILE')
        if instance == 'A':
            emit('SERVICE_READY', phase='PRE', ready=True)
            data('CLIENT_REQUEST', 'PRE'); data('CLIENT_RESPONSE', 'PRE')
            emit('CONTROL_ACCEPTED', phase='OLD'); data('CLIENT_REQUEST', 'OLD')
            emit('OLD_PENDING', checkpoint='BEFORE_B0_STOP', done=False, cancelled=False)
            emit('SERVICE_UNAVAILABLE', ready=False, old_done=False)
            emit('CONTROL_ACCEPTED', phase='POST'); emit('SERVICE_READY', phase='NEW', ready=True)
            data('CLIENT_REQUEST', 'NEW')
            if late:
                data('CLIENT_RESPONSE', 'OLD')
            data('CLIENT_RESPONSE', 'NEW')
        elif instance == 'B0':
            data('SERVER_REQUEST', 'PRE'); data('SERVER_RESPONSE', 'PRE')
            emit('SERVER_PAUSED', after_callback_return=True, takes_further_requests=False)
        else:
            if late:
                data('SERVER_REQUEST', 'OLD'); data('SERVER_RESPONSE', 'OLD')
            data('SERVER_REQUEST', 'NEW'); data('SERVER_RESPONSE', 'NEW')
        emit('PHASE_COMPLETE', phase='PRE' if instance == 'B0' else 'NEW')
        emit('FINISH_ACCEPTED', reason='NORMAL_COMPLETION')
        if instance == 'A':
            emit('OLD_FINAL_STATE', done=late, cancelled=False, response_observed=late)
            if not late:
                emit('OLD_LOCAL_DISPOSE', cancelled=True, remote_cancel=False)
        emit('ENTITY_DESTROY_RETURN', entity='client' if instance == 'A' else 'service', returned=True)
        emit('NODE_DESTROY_RETURN', returned=True); emit('SHUTDOWN_RETURN', returned=True)
        emit('FINAL', success=True, node_created=True)
    return records


def parse(records, key):
    text = '\n'.join('R25_PROBE ' + json.dumps(row) for row in records)
    return O.rows(text, SCENE, key, RUNS[key], 100 + O.INSTANCES.index(key))


class OracleTests(unittest.TestCase):
    def test_type_qualification_requires_owned_manifest_capsules_cdr(self):
        mutations = [lambda r:r['origins']['rclpy.client'].update(path='/unowned/client.py'),
                     lambda r:r['origins']['rclpy.client'].update(sha256='0'*64),
                     lambda r:r['capsules'].pop('Service'),
                     lambda r:r['cdr'][0].update(values=[1,2]),
                     lambda r:r.update(node_created=True), lambda r:r.update(soabi='host')]
        for mutate in mutations:
            data=fixtures();mutate(data['A'][0])
            with self.assertRaises(ValueError):
                O.all_content(data,SCENE,FILES)
        with self.assertRaises(ValueError):
            O.all_content(fixtures(),SCENE,{})

    def test_service_endpoints_are_real_request_writer_reply_reader(self):
        request, reply='rq/service_restart_'+SCENE+'Request','rr/service_restart_'+SCENE+'Reply'
        lines=[]
        for point in ('BEFORE','PROCESS_AFTER'):
            for role,topic,guid in [('writer',request,'1'*32),('reader',reply,'2'*32)]:
                lines.append('NATIVE_ENDPOINT_ANCHOR run='+RUNS['A']+' side=A point='+point+' role='+role+
                             ' guid='+guid+' topic='+topic+' identity_source=SDK_SEND_SEDP')
        texts={'A':'\n'.join(lines),'B0':'','B1':''}
        for key in texts:
            for role,topic in ([('writer',request),('reader',reply)] if key=='A' else [('reader',request),('writer',reply)]):
                texts[key]+='\nFDDST_ENDPOINT role='+role+' data_sharing=OFF topic='+topic
        O.service_endpoints(texts,SCENE,RUNS['A'])
        for old,new in [(request,'rt/topic_echo'),('identity_source=SDK_SEND_SEDP','identity_source=HOST'),
                        ('point=PROCESS_AFTER','point=OTHER'),('role=writer','role=reader')]:
            bad=dict(texts);bad['A']=bad['A'].replace(old,new)
            with self.assertRaises(ValueError):
                O.service_endpoints(bad,SCENE,RUNS['A'])

    def test_real_service_generations_and_optional_late_old(self):
        for late in (False, True):
            data = fixtures(late)
            for key, value in data.items():
                parse(value, key)
            report = O.all_content(data, SCENE, FILES)
            self.assertTrue(report['new_request_recovery'])
            self.assertEqual(report['application_requests'], 3)
            self.assertEqual(report['instances']['B1']['served_old'], late)

    def test_old_response_cannot_substitute_for_new(self):
        data = fixtures(True)
        data['A'] = [r for r in data['A'] if not (r['event'] == 'CLIENT_RESPONSE' and r.get('phase') == 'NEW')]
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)
        with self.assertRaises(ValueError):
            O.pair(fixtures()['A'], fixtures()['B1'], SCENE, 'OLD')

    def test_disappearance_and_post_release_required(self):
        for event in ('SERVICE_UNAVAILABLE', 'OLD_PENDING'):
            data = fixtures(); data['A'] = [r for r in data['A'] if r['event'] != event]
            with self.subTest(event=event), self.assertRaises(ValueError):
                O.all_content(data, SCENE, FILES)
        data = fixtures(); O.one(data['A'], 'SERVICE_UNAVAILABLE')['ready'] = True
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)

    def test_duplicate_foreign_relabelled_or_boolean_identity(self):
        for key, value in [('run', RUNS['B0']), ('instance', 'B1'), ('pid', True), ('seq', True), ('monotonic_ns', False)]:
            data = fixtures()['A']; data[3][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                parse(data, 'A')
        data = fixtures(); data['A'].append(copy.deepcopy(data['A'][3]))
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)

    def test_cdr_corruption_and_old_new_alias(self):
        for field, value in [('cdr_sha256', '0'*64), ('cdr_bytes', True), ('a', 1), ('sum', 0), ('cdr_base64', 'AAAA')]:
            data = fixtures(); O.one(data['A'], 'CLIENT_RESPONSE', 'NEW')[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                O.all_content(data, SCENE, FILES)
        data = fixtures(True)
        old = O.one(data['A'], 'CLIENT_RESPONSE', 'OLD')
        new = O.one(data['A'], 'CLIENT_RESPONSE', 'NEW')
        for key in ('a', 'b', 'sum', 'cdr_base64', 'cdr_sha256', 'cdr_bytes'):
            new[key] = old[key]
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)

    def test_pending_disposal_not_remote_cancellation(self):
        data = fixtures(); O.one(data['A'], 'OLD_LOCAL_DISPOSE')['remote_cancel'] = True
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)
        data = fixtures(); O.one(data['A'], 'OLD_FINAL_STATE')['response_observed'] = True
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)

    def test_server_pause_and_normal_cleanup_required(self):
        for key, event, field in [('B0', 'SERVER_PAUSED', 'after_callback_return'),
                                  ('A', 'ENTITY_DESTROY_RETURN', 'returned'), ('B1', 'FINAL', 'success')]:
            data = fixtures(); O.one(data[key], event)[field] = False
            with self.subTest(event=event), self.assertRaises(ValueError):
                O.all_content(data, SCENE, FILES)
        data = fixtures(); O.one(data['B0'], 'SERVER_PAUSED')['takes_further_requests'] = True
        with self.assertRaises(ValueError):
            O.all_content(data, SCENE, FILES)


class ProbeTests(unittest.TestCase):
    def probe(self, key='A'):
        args = SimpleNamespace(run=RUNS[key], scenario=SCENE, instance=key, side=key[0],
                               role='requester' if key=='A' else 'responder')
        args.finish_control = '/data/local/tmp/mdds_dds_' + args.run + '_' + args.side + '/control.PROBE_FINISH'
        with patch.object(P.OrdinaryRuntimeProbe, '__init__', lambda obj, a: setattr(obj, 'args', a)):
            return P.ServiceRestartProbe(args)

    def test_distinct_bounded_request_operands(self):
        self.assertEqual(len({P.operands(SCENE, p) for p in ('PRE', 'OLD', 'NEW')}), 3)
        for phase in ('PRE', 'OLD', 'NEW'):
            self.assertEqual(P.operands(SCENE, phase), O.operands(SCENE, phase))
        with self.assertRaises(ValueError):
            P.operands(SCENE, 'POST')

    def test_b0_refuses_old_or_duplicate_request(self):
        p = self.probe('B0'); p.content = lambda *args: None
        a, b = P.operands(SCENE, 'PRE')
        response = SimpleNamespace(sum=0)
        self.assertEqual(p.respond(SimpleNamespace(a=a,b=b), response).sum, a+b)
        with self.assertRaises(RuntimeError):
            p.respond(SimpleNamespace(a=a,b=b), response)
        a, b = P.operands(SCENE, 'OLD')
        with self.assertRaises(RuntimeError):
            p.respond(SimpleNamespace(a=a,b=b), response)

    def test_new_not_issued_without_disappearance(self):
        p = self.probe(); p.futures = {'OLD': object()}; p.received = {'PRE'}
        p.responses = lambda: None; p.control = lambda phase: True
        p.client = SimpleNamespace(service_is_ready=lambda: True)
        with self.assertRaises(RuntimeError):
            p.client_step()


class RunnerTests(unittest.TestCase):
    def test_independent_limits_retain_single_restart_bound(self):
        self.assertEqual(H.LIMITS, {'business_seconds':35,'probe_seconds':40,'normal_close_release_seconds':50,
            'final_exit_seconds':20,'analysis_seconds':60,'query_construct_seconds':300,
            'query_dispatch_seconds':240,'replacements':1,'application_requests':3})

    def test_service_argv_and_config_do_not_enable_two_cycles(self):
        p = H.ServiceProcess.__new__(H.ServiceProcess)
        p.prefix={'A':'/data/local/tmp/mdds_dds_'+RUNS['A']+'_A'}
        p.roles={'A':'requester'}; p.run=RUNS['A'];p.scenario=SCENE;p.instance='A'
        self.assertIn('/package/scripts/service_restart_probe.py', p.argv('A')[2])
        fields=p.configuration_fields('A', {'backend':'OHOS_PUBLIC_SOCKET_BOUNDED_REACCEPT_EXPERIMENTAL'})
        self.assertNotIn('process_restart_cycles',fields)
        self.assertEqual(fields['service_prefix'],'com.kaihong.mdds.r29.'+SCENE)

    def test_no_post_or_old_control_on_server(self):
        p = H.ServiceProcess.__new__(H.ServiceProcess);p.instance='B0'
        with self.assertRaises(ValueError):
            p.extra_control('OLD')

    def test_evidence_write_deadline_is_checked_after_write(self):
        s=H.ServiceScene.__new__(H.ServiceScene);s.deadline=35;s.write=lambda *args: None
        with patch.object(H.time,'monotonic',side_effect=[34,34.5,35.01]), self.assertRaises(ValueError):
            s.stamp('BUSINESS')


if __name__ == '__main__':
    unittest.main()
