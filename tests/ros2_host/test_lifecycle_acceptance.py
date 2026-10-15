"""Synthetic receipt-validator controls; no ROS or DSoftBus execution."""
import copy
import contextlib
import io
import itertools
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import run_lifecycle_acceptance as runner
from run_lifecycle_acceptance import analyze_lifecycle, run_lifecycle_acceptance

RUN = '0123456789abcdef'
PIDS = {'A': 123, 'B': 456}


def model():
    """Hand-authored finite ROS contract, independent of parser constants."""
    records = {'A': [], 'B': []}
    ns = '/lifecycle_' + RUN
    def add(side, event, **fields):
        records[side].append(dict(event=event, **{k: str(v) for k, v in fields.items()}))
    for side in 'AB':
        add(side, 'READY', namespace=ns); add(side, 'MATCHED', nodes=2); add(side, 'START', nodes=2)
    def callback(node, name, count, previous, current, code):
        add('A', 'CALLBACK', node=node, callback=name, ordinal=count, previous=previous, current=current, **{'return': code})
    def snapshot(node, op, state):
        labels = {1:'unconfigured', 2:'inactive', 3:'active', 4:'finalized'}
        legal = {1:'1,5', 2:'2,3,6', 3:'4,7', 4:'-'}
        add('B', 'STATE', node=node, operation=op, service=ns + '/' + node + '/get_state', state=state, label=labels[state])
        add('B', 'AVAILABLE', node=node, operation=op, service=ns + '/' + node + '/get_available_transitions', ids=legal[state])
    ordinal = {'recover':0, 'fatal':0}
    def change(node, op, transition, response, state, pairs):
        add('B', 'REQUEST', node=node, operation=op, service=ns + '/' + node + '/change_state',
            transition=transition, label='unknown_transition' if transition == 0 else '-')
        for start, goal in pairs:
            ordinal[node] += 1
            add('B', 'TRANSITION', node=node, ordinal=ordinal[node], transition=0, start=start, goal=goal)
        add('B', 'RESPONSE', node=node, operation=op, success=response)
        snapshot(node, op, state)
    snapshot('recover', 0, 1)
    change('recover', 1, 0, 0, 1, [])
    callback('recover', 'configure', 1, 1, 10, 98)
    change('recover', 2, 1, 0, 1, [(1,10),(10,1)])
    callback('recover', 'configure', 2, 1, 10, 97)
    change('recover', 3, 1, 1, 2, [(1,10),(10,2)])
    callback('recover', 'activate', 1, 2, 13, 97)
    change('recover', 4, 3, 1, 3, [(2,13),(13,3)])
    callback('recover', 'deactivate', 1, 3, 14, 97)
    change('recover', 5, 4, 1, 2, [(3,14),(14,2)])
    callback('recover', 'cleanup', 1, 2, 11, 97)
    change('recover', 6, 2, 1, 1, [(2,11),(11,1)])
    callback('recover', 'configure', 3, 1, 10, 99)
    callback('recover', 'error', 1, 1, 15, 97)
    change('recover', 7, 1, 0, 1, [(1,10),(10,15),(15,1)])
    callback('recover', 'configure', 4, 1, 10, 97)
    change('recover', 8, 1, 1, 2, [(1,10),(10,2)])
    callback('recover', 'shutdown', 1, 2, 12, 97)
    change('recover', 9, 6, 1, 4, [(2,12),(12,4)])
    snapshot('fatal', 0, 1)
    callback('fatal', 'configure', 1, 1, 10, 99)
    callback('fatal', 'error', 1, 1, 15, 98)
    change('fatal', 1, 1, 0, 4, [(1,10),(10,15),(15,4)])
    for side in 'AB':
        if side == 'A': add(side, 'BUSINESS_DONE', callbacks=11, finalized=2, pending=0)
        else: add(side, 'BUSINESS_DONE', changes=10, states=12, events=20, pending=0)
        add(side, 'CLOSED', result=0)
    return records


def text(records):
    return {side: '\n'.join(f'LIFECYCLE_ACCEPT run={RUN} side={side} pid={PIDS[side]} local={i} ' +
                           ' '.join(k + '=' + v for k, v in row.items())
                           for i, row in enumerate(rows, 1)) for side, rows in records.items()}


