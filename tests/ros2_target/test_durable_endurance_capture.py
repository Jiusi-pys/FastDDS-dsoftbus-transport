"""HOST models plus real Windows job/process fixtures; no SDK/device command."""
from contextlib import ExitStack
from copy import deepcopy
from datetime import datetime, timedelta, timezone
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('durable_capture_under_test', HERE / 'durable_endurance_capture.py')
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)


class PrivateFiles(unittest.TestCase):
    def setUp(self):
        self.allowed_temp = Path(tempfile.gettempdir()).resolve()
        self.temporary = tempfile.TemporaryDirectory(prefix='mdds-capture-host-')
        self.root = Path(self.temporary.name).resolve()
        self.assertTrue(self.root.is_relative_to(self.allowed_temp))
        self.patches = ExitStack()

    def tearDown(self):
        self.patches.close()
        # Explicitly constrain recursive TemporaryDirectory cleanup on Windows.
        self.assertTrue(self.root.resolve().is_relative_to(self.allowed_temp))
        self.assertFalse(self.root.is_symlink())
        retained = os.environ.get('R38_FIXTURE_EVIDENCE_ROOT')
        if retained:
            # Explicit opt-in diagnostics stay within this phase's evidence tree.
            stage = HERE.parents[2].resolve()
            destination = Path(retained).resolve(strict=True)
            self.assertTrue(destination.is_relative_to(stage))
            shutil.copytree(self.root, destination / self.root.name)
        self.temporary.cleanup()

    def write(self, path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding='utf-8')

    def wait_file(self, path, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if path.exists():
                try:
                    return C.read(path)
                except json.JSONDecodeError:
                    pass
            time.sleep(0.01)
        self.fail('Private fixture file did not complete: ' + str(path))


class SpecModels(PrivateFiles):
    def setUp(self):
        super().setUp()
        self.repo, self.stage, self.business = [self.root / n for n in ('repo', 'stage', 'business')]
        for path in (self.repo, self.stage, self.business):
            path.mkdir()
        for name in C.SOURCE_PATHS:
            target = self.repo / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text('HOST_MODEL_NOT_DEVICE\n' + name, encoding='utf-8')
        self.sdk = self.root / 'HOST_FAKE_NOT_EXECUTABLE_hdc.exe'
        self.sdk.write_bytes(b'HOST MODEL ONLY: NEVER EXECUTE')
        for name, value in {'REPO': self.repo, 'RUNNER': self.repo / 'tests/ros2_target/run_endurance_roundtrip.py',
                            'CAPTURE_ROOT': self.stage, 'BUSINESS_ROOT': self.business, 'SDK': self.sdk}.items():
            self.patches.enter_context(patch.object(C, name, value))
        self.run = '0123456789abcdef'
        self.prepared = self.business / 'prepared-1111111111111111/PLAN.json'
        self.release = self.business / 'MAIN_BUSINESS_RELEASE_MODEL.json'
        self.routes = self.root / 'routes.json'
        self.spec_path = self.stage / 'CAPTURE_SPEC_MODEL.json'
        self.capture = self.stage / 'capture-2222222222222222'
        self.write(self.routes, {'observed_utc': datetime.now(timezone.utc).isoformat()})
        self.plan = dict(scope='R37_NATIVE_CLOSE_ENDURANCE_PREPARED_NOT_DISPATCHED', run=self.run,
            case='endurance_1801_ABA_30MIN', routes=str(self.routes),
            bindings={str((self.repo / p).resolve()): C.sha(self.repo / p) for p in C.SOURCE_PATHS})
        self.value = dict(schema=1, scope=C.SCOPE, run=self.run, prepared=str(self.prepared), prepared_sha256='',
            release=str(self.release), release_sha256='', output=str(self.business / 'execution-3333333333333333'),
            sdk=str(self.sdk), sdk_sha256=C.sha(self.sdk), source_sha256={p: C.sha(self.repo / p) for p in C.SOURCE_PATHS},
            capture_state=str(self.capture))
        self.commit_model()

    def commit_model(self):
        self.write(self.prepared, self.plan)
        permit = dict(schema=1, scope='MAIN_ONE_SHOT_R37_NATIVE_CLOSE_ENDURANCE_RELEASE', run=self.run,
                      prepared_sha256=C.sha(self.prepared), runner_sha256=C.sha(C.RUNNER))
        self.write(self.release, permit)
        self.value.update(prepared_sha256=C.sha(self.prepared), release_sha256=C.sha(self.release))
        self.write(self.spec_path, self.value)

    def test_valid_readonly_spec_admits_exact_thirteen_without_process(self):
        with patch.object(C.subprocess, 'Popen', side_effect=AssertionError('No host model deployment')):
            path, value, capture = C.checked_spec(self.spec_path)
        self.assertEqual(path, self.spec_path)
        self.assertEqual(capture, self.capture)
        self.assertEqual(len(value['source_sha256']), 13)
        self.assertFalse(self.capture.exists())

    def test_extra_missing_and_boolean_schema_reject(self):
        for key, value in [('schema', True), ('scope', 'OLD_SCOPE'), ('extra', 'not admitted')]:
            candidate = deepcopy(self.value)
            candidate[key] = value
            self.write(self.spec_path, candidate)
            with self.subTest(key=key), self.assertRaises(ValueError):
                C.checked_spec(self.spec_path)
        candidate = deepcopy(self.value)
        del candidate['source_sha256']
        self.write(self.spec_path, candidate)
        with self.assertRaises(ValueError):
            C.checked_spec(self.spec_path)

    def test_current_capture_or_test_source_drift_rejects_before_consume(self):
        for name in C.SOURCE_PATHS[-2:]:
            source = self.repo / name
            original = source.read_bytes()
            source.write_bytes(original + b'changed')
            with self.subTest(source=name), self.assertRaisesRegex(ValueError, 'thirteen'):
                C.checked_spec(self.spec_path)
            self.assertFalse((self.prepared.parent / (self.run + '.CONSUMED.json')).exists())
            source.write_bytes(original)

    def test_capture_validation_input_missing_from_prepared_rejects(self):
        self.plan['bindings'].pop(str((self.repo / C.SOURCE_PATHS[-1]).resolve()))
        self.commit_model()
        with self.assertRaisesRegex(ValueError, 'missing from prepared'):
            C.checked_spec(self.spec_path)

    def test_consumed_release_and_existing_output_never_replay(self):
        marker = self.prepared.parent / (self.run + '.CONSUMED.json')
        marker.write_text('original consumed bytes', encoding='ascii')
        with self.assertRaisesRegex(ValueError, 'already consumed'):
            C.checked_spec(self.spec_path)
        self.assertEqual(marker.read_text(), 'original consumed bytes')
        marker.unlink()
        Path(self.value['output']).mkdir()
        with self.assertRaisesRegex(ValueError, 'already exists'):
            C.checked_spec(self.spec_path)

    def test_240_second_freshness_and_future_observation_reject(self):
        for age in (241, -10):
            self.write(self.routes, {'observed_utc': (datetime.now(timezone.utc) - timedelta(seconds=age)).isoformat()})
            with self.subTest(age=age), self.assertRaisesRegex(ValueError, '240-second'):
                C.checked_spec(self.spec_path)

    def test_sdk_path_cannot_expand_to_same_bytes_elsewhere(self):
        other = self.root / 'other.exe'
        other.write_bytes(self.sdk.read_bytes())
        self.value['sdk'] = str(other)
        self.write(self.spec_path, self.value)
        with self.assertRaisesRegex(ValueError, 'SDK23'):
            C.checked_spec(self.spec_path)

    def test_capture_state_outside_stage_rejects(self):
        self.value['capture_state'] = str(self.root / 'capture-2222222222222222')
        self.write(self.spec_path, self.value)
        with self.assertRaisesRegex(ValueError, 'namespace'):
            C.checked_spec(self.spec_path)

    def test_old_release_scope_cannot_authorize_native_capture(self):
        permit = C.read(self.release)
        permit['scope'] = 'MAIN_ONE_SHOT_R36_ENDURANCE_RELEASE'
        self.write(self.release, permit)
        self.value['release_sha256'] = C.sha(self.release)
        self.write(self.spec_path, self.value)
        with self.assertRaisesRegex(ValueError, 'business permit'):
            C.checked_spec(self.spec_path)

    def test_launch_intent_cannot_renew_outer_or_start_deadline(self):
        self.capture.mkdir()
        tick = time.monotonic()
        intent = dict(scope='R38_CAPTURE_LAUNCH_INTENT', utc=C.utc(), run=self.run, spec=str(self.spec_path),
            spec_sha256=C.sha(self.spec_path), launcher_pid=os.getpid(), started_monotonic=tick,
            start_deadline=tick+C.START_SECONDS, outer_deadline=tick+C.OUTER_SECONDS, outer_seconds=C.OUTER_SECONDS)
        self.write(self.capture / 'INTENT.json', intent)
        self.assertEqual(C.checked_directory(self.capture)[0], self.capture)
        for field, value in [('outer_deadline', tick+2351), ('start_deadline', tick+11),
                             ('outer_seconds', True), ('started_monotonic', float('nan'))]:
            candidate = dict(intent)
            candidate[field] = value
            self.write(self.capture / 'INTENT.json', candidate)
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'fixed capture deadlines'):
                C.checked_directory(self.capture)


