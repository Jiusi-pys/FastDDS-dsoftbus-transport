"""Directed two-process Windows Host seam evidence; not ROS or native DSoftBus."""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import socket
import subprocess
import time

from control_files import publish_control
from host_connection_capture import match_connections
from host_io_capture import validate_capture


def fields(line, prefix):
    if not line.startswith(prefix + ' '):
        raise ValueError('record prefix')
    pairs = [part.split('=', 1) for part in line.split()[1:]]
    if any(len(pair) != 2 for pair in pairs):
        raise ValueError('record syntax')
    result = dict(pairs)
    if len(result) != len(pairs):
        raise ValueError('duplicate field')
    return result


def analyze(texts, run, pids, mode):
    """Check actual result/counters, local shutdown and send association, no clock inference."""
    pairs = match_connections(texts, run, pids)
    if len(pairs) != 1 or pairs[0]['A']['role'] != 'OUTGOING' or pairs[0]['B']['role'] != 'ACCEPTED':
        raise ValueError('directed connection identity')
    pair = pairs[0]
    done, io_events = {}, {}
    for side in 'AB':
        identity = {'schema': '1', 'run': run, 'side': side, 'pid': str(pids[side])}
        closes_here = (mode == 'peer-close' and side == 'B') or (mode == 'local-close' and side == 'A')
        expected_commands = (['CLOSE'] if closes_here else []) + (['SEND'] if side == 'A' else []) + ['STOP']
        expected_receipts = ['READY'] + (['CLOSED'] if closes_here else []) + (['SENT'] if side == 'A' else
                            ['RECEIVED'] if mode == 'positive' else [])
        for prefix, actions in [('HOST_DIRECTED_COMMAND', expected_commands), ('HOST_DIRECTED_CONTROL', expected_receipts)]:
            controls = [fields(line, prefix) for line in texts[side].splitlines() if line.startswith(prefix + ' ')]
            if controls != [dict(identity, action=action) for action in actions]:
                raise ValueError('child control sequence')
        rows = [fields(line, 'HOST_DIRECTED_DONE') for line in texts[side].splitlines()
                if line.startswith('HOST_DIRECTED_DONE ')]
        if len(rows) != 1 or any(rows[0].get(k) != v for k, v in identity.items()):
            raise ValueError('done identity/count')
        row = done[side] = rows[0]
        if set(row) != set(identity) | {'mode', 'socket', 'result', 'binds', 'received', 'shutdowns',
                                       'correct', 'sends', 'ingress', 'send_errors', 'joined'}:
            raise ValueError('done fields')
        expected_received = '1' if side == 'B' and mode == 'positive' else '0'
        if (row['mode'] != mode or row['socket'] != ('2' if side == 'A' else '3') or row['binds'] != '1'
                or row['received'] != expected_received or row['ingress'] != expected_received
                or row['correct'] != '1' or row['joined'] != '1' or row['shutdowns'] not in ('0', '1')):
            raise ValueError('callback/cleanup contract')
        io_events[side] = [fields(line, 'HOST_IO_EVENT') for line in texts[side].splitlines()
                           if line.startswith('HOST_IO_EVENT ')]
    actual = int(done['A']['result'])
    allowed = {'positive': (0,), 'local-close': (-608,), 'peer-close': (0, -608, -609)}[mode]
    if actual not in allowed or done['B']['result'] != '999':
        raise ValueError('send result')
    for side in 'AB':
        if (done[side]['sends'] != str(int(side == 'A' and actual != -608)) or
                done[side]['send_errors'] != str(int(side == 'A' and actual == -609))):
            raise ValueError('send counters')
    send_rows = [fields(line, 'HOST_DIRECTED_SEND') for line in texts['A'].splitlines()
                 if line.startswith('HOST_DIRECTED_SEND ')]
    if len(send_rows) != 1 or send_rows[0] != {'schema': '1', 'run': run, 'side': 'A',
            'pid': str(pids['A']), 'socket': '2', 'result': str(actual)}:
        raise ValueError('send record')
    entries = [row for row in io_events['A'] if row['kind'] == 'SEND_ENTER']
    returns = [row for row in io_events['A'] if row['kind'] == 'SEND_RETURN']
    if (len(entries) != 1 or len(returns) != 1 or entries[0]['send'] != returns[0]['send']
            or entries[0]['send'] == '0' or returns[0]['result'] != str(actual)
            or returns[0]['result_known'] != '1' or returns[0]['socket'] != '2'):
        raise ValueError('send observation association')
    captures = [row for row in io_events['A'] if row['kind'] == 'SEND_CAPTURE']
    if (len(captures) != 1 or captures[0]['send'] != entries[0]['send']
            or any(int(captures[0][key]) != pair['A'][key] for key in ('socket', 'epoch'))
            or any(captures[0][key] != returns[0][key] for key in ('socket', 'epoch', 'handle'))
            or not int(entries[0]['seq']) < int(captures[0]['seq']) < int(returns[0]['seq'])):
        raise ValueError('send capture connection association')
    if actual != -608 and int(captures[0]['handle']) != pair['A']['handle']:
        raise ValueError('send handle association')
    transfers = [row for row in io_events['A'] if row['kind'] == 'SEND_TRANSFER']
    if any(row['send'] != entries[0]['send'] or any(int(row[key]) != pair['A'][key]
            for key in ('socket', 'epoch', 'handle')) for row in transfers):
        raise ValueError('send transfer identity')
    if actual == -608 and transfers:
        raise ValueError('invalid handle performed transfer')
    if actual == 0 and ([row['phase'] for row in transfers] != ['PREFIX', 'BODY']
            or [row['result'] for row in transfers] != ['4', '3']
            or any(row['wsa_known'] != '0' or row['termination'] != 'NONE' for row in transfers)):
        raise ValueError('successful transfer evidence')
    if actual == -609 and (not transfers or transfers[-1]['termination'] not in ('SOCKET_ERROR', 'ZERO_PROGRESS')):
        raise ValueError('failed transfer evidence')
    close_side = {'positive': None, 'peer-close': 'B', 'local-close': 'A'}[mode]
    public_closes = {side: [row for row in io_events[side] if row['kind'] == 'CLOSE_RETURN'
                           and row['phase'] == 'PUBLIC_SHUTDOWN'] for side in 'AB'}
    for side in 'AB':
        if len(public_closes[side]) != int(side == close_side):
            raise ValueError('public close count')
    if close_side:
        close = public_closes[close_side][0]
        if (any(int(close[key]) != pair[close_side][key] for key in ('socket', 'epoch', 'handle'))
                or close['result_known'] != '1' or close['result'] != '0'):
            raise ValueError('actual close result')
        shutdown = [row for row in io_events[close_side] if row['kind'] == 'SHUTDOWN_RETURN'
                    and row['phase'] == 'PUBLIC_SHUTDOWN']
        if (len(shutdown) != 1 or any(int(shutdown[0][key]) != pair[close_side][key]
                for key in ('socket', 'epoch', 'handle')) or shutdown[0]['result_known'] != '1'
                or shutdown[0]['result'] != '0' or int(shutdown[0]['seq']) >= int(close['seq'])):
            raise ValueError('actual shutdown result')
        if close_side == 'A' and int(close['seq']) >= int(entries[0]['seq']):
            raise ValueError('local close/send ordering')
    return {'actual_send_result': actual, 'peer_close_send_succeeded': mode == 'peer-close' and actual == 0,
            'payload_received': int(done['B']['received']), 'scope': 'WINDOWS_HOST_SOCKET_SEAM_NOT_ROS_NOT_NATIVE'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('positive', 'peer-close', 'local-close'), required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    inputs = [args.exe.resolve(), Path(__file__).resolve(), Path(__file__).with_name('host_connection_capture.py'),
              Path(__file__).with_name('host_io_capture.py'), Path(__file__).with_name('control_files.py')]
    def hashes():
        return {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    before = hashes()
    run = secrets.token_hex(8)
    processes, streams, commands, receipts, errors = {}, [], {}, [], []
    reservations = [socket.socket() for _ in 'AB']
    try:
        for sock in reservations:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            sock.bind(('127.0.0.1', 0))
        ports = [sock.getsockname()[1] for sock in reservations]
    finally:
        for sock in reservations:
            sock.close()
    deadline = time.monotonic() + 40
    def record(side, action):
        return f'HOST_DIRECTED_CONTROL schema=1 run={run} side={side} pid={processes[side].pid} action={action}\n'
    def publish(side, action):
        publish_control(output / f'{side}.{action}', record(side, action))
        receipts.append({'order': len(receipts) + 1, 'direction': 'command', 'side': side, 'action': action,
                         'controller_monotonic_ns': time.monotonic_ns()})
    def wait_receipt(side, action):
        path = output / f'{side}.{action}'
        while not path.exists():
            if time.monotonic() >= deadline or any(p.poll() is not None for p in processes.values()):
                raise RuntimeError(f'child exited or deadline waiting {side}.{action}')
            time.sleep(.005)
        if path.read_text(encoding='utf-8') != record(side, action):
            raise ValueError('receipt identity/format')
        receipts.append({'order': len(receipts) + 1, 'direction': 'receipt', 'side': side, 'action': action,
                         'controller_monotonic_ns': time.monotonic_ns()})
    try:
        for index, side in enumerate('AB'):
            out = (output / f'{side}.stdout').open('wb')
            err = (output / f'{side}.stderr').open('wb')
            streams.extend([out, err])
            commands[side] = [str(args.exe.resolve()), side, run, args.mode, str(ports[index]), str(ports[1-index]), str(output)]
            processes[side] = subprocess.Popen(commands[side], stdout=out, stderr=err)
        for side in 'AB':
            wait_receipt(side, 'READY')
        close_side = {'positive': None, 'peer-close': 'B', 'local-close': 'A'}[args.mode]
        if close_side:
            publish(close_side, 'CLOSE')
            wait_receipt(close_side, 'CLOSED')
        publish('A', 'SEND')
        wait_receipt('A', 'SENT')
        if args.mode == 'positive':
            wait_receipt('B', 'RECEIVED')
        for side in 'AB':
            publish(side, 'STOP')
        for process in processes.values():
            process.wait(timeout=max(.01, deadline - time.monotonic()))
    except Exception as exc:
        errors.append(type(exc).__name__ + ': ' + str(exc))
    finally:
        killed = []
        for side, process in processes.items():
            if process.poll() is None:
                killed.append(side); process.kill()
            process.wait(timeout=5)
        for stream in streams:
            stream.close()
    result = {'run': run, 'mode': args.mode, 'commands': commands, 'controller_sequence': receipts,
              'inputs_before': before, 'inputs_after': hashes(), 'ports': ports, 'forced_kill': killed,
              'processes': {side: {'pid': p.pid, 'exit': p.returncode, 'owned_process_ended': p.poll() is not None}
                            for side, p in processes.items()},
              'device_tested': False, 'ros_tested': False, 'native_tested': False}
    try:
        expected_sequence = [('receipt', 'A', 'READY'), ('receipt', 'B', 'READY')]
        close_side = {'positive': None, 'peer-close': 'B', 'local-close': 'A'}[args.mode]
        if close_side:
            expected_sequence += [('command', close_side, 'CLOSE'), ('receipt', close_side, 'CLOSED')]
        expected_sequence += [('command', 'A', 'SEND'), ('receipt', 'A', 'SENT')]
        if args.mode == 'positive':
            expected_sequence += [('receipt', 'B', 'RECEIVED')]
        expected_sequence += [('command', 'A', 'STOP'), ('command', 'B', 'STOP')]
        if [(r['direction'], r['side'], r['action']) for r in receipts] != expected_sequence:
            raise ValueError('controller sequence incomplete')
        if len(processes) != 2 or killed or any(p.returncode != 0 for p in processes.values()):
            raise ValueError('process failure')
        texts = {side: (output / f'{side}.stdout').read_text(encoding='utf-8') for side in 'AB'}
        pids = {side: p.pid for side, p in processes.items()}
        for side in 'AB':
            capture = validate_capture(texts[side], run, side, pids[side])
            result[side + '_capture'] = capture
            if not capture['complete']:
                raise ValueError('incomplete capture')
        pairs = match_connections(texts, run, pids)
        if len(pairs) != 1:
            raise ValueError('connection pair count')
        result['matched_connections'] = pairs
        result['business'] = analyze(texts, run, pids, args.mode)
    except (ValueError, KeyError, OSError) as exc:
        errors.append(str(exc))
    result['exclusive_rebind'] = []
    for port in ports:
        try:
            with socket.socket() as sock:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
                sock.bind(('127.0.0.1', port))
            result['exclusive_rebind'].append(True)
        except OSError:
            result['exclusive_rebind'].append(False); errors.append('port_rebind')
    if before != result['inputs_after']:
        errors.append('input_hash_changed')
    result.update(errors=errors, passed=not errors)
    (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
