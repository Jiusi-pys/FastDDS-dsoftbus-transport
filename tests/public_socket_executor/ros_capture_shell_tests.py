"""Run the materialized collector with a live WSL /proc process; never use HDC."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import time
import unittest


REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / 'scripts/public_socket/capture-load.sh'
OLD = REPO / '.artifacts/ros2-device-acceptance/exec-service-name-20261002/windows/data-1/A/aux/capture-load.sh'
NAMES = ('ordinary_ros2_transport_consumer', 'mdds_installed_consumer', 'public_socket_dds_experiment', 'fastdds_dsoftbus_dds214_baseline')
SHELL = ('wsl', '-d', 'Ubuntu-20.04', '-u', 'root', '--exec')
HELPER = r'''
#include <stdio.h>
#include <unistd.h>
int main(void) {
    FILE *pid = fopen("pid.txt", "w");
    if (!pid) return 10;
    fprintf(pid, "%ld\n", (long)getpid());
    fclose(pid);
    for (int i = 0; i < 4000; ++i) {
        if (access("stop", F_OK) == 0) return 0;
        usleep(10000);
    }
    return 11;
}
'''


def wsl(*args, input=None, timeout=30):
    return subprocess.run([*SHELL, *args], input=input, capture_output=True,
                          timeout=timeout, check=False)


def linux_path(path):
    path = Path(path).resolve().as_posix()
    if not re.fullmatch(r'[A-Za-z]:/.*', path):
        raise ValueError('Expected Windows absolute path')
    return '/mnt/' + path[0].lower() + path[2:]


def materialize(task):
    task.parent.mkdir(parents=True, exist_ok=True)
    profile = task.parent / (task.name + '-local-profile.json')
    profile.write_text(json.dumps(dict(
        targets={'A': 'HOST_A', 'B': 'HOST_B'}, tokens={'A': '123', 'B': '456'},
        hdc_path=sys.executable, query_helper_path=sys.executable,
        permission_baseline_sha256={'A': 'a'*64, 'B': 'b'*64})), encoding='utf-8')
    spec = importlib.util.spec_from_file_location('formal_materializer',
        REPO / 'scripts/public_socket/shared_reattach/materialize.py')
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    mod.materialize(REPO, task, profile)
    script = task / 'executor/capture-load.sh'
    assert hashlib.sha256(script.read_bytes()).digest() == hashlib.sha256(SOURCE.read_bytes()).digest()
    return script


class RealShellCollector(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        probe = wsl('/bin/sh', '-c', 'test -r /proc/self/stat && command -v cc && command -v readlink')
        if probe.returncode:
            raise RuntimeError('Ubuntu-20.04 WSL /proc, compiler and readlink required: '
                               + probe.stderr.decode(errors='replace'))

    def setUp(self):
        self.run = secrets.token_hex(8)
        self.root = f'/data/local/tmp/mdds_dds_{self.run}_A'
        self.evidence = REPO / '.artifacts/ros2-load-shell-tests' / ('run-' + self.run)
        self.evidence.mkdir(parents=True, exist_ok=False)
        self.call_index = 0
        result = wsl('/bin/sh', '-c', f'mkdir -p {self.root}')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.child = None

    def tearDown(self):
        if self.child is not None:
            wsl('/bin/sh', '-c', f'touch {self.root}/stop')
            try:
                self.child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                # Only the exact test PID written by our own executable is eligible.
                pid = self.live_pid()
                if pid:
                    wsl('/bin/sh', '-c', f'kill {pid}')
                self.child.wait(timeout=8)
            self.child.stdout.close()
            self.child.stderr.close()
        # Keep the bounded test root and raw files for review; never touch old windows.

    def live_pid(self):
        proc = wsl('/bin/sh', '-c', f'cat {self.root}/pid.txt')
        value = proc.stdout.decode().strip()
        return value if proc.returncode == 0 and re.fullmatch(r'[1-9][0-9]*', value) else None

    def start(self, name):
        self.assertIn(name, NAMES)
        compiled = wsl('cc', '-x', 'c', '-o', f'{self.root}/{name}', '-', input=HELPER.encode())
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        cmd = f'cd {self.root} && env -i PATH=/system/bin:/system/xbin ' \
              f'LD_LIBRARY_PATH={self.root}:/system/lib64/platformsdk SKIP_DEFAULT_XML=1 ' \
              f'ROS_LOG_DIR={self.root}/ros-log RMW_IMPLEMENTATION=rmw_fastrtps_cpp ' \
              f'ROS_DOMAIN_ID=201 ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET ' \
              f'FASTDDS_DSOFTBUS_REQUIRED_CONFIG={self.root}/ros.conf ' \
              f'FASTDDS_DSOFTBUS_NATIVE_TOKEN=123 FASTDDS_DSOFTBUS_IDENTITY_ADMITTED=1 ' \
              f'FASTDDS_DSOFTBUS_ROS_CONTROL_PREFIX={self.root}/control ' \
              f'./{name} A {self.run} dual'
        self.child = subprocess.Popen([*SHELL, '/bin/sh', '-c', cmd],
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for _ in range(50):
            pid = self.live_pid()
            if pid:
                self.assertIsNone(self.child.poll(), 'Local helper exited before capture')
                return pid
            time.sleep(.05)
        self.fail('Local helper did not expose its actual WSL PID')

    def run_script(self, script, name, pid, root=None, side='A', run=None):
        root = self.root if root is None else root
        run = self.run if run is None else run
        script = linux_path(script)
        command = f'PATH=/usr/bin:/bin /bin/sh {script} {run} {pid} {root} {side}'
        if name is not None:
            command += ' ' + name
        result = wsl('/bin/sh', '-c', command, timeout=25)
        self.call_index += 1
        prefix = f'shell-{self.call_index:02}'
        (self.evidence / (prefix + '-command.json')).write_text(json.dumps(dict(
            command=command, script_sha256=hashlib.sha256(Path(script.replace('/mnt/c/', 'C:/')).read_bytes()).hexdigest(),
            main=name, pid=pid, run=run, side=side, root=root, exit_code=result.returncode,
            host_scope='HOST_LIVE_PROC_COLLECTOR_TEST_NOT_DEVICE_EVIDENCE'), indent=2), encoding='utf-8')
        (self.evidence / (prefix + '.stdout')).write_bytes(result.stdout)
        (self.evidence / (prefix + '.stderr')).write_bytes(result.stderr)
        return result

    def test_old_bytes_reject_real_ros_main(self):
        if not OLD.is_file():
            self.skipTest('Historical immutable device helper is not distributed with a clone')
        self.assertEqual(hashlib.sha256(OLD.read_bytes()).hexdigest(),
                         'ad2268f9e9472c00bf6d18261fa400b708151114a2cd32936c978bd72563290c')
        pid = self.start(NAMES[0])
        result = self.run_script(OLD, NAMES[0], pid)
        self.assertEqual(result.returncode, 2)
        self.assertNotEqual(wsl('/bin/sh', '-c', f'test -e {self.root}/load-status.txt').returncode, 0)

    def test_materialized_ros_main_reaches_real_capture_body(self):
        task = REPO / '.artifacts/ros2-load-materialized' / ('run-' + self.run)
        script = materialize(task)
        pid = self.start(NAMES[0])
        wsl('/bin/sh', '-c', f'printf "%s\\n" {self.root}/{NAMES[0]} > {self.root}/required-load-paths.txt')
        result = self.run_script(script, NAMES[0], pid)
        self.assertEqual(result.returncode, 0, result.stderr)
        status = wsl('/bin/sh', '-c', f'cat {self.root}/load-status.txt')
        self.assertIn(b'status=COMPLETE', status.stdout)
        for name in ('load-exe.txt', 'load-cwd.txt', 'load-cmdline.txt', 'load-environ.txt',
                     'load-stat-before.txt', 'load-stat-after.txt', 'load-uptime.txt', 'load-maps.txt'):
            self.assertEqual(wsl('/bin/sh', '-c', f'test -s {self.root}/{name}').returncode, 0, name)

    def test_existing_installed_main_still_collects(self):
        self.assert_approved_main_collects(NAMES[1])

    def test_existing_probe_main_still_collects(self):
        self.assert_approved_main_collects(NAMES[2])

    def assert_approved_main_collects(self, name):
        task = REPO / '.artifacts/ros2-load-materialized' / ('run-' + self.run)
        script = materialize(task)
        pid = self.start(name)
        wsl('/bin/sh', '-c', f'printf "%s\\n" {self.root}/{name} > {self.root}/required-load-paths.txt')
        result = self.run_script(script, None if name == NAMES[2] else name, pid)
        self.assertEqual(result.returncode, 0, result.stderr)
        status = wsl('/bin/sh', '-c', f'cat {self.root}/load-status.txt')
        self.assertIn(b'status=COMPLETE', status.stdout)

    def test_bad_identity_and_output_root_rejected(self):
        task = REPO / '.artifacts/ros2-load-materialized' / ('run-' + self.run)
        script = materialize(task)
        pid = self.start(NAMES[0])
        for member, target_pid, root, side, run in (
            ('unapproved_consumer', pid, self.root, 'A', self.run),
            (NAMES[0], 'not-a-pid', self.root, 'A', self.run),
            (NAMES[0], pid, self.root + '-elsewhere', 'A', self.run),
            (NAMES[0], pid, self.root, 'C', self.run),
            (NAMES[0], pid, self.root, 'A', self.run.upper()),
        ):
            with self.subTest(member=member, pid=target_pid, root=root, side=side, run=run):
                result = self.run_script(script, member, target_pid, root, side, run)
                self.assertEqual(result.returncode, 2)
                self.assertNotEqual(wsl('/bin/sh', '-c', f'test -e {self.root}/load-status.txt').returncode, 0)
        valid_but_wrong_pid = self.run_script(script, NAMES[0], '9999999999')
        self.assertNotEqual(valid_but_wrong_pid.returncode, 0)
        status = wsl('/bin/sh', '-c', f'cat {self.root}/load-status.txt')
        self.assertIn(b'status=PROCESS_EXITED', status.stdout)
        self.assertNotIn(b'status=COMPLETE', status.stdout)


if __name__ == '__main__':
    unittest.main()
