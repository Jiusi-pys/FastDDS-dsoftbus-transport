#!/usr/bin/env python3
"""Local stand-in for the device tool at the LOWEST command-execution boundary.

Scope, deliberately narrow:

  * not a device model and not a general simulator. It sits where the formal flow
    would exec the real device tool, records exactly what was asked, and refuses
    everything it has not been taught yet with an explicit non-zero result;
  * the supported list below grew from real calls observed in the formal flow -
    nothing is claimed as covered before an actual caller exercised it;
  * it never runs a shell on the caller's behalf, never falls back to another
    program, and never touches anything outside the sandbox root;
  * values come from real sandbox bytes. Hashes are computed, never echoed from an
    expected constant, and a missing file is a failure rather than a pass.

Reject codes (non-zero, stable, greppable):

  64 ARGV_SHAPE   65 UNKNOWN_TARGET   66 UNSUPPORTED_SUBCOMMAND
  67 PATH_ESCAPES_SANDBOX   68 SANDBOX_UNAVAILABLE   69 REAL_DEVICE_CONFIG
  70 LOCAL_TRANSFER_FAILED  71 JOURNAL_WRITE_FAILED

Shell command failures use remote_rc=1 with driver_exit=0 and one marker.
These driver-internal codes are never remote shell results.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import sys
import time
import uuid
from pathlib import Path
import finite_model as model

HERE = Path(__file__).resolve().parent
SIM_ROOT = HERE.parent
SANDBOX = Path(os.environ.get("FAKE_HDC_SANDBOX") or (SIM_ROOT / "sandbox"))
LOG = SANDBOX / "fake-hdc-invocations.jsonl"
RAW_LOG = Path(os.environ.get("FAKE_HDC_RAW_LOG") or (SIM_ROOT / "logs" / "fake-hdc-raw.log"))

SYNTHETIC_TARGETS = {"SIM-A": "boardA", "SIM-B": "boardB"}
REAL_BOARD_RE = re.compile(r"^3e01ff55454d2020201[0-9A-F]{12}$", re.I)
REMOTE_ROOT_RE = re.compile(r"^/data/local/tmp/mdds_dds_([0-9a-f]{16})_([AB])$")
EXIT_WRAPPER_RE = re.compile(r";[ ]*mdds_rc=[$][?];[ ]*echo[ ]+MDDS_REMOTE_EXIT=[$]mdds_rc[ ]*$")

# Synthetic platform identity. Both the value and its hash live in the sandbox, so
# the formal hash comparison reads sandbox bytes rather than a fixed expectation.
PLATFORM_FILES = ("/system/lib64/platformsdk/libsoftbus_client.z.so",
                  "/system/lib64/libc++.so", "/system/lib64/platformsdk/libsoftbus_server.z.so")
PLATFORM_PARAMS = {"const.product.software.version": "KaihongOS-SIM",
                   "const.product.devicetype": "sim"}
KERNEL = "Linux localhost 6.6.101-sim #1 SMP PREEMPT SIM"

BSEP = chr(92)


def _raw_tail():
    """Last raw command-line tail the shim recorded, decoded as a whole.

    The shim appends UTF-16LE with no BOM, so splitting the raw bytes on a single
    0x0A would cut through the low byte of an unrelated character.
    """
    try:
        data = RAW_LOG.read_bytes()
    except OSError:
        return None
    if not data:
        return None
    try:
        lines = [l for l in data.decode("utf-16-le", errors="strict").splitlines() if l.strip()]
    except UnicodeDecodeError:
        return None
    return lines[-1] if lines else None


def _classify(argv):
    """Caller purpose inferred from the argv shape, so the log stays diagnosable."""
    if not argv:
        return "UNKNOWN"
    sub, joined = argv[0], " ".join(argv)
    if sub == "shell":
        if "MDDS_REMOTE_EXIT=" in joined:
            return "REMOTE_COMMAND_EXPECTING_MDDS_REMOTE_EXIT"
        if "CONTROL_REMOTE_EXIT=" in joined:
            return "STAGE_CONTROL_RELEASE"
        if "LOAD_OBSERVER_REMOTE_EXIT=" in joined:
            return "LOAD_OBSERVER_COLLECTION"
        return "REMOTE_SHELL_COMMAND"
    if sub == "file":
        if len(argv) >= 2 and argv[1] == "recv":
            return "FILE_RECV_FROM_REMOTE"
        if len(argv) >= 2 and argv[1] == "send":
            return "FILE_SEND_TO_REMOTE"
        return "FILE_OTHER"
    return "UNKNOWN"


def _sandbox_path(remote):
    """Map a remote path into the sandbox, or None when unknown / escaping."""
    return model.mapped(SANDBOX,remote)


def _iter_strings(argv):
    for a in argv:
        yield from re.findall(r"[A-Za-z]:[^ ]+|/[A-Za-z0-9_./-]{3,}", a)


def _escape_check(argv):
    sandbox_str = str(SANDBOX).replace(BSEP, "/").lower()
    for s in _iter_strings(argv):
        norm = s.replace(BSEP, "/").rstrip(",;")
        if REMOTE_ROOT_RE.match(norm):
            continue
        if norm in PLATFORM_FILES:
            continue
        if re.match(r"^/[A-Za-z]", norm) and norm.lower().startswith(
                ("/system/", "/data/local/tmp/mdds_dds_", "/dev/", "/proc/")):
            continue
        if re.match(r"^[A-Za-z]:", norm) and not Path(norm).resolve().is_relative_to(SIM_ROOT.resolve()):
            return norm
    return None


class DriverRejected(Exception):
    def __init__(self, code, detail):
        self.code, self.detail = code, detail


def _handle_shell(command, unsupported):
    """Return simulated remote output/rc; rejection is a DIFFERENT result type."""
    suffix = EXIT_WRAPPER_RE.search(command)
    if suffix is None or command.count('MDDS_REMOTE_EXIT=') != 1 or '\n' in command or '\r' in command:
        raise DriverRejected(64, 'MALFORMED_REMOTE_WRAPPER')
    core = command[:suffix.start()].strip()
    if core == "ps -A":
        return model.handle(core,SANDBOX,DriverRejected)
    if core == "uname -a":
        return KERNEL + "\n", 0
    m = re.fullmatch(r"sha256sum[ ]+(/[A-Za-z0-9_./+-]+)", core)
    if m:
        remote = m.group(1)
        local = _sandbox_path(remote)
        if local is None or not local.resolve().is_relative_to(SANDBOX.resolve()) or '..' in remote.split('/'):
            raise DriverRejected(67, 'PATH_NOT_ALLOWED')
        if not local.is_file():
            return "", 1  # supported remote sha256sum failure, NOT driver 68
        if model.fault(SANDBOX)=='empty_hash' and '/mdds_dds_' in remote and Path(remote).name==model.config().get('main_member'):return '',0
        return "%s  %s\n" % (hashlib.sha256(local.read_bytes()).hexdigest(), remote), 0
    m = re.fullmatch(r"param[ ]+get[ ]+([A-Za-z0-9_.]+)", core)
    if m:
        if m.group(1) not in PLATFORM_PARAMS:
            return "", 1  # supported remote parameter lookup failure
        return PLATFORM_PARAMS[m.group(1)] + "\n", 0
    return model.handle(core,SANDBOX,DriverRejected)


def _handle_file(rest, unsupported):
    if len(rest) < 3:
        return "", 64
    mode, a, b = rest[0], rest[1], rest[2]
    if mode == "recv":
        local = _sandbox_path(a)
        if local is None or not local.resolve().is_relative_to(SANDBOX.resolve()):
            return "", 67
        if not local.is_file():
            return "", 68
        if model.fault(SANDBOX)=='missing_recv' and a.endswith('/load-maps.txt'):return '',68
        dst = Path(b)
        if not dst.resolve().is_relative_to(SIM_ROOT.resolve()):return '',67
        try:
            dst.parent.mkdir(parents=True, exist_ok=True)
            if dst.exists():
                return "", 70
            dst.write_bytes(local.read_bytes())
        except OSError:
            return "", 68
        return "", 0
    if mode == "send":
        dst = _sandbox_path(b)
        if dst is None or not dst.resolve().is_relative_to(SANDBOX.resolve()):
            return "", 67
        src = Path(a)
        if not src.resolve().is_relative_to(SIM_ROOT.resolve()):return '',67
        if not src.is_file():
            return "", 68
        try:
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(src.read_bytes())
        except OSError:
            return "", 68
        return "", 0
    unsupported.append("file " + mode)
    return "", 66


def _inventory(record):
    # Host-local fixed inventory seam; never invokes any device executable.
    if not SANDBOX.is_dir(): return 68
    record.update(target='HOST_INVENTORY',action_class='READ_ONLY',driver_status='DRIVER_ACCEPTED_INVOCATION',driver_exit=0,remote_rc=None,completed=True,model_mutations=[],mutation_intents=[])
    with LOG.open('a',encoding='utf-8') as stream:stream.write(json.dumps(record)+'\n')
    sys.stdout.write('SIM-A\nSIM-B\n');return 0

def main(argv):
    record = {"schema": 2, "ts_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "pid": os.getpid(), "cwd": os.getcwd(),
              "test_run": os.environ.get("FAKE_HDC_TEST_RUN", ""),
              "window_root": os.environ.get("FAKE_HDC_WINDOW_ROOT", ""),
              "sandbox_root": str(SANDBOX.resolve()),
              "driver_image_sha256": hashlib.sha256((HERE / "fake-hdc.exe").read_bytes()).hexdigest(),
              "argv": argv, "raw_tail": _raw_tail(),
              "driver_source": str(Path(__file__).resolve()),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    if argv==['list','targets']:return _inventory(record)
    stdout, stderr, driver_rc, remote_rc, detail, unsupported = "", "", 0, None, "", []
    try:
        if not SANDBOX.is_dir():
            raise DriverRejected(68, 'SANDBOX_UNAVAILABLE')
        # Diagnostic code provenance only, never a simulated platform mutation.
        snapshot = SANDBOX / 'driver-sources' / (record['driver_sha256'] + '.py')
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        if not snapshot.exists():
            with snapshot.open('xb') as f:
                f.write(Path(__file__).read_bytes())
        if hashlib.sha256(snapshot.read_bytes()).hexdigest() != record['driver_sha256']:
            raise DriverRejected(71, 'DRIVER_SNAPSHOT_MISMATCH')
        record['driver_source_snapshot'] = str(snapshot.resolve())
        if len(argv) < 3 or argv[0] != '-t':
            raise DriverRejected(64, 'ARGV_SHAPE')
        target, rest = argv[1], argv[2:]
        record.update(target=target, subcommand=rest[0], subcommand_args=rest[1:], caller_purpose=_classify(rest))
        if REAL_BOARD_RE.fullmatch(target):
            raise DriverRejected(69, 'REAL_DEVICE_CONFIG')
        if target not in SYNTHETIC_TARGETS:
            raise DriverRejected(65, 'UNKNOWN_TARGET')
        os.environ['FAKE_HDC_SIDE']='A' if target=='SIM-A' else 'B'
        record['command_id']=uuid.uuid4().hex
        journal_identity=dict(command_id=record['command_id'],run=record['test_run'],window=record['window_root'],side=os.environ['FAKE_HDC_SIDE'],purpose=record['caller_purpose'],pid=os.getpid(),argv=argv,driver_sha256=record['driver_sha256'],model_sha256=hashlib.sha256(Path(model.__file__).read_bytes()).hexdigest())
        # Persist intent before ANY modeled mutation. Unknown commands remain
        # potentially mutable until their explicit grammar rejects them.
        model.event(SANDBOX,dict(journal_identity,event='DISPATCH_INTENT',possible_mutation=True,result='PENDING'))
        if _escape_check(argv):
            raise DriverRejected(67, 'PATH_ESCAPES_SANDBOX')
        if rest[0] == 'shell' and len(rest) == 2 and target=='SIM-B' and model.fault(SANDBOX) in ('recovery_offline','recovery_offline_once') and rest[1].startswith('sha256sum /system/'):
            # Deliberate transport-capture fault fixture, not a simulated remote rc.
            stdout='[Fail][E001005] Device not found or connected\n';stderr='[Fail]ExecuteCommand need connect-key\n'
            record['action_class']='READ_ONLY';record['fault']='INJECTED_MISSING_REMOTE_ENVELOPE'
            if model.fault(SANDBOX)=='recovery_offline_once':model.write(SANDBOX/'fault.json',dict(fault='',consumed_test_transport_fault=True))
        elif rest[0] == 'shell' and len(rest) == 2:
            if EXIT_WRAPPER_RE.search(rest[1]):
                stdout, remote_rc = _handle_shell(rest[1], unsupported)
                stdout += 'MDDS_REMOTE_EXIT=%d\n' % remote_rc
                record['action_class'] = model.classify(rest[1][:EXIT_WRAPPER_RE.search(rest[1]).start()].strip())
                if record['action_class']=='READ_ONLY_OR_UNKNOWN':record['action_class']='READ_ONLY'
            else:
                stdout,driver_rc=model.special(rest[1],SANDBOX,DriverRejected)
                record['action_class']=model.classify(rest[1])
            if remote_rc:
                stderr = 'SIMULATED_REMOTE_FAILURE rc=%d\n' % remote_rc
        elif rest[0] == 'file' and len(rest) == 4:
            stdout, code = _handle_file(rest[1:], unsupported)
            if code:
                raise DriverRejected(70, 'LOCAL_TRANSFER_FAILED code=%d' % code)
            record['action_class'] = 'LOCAL_CAPTURE' if rest[1] == 'recv' else 'MUTATION'
        else:
            raise DriverRejected(66, 'UNSUPPORTED_SUBCOMMAND')
        detail = 'simulation completed'
    except DriverRejected as error:
        driver_rc, detail = error.code, error.detail
        stdout = ''
        stderr = 'FAKE_HDC_REJECT code=%d detail=%s\n' % (driver_rc, detail)
    except OSError as error:
        driver_rc, detail, stdout = 68, 'SANDBOX_IO_FAILURE: '+type(error).__name__, ''
        stderr = 'FAKE_HDC_REJECT code=68 detail=%s\n' % detail
    record.update(driver_status='DRIVER_REJECTED_INVOCATION' if driver_rc else 'DRIVER_ACCEPTED_INVOCATION',
                  driver_exit=driver_rc, remote_rc=remote_rc if not driver_rc else None,
                  detail=detail, completed=True, spawned_children=[], model_mutations=[] if record.get("action_class") == "READ_ONLY" else ["NON_READ_ONLY_OR_UNDETERMINED"],
                  mutation_intents=[] if record.get('action_class') == 'READ_ONLY' else ['NON_READ_ONLY_OR_REJECTED'])
    model.event(SANDBOX,dict(command_id=record.get('command_id'),run=record['test_run'],window=record['window_root'],side=os.environ.get('FAKE_HDC_SIDE'),pid=os.getpid(),argv=argv,event='DISPATCH_COMPLETED',driver_exit=driver_rc,remote_rc=remote_rc,action_class=record.get('action_class','UNKNOWN'),model_mutations=record['model_mutations']))
    try:
        with LOG.open('a', encoding='utf-8', newline='\n') as f:
            f.write(json.dumps(record, ensure_ascii=False)+'\n');f.flush();os.fsync(f.fileno())
    except OSError:
        sys.stderr.write('FAKE_HDC_REJECT code=71 JOURNAL_WRITE_FAILED\n')
        return 71  # never silently succeed with missing action evidence
    sys.stdout.write(stdout);sys.stdout.flush()
    sys.stderr.write(stderr);sys.stderr.flush()
    return driver_rc


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
