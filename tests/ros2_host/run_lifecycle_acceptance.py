"""One bounded real ROS lifecycle pair on the published Windows default Host seam."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import socket
import subprocess
import time
import psutil
from control_files import publish_control
from host_io_capture import validate_capture
from host_connection_capture import match_connections
from host_receive_capture import validate_receive_capture


def analyze_lifecycle(texts, run, pids):
    """Validate independent server callbacks, remote services and ordered ROS events."""
    schemas = {
        'READY': {'namespace'}, 'MATCHED': {'nodes'}, 'START': {'nodes'},
        'CALLBACK': {'node', 'callback', 'ordinal', 'previous', 'current', 'return'},
        'REQUEST': {'node', 'operation', 'service', 'transition', 'label'},
        'RESPONSE': {'node', 'operation', 'success'},
        'STATE': {'node', 'operation', 'service', 'state', 'label'},
        'AVAILABLE': {'node', 'operation', 'service', 'ids'},
        'TRANSITION': {'node', 'ordinal', 'transition', 'start', 'goal'},
        'BUSINESS_DONE': set(),  # Side-specific fields below: no invented peer counters.
        'CLOSED': {'result'},
    }
    common = {'READY', 'MATCHED', 'START', 'BUSINESS_DONE', 'CLOSED'}
    allowed = {'A': common | {'CALLBACK'},
               'B': common | {'REQUEST', 'RESPONSE', 'STATE', 'AVAILABLE', 'TRANSITION'}}
    if set(texts) != {'A', 'B'} or set(pids) != {'A', 'B'} or pids['A'] == pids['B']:
        raise ValueError('two process identities')
    if not re.fullmatch('[0-9a-f]{16}', run) or any(type(p) is not int or p <= 0 for p in pids.values()):
        raise ValueError('run/PID format')
    rows = {}
    for side in 'AB':
        rows[side] = []
        for line in texts[side].splitlines():
            if not line.startswith('LIFECYCLE_ACCEPT'):
                continue
            pairs = [token.split('=', 1) for token in line.split()[1:]]
            if not line.startswith('LIFECYCLE_ACCEPT ') or any(len(pair) != 2 for pair in pairs):
                raise ValueError('record syntax')
            row = dict(pairs)
            event = row.get('event')
            fields = ({'callbacks', 'finalized', 'pending'} if side == 'A' else {'changes', 'states', 'events', 'pending'}) if event == 'BUSINESS_DONE' else schemas.get(event, set())
            if (len(row) != len(pairs) or event not in allowed[side] or
                    set(row) != fields | {'run', 'side', 'pid', 'local', 'event'} or
                    (row['run'], row['side'], row['pid'], row['local']) !=
                    (run, side, str(pids[side]), str(len(rows[side]) + 1))):
                raise ValueError('record identity/schema/sequence')
            rows[side].append(row)

    def select(side, event):
        return [r for r in rows[side] if r['event'] == event]

    def body(row):
        return {k: v for k, v in row.items() if k not in {'run', 'side', 'pid', 'local', 'event'}}

    def require_equal(actual, expected, reason):
        if actual != expected:
            raise ValueError(reason)

    ns = '/lifecycle_' + run
    for side in 'AB':
        require_equal([r['event'] for r in rows[side][:3]], ['READY', 'MATCHED', 'START'], 'startup barrier')
        require_equal([r['event'] for r in rows[side][-2:]], ['BUSINESS_DONE', 'CLOSED'], 'drain closure')
        for event, expected in (
                ('READY', {'namespace': ns}), ('MATCHED', {'nodes': '2'}), ('START', {'nodes': '2'}),
                ('BUSINESS_DONE', {'callbacks': '11', 'finalized': '2', 'pending': '0'} if side == 'A' else
                 {'changes': '10', 'states': '12', 'events': '20', 'pending': '0'}),
                ('CLOSED', {'result': '0'})):
            require_equal([body(r) for r in select(side, event)], [expected], event)

    # Numeric constants are the installed lifecycle_msgs contract; the oracle is
    # deliberately independent of whatever the child claims in its DONE record.
    callbacks = [
        ('recover', 'configure', 1, 1, 10, 98), ('recover', 'configure', 2, 1, 10, 97),
        ('recover', 'activate', 1, 2, 13, 97), ('recover', 'deactivate', 1, 3, 14, 97),
        ('recover', 'cleanup', 1, 2, 11, 97), ('recover', 'configure', 3, 1, 10, 99),
        ('recover', 'error', 1, 1, 15, 97), ('recover', 'configure', 4, 1, 10, 97),
        ('recover', 'shutdown', 1, 2, 12, 97), ('fatal', 'configure', 1, 1, 10, 99),
        ('fatal', 'error', 1, 1, 15, 98),
    ]
    require_equal([body(r) for r in select('A', 'CALLBACK')], [dict(zip(
        ('node', 'callback', 'ordinal', 'previous', 'current', 'return'), map(str, c))) for c in callbacks], 'callback script')
    # rcl 9.2.9 events populate only states: raw transition.id must remain 0.
    # Requested transition identity is independently recorded by ChangeState.
    # (node, operation, transition, response, resulting stable state, events)
    script = [
        ('recover', 1, 0, 0, 1, []),
        ('recover', 2, 1, 0, 1, [(0,1,10),(0,10,1)]),
        ('recover', 3, 1, 1, 2, [(0,1,10),(0,10,2)]),
        ('recover', 4, 3, 1, 3, [(0,2,13),(0,13,3)]),
        ('recover', 5, 4, 1, 2, [(0,3,14),(0,14,2)]),
        ('recover', 6, 2, 1, 1, [(0,2,11),(0,11,1)]),
        ('recover', 7, 1, 0, 1, [(0,1,10),(0,10,15),(0,15,1)]),
        ('recover', 8, 1, 1, 2, [(0,1,10),(0,10,2)]),
        ('recover', 9, 6, 1, 4, [(0,2,12),(0,12,4)]),
        ('fatal', 1, 1, 0, 4, [(0,1,10),(0,10,15),(0,15,4)]),
    ]
    requests = select('B', 'REQUEST')
    responses = select('B', 'RESPONSE')
    require_equal(len(requests), 10, 'request count')
    require_equal(len(responses), 10, 'response count')
    states = select('B', 'STATE')
    available = select('B', 'AVAILABLE')
    snapshots = [('recover', 0, 1)] + [(n, op, st) for n, op, _, _, st, _ in script[:9]] + [('fatal', 0, 1), ('fatal', 1, 4)]
    require_equal(len(states), 12, 'state count')
    require_equal(len(available), 12, 'available count')
    labels = {1: 'unconfigured', 2: 'inactive', 3: 'active', 4: 'finalized'}
    legal = {1: '1,5', 2: '2,3,6', 3: '4,7', 4: '-'}
    for state_row, avail_row, (node, operation, state) in zip(states, available, snapshots):
        base = ns + '/' + node
        require_equal(body(state_row), dict(node=node, operation=str(operation), service=base + '/get_state',
                      state=str(state), label=labels[state]), 'remote state')
        require_equal(body(avail_row), dict(node=node, operation=str(operation), service=base + '/get_available_transitions',
                      ids=legal[state]), 'available transitions')
        if int(state_row['local']) >= int(avail_row['local']):
            raise ValueError('snapshot order')
    events = select('B', 'TRANSITION')
    cursor, ordinal = 0, {'recover': 0, 'fatal': 0}
    prior = available[0]
    for i, ((node, operation, transition, success, state, triples), request, response) in enumerate(zip(script, requests, responses)):
        if i == 9:
            if int(available[9]['local']) >= int(states[10]['local']):
                raise ValueError('fatal initial snapshot order')
            prior = available[10]
        require_equal(body(request), dict(node=node, operation=str(operation), service=ns + '/' + node + '/change_state',
                      transition=str(transition), label='unknown_transition' if transition == 0 else '-'), 'service request')
        require_equal(body(response), dict(node=node, operation=str(operation), success=str(success)), 'service response')
        after_index = i + 1 if i < 9 else 11
        if not int(prior['local']) < int(request['local']) < int(response['local']) < int(states[after_index]['local']):
            raise ValueError('service/snapshot order')
        for triple in triples:
            if cursor >= len(events):
                raise ValueError('missing transition')
            event = events[cursor]; cursor += 1; ordinal[node] += 1
            require_equal(body(event), dict(node=node, ordinal=str(ordinal[node]), transition=str(triple[0]),
                          start=str(triple[1]), goal=str(triple[2])), 'transition script')
            if not int(request['local']) < int(event['local']) < int(states[after_index]['local']):
                raise ValueError('transition request interval')
        prior = available[after_index]
    require_equal(cursor, len(events), 'extra transition')
    require_equal(ordinal, {'recover': 17, 'fatal': 3}, 'event cardinality')
    return {'changes': 10, 'states': 12, 'available_snapshots': 12, 'transition_events': 20,
            'callbacks': 11, 'finalized_nodes': 2, 'unknown_label_rejected': True,
            'callback_failure': True, 'error_recovery_success': True, 'error_recovery_failure': True}


def run_lifecycle_acceptance():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'bridge', 'fastdds-prefix', 'ros-prefix', 'pixi-prefix', 'host-prefix', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--scenario', choices=('lifecycle', 'managed-publisher', 'serialized',
                                             'fragment-reliable', 'fragment-best-effort'), default='lifecycle')
    args = parser.parse_args()
    analyzer, prefix = analyze_lifecycle, 'LIFECYCLE_ACCEPT'
    if args.scenario == 'managed-publisher':
        from lifecycle_publisher_capture import analyze_publisher
        analyzer, prefix = analyze_publisher, 'PUBLISHER_ACCEPT'
    elif args.scenario == 'serialized':
        from serialized_capture import analyze_serialized
        analyzer, prefix = analyze_serialized, 'SERIALIZED_ACCEPT'
    elif args.scenario.startswith('fragment-'):
        from fragment_capture import analyze_fragment
        fragment_reliability = args.scenario[len('fragment-'):].replace('-', '_')
        analyzer = lambda texts, run, pids: analyze_fragment(texts, run, pids, reliability=fragment_reliability)
        prefix = 'FRAGMENT_ACCEPT'
    args.output.mkdir(parents=True, exist_ok=False)
    result = {'passed': False, 'device_tested': False, 'native_tested': False,
              'profile': 'default-2145', 'scenario': args.scenario, 'processes': {}, 'errors': []}
    processes, streams, reservations = {}, [], []
    started = released = runtime_released = timed_out = False
    forced_kill = []
    observed = {s: {} for s in 'AB'}
    before = None
    inputs = []
    try:
        # This scope uses the existing non-security package. Never silently
        # rewrite an inherited secure configuration into a different policy.
        conflict = sorted(k for k, v in os.environ.items() if k.startswith('ROS_SECURITY_') and v)
        if conflict:
            raise ValueError('security environment conflict: ' + ','.join(conflict))
        package = args.host_prefix / 'lib/cmake/FastDDSDSoftBusTransportRos2Host/FastDDSDSoftBusTransportRos2HostConfig.cmake'
        if 'set(FastDDSDSoftBusTransportRos2Host_PROFILE "default-2145")' not in package.read_text(encoding='utf-8'):
            raise ValueError('published default Host package required')
        if args.bridge.resolve().parent != (args.host_prefix / 'bin').resolve():
            raise ValueError('bridge must belong to declared installed Host prefix')
        run = secrets.token_hex(8)
        result['run'] = run
        log_dir = args.output / 'ros-logs'; log_dir.mkdir()
        dlls = [args.exe, args.bridge, args.fastdds_prefix / 'bin/fastrtps-2.14.dll', args.ros_prefix / 'bin/fastcdr-2.2.dll']
        if args.scenario == 'serialized' or args.scenario.startswith('fragment-'):
            dlls.extend(args.ros_prefix / 'bin' / name for name in (
                'rclcpp.dll', 'rcl.dll', 'rmw_fastrtps_cpp.dll', 'rmw_fastrtps_shared_cpp.dll',
                'std_msgs__rosidl_typesupport_cpp.dll', 'std_msgs__rosidl_typesupport_fastrtps_cpp.dll'))
            server_extra = []
        else:
            dlls.extend(args.ros_prefix / 'bin' / name for name in (
                'rclcpp.dll', 'rclcpp_lifecycle.dll', 'rcl_lifecycle.dll', 'rmw_fastrtps_cpp.dll',
                'lifecycle_msgs__rosidl_typesupport_cpp.dll', 'lifecycle_msgs__rosidl_typesupport_fastrtps_cpp.dll'))
            if args.scenario == 'managed-publisher':
                dlls.extend(args.ros_prefix / 'bin' / name for name in (
                    'std_msgs__rosidl_typesupport_cpp.dll', 'std_msgs__rosidl_typesupport_fastrtps_cpp.dll',
                    'std_srvs__rosidl_typesupport_cpp.dll', 'std_srvs__rosidl_typesupport_fastrtps_cpp.dll'))
            server_extra = [args.ros_prefix / 'bin' / name for name in (
                'lifecycle_msgs__rosidl_typesupport_c.dll', 'lifecycle_msgs__rosidl_typesupport_fastrtps_c.dll')]
        expected = {'A': {p.name.lower(): p.resolve() for p in dlls + server_extra},
                    'B': {p.name.lower(): p.resolve() for p in dlls}}
        inputs = dlls + server_extra + [package]
        inputs.extend(Path(__file__).with_name(n) for n in (
            'run_lifecycle_acceptance.py', 'control_files.py', 'host_io_capture.py', 'host_connection_capture.py', 'host_receive_capture.py'))
        if args.scenario == 'managed-publisher':
            inputs.append(Path(__file__).with_name('lifecycle_publisher_capture.py'))
        elif args.scenario == 'serialized':
            inputs.append(Path(__file__).with_name('serialized_capture.py'))
        elif args.scenario.startswith('fragment-'):
            inputs.append(Path(__file__).with_name('fragment_capture.py'))
        before = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        result['inputs_before'] = before
        result['host_prefix'] = str(args.host_prefix.resolve())
        for _ in 'AB':
            sock = socket.socket(); reservations.append(sock)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            sock.bind(('127.0.0.1', 0))
        ports = [s.getsockname()[1] for s in reservations]
        result['ports'] = ports
        for sock in reservations: sock.close()
        reservations.clear()
        result['commands'], result['environment_delta'] = {}, {}
        for index, side in enumerate('AB'):
            config = args.output / (side + '.conf')
            config.write_text('\n'.join([
                'bridge_library=' + str(args.bridge.resolve()),
                'fastdds_module=' + str(dlls[2].resolve()), 'fastcdr_module=' + str(dlls[3].resolve()),
                'locator_version=V1', 'native_name_contract=PACKAGE_SCOPED_V1', 'authorized_package=host.ros.test',
                'service_prefix=host.ros.test.v1', 'backend=HOST_OPAQUE_BYTES_TEST_ONLY', 'run=' + run,
                'side=' + str(index), 'port=' + str(ports[index]), 'peer_port=' + str(ports[1-index]),
                'domain=201', 'forward=1', 'host_close_error=0', 'host_io_observation=1', 'host_receive_observation=1',
                'runtime_close_release=' + str((args.output / (side + '.runtime')).resolve())]) + '\n', encoding='utf-8')
            env = os.environ.copy()
            removed = []
            for key in list(env):
                if key.startswith(('FASTDDS_', 'FDDST_', 'FASTRTPS_')) or key in ('ROS_LOCALHOST_ONLY', 'RMW_FASTRTPS_USE_QOS_FROM_XML'):
                    removed.append(key); env.pop(key)
            delta = dict(PATH=os.pathsep.join([str(args.fastdds_prefix / 'bin'), str(args.ros_prefix / 'bin'),
                         str(args.pixi_prefix / 'Library/bin'), env.get('PATH', '')]),
                         RMW_IMPLEMENTATION='rmw_fastrtps_cpp', ROS_DOMAIN_ID='201', ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET',
                         ROS_LOG_DIR=str(log_dir.resolve()), RCUTILS_CONSOLE_OUTPUT_FORMAT='[{severity}] [{name}]: {message}',
                         RCUTILS_COLORIZED_OUTPUT='0', RCUTILS_LOGGING_USE_STDOUT='0',
                         FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(config.resolve()), FASTDDS_ENVIRONMENT_FILE='', SKIP_DEFAULT_XML='1',
                         FDDST_LIFECYCLE_START=str((args.output / 'business.start').resolve()),
                         FDDST_LIFECYCLE_RELEASE=str((args.output / 'business.release').resolve()))
            env.update(delta)
            result['environment_delta'][side] = {'set': delta, 'removed_keys': removed}
            out = (args.output / (side + '.stdout')).open('wb'); err = (args.output / (side + '.stderr')).open('wb')
            streams.extend([out, err])
            command = [str(args.exe.resolve()), side, run]; result['commands'][side] = command
            if args.scenario.startswith('fragment-'):
                command.append(fragment_reliability)
            processes[side] = subprocess.Popen(command, env=env, stdout=out, stderr=err)
        deadline = time.monotonic() + 65
        while any(p.poll() is None for p in processes.values()):
            current = {s: {} for s in 'AB'}
            for side, process in processes.items():
                if process.poll() is not None: continue
                try:
                    for mapping in psutil.Process(process.pid).memory_maps(grouped=False):
                        path = Path(mapping.path).resolve(); name = path.name.lower()
                        if name in expected[side]:
                            if path != expected[side][name]:
                                raise ValueError('actual module path: ' + name)
                            if started and name in observed[side]:
                                continue  # Preserve post-START history collection cost.
                            digest = hashlib.sha256(path.read_bytes()).hexdigest()
                            if path != expected[side][name] or digest != before[str(expected[side][name])]:
                                raise ValueError('actual module binding: ' + name)
                            current[side][name] = {'path': str(path), 'sha256': digest}
                            observed[side][name] = current[side][name]  # History, not the START predicate.
                except psutil.NoSuchProcess:
                    pass
            texts = {s: (args.output / (s + '.stdout')).read_text(encoding='utf-8', errors='replace') for s in 'AB'}
            failed = any('ROS_FAILURE ' in texts[s] or processes[s].poll() not in (None, 0) for s in 'AB')
            def marker(side, event):
                return re.search('^' + prefix + ' run=' + run + ' side=' + side + ' pid=' + str(processes[side].pid) +
                                 r' local=[1-9][0-9]* event=' + event + r' ', texts[side], re.M) is not None
            if not started and not failed and all(marker(s, 'MATCHED') and set(current[s]) == set(expected[s]) for s in 'AB'):
                # Recheck both children after this iteration's complete maps.
                # This is an observation, not atomic liveness/tamper protection.
                start_polls = {s: p.poll() for s, p in processes.items()}
                if all(value is None for value in start_polls.values()):
                    result['module_barrier'] = {'pids': {s: p.pid for s, p in processes.items()},
                                                'polls': start_polls, 'modules': current}
                    publish_control(args.output / 'business.start', 'START ' + run + '\n'); started = True
            if not released and (failed or all(marker(s, 'BUSINESS_DONE') for s in 'AB')):
                publish_control(args.output / 'business.release', 'RELEASE ' + run + '\n'); released = True
            if not runtime_released and (failed or all(re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run=' + run +
                    ' side=' + s + ' pid=' + str(processes[s].pid) + ' ', texts[s], re.M) for s in 'AB')):
                for side in 'AB':
                    publish_control(args.output / (side + '.runtime'),
                        f'ROS_RUNTIME_CLOSE_RELEASE schema=1 run={run} side={side} pid={processes[side].pid} stage=RUNTIME_CLOSE reason=' +
                        ('WINDOW_FAILED' if failed else 'NORMAL_COMPLETION') + '\n')
                runtime_released = True
            if time.monotonic() > deadline: timed_out = True; break
            time.sleep(.01)
    except Exception as exc:
        result['errors'].append(type(exc).__name__ + ': ' + str(exc))
    finally:
        for sock in reservations: sock.close()
        for side, process in processes.items():
            if process.poll() is None:
                forced_kill.append({'side': side, 'pid': process.pid}); process.kill()
            process.wait(timeout=5)
        for stream in streams: stream.close()
    result.update(processes={s: {'pid': p.pid, 'exit': p.returncode} for s, p in processes.items()},
                  actual_loaded_modules=observed, started=started, released=released,
                  runtime_released=runtime_released, timed_out=timed_out, forced_kill=forced_kill)
    if len(processes) != 2 or any(p.returncode != 0 for p in processes.values()) or timed_out or forced_kill:
        result['errors'].append('process_failed_or_timed_out')
    try:
        result['inputs_after'] = {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
        if before != result['inputs_after'] or not started or not released or not runtime_released:
            raise ValueError('input binding or release')
        texts = {s: (args.output / (s + '.stdout')).read_text(encoding='utf-8') for s in 'AB'}
        pids = {s: p.pid for s, p in processes.items()}
        result['business'] = analyzer(texts, run, pids)
        for index, side in enumerate('AB'):
            stderr = (args.output / (side + '.stderr')).read_text(encoding='utf-8')
            combined = texts[side] + '\n' + stderr
            if 'ROS_FAILURE ' in combined or re.search(r'failed to send response[^\r\n]*timeout', combined, re.I):
                raise ValueError('application/response failure')
            capture = validate_capture(texts[side], run, side, pids[side]); result[side + '_capture'] = capture
            if not capture['complete'] or f'FDDST_ROS_PRE_PARTICIPANT run={run} side={index} builtin=0 intraprocess=0 mode=HOST_OPAQUE_BYTES_TEST_ONLY' not in stderr:
                raise ValueError('transport path/capture')
            io = re.findall(r'FDDST_HOST_IO run=' + run + ' side=' + str(index) +
                            r' sends=([0-9]+) ingress=([0-9]+) send_errors=([0-9]+) fake_sdk=1 ([^\r\n]*)', stderr)
            if len(io) != 1 or int(io[0][0]) <= 0 or int(io[0][1]) <= 0 or int(io[0][2]) != 0:
                raise ValueError('Host traffic/errors')
            result[side + '_host_io'] = {'sends': int(io[0][0]), 'ingress': int(io[0][1]), 'send_errors': int(io[0][2])}
            if f'FDDST_ROS_CLOSED run={run} side={index} pump_joined=1 runtime_closed=1 local_only=1' not in stderr:
                raise ValueError('runtime cleanup')
            if 'FDDST_HOST_RX_CAPTURE_INCOMPLETE' in combined:
                raise ValueError('receive export incomplete')
        result['connections'] = match_connections(texts, run, pids)
        result['receive_capture'] = {s: validate_receive_capture(texts[s], run, s, pids[s], profile='full_host',
            endpoints=[p[s] for p in result['connections']], expected_ingress=result[s + '_host_io']['ingress']) for s in 'AB'}
    except Exception as exc:
        result['errors'].append(type(exc).__name__ + ': ' + str(exc))
    result['exclusive_rebind'] = []
    for port in result.get('ports', []):
        try:
            with socket.socket() as sock:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1); sock.bind(('127.0.0.1', port))
            result['exclusive_rebind'].append(True)
        except OSError as exc:
            result['exclusive_rebind'].append(False); result['errors'].append('port_rebind: ' + str(exc))
    result['passed'] = not result['errors']
    (args.output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2)); return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(run_lifecycle_acceptance())
