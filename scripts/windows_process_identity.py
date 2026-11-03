"""Read-only, exact-source Windows process identity binding.

CIM date displays are auxiliary descriptions, never native execution identity.
Records validate data; they confer no authority to open or control a process.
Importing this module loads no Win32 DLL and performs no process queries.
"""
import ctypes as C
from ctypes import wintypes as W
from dataclasses import dataclass
import ntpath
import os
import re

SOURCE = 'win32.GetProcessTimes.lpCreationTime'
FORMAT = 'WIN32_PROCESS_IDENTITY_V1'
EPOCH = '1601-01-01T00:00:00Z'


class IdentityBindingError(Exception):
    def __init__(self, code, winerror=None):
        self.code = code
        self.winerror = winerror
        super().__init__(code)


def require(condition, code):
    if not condition:
        raise IdentityBindingError(code)


def combine_filetime(high, low):
    require(type(high) is int and type(low) is int and
            0 <= high <= 0xffffffff and 0 <= low <= 0xffffffff, 'FILETIME_WORDS')
    return (high << 32) | low


def normalized_local_path(path):
    require(type(path) is str and '\x00' not in path and
            re.match(r'^[A-Za-z]:[\\/]', path) is not None, 'LOCAL_ABSOLUTE_IMAGE')
    return ntpath.normcase(ntpath.normpath(path))


@dataclass(frozen=True)
class NativeProcessIdentity:
    pid: int
    birth_100ns: int
    image: str
    domain: str
    user: str
    session: int

    def __post_init__(self):
        require(type(self.pid) is int and 0 < self.pid <= 0xffffffff, 'PID_TYPE')
        require(type(self.birth_100ns) is int and
                0 < self.birth_100ns <= 0xffffffffffffffff, 'NATIVE_BIRTH_INTEGER')
        require(self.image == normalized_local_path(self.image), 'NORMALIZED_IMAGE')
        require(all(type(v) is str and v and not any(ord(c) < 32 for c in v)
                    for v in (self.domain, self.user)), 'OWNER_KNOWN')
        require(type(self.session) is int and 0 <= self.session <= 0xffffffff,
                'SESSION_KNOWN')

    def to_record(self):
        # Decimal text avoids IEEE-754 loss in intermediaries. Never float/datetime.
        return dict(format=FORMAT, source=SOURCE, unit='100ns', resolution_100ns=1,
                    epoch=EPOCH, pid=self.pid, birth_100ns=str(self.birth_100ns),
                    image=self.image, owner=dict(domain=self.domain, user=self.user),
                    session=self.session)

    @classmethod
    def from_record(cls, record):
        keys = {'format', 'source', 'unit', 'resolution_100ns', 'epoch', 'pid',
                'birth_100ns', 'image', 'owner', 'session'}
        require(type(record) is dict and set(record) == keys, 'IDENTITY_RECORD_FIELDS')
        require(record['format'] == FORMAT and record['source'] == SOURCE and
                record['unit'] == '100ns' and record['epoch'] == EPOCH and
                type(record['resolution_100ns']) is int and
                record['resolution_100ns'] == 1, 'NATIVE_SOURCE_REQUIRED')
        birth = record['birth_100ns']
        require(type(birth) is str and re.fullmatch(r'[1-9][0-9]{0,19}', birth) is not None,
                'LOSSLESS_DECIMAL_BIRTH_REQUIRED')
        owner = record['owner']
        require(type(owner) is dict and set(owner) == {'domain', 'user'}, 'OWNER_FIELDS')
        return cls(record['pid'], int(birth), record['image'], owner['domain'],
                   owner['user'], record['session'])


def capture_native_identity(handle, metadata):
    """All fields from one caller-retained handle, with native birth read twice."""
    require(type(handle) is int and handle > 0, 'LIVE_HANDLE_REQUIRED')
    require(getattr(metadata, 'birth_source', None) == SOURCE and
            type(getattr(metadata, 'resolution_100ns', None)) is int and
            metadata.resolution_100ns == 1, 'NATIVE_METADATA_SOURCE_REQUIRED')
    pid, birth = metadata.pid_birth(handle)
    image = metadata.image(handle)
    domain, user, session = metadata.owner_session(handle)
    require((pid, birth) == metadata.pid_birth(handle), 'HANDLE_IDENTITY_CHANGED')
    return NativeProcessIdentity(pid, birth, image, domain, user, session)


