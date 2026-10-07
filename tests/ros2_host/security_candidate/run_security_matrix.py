#!/usr/bin/env python
"""Run the CVE-2026-63340 Host verification matrix.

Task: CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492.

Every scenario starts the subscriber role as its own OS process and then the
publisher role as a second OS process, so the authenticated handshake and the
access-control decision happen between independent participants rather than
through an intra-process shortcut. Each process writes its own JSON observation
record; this driver only sequences them and records the pass/fail attribution.

The driver never sends RTPS bytes itself and never touches a network interface
other than the loopback the probe is whitelisted to.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

# name -> scenario definition.
#
#   sub_role / pub_role      probe roles
#   governance               "sign" for the candidate profile, "none" for the
#                            unmitigated control document
#   sub_expect / pub_expect  the verdict each side must report
#
SCENARIOS = [
    {
        "name": "00_unprotected_baseline_pair",
        "why": "Baseline for the harness itself: two ordinary participants with no "
               "security configuration at all must match and deliver. Without this, "
               "a silent transport failure would look like a successful mitigation.",
        "sub_role": "plain_sub", "sub_identity": "b", "pub_role": "plain_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "MATCHED_AND_DELIVERED", "pub_expect": "MATCHED_AND_WRITTEN",
    },
    {
        "name": "01_secure_pair_sign_discovery",
        "why": "Authenticated, authorized pair under the candidate profile must "
               "discover and deliver.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "secure_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "MATCHED_AND_DELIVERED", "pub_expect": "MATCHED_AND_WRITTEN",
        "expect_authorized_min": 1,
    },
    {
        "name": "02_plain_peer_against_secure_subscriber",
        "why": "A peer with no security configuration at all must not be "
               "discovered, matched or delivered to while SIGN discovery "
               "protection is in force.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "plain_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "NO_MATCH", "pub_expect": "NO_MATCH",
        "expect_authorized_max": 0,
        # The unauthenticated peer is observed and refused by the plugin.
        "sub_unauthorized_min": 1,
        # Under the candidate profile the unauthenticated peer is never turned
        # into a discovered participant. Scenario 10 shows this is governed by
        # allow_unauthenticated_participants, not by discovery_protection_kind.
        "sub_discovered_max": 0,
    },
    {
        "name": "03_plain_subscriber_against_secure_publisher",
        "why": "Same negative observed from the other side of the pair.",
        "sub_role": "plain_sub", "sub_identity": "b", "pub_role": "secure_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "NO_MATCH", "pub_expect": "NO_MATCH",
        "expect_authorized_max": 0,
        "pub_unauthorized_min": 1,
        "pub_discovered_max": 0,
    },
    {
        "name": "04_valid_identity_without_permission",
        "why": "Certificate chain valid, but the grant does not cover the governed "
               "domain or topic. Access control, not authentication, is the layer "
               "that refuses, and it refuses at local endpoint creation.",
        "sub_role": "wrongperm_sub", "sub_identity": "c", "pub_role": "secure_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "CREATE_READER_FAILED", "pub_expect": "NO_MATCH",
        "expect_authorized_max": 0,
        "sub_log_contains": [
            "topic not found in allow rule",
            "Error checking creation of local reader",
        ],
    },
    {
        "name": "05_control_unauthenticated_still_refused",
        "why": "Control that moves the governance knobs to the unmitigated setting "
               "(discovery protection NONE, allow_unauthenticated_participants true, "
               "join access control off). This Fast DDS version still refuses the "
               "unauthenticated peer's endpoints, so this control does NOT produce "
               "an unprotected success path and the refusal cannot be attributed to "
               "discovery_protection_kind alone. It also does not isolate the knob: "
               "scenario 10 shows that flipping allow_unauthenticated_participants on "
               "its own, with discovery protection still SIGN, produces the same "
               "discovered-participant count as this scenario. Four governance fields "
               "move together here and this one is not the discriminator.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "plain_pub", "pub_identity": "a",
        "governance": "none", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "NO_MATCH", "pub_expect": "NO_MATCH",
        "sub_unauthorized_min": 1,
        "sub_log_contains": ["Security manager returns an error for reader"],
        # This document also sets allow_unauthenticated_participants true, and
        # the same plain peer IS reported as discovered. Scenario 10 attributes
        # that change to this field rather than to discovery protection.
        "sub_discovered_min": 1,
    },
    {
        "name": "10_single_knob_allow_unauthenticated_only",
        "why": "Single-knob probe: identical to the candidate profile except that "
               "allow_unauthenticated_participants is true; discovery protection stays "
               "SIGN and join access control stays on. Observed: the secure side reports "
               "the plain peer as discovered (1), exactly as in scenario 05. The "
               "0-to-1 change is therefore driven by allow_unauthenticated_participants "
               "and NOT by discovery_protection_kind, which this matrix never isolates.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "plain_pub", "pub_identity": "a",
        "governance": "allow_unauth_only", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "NO_MATCH", "pub_expect": "NO_MATCH",
        "expect_authorized_max": 0,
        "sub_unauthorized_min": 1,
        # The middle row of the three-configuration attribution table. Without
        # this assertion a regression to 0 here would still pass the scenario
        # while invalidating the re-attribution.
        "sub_discovered_min": 1,
    },
    {
        "name": "06_control_secure_pair_discovery_protection_off",
        "why": "Control with both sides secured and discovery protection disabled, "
               "so a failure in 05 cannot be explained by the secure side being "
               "unable to run at all: the same harness observes a successful "
               "authenticated match and delivery here.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "secure_pub", "pub_identity": "a",
        "governance": "none", "payload_bytes": 4096, "samples": 5,
        "sub_expect": "MATCHED_AND_DELIVERED", "pub_expect": "MATCHED_AND_WRITTEN",
        "expect_authorized_min": 1,
    },
    {
        "name": "07_authorized_fragmented_sample",
        "why": "A sample far larger than the 65500-byte UDP datagram limit must "
               "still be fragmented, reassembled and delivered for an authenticated "
               "pair; the mitigation must not be a fragment filter.",
        "sub_role": "secure_sub", "sub_identity": "b", "pub_role": "secure_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 200000, "samples": 3,
        "sub_expect": "MATCHED_AND_DELIVERED", "pub_expect": "MATCHED_AND_WRITTEN",
        "expect_authorized_min": 1,
    },
    {
        "name": "08_missing_governance_fails_closed",
        "why": "Security plugin declared but the governance document is absent. "
               "Silent fallback to non-secure operation would create a participant.",
        "sub_role": None, "sub_identity": None, "pub_role": "misconfig_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 1,
        "sub_expect": None, "pub_expect": "PARTICIPANT_CREATE_FAILED",
    },
    {
        "name": "09_missing_identity_certificate_fails_closed",
        "why": "Same fail-closed requirement for an absent identity certificate.",
        "sub_role": None, "sub_identity": None, "pub_role": "missing_cert_pub", "pub_identity": "a",
        "governance": "sign", "payload_bytes": 4096, "samples": 1,
        "sub_expect": None, "pub_expect": "PARTICIPANT_CREATE_FAILED",
    },
]


# Roles that run the DDS Security plugins. on_participant_authentication is
# raised only by the security manager, so a plain participant always reports
# zero authorized peers and asserting that is a tautology, not a check.
SECURE_ROLES = {"secure_pub", "secure_sub", "wrongperm_sub", "misconfig_pub", "missing_cert_pub"}

GOVERNANCE_FILES = {
    "sign": "governance_sign.smime",
    "none": "governance_none_control.smime",
    "allow_unauth_only": "governance_allow_unauth_only.smime",
}


def secured_sides(scenario):
    """Which sides of this scenario actually run the security plugins."""
    sides = []
    if scenario.get("sub_role") in SECURE_ROLES:
        sides.append("subscriber")
    if scenario.get("pub_role") in SECURE_ROLES:
        sides.append("publisher")
    return sides


def free_port_triple(base: int) -> tuple[int, int, int]:
    return base, base + 1, base + 2


def port_available(port: int) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.bind(("127.0.0.1", port))
        except OSError:
            return False
    return True


def child_env(probe: Path, runtime_dirs):
    """Restrict the child DLL search order to the intended prefixes.

    The probe directory comes first so the SECURITY=ON fastrtps/fastcdr built
    for this task are the ones loaded; the explicit runtime directories only
    supply third-party artifacts the candidate prefix itself does not ship.
    """
    env = dict(os.environ)
    parts = [str(probe.parent)] + [str(item) for item in runtime_dirs]
    parts.append(env.get("PATH", ""))
    env["PATH"] = os.pathsep.join(parts)
    return env


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_scenario(probe: Path, pki: Path, out: Path, scenario: dict, base_port: int,
                 timeout_ms: int, runtime_dirs) -> dict:
    name = scenario["name"]
    work = out / name
    work.mkdir(parents=True, exist_ok=True)
    sub_port, pub_port, sub_data_port = free_port_triple(base_port)
    pub_data_port = sub_data_port + 1
    ports = (sub_port, pub_port, sub_data_port, pub_data_port)
    while not all(port_available(item) for item in ports):
        sub_port, pub_port, sub_data_port = free_port_triple(sub_port + 4)
        pub_data_port = sub_data_port + 1
        ports = (sub_port, pub_port, sub_data_port, pub_data_port)

    def governance_arg(role: str):
        # Only secure roles read the governance document.
        if not role or not role.startswith(("secure_", "wrongperm_", "misconfig_", "missing_cert_")):
            return []
        return ["--governance", str(pki / GOVERNANCE_FILES[scenario["governance"]])]

    common = [
        "--pki", str(pki),
        "--domain", "0",
        "--topic", "Cve63340IsolatedTest",
        "--payload-bytes", str(scenario["payload_bytes"]),
        "--samples", str(scenario["samples"]),
    ]

    sub_proc = None
    if scenario["sub_role"]:
        sub_cmd = [
            str(probe), "--role", scenario["sub_role"],
            "--identity", scenario["sub_identity"],
            *common,
            *governance_arg(scenario["sub_role"]),
            "--deadline-ms", str(timeout_ms),
            "--metatraffic-port", str(sub_port),
            "--peer-port", str(pub_port),
            "--data-port", str(sub_data_port),
            "--result", str(work / "sub.json"),
            "--note", name + " / subscriber side",
        ]
        with (work / "sub.log").open("wb") as log:
            sub_proc = subprocess.Popen(sub_cmd, stdout=log, stderr=subprocess.STDOUT,
                                        env=child_env(probe, runtime_dirs))
        time.sleep(1.5)

    pub_cmd = [
        str(probe), "--role", scenario["pub_role"],
        "--identity", scenario["pub_identity"],
        *common,
        *governance_arg(scenario["pub_role"]),
        "--deadline-ms", str(max(2000, timeout_ms - 2000)),
        "--match-timeout-ms", str(max(1000, timeout_ms - 4000)),
        "--metatraffic-port", str(pub_port),
        "--peer-port", str(sub_port),
        "--data-port", str(pub_data_port),
        "--result", str(work / "pub.json"),
        "--note", name + " / publisher side",
    ]
    pub_timeout = False
    pub_exit = None
    try:
        with (work / "pub.log").open("wb") as log:
            pub = subprocess.run(pub_cmd, stdout=log, stderr=subprocess.STDOUT,
                                 env=child_env(probe, runtime_dirs),
                                 timeout=(timeout_ms / 1000.0) + 60)
            pub_exit = pub.returncode
    except subprocess.TimeoutExpired:
        # The publisher hung. Record it and fall through to the mandatory
        # subscriber cleanup rather than letting the exception escape and leave
        # an orphan holding the scenario's ports.
        pub_timeout = True

    if sub_proc is not None:
        try:
            sub_proc.wait(timeout=(timeout_ms / 1000.0) + 60)
        except subprocess.TimeoutExpired:
            sub_proc.kill()
            sub_proc.wait(timeout=30)

    def read(which: Path) -> dict | None:
        if not which.exists():
            return None
        return json.loads(which.read_text(encoding="utf-8"))

    sub_result = read(work / "sub.json")
    pub_result = read(work / "pub.json")

    checks = []

    def expect(key: str, actual, wanted, comparator):
        ok = comparator(actual, wanted)
        checks.append({"check": key, "actual": actual, "expected": wanted, "pass": bool(ok)})
        return ok

    if scenario["sub_expect"] is not None:
        actual = (sub_result or {}).get("verdict")
        expect("subscriber_verdict", actual, scenario["sub_expect"], lambda a, w: a == w)
    if scenario["pub_expect"] is not None:
        actual = (pub_result or {}).get("verdict")
        expect("publisher_verdict", actual, scenario["pub_expect"], lambda a, w: a == w)

    if "expect_authorized_min" in scenario:
        actual = (sub_result or {}).get("authorized_participants")
        expect("subscriber_authorized_participants", actual,
               scenario["expect_authorized_min"], lambda a, w: a is not None and a >= w)
    # Only the sides that actually run the security plugins are checked.
    if "expect_authorized_max" in scenario:
        sides = secured_sides(scenario)
        if not sides:
            checks.append({"check": "secured_side_present", "actual": "none",
                           "expected": "at least one secured side", "pass": False})
        for side in sides:
            source = sub_result if side == "subscriber" else pub_result
            actual = (source or {}).get("authorized_participants")
            expect(side + "_authorized_participants", actual,
                   scenario["expect_authorized_max"], lambda a, w: a is not None and a <= w)
    if "sub_discovered_max" in scenario:
        actual = (sub_result or {}).get("discovered_participants")
        expect("subscriber_discovered_participants", actual,
               scenario["sub_discovered_max"], lambda a, w: a is not None and a <= w)
    if "pub_discovered_max" in scenario:
        actual = (pub_result or {}).get("discovered_participants")
        expect("publisher_discovered_participants", actual,
               scenario["pub_discovered_max"], lambda a, w: a is not None and a <= w)
    if "pub_discovered_min" in scenario:
        actual = (pub_result or {}).get("discovered_participants")
        expect("publisher_discovered_participants", actual,
               scenario["pub_discovered_min"], lambda a, w: a is not None and a >= w)
    if "sub_discovered_min" in scenario:
        actual = (sub_result or {}).get("discovered_participants")
        expect("subscriber_discovered_participants", actual,
               scenario["sub_discovered_min"], lambda a, w: a is not None and a >= w)
    if "expect_unauthorized_min" in scenario:
        actual = (sub_result or {}).get("unauthorized_participants")
        expect("subscriber_unauthorized_participants", actual,
               scenario["expect_unauthorized_min"], lambda a, w: a is not None and a >= w)
    if "sub_unauthorized_min" in scenario:
        actual = (sub_result or {}).get("unauthorized_participants")
        expect("subscriber_unauthorized_participants", actual,
               scenario["sub_unauthorized_min"], lambda a, w: a is not None and a >= w)
    if "pub_unauthorized_min" in scenario:
        actual = (pub_result or {}).get("unauthorized_participants")
        expect("publisher_unauthorized_participants", actual,
               scenario["pub_unauthorized_min"], lambda a, w: a is not None and a >= w)

    # Log-level evidence: which layer produced the refusal. Reading the process
    # log is the only way to attribute the decision to a named plugin check.
    for role, log_name in (("subscriber", "sub.log"), ("publisher", "pub.log")):
        key = "sub_log_contains" if role == "subscriber" else "pub_log_contains"
        for fragment in scenario.get(key, []):
            text = ""
            log_path = work / log_name
            if log_path.exists():
                text = log_path.read_text(encoding="utf-8", errors="replace")
            checks.append({"check": role + "_log_contains(" + fragment + ")",
                           "actual": fragment in text,
                           "expected": True,
                           "pass": fragment in text})

    if pub_timeout:
        checks.append({"check": "publisher_completed_within_timeout", "actual": "timed out",
                       "expected": "completed", "pass": False})
    elif pub_exit != 0:
        checks.append({"check": "publisher_exit_code", "actual": pub_exit,
                       "expected": 0, "pass": False})

    passed = all(check["pass"] for check in checks) and bool(checks)
    return {
        "name": name,
        "why": scenario["why"],
        "governance": scenario["governance"],
        "subscriber": sub_result,
        "publisher": pub_result,
        "checks": checks,
        "pass": passed,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--pki", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--timeout-ms", type=int, default=16000)
    parser.add_argument("--only", default=None, help="run only scenarios whose name contains this")
    parser.add_argument("--runtime-dir", action="append", default=[],
                        help="extra directory supplying third-party runtime DLLs; repeatable")
    args = parser.parse_args()

    probe = Path(args.probe).resolve()
    pki = Path(args.pki).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    runtime_dirs = [Path(item) for item in args.runtime_dir]
    # This is a catalogue of the DLLs the probe directory holds, derived from
    # disk. It is not a list of modules a process actually loaded; the child
    # PATH is constructed so the probe directory is searched first, and the
    # digests here are what that search would find.
    probe_directory_libraries = {}
    for library in ("fastrtps-2.14.dll", "fastcdr-2.2.dll"):
        candidate = probe.parent / library
        if candidate.exists():
            probe_directory_libraries[library] = {
                "path": str(candidate), "sha256": sha256_file(candidate),
                "meaning": "catalogue entry, not a verified per-process load"}


    started = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    results = []
    base = 18000
    for scenario in SCENARIOS:
        if args.only and args.only not in scenario["name"]:
            continue
        print("== " + scenario["name"], flush=True)
        outcome = run_scenario(probe, pki, out, scenario, base, args.timeout_ms, runtime_dirs)
        base += 8
        results.append(outcome)
        print("   pass=" + str(outcome["pass"]), flush=True)

    summary = {
        "task": "CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492",
        "started_utc": started,
        "finished_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "probe": str(probe),
        "pki": str(pki),
        "runtime_dirs": [str(item) for item in runtime_dirs],
        "probe_directory_libraries": probe_directory_libraries,
        "scenarios_total": len(results),
        "scenarios_passed": sum(1 for item in results if item["pass"]),
        "scenarios": results,
    }
    (out / "SECURITY_MATRIX_RESULT.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(json.dumps({"scenarios_total": summary["scenarios_total"],
                      "scenarios_passed": summary["scenarios_passed"]}))
    return 0 if summary["scenarios_passed"] == summary["scenarios_total"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
