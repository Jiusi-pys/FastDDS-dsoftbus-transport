"""A2 SSH dual-board matrix; runtime roots must already be deployed and isolated.

Stock/off XCDR2 failures are measured controls, never conformance passes.
Every case retains command, exit, probe observations and runtime hashes.
"""
import argparse
import concurrent.futures as futures
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time
import uuid


# Optional per-invocation OpenSSH configuration; no user-global SSH edits.
SSH_CONFIG = None


def ssh(board, command, timeout=35):
    config = ["-F", str(SSH_CONFIG)] if SSH_CONFIG is not None else []
    return subprocess.run(["ssh", "-n", *config, "-o", "BatchMode=yes", "-o", "ConnectTimeout=10",
                           "root@" + board, command], capture_output=True, timeout=timeout)


def observations(output):
    rows = []
    for line in output.decode(errors="replace").splitlines():
        try:
            if not line.startswith("DDS_STANDARDS_RESULT "):
                continue
            row = json.loads(line[len("DDS_STANDARDS_RESULT "):])
            if not isinstance(row, dict):
                return None
            rows.append(row)
        except ValueError:
            return None
    return rows[0] if len(rows) == 1 else None


def run_remote_peer(board, command):
    try:
        result = ssh(board, command)
        result.timed_out = False
        return result
    except subprocess.TimeoutExpired as error:
        # Preserve partial execution evidence instead of losing it in a traceback.
        result = subprocess.CompletedProcess(command, 124, error.stdout or b"",
                                             (error.stderr or b"") + b"\nA2 SSH timeout after 35 seconds\n")
        result.timed_out = True
        return result


def run_case(args, mode, pair, reverse, representation, domain):
    boards = [args.a, args.b][::(-1 if reverse else 1)]
    versions = pair.split(":")
    name = f"{mode}-{pair.replace(':','w-')}r-{'BA' if reverse else 'AB'}-{representation}"
    gate = "1" if mode == "on" else "0"
    roots = [args.modern if v == "3.6.2" else args.stock if mode == "stock" else args.candidate for v in versions]
    topic = "A2_" + uuid.uuid4().hex
    commands = {}
    for role, root, seconds in [("writer", roots[0], 9), ("reader", roots[1], 12)]:
        commands[role] = (f"cd {shlex.quote(root)} && LD_LIBRARY_PATH=$PWD/lib "
                          f"FASTDDS_XCDR2_DYNAMIC_FRAMING={gate} FASTDDS_XTYPES_HONOR_TYPE_CONSISTENCY=0 "
                          f"DDS_STANDARDS_FORCE_TYPE_VALIDATION=0 ./bin/dds_type_probe "
                          f"identical_{representation} {role} {domain} {topic} {seconds}")
    with futures.ThreadPoolExecutor(max_workers=2) as pool:
        reader = pool.submit(run_remote_peer, boards[1], commands["reader"])
        time.sleep(2)
        writer = pool.submit(run_remote_peer, boards[0], commands["writer"])
        results = {"writer": writer.result(), "reader": reader.result()}
    peers = {}
    for role, result in results.items():
        (args.out / (name + "-" + role + ".out")).write_bytes(result.stdout)
        (args.out / (name + "-" + role + ".err")).write_bytes(result.stderr)
        version = versions[0 if role == "writer" else 1]
        expected_identity = dict(case="identical_" + representation, role=role,
                                 fastdds="3.6.2.0" if version == "3.6.2" else version,
                                 transport="udp4", expectation="delivery")
        observation = observations(result.stdout)
        identity_ok = observation is not None and all(observation.get(key) == value
                                                       for key, value in expected_identity.items())
        peers[role] = {"command": commands[role], "exit": result.returncode, "timed_out": result.timed_out,
                       "observation": observation, "expected_identity": expected_identity,
                       "identity_ok": identity_ok}
    # A live, cleanly closed DDS control is necessary even for expected stock failures.
    live = all(p["identity_ok"] and p["observation"].get("cleanup_ok") is True and
               p["observation"].get("peer_alive") is True for p in peers.values())
    delivery = live and all(p["exit"] == 0 and p["observation"].get("passed") is True and
                           p["observation"].get("matched") is True for p in peers.values())
    reader_count = peers["reader"]["observation"].get("received") if peers["reader"]["observation"] else None
    delivery = delivery and type(reader_count) is int and reader_count > 0
    expected_delivery = mode == "on" or representation == "xcdr1" or pair == "2.14.5:2.14.5"
    # Off mixed XCDR2 must reproduce its specific failure rather than merely fail somehow.
    reader_exit = 1 if versions[0] == "2.14.5" else 2
    writer_live = (peers["writer"]["identity_ok"] and
                   all(peers["writer"]["observation"].get(key) is True
                       for key in ("peer_alive", "cleanup_ok", "matched", "passed")))
    if reader_exit == 1:
        reader_control = (live and type(reader_count) is int and reader_count == 0 and
                          peers["reader"]["observation"].get("matched") is True and
                          peers["reader"]["observation"].get("passed") is False)
    else:
        # This historical value-check exception precedes the reader's JSON result.
        # Retain it as an incompatibility control, without claiming clean reader shutdown.
        reader_control = (b"DDS_STANDARDS_RESULT " not in results["reader"].stdout and
                          b"DDS_STANDARDS_ERROR sample value mismatch" in results["reader"].stderr)
    control = writer_live and reader_control and peers["writer"]["exit"] == 0 and peers["reader"]["exit"] == reader_exit
    blocked = any(p["timed_out"] for p in peers.values())
    result = dict(name=name, mode=mode, gate_value=gate, gate_on=gate == "1", versions=versions, boards=boards,
                  roots=roots, representation=representation, peers=peers,
                  delivery_status="BLOCKED" if blocked else "PASS" if delivery else "FAIL",
                  expected="delivery" if expected_delivery else "recorded stock incompatibility",
                  validation="BLOCKED" if blocked else "PASS" if (delivery if expected_delivery else control) else "FAIL")
    print(name, result["delivery_status"], result["validation"], flush=True)
    return result


