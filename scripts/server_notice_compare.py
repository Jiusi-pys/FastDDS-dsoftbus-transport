"""Fixed 311-byte comparison adapter; import/CLI never read private data.

Reuses the published reader in an isolated namespace, not a new I/O engine.
Calling its library API still requires separate trusted caller/data authority.
Public templates describe text only, never device permissions or readiness.
"""
from pathlib import Path
import hashlib
import re
import sys
import types

ENGINE_SHA256 = '07a25d5c5a696541b8b407bdae26f2d17f72b09e495e379faeff803e86c5681a'
RANGES = ((10832, 130), (14131, 181))
SIZE = 14312
CAP = 181

# Fixed public comparison source 8efffcf2fa2f5d4f997f2de8eb287432959417c8:
# base.cpp GetLogDebugFunctionName/GetLogLevelAndTime/PrintLogEx;
# define_plus.h MaskString/MaskSessionIdToString; server.cpp auth handlers.
# Historical 3.0.0b reference:443234c10deea8ba553437cf7365ce5e20970f0d.
# Neither reference is an attestation of an installed vendor executable.
# No arbitrary suffix, function, message or device-TLV value is a whitelist.
STAMP = rb'[0-9]{4}-(?:0[1-9]|1[0-2])-(?:0[1-9]|[12][0-9]|3[01]) (?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]'
FUNCTIONS = rb'(?:server\.cpp|GetDaemonAuthType|HandleAuthPubkeyMsg|HandleAuthSignatureMsg|HandServerAuth|ServerSessionHandshake|UpdateHdiInfo)'
WRAPPED = (
    re.compile(rb'\[(?P<level>[FIWDA])\]\[' + STAMP + rb'\.[0-9]{3}\]\[[0-9a-f]{1,16}\]\[(?P<function>' + FUNCTIONS + rb'):[1-9][0-9]{0,4}\] (?P<body>[^\r\n\x00]+)'),
    re.compile(rb'\[(?P<level>[FIWDA])\]\[' + STAMP + rb'\] (?P<body>[^\r\n\x00]+)'),
)
MASK = rb'(?P<session>(?:[0-9]\*{1,6}[0-9]|[1-9][0-9]{2}\*{6}[0-9]{3}\(L:(?:7|8|9|10)\)))'

