"""Windows host capture whose finite coordinator outlives its interactive launcher.

Launch success is never integration success. The existing runner still owns all
device commands, one-shot consumption, fixed deadlines and strict close checks.
Only already-permitted job breakaway is used; there is no broker/service fallback.
"""
import argparse
import ctypes
from ctypes import wintypes as WT
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import threading
import time
import traceback

REPO = Path(__file__).resolve().parents[2]
ROOT = REPO.parents[2]
CAPTURE_ROOT = REPO.parent
BUSINESS_ROOT = ROOT / 'build/r37-native-close'
RUNNER = REPO / 'tests/ros2_target/run_endurance_roundtrip.py'
SDK = Path(r'C:\Users\17715\AppData\Local\OpenHarmony\Sdk\23\toolchains\hdc.exe')
OUTER_SECONDS, START_SECONDS, LOG_CAP = 2350, 10, 4194304
SCOPE = 'MAIN_ONE_SHOT_R38_DURABLE_ENDURANCE_CAPTURE'
SOURCE_PATHS = (
    'integrations/ros2/target/NativeObservation.hpp',
    'integrations/ros2/target/NativeObservation.cpp',
    'integrations/ros2/target/TargetBridge.cpp',
    'integrations/ros2/target/native_bridge_receipt.py',
    'tests/ros2_host/locator_observation_tests.cpp',
    'tests/ros2_host/locator_observation/CMakeLists.txt',
    'tests/ros2_target/test_native_close_receipt.py',
    'tests/ros2_target/run_endurance_roundtrip.py',
    'integrations/ros2/target/endurance_roundtrip_probe.py',
    'tests/ros2_target/endurance_roundtrip_oracle.py',
    'tests/ros2_target/test_endurance_roundtrip.py',
    'tests/ros2_target/durable_endurance_capture.py',
    'tests/ros2_target/test_durable_endurance_capture.py',
)


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read(path):
    if Path(path).name == 'STATE.jsonl':
        with Path(path).open('rb') as stream:
            data = stream.read(LOG_CAP + 1)
        require(len(data) <= LOG_CAP, 'State journal exceeds four MiB')
        end = data.rfind(b'\n')
        require(len(data) - end - 1 <= 4096, 'Incomplete state row exceeds its cap')
        if end < 0:
            return {}
        row = data[:end].rsplit(b'\n', 1)[-1]
        require(len(row) <= 4096, 'State row exceeds its cap')
        return json.loads(row)
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def utc():
    return datetime.now(timezone.utc).isoformat()


def save(path, value):
    """Immutable receipts; readers of a pending receipt must retry parsing."""
    with Path(path).open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(value, stream, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())


def state(path, value):
    """One writer appends bounded coherent rows; readers never block renames."""
    require(Path(path).name == 'STATE.jsonl' and isinstance(value, dict), 'Exact state journal required')
    row = (json.dumps(value, separators=(',', ':')) + '\n').encode('utf-8')
    require(len(row) <= (1024 if value.get('phase') == 'RUNNING' else 4096), 'State row exceeds its cap')
    with Path(path).open('ab', buffering=0) as stream:
        stream.seek(0, os.SEEK_END)
        require(stream.tell() + len(row) <= LOG_CAP, 'State journal exceeds four MiB')
        require(stream.write(row) == len(row), 'Incomplete state journal append')
        os.fsync(stream.fileno())


class BasicLimits(ctypes.Structure):
    _fields_ = [('process_time', ctypes.c_int64), ('job_time', ctypes.c_int64),
                ('flags', WT.DWORD), ('minimum_working_set', ctypes.c_size_t),
                ('maximum_working_set', ctypes.c_size_t), ('active_limit', WT.DWORD),
                ('affinity', ctypes.c_size_t), ('priority', WT.DWORD), ('scheduling', WT.DWORD)]


class IOCounts(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64) for name in
                ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class ExtendedLimits(ctypes.Structure):
    _fields_ = [('basic', BasicLimits), ('io', IOCounts), ('process_memory', ctypes.c_size_t),
                ('job_memory', ctypes.c_size_t), ('peak_process_memory', ctypes.c_size_t),
                ('peak_job_memory', ctypes.c_size_t)]


