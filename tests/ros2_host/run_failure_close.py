"""Actual ROS Host processes withhold one business release; no device APIs."""
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


p = argparse.ArgumentParser()
for name in ('exe', 'bridge', 'fastdds-prefix', 'ros-prefix', 'pixi-prefix', 'output'):
    p.add_argument('--' + name, type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=False)
run = secrets.token_hex(8)
ports = []
for _ in range(2):
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        ports.append(s.getsockname()[1])
assert len(set(ports)) == 2
inputs = [a.exe, a.bridge, a.fastdds_prefix / 'bin/fastrtps-2.14.dll']
hashes = {str(x.resolve()): hashlib.sha256(x.read_bytes()).hexdigest() for x in inputs}
processes, streams, configs, commands = {}, {}, {}, {}
for index, side in enumerate('AB'):
    conf = a.output / (side + '.conf')
    conf.write_text('\n'.join([
        'bridge_library=' + str(a.bridge.resolve()),
        'fastdds_module=' + str((a.fastdds_prefix / 'bin/fastrtps-2.14.dll').resolve()),
        'fastcdr_module=' + str((a.ros_prefix / 'bin/fastcdr-2.2.dll').resolve()),
        'locator_version=V1\nbackend=HOST_OPAQUE_BYTES_TEST_ONLY', 'run=' + run, 'side=' + str(index),
        'port=' + str(ports[index]), 'peer_port=' + str(ports[1-index]),
        'domain=201', 'forward=1']) + '\n', encoding='utf-8')
    env = os.environ.copy()
    env['PATH'] = os.pathsep.join([str(a.fastdds_prefix / 'bin'), str(a.ros_prefix / 'bin'),
                                  str(a.pixi_prefix / 'Library/bin'), env.get('PATH', '')])
    env.update(RMW_IMPLEMENTATION='rmw_fastrtps_cpp', ROS_DOMAIN_ID='201',
               ROS_AUTOMATIC_DISCOVERY_RANGE='SUBNET',
               FASTDDS_DSOFTBUS_REQUIRED_CONFIG=str(conf.resolve()),
               FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX=str(a.output / (side + '-control')),
               FASTDDS_ENVIRONMENT_FILE='', SKIP_DEFAULT_XML='1')
    for key in ('FASTRTPS_DEFAULT_PROFILES_FILE', 'FASTDDS_DEFAULT_PROFILES_FILE', 'ROS_LOCALHOST_ONLY'):
        env.pop(key, None)
    out = (a.output / (side + '.stdout')).open('wb')
    err = (a.output / (side + '.stderr')).open('wb')
    streams[side] = (out, err)
    argv = [str(a.exe.resolve()), side, run, 'dual']
    commands[side] = argv
    configs[side] = str(conf)
    processes[side] = subprocess.Popen(argv, env=env, stdout=out, stderr=err, shell=False)

ready = set()
deadline = time.monotonic() + 35
while time.monotonic() < deadline and len(ready) < 2:
    for side in 'AB':
        text = (a.output / (side + '.stdout')).read_text(encoding='utf-8', errors='replace')
        if re.search(r'^ROS_STAGE run=' + run + r' side=' + side +
                     r' pid=[1-9][0-9]* stage=RELIABLE_READY$', text, re.M):
            ready.add(side)
    if any(proc.poll() is not None for proc in processes.values()):
        break
    time.sleep(.02)

if len(ready) == 2:
    # A nonempty but invalid control record makes the real application return
    # from ControlStage without stretching its fixed 90-second budget.
    for side in 'AB':
        (a.output / (side + '-control.RELIABLE_READY')).write_text(
            f'ROS_RELEASE run={run} side={side} stage=WITHHELD_BY_HOST_TEST\n', encoding='utf-8')

timed_out = False
deadline = time.monotonic() + 30
while any(proc.poll() is None for proc in processes.values()):
    if time.monotonic() > deadline:
        timed_out = True
        for proc in processes.values():
            if proc.poll() is None:
                proc.kill()
        break
    time.sleep(.02)
for proc in processes.values():
    proc.wait(timeout=5)
for pair in streams.values():
    for stream in pair:
        stream.close()

texts = {side: (a.output / (side + '.stdout')).read_text(encoding='utf-8', errors='replace') +
               '\n' + (a.output / (side + '.stderr')).read_text(encoding='utf-8', errors='replace') for side in 'AB'}
checks = {}
for side in 'AB':
    text = texts[side]
    checks[side] = dict(
        ready=side in ready,
        failure=bool(re.search(r'^ROS_FAILURE schema=1 run=' + run + r' side=' + side +
                               r' pid=[1-9][0-9]* seq=[1-9][0-9]* stage=PHASE_READY reason=PHASE_RELEASE_MISSING failure_ref=0$', text, re.M)),
        context_failure=bool(re.search(r'^ROS_CONTEXT_CLOSED run=' + run + r' side=' + side +
                                        r' result=4 result_scope=APPLICATION_EXIT_STATUS shutdown_return=1$', text, re.M)),
        writes=len(re.findall(r'^ROS_WRITE ', text, re.M)),
        takes=len(re.findall(r'^ROS_TAKE ', text, re.M)),
        runtime_closed=('FDDST_ROS_CLOSED run=' + run) in text,
        startup_capture=('ROS_STARTUP_CAPTURE schema=1 run=' + run) in text,
        safe_snapshot_refusal='ROS_DIAGNOSTIC_CAPTURE status=INCOMPLETE reason=NO_SAFE_MODULE_SNAPSHOT' in text,
        factory_incomplete='FDDST_FACTORY_DESTRUCTOR_INCOMPLETE_NO_NORMAL_CLOSE' in text,
        module_unload_fault=any(x in text for x in ('use-after-free', 'heap-use-after-free', 'FDDST_MODULE_UNLOAD_WHILE_ACTIVE')))

result = dict(scope='ACTUAL_ROS_HOST_FAILURE_PATH_NO_DSOFTBUS_DEVICE', run=run,
              inputs=hashes, commands=commands, configurations=configs,
              ready_sides=sorted(ready), injected_release='INVALID_NONEMPTY_AFTER_BOTH_READY' if len(ready) == 2 else 'NOT_INJECTED',
              processes={side: dict(pid=proc.pid, exit_code=proc.returncode) for side, proc in processes.items()},
              checks=checks, timed_out=timed_out, device_operations=0)
result['pass'] = not timed_out and len(ready) == 2 and all(
    processes[side].returncode == 4 and checks[side]['failure'] and checks[side]['context_failure'] and
    checks[side]['writes'] == checks[side]['takes'] == 0 and checks[side]['runtime_closed'] and
    (checks[side]['startup_capture'] or checks[side]['safe_snapshot_refusal']) and
    not checks[side]['factory_incomplete'] and not checks[side]['module_unload_fault'] for side in 'AB')
(a.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result))
raise SystemExit(0 if result['pass'] else 1)