def main():
    global SSH_CONFIG
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a", required=True)
    parser.add_argument("--b", required=True)
    parser.add_argument("--candidate", required=True, help="Remote 2.14.5 root with bin/ and lib/")
    parser.add_argument("--stock", required=True, help="Remote pristine 2.14.5 root")
    parser.add_argument("--modern", required=True, help="Remote 3.6.2 root")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--ssh-config", type=Path,
                        help="Task-specific OpenSSH config (for example, pinned LAN host aliases)")
    parser.add_argument("--domain", type=int, default=210)
    parser.add_argument("--jobs", type=int, choices=(1, 2, 3), default=1,
                        help="Concurrent DDS pairs; the validated board configuration uses 1")
    parser.add_argument("--modes", nargs="+", choices=["stock", "off", "on"], default=["stock", "off", "on"])
    args = parser.parse_args()
    SSH_CONFIG = args.ssh_config.resolve() if args.ssh_config is not None else None
    if SSH_CONFIG is not None and not SSH_CONFIG.is_file():
        parser.error("--ssh-config must name an existing file")
    ssh_configuration = (dict(path=str(SSH_CONFIG), sha256=hashlib.sha256(SSH_CONFIG.read_bytes()).hexdigest())
                         if SSH_CONFIG is not None else None)
    if args.a == args.b or not 0 <= args.domain <= 230:
        parser.error("distinct boards and domain 0..230 required")
    args.out.mkdir(parents=True, exist_ok=False)
    inventory = {}
    for board in (args.a, args.b):
        for root in (args.candidate, args.stock, args.modern):
            result = ssh(board, f"uname -a; cd {shlex.quote(root)} && sha256sum bin/dds_type_probe lib/*")
            inventory[board + ":" + root] = dict(exit=result.returncode, output=result.stdout.decode(errors="replace"))
            if result.returncode:
                raise RuntimeError("runtime inventory failed")
    cases = []
    # Keep the full matrix; serialize pairs by default to avoid board contention.
    for mode in args.modes:
        for reverse in (False, True):
            for rep in ("xcdr1", "xcdr2"):
                pairs = ("2.14.5:2.14.5", "2.14.5:3.6.2", "3.6.2:2.14.5")
                if args.jobs == 1:
                    for i, pair in enumerate(pairs):
                        row = run_case(args, mode, pair, reverse, rep, args.domain+i)
                        cases.append(row)
                        if row["validation"] == "BLOCKED":
                            break
                else:
                    with futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                        tasks = [pool.submit(run_case, args, mode, pair, reverse, rep, args.domain+i)
                                 for i, pair in enumerate(pairs)]
                        cases.extend(t.result() for t in tasks)
                # Durable progress even if a later SSH call fails.
                (args.out / "results.json").write_text(json.dumps(dict(schema_version=3, inventory=inventory, cases=cases,
                    gate_variable="FASTDDS_XCDR2_DYNAMIC_FRAMING", transport="udp4",
                    runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    ssh_config=ssh_configuration,
                    status="BLOCKED" if any(c["validation"] == "BLOCKED" for c in cases) else
                    "PASS" if all(c["validation"] == "PASS" for c in cases) else "FAIL"), indent=2))
                if any(c["validation"] == "BLOCKED" for c in cases):
                    return 2  # Stop further batches; a timed-out remote process may remain.
    return 0 if all(c["validation"] == "PASS" for c in cases) else 1


if __name__ == "__main__":
    raise SystemExit(main())
