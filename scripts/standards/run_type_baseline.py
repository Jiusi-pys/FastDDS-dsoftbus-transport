"""Execute independent UDP DDS peers and retain machine-readable evidence.

Exit 0 means every selected requirement passed, 1 means a requirement failed,
and 2 means execution was blocked. Stock limitations are reported as failures.
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import uuid


CASES = (
    "identical_xcdr1", "identical_xcdr2", "appendable_added_member",
    "mutable_added_member", "incompatible_member", "xml",
)
VERSIONS = ("2.14.5", "3.6.2")
# What each case must do, decided HERE rather than by the probe.
#
# The probe emits its own `expectation` field, and trusting it would let a future
# edit flip a delivery case to a rejection case and still report PASS -- the
# "change the expected result to match a wrong implementation" failure that
# AGENTS.md section 5.3 forbids. The runner therefore requires the probe's
# declaration to match this table, so a probe cannot grade itself.
CASE_EXPECTATION = {
    "identical_xcdr1": "delivery",
    "identical_xcdr2": "delivery",
    "appendable_added_member": "delivery",
    "mutable_added_member": "delivery",
    "incompatible_member": "rejection",
    # Neither Fast DDS version implements an XML data representation, so this
    # case asserts the rejection both perform. That is a recorded
    # non-conformance, not a conformance pass.
    "xml": "rejection",
}
# The environment variable that selects the patched 2.14.5 matching behaviour.
# Every peer record carries its observed value, because a results file that
# cannot distinguish the two configurations it exists to contrast is not
# evidence (AGENTS.md section 3).
GATE_VARIABLE = "FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY"
# The delivery case every run performs first, so that a rejection case is only
# graded in a run where delivery demonstrably works. It must be a delivery case,
# or a rejection case would have no control to depend on.
CONTROL_CASE = "identical_xcdr2"
# Not an assert: `python -O` strips those, and this guard must be unconditional.
if CASE_EXPECTATION[CONTROL_CASE] != "delivery":
    raise RuntimeError("CONTROL_CASE %r must be a delivery case, so that a rejection "
                       "case has a live-delivery control to depend on" % CONTROL_CASE)
# Shortest accepted --allow-shadowed digest prefix. Short prefixes make a pin
# meaningless: one hex character matches about one digest in sixteen.
MINIMUM_PINNED_DIGEST = 8
# Shared object / DLL extensions whose shadowing silently changes the loaded ABI.
RUNTIME_SUFFIXES = (".dll", ".so")
# Platform runtimes. Several copies of these on one search path is the normal
# state of a toolchain environment, they are not pinned to a DDS release, and the
# loader picking any recent one is well defined. Excluding them keeps the check
# pointed at libraries that do carry a pinned ABI: Fast DDS, Fast CDR,
# foonathan_memory, OpenSSL, tinyxml2 and the C++ standard library.
#
# libstdc++ and libc++_shared are deliberately NOT excluded. They are a pinned
# C++ ABI boundary, and mixing two builds of them across one search path is the
# same class of defect this guard exists to catch.
#
# Each entry is matched against the whole file name, so it must include the
# `.so` where one is needed: bare "libc" would also swallow libcrypto.so.3.
SYSTEM_RUNTIME_PREFIXES = (
    # Windows / MSVC redistributables
    "msvcp", "msvcr", "vcruntime", "concrt", "vccorlib", "mfc", "ucrtbase", "api-ms-win-",
    # POSIX / musl platform runtimes
    "ld-musl", "ld-linux", "libc.so", "libm.so", "libdl.so", "librt.so",
    "libpthread.so", "libgcc_s.so", "libresolv.so", "libutil.so", "linux-vdso",
)


def is_system_runtime(name):
    return name.startswith(SYSTEM_RUNTIME_PREFIXES)


def is_runtime_library(name):
    """Whether a lowercased file name is a loadable shared library.

    A runtime suffix must be the final component (`libfoo.so`, `foo.dll`) or be
    followed only by version numbers (`libfoo.so.2`, `libfoo.so.2.14`).
    Requiring that rejects names that merely contain a suffix earlier on, such
    as `foo.dll.bak` or `libfoo.so.disabled`, which are not loadable and would
    otherwise be compared as if they were.
    """
    parts = name.split(".")
    for index, part in enumerate(parts[1:], start=1):
        if "." + part in RUNTIME_SUFFIXES:
            return all(rest.isdigit() for rest in parts[index + 1:])
    return False


def runtime_libraries(directory):
    """List a runtime directory's shared libraries, keyed by exact file name.

    The key is the whole lowercased file name, because that is what the loader
    resolves: `libfastcdr.so.2` and `libfastcdr.so.3` are different sonames and
    do not shadow each other, while two files both named `libfastcdr.so.2` do.
    Keying on a truncated stem would collapse the first pair into a false
    conflict, and would treat `.so` differently from `.dll`.
    """
    libraries = {}
    for file in sorted(directory.iterdir()):
        if not file.is_file():
            continue
        name = file.name.lower()
        if is_runtime_library(name):
            libraries.setdefault(name, []).append(file)
    return libraries


def parse_acknowledgements(entries):
    """Parse --allow-shadowed values into {file name: expected digest or None}.

    `NAME` suppresses every content difference under that name. `NAME=DIGEST`
    suppresses it only while the copy that actually loads has a digest starting
    with DIGEST, so an acknowledgement cannot outlive the library it was
    written for.
    """
    parsed = {}
    for entry in entries:
        name, separator, digest = entry.partition("=")
        name, digest = name.strip().lower(), digest.strip().lower()
        if not name:
            raise ValueError("--allow-shadowed needs a library name: %r" % entry)
        if separator and not digest:
            # `NAME=` used to degrade to a fully unpinned acknowledgement.
            raise ValueError("--allow-shadowed %r has an empty digest; drop the '=' to "
                             "acknowledge the name unpinned" % entry)
        if digest:
            if len(digest) < MINIMUM_PINNED_DIGEST:
                # A 1-character prefix matches roughly one digest in 16.
                raise ValueError("--allow-shadowed digest for %r must be at least %d hex "
                                 "characters, got %d" % (name, MINIMUM_PINNED_DIGEST, len(digest)))
            if any(character not in "0123456789abcdef" for character in digest):
                raise ValueError("--allow-shadowed digest for %r is not hexadecimal: %r" % (name, digest))
        if name in parsed and parsed[name] != (digest or None):
            # Last-wins silently widened a pinned entry back to unpinned.
            raise ValueError("--allow-shadowed names %r twice with different digests" % name)
        parsed[name] = digest or None
    return parsed


def shadowed_libraries(directories, acknowledged=()):
    """Report libraries that one runtime directory shadows in another.

    The device orchestration that produced the 2026-11-10 mixed-version
    evidence placed Fast CDR 2.3.7 ahead of Fast CDR 2.2.5 for a Fast DDS
    2.14.5 process. Both ship `libfastcdr.so.2`, so the loader silently
    substituted an incompatible ABI and the process died in CDR
    serialization. Differing content under one name is never valid here, so
    refuse the run instead of recording the result it produces.

    Only the first directory's copy of a name can load, so that copy is the one
    reported as `loaded` and the one a pinned acknowledgement is checked against.
    """
    allowed = acknowledged if isinstance(acknowledged, dict) else parse_acknowledgements(acknowledged)
    seen = {}
    conflicts = []
    for directory in directories:
        for name, files in runtime_libraries(directory).items():
            for file in files:
                digest = sha256(file)
                previous = seen.get(name)
                if previous is None:
                    seen[name] = (file, digest)
                    continue
                if previous[1] == digest or is_system_runtime(name):
                    continue
                if name in allowed:
                    expected = allowed[name]
                    # An unpinned acknowledgement suppresses the name outright; a
                    # pinned one only while the loaded copy still matches.
                    if expected is None or previous[1].startswith(expected):
                        continue
                conflicts.append((name, previous[0], previous[1], file, digest))
    return conflicts


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


# Windows exception codes that mean the process died rather than exited.
WINDOWS_CRASH_CODES = {
    0xC0000005: "EXCEPTION_ACCESS_VIOLATION",
    0xC000001D: "EXCEPTION_ILLEGAL_INSTRUCTION",
    0xC0000025: "EXCEPTION_NONCONTINUABLE_EXCEPTION",
    0xC0000094: "EXCEPTION_INT_DIVIDE_BY_ZERO",
    0xC00000FD: "EXCEPTION_STACK_OVERFLOW",
    0xC0000374: "STATUS_HEAP_CORRUPTION",
}


def describe_termination(returncode):
    """Name the crash behind a return code, or None for an ordinary exit.

    POSIX signal deaths arrive as a negative return code; `-11` is the SIGSEGV
    that a shell would report as 139. Windows exceptions arrive as the NTSTATUS
    value. An ordinary probe exit (0, 1, 2, 3) returns None.

    Boundary: this covers processes this script launches itself, which it always
    does directly, never through a shell. A peer started through a shell or over
    ssh reports SIGSEGV as a POSITIVE 128+N, which is indistinguishable here
    from an ordinary exit status, so it is deliberately not guessed at. Device
    runs are driven by hand over ssh and are outside this script; their records
    must treat exit 139 as a crash explicitly, as the 2026-11-10 evidence does.
    """
    if returncode is None:
        return None
    if returncode < 0:
        signal_number = -returncode
        try:
            name = signal.Signals(signal_number).name
        except ValueError:
            name = "SIG%d" % signal_number
        return {"kind": "signal", "signal": signal_number, "name": name,
                "shell_exit_code": 128 + signal_number}
    name = WINDOWS_CRASH_CODES.get(returncode & 0xFFFFFFFF)
    if name is not None:
        return {"kind": "windows_exception", "code": "0x%08X" % (returncode & 0xFFFFFFFF), "name": name}
    return None


def run_peer(executable, version, role, case, domain, topic, seconds, directories, output):
    command = [str(executable), case, role, str(domain), topic, str(seconds)]
    environment = dict(os.environ)
    # Windows resolves DLLs through PATH, which is what this runner targets.
    #
    # Boundary: on POSIX, `.so` resolution is governed by LD_LIBRARY_PATH and
    # RPATH, so setting PATH alone would NOT make the loader use these
    # directories. The `.so` handling in `runtime_libraries` and
    # `SYSTEM_RUNTIME_PREFIXES` exists to AUDIT a directory set, including a
    # device deployment staged on this host, not to launch peers from it. Device
    # peers are started by hand over ssh with an explicit LD_LIBRARY_PATH and
    # are outside this script.
    environment["PATH"] = os.pathsep.join([str(p) for p in directories] + [environment.get("PATH", "")])
    # Keep parent-machine XML profiles from silently changing this UDP baseline.
    environment["SKIP_DEFAULT_XML"] = "1"
    environment.pop("FASTDDS_DEFAULT_PROFILES_FILE", None)
    environment.pop("FASTRTPS_DEFAULT_PROFILES_FILE", None)
    record = {"command": command, "executable_sha256": sha256(executable),
              "expected_fastdds": version, "runtime_directories": [str(p) for p in directories],
              # Recorded before the run so it survives a timeout or an OSError.
              "gate_variable": GATE_VARIABLE,
              "gate_value": environment.get(GATE_VARIABLE),
              "gate_on": environment.get(GATE_VARIABLE) == "1",
              "status": "BLOCKED", "exit_code": None}
    try:
        result = subprocess.run(command, capture_output=True, timeout=seconds + 15, env=environment)
        stdout_raw, stderr_raw = result.stdout, result.stderr
        stdout = result.stdout.decode("utf-8", "replace")
        stderr = result.stderr.decode("utf-8", "replace")
        record["exit_code"] = result.returncode
        try:
            observations = [json.loads(line.removeprefix("DDS_STANDARDS_RESULT "))
                            for line in stdout.splitlines() if line.startswith("DDS_STANDARDS_RESULT ")]
        except json.JSONDecodeError as error:
            observations = []
            record["error"] = str(error)
        record["observations"] = observations
        record["status"] = "FAIL"
        if len(observations) == 1 and isinstance(observations[0], dict):
            observation = observations[0]
            reported_version = observation.get("fastdds")
            expected_version = "3.6.2.0" if version == "3.6.2" else version
            expected_expectation = CASE_EXPECTATION[case]
            declared = observation.get("expectation")
            if declared != expected_expectation:
                record["error"] = ("probe declared expectation %r for case %r, this runner requires %r"
                                   % (declared, case, expected_expectation))
            identity = (observation.get("case") == case and observation.get("role") == role and
                        reported_version == expected_version and observation.get("transport") == "udp4" and
                        declared == expected_expectation)
            if (identity and result.returncode == 0 and observation.get("passed") is True and
                    observation.get("cleanup_ok") is True and observation.get("peer_alive") is True):
                record["status"] = "PASS"
        # A peer killed by a signal, or dead of a Windows exception, is a crash.
        # subprocess reports POSIX signal deaths as a negative return code and
        # Windows exceptions as the NTSTATUS value, so neither resembles the
        # probe's own exit codes. Recording a crash as a plain non-match is how
        # a segfault gets mistaken for a type that simply did not match, which
        # is exactly the confusion the 2026-11-10 exit-139 evidence caused.
        termination = describe_termination(result.returncode)
        if termination is not None:
            record["crashed"] = True
            record["termination"] = termination
            record["status"] = "FAIL"
        # Windows loader failures indicate missing runtime dependencies.
        elif result.returncode == 3 or result.returncode & 0xFFFFFFFF in (0xC0000135, 0xC000007B):
            record["status"] = "BLOCKED"
    except subprocess.TimeoutExpired as error:
        stdout_raw, stderr_raw = error.stdout or b"", error.stderr or b""
        stdout = (error.stdout or b"").decode("utf-8", "replace")
        stderr = (error.stderr or b"").decode("utf-8", "replace")
        record["error"] = "process timeout"
    except OSError as error:
        stdout_raw, stderr_raw = b"", str(error).encode("utf-8")
        stdout, stderr = "", str(error)
        record["error"] = str(error)
    output.with_suffix(".stdout.log").write_bytes(stdout_raw)
    output.with_suffix(".stderr.log").write_bytes(stderr_raw)
    record["stdout"] = str(output.with_suffix(".stdout.log"))
    record["stderr"] = str(output.with_suffix(".stderr.log"))
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--writer", type=Path, required=True)
    parser.add_argument("--reader", type=Path, required=True)
    parser.add_argument("--writer-version", choices=VERSIONS, required=True)
    parser.add_argument("--reader-version", choices=VERSIONS, required=True)
    parser.add_argument("--writer-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--reader-runtime-dir", type=Path, action="append", default=[])
    parser.add_argument("--allow-shadowed", action="append", default=[], metavar="LIBRARY[=DIGEST]",
                        help="Acknowledge one shadowed library file name, for example "
                             "'fastrtps-2.14.dll', or pin it to the copy that must load, for example "
                             "'libfastcdr.so.2=113f8ef5'. Only the first directory's copy is loaded, so "
                             "this asserts that the ordering is deliberate; the pinned form stops the "
                             "acknowledgement outliving the library it was written for. Every "
                             "acknowledgement and the digest that actually loaded are recorded in the "
                             "evidence.")
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    # Fast DDS derives the RTPS metatraffic port as 7400 + 250 * domain. If that
    # port falls inside an OS-reserved range, discovery silently never happens and
    # every case reports peer_alive false. On Windows, check with
    # `netsh int ipv4 show excludedportrange protocol=udp` before blaming the code.
    parser.add_argument("--domain", type=int, default=171)
    parser.add_argument("--seconds", type=int, default=4)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.domain < 233 or not 1 <= args.seconds <= 60:
        parser.error("domain must be 0..232 and seconds must be 1..60")
    for path in [args.writer, args.reader]:
        if not path.is_file():
            parser.error(f"missing executable: {path}")
    directories = args.writer_runtime_dir + args.reader_runtime_dir
    if any(not path.is_dir() for path in directories):
        parser.error("runtime directories must exist")
    # Each role resolves libraries from its own ordered directory list, so a
    # soname carrying different content within one role's list means the
    # loaded ABI is not the one the executable was built against.
    try:
        acknowledgements = parse_acknowledgements(args.allow_shadowed)
    except ValueError as error:
        parser.error(str(error))
    for role, paths in (("writer", args.writer_runtime_dir), ("reader", args.reader_runtime_dir)):
        conflicts = shadowed_libraries([p.resolve() for p in paths], acknowledgements)
        if conflicts:
            report = "; ".join(
                f"{soname}: {first} ({first_digest[:12]}) shadows {second} ({second_digest[:12]})"
                for soname, first, first_digest, second, second_digest in conflicts)
            parser.error(f"{role} runtime directories shadow incompatible libraries -- {report}. "
                         f"Use a self-contained runtime directory, or acknowledge each name with "
                         f"--allow-shadowed if the directory order is deliberate.")
    # A rejected-type test requires a successful delivery control in this run.
    cases = list(dict.fromkeys([CONTROL_CASE] + args.cases))
    args.out.mkdir(parents=True, exist_ok=False)
    output = args.out.resolve()
    evidence = {"schema_version": 2, "transport": "udp4", "cases": [], "status": "BLOCKED",
                "gate_variable": GATE_VARIABLE,
                "gate_value": os.environ.get(GATE_VARIABLE),
                "gate_on": os.environ.get(GATE_VARIABLE) == "1",
                "case_expectations": {case: CASE_EXPECTATION[case] for case in CASES},
                "domain": args.domain,
                # Recorded so that a run where no peer was ever seen can be checked
                # against the host's reserved port ranges without re-deriving it.
                "metatraffic_port": 7400 + 250 * args.domain,
                "acknowledged_shadowed_libraries": [
                    {"library": name, "pinned_digest": digest, "pinned": digest is not None}
                    for name, digest in sorted(acknowledgements.items())]}
    if acknowledgements:
        # Recorded with the acknowledgement suppressed, so the evidence always
        # names which copy loads and which one it hides.
        evidence["shadowed_library_detail"] = {
            role: [{"library": name, "loaded": str(first), "loaded_sha256": first_digest,
                    "shadowed": str(second), "shadowed_sha256": second_digest}
                   for name, first, first_digest, second, second_digest
                   in shadowed_libraries([p.resolve() for p in paths])]
            for role, paths in (("writer", args.writer_runtime_dir), ("reader", args.reader_runtime_dir))
        }
    runtime_paths = {"writer": args.writer_runtime_dir, "reader": args.reader_runtime_dir}
    # Share `runtime_libraries` with the shadowing check so the recorded inventory
    # and the enforced decision can never disagree about what a library is. A
    # plain `file.suffix` test missed every `*.so.<version>` name, so on POSIX
    # libfastcdr.so.2 and libfastrtps.so.2.14 were recorded with no digest --
    # exactly the libraries whose shadowing caused the 2026-11-10 exit 139.
    evidence["runtime_files"] = {
        role: {str(file.resolve()): sha256(file)
               for directory in paths
               for files in runtime_libraries(directory.resolve()).values()
               for file in files}
        for role, paths in runtime_paths.items()
    }
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for case in cases:
            topic = "DdsStandards_" + uuid.uuid4().hex
            pending = [pool.submit(run_peer, exe.resolve(), version, role, case, args.domain,
                                   topic, args.seconds, [p.resolve() for p in paths], output / f"{case}_{role}")
                       for role, exe, version, paths in (
                           ("reader", args.reader, args.reader_version, args.reader_runtime_dir),
                           ("writer", args.writer, args.writer_version, args.writer_runtime_dir))]
            peers = [future.result() for future in pending]
            crashed = any(p.get("crashed") for p in peers)
            status = "PASS" if all(p["status"] == "PASS" for p in peers) else "FAIL"
            if any(p["status"] == "BLOCKED" for p in peers):
                status = "BLOCKED"
            # A rejection case only means something when delivery demonstrably
            # works in the same run, so the control case must have passed first.
            control = next((c for c in evidence["cases"] if c["case"] == CONTROL_CASE), None)
            if CASE_EXPECTATION[case] == "rejection" and (control is None or control["status"] != "PASS"):
                status = "BLOCKED"
            # A crash outranks BLOCKED. One peer dying of SIGSEGV while the other
            # merely times out would otherwise be reported as a blocked run, which
            # is the same confusion that let the 2026-11-10 exit 139 read as a
            # type-matching failure.
            if crashed:
                status = "FAIL"
            evidence["cases"].append({"case": case, "status": status, "peers": peers, "crashed": crashed})
            evidence["status"] = "PASS" if all(c["status"] == "PASS" for c in evidence["cases"]) else "FAIL"
            if any(c["status"] == "BLOCKED" for c in evidence["cases"]):
                evidence["status"] = "BLOCKED"
            if any(c["crashed"] for c in evidence["cases"]):
                evidence["status"] = "FAIL"
            (output / "results.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
            print(f"{case}: {status}", flush=True)
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[evidence["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
