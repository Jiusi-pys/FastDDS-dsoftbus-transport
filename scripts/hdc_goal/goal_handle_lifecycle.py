"""Original owned Win32 handles: observation never grants a new signal right.

The creator must first verify the live image and retain its no-write/delete
image pin. No PID reopen, process factory, privilege change or HDC entry exists.
"""
from dataclasses import dataclass
import math
import time


@dataclass(frozen=True)
class HandleBinding:
    process: int
    image_pin: int
    pid: int
    birth: int
    image: str
    file_id: tuple


class LifecycleError(Exception):
    def __init__(self, code):
        self.code = code
        super().__init__(code)


class ImageUnavailable(LifecycleError):
    def __init__(self, error):
        self.error = error
        super().__init__('QUERY_IMAGE_FAILED')


def require(value, code):
    if not value:
        raise LifecycleError(code)


def observe_binding(binding, ops, *, deadline=None, clock=time.monotonic,
                    terminal_only=False):
    """Prove LIVE by image query, or TERMINAL by the original retained object.

    QFPI can fail before the process object becomes signaled. Following that
    failure only, one read-only wait of at most 50ms may use the remaining
    existing deadline. Neither error 5 nor a timeout proves termination.
    """
    require(type(binding) is HandleBinding, 'BINDING_TYPE')
    require(all(type(v) is int and v > 0 for v in
                (binding.process, binding.image_pin, binding.pid, binding.birth))
            and isinstance(binding.image, str) and bool(binding.image), 'BINDING_FIELDS')
    require(type(binding.file_id) is tuple and len(binding.file_id) == 3
            and all(type(v) is int and v >= 0 for v in binding.file_id), 'BINDING_FILE_ID')

    def anchored():
        require(ops.pid_birth(binding.process) == (binding.pid, binding.birth), 'PROCESS_BINDING_CHANGED')
        require(tuple(ops.file_id(binding.image_pin)) == binding.file_id, 'IMAGE_PIN_CHANGED')

    def terminal(transition=None):
        require(ops.wait(binding.process) == 0, 'TERMINAL_WAIT_NOT_SIGNALED')
        anchored()
        code = ops.exit_code(binding.process)
        require(type(code) is int and 0 <= code <= 0xffffffff, 'TERMINAL_EXIT_UNKNOWN')
        return {'phase': 'TERMINAL', 'exit_code': code, 'transition_error': transition,
                'proof': {'method': 'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN',
                          'hProcess': binding.process, 'pid': binding.pid, 'birth': binding.birth,
                          'image': binding.image, 'FileID': list(binding.file_id),
                          'required_apis_ok': True, 'image_pin_retained_at_check': True,
                          'full_image_was_verified_while_live': True}}

    anchored()
    state = ops.wait(binding.process)
    require(state in (0, 258), 'WAIT_UNKNOWN')
    if state == 0:
        return terminal()
    require(not terminal_only, 'TERMINAL_WAIT_NOT_SIGNALED')
    try:
        image = ops.image(binding.process)
    except ImageUnavailable as failure:
        state = ops.wait(binding.process)
        require(state in (0, 258), 'WAIT_UNKNOWN')
        waited_ms = 0
        if state != 0:
            if deadline is None:
                raise
            before = clock()
            require(type(deadline) in (int, float) and math.isfinite(deadline)
                    and math.isfinite(before), 'IDENTITY_CLOCK_INVALID')
            waited_ms = min(50, math.floor((deadline - before) * 1000))
            if waited_ms <= 0:
                raise
            state = ops.wait_terminal(binding.process, waited_ms)
            after = clock()
            require(math.isfinite(after) and after >= before, 'IDENTITY_CLOCK_REGRESSION')
            require(state in (0, 258), 'WAIT_UNKNOWN')
            if after >= deadline or state != 0:
                raise failure
        return terminal({'api': 'QueryFullProcessImageNameW', 'error': failure.error,
                         'classification': 'LIVE_TO_TERMINAL_IMAGE_UNAVAILABLE',
                         'bounded_wait_ms': waited_ms})
    require(image == binding.image, 'LIVE_IMAGE_CHANGED')
    anchored()
    state = ops.wait(binding.process)
    require(state in (0, 258), 'WAIT_UNKNOWN')
    if state == 0:
        return terminal()
    return {'phase': 'LIVE', 'image_checked': True}