def verify_bound_handle(handle, record, metadata):
    expected = NativeProcessIdentity.from_record(record)  # before any API call
    observed = capture_native_identity(handle, metadata)
    require(observed == expected, 'NATIVE_IDENTITY_MISMATCH')
    return observed


def expected_for_stop_core(record):
    """Strict input adapter; callers still need separate approval/once/handle gates."""
    value = NativeProcessIdentity.from_record(record)
    return dict(pid=value.pid, birth=value.birth_100ns, image=value.image,
                domain=value.domain, user=value.user, session=value.session)


class _FILETIME(C.Structure):
    _fields_ = [('low', W.DWORD), ('high', W.DWORD)]


class Win32ProcessMetadata:
    """Queries only a supplied handle. No OpenProcess, termination, or PID lookup."""
    birth_source = SOURCE
    resolution_100ns = 1
    def __init__(self):
        require(os.name == 'nt', 'WINDOWS_REQUIRED')
        k = C.WinDLL('kernel32', use_last_error=True)
        a = C.WinDLL('advapi32', use_last_error=True)
        self._functions = {}
        entries = [
            (k, 'GetProcessId', W.DWORD, [W.HANDLE]),
            (k, 'GetProcessTimes', W.BOOL, [W.HANDLE] + [C.POINTER(_FILETIME)] * 4),
            (k, 'QueryFullProcessImageNameW', W.BOOL,
             [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]),
            (k, 'GetLongPathNameW', W.DWORD, [W.LPCWSTR, W.LPWSTR, W.DWORD]),
            (k, 'CloseHandle', W.BOOL, [W.HANDLE]),
            (a, 'OpenProcessToken', W.BOOL, [W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)]),
            (a, 'GetTokenInformation', W.BOOL,
             [W.HANDLE, C.c_int, W.LPVOID, W.DWORD, C.POINTER(W.DWORD)]),
            (a, 'LookupAccountSidW', W.BOOL,
             [W.LPCWSTR, W.LPVOID, W.LPWSTR, C.POINTER(W.DWORD), W.LPWSTR,
              C.POINTER(W.DWORD), C.POINTER(W.DWORD)]),
        ]
        for dll, name, restype, args in entries:
            fn = getattr(dll, name)
            fn.restype, fn.argtypes = restype, args
            self._functions[name] = fn

    def _call(self, name, *args):
        C.set_last_error(0)
        value = self._functions[name](*args)
        error = C.get_last_error()
        if not value:
            raise IdentityBindingError(name + '_FAILED', int(error))
        return value

    def pid_birth(self, handle):
        times = [_FILETIME() for _ in range(4)]
        self._call('GetProcessTimes', handle, *[C.byref(v) for v in times])
        pid = int(self._call('GetProcessId', handle))
        return pid, combine_filetime(int(times[0].high), int(times[0].low))

    def image(self, handle):
        path = C.create_unicode_buffer(32768)
        size = W.DWORD(len(path))
        self._call('QueryFullProcessImageNameW', handle, 0, path, C.byref(size))
        # Reject UNC/device paths before a normal OS local-path expansion.
        normalized_local_path(path.value)
        expanded = C.create_unicode_buffer(32768)
        length = self._call('GetLongPathNameW', path.value, expanded, len(expanded))
        require(length < len(expanded), 'PATH_BUFFER_LIMIT')
        return normalized_local_path(expanded.value)

    def owner_session(self, handle):
        token = W.HANDLE()
        self._call('OpenProcessToken', handle, 8, C.byref(token))  # TOKEN_QUERY only
        try:
            size = W.DWORD()
            user_info = C.create_string_buffer(65536)
            self._call('GetTokenInformation', token, 1, user_info, len(user_info),
                       C.byref(size))
            require(C.sizeof(C.c_void_p) <= size.value <= len(user_info), 'TOKEN_USER_SIZE')
            sid = C.cast(user_info, C.POINTER(W.LPVOID))[0]
            user, domain = C.create_unicode_buffer(256), C.create_unicode_buffer(256)
            user_size, domain_size, kind = W.DWORD(256), W.DWORD(256), W.DWORD()
            self._call('LookupAccountSidW', None, sid, user, C.byref(user_size),
                       domain, C.byref(domain_size), C.byref(kind))
            session = W.DWORD()
            self._call('GetTokenInformation', token, 12, C.byref(session), 4,
                       C.byref(size))
            require(size.value == 4, 'TOKEN_SESSION_SIZE')
            return domain.value, user.value, int(session.value)
        finally:
            self._call('CloseHandle', token)  # Never closes the caller process handle.
