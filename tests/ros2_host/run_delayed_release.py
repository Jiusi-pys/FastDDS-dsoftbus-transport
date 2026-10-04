"""Actual ROS pair: a valid delayed control release must not consume business time."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import time

p = argparse.ArgumentParser()
for name in ('exe', 'bridge', 'fastdds-prefix', 'ros-prefix', 'pixi-prefix', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
p.add_argument('--delay', type=float, default=31)
a = p.parse_args()
if not 0 <= a.delay <= 35:
    p.error('Only bounded 0..35 second Host control delays are supported')
a.output = a.output.resolve()
a.output.mkdir(parents=True, exist_ok=False)
run = secrets.token_hex(8)
ports = []
for _ in 'AB':
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        ports.append(sock.getsockname()[1])
assert len(set(ports)) == 2
inputs = [a.exe, a.bridge, a.fastdds_prefix / 'bin/fastrtps-2.14.dll']
hashes = {str(x.resolve()): hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}
processes, streams, commands = {}, [], {}
for i, side in enumerate('AB'):
    conf = a.output / (side + '.conf')
    conf.write_text('\n'.join([
        'bridge_library=' + str(a.bridge.resolve()),
        'fastdds_module=' + str((a.fastdds_prefix / 'bin/fastrtps-2.14.dll').resolve()),
        'fastcdr_module=' + str((a.ros_prefix / 'bin/fastcdr-2.2.dll').resolve()),
        'locator_version=V1\nbackend=HOST_OPAQUE_BYTES_TEST_ONLY', 'run=' + run, 'side=' + str(i),
        'port=' + str(ports[i]), 'peer_port=' + str(ports[1-i]), 'domain=201', 'forward=1']) + '\n')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join([str(a.fastdds_prefix / 'bin'), str(a.ros_prefix / 'bin'),
                                  str(a.pixi_prefix / 'Library/bin'), env.get('PATH', '')])
    env.update(RMW_IMPLEMENTATION='rmw_fastrtps_cpp', ROS_DOMAIN_ID='201',
               ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET', SKIP_DEFAULT_XML='1', FASTDDS_ENVIRONMENT_FILE='',
               FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(conf),
               FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=str(a.output / (side + '-control')))
    for key in ('FASTRTPS_DEFAULT_PROFILES_FILE', 'FASTDDS_DEFAULT_PROFILES_FILE', 'ROS_LOCALHOST_ONLY'):
        env.pop(key, None)
    out = (a.output / (side + '.stdout')).open('wb')
    err = (a.output / (side + '.stderr')).open('wb')
    streams += [out, err]
    commands[side] = [str(a.exe.resolve()), side, run, 'dual']
    processes[side] = subprocess.Popen(commands[side], env=env, stdout=out, stderr=err, shell=False)
stages = ('RELIABLE_READY', 'RELIABLE_DONE', 'BEST_EFFORT_READY', 'BEST_EFFORT_DONE', 'TEARDOWN_READY')
ready, releases, killed = {}, [], []
deadline = time.monotonic() + 100
try:
    while any(proc.poll() is None for proc in processes.values()) and time.monotonic() < deadline:
        texts = {s: (a.output / (s + '.stdout')).read_text(errors='replace') for s in 'AB'}
        for stage in stages:
            if stage in {r['stage'] for r in releases}:
                continue
            if all(f'ROS_STAGE run={run} side={s} pid={processes[s].pid} stage={stage}\n' in texts[s] for s in 'AB'):
                now = time.monotonic()
                ready.setdefault(stage, now)
                delay = a.delay if stage == 'RELIABLE_READY' else 0
                if now - ready[stage] >= delay:
                    for side in 'AB':
                        if stage == 'TEARDOWN_READY':
                            (a.output / (side + '-control.CLEANUP')).write_text(
                                f'ROS_CLEANUP schema=1 run={run} side={side} pid={processes[side].pid} stage=CLEANUP reason=NORMAL_COMPLETION\n')
                        else:
                            (a.output / (side + '-control.' + stage)).write_text(
                                f'ROS_RELEASE run={run} side={side} stage={stage}\n')
                    releases.append(dict(stage=stage, waited_seconds=now-ready[stage]))
        time.sleep(.02)
finally:
    for side, proc in processes.items():
        if proc.poll() is None:
            killed.append(side)
            proc.kill()
        proc.wait(timeout=5)
    for stream in streams:
        stream.close()
spec = importlib.util.spec_from_file_location('formal_ros', Path(__file__).resolve().parents[2] / 'scripts/public_socket/analyze_ros2.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
texts = {s: (a.output / (s + '.stdout')).read_text(errors='replace') for s in 'AB'}
ledger = module.analyze(texts, run, ['reliable', 'best_effort'])
closed = {s: ('FDDST_ROS_CLOSED run=' + run) in (a.output / (s + '.stderr')).read_text(errors='replace') for s in 'AB'}
passed = not killed and ledger['pass'] and all(proc.returncode == 0 for proc in processes.values()) and all(closed.values()) and len(releases) == len(stages)
result = dict(scope='ACTUAL_ROS_HOST_DELAYED_RELEASE_NOT_DEVICE', run=run, delay=a.delay,
              inputs=hashes, commands=commands, releases=releases, killed=killed,
              exit_codes={s: proc.returncode for s, proc in processes.items()}, runtime_closed=closed,
              ledger=ledger, device_operations=0, **{'pass': passed})
(a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if passed else 1)
