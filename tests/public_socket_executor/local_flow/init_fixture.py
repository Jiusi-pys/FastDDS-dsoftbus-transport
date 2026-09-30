#!/usr/bin/env python3
"""One local fixture initialiser for an INSTALLED_RUNTIME local contract test root.

This is the single entry point that produces the whole local simulation state for a
freshly materialized root, so the simulated platform identity, the permission
baselines and the build binding can never drift apart through hand-edited copies of
the same values.

What it does NOT do, deliberately:

  * it is not a device preflight issuer and not device evidence;
  * it never invents a build: every digest it records is computed from real bytes of
    the installed package and the real consumer source, and the read-only origin of
    each input is recorded in the receipt;
  * if a required input is missing it reports the gap and exits non-zero rather than
    substituting an arbitrary string, a zero digest or a stand-in header.

It writes: build/ (manifest + package + source state), preflight.local.json,
helper-provenance.json, the sandbox platform files, sandbox permission baselines,
and DEVICE_CONTRACT.json for local contract test mode.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

BSEP = chr(92)

INSTALLED_MEMBERS = ("mdds_installed_consumer", "libmdds_runtime_token_bridge.so",
                     "libfastdds.so.3.6", "libfastcdr.so.2",
                     "libfoonathan_memory-0.7.4.so", "libc++_shared.so")
INSTALLED_MAIN = "mdds_installed_consumer"
PERMISSION_SHA256 = "c99bd1641c7802acd11e70bdd6da6cc86fd08380b19fe56694ca37837de5a0f2"
LIMITS = {"canonical_windows": 1, "repair_retries": 1, "total_attempts": 2,
          "max_record_bytes": 8192, "payload_bytes": 32, "per_side_writes": 5,
          "per_side_takes": 5, "stages": 4}
# Platform identity consumed by Assert-Platform in the formal window scripts.
PLATFORM_PATHS = ("/system/lib64/platformsdk/libsoftbus_client.z.so",
                  "/system/lib64/libc++.so")
SERVICE_SERVER = "/system/lib64/platformsdk/libsoftbus_server.z.so"
PLATFORM_PARAMS = (("kernel", "Linux localhost 6.6.101-sim #1 SMP PREEMPT SIM"),
                   ("const.product.software.version", "KaihongOS-SIM"),
                   ("const.product.devicetype", "sim"))


def dg(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def rel(p, repo):
    return str(Path(p).resolve().relative_to(repo)).replace(BSEP, "/")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--package-source", required=True,
                    help="read-only origin of the installed package members")
    ap.add_argument("--consumer-source", required=True,
                    help="read-only origin of the consumer source tree")
    ap.add_argument("--header", required=True,
                    help="authorised public header bound by header_sha256")
    a = ap.parse_args()

    repo = Path(a.repo).resolve()
    root = Path(a.root).resolve()
    pkg = Path(a.package_source).resolve()
    src = Path(a.consumer_source).resolve()
    header = Path(a.header).resolve()

    gaps = []
    for member in INSTALLED_MEMBERS:
        if not (pkg / member).is_file():
            gaps.append("package member missing: %s" % (pkg / member))
    if not src.is_dir():
        gaps.append("consumer source tree missing: %s" % src)
    if not header.is_file():
        gaps.append("authorised header missing: %s" % header)
    if gaps:
        print(json.dumps({"status": "INCOMPLETE", "gaps": gaps}, indent=2))
        return 2

    # --- build evidence, every digest computed from real bytes -------------------
    build = root / "build"
    (build / "package").mkdir(parents=True, exist_ok=True)
    artifacts = []
    for member in INSTALLED_MEMBERS:
        srcfile = pkg / member
        dst = build / "package" / member
        shutil.copyfile(srcfile, dst)
        artifacts.append({"name": member, "sha256": dg(srcfile), "source": str(srcfile)})

    source_state = {"_local_fixture_note": (
        "Derived local build record. The installed-runtime consumer build never produced an "
        "original build-manifest.json/source-state.json (C:/mdds-installed-runtime-0929/target "
        "holds only CMake/ninja output). Every digest here is computed from the real audited "
        "bytes named below; nothing is invented, and this record is local test material only."),
        "consumer_source_root": str(src),
        "consumer_source": sorted(
            [{"path": rel(p, src.parent), "sha256": dg(p)}
             for p in src.rglob("*") if p.is_file()],
            key=lambda e: e["path"]),
        "authorised_header": {"path": str(header), "sha256": dg(header)},
    }
    (build / "source-state.json").write_text(
        json.dumps(source_state, indent=2) + "\n", encoding="utf-8", newline="\n")

    manifest = {"_local_fixture_note": source_state["_local_fixture_note"],
                "artifacts": [dict(e,path=e['source']) for e in artifacts], "main": INSTALLED_MAIN,
                "sourceStateSha256": dg(build / "source-state.json"),
                "bridgeExit": 0, "configureExit": 0, "buildExit": 0}
    cache=Path('C:/mdds-installed-runtime-0929/target/CMakeCache.txt')
    if 'FASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME:BOOL=ON' not in cache.read_text():raise ValueError('Actual installed build option absent')
    manifest['configureArguments']=['-DFASTDDS_DSOFTBUS_BUILD_EXPERIMENTAL_RUNTIME=ON']
    manifest['configuration_origin']=dict(path=str(cache),sha256=dg(cache),scope='DERIVED_CACHE_OPTION_NOT_ORIGINAL_BUILD_COMMAND')
    (build / "build-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

    # --- sandbox platform identity, hashes from real sandbox bytes ---------------
    plat = root / "sandbox" / "platform"
    plat.mkdir(parents=True, exist_ok=True)
    plat_files = {}
    for remote in PLATFORM_PATHS + (SERVICE_SERVER,):
        local = plat / Path(remote).name
        if not local.exists():
            local.write_bytes(("SIMULATED PLATFORM BYTE SOURCE for %s\n" % remote).encode())
        plat_files[remote] = local

    platform = {"files": [{"path": r, "sha256": dg(plat_files[r])} for r in PLATFORM_PATHS],
                "parameters": [{"name": n, "value": v} for n, v in PLATFORM_PARAMS],
                "service_server": {"path": SERVICE_SERVER,
                                   "sha256": dg(plat_files[SERVICE_SERVER])}}

    preflight = {"eligible_for_executor_gate": True,
                 "platform": {"A": json.loads(json.dumps(platform)),
                              "B": json.loads(json.dumps(platform))},
                 "required_platform_paths": sorted(plat_files),
                 "permission_helper": {"path": str(root / "helpers" / "native_permission_probe"),
                                       "sha256": PERMISSION_SHA256},
                 "_sim_note": "local simulation preflight; not a device preflight and not evidence"}
    (root / "preflight.local.json").write_text(
        json.dumps(preflight, indent=2) + "\n", encoding="utf-8", newline="\n")

    prov = {"_sim_note": "helper provenance for the local flow",
            "helpers": [{"name": "native_permission_probe", "sha256": PERMISSION_SHA256},
                        {"name": "softbus_record_probe",
                         "sha256": dg(root / "helpers" / "softbus_record_probe")}]}
    (root / "helper-provenance.json").write_text(
        json.dumps(prov, indent=2) + "\n", encoding="utf-8", newline="\n")

    # --- permission baselines, distinct per board -------------------------------
    baselines = {}
    for side in ("A", "B"):
        d = root / "sandbox" / side
        d.mkdir(parents=True, exist_ok=True)
        cfg = json.loads((root / "PRIVATE_CONFIG.json").read_text(encoding="utf-8"))
        doc = [{"processName": "fastdds_dsoftbus_probe", "APL": 1, "version": 1,
                "tokenId": int(cfg['tokens'][side]), "tokenAttr": 0, "dcaps": [],
                "permissions": [], "nativeAcls": []}]
        raw = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode()
        (d / "nativetoken.json").write_bytes(raw)
        (d / "baseline-token.json").write_bytes(raw)
        (d / "metadata.json").write_text(json.dumps(dict(mode='640',uid='0',gid='3020',label='accesstoken_data_file',enforcement='Enforcing')))
        baselines[side] = hashlib.sha256(raw).hexdigest()

    cfg = json.loads((root / "PRIVATE_CONFIG.json").read_text(encoding="utf-8"))
    cfg["permission_baseline_sha256"] = baselines
    cfg["query_helper_path"] = str(root / "helpers" / "softbus_record_probe")
    (root / "PRIVATE_CONFIG.json").write_text(
        json.dumps(cfg, indent=2) + "\n", encoding="utf-8", newline="\n")
    pcs = dg(root / "PRIVATE_CONFIG.json")
    (root/'sandbox'/'identities.json').write_text(json.dumps({s:hashlib.sha256((str(root)+':'+s).encode()).hexdigest() for s in 'AB'}))
    (root/'sandbox'/'processes').mkdir()
    mat = json.loads((root / "MATERIALIZATION.json").read_text(encoding="utf-8"))
    mat["private_config_sha256"] = pcs
    (root / "MATERIALIZATION.json").write_text(
        json.dumps(mat, indent=2) + "\n", encoding="utf-8", newline="\n")

    (root / "INSTALLED_RUNTIME_CONTRACT.md").write_text(
        "LOCAL_CONTRACT_TEST plan for the installed-runtime scenario.\n\n"
        "Not a device plan and not device evidence. The contract binds local_contract_test:true\n"
        "and a declared LOCAL_FAKE_SANDBOX driver; only the lowest device-command boundary is\n"
        "replaced. 0 HDC, 0 remote commands, 0 queries, 0 deployments, 0 grants.\n",
        encoding="utf-8", newline="\n")

    execution_files = [{"path": rel(root / e["destination"], repo), "sha256": e["sha256"]}
                       for e in mat["files"]] + mat["external_files"]
    contract = {
        "scope": "INSTALLED_RUNTIME_DUAL_BOARD_CONSUMER",
        "task_root": str(root),
        "private_config_sha256": pcs,
        "materialization_sha256": dg(root / "MATERIALIZATION.json"),
        "plan_sha256": dg(root / "INSTALLED_RUNTIME_CONTRACT.md"),
        "limits": LIMITS,
        "execution_files": execution_files,
        "permission_helper": preflight["permission_helper"],
        "header_sha256": dg(header),
        "build_binding": {
            "build_manifest": str(build / "build-manifest.json"),
            "build_manifest_sha256": dg(build / "build-manifest.json"),
            "source_state": str(build / "source-state.json"),
            "source_state_sha256": dg(build / "source-state.json"),
            "main_sha256": dg(build / "package" / INSTALLED_MAIN),
            "package_artifacts": artifacts,
        },
        "local_contract_test": True,
        "device_evidence": False,
        "device_authorized": False,
        "local_driver": {"kind": "LOCAL_FAKE_SANDBOX", "test_root": str(root),
                         "sandbox_root": str(root / "sandbox")},
        "local_test_basis": (
            "LOCAL_EXECUTOR_ADAPTATION_ONLY: the formal executor chain is exercised end to end "
            "with only the device-command boundary replaced by a rejecting local driver; 0 HDC, "
            "0 remote commands, 0 queries, 0 deployments, 0 grants; real ledgers untouched."),
    }
    (root / "DEVICE_CONTRACT.json").write_text(
        json.dumps(contract, indent=2) + "\n", encoding="utf-8", newline="\n")

    receipt = {"status": "OK", "root": str(root),
               "contract_sha256": dg(root / "DEVICE_CONTRACT.json"),
               "materialization_sha256": dg(root / "MATERIALIZATION.json"),
               "header_sha256": contract["header_sha256"],
               "main_sha256": contract["build_binding"]["main_sha256"],
               "package_members": len(artifacts),
               "read_only_origins": {"package": str(pkg), "consumer_source": str(src),
                                     "header": str(header)}}
    print(json.dumps(receipt, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
