"""One bounded real ROS parameter pair over the explicitly configured Host seam."""
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


def reject_response_timeouts(text):
    # This installed rclcpp API logs RCL_RET_TIMEOUT and returns void. A normal
    # return alone cannot attest a late response send; retain other warnings.
    if re.search(r'failed to send response to [^\r\n]+ \(timeout\):', text):
        raise ValueError('service_response_send_timeout')


def analyze(texts, run, pids):
    rows = {}
    for side in 'AB':
        rows[side] = []
        for line in texts[side].splitlines():
            if not line.startswith('PARAM_ACCEPT '):
                continue
            pairs = [item.split('=', 1) for item in line.split()[1:]]
            if any(len(item) != 2 for item in pairs):
                raise ValueError('record syntax')
            row = dict(pairs)
            if len(row) != len(pairs) or (row.get('run'), row.get('side'), row.get('pid')) != (run, side, str(pids[side])):
                raise ValueError('record identity')
            rows[side].append(row)
    encode = lambda value: value.encode().hex() or '-'
    def packed(counter, label, readonly=7):
        return f'case.counter:2:{counter}|case.label:4:{encode(label)}|case.readonly:2:{readonly}'
    def one(side, event, **fields):
        selected = [row for row in rows[side] if row.get('event') == event]
        if len(selected) != 1 or any(selected[0].get(k) != str(v) for k, v in fields.items()):
            raise ValueError(event)
    initial = packed(10, 'initial_' + run)
    accepted = packed(12, 'accepted_' + run)
    final = packed(13, 'recovered_' + run)
    server = '/param_' + run + '/server'
    one('A', 'SERVER_READY', node=server, values=initial)
    one('A', 'BUSINESS_DONE', values=final, undeclared=0)
    one('A', 'CLOSED', result=0)
    if len(rows['A']) != 3:
        raise ValueError('server record cardinality')
    one('B', 'CLIENT_READY', node=server)
    one('B', 'ABSENT', ready=0, wait_ms=100)
    one('B', 'BUSINESS_DONE', calls=17, accepted_mutations=3, rejected_mutations=4, changed_events=3)
    one('B', 'CLOSED', result=0)
    operations = [row for row in rows['B'] if 'op' in row]
    if [row['op'] for row in operations] != [str(i) for i in range(1, 18)]:
        raise ValueError('operation order/cardinality')
    reads = {1: initial, 6: accepted, 8: accepted, 10: accepted, 12: accepted, 14: accepted, 16: final}
    requests = {
        4: 'case.counter:2:11',
        5: f'case.counter:2:12|case.label:4:{encode("accepted_" + run)}',
        7: 'case.counter:2:99|case.readonly:2:8',
        9: f'case.counter:4:{encode("wrong_type")}|case.label:4:{encode("bad_type_" + run)}',
        11: 'case.counter:2:99|case.undeclared:2:1',
        13: f'case.counter:2:-1|case.label:4:{encode("bad_callback_" + run)}',
        15: f'case.counter:2:13|case.label:4:{encode("recovered_" + run)}',
    }
    for row in operations:
        op = int(row['op'])
        if op in reads:
            if row.get('event') != 'READ' or row.get('values') != reads[op]:
                raise ValueError('readback after mutation')
        elif op == 2:
            if row.get('event') != 'DESCRIBE' or row.get('descriptors') != 'case.counter:2:0:0|case.label:4:0:0|case.readonly:2:1:0':
                raise ValueError('descriptor')
        elif op in (3, 17):
            if row.get('event') != 'LIST' or row.get('names') != 'case.counter,case.label,case.readonly':
                raise ValueError('listed names')
        else:
            success = op in (4, 5, 15)
            if (row.get('event') != 'MUTATE' or row.get('request') != requests[op] or
                    row.get('atomic') != ('0' if op == 4 else '1') or row.get('ok') != str(int(success))):
                raise ValueError('mutation result/request')
            reason = row.get('reason', '')
            if not re.fullmatch(r'(?:-|(?:[0-9a-f]{2})+)', reason) or (not success and reason == '-'):
                raise ValueError('rejection reason')
            if op == 13 and reason != encode('negative_counter'):
                raise ValueError('callback rejection')
    changes = []
    events = [row for row in rows['B'] if row.get('event') == 'PARAM_EVENT']
    for row in events:
        if row.get('node') != server or not re.fullmatch(r'-?[0-9]+', row.get('stamp_sec', '')) or not re.fullmatch(r'[0-9]+', row.get('stamp_nsec', '')):
            raise ValueError('event identity/time')
        if not 0 <= int(row['stamp_nsec']) < 1000000000 or row.get('deleted') != '-':
            raise ValueError('unexpected deletion/event stamp')
        if row.get('changed') != '-':
            if row.get('new') != '-':
                raise ValueError('mixed change event')
            changes.append(row['changed'])
        elif row.get('new') not in initial.split('|'):
            raise ValueError('unexpected declaration event')
    if changes != [requests[4], requests[5], requests[15]]:
        raise ValueError('changed event sequence/cardinality/value')
    if len(rows['B']) != 21 + len(events):
        raise ValueError('unexpected client event')
    return {'remote_calls': 17, 'read_metadata_calls': 10, 'accepted_mutations': 3,
            'rejected_mutations': 4, 'changed_events': 3, 'declaration_events': len(events) - 3,
            'scope': 'REAL_ROS_WINDOWS_HOST_PARTIAL_PARAMETERS_NOT_NATIVE',
            'old_values': 'PRIOR_GET_SNAPSHOTS_NOT_EVENT_FIELDS', 'absence_observation_ms': 250}

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
        'rclcpp.dll', 'rmw_fastrtps_cpp.dll', 'rcl_interfaces__rosidl_typesupport_cpp.dll',
        'rcl_interfaces__rosidl_typesupport_fastrtps_cpp.dll'))
    expected_modules = {path.name.lower(): path.resolve() for path in inputs}
    observed_modules = {side: {} for side in 'AB'}
    hashes = lambda: {str(p.resolve()): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
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
                'domain=201', 'forward=1', 'host_close_error=0', 'host_io_observation=1',
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
                       FDDST_PARAMETER_RELEASE=str((args.output / 'business.release').resolve()))
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
            if not released and (failed or ('event=SERVER_READY ' in texts['A'] and 'event=BUSINESS_DONE ' in texts['B'])):
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
            reject_response_timeouts(texts[side] + '\n' + stderr)
            capture = validate_capture(texts[side], run, side, processes[side].pid)
            result[side + '_capture'] = capture
            if not capture['complete'] or f'FDDST_ROS_PRE_PARTICIPANT run={run} side={index} builtin=0 intraprocess=0 mode=HOST_OPAQUE_BYTES_TEST_ONLY' not in stderr:
                raise ValueError('transport path/capture')
            if not re.search(r'FDDST_HOST_IO run=' + run + ' side=' + str(index) + r' sends=[1-9][0-9]* ingress=[1-9][0-9]* send_errors=0 fake_sdk=1 ', stderr):
                raise ValueError('host traffic/error')
            if f'FDDST_ROS_CLOSED run={run} side={index} pump_joined=1 runtime_closed=1 local_only=1' not in stderr:
                raise ValueError('runtime cleanup')
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