RULES = (
    ('PUBLIC_SIGNING_PROGRESS', 'PROGRESS_NOT_DEVICE_ACCEPTANCE', 'I', ('HandleAuthSignatureMsg','HandServerAuth'), rb'response auth signture success'),
    ('PUBLIC_PUBLICKEY_PROGRESS', 'PROGRESS_NOT_DEVICE_ACCEPTANCE', 'I', ('HandleAuthPubkeyMsg','HandServerAuth'), rb'(?:recive get publickey cmd|send pubkey over)'),
    ('PUBLIC_SIGNATURE_CHALLENGE_PROGRESS', 'PROGRESS_NOT_DEVICE_ACCEPTANCE', 'I', 'HandServerAuth', rb'recive auth signture cmd'),
    ('PUBLIC_AUTH_ALGORITHM_PROGRESS', 'PROGRESS_NOT_DEVICE_ACCEPTANCE', 'I', 'GetDaemonAuthType', rb'daemon auth type is rsa_3072_sha512 for ' + MASK + rb' session'),
    ('PUBLIC_ENCRYPT_NEGOTIATION_WARNING', 'PROTOCOL_WARNING_NOT_USER_REQUEST', 'W', 'HandleAuthPubkeyMsg', rb'daemon did not confirm encrypt-tcp negotiation for ' + MASK + rb' session, enforcement deferred to HandServerAuth/ServerSessionHandshake'),
    ('PUBLIC_AUTH_HANDLER_FAILURE', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', ('W','D'), 'ServerSessionHandshake', rb'Auth failed'),
    ('PUBLIC_KEY_LOAD_FAILURE', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', ('HandleAuthPubkeyMsg','HandServerAuth'), rb'load public key failed'),
    ('PUBLIC_SIGNING_FAILURE', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', ('HandleAuthSignatureMsg','HandServerAuth'), rb'sign failed'),
    ('PUBLIC_SSL_INIT_FAILURE', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', 'HandServerAuth', rb'SSL init failed'),
    ('PUBLIC_OLD_ALGORITHM_FALLBACK', 'ALGORITHM_FALLBACK_NOT_AUTH_ACCEPTANCE', 'I', 'GetDaemonAuthType', rb'the deamon maybe old version for ' + MASK + rb' session, so use rsa encrypt'),
    ('PUBLIC_INVALID_ALGORITHM_FALLBACK', 'ALGORITHM_FALLBACK_NOT_AUTH_ACCEPTANCE', 'F', 'GetDaemonAuthType', rb'the buf is invalid for ' + MASK + rb' session, so use rsa encrypt'),
    ('PUBLIC_INVALID_AUTH_TYPE', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', 'HandServerAuth', rb'invalid auth type (?:[0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])'),
    ('PUBLIC_PLAINTEXT_REJECTED', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', 'HandServerAuth', rb'reject plaintext auth-signature \(downgrade\) for session ' + MASK + rb', encryption required'),
    ('PUBLIC_SSL_REQUIRED', 'AUTH_PROTOCOL_FAILURE_NOT_USER_DENIAL', 'F', 'ServerSessionHandshake', rb'reject auth-ok without ssl handshake \(downgrade\) for session ' + MASK + rb', encryption required'),
    ('PUBLIC_CONFIRMATION_TEXT', 'REQUEST_TEXT_NOT_OS_ATTESTATION', None, None, rb'(?:\[Fail\])?\[E000002\]:The device unauthorized\.|Please check for a confirmation dialog on your device\.'),
    ('PUBLIC_DENIAL_TEXT', 'DENIAL_TEXT_NOT_OS_ATTESTATION', None, None, rb'(?:\[Fail\])?\[E000003\]:The device unauthorized\.|The user denied the access for the device\.'),
)


REASONS = frozenset(('FULL_PUBLIC_TEMPLATE','FRAME_OR_ENCODING_UNSUPPORTED',
                    'WRAPPER_OR_BODY_UNSUPPORTED','BODY_UNSUPPORTED',
                    'BODY_KNOWN_WRAPPER_CONFLICT','FIELD_FORMAT_UNSUPPORTED',
                    'PUBLIC_DYNAMIC_FIELD_FORMAT_ONLY','AMBIGUOUS_PUBLIC_RULES'))


def unknown(reason='WRAPPER_OR_BODY_UNSUPPORTED'):
    if reason not in REASONS:
        raise ValueError('REASON_ENUM')
    return dict(code='OTHER_OR_UNMATCHED', template='UNMATCHED', state='UNKNOWN',
                category='UNKNOWN', dynamic_fields_discarded=True, reason=reason)


def classify_server_record(data, *, framing='LF_TERMINATED'):
    if type(data) is not bytes or len(data) > CAP or framing != 'LF_TERMINATED':
        return unknown('FRAME_OR_ENCODING_UNSUPPORTED')
    # Reuse the established exact framing logic without changing its module.
    if not data.endswith(b'\n'):
        return unknown('FRAME_OR_ENCODING_UNSUPPORTED')
    body = data[:-1]
    body = body[:-2] if body.endswith(b'\r\r') else body.removesuffix(b'\r')
    if any(c in body for c in (b'\r', b'\n', b'\x00')):
        return unknown('FRAME_OR_ENCODING_UNSUPPORTED')
    try:
        body.decode('utf-8', 'strict')
    except UnicodeError:
        return unknown('FRAME_OR_ENCODING_UNSUPPORTED')
    wrapper = next((hit for pattern in WRAPPED if (hit := pattern.fullmatch(body))), None)
    level = function = None
    if wrapper:
        level, body = wrapper.group('level', 'body')
        function = wrapper.groupdict().get('function')
    found = []; wrapper_conflict = field_invalid = False
    for template, category, expected_level, expected_function, pattern in RULES:
        matched = re.fullmatch(pattern, body)
        if not matched:
            continue
        # MaskString of a one-digit integer repeats that digit around one '*'.
        session = matched.groupdict().get('session')
        short = re.fullmatch(rb'([0-9])(\*{1,6})([0-9])', session) if session else None
        if short:
            if (len(short[2]) == 1 and short[1] != short[3]
                    or len(short[2]) > 1 and short[1] == b'0'):
                field_invalid = True
                continue
        levels = (expected_level,) if type(expected_level) is str else expected_level
        functions = (expected_function,) if type(expected_function) is str else expected_function
        if wrapper and levels and level not in tuple(x.encode('ascii') for x in levels):
            wrapper_conflict = True
            continue
        if wrapper and function and functions and function not in (b'server.cpp', *(x.encode('ascii') for x in functions)):
            wrapper_conflict = True
            continue
        found.append((template, category))
    if len(found) != 1:
        if len(found) > 1: return unknown('AMBIGUOUS_PUBLIC_RULES')
        if field_invalid: return unknown('FIELD_FORMAT_UNSUPPORTED')
        if wrapper_conflict: return unknown('BODY_KNOWN_WRAPPER_CONFLICT')
        # Structural evidence only: device-provided values stay opaque UNKNOWN.
        if (wrapper and function in (b'UpdateHdiInfo',b'ServerSessionHandshake',b'server.cpp')
                and body.startswith((b'handshake.buf = ',b'emgmsg = ',b'daemonauthstatus = ',
                                     b'handshake info is : ',b'handshake.banner:'))):
            return unknown('PUBLIC_DYNAMIC_FIELD_FORMAT_ONLY')
        return unknown('BODY_UNSUPPORTED' if wrapper else 'WRAPPER_OR_BODY_UNSUPPORTED')
    template, category = found[0]
    return dict(code='E000002' if category.startswith('REQUEST') else
                'E000003' if category.startswith('DENIAL') else 'OTHER_OR_UNMATCHED',
                template=template, category=category, state='PUBLIC_TEMPLATE_MATCH',
                dynamic_fields_discarded=True, reason='FULL_PUBLIC_TEMPLATE')


def engine():
    """Three exact fixed-scope substitutions; original engine stays unchanged."""
    p = Path(__file__).with_name('bounded_public_compare.py')
    raw = p.read_bytes()
    if hashlib.sha256(raw).hexdigest() != ENGINE_SHA256:
        raise ValueError('PUBLIC_ENGINE_PIN')
    source = raw.decode('utf-8')
    for before, after in (
        ('RANGES=((0,65),(115,77))', 'RANGES=((10832,130),(14131,181))'),
        ('self.size==193', 'self.size==14312'),
        ('len(data)<=77', 'len(data)<=181'),
    ):
        if source.count(before) != 1:
            raise ValueError('EXACT_ADAPTER_POINT')
        source = source.replace(before, after)
    name = '_fixed_server_notice_read_engine'
    m = types.ModuleType(name); m.__file__ = str(p)
    previous = sys.modules.get(name); sys.modules[name] = m
    try:
        exec(compile(source, str(p), 'exec'), m.__dict__)
    finally:
        if previous is None: sys.modules.pop(name, None)
        else: sys.modules[name] = previous
    m.classify_record = classify_server_record
    return m


def native_ops(m, path, *, volume, file_index, write_100ns):
    """Retain original I/O/close behavior, add caller-bound native metadata."""
    class NativeBoundOps(m.Win32ReadOps):
        def handle_identity(self):
            identity = super().handle_identity()
            C, W, k, FI = self.api; info = FI()
            m.require(k.GetFileInformationByHandle(self.handle, C.byref(info)), 'NATIVE_HANDLE_INFO')
            m.require((int(info.volume), (int(info.indexhi) << 32) | int(info.indexlo),
                       (int(info.written.high) << 32) | int(info.written.low)) ==
                      (volume, file_index, write_100ns), 'NATIVE_FILE_BINDING')
            return identity
    return NativeBoundOps(path)


def main():
    print('SERVER_NOTICE_COMPARE_REQUIRES_SEPARATE_CALLER_AUTHORITY')
    return 78


if __name__ == '__main__':
    raise SystemExit(main())
