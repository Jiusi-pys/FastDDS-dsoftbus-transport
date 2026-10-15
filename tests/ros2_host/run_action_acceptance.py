"""One bounded real ROS action pair over the explicitly configured Windows Host seam."""
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


def analyze(texts, run, pids):
    """Join ordinary action receipts; local ordering never compares peer clocks."""
    schemas = {
        'SERVER_READY': {'action'}, 'CLIENT_READY': {'action'},
        'GOAL': {'order', 'uuid', 'accepted'}, 'SENT': {'order'},
        'ACCEPTED': {'order', 'uuid'}, 'RESPONSE': {'order', 'uuid', 'accepted'},
        'FEEDBACK': {'order', 'uuid', 'values'},
        'CONCURRENT': {'normal', 'target', 'normal_active', 'target_active'},
        'CANCEL': {'order', 'uuid', 'normal', 'normal_active', 'accepted'},
        'CANCEL_REQUEST': {'uuid', 'normal', 'feedback_seen'},
        'CANCEL_RESPONSE': {'uuid', 'return_code', 'count'},
        'TERMINAL': {'order', 'uuid', 'code', 'values', 'prestate'},
        'RESULT': {'order', 'uuid', 'code', 'values'},
        'READY_REQUEST': {'phase', 'service', 'primary', 'secondary', 'held'},
        'READY_ACCEPT': {'phase', 'service', 'primary', 'secondary', 'executing'},
        'READY_REPLY': {'phase', 'service', 'primary', 'secondary', 'success', 'receipt'},
        'BUSINESS_DONE': {'requested', 'accepted', 'rejected', 'terminals', 'pending'},
        'CLOSED': {'result'},
    }
    allowed = {'A': {'SERVER_READY', 'GOAL', 'ACCEPTED', 'FEEDBACK', 'CONCURRENT', 'CANCEL', 'TERMINAL', 'READY_ACCEPT', 'BUSINESS_DONE', 'CLOSED'},
               'B': {'CLIENT_READY', 'SENT', 'RESPONSE', 'FEEDBACK', 'CANCEL_REQUEST', 'CANCEL_RESPONSE', 'RESULT', 'READY_REQUEST', 'READY_REPLY', 'BUSINESS_DONE', 'CLOSED'}}
    rows = {}
    for side in 'AB':
        rows[side] = []
        for line in texts[side].splitlines():
            if not line.startswith('ACTION_ACCEPT '):
                continue
            pairs = [token.split('=', 1) for token in line.split()[1:]]
            if any(len(pair) != 2 for pair in pairs):
                raise ValueError('record syntax')
            row = dict(pairs)
            event = row.get('event')
            if (len(row) != len(pairs) or event not in allowed[side] or
                    set(row) != schemas[event] | {'run', 'side', 'pid', 'local', 'event'} or
                    (row['run'], row['side'], row['pid'], row['local']) !=
                    (run, side, str(pids[side]), str(len(rows[side]) + 1))):
                raise ValueError('record identity/schema/sequence')
            rows[side].append(row)
    def select(side, event, **fields):
        return [r for r in rows[side] if r['event'] == event and all(r.get(k) == str(v) for k, v in fields.items())]
    def one(side, event, **fields):
        found = select(side, event, **fields)
        if len(found) != 1:
            raise ValueError('event cardinality/fields: ' + event)
        return found[0]
    def before(side, earlier, later):
        if rows[side].index(earlier) >= rows[side].index(later):
            raise ValueError('local causal order')
    def valid_uuid(value):
        if not re.fullmatch('[0-9a-f]{32}', value) or value == '0' * 32:
            raise ValueError('UUID format')
    action = '/action_' + run + '/fibonacci'
    for side, ready in (('A', 'SERVER_READY'), ('B', 'CLIENT_READY')):
        one(side, ready, action=action)
        one(side, 'BUSINESS_DONE', requested=5, accepted=4, rejected=1, terminals=4, pending=0)
        one(side, 'CLOSED', result=0)
        if rows[side][0]['event'] != ready or [r['event'] for r in rows[side][-2:]] != ['BUSINESS_DONE', 'CLOSED']:
            raise ValueError('ready/drain lifecycle')
    orders = ['6', '40', '-1', '0', '5']
    for side, event in (('A', 'GOAL'), ('B', 'SENT'), ('B', 'RESPONSE')):
        if [r['order'] for r in select(side, event)] != orders:
            raise ValueError('goal request/response order/count')
    if [r['order'] for r in select('A', 'ACCEPTED')] != ['6', '40', '0', '5']:
        raise ValueError('accepted count')
    ids = {}
    for order in orders:
        goal = one('A', 'GOAL', order=order, accepted=int(order != '-1'))
        valid_uuid(goal['uuid'])
        ids[order] = goal['uuid']
        response = one('B', 'RESPONSE', order=order, accepted=int(order != '-1'), uuid=ids[order] if order != '-1' else '-')
        before('B', one('B', 'SENT', order=order), response)
        if order != '-1':
            before('A', goal, one('A', 'ACCEPTED', order=order, uuid=ids[order]))
    if len(set(ids.values())) != 5:
        raise ValueError('duplicate goal UUID')
    # Additional application acceptance precondition; the original Action checks
    # below still apply. A service reply never substitutes for Action feedback.
    for side, event in (('A', 'READY_ACCEPT'), ('B', 'READY_REQUEST'), ('B', 'READY_REPLY')):
        if [r['phase'] for r in select(side, event)] != ['PAIR', 'TAIL']:
            raise ValueError('readiness phase/count')
    for phase, orders in (('PAIR', ('6', '40')), ('TAIL', ('5',))):
        primary = ids[orders[0]]
        secondary = ids['40'] if phase == 'PAIR' else '-'
        service = '/action_' + run + '/ready/' + phase.lower() + '/u' + primary
        receipt = 'ready_v1:' + run + ':' + phase + ':' + primary
        if phase == 'PAIR':
            service += '/u' + secondary
            receipt += ':' + secondary
        fields = dict(phase=phase, service=service, primary=primary, secondary=secondary)
        request = one('B', 'READY_REQUEST', **fields, held=len(orders))
        grant = one('A', 'READY_ACCEPT', **fields, executing=len(orders))
        reply = one('B', 'READY_REPLY', **fields, success=1, receipt=receipt)
        before('B', request, reply)
        for order in orders:
            before('B', one('B', 'RESPONSE', order=order), request)
            before('A', one('A', 'ACCEPTED', order=order), grant)
            for feedback in select('A', 'FEEDBACK', order=order):
                before('A', grant, feedback)
            for feedback in select('B', 'FEEDBACK', order=order):
                before('B', request, feedback)
        # Feedback is allowed while B spins awaiting the service response.
        # There is intentionally no READY_REPLY-before-FEEDBACK constraint.
        if phase == 'PAIR':
            before('B', reply, one('B', 'CANCEL_REQUEST'))
        else:
            before('A', one('A', 'TERMINAL', order=0), grant)
            before('B', one('B', 'RESULT', order=0), request)
            before('B', reply, one('B', 'RESULT', order=5))
    expected = {'6': ('4', '0,1,1,2,3,5'), '40': ('5', '0,1'), '0': ('6', '-'), '5': ('4', '0,1,1,2,3')}
    for side, event in (('A', 'TERMINAL'), ('B', 'RESULT')):
        if [r['order'] for r in select(side, event)] != ['40', '6', '0', '5']:
            raise ValueError('terminal count/order')
        for order, (code, values) in expected.items():
            extra = {'prestate': 'CANCELING' if order == '40' else 'EXECUTING'} if side == 'A' else {}
            final = one(side, event, order=order, uuid=ids[order], code=code, values=values, **extra)
            before(side, one(side, 'ACCEPTED' if side == 'A' else 'RESPONSE', order=order), final)
    # Validate feedback against server emissions. A valid observed subsequence
    # does not assert all feedback delivery, status-topic delivery, or replay.
    observed = {}
    for side in 'AB':
        observed[side] = {}
        for row in select(side, 'FEEDBACK'):
            order = row['order']
            if order not in ('6', '40', '5') or row['uuid'] != ids[order]:
                raise ValueError('feedback goal ownership')
            full = expected[order][1].split(',')
            values = row['values'].split(',')
            previous = observed[side].setdefault(order, [])
            if (len(values) < 2 or values != full[:len(values)] or len(values) > len(full) or
                    (previous and len(values) <= len(previous[-1].split(',')))):
                raise ValueError('feedback prefix/order')
            previous.append(row['values'])
            if side == 'A':
                before(side, one(side, 'ACCEPTED', order=order), row)
                before(side, row, one(side, 'TERMINAL', order=order))
            else:
                # Preserve the original minimum ROS receipt order. The added
                # application readiness precondition is checked separately above.
                before(side, one(side, 'SENT', order=order), row)
                if row['values'] not in [r['values'] for r in select('A', 'FEEDBACK', order=order)]:
                    raise ValueError('feedback not emitted')
        if observed[side].get('40') != ['0,1']:
            raise ValueError('mandatory cancel feedback')
    for order in ('6', '40', '5'):
        full = expected[order][1].split(',')
        if observed['A'].get(order) != [','.join(full[:n]) for n in range(2, len(full) + 1)]:
            raise ValueError('server feedback emissions')
    concurrent = one('A', 'CONCURRENT', normal=ids['6'], target=ids['40'], normal_active=1, target_active=1)
    cancel = one('A', 'CANCEL', order=40, uuid=ids['40'], normal=ids['6'], normal_active=1, accepted=1)
    before('A', one('A', 'ACCEPTED', order=6), concurrent)
    before('A', one('A', 'ACCEPTED', order=40), concurrent)
    before('A', concurrent, one('A', 'FEEDBACK', order=40))
    before('A', one('A', 'FEEDBACK', order=40), cancel)
    before('A', cancel, one('A', 'TERMINAL', order=40))
    before('A', one('A', 'TERMINAL', order=40), one('A', 'TERMINAL', order=6))
    request = one('B', 'CANCEL_REQUEST', uuid=ids['40'], normal=ids['6'], feedback_seen=1)
    response = one('B', 'CANCEL_RESPONSE', uuid=ids['40'], return_code=0, count=1)
    before('B', one('B', 'RESPONSE', order=40), request)
    before('B', one('B', 'FEEDBACK', order=40), request)
    before('B', request, response)
    before('B', response, one('B', 'RESULT', order=40))
    before('B', one('B', 'RESULT', order=6), one('B', 'SENT', order=-1))
    before('B', one('B', 'RESPONSE', order=-1), one('B', 'SENT', order=0))
    before('B', one('B', 'RESULT', order=0), one('B', 'SENT', order=5))
    # one(...fields) is not sufficient by itself: reject surplus mismatched
    # milestones as well as duplicate valid milestones.
    counts = {'A': {'SERVER_READY': 1, 'GOAL': 5, 'ACCEPTED': 4, 'CONCURRENT': 1,
                    'CANCEL': 1, 'TERMINAL': 4, 'READY_ACCEPT': 2, 'BUSINESS_DONE': 1, 'CLOSED': 1},
              'B': {'CLIENT_READY': 1, 'SENT': 5, 'RESPONSE': 5, 'CANCEL_REQUEST': 1,
                    'CANCEL_RESPONSE': 1, 'RESULT': 4, 'READY_REQUEST': 2, 'READY_REPLY': 2, 'BUSINESS_DONE': 1, 'CLOSED': 1}}
    for side in 'AB':
        for event, count in counts[side].items():
            if len(select(side, event)) != count:
                raise ValueError('surplus event: ' + event)
    return {'requests': 5, 'accepted': 4, 'rejected': 1, 'succeeded': 2, 'canceled': 1, 'aborted': 1,
            'targeted_cancel': 1, 'accepted_uuids': {k: v for k, v in ids.items() if k != '-1'},
            'rejected_uuid': {'server': ids['-1'], 'client': 'NOT_EXPOSED_NULL_GOAL_HANDLE'},
            'observed_feedback': observed, 'concurrency': 'TWO_EXECUTING_SERVER_GOALS_NORMAL_ACTIVE_DURING_CANCEL',
            'feedback_oracle': 'VALID_OBSERVED_SUBSEQUENCE_WITH_MANDATORY_CANCEL_PREFIX',
            'scope': 'REAL_ROS_WINDOWS_HOST_ACTION_ACCEPTANCE_WITH_APPLICATION_READINESS_NOT_NATIVE',
            'acceptance_precondition': {'service': 'example_interfaces/srv/Trigger', 'phases': ['PAIR', 'TAIL'],
                                        'requests': 2, 'grants': 2, 'replies': 2},
            'unbarriered_action_validated': False, 'status_topic_validated': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'bridge', 'fastdds-prefix', 'ros-prefix', 'pixi-prefix', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    log_dir = args.output / 'ros-logs'
    log_dir.mkdir()
    run = secrets.token_hex(8)
    reservations = [socket.socket() for _ in 'AB']
    try:
        for sock in reservations:
            sock.bind(('127.0.0.1', 0))
        ports = [sock.getsockname()[1] for sock in reservations]
    finally:
        for sock in reservations:
            sock.close()
    processes, streams, commands, errors = {}, [], {}, []
    released, runtime_released, timed_out = False, False, False
    inputs = [args.exe, args.bridge, args.fastdds_prefix / 'bin/fastrtps-2.14.dll', args.ros_prefix / 'bin/fastcdr-2.2.dll']
    inputs.extend(args.ros_prefix / 'bin' / name for name in (
        'rclcpp.dll', 'rclcpp_action.dll', 'rcl_action.dll', 'rmw_fastrtps_cpp.dll',
        'example_interfaces__rosidl_typesupport_cpp.dll', 'example_interfaces__rosidl_typesupport_fastrtps_cpp.dll',
        'action_msgs__rosidl_typesupport_cpp.dll', 'action_msgs__rosidl_typesupport_fastrtps_cpp.dll'))
    expected_modules = {path.name.lower(): path.resolve() for path in inputs}
    observed_modules = {side: {} for side in 'AB'}
    hashes = lambda: {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    inputs.extend(Path(__file__).with_name(name) for name in (
        'run_action_acceptance.py', 'control_files.py', 'host_receive_capture.py', 'host_connection_capture.py', 'host_io_capture.py'))
    before = hashes()
    try:
        for index, side in enumerate('AB'):
            config = args.output / (side + '.conf')
            config.write_text('\n'.join([
                'bridge_library=' + str(args.bridge.resolve()),
                'fastdds_module=' + str(inputs[2].resolve()), 'fastcdr_module=' + str(inputs[3].resolve()),
                'locator_version=V1', 'native_name_contract=PACKAGE_SCOPED_V1', 'authorized_package=host.ros.test',
                'service_prefix=host.ros.test.v1', 'backend=HOST_OPAQUE_BYTES_TEST_ONLY', 'run=' + run,
                'side=' + str(index), 'port=' + str(ports[index]), 'peer_port=' + str(ports[1-index]),
                'domain=201', 'forward=1', 'host_close_error=0', 'host_io_observation=1', 'host_receive_observation=1',
                'runtime_close_release=' + str((args.output / (side + '.runtime')).resolve())]) + '\n')
            env = os.environ.copy()
            for key in list(env):
                if key.startswith(('FASTDDS_', 'FDDST_', 'FASTRTPS_')) or key in ('ROS_LOCALHOST_ONLY', 'RMW_FASTRTPS_USE_QOS_FROM_XML'):
                    env.pop(key)
            env.update(PATH=os.pathsep.join([str(args.fastdds_prefix / 'bin'), str(args.ros_prefix / 'bin'),
                       str(args.pixi_prefix / 'Library/bin'), env.get('PATH', '')]),
                       RMW_IMPLEMENTATION='rmw_fastrtps_cpp', ROS_DOMAIN_ID='201', ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET',
                       ROS_LOG_DIR=str(log_dir.resolve()),
                       RCUTILS_CONSOLE_OUTPUT_FORMAT='[{severity}] [{name}]: {message}',
                       RCUTILS_COLORIZED_OUTPUT='0', RCUTILS_LOGGING_USE_STDOUT='0',
                       FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(config.resolve()), FASTDDS_ENVIRONMENT_FILE='', SKIP_DEFAULT_XML='1',
                       FDDST_ACTION_RELEASE=str((args.output / 'business.release').resolve()))
            out = (args.output / (side + '.stdout')).open('wb')
            err = (args.output / (side + '.stderr')).open('wb')
            streams.extend([out, err])
            commands[side] = [str(args.exe.resolve()), side, run]
            processes[side] = subprocess.Popen(commands[side], env=env, stdout=out, stderr=err)
        deadline = time.monotonic() + 65
        while any(p.poll() is None for p in processes.values()):
            for side, process in processes.items():
                if process.poll() is not None or len(observed_modules[side]) == len(expected_modules):
                    continue
                try:
                    for mapping in psutil.Process(process.pid).memory_maps(grouped=False):
                        path = Path(mapping.path).resolve()
                        name = path.name.lower()
                        if name in expected_modules and name not in observed_modules[side]:
                            observed_modules[side][name] = {'path': str(path),
                                'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                except psutil.NoSuchProcess:
                    pass
            texts = {s: (args.output / (s + '.stdout')).read_text(errors='replace') for s in 'AB'}
            failed = any('ROS_FAILURE ' in texts[s] or processes[s].poll() not in (None, 0) for s in 'AB')
            if not released and (failed or all('event=BUSINESS_DONE ' in texts[s] for s in 'AB')):
                publish_control(args.output / 'business.release', 'RELEASE ' + run + '\n')
                released = True
            if not runtime_released and (failed or all(re.search(r'^ROS_RUNTIME_CLOSE_READY schema=1 run=' + run +
                    ' side=' + s + ' pid=' + str(processes[s].pid) + ' ', texts[s], re.M) for s in 'AB')):
                for side in 'AB':
                    publish_control(args.output / (side + '.runtime'),
                        f'ROS_RUNTIME_CLOSE_RELEASE schema=1 run={run} side={side} pid={processes[side].pid} stage=RUNTIME_CLOSE reason=' +
                        ('WINDOW_FAILED' if failed else 'NORMAL_COMPLETION') + '\n')
                runtime_released = True
            if time.monotonic() > deadline:
                timed_out = True
                break
            time.sleep(.01)
    except Exception as exc:
        errors.append(type(exc).__name__ + ': ' + str(exc))
    finally:
        for process in processes.values():
            if process.poll() is None:
                process.kill()
            process.wait(timeout=5)
        for stream in streams:
            stream.close()
    if timed_out or len(processes) != 2 or any(p.returncode != 0 for p in processes.values()):
        errors.append('process_failed_or_timed_out')
    result = {'run': run, 'commands': commands, 'inputs_before': before, 'inputs_after': hashes(),
              'processes': {s: {'pid': p.pid, 'exit': p.returncode} for s, p in processes.items()},
              'ports': ports, 'timed_out': timed_out, 'device_tested': False, 'native_tested': False,
              'actual_loaded_modules': observed_modules}
    try:
        for side in 'AB':
            if set(observed_modules[side]) != set(expected_modules):
                raise ValueError('actual module coverage')
            for name, expected in expected_modules.items():
                observed = observed_modules[side][name]
                if Path(observed['path']) != expected or observed['sha256'] != before[str(expected)]:
                    raise ValueError('actual module binding')
        texts = {s: (args.output / (s + '.stdout')).read_text(errors='strict') for s in 'AB'}
        result['business'] = analyze(texts, run, {s: p.pid for s, p in processes.items()})
        for index, side in enumerate('AB'):
            stderr = (args.output / (side + '.stderr')).read_text(errors='strict')
            if re.search(r'failed to send response[^\r\n]*timeout', texts[side] + '\n' + stderr, re.I):
                raise ValueError('action_response_send_timeout')
            capture = validate_capture(texts[side], run, side, processes[side].pid)
            result[side + '_capture'] = capture
            if not capture['complete'] or f'FDDST_ROS_PRE_PARTICIPANT run={run} side={index} builtin=0 intraprocess=0 mode=HOST_OPAQUE_BYTES_TEST_ONLY' not in stderr:
                raise ValueError('transport path/capture')
            io = re.findall(r'FDDST_HOST_IO run=' + run + ' side=' + str(index) +
                            r' sends=([0-9]+) ingress=([0-9]+) send_errors=([0-9]+) fake_sdk=1 ([^\r\n]*)', stderr)
            if len(io) != 1 or int(io[0][0]) == 0 or int(io[0][1]) == 0:
                raise ValueError('host traffic/summary')
            result[side + '_host_io'] = {'sends': int(io[0][0]), 'ingress': int(io[0][1]),
                                        'send_errors': int(io[0][2]), 'remaining_summary': io[0][3]}
            if int(io[0][2]) != 0:
                raise ValueError('observed Host send errors')
            if f'FDDST_ROS_CLOSED run={run} side={index} pump_joined=1 runtime_closed=1 local_only=1' not in stderr:
                raise ValueError('runtime cleanup')
        pids = {s: p.pid for s, p in processes.items()}
        result['connections'] = match_connections(texts, run, pids)
        result['receive_capture'] = {}
        for side in 'AB':
            result['receive_capture'][side] = validate_receive_capture(texts[side], run, side, pids[side],
                profile='full_host', endpoints=[pair[side] for pair in result['connections']],
                expected_ingress=result[side + '_host_io']['ingress'])
            if 'FDDST_HOST_RX_CAPTURE_INCOMPLETE' in (args.output / (side + '.stderr')).read_text():
                raise ValueError('receive capture export incomplete')
    except (ValueError, KeyError, OSError) as exc:
        errors.append(str(exc))
    result['exclusive_rebind'] = []
    for port in ports:
        try:
            with socket.socket() as sock:
                if hasattr(socket, 'SO_EXCLUSIVEADDRUSE'):
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                sock.bind(('127.0.0.1', port))
            result['exclusive_rebind'].append(True)
        except OSError:
            result['exclusive_rebind'].append(False)
            errors.append('port_rebind')
    if before != result['inputs_after'] or not released or not runtime_released:
        errors.append('binding_or_release')
    result.update(errors=errors, passed=not errors)
    (args.output / 'result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