class Accounting(ctypes.Structure):
    _fields_ = [('user', ctypes.c_int64), ('kernel', ctypes.c_int64),
                ('period_user', ctypes.c_int64), ('period_kernel', ctypes.c_int64),
                ('page_faults', WT.DWORD), ('total', WT.DWORD), ('active', WT.DWORD),
                ('terminated', WT.DWORD)]


def kernel():
    require(sys.platform == 'win32' and ctypes.sizeof(ctypes.c_void_p) == 8,
            'Windows x64 capture required; no alternative launcher fallback')
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    signatures = {
        'GetCurrentProcess': ([], WT.HANDLE),
        'IsProcessInJob': ([WT.HANDLE, WT.HANDLE, ctypes.POINTER(WT.BOOL)], WT.BOOL),
        'CreateJobObjectW': ([ctypes.c_void_p, WT.LPCWSTR], WT.HANDLE),
        'OpenJobObjectW': ([WT.DWORD, WT.BOOL, WT.LPCWSTR], WT.HANDLE),
        'SetInformationJobObject': ([WT.HANDLE, ctypes.c_int, ctypes.c_void_p, WT.DWORD], WT.BOOL),
        'QueryInformationJobObject': ([WT.HANDLE, ctypes.c_int, ctypes.c_void_p, WT.DWORD,
                                      ctypes.POINTER(WT.DWORD)], WT.BOOL),
        'AssignProcessToJobObject': ([WT.HANDLE, WT.HANDLE], WT.BOOL),
        'TerminateJobObject': ([WT.HANDLE, WT.UINT], WT.BOOL),
        'CloseHandle': ([WT.HANDLE], WT.BOOL),
        'OpenProcess': ([WT.DWORD, WT.BOOL, WT.DWORD], WT.HANDLE),
        'GetProcessTimes': ([WT.HANDLE, *[ctypes.POINTER(WT.FILETIME)] * 4], WT.BOOL),
        'GetExitCodeProcess': ([WT.HANDLE, ctypes.POINTER(WT.DWORD)], WT.BOOL),
    }
    for name, (arguments, result) in signatures.items():
        function = getattr(api, name)
        function.argtypes, function.restype = arguments, result
    require(ctypes.sizeof(ExtendedLimits) == 144 and ctypes.sizeof(Accounting) == 48,
            'Unexpected Windows job structure layout')
    return api


def checked_win32(result):
    if not result:
        raise ctypes.WinError(ctypes.get_last_error())
    return result


