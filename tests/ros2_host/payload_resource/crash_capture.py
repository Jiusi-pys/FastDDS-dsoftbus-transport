"""Per-process exception capture for the payload-resource scenes (Windows only).

Starts exactly one child process under the Win32 debug API. No global debugger registration, no
registry, no WER setting. On the first fatal-looking exception it records the code, the
FAST_FAIL subcode (ExceptionInformation[0] for 0xC0000409), the faulting thread/address, the loaded
module map, and writes a local minidump. Dumps may contain test key material: keep them in the
isolated build directory, never in Git. The exception is then passed on unchanged
(DBG_EXCEPTION_NOT_HANDLED); this module never swallows, retries or alters the child's behaviour.
"""
import ctypes
import json
import os
import threading
from ctypes import wintypes as w

K = ctypes.WinDLL('kernel32', use_last_error=True)
D = ctypes.WinDLL('dbghelp')

DEBUG_ONLY_THIS_PROCESS = 0x2
CREATE_UNICODE_ENVIRONMENT = 0x400
STARTF_USESTDHANDLES = 0x100
DBG_CONTINUE = 0x10002
DBG_EXCEPTION_NOT_HANDLED = 0x80010001
EXCEPTION, CREATE_THREAD, CREATE_PROCESS, EXIT_THREAD, EXIT_PROCESS, LOAD_DLL, UNLOAD_DLL = 1, 2, 3, 4, 5, 6, 7
IGNORED = {0xE06D7363, 0x406D1388, 0x80000003, 0x4000001F, 0x80000004, 0x40010006, 0x4001000A}
FATAL = {0xC0000409, 0xC0000005, 0xC000001D, 0xC0000374, 0xC00000FD, 0xC0000094, 0xC0000096, 0xC0000420}


class EXCEPTION_RECORD(ctypes.Structure):
    pass


EXCEPTION_RECORD._fields_ = [('code', w.DWORD), ('flags', w.DWORD), ('chain', ctypes.c_void_p), ('address', ctypes.c_void_p),
                             ('nparams', w.DWORD), ('info', ctypes.c_uint64 * 15)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [('code', w.DWORD), ('pid', w.DWORD), ('tid', w.DWORD), ('_pad', w.DWORD), ('u', ctypes.c_ubyte * 176)]


class EXC_INFO(ctypes.Structure):
    _fields_ = [('record', EXCEPTION_RECORD), ('first_chance', w.DWORD)]


class CREATE_PROCESS_INFO(ctypes.Structure):
    _fields_ = [('file', ctypes.c_void_p), ('process', ctypes.c_void_p), ('thread', ctypes.c_void_p), ('base', ctypes.c_void_p),
                ('off', w.DWORD), ('size', w.DWORD), ('tls', ctypes.c_void_p), ('start', ctypes.c_void_p),
                ('image', ctypes.c_void_p), ('unicode', w.WORD)]


class CREATE_THREAD_INFO(ctypes.Structure):
    _fields_ = [('thread', ctypes.c_void_p), ('tls', ctypes.c_void_p), ('start', ctypes.c_void_p)]


class LOAD_DLL_INFO(ctypes.Structure):
    _fields_ = [('file', ctypes.c_void_p), ('base', ctypes.c_void_p), ('off', w.DWORD), ('size', w.DWORD),
                ('image', ctypes.c_void_p), ('unicode', w.WORD)]


class EXIT_PROCESS_INFO(ctypes.Structure):
    _fields_ = [('exit_code', w.DWORD)]


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR), ('x', w.DWORD), ('y', w.DWORD),
                ('xs', w.DWORD), ('ys', w.DWORD), ('xc', w.DWORD), ('yc', w.DWORD), ('fill', w.DWORD), ('flags', w.DWORD),
                ('show', w.WORD), ('res2', w.WORD), ('res3', ctypes.c_void_p), ('stdin', ctypes.c_void_p),
                ('stdout', ctypes.c_void_p), ('stderr', ctypes.c_void_p)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('process', ctypes.c_void_p), ('thread', ctypes.c_void_p), ('pid', w.DWORD), ('tid', w.DWORD)]


class EXCEPTION_POINTERS(ctypes.Structure):
    _fields_ = [('record', ctypes.c_void_p), ('context', ctypes.c_void_p)]


class MINIDUMP_EXCEPTION_INFORMATION(ctypes.Structure):
    _pack_ = 4
    _fields_ = [('tid', w.DWORD), ('pointers', ctypes.c_void_p), ('client', w.BOOL)]


K.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, w.BOOL, w.DWORD, ctypes.c_void_p, w.LPCWSTR,
                             ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION)]
