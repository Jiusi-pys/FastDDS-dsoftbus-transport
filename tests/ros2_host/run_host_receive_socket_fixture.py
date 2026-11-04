"""One directed actual Host TCP/Runtime budget fixture, never native or ROS."""
import argparse
import hashlib
import json
from pathlib import Path
import secrets
import socket
import subprocess
import time

from host_io_capture import validate_capture
from host_connection_capture import _capture
from host_receive_capture import validate_receive_capture


def rows(text, prefix):
    result = []
    for line in text.splitlines():
        if not line.startswith(prefix + ' '):
            continue
        pairs = [token.split('=', 1) for token in line.split()[1:]]
        if any(len(pair) != 2 for pair in pairs) or len(dict(pairs)) != len(pairs):
            raise ValueError('malformed or duplicate fixture field')
        result.append(dict(pairs))
    return result


def analyze(text, run, pid, port):
    io = validate_capture(text, run, 'A', pid)
    if not io['complete']:
        raise ValueError('incomplete IO capture')
    connections = _capture(text, run, 'A', pid)
    journal = validate_receive_capture(text, run, 'A', pid, profile='full_host', endpoints=connections, expected_ingress=6)
    if (journal['charged'], journal['refunded'], journal['refusals'], journal['scopes']) != (5, 5, 1, 1):
        raise ValueError('directed budget counts')
    # Same-process self-loop only; never fabricate A/B cross-process identities.
    if len(connections) != 2 or {row['role'] for row in connections} != {'OUTGOING', 'ACCEPTED'}:
        raise ValueError('self-loop endpoint count/roles')
    outgoing = next(row for row in connections if row['role'] == 'OUTGOING')
    accepted = next(row for row in connections if row['role'] == 'ACCEPTED')
    if (outgoing['local_address'], outgoing['local_port'], outgoing['peer_address'], outgoing['peer_port']) != (
            accepted['peer_address'], accepted['peer_port'], accepted['local_address'], accepted['local_port']):
        raise ValueError('self-loop endpoint reverse tuple')
    identity = dict(schema='1', run=run, side='A', pid=str(pid))
    done = rows(text, 'HOST_RX_SOCKET_DONE')
    expected = dict(identity, port=str(port), sends='6', ingress='6', held='4', refused='1', delivered='5',
                    released='5', runtime_closed='1', joined='1', payload='OPAQUE_COMPONENT',
                    ros='NOT_TESTED', native='NOT_TESTED', historical='NOT_REPRODUCED')
    if done != [expected]:
        raise ValueError('fixture completion/cardinality')
    admissions = rows(text, 'HOST_RX_SOCKET_ADMISSION')
    if len(admissions) != 6:
        raise ValueError('fixture admission count')
    events = rows(text, 'HOST_RX_EVENT')
    gates = {(row['scope'], row['gate']): row for row in events if row['kind'] not in ('DECISION', 'STAGE')}
    decisions = [row for row in events if row['kind'] == 'DECISION' and row['bridge_called'] == '1']
    if len(decisions) != 6:
        raise ValueError('six original bridge decisions required')
    records = []
    for index, row in enumerate(admissions, 1):
        if any(row.get(key) != value for key, value in identity.items()):
            raise ValueError('admission identity')
        if (row['attempt'], row['ingress'], row['payload']) != (str(index), str(index), str(100 + min(index, 5))):
            raise ValueError('directed payload/ingress sequence')
        if any(int(row[key]) != accepted[key] for key in ('socket', 'epoch', 'handle')):
            raise ValueError('admission original accepted connection')
        original = gates.get((row['scope'], row['gate']))
        if not original or original['kind'] != ('CAPACITY_REFUSAL' if index == 5 else 'CHARGE'):
            raise ValueError('admission exact budget event')
        if (any(row[key] != original[key] for key in ('record', 'ingress')) or original['size'] != '65500'
                or row['size'] != original['ingress_size'] or row['size'] != '65500'
                or any(row[key] != original['ingress_' + key] for key in ('socket', 'epoch', 'handle'))):
            raise ValueError('admission scope/record/size join')
        matches = [decision for decision in decisions if decision['scope'] == row['scope']
                   and decision['gate'] == row['gate']]
        if len(matches) != 1 or matches[0]['reason'] != row['reason']:
            raise ValueError('original decision join')
        if index == 5:
            if (original['local_bytes'], original['local_bytes_limit'], original['gate_capacity_mask'],
                    original['gate_capacity_reason'], original['record']) != ('262000', '262144', '8', '28', '0'):
                raise ValueError('original LOCAL_BYTES gate snapshot')
        else:
            records.append(row['record'])
    if len(set(records)) != 5 or '0' in records:
        raise ValueError('immutable record identity/recovery reuse')
    recovery_gate = int(admissions[-1]['gate'])
    refunded = [event for event in events if event['kind'] == 'REFUND_RECORD' and event['record'] in records[:4]]
    if len(refunded) != 4 or any(int(event['gate']) >= recovery_gate for event in refunded):
        raise ValueError('held reservations not refunded before explicit resend')
    deliveries = [event for event in events if event['kind'] == 'STAGE' and event['stage'] == '4']
    releases = [event for event in events if event['kind'] == 'STAGE' and event['stage'] == '6']
    if {event['record'] for event in deliveries} != set(records) or len(deliveries) != 5:
        raise ValueError('actual receiver invocation stages')
    if {event['record'] for event in releases} != set(records) or len(releases) != 5:
        raise ValueError('actual payload release stages')
    return dict(io=io, journal=journal, self_loop_connection=dict(outgoing=outgoing, accepted=accepted),
                delivered=5, released=5, refusal='LOCAL_BYTES', explicit_resend_accepted=True,
                scope='SINGLE_PROCESS_HOST_SOCKET_RUNTIME_COMPONENT_NOT_ROS_NOT_NATIVE')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--fastdds', type=Path, required=True)
    parser.add_argument('--fastcdr', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    output = args.output.resolve()
    inputs = [args.exe.resolve(), args.fastdds.resolve(), args.fastcdr.resolve(), Path(__file__).resolve()]
    inputs += [Path(__file__).with_name(name).resolve() for name in
               ('host_io_capture.py', 'host_connection_capture.py', 'host_receive_capture.py')]
    def hashes():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
    before = hashes()
    run = secrets.token_hex(8)
    with socket.socket() as reservation:
        reservation.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command = [str(args.exe.resolve()), run, str(port), str(args.fastdds.resolve()), str(args.fastcdr.resolve())]
    errors, forced_kill, process = [], False, None
    started = time.time()
    with (output / 'A.stdout').open('wb') as out, (output / 'A.stderr').open('wb') as err:
        try:
            process = subprocess.Popen(command, stdout=out, stderr=err)
            process.wait(timeout=35)
        except Exception as error:
            errors.append(type(error).__name__ + ': ' + str(error))
        finally:
            if process is not None:
                if process.poll() is None:
                    forced_kill = True
                    process.kill()
                process.wait(timeout=5)
    result = dict(command=command, run=run, port=port, started_unix=started, ended_unix=time.time(),
                  inputs_before=before, inputs_after=hashes(), forced_kill=forced_kill,
                  process={'pid': process.pid, 'exit': process.returncode, 'owned_process_ended': process.poll() is not None} if process else None,
                  device_tested=False, native_tested=False, ros_tested=False)
    try:
        if process is None or process.returncode != 0 or forced_kill:
            raise ValueError('fixture process failed')
        text = (output / 'A.stdout').read_text(encoding='utf-8')
        result['evidence'] = analyze(text, run, process.pid, port)
        modules = [line for line in text.splitlines() if line.startswith('HOST_RX_SOCKET_MODULE ')]
        expected = {'fastrtps-2.14.dll': args.fastdds.resolve(), 'fastcdr-2.2.dll': args.fastcdr.resolve()}
        if len(modules) != 2:
            raise ValueError('actual dependency module receipts')
        for line in modules:
            module, path = line.removeprefix('HOST_RX_SOCKET_MODULE module=').split(' path=', 1)
            if module not in expected or not Path(path).samefile(expected.pop(module)):
                raise ValueError('actual loaded dependency mismatch')
    except (ValueError, KeyError, OSError) as error:
        errors.append(str(error))
    try:
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            probe.bind(('127.0.0.1', port))
        result['exclusive_rebind'] = True
    except OSError as error:
        result['exclusive_rebind'] = False
        errors.append('port rebind: ' + str(error))
    if before != result['inputs_after']:
        errors.append('input hash changed')
    result.update(errors=errors, passed=not errors)
    (output / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