def observe_owned_binding(root, ops, require_live=False):
    """Separate revoked operation authority from read-only terminal evidence.

    An unavailable observation can later be proved terminal. An observed
    contradiction is latched and can never be washed away by a later match.
    The narrow recovery path cannot return LIVE or restore resume/signal.
    """
    readonly = not root.authority
    try:
        binding = root.binding
        require(not getattr(root, '_identity_invalid', False), 'IDENTITY_PREVIOUSLY_INVALID')
        require(not root.closed and type(binding) is HandleBinding, 'ROOT_BINDING_NOT_RETAINED')
        require(not readonly or getattr(root, '_terminal_observation_only', False)
                or root.receipt.get('terminal_binding_verified') is True, 'ROOT_AUTHORITY_REVOKED')
        require(int(root.pi.hProcess) == binding.process and root.image_pin == binding.image_pin
                and binding.process in root.handles and binding.image_pin in root.handles,
                'ORIGINAL_HANDLES_NOT_RETAINED')
        owner = {'pid': binding.pid, 'birth': binding.birth, 'image': binding.image}
        require(root.owner == owner and root.receipt.get('owner') == owner,
                'CREATION_ANCHOR_CHANGED')
        require(root.receipt.get('created') is True and root.receipt.get('bound') is True
                and root.receipt.get('hProcess') == binding.process
                and root.receipt.get('FileID') == list(binding.file_id), 'CREATION_PROOF_MISSING')
        seen = observe_binding(binding, ops, terminal_only=readonly,
                               deadline=getattr(root, 'identity_deadline', None))
        phase = seen['phase']
        require(not root.receipt.get('terminal') or phase == 'TERMINAL', 'TERMINAL_STATE_REGRESSION')
        root.receipt['identity_phase'] = phase
        if phase == 'TERMINAL':
            if getattr(root, 'terminal_at', None) is None:
                root.terminal_at = time.monotonic()
            root.authority = False
            root.receipt.update(terminal=True, native_exit=seen['exit_code'],
                                signal_authority=False, terminal_binding_verified=True,
                                terminal_proof=seen['proof'])
            if seen.get('transition_error'):
                root.receipt['identity_transition_error'] = seen['transition_error']
        else:
            root.receipt['signal_authority'] = True
    except BaseException as failure:
        root.authority = False
        root.receipt.update(signal_authority=False, terminal_binding_verified=False)
        recoverable = isinstance(failure, ImageUnavailable) or (
            readonly and isinstance(failure, LifecycleError)
            and failure.code == 'TERMINAL_WAIT_NOT_SIGNALED')
        root._terminal_observation_only = recoverable
        if not recoverable:
            root._identity_invalid = True
        raise
    # A valid terminal proof is retained even when the caller required LIVE.
    require(not require_live or phase == 'LIVE', 'EXPECTED_LIVE_PROCESS_TERMINATED')
    return phase


def terminal_receipt_qualified(receipt):
    proof = receipt.get('terminal_proof') or {}
    owner = receipt.get('owner') or {}
    return (all(type(proof.get(k)) is int and proof[k] > 0 for k in ('hProcess', 'pid', 'birth'))
            and isinstance(proof.get('image'), str) and bool(proof['image'])
            and isinstance(proof.get('FileID'), list) and len(proof['FileID']) == 3
            and receipt.get('created') is True and receipt.get('bound') is True
            and receipt.get('terminal') is True and receipt.get('EOF') is True
            and receipt.get('terminal_binding_verified') is True
            and receipt.get('signal_authority') is False
            and ('drain' not in receipt or receipt['drain'].get('status') == 'EOF')
            and proof.get('method') == 'ORIGINAL_HANDLE_WAIT_PID_BIRTH_CREATED_IMAGE_PIN'
            and proof.get('hProcess') == receipt.get('hProcess')
            and proof.get('pid') == owner.get('pid') and proof.get('birth') == owner.get('birth')
            and proof.get('image') == owner.get('image') and proof.get('FileID') == receipt.get('FileID')
            and proof.get('required_apis_ok') is True
            and proof.get('image_pin_retained_at_check') is True
            and proof.get('full_image_was_verified_while_live') is True)


def bounded_drain(read_once, eof, *, deadline, remaining, clock=time.monotonic,
                  pause=time.sleep, cancelled=lambda: False, max_polls=4096):
    require(type(remaining) is int and remaining >= 0, 'DRAIN_CAP_INVALID')
    used = 0
    polls = 0
    start = clock()
    last = start

    def result(status):
        finished = clock()
        require(finished >= last, 'DRAIN_CLOCK_REGRESSION')
        if cancelled():
            status = 'CANCELLED'
        elif status == 'EOF' and finished >= deadline:
            status = 'DEADLINE'
        return {'status': status, 'EOF': bool(eof()), 'bytes': used, 'polls': polls,
                'started': start, 'finished': finished, 'deadline': deadline}

    while True:
        now = clock()
        require(now >= last, 'DRAIN_CLOCK_REGRESSION')
        last = now
        if cancelled():
            return result('CANCELLED')
        if now >= deadline:
            return result('DEADLINE')
        if eof():
            return result('EOF')
        require(polls < max_polls, 'DRAIN_POLL_CAP')
        polls += 1
        chunks = read_once(remaining-used)
        amount = sum(len(data) for _, data in chunks)
        require(used+amount <= remaining, 'DRAIN_OUTPUT_CAP')
        used += amount
        if not eof():
            pause(min(.005, max(0, deadline-clock())))