@unittest.skipUnless(sys.platform == 'win32', 'Real Windows job lifecycle fixtures')
class WindowsLifecycle(PrivateFiles):
    def script(self, name, code):
        path = self.root / name
        prefix = 'import sys\nsys.path.insert(0, ' + repr(str(HERE)) + ')\nimport durable_endurance_capture as C\n'
        path.write_text(prefix + code, encoding='utf-8')
        return path

    def own_handle(self, identity):
        api = C.kernel()
        handle = C.checked_win32(api.OpenProcess(0x1000, False, identity['pid']))
        self.assertEqual(C.creation_time(handle), identity['creation_time'])
        return handle

    def exit_code(self, handle, seconds=5):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            code = C.WT.DWORD()
            C.checked_win32(C.kernel().GetExitCodeProcess(handle, C.ctypes.byref(code)))
            if code.value != 259:
                return int(code.value)
            time.sleep(0.01)
        self.fail('Owned fixture process did not exit')

    def test_controlled_launcher_job_close_old_child_dies_detached_child_survives(self):
        worker = self.script('worker.py', '''from pathlib import Path
import os,time
d=Path(sys.argv[1]); deadline=time.monotonic()+8
C.save(d/'started.json',dict(pid=os.getpid(),job=C.job_snapshot()))
while not (d/'WORK').exists():
    if time.monotonic()>deadline: raise TimeoutError('private fixture work permission')
    time.sleep(.01)
for i in range(6):
    C.save(d/('pulse-'+str(i)+'.json'),dict(sequence=i));time.sleep(.05)
C.save(d/'terminal.json',dict(pulses=6))
''')
        launcher = self.script('launcher.py', '''from pathlib import Path
import subprocess,time
d,script=Path(sys.argv[1]),sys.argv[2]; deadline=time.monotonic()+8
while not (d/'ASSIGNED').exists():
    if time.monotonic()>deadline: raise TimeoutError('private fixture job assignment')
    time.sleep(.01)
argv=[sys.executable,'-X','utf8','-B',script,str(d)]
with (d/'worker.stdout').open('xb') as out,(d/'worker.stderr').open('xb') as err:
    if sys.argv[3]=='detached': child=C.detached_spawn(argv,d,out,err)
    else: child=subprocess.Popen(argv,cwd=d,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
C.save(d/'identity.json',dict(pid=child.pid,creation_time=C.creation_time(int(child._handle))))
time.sleep(15)
C.save(d/'launcher-normal-end.json',dict(normal=True))
''')
        for mode in ('inherited', 'detached'):
            with self.subTest(mode=mode):
                directory = self.root / mode
                directory.mkdir()
                handle = None
                job = C.WindowsJob(allow_breakaway=True)
                try:
                    parent = subprocess.Popen([sys.executable, '-X', 'utf8', '-B', str(launcher), str(directory),
                                               str(worker), mode], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                               creationflags=subprocess.CREATE_NO_WINDOW)
                    job.assign(parent)
                    (directory / 'ASSIGNED').write_text('own fixture assigned')
                    identity = self.wait_file(directory / 'identity.json')
                    handle = self.own_handle(identity)
                    self.wait_file(directory / 'started.json')
                    self.assertIsNone(parent.poll())
                    self.assertEqual(C.process_status(identity['pid'], identity['creation_time'])['state'], 'RUNNING')
                    job.close()
                    self.assertIsInstance(parent.wait(timeout=3), int)
                    self.assertFalse((directory / 'launcher-normal-end.json').exists())
                    (directory / 'WORK').write_text('after launcher job close')
                    code = self.exit_code(handle)
                    if mode == 'detached':
                        self.assertEqual(code, 0)
                        self.assertEqual(self.wait_file(directory / 'terminal.json')['pulses'], 6)
                    else:
                        self.assertIsInstance(code, int)
                        self.assertFalse((directory / 'terminal.json').exists())
                finally:
                    job.close()
                    if handle:
                        C.checked_win32(C.kernel().CloseHandle(handle))
                    if parent.poll() is None:
                        parent.kill();parent.wait(timeout=3)
                    if parent.stderr:
                        parent.stderr.close()

    def test_worker_abrupt_exit_closes_only_its_owned_descendant_job(self):
        grand = self.script('grand.py', '''from pathlib import Path
import os,time
C.save(Path(sys.argv[1])/'grand.json',dict(pid=os.getpid(),creation_time=C.creation_time(C.kernel().GetCurrentProcess())))
time.sleep(15)
C.save(Path(sys.argv[1])/'grand-normal-end.json',dict(normal=True))
''')
        child = self.script('child.py', '''from pathlib import Path
import os,subprocess,time
d=Path(sys.argv[1]);deadline=time.monotonic()+8
while not (d/'CHILD_JOB_ASSIGNED').exists():
    if time.monotonic()>deadline: raise TimeoutError('child job gate')
    time.sleep(.01)
grand=subprocess.Popen([sys.executable,'-B',sys.argv[2],str(d)],creationflags=subprocess.CREATE_NO_WINDOW)
C.save(d/'child.json',dict(pid=os.getpid(),creation_time=C.creation_time(C.kernel().GetCurrentProcess())))
time.sleep(15)
C.save(d/'child-normal-end.json',dict(normal=True))
''')
        owner = self.script('owner.py', '''from pathlib import Path
import os,subprocess,time
d=Path(sys.argv[1]);job=C.WindowsJob()
p=subprocess.Popen([sys.executable,'-B',sys.argv[2],str(d),sys.argv[3]],creationflags=subprocess.CREATE_NO_WINDOW)
job.assign(p);(d/'CHILD_JOB_ASSIGNED').write_text('assigned')
C.save(d/'owner.json',dict(job_name=job.name))
deadline=time.monotonic()+8
while not (d/'CRASH').exists():
    if time.monotonic()>deadline: raise TimeoutError('private owner crash gate')
    time.sleep(.01)
os._exit(23)
''')
        handles = []
        process = subprocess.Popen([sys.executable, '-B', str(owner), str(self.root), str(child), str(grand)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            for name in ('child.json', 'grand.json'):
                identity = self.wait_file(self.root / name)
                handles.append(self.own_handle(identity))
                self.assertEqual(C.process_status(identity['pid'], identity['creation_time'])['state'], 'RUNNING')
            (self.root / 'CRASH').write_text('owned worker failure fixture')
            self.assertEqual(process.wait(timeout=3), 23)
            for handle in handles:
                self.assertIsInstance(self.exit_code(handle), int)
            for name in ('child-normal-end.json', 'grand-normal-end.json'):
                self.assertFalse((self.root / name).exists())
        finally:
            if process.poll() is None:
                process.kill();process.wait(timeout=3)
            for handle in handles:
                C.checked_win32(C.kernel().CloseHandle(handle))
            if process.stderr:
                process.stderr.close()

    def test_creation_time_mismatch_is_not_reported_as_running_owner(self):
        actual = C.creation_time(C.kernel().GetCurrentProcess())
        self.assertEqual(C.process_status(os.getpid(), actual)['state'], 'RUNNING')
        self.assertEqual(C.process_status(os.getpid(), actual + 1)['state'], 'PID_REUSED')

    def test_live_state_reader_allows_coherent_append_and_ignores_partial_tail(self):
        target = self.root / 'STATE.jsonl'
        previous, following = {'generation': 1}, {'generation': 2}
        C.state(target, previous)
        with target.open('rb') as reader:
            C.state(target, following)
            self.assertEqual([json.loads(row) for row in reader.read().splitlines()], [previous, following])
            self.assertEqual(C.read(target), following)
        with target.open('ab', buffering=0) as stream:
            stream.write(b'{"generation":3')
            os.fsync(stream.fileno())
        self.assertEqual(C.read(target), following)
        with self.assertRaises(FileNotFoundError):
            C.read(self.root / 'missing' / 'STATE.jsonl')
        pending = self.root / 'capture-2222222222222222'
        pending.mkdir()
        (pending / 'STATE.jsonl').write_bytes(b'{"phase":"RUN')
        with patch.object(C, 'CAPTURE_ROOT', self.root):
            self.assertEqual(C.status_main(pending)['phase'], 'START_PENDING')

    def test_state_journal_caps_fail_before_append_and_never_read_unbounded_data(self):
        target = self.root / 'STATE.jsonl'
        with self.assertRaisesRegex(ValueError, 'State row'):
            C.state(target, dict(phase='RUNNING', oversized='x' * 1024))
        self.assertFalse(target.exists())
        target.write_bytes(b'x' * C.LOG_CAP)
        with self.assertRaisesRegex(ValueError, 'four MiB'):
            C.state(target, dict(generation=1))
        self.assertEqual(target.stat().st_size, C.LOG_CAP)
        with self.assertRaisesRegex(ValueError, 'Incomplete state row'):
            C.read(target)
        with target.open('ab') as stream:
            stream.write(b'x')
        with self.assertRaisesRegex(ValueError, 'four MiB'):
            C.read(target)

    def test_progress_is_persisted_before_pipe_eof(self):
        child = subprocess.Popen([sys.executable, '-B', '-c',
            "import time;print('first',flush=True);time.sleep(.8);print('last',flush=True)"],
            stdout=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        target, result, overflow = self.root / 'progress.stdout', {}, threading.Event()
        thread = threading.Thread(target=C.capture_stream, args=(child.stdout, target, result, overflow))
        thread.start()
        try:
            deadline = time.monotonic() + .6
            while time.monotonic() < deadline and (not target.exists() or not target.stat().st_size):
                time.sleep(.01)
            self.assertIsNone(child.poll())
            self.assertIn(b'first', target.read_bytes())
            self.assertEqual(child.wait(timeout=3), 0)
            thread.join(timeout=2)
            self.assertFalse(thread.is_alive())
            self.assertTrue(result['complete'])
            self.assertFalse(overflow.is_set())
        finally:
            if child.poll() is None:
                child.kill();child.wait(timeout=3)
            thread.join(timeout=2)

    def test_stream_over_original_four_MiB_fails_without_growing_capture(self):
        child = subprocess.Popen([sys.executable, '-B', '-c',
            'import sys;sys.stdout.buffer.write(b"x"*4194305);sys.stdout.buffer.flush()'],
            stdout=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
        target, result, overflow = self.root / 'capped.stdout', {}, threading.Event()
        thread = threading.Thread(target=C.capture_stream, args=(child.stdout, target, result, overflow))
        thread.start()
        self.assertEqual(child.wait(timeout=5), 0)
        thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(target.stat().st_size, 4194304)
        self.assertTrue(overflow.is_set())
        self.assertFalse(result['complete'])


@unittest.skipUnless(sys.platform == 'win32', 'Real worker process with explicitly modelled admission/runner')
class WorkerModels(PrivateFiles):
    """Real production worker/job/streams; only admission namespace and runner are modelled."""
    def setUp(self):
        super().setUp()
        self.model = SpecModels('test_valid_readonly_spec_admits_exact_thirteen_without_process')
        self.model.setUp()
        self.addCleanup(self.model.tearDown)

    def bootstrap(self, runner_text, *, outer_seconds=2350, held_controller=False, assignment_failure=False):
        m = self.model
        C.RUNNER.write_text(runner_text, encoding='utf-8')
        name = 'tests/ros2_target/run_endurance_roundtrip.py'
        m.value['source_sha256'][name] = C.sha(C.RUNNER)
        m.plan['bindings'][str(C.RUNNER.resolve())] = C.sha(C.RUNNER)
        m.commit_model()
        script = self.root / 'HOST_MODEL_BOOTSTRAP.py'
        lines = ['import sys,time', 'from pathlib import Path',
                 'sys.path.insert(0, ' + repr(str(HERE)) + ')', 'import durable_endurance_capture as C']
        for key in ('REPO', 'CAPTURE_ROOT', 'BUSINESS_ROOT', 'RUNNER', 'SDK'):
            lines.append('C.' + key + '=Path(' + repr(str(getattr(C, key))) + ')')
        lines += ['C.__file__=__file__', 'C.OUTER_SECONDS=' + str(outer_seconds)]
        if assignment_failure:
            lines += ['def reject_assignment(job,child):',
                      '    C.save(Path(' + repr(str(self.root / 'ASSIGNMENT_CHILD.json')) + '),dict(pid=child.pid,creation_time=C.creation_time(int(child._handle)),worker_pid=C.os.getpid(),worker_creation_time=C.creation_time(C.kernel().GetCurrentProcess())))',
                      '    deadline=time.monotonic()+3',
                      '    while not Path(' + repr(str(self.root / 'ASSIGNMENT_OBSERVED')) + ').exists():',
                      "        if time.monotonic()>deadline: raise TimeoutError('private assignment observation gate')",
                      '        time.sleep(.01)',
                      "    raise OSError(5,'HOST_MODEL_ASSIGNMENT_FAILURE_BEFORE_ASSIGN')",
                      'C.WindowsJob.assign=reject_assignment']
        if held_controller:
            lines += ["if sys.argv[1]=='start':", '    deadline=time.monotonic()+8',
                      '    while not Path(' + repr(str(self.root / 'CONTROLLER_ASSIGNED')) + ').exists():',
                      "        if time.monotonic()>deadline: raise TimeoutError('private controller job gate')",
                      '        time.sleep(.01)']
        lines.append('result=C.main()')
        if held_controller:
            lines += ["if sys.argv[1]=='start':", '    C.save(Path(' + repr(str(self.root / 'CONTROLLER_READY.json')) + '),dict(ready=True))',
                      '    time.sleep(15)', '    C.save(Path(' + repr(str(self.root / 'CONTROLLER_NORMAL_END.json')) + '),dict(normal=True))']
        lines.append('raise SystemExit(result)')
        script.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        self.patches.enter_context(patch.object(C, '__file__', str(script)))
        self.patches.enter_context(patch.object(C, 'OUTER_SECONDS', outer_seconds))
        return script

    def test_assignment_failure_reaps_exact_child_and_preserves_failure_terminal(self):
        script = self.bootstrap("raise AssertionError('Runner must never start')\n", assignment_failure=True)
        handles = []
        release = self.root / 'UNRELATED_RELEASE'
        unrelated = subprocess.Popen([sys.executable, '-B', '-c',
            'import pathlib,sys,time\np=pathlib.Path(sys.argv[1])\nd=time.monotonic()+8\n'
            'while not p.exists():\n'
            '    if time.monotonic()>d: raise TimeoutError("private unrelated fixture")\n'
            '    time.sleep(.01)\n', str(release)], creationflags=subprocess.CREATE_NO_WINDOW)
        with (self.root / 'controller.stdout').open('xb') as out, (self.root / 'controller.stderr').open('xb') as err:
            controller = subprocess.Popen([sys.executable, '-X', 'utf8', '-B', str(script), 'start',
                '--spec', str(self.model.spec_path)], stdout=out, stderr=err,
                creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                identity = self.wait_file(self.root / 'ASSIGNMENT_CHILD.json')
                api = C.kernel()
                for pid, created in ((identity['pid'], identity['creation_time']),
                                     (identity['worker_pid'], identity['worker_creation_time'])):
                    handle = C.checked_win32(api.OpenProcess(0x1000, False, pid))
                    handles.append(handle)
                    self.assertEqual(C.creation_time(handle), created)
                    self.assertEqual(C.process_status(pid, created)['state'], 'RUNNING')
                (self.root / 'ASSIGNMENT_OBSERVED').write_text('Exact live child/worker handles retained')
                self.assertEqual(controller.wait(timeout=5), 70)
                launch = C.read(self.root / 'controller.stdout')
                self.assertEqual(launch['status'], 'TERMINAL_NOT_ACCEPTED')
                self.assertEqual(launch['launch_exit'], 70)
                terminal = self.wait_file(self.model.capture / 'TERMINAL.json', seconds=3)
                exits = [WindowsLifecycle.exit_code(self, handle, seconds=3) for handle in handles]
                self.assertTrue(all(type(code) is int and code != 259 for code in exits))
                self.assertEqual(exits[1], 70)
                self.assertEqual(terminal['child_exit'], exits[0])
                self.assertEqual(terminal['worker_exit'], exits[1])
                self.assertEqual(terminal['capture_error'], 'WORKER_EXCEPTION')
                self.assertEqual(terminal['forced_owned_processes'], 0)
                self.assertEqual(terminal['forced_unassigned_child'], 1)
                self.assertIsNone(terminal['runner_exit'])
                failure = C.read(self.model.capture / 'WORKER_FAILURE.json')
                self.assertIn('HOST_MODEL_ASSIGNMENT_FAILURE_BEFORE_ASSIGN', failure['traceback'])
                self.assertEqual(C.read(self.model.capture / 'STATE.jsonl')['phase'], 'TERMINAL')
                for name in ('JOB_READY.json', 'RUNNER_COMMAND.json', 'RUNNER_STARTED.json'):
                    self.assertFalse((self.model.capture / name).exists())
                self.assertIsNone(unrelated.poll())
                release.write_text('Normal completion of unrelated private process')
                self.assertEqual(unrelated.wait(timeout=3), 0)
            finally:
                if controller.poll() is None:
                    controller.kill();controller.wait(timeout=3)
                release.touch(exist_ok=True)
                if unrelated.poll() is None:
                    unrelated.wait(timeout=3)
                for handle in handles:
                    C.checked_win32(C.kernel().CloseHandle(handle))

    def test_worker_keeps_actual_nonzero_runner_exit(self):
        self.bootstrap("import sys,time\nprint('HOST MODEL progress',flush=True)\ntime.sleep(.3)\nsys.exit(7)\n")
        launched = C.launch(self.model.spec_path)
        self.assertNotIn(launched['status'], ('PASS', 'ACCEPTED'))
        terminal = self.wait_file(self.model.capture / 'TERMINAL.json')
        self.assertEqual(terminal['child_exit'], 7)
        self.assertEqual(terminal['runner_exit']['exit_code'], 7)
        self.assertEqual(terminal['worker_exit'], 7)
        self.assertEqual(terminal['capture_error'], '')
        self.assertEqual(terminal['forced_owned_processes'], 0)
        self.assertFalse(terminal['executor_forced_runner'])
        self.assertIn(b'HOST MODEL progress', (self.model.capture / 'business.stdout').read_bytes())

    def test_worker_timeout_is_a_failure_and_contains_runner_tree(self):
        self.bootstrap("import time\nprint('HOST MODEL waiting',flush=True)\ntime.sleep(15)\n", outer_seconds=1)
        C.launch(self.model.spec_path)
        terminal = self.wait_file(self.model.capture / 'TERMINAL.json', seconds=6)
        self.assertEqual(terminal['worker_exit'], 124)
        self.assertEqual(terminal['capture_error'], 'OUTER_2350_SECOND_DEADLINE')
        self.assertTrue(terminal['forced_owned_processes'] > 0 or terminal['executor_forced_runner'] is True)
        if terminal['runner_exit'] is not None:
            self.assertTrue(terminal['runner_exit']['outer_timeout'])
            self.assertTrue(terminal['runner_exit']['executor_forced_runner'])
        identity = C.read(self.model.capture / 'RUNNER_STARTED.json')
        self.assertNotEqual(C.process_status(identity['pid'], identity['creation_time'])['state'], 'RUNNING')

    def test_whole_worker_survives_controller_job_close_and_finishes_normally(self):
        after_close = self.root / 'WORK_AFTER_CONTROLLER_CLOSED'
        runner = ("from pathlib import Path\nimport time\n"
                  'permission=Path(' + repr(str(after_close)) + ')\n'
                  'deadline=time.monotonic()+8\nwhile not permission.exists():\n'
                  "    if time.monotonic()>deadline: raise TimeoutError('private model work permission')\n"
                  "    time.sleep(.01)\nprint('HOST MODEL completed after controller closed',flush=True)\n")
        script = self.bootstrap(runner, held_controller=True)
        with C.WindowsJob(allow_breakaway=True) as job, (self.root / 'controller.stderr').open('xb') as stderr:
            controller = subprocess.Popen([sys.executable, '-X', 'utf8', '-B', str(script), 'start',
                                           '--spec', str(self.model.spec_path)], stdout=subprocess.DEVNULL,
                                           stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                job.assign(controller)
                (self.root / 'CONTROLLER_ASSIGNED').write_text('owned controller job assigned')
                self.wait_file(self.root / 'CONTROLLER_READY.json')
                self.assertEqual(C.read(self.model.capture / 'STATE.jsonl')['phase'], 'RUNNING')
                self.assertIsNone(controller.poll())
                job.close()
                self.assertIsInstance(controller.wait(timeout=3), int)
                self.assertFalse((self.root / 'CONTROLLER_NORMAL_END.json').exists())
                after_close.write_text('private host model permission only')
                terminal = self.wait_file(self.model.capture / 'TERMINAL.json')
                self.assertEqual(terminal['worker_exit'], 0)
                self.assertEqual(terminal['child_exit'], 0)
                self.assertEqual(terminal['runner_exit']['exit_code'], 0)
                self.assertEqual(terminal['forced_owned_processes'], 0)
                self.assertFalse(terminal['executor_forced_runner'])
                self.assertEqual(terminal['capture_error'], '')
                self.assertIn('UNASSESSED', terminal['acceptance'])
            finally:
                if controller.poll() is None:
                    controller.kill();controller.wait(timeout=3)

    def test_dead_worker_without_terminal_remains_unknown(self):
        self.bootstrap("import time\nprint('HOST MODEL long wait',flush=True)\ntime.sleep(15)\n")
        C.launch(self.model.spec_path)
        started = C.read(self.model.capture / 'WORKER_STARTED.json')
        runner = self.wait_file(self.model.capture / 'RUNNER_STARTED.json')
        api = C.kernel()
        handle = C.checked_win32(api.OpenProcess(0x1001, False, started['worker_pid']))
        try:
            self.assertEqual(C.creation_time(handle), started['worker_creation_time'])
            api.TerminateProcess.argtypes, api.TerminateProcess.restype = [C.WT.HANDLE, C.WT.UINT], C.WT.BOOL
            C.checked_win32(api.TerminateProcess(handle, 23))
        finally:
            C.checked_win32(api.CloseHandle(handle))
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and C.process_status(started['worker_pid'], started['worker_creation_time'])['state'] == 'RUNNING':
            time.sleep(.01)
        observed = C.status_main(self.model.capture)
        self.assertEqual(observed['phase'], 'UNKNOWN_WORKER_LOST_WITHOUT_TERMINAL')
        self.assertFalse((self.model.capture / 'TERMINAL.json').exists())
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline and C.process_status(runner['pid'], runner['creation_time'])['state'] == 'RUNNING':
            time.sleep(.01)
        self.assertNotEqual(C.process_status(runner['pid'], runner['creation_time'])['state'], 'RUNNING')


if __name__ == '__main__':
    unittest.main(verbosity=2)
