"""One bounded real ROS service pair over the explicitly configured Host seam."""
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
            if not line.startswith('SERVICE_ACCEPT '):
                continue
            pairs = [x.split('=', 1) for x in line.split()[1:]]
            if any(len(x) != 2 for x in pairs):
                raise ValueError('record syntax')
            row = dict(pairs)
            if len(row) != len(pairs) or (row.get('run'), row.get('side'), row.get('pid')) != (run, side, str(pids[side])):
                raise ValueError('record identity')
            rows[side].append(row)
    for side in 'AB':
        peer = 'B' if side == 'A' else 'A'
        own, remote = rows[side], rows[peer]
        select = lambda records, event: [x for x in records if x.get('event') == event]
        sent = select(own, 'CLIENT_SENT')
        responses = select(own, 'CLIENT_RESPONSE')
        received = select(remote, 'SERVER_REQUEST')
        returned = select(remote, 'SERVER_RESPONSE')
        seed = '1000' if side == 'A' else '2000'
        values = ['1', '2', '3', '4', '-100', '-101', '99']
        if [x.get('a') for x in sent] != values or len(received) != 7 or len(returned) != 7:
            raise ValueError('request cardinality')
        if [x.get('a') for x in returned[:4]] != ['4', '3', '2', '1']:
            raise ValueError('reverse response order')
        writers = []
        for request in sent:
            value, ident = request['a'], request['id']
            generation = '2' if value == '99' else '1'
            if request.get('b') != seed or request.get('generation') != generation:
                raise ValueError('request value/generation')
            matches = [x for x in received if x.get('a') == value and x.get('id') == ident and x.get('b') == seed]
            if len(matches) != 1 or not re.fullmatch('[0-9a-f]{32}', matches[0].get('writer', '')):
                raise ValueError('remote request identity')
            writers.append(matches[0]['writer'])
            reply = [x for x in returned if x.get('a') == value and x.get('id') == ident and x.get('b') == seed]
            if len(reply) != 1 or reply[0].get('sum') != str(int(value) + int(seed)):
                raise ValueError('remote response')
            accepted = [x for x in responses if x.get('a') == value and x.get('generation') == generation and x.get('id') == ident]
            if value == '-100':
                if accepted:
                    raise ValueError('expired response accepted')
            elif len(accepted) != 1 or accepted[0].get('sum') != str(int(value) + int(seed)):
                raise ValueError('client response identity/value')
        if len(responses) != 6 or len(set(writers[:6])) != 1 or writers[6] == writers[0]:
            raise ValueError('client recreation identity')
        if len({x['id'] for x in sent[:6]}) != 6:
            raise ValueError('duplicate active request id')
        expected = {
            'ABSENT': {'ready': '0'}, 'CONCURRENT': {'pending': '4'},
            'TIMEOUT': {'id': sent[4]['id'], 'removed': '1', 'second_remove': '0', 'wait_ms': '250'},
            'LATE_IGNORED': {'id': sent[4]['id'], 'future': 'broken_promise'},
            'BUSINESS_DONE': {'received': '7', 'responded': '7', 'accepted': '6', 'timed_out': '1'},
            'CLOSED': {'result': '0'},
        }
        for event, fields in expected.items():
            matches = select(own, event)
            if len(matches) != 1 or any(matches[0].get(k) != v for k, v in fields.items()):
                raise ValueError(event)
        if len(own) != 33:  # 7 sent + 6 accepted + 7 server requests + 7 replies + 6 milestones
            raise ValueError('unexpected service event')
    return {'requests': 14, 'server_responses': 14, 'accepted_responses': 12, 'timed_out_removed': 2,
            'recreated_clients': 2, 'scope': 'REAL_ROS_WINDOWS_HOST_NOT_NATIVE'}


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
                       FDDST_SERVICE_RELEASE=str((args.output / 'business.release').resolve()))
            out = (args.output / (side + '.stdout')).open('wb')
            err = (args.output / (side + '.stderr')).open('wb')
            streams.extend([out, err])
            commands[side] = [str(args.exe.resolve()), side, run]
            processes[side] = subprocess.Popen(commands[side], env=env, stdout=out, stderr=err)
        deadline = time.monotonic() + 65
        while any(p.poll() is None for p in processes.values()):
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
              'ports': ports, 'timed_out': timed_out, 'device_tested': False, 'native_tested': False}
    try:
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