class LifecycleReceiptTests(unittest.TestCase):
    def row(self, records, side, event, **fields):
        return next(r for r in records[side] if r['event'] == event and all(r.get(k) == str(v) for k, v in fields.items()))

    def test_valid_model(self):
        result = analyze_lifecycle(text(model()), RUN, PIDS)
        self.assertEqual((result['changes'], result['states'], result['transition_events'], result['callbacks']), (10,12,20,11))

    def test_response_can_arrive_before_events(self):
        records = model()
        response = self.row(records, 'B', 'RESPONSE', node='recover', operation=2)
        records['B'].remove(response)
        event = self.row(records, 'B', 'TRANSITION', node='recover', ordinal=1)
        records['B'].insert(records['B'].index(event), response)
        analyze_lifecycle(text(records), RUN, PIDS)

    def test_semantic_mutants(self):
        cases = [
            ('A','CALLBACK',{'node':'recover','callback':'configure','ordinal':1},'return','97'),
            ('A','CALLBACK',{'node':'recover','callback':'configure','ordinal':3},'return','98'),
            ('A','CALLBACK',{'node':'recover','callback':'error'},'return','98'),
            ('A','CALLBACK',{'node':'fatal','callback':'error'},'return','97'),
            ('A','CALLBACK',{'node':'recover','callback':'activate'},'previous','1'),
            ('A','CALLBACK',{'node':'recover','callback':'error'},'current','10'),
            ('A','CALLBACK',{'node':'recover','callback':'configure','ordinal':4},'ordinal','3'),
            ('B','STATE',{'node':'fatal','operation':1},'state','1'),
            ('B','STATE',{'node':'recover','operation':2},'state','2'),
            ('B','STATE',{'node':'recover','operation':7},'label','finalized'),
            ('B','AVAILABLE',{'node':'recover','operation':4},'ids','1,5'),
            ('B','REQUEST',{'node':'recover','operation':1},'label','configure'),
            ('B','REQUEST',{'node':'recover','operation':9},'transition','5'),
            ('B','REQUEST',{'node':'recover','operation':3},'service','/wrong/change_state'),
            ('B','RESPONSE',{'node':'recover','operation':7},'success','1'),
            ('B','RESPONSE',{'node':'fatal','operation':1},'success','1'),
            ('B','TRANSITION',{'node':'recover','ordinal':1},'transition','1'),
            ('B','TRANSITION',{'node':'fatal','ordinal':3},'goal','1'),
            ('B','TRANSITION',{'node':'recover','ordinal':12},'start','1'),
            ('B','CLOSED',{},'result','4'),
            ('A','BUSINESS_DONE',{},'pending','1'),
        ]
        for side, event, fields, key, value in cases:
            with self.subTest(side=side, event=event, fields=fields, key=key):
                records = model(); self.row(records, side, event, **fields)[key] = value
                with self.assertRaises(ValueError): analyze_lifecycle(text(records), RUN, PIDS)

    def test_missing_or_duplicate_evidence(self):
        for side, event in [('A','CALLBACK'),('A','START'),('A','CLOSED'),('B','REQUEST'),('B','RESPONSE'),
                            ('B','STATE'),('B','AVAILABLE'),('B','TRANSITION'),('B','BUSINESS_DONE')]:
            for duplicate in (False, True):
                with self.subTest(side=side,event=event,duplicate=duplicate):
                    records = model(); row = self.row(records, side, event)
                    if duplicate: records[side].insert(records[side].index(row), copy.deepcopy(row))
                    else: records[side].remove(row)
                    with self.assertRaises(ValueError): analyze_lifecycle(text(records), RUN, PIDS)

    def test_event_or_request_outside_interval(self):
        for event, fields in [('TRANSITION', {'node':'recover','ordinal':1}),
                              ('RESPONSE', {'node':'recover','operation':2}),
                              ('STATE', {'node':'recover','operation':2})]:
            with self.subTest(event=event):
                records = model(); row = self.row(records, 'B', event, **fields)
                records['B'].remove(row); records['B'].insert(3, row)
                with self.assertRaises(ValueError): analyze_lifecycle(text(records), RUN, PIDS)

    def test_wrong_identity_or_schema(self):
        original = text(model())
        for old, new in [('pid=456','pid=123'),('run='+RUN,'run=ffffffffffffffff'),('local=1 ','local=2 '),
                         ('event=READY','event=UNKNOWN'),('nodes=2','nodes=2 extra=1')]:
            with self.subTest(old=old):
                changed = dict(original); changed['B'] = changed['B'].replace(old,new,1)
                with self.assertRaises(ValueError): analyze_lifecycle(changed,RUN,PIDS)
        with self.assertRaises(ValueError): analyze_lifecycle(original,RUN,{'A':123,'B':123})

    def test_duplicate_field(self):
        texts = text(model()); texts['B'] = texts['B'].replace('event=READY', 'event=READY event=READY',1)
        with self.assertRaises(ValueError): analyze_lifecycle(texts,RUN,PIDS)

    def test_secure_environment_refused_before_process_creation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'run'
            argv = ['runner']
            for name in ('exe','bridge','fastdds-prefix','ros-prefix','pixi-prefix','host-prefix'):
                argv.extend(['--'+name, str(Path(directory)/'unused')])
            argv.extend(['--output',str(output)])
            with patch('sys.argv',argv), patch.dict(os.environ,{'ROS_SECURITY_ENABLE':'true'}), \
                    patch('run_lifecycle_acceptance.subprocess.Popen') as child, contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(run_lifecycle_acceptance(),1)
                child.assert_not_called()
            result = json.loads((output/'result.json').read_text(encoding='utf-8'))
            self.assertFalse(result['passed']); self.assertEqual(result['processes'],{})
            self.assertTrue(any('security environment conflict' in e for e in result['errors']))

    def test_current_module_barrier(self):
        # Exercise the real runner decision with synthetic processes, maps and
        # sockets. START is intercepted; no fake receipt can become a ROS PASS.
        cases = ('complete', 'complete_after_incomplete', 'alternating_incomplete', 'previously_seen_missing',
                 'peer_exit_zero', 'peer_exit_nonzero', 'wrong_path', 'wrong_hash', 'changed_seen_hash',
                 'mapping_exception', 'mapping_process_gone')
        for scenario, case in itertools.product(('lifecycle', 'managed-publisher', 'serialized'), cases + ('required_module_missing',)):
            with self.subTest(scenario=scenario, case=case), tempfile.TemporaryDirectory() as directory:
                scratch = Path(directory)
                host, ros, dds = (scratch / n for n in ('host', 'ros', 'dds'))
                exe, bridge = scratch / 'consumer.exe', host / 'bin/bridge.dll'
                files = [exe, bridge, dds / 'bin/fastrtps-2.14.dll', ros / 'bin/fastcdr-2.2.dll']
                files += [ros / 'bin' / n for n in (
                    'rclcpp.dll', 'rclcpp_lifecycle.dll', 'rcl_lifecycle.dll', 'rmw_fastrtps_cpp.dll',
                    'lifecycle_msgs__rosidl_typesupport_cpp.dll', 'lifecycle_msgs__rosidl_typesupport_fastrtps_cpp.dll')]
                if scenario == 'managed-publisher':
                    files += [ros / 'bin' / n for n in (
                        'std_msgs__rosidl_typesupport_cpp.dll', 'std_msgs__rosidl_typesupport_fastrtps_cpp.dll',
                        'std_srvs__rosidl_typesupport_cpp.dll', 'std_srvs__rosidl_typesupport_fastrtps_cpp.dll')]
                extra = [ros / 'bin' / n for n in (
                    'lifecycle_msgs__rosidl_typesupport_c.dll', 'lifecycle_msgs__rosidl_typesupport_fastrtps_c.dll')]
                if scenario == 'serialized':
                    files = files[:4] + [ros / 'bin' / n for n in (
                        'rclcpp.dll', 'rcl.dll', 'rmw_fastrtps_cpp.dll', 'rmw_fastrtps_shared_cpp.dll',
                        'std_msgs__rosidl_typesupport_cpp.dll', 'std_msgs__rosidl_typesupport_fastrtps_cpp.dll')]
                    extra = []
                for path in files + extra:
                    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'NOT_A_DLL_SYNTHETIC')
                package = host / 'lib/cmake/FastDDSDSoftBusTransportRos2Host/FastDDSDSoftBusTransportRos2HostConfig.cmake'
                package.parent.mkdir(parents=True)
                package.write_text('set(FastDDSDSoftBusTransportRos2Host_PROFILE "default-2145")', encoding='utf-8')
                processes, mapping_calls, starts = {}, {}, []
                clock = [0]

                class FakeSocket:
                    serial = 54000
                    def __init__(self):
                        FakeSocket.serial += 1; self.port = FakeSocket.serial
                    def setsockopt(self, *args): pass
                    def bind(self, *args): pass
                    def getsockname(self): return ('127.0.0.1', self.port)
                    def close(self): pass
                    def __enter__(self): return self
                    def __exit__(self, *args): pass

                class FakeProcess:
                    def __init__(self, command, env, stdout, stderr):
                        self.side = command[1]; self.pid = 9001 if self.side == 'A' else 9002; self.returncode = None
                        processes[self.side] = self
                        for local, event, fields in ((1, 'READY', 'namespace=/lifecycle_' + command[2]), (2, 'MATCHED', 'nodes=2')):
                            prefix = {'lifecycle':'LIFECYCLE_ACCEPT', 'managed-publisher':'PUBLISHER_ACCEPT',
                                      'serialized':'SERIALIZED_ACCEPT'}[scenario]
                            stdout.write(f'{prefix} run={command[2]} side={self.side} pid={self.pid} local={local} event={event} {fields}\n'.encode())
                        stdout.flush()
                    def poll(self): return self.returncode
                    def kill(self): self.returncode = -9
                    def wait(self, timeout=None): return self.returncode

                class MappingProcess:
                    def __init__(self, pid): self.pid = pid
                    def memory_maps(self, grouped=False):
                        count = mapping_calls.get(self.pid, 0) + 1; mapping_calls[self.pid] = count
                        paths = files + (extra if self.pid == 9001 else [])
                        if case == 'required_module_missing': paths = [p for p in paths if p != files[-1]]
                        if case == 'mapping_exception': raise runner.psutil.AccessDenied(self.pid)
                        if case == 'mapping_process_gone': raise runner.psutil.NoSuchProcess(self.pid)
                        if case == 'alternating_incomplete' or (case == 'complete_after_incomplete' and count < 3):
                            paths = paths[:-1] if count % 2 else paths[-1:]
                        if case in ('previously_seen_missing', 'changed_seen_hash'):
                            if self.pid == 9002 and count == 1: paths = paths[:-1]
                            if self.pid == 9001 and count > 1:
                                if case == 'previously_seen_missing': paths = []
                                else: exe.write_bytes(b'CHANGED_SYNTHETIC_BYTES')
                        if self.pid == 9002 and case in ('peer_exit_zero', 'peer_exit_nonzero'):
                            processes['A'].returncode = 0 if case == 'peer_exit_zero' else 4
                        if case == 'wrong_path': paths = [scratch / 'wrong' / paths[0].name] + paths[1:]
                        if case == 'wrong_hash': exe.write_bytes(b'WRONG_SYNTHETIC_BYTES')
                        return [SimpleNamespace(path=str(p)) for p in paths]

                def publish(path, value):
                    if path.name == 'business.start':
                        starts.append({'polls': {s: p.poll() for s, p in processes.items()}, 'maps': dict(mapping_calls)})
                        raise RuntimeError('SYNTHETIC_STOP_AT_START_ATTEMPT')

                def monotonic():
                    clock[0] += 10
                    return clock[0]

                argv = ['runner']
                if scenario != 'lifecycle': argv += ['--scenario', scenario]
                for key, path in (('exe', exe), ('bridge', bridge), ('fastdds-prefix', dds), ('ros-prefix', ros),
                                  ('pixi-prefix', scratch / 'pixi'), ('host-prefix', host), ('output', scratch / 'output')):
                    argv.extend(['--' + key, str(path)])
                environment = {k: v for k, v in os.environ.items() if not k.startswith('ROS_SECURITY_')}
                with patch('sys.argv', argv), patch.dict(os.environ, environment, clear=True), \
                        patch.object(runner.socket, 'socket', FakeSocket), patch.object(runner.subprocess, 'Popen', FakeProcess), \
                        patch.object(runner.psutil, 'Process', MappingProcess), patch.object(runner, 'publish_control', publish), \
                        patch.object(runner.time, 'monotonic', monotonic), patch.object(runner.time, 'sleep', lambda _: None), \
                        contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(run_lifecycle_acceptance(), 1)
                result = json.loads((scratch / 'output/result.json').read_text(encoding='utf-8'))
                self.assertFalse(result['passed'])  # All cases intentionally stop before business.
                if case in ('complete', 'complete_after_incomplete'):
                    self.assertEqual(len(starts), 1)
                    count = 1 if case == 'complete' else 3
                    self.assertEqual(starts[0], {'polls': {'A': None, 'B': None}, 'maps': {9001: count, 9002: count}})
                    barrier = result['module_barrier']
                    self.assertEqual(barrier['polls'], {'A': None, 'B': None})
                    self.assertEqual(barrier['pids'], {'A': 9001, 'B': 9002})
                    for side, paths in (('A', files + extra), ('B', files)):
                        self.assertEqual(barrier['modules'][side], {p.name.lower(): {
                            'path': str(p.resolve()), 'sha256': result['inputs_before'][str(p.resolve())]} for p in paths})
                else:
                    self.assertEqual(starts, [])
                    self.assertNotIn('module_barrier', result)
                    if case == 'wrong_path': self.assertTrue(any('actual module path' in e for e in result['errors']))
                    if case in ('wrong_hash', 'changed_seen_hash'):
                        self.assertTrue(any('actual module binding' in e for e in result['errors']))
                    if case == 'mapping_exception': self.assertTrue(any('AccessDenied' in e for e in result['errors']))

    def test_unknown_scenario_refused_before_process_creation(self):
        argv = ['runner', '--scenario', 'arbitrary-module']
        for name in ('exe','bridge','fastdds-prefix','ros-prefix','pixi-prefix','host-prefix','output'):
            argv += ['--' + name, 'unused']
        with patch('sys.argv', argv), patch.object(runner.subprocess, 'Popen') as child, \
                contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as stopped:
            run_lifecycle_acceptance()
        self.assertEqual(stopped.exception.code, 2)
        child.assert_not_called()


if __name__ == '__main__':
    unittest.main()