def creation_time(handle):
    values = [WT.FILETIME() for _ in range(4)]
    checked_win32(kernel().GetProcessTimes(handle, *[ctypes.byref(value) for value in values]))
    return (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime


def job_snapshot(handle=None):
    api, inside = kernel(), WT.BOOL()
    if handle is None:
        checked_win32(api.IsProcessInJob(api.GetCurrentProcess(), None, ctypes.byref(inside)))
        if not inside.value:
            return {'in_job': False, 'flags': 0}
    info, length = ExtendedLimits(), WT.DWORD()
    checked_win32(api.QueryInformationJobObject(handle, 9, ctypes.byref(info),
                                               ctypes.sizeof(info), ctypes.byref(length)))
    flags = int(info.basic.flags)
    return {'in_job': True, 'flags': flags, 'kill_on_close': bool(flags & 0x2000),
            'breakaway_allowed': bool(flags & 0x800), 'silent_breakaway_allowed': bool(flags & 0x1000)}


def process_status(pid, expected_creation=None):
    require(type(pid) is int and pid > 0, 'Positive process identity required')
    api = kernel()
    handle = api.OpenProcess(0x1000, False, pid)
    if not handle:
        error = ctypes.get_last_error()
        return {'state': 'ABSENT' if error == 87 else 'UNKNOWN', 'winerror': error, 'pid': pid}
    try:
        created, code = creation_time(handle), WT.DWORD()
        checked_win32(api.GetExitCodeProcess(handle, ctypes.byref(code)))
        status = 'RUNNING' if code.value == 259 else 'EXITED'
        if expected_creation is not None and created != expected_creation:
            status = 'PID_REUSED'
        return {'state': status, 'pid': pid, 'creation_time': created,
                'exit_code': None if code.value == 259 else int(code.value)}
    finally:
        checked_win32(api.CloseHandle(handle))


class WindowsJob:
    """Own only newly-created Popen children; closing the owner contains their tree."""
    def __init__(self, *, allow_breakaway=False):
        self.api, self.name = kernel(), 'Local\\mdds_capture_' + secrets.token_hex(16)
        self.handle = self.api.CreateJobObjectW(None, self.name)
        checked_win32(self.handle)
        try:
            require(ctypes.get_last_error() != 183, 'Job identity already exists')
            limits = ExtendedLimits()
            limits.basic.flags = 0x2000 | (0x800 if allow_breakaway else 0)
            checked_win32(self.api.SetInformationJobObject(self.handle, 9, ctypes.byref(limits),
                                                           ctypes.sizeof(limits)))
        except BaseException:
            self.close()
            raise

    def assign(self, child):
        require(isinstance(child, subprocess.Popen) and child.poll() is None,
                'Job accepts only a live child created by this worker')
        checked_win32(self.api.AssignProcessToJobObject(self.handle, int(child._handle)))

    def active(self):
        info, length = Accounting(), WT.DWORD()
        checked_win32(self.api.QueryInformationJobObject(self.handle, 1, ctypes.byref(info),
                                                       ctypes.sizeof(info), ctypes.byref(length)))
        return int(info.active)

    def terminate(self, code):
        checked_win32(self.api.TerminateJobObject(self.handle, code))

    def close(self):
        if self.handle:
            handle, self.handle = self.handle, None
            checked_win32(self.api.CloseHandle(handle))

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


def detached_spawn(argv, cwd, stdout, stderr):
    inherited = job_snapshot()
    require(not inherited['in_job'] or inherited.get('breakaway_allowed') or
            inherited.get('silent_breakaway_allowed'), 'Inherited job does not permit breakaway')
    return subprocess.Popen(argv, cwd=cwd, stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr,
                            close_fds=True, creationflags=(subprocess.DETACHED_PROCESS |
                                subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.CREATE_BREAKAWAY_FROM_JOB))


def direct_path(value, parent, pattern, *, exists):
    raw = Path(value).absolute()
    resolved = raw.resolve(strict=exists)
    require(resolved.parent == Path(parent).resolve() and re.fullmatch(pattern, resolved.name),
            'Path outside the exact stage namespace')
    require(not raw.is_symlink() and not (hasattr(raw, 'is_junction') and raw.is_junction()),
            'Reparse-point path is not admitted')
    return resolved


def checked_spec(path):
    path = direct_path(path, CAPTURE_ROOT, r'CAPTURE_SPEC_[A-Z0-9_]+\.json', exists=True)
    value = read(path)
    require(set(value) == {'schema', 'scope', 'run', 'prepared', 'prepared_sha256', 'release',
            'release_sha256', 'output', 'sdk', 'sdk_sha256', 'source_sha256', 'capture_state'},
            'Exact Main capture specification required')
    require(type(value['schema']) is int and value['schema'] == 1 and value['scope'] == SCOPE and
            re.fullmatch('[0-9a-f]{16}', value.get('run', '')), 'Capture identity/scope rejected')
    prepared = Path(value['prepared']).resolve(strict=True)
    require(prepared.name == 'PLAN.json', 'Prepared plan required')
    direct_path(prepared.parent, BUSINESS_ROOT, 'prepared-[0-9a-f]{16}', exists=True)
    release = direct_path(value['release'], BUSINESS_ROOT, r'MAIN_BUSINESS_RELEASE_[A-Z0-9_]+\.json', exists=True)
    output = direct_path(value['output'], BUSINESS_ROOT, 'execution-[0-9a-f]{16}', exists=False)
    capture = direct_path(value['capture_state'], CAPTURE_ROOT, 'capture-[0-9a-f]{16}', exists=False)
    require(not output.exists(), 'Business output already exists')
    require(sha(prepared) == value['prepared_sha256'] and sha(release) == value['release_sha256'],
            'Prepared/release drift')
    require(Path(value['sdk']).resolve(strict=True) == SDK.resolve(strict=True) and
            sha(SDK) == value['sdk_sha256'], 'SDK23 selection changed')
    sources = {name: sha(REPO / name) for name in SOURCE_PATHS}
    require(len(sources) == 13 and value['source_sha256'] == sources, 'Exact thirteen capture/source bindings required')
    plan, permit = read(prepared), read(release)
    require(plan.get('scope') == 'R37_NATIVE_CLOSE_ENDURANCE_PREPARED_NOT_DISPATCHED' and
            plan.get('run') == value['run'] and plan.get('case') == 'endurance_1801_ABA_30MIN',
            'Original finite native-close case required')
    require(all(plan['bindings'].get(str((REPO / name).resolve())) == digest for name, digest in sources.items()),
            'Capture validation inputs missing from prepared bindings')
    require(set(permit) == {'schema', 'scope', 'run', 'prepared_sha256', 'runner_sha256'} and
            type(permit['schema']) is int and permit['schema'] == 1 and
            permit['scope'] == 'MAIN_ONE_SHOT_R37_NATIVE_CLOSE_ENDURANCE_RELEASE' and
            permit['run'] == value['run'] and permit['prepared_sha256'] == sha(prepared) and
            permit['runner_sha256'] == sha(RUNNER), 'Exact one-shot Main business permit required')
    require(not (prepared.parent / (value['run'] + '.CONSUMED.json')).exists(), 'Release already consumed')
    observed = datetime.fromisoformat(read(plan['routes'])['observed_utc'])
    require(observed.utcoffset() is not None and
            0 <= (datetime.now(timezone.utc) - observed).total_seconds() < 240, 'Original 240-second route freshness required')
    return path, value, capture


def checked_directory(path):
    directory = direct_path(path, CAPTURE_ROOT, 'capture-[0-9a-f]{16}', exists=True)
    intent = read(directory / 'INTENT.json')
    require(intent['scope'] == 'R38_CAPTURE_LAUNCH_INTENT', 'Capture launch intent required')
    require(set(intent) == {'scope', 'utc', 'run', 'spec', 'spec_sha256', 'launcher_pid',
            'started_monotonic', 'start_deadline', 'outer_deadline', 'outer_seconds'}, 'Exact launch intent required')
    require(all(type(intent[k]) in (int, float) and math.isfinite(intent[k]) for k in
            ('started_monotonic', 'start_deadline', 'outer_deadline')) and
            type(intent['outer_seconds']) is int and intent['outer_seconds'] == OUTER_SECONDS and
            intent['start_deadline'] == intent['started_monotonic'] + START_SECONDS and
            intent['outer_deadline'] == intent['started_monotonic'] + OUTER_SECONDS and
            intent['started_monotonic'] <= time.monotonic(), 'Original fixed capture deadlines required')
    spec_path, spec, expected = checked_spec(intent['spec'])
    require(expected == directory and sha(spec_path) == intent['spec_sha256'] and spec['run'] == intent['run'],
            'Capture launch identity drift')
    return directory, intent, spec


def launch(spec_path):
    spec_path, spec, directory = checked_spec(spec_path)
    directory.mkdir()
    tick = time.monotonic()
    intent = {'scope': 'R38_CAPTURE_LAUNCH_INTENT', 'utc': utc(), 'run': spec['run'],
              'spec': str(spec_path), 'spec_sha256': sha(spec_path), 'launcher_pid': os.getpid(),
              'started_monotonic': tick, 'start_deadline': tick + START_SECONDS,
              'outer_deadline': tick + OUTER_SECONDS, 'outer_seconds': OUTER_SECONDS}
    save(directory / 'INTENT.json', intent)
    argv = [sys.executable, '-X', 'utf8', '-B', str(Path(__file__).resolve()), '_worker', '--state', str(directory)]
    try:
        with (directory / 'worker.stdout').open('xb') as out, (directory / 'worker.stderr').open('xb') as err:
            worker = detached_spawn(argv, REPO, out, err)
        created = creation_time(int(worker._handle))
        save(directory / 'LAUNCH.json', {'utc': utc(), 'argv': argv, 'pid': worker.pid,
             'creation_time': created, 'spec_sha256': intent['spec_sha256'], 'launch_is_acceptance': False})
        while time.monotonic() < intent['start_deadline']:
            status = read(directory / 'STATE.jsonl') if (directory / 'STATE.jsonl').exists() else {}
            if status.get('phase') == 'RUNNING' and status.get('worker_pid') == worker.pid:
                return {'status': 'LAUNCHED_NOT_ACCEPTED', 'state': str(directory), 'worker_pid': worker.pid,
                        'worker_creation_time': created, 'run': spec['run']}
            if status.get('phase') == 'TERMINAL' and status.get('worker_pid') == worker.pid:
                return {'status': 'TERMINAL_NOT_ACCEPTED', 'state': str(directory), 'worker_pid': worker.pid,
                        'worker_creation_time': created, 'run': spec['run'], 'launch_exit': status['worker_exit']}
            if worker.poll() is not None:
                raise RuntimeError('Worker exited before readiness: ' + str(worker.returncode))
            time.sleep(0.05)
        raise TimeoutError('Capture startup deadline; worker must reject a late start')
    except BaseException:
        save(directory / 'LAUNCH_FAILURE.json', {'utc': utc(), 'traceback': traceback.format_exc(),
                                               'automatic_retry': False})
        raise


def capture_stream(source, destination, result, overflow):
    try:
        with destination.open('xb') as target:
            written = 0
            while True:
                chunk = source.read1(65536)
                if not chunk:
                    break
                allowed = max(0, LOG_CAP - written)
                target.write(chunk[:allowed])
                target.flush()
                written += min(len(chunk), allowed)
                if len(chunk) > allowed:
                    overflow.set()
            result.update(bytes=written, sha256=sha(destination), complete=not overflow.is_set())
    except BaseException:
        result['error'] = traceback.format_exc()
        overflow.set()
    finally:
        source.close()


def worker_main(path):
    directory, intent, spec = checked_directory(path)
    require(time.monotonic() < intent['start_deadline'], 'Late worker start rejected before SDK use')
    inherited = job_snapshot()
    require(not inherited.get('kill_on_close'), 'Worker still belongs to the launcher kill-on-close job')
    own_time = creation_time(kernel().GetCurrentProcess())
    owner = {'utc': utc(), 'worker_pid': os.getpid(), 'worker_creation_time': own_time,
             'run': spec['run'], 'spec_sha256': intent['spec_sha256'],
             'outer_deadline': intent['outer_deadline'], 'inherited_job': inherited}
    save(directory / 'WORKER_STARTED.json', owner)
    argv = [sys.executable, '-X', 'utf8', '-B', str(Path(__file__).resolve()), '_execute', '--state', str(directory)]
    child, streams, threads, overflow = None, {}, [], threading.Event()
    reason, forced, forced_unassigned, child_assigned = '', 0, 0, False
    with WindowsJob() as job:
        try:
            child = subprocess.Popen(argv, cwd=REPO, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, close_fds=True,
                                     creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
            job.assign(child)
            child_assigned = True
            save(directory / 'JOB_READY.json', {'job_name': job.name, 'child_pid': child.pid,
                 'child_creation_time': creation_time(int(child._handle)), 'worker_pid': os.getpid(),
                 'spec_sha256': intent['spec_sha256'], 'run': spec['run'], 'job': job_snapshot(job.handle)})
            for name in ('stdout', 'stderr'):
                streams[name] = {}
                thread = threading.Thread(target=capture_stream,
                    args=(getattr(child, name), directory / ('business.' + name), streams[name], overflow), daemon=True)
                threads.append(thread)
                thread.start()
            state(directory / 'STATE.jsonl', dict(owner, phase='RUNNING', child_pid=child.pid, utc=utc()))
            next_heartbeat = time.monotonic() + 1
            while child.poll() is None:
                if time.monotonic() >= intent['outer_deadline']:
                    reason = 'OUTER_2350_SECOND_DEADLINE'
                    break
                if overflow.is_set():
                    reason = 'CAPTURE_STREAM_CAP_OR_IO_ERROR'
                    break
                if time.monotonic() >= next_heartbeat:
                    state(directory / 'STATE.jsonl', dict(owner, phase='RUNNING', child_pid=child.pid, utc=utc()))
                    next_heartbeat = time.monotonic() + 1
                time.sleep(0.2)
            if child.poll() is None:
                forced = job.active()
                job.terminate(124 if reason == 'OUTER_2350_SECOND_DEADLINE' else 70)
            child.wait(timeout=2)
            for thread in threads:
                thread.join(timeout=2)
            if any(thread.is_alive() for thread in threads):
                reason = reason or 'CAPTURE_READER_NOT_REAPED'
            if overflow.is_set():
                reason = reason or 'CAPTURE_STREAM_CAP_OR_IO_ERROR'
            active = job.active()
            if active:
                forced += active
                reason = reason or 'OWNED_DESCENDANTS_REMAIN'
                job.terminate(70)
        except BaseException:
            reason = reason or 'WORKER_EXCEPTION'
            save(directory / 'WORKER_FAILURE.json', {'utc': utc(), 'traceback': traceback.format_exc()})
            active = job.active()
            if active:
                forced += active
                job.terminate(70)
            if child is not None:
                # Assignment can fail before this exact Popen child belongs to our job.
                # The startup barrier prevents SDK use, but does not reap that child.
                if not child_assigned and child.poll() is None:
                    forced_unassigned = 1
                    child.kill()
                child.wait(timeout=2)
                if not child_assigned:
                    child.stdout.close()
                    child.stderr.close()
            for thread in threads:
                thread.join(timeout=2)
    exit_row = read(directory / 'RUNNER_EXIT.json') if (directory / 'RUNNER_EXIT.json').exists() else None
    child_code = None if child is None else child.returncode
    expected_child = None if exit_row is None else (124 if exit_row.get('outer_timeout') else exit_row.get('exit_code'))
    if exit_row is not None and exit_row.get('outer_timeout'):
        reason = reason or 'OUTER_2350_SECOND_DEADLINE'
    if exit_row is None or expected_child != child_code:
        reason = reason or 'ACTUAL_RUNNER_EXIT_NOT_CAPTURED_OR_MISMATCHED'
    code = (124 if reason == 'OUTER_2350_SECOND_DEADLINE' else 70) if reason else child_code
    terminal = dict(owner, phase='TERMINAL', utc=utc(), capture_error=reason, child_exit=child_code,
                    runner_exit=exit_row, worker_exit=code, forced_owned_processes=forced,
                    forced_unassigned_child=forced_unassigned,
                    executor_forced_runner=None if exit_row is None else exit_row['executor_forced_runner'],
                    state_journal='append-jsonl/last-complete/heartbeat-at-most-1Hz/4MiB',
                    streams=streams, original_inner_limits='1801/1Hz/minspan1800/business1900/probe1910/absoluteclose1950/exit20/4MiB',
                    acceptance='UNASSESSED_READ_ORIGINAL_RUNNER_RESULT_AND_STRICT_LOGS')
    save(directory / 'TERMINAL.json', terminal)
    state(directory / 'STATE.jsonl', terminal)
    return code


def execute_main(path):
    directory, intent, spec = checked_directory(path)
    require(time.monotonic() < intent['start_deadline'], 'Late child bootstrap rejected before SDK use')
    ready = None
    while time.monotonic() < intent['start_deadline']:
        try:
            ready = read(directory / 'JOB_READY.json')
            break
        except (FileNotFoundError, json.JSONDecodeError):
            time.sleep(0.02)
    require(ready is not None and ready['child_pid'] == os.getpid() and
            ready['child_creation_time'] == creation_time(kernel().GetCurrentProcess()) and
            re.fullmatch(r'Local\\mdds_capture_[0-9a-f]{32}', ready['job_name']) and
            ready['spec_sha256'] == intent['spec_sha256'] and ready['run'] == spec['run'],
            'Own job assignment barrier missing or invalid')
    api, member = kernel(), WT.BOOL()
    handle = checked_win32(api.OpenJobObjectW(4, False, ready['job_name']))
    try:
        checked_win32(api.IsProcessInJob(api.GetCurrentProcess(), handle, ctypes.byref(member)))
        require(member.value and job_snapshot(handle)['flags'] == 0x2000,
                'Child is not contained in its exact worker job')
    finally:
        checked_win32(api.CloseHandle(handle))
    owner = read(directory / 'WORKER_STARTED.json')
    require(process_status(owner['worker_pid'], owner['worker_creation_time'])['state'] == 'RUNNING',
            'Worker identity no longer live')
    argv = [sys.executable, '-X', 'utf8', '-B', str(RUNNER), 'run', '--prepared', spec['prepared'],
            '--release', spec['release'], '--hdc', spec['sdk'], '--output', spec['output']]
    temporary = directory / 'tmp'
    temporary.mkdir()
    overrides = {'PYTHONDONTWRITEBYTECODE': '1', 'PYTHONUTF8': '1', 'GIT_OPTIONAL_LOCKS': '0',
                 'TMP': str(temporary), 'TEMP': str(temporary), 'TMPDIR': str(temporary)}
    save(directory / 'RUNNER_COMMAND.json', {'argv': argv, 'cwd': str(REPO), 'env_overrides': overrides,
         'utc': utc(), 'prepared_sha256': spec['prepared_sha256'], 'release_sha256': spec['release_sha256'],
         'source_sha256': spec['source_sha256'], 'absolute_outer_deadline': intent['outer_deadline']})
    remaining = intent['outer_deadline'] - time.monotonic()
    require(0 < remaining <= OUTER_SECONDS, 'Capture outer deadline already exhausted')
    child = subprocess.Popen(argv, cwd=REPO, env=dict(os.environ, **overrides), stdin=subprocess.DEVNULL)
    save(directory / 'RUNNER_STARTED.json', {'pid': child.pid, 'creation_time': creation_time(int(child._handle)), 'utc': utc()})
    timed_out = False
    try:
        code = child.wait(timeout=remaining)
    except subprocess.TimeoutExpired:
        timed_out = True
        child.kill()
        code = child.wait(timeout=2)
    save(directory / 'RUNNER_EXIT.json', {'exit_code': code, 'outer_timeout': timed_out,
                                       'executor_forced_runner': timed_out, 'utc': utc()})
    return 124 if timed_out else code


def status_main(path):
    directory = direct_path(path, CAPTURE_ROOT, 'capture-[0-9a-f]{16}', exists=True)
    value = read(directory / 'STATE.jsonl') if (directory / 'STATE.jsonl').exists() else {'phase': 'START_PENDING'}
    if not value:
        value = {'phase': 'START_PENDING'}
    if 'worker_pid' not in value and (directory / 'LAUNCH.json').exists():
        launch_row = read(directory / 'LAUNCH.json')
        value.update(worker_pid=launch_row['pid'], worker_creation_time=launch_row['creation_time'])
    if 'worker_pid' in value:
        value['observed_worker'] = process_status(value['worker_pid'], value['worker_creation_time'])
        if value['phase'] != 'TERMINAL' and value['observed_worker']['state'] != 'RUNNING':
            value['phase'] = 'UNKNOWN_WORKER_LOST_WITHOUT_TERMINAL'
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['start', 'status', '_worker', '_execute'])
    parser.add_argument('--spec', type=Path)
    parser.add_argument('--state', type=Path)
    args = parser.parse_args()
    if args.operation == 'start':
        require(args.spec is not None and args.state is None, 'start requires only --spec')
        value = launch(args.spec)
        print(json.dumps(value), flush=True)
        return value.get('launch_exit', 0)
    require(args.state is not None and args.spec is None, 'state operation requires only --state')
    if args.operation == 'status':
        print(json.dumps(status_main(args.state)), flush=True)
        return 0
    return worker_main(args.state) if args.operation == '_worker' else execute_main(args.state)


if __name__ == '__main__':
    raise SystemExit(main())