K.CreateProcessW.restype = w.BOOL
K.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), w.DWORD]
K.WaitForDebugEvent.restype = w.BOOL
K.ContinueDebugEvent.argtypes = [w.DWORD, w.DWORD, w.DWORD]
K.GetThreadContext.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
K.CloseHandle.argtypes = [ctypes.c_void_p]
K.SetHandleInformation.argtypes = [ctypes.c_void_p, w.DWORD, w.DWORD]
K.TerminateProcess.argtypes = [ctypes.c_void_p, w.UINT]
K.GetFinalPathNameByHandleW.argtypes = [ctypes.c_void_p, w.LPWSTR, w.DWORD, w.DWORD]
K.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, ctypes.c_void_p, w.DWORD, w.DWORD, ctypes.c_void_p]
K.CreateFileW.restype = ctypes.c_void_p
D.MiniDumpWriteDump.argtypes = [ctypes.c_void_p, w.DWORD, ctypes.c_void_p, w.DWORD, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
D.MiniDumpWriteDump.restype = w.BOOL
# DataSegs | ProcessThreadData | ThreadInfo | UnloadedModules | WithoutOptionalData off: stacks of every thread are kept.
DUMP_FLAGS = 0x1 | 0x100 | 0x1000 | 0x20


def _path(handle):
    if not handle:
        return ''
    buf = ctypes.create_unicode_buffer(1024)
    n = K.GetFinalPathNameByHandleW(handle, buf, 1024, 0)
    return buf.value if 0 < n < 1024 else ''


class Captured:
    """Popen-like: wait(timeout) -> exit code. `report` holds the capture facts."""

    def __init__(self, argv, env, stdout, stderr, dump_dir, max_exceptions=64):
        self.report = dict(kind='PER_PROCESS_DEBUG_CAPTURE', argv=list(argv), first_fatal=None, notable_exceptions=[],
                           modules=[], dump=None, exit_code=None, capture_complete=True, notes=[])
        self._dump_dir = dump_dir
        os.makedirs(dump_dir, exist_ok=True)
        self._max = max_exceptions
        self._done = threading.Event()
        self._process = None
        self._tid_thread = {}
        self._pid = None
        self._error = None
        self._started = threading.Event()
        self._thread = threading.Thread(target=self._run, args=(argv, env, stdout, stderr), daemon=True)
        self._thread.start()
        self._started.wait(30)
        if self._error:
            raise OSError(self._error)

    def _run(self, argv, env, stdout, stderr):
        try:
            import msvcrt
            for stream in (stdout, stderr):
                K.SetHandleInformation(msvcrt.get_osfhandle(stream.fileno()), 1, 1)
            nul = K.CreateFileW(os.devnull, 0x80000000, 3, None, 3, 0x80, None)
            si = STARTUPINFOW()
            si.cb = ctypes.sizeof(si)
            si.flags = STARTF_USESTDHANDLES
            si.stdin, si.stdout, si.stderr = nul, msvcrt.get_osfhandle(stdout.fileno()), msvcrt.get_osfhandle(stderr.fileno())
            block = ''.join('%s=%s\0' % (k, v) for k, v in env.items()) + '\0'
            envbuf = ctypes.create_unicode_buffer(block, len(block))
            pi = PROCESS_INFORMATION()
            cmd = ctypes.create_unicode_buffer(' '.join('"%s"' % a if ' ' in a else a for a in argv))
            if not K.CreateProcessW(None, cmd, None, None, True, DEBUG_ONLY_THIS_PROCESS | CREATE_UNICODE_ENVIRONMENT,
                                    ctypes.cast(envbuf, ctypes.c_void_p), None, ctypes.byref(si), ctypes.byref(pi)):
                self._error = 'CreateProcessW failed %d' % ctypes.get_last_error()
                self._started.set()
                return
            self._process, self._pid = pi.process, pi.pid
            self._started.set()
            self._loop()
        except Exception as error:  # capture failure is never silent
            self.report['capture_complete'] = False
            self.report['notes'].append('capture driver exception: %r' % (error,))
            self._started.set()
        finally:
            self._done.set()

    def _loop(self):
        event = DEBUG_EVENT()
        notable = 0
        while K.WaitForDebugEvent(ctypes.byref(event), 0xFFFFFFFF):
            status = DBG_CONTINUE
            code = event.code
            if code == CREATE_PROCESS:
                info = CREATE_PROCESS_INFO.from_buffer_copy(bytes(event.u[:ctypes.sizeof(CREATE_PROCESS_INFO)]))
                self._tid_thread[event.tid] = info.thread
                self.report['modules'].append(dict(path=_path(info.file), base=hex(info.base or 0)))
                if info.file:
                    K.CloseHandle(info.file)
            elif code == CREATE_THREAD:
                info = CREATE_THREAD_INFO.from_buffer_copy(bytes(event.u[:ctypes.sizeof(CREATE_THREAD_INFO)]))
                self._tid_thread[event.tid] = info.thread
            elif code == LOAD_DLL:
                info = LOAD_DLL_INFO.from_buffer_copy(bytes(event.u[:ctypes.sizeof(LOAD_DLL_INFO)]))
                if len(self.report['modules']) < 400:
                    self.report['modules'].append(dict(path=_path(info.file), base=hex(info.base or 0)))
                if info.file:
                    K.CloseHandle(info.file)
            elif code == EXCEPTION:
                info = EXC_INFO.from_buffer_copy(bytes(event.u[:ctypes.sizeof(EXC_INFO)]))
                rec = info.record
                if rec.code == 0xC0000005 and info.first_chance:
                    # ASan commits shadow memory through first-chance access violations; counted, never listed or dumped.
                    self.report['benign_first_chance_access_violations'] = self.report.get('benign_first_chance_access_violations', 0) + 1
                    status = DBG_EXCEPTION_NOT_HANDLED
                elif rec.code not in IGNORED:
                    status = DBG_EXCEPTION_NOT_HANDLED
                    row = dict(code=hex(rec.code), first_chance=bool(info.first_chance), address=hex(rec.address or 0), tid=event.tid,
                               params=[hex(rec.info[i]) for i in range(min(rec.nparams, 15))])
                    if notable < self._max:
                        self.report['notable_exceptions'].append(row)
                        notable += 1
                    else:
                        self.report['capture_complete'] = False
                        self.report['notes'].append('CAPTURE_INCOMPLETE: notable exception list capped')
                    if (rec.code == 0xC0000409 or (rec.code in FATAL and not info.first_chance)) and self.report['first_fatal'] is None:
                        row['fast_fail_subcode'] = rec.info[0] if rec.code == 0xC0000409 and rec.nparams else None
                        self.report['first_fatal'] = row
                        self._dump(event.tid, rec)
                else:
                    status = DBG_EXCEPTION_NOT_HANDLED if rec.code == 0xE06D7363 else DBG_CONTINUE
            elif code == EXIT_PROCESS:
                info = EXIT_PROCESS_INFO.from_buffer_copy(bytes(event.u[:4]))
                self.report['exit_code'] = info.exit_code
                K.ContinueDebugEvent(event.pid, event.tid, DBG_CONTINUE)
                return
            K.ContinueDebugEvent(event.pid, event.tid, status)
        self.report['capture_complete'] = False
        self.report['notes'].append('WaitForDebugEvent failed %d' % ctypes.get_last_error())

    def _dump(self, tid, rec):
        try:
            thread = self._tid_thread.get(tid)
            raw = ctypes.create_string_buffer(1232 + 64)
            addr = (ctypes.addressof(raw) + 15) & ~15
            ctypes.c_uint32.from_address(addr + 0x30).value = 0x10001F  # CONTEXT_ALL
            if not K.GetThreadContext(thread, ctypes.c_void_p(addr)):
                self.report['notes'].append('GetThreadContext failed %d' % ctypes.get_last_error())
            local = EXCEPTION_RECORD.from_buffer_copy(rec)
            pointers = EXCEPTION_POINTERS(ctypes.cast(ctypes.pointer(local), ctypes.c_void_p), ctypes.c_void_p(addr))
            exc = MINIDUMP_EXCEPTION_INFORMATION(tid, ctypes.cast(ctypes.pointer(pointers), ctypes.c_void_p), False)
            path = os.path.join(self._dump_dir, 'first-fatal-%d.dmp' % self._pid)
            handle = K.CreateFileW(path, 0x40000000, 0, None, 2, 0x80, None)
            ok = D.MiniDumpWriteDump(self._process, self._pid, handle, DUMP_FLAGS, ctypes.byref(exc), None, None)
            K.CloseHandle(handle)
            self.report['dump'] = dict(path=path, ok=bool(ok), bytes=os.path.getsize(path), flags=hex(DUMP_FLAGS))
            self.report['first_fatal']['rip'] = hex(ctypes.c_uint64.from_address(addr + 0xF8).value)
            self.report['first_fatal']['rsp'] = hex(ctypes.c_uint64.from_address(addr + 0x98).value)
        except Exception as error:
            self.report['capture_complete'] = False
            self.report['notes'].append('dump failed: %r' % (error,))

    # Popen-compatible surface used by run_scenes.py
    @property
    def pid(self):
        return self._pid

    def wait(self, timeout=None):
        if not self._done.wait(timeout):
            import subprocess
            raise subprocess.TimeoutExpired('captured-process', timeout)
        return self.report['exit_code']

    def kill(self):
        if self._process:
            K.TerminateProcess(self._process, 1)

    def write_report(self, path):
        with open(path, 'w') as f:
            json.dump(self.report, f, indent=2)
