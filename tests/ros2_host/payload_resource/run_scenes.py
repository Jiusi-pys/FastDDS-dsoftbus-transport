"""Local-only ordinary-ROS payload resource scenes: two real secure ROS processes on loopback.

No HDC, device, SDK or external address. Scene bytes are mutated only on a loopback TCP relay
between the two test processes (see loopback_relay.py). The relay is a test fixture, never a guard.
"""
import argparse
import json
import os
import re
import secrets
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from loopback_relay import Relay  # noqa: E402

SCENES = {
    # scene: (M, B, N)  TEST_ONLY values, never product values.
    'boundary': (4096, 65536, 16),
    'huge': (4096, 65536, 16),
    'count': (4096, 1 << 20, 2),
    'budget': (4096, 2 * (4096 + 6) + 100, 64),
}


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def signal(root, name):
    (root / name).write_text(name + '\n')


def wait_for(path, text, seconds):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if path.is_file() and text in path.read_text(errors='replace'):
            return True
        time.sleep(0.05)
    return False


def main():
    p = argparse.ArgumentParser()
    for name in ('exe', 'bridge', 'fastdds-prefix', 'ros-prefix', 'pixi-prefix', 'keystore', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--scene', choices=sorted(SCENES), required=True)
    p.add_argument('--limits', nargs=3, type=int, help='override TEST_ONLY M B N')
    p.add_argument('--no-guard', action='store_true', help='negative control: omit payload keys (legacy unprotected)')
    p.add_argument('--capture-dir', type=Path, help='run each child under the per-process debug capture (Windows); dumps stay local')
    p.add_argument('--inject-close-incomplete', choices=('A', 'B'), help='Host-only counterexample: pin one side\'s close verdict to incomplete')
    p.add_argument('--inject-send-terminal', choices=('A', 'B'), help='Host scheduling only: end an original connection after provider admission, before the real backend predicate')
    p.add_argument('--paired-close', action='store_true', help='use the existing sealed/pump-joined native-close barrier on both sides')
    a = p.parse_args()
    m, b, n = a.limits if a.limits else SCENES[a.scene]
    run = '0123456789abcdef'  # the frozen id the test keystore grants name; no other id is authorized
    a.output.mkdir(parents=True, exist_ok=False)
    root = a.output / 'control'
    root.mkdir()
    ports = [free_port(), free_port()]
    relay = Relay(ports[1], a.scene, m)  # A's outgoing connection goes through the relay to B.
    peers = [relay.port, ports[0]]
    procs, files = {}, {}
    for index, side in enumerate('AB'):
        conf = a.output / (side + '.conf')
        conf.write_text('\n'.join([
            'bridge_library=' + str(a.bridge.resolve()),
            'fastdds_module=' + str((a.fastdds_prefix / 'bin/fastrtps-2.14.dll').resolve()),
            'fastcdr_module=' + str((a.ros_prefix / 'bin/fastcdr-2.2.dll').resolve()),
            'locator_version=V1\nnative_name_contract=PACKAGE_SCOPED_V1\nauthorized_package=host.ros.test\n'
            'service_prefix=host.ros.test.v1\nbackend=HOST_OPAQUE_BYTES_TEST_ONLY',
            'run=' + run, 'side=' + str(index), 'port=' + str(ports[index]), 'peer_port=' + str(peers[index]),
            'domain=201', 'forward=1', 'host_close_error=0'] +
            (['host_send_terminal_control='+str((root/'REFUSE').resolve())] if a.inject_send_terminal == side else []) +
            (['runtime_close_release='+str((root/(side+'-CLOSE')).resolve())] if a.paired_close else []) +
            (['host_close_incomplete=1'] if a.inject_close_incomplete == side else []) + ([] if a.no_guard else [
            'payload_guard_profile=TEST_ONLY_BOUNDED', 'payload_max_sample_bytes=%d' % m,
            'payload_pool_budget_bytes=%d' % b, 'payload_max_allocations=%d' % n])) + '\n')
        env = os.environ.copy()
        env['PATH'] = os.pathsep.join([str(a.fastdds_prefix / 'bin'), str(a.ros_prefix / 'bin'),
                                       str(a.pixi_prefix / 'Library/bin'), env.get('PATH', '')])
        env.update(RMW_IMPLEMENTATION='rmw_fastrtps_cpp', ROS_DOMAIN_ID='201', ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET',
                   FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(conf.resolve()), FASTDDS_ENVIRONMENT_FILE='', SKIP_DEFAULT_XML='1',
                   FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=str((a.output / (side + '-control')).resolve()),
                   FDDST_SECURITY_TRACE='1', ROS_SECURITY_ENABLE='true', ROS_SECURITY_STRATEGY='Enforce',
                   ROS_SECURITY_KEYSTORE=str(a.keystore.resolve()), ROS_SECURITY_ENCLAVE_OVERRIDE='/' + side)
        for key in ('FASTRTPS_DEFAULT_PROFILES_FILE', 'FASTDDS_DEFAULT_PROFILES_FILE', 'ROS_LOCALHOST_ONLY'):
            env.pop(key, None)
        out = (a.output / (side + '.stdout')).open('wb')
        err = (a.output / (side + '.stderr')).open('wb')
        files[side] = (out, err)
        argv = [str(a.exe.resolve()), side, run, a.scene, str(root.resolve()), str(m)]
        if a.capture_dir:
            from crash_capture import Captured
            procs[side] = Captured(argv, env, out, err, str((a.capture_dir / side).resolve()))
        else:
            procs[side] = subprocess.Popen(argv, env=env, stdout=out, stderr=err)
    outcome = dict(scene=a.scene, M=m, B=b, N=n, run=run, steps={})
    try:
        ready = all(wait_for(a.output / (s + '.stdout'), 'RESOURCE_READY', 40) for s in 'AB')
        outcome['steps']['ready'] = ready
        if ready:
            if a.inject_send_terminal:
                signal(root, 'REFUSE')
            signal(root, 'START')
            outcome['steps']['pre_sent'] = wait_for(a.output / 'A.stdout', 'RESOURCE_PRE_SENT', 30)
            time.sleep(1.5)
            signal(root, 'EARLY')
            outcome['steps']['cancelled'] = wait_for(a.output / 'B.stdout', 'RESOURCE_CANCELLED', 20)
            if a.inject_send_terminal:
                signal(root, 'REFUSE')
            signal(root, 'PROGRESS')
            outcome['steps']['sent'] = wait_for(a.output / 'A.stdout', 'RESOURCE_SENT', 20)
            time.sleep(1.0)
            signal(root, 'OBSERVE')
            outcome['steps']['oracle'] = wait_for(a.output / 'B.stdout', 'RESOURCE_ORACLE', 20)
            outcome['relay_errors_before_oracle'] = list(relay.errors)  # later errors are teardown noise
            wait_for(a.output / 'B.stdout', 'RESOURCE_DONE', 20)
            signal(root, 'CLEANUP')
            if a.paired_close:
                close_ready=all(wait_for(a.output/(s+'.stdout'),'ROS_RUNTIME_CLOSE_READY',20) for s in 'AB')
                outcome['steps']['paired_close_ready']=close_ready
                for s in 'AB':
                    log=(a.output/(s+'.stdout')).read_text(errors='replace')
                    row=re.search(r'ROS_RUNTIME_CLOSE_READY schema=1 run='+run+r' side='+s+r' pid=(\d+)',log)
                    if row:
                        (root/(s+'-CLOSE')).write_text('ROS_RUNTIME_CLOSE_RELEASE schema=1 run='+run+' side='+s+
                            ' pid='+row.group(1)+' stage=RUNTIME_CLOSE reason='+('NORMAL_COMPLETION' if close_ready else 'WINDOW_FAILED')+'\n')
    finally:
        codes = {}
        for side, proc in procs.items():
            try:
                codes[side] = proc.wait(timeout=40)
            except subprocess.TimeoutExpired:
                proc.kill()
                codes[side] = 'KILLED_BY_TEST_TIMEOUT'
        for out, err in files.values():
            out.close()
            err.close()
        relay.close()
    outcome['exit'] = codes
    if a.capture_dir:
        outcome['capture'] = {}
        for side, proc in procs.items():
            proc.write_report(str(a.output / (side + '.capture.json')))
            r = proc.report
            outcome['capture'][side] = dict(exit_code=r['exit_code'], first_fatal=r['first_fatal'], complete=r['capture_complete'],
                                            dump=(r['dump'] or {}).get('path'))
    outcome['relay_events'] = relay.events[:8]
    outcome['relay_errors_after_oracle'] = relay.errors[len(outcome.get('relay_errors_before_oracle', [])):][:4]
    outcome['relay_errors'] = outcome.get('relay_errors_before_oracle', relay.errors)[:4]
    text = (a.output / 'B.stdout').read_text(errors='replace')
    err_a = (a.output / 'A.stderr').read_text(errors='replace') + (a.output / 'B.stderr').read_text(errors='replace')
    outcome['bound_readers'] = len(re.findall('FDDST_PAYLOAD_READER_CREATED', err_a))
    outcome['asan'] = 'AddressSanitizer' in err_a
    outcome['oracle_line'] = next((l for l in text.splitlines() if l.startswith('RESOURCE_ORACLE')), None)
    # The teardown contract is asserted, not assumed, and its two failure shapes are kept
    # apart. A side that never printed its joined local close left the owner retained: that is
    # an incomplete close (FDDST_ROS_CLOSED absent or the incomplete markers present), and it
    # exits 0, so without this check a broken teardown was only ever visible through a fatal
    # exit code -- which is exactly what the incomplete path can fail to produce. A side that
    # did release its owner but reported an execution error is a different, already-reported
    # outcome and must not be read as the same thing.
    close_contract = {}
    for side in 'AB':
        err_side = (a.output / (side + '.stderr')).read_text(errors='replace')
        released = ('FDDST_ROS_CLOSED run=%s side=%d' % (run, 'AB'.index(side))) in err_side
        incomplete = ('FDDST_RUNTIME_CLOSE_INCOMPLETE' in err_side
                      or 'FDDST_CLOSE_INCOMPLETE' in err_side)
        runtime_error = ('FDDST_CLOSE_RESOURCE_COMPLETE_WITH_RUNTIME_ERROR' in err_side
                         or 'Failed to delete participant' in err_side)
        close_contract[side] = dict(released=released, incomplete=incomplete, runtime_error=runtime_error,
                                    ok=released and not incomplete and not runtime_error)
    outcome['close_contract'] = close_contract
    outcome['passed'] = (codes.get('A') == 0 and codes.get('B') == 0 and not outcome['asan']
                         and 'result=PASS' in (outcome['oracle_line'] or '') and not outcome['relay_errors']
                         and close_contract['A']['ok'] and close_contract['B']['ok'])
    (a.output / 'RESULT.json').write_text(json.dumps(outcome, indent=2) + '\n')
    print(json.dumps(outcome))
    return 0 if outcome['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
