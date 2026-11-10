"""Generate the bounded IDL fixture with a version-bound Fast DDS-Gen JAR."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from run_type_baseline import sha256


GENERATORS = {"2.14.5": "3.3.1", "3.6.2": "4.3.0"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--java", type=Path, required=True)
    parser.add_argument("--generator", type=Path, required=True)
    parser.add_argument("--fastdds", choices=GENERATORS, required=True)
    parser.add_argument("--generator-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not args.java.is_file() or not args.generator.is_file():
        parser.error("java and generator must be existing files")
    if not re.fullmatch(r"[a-fA-F0-9]{64}", args.generator_sha256):
        parser.error("generator SHA256 must contain 64 hexadecimal digits")
    digest = sha256(args.generator)
    if digest != args.generator_sha256.lower():
        parser.error("generator SHA256 mismatch")
    version_command = [str(args.java.resolve()), "-jar", str(args.generator.resolve()), "-version"]
    expected = GENERATORS[args.fastdds]
    source = Path(__file__).resolve().parents[2] / "integrations/standards/idl/BoundedRecord.idl"
    args.out.mkdir(parents=True, exist_ok=False)
    destination = args.out.resolve()
    command = [str(args.java.resolve()), "-jar", str(args.generator.resolve()), "-ppDisable",
               "-flat-output-dir", "-d", str(destination), str(source)]
    evidence = {"fastdds": args.fastdds, "generator_version": expected,
                "generator_sha256": digest, "java_sha256": sha256(args.java),
                "idl_sha256": sha256(source), "command": command, "stages": [],
                "exit_code": None, "status": "BLOCKED"}
    for stage, argv, timeout in (("version", version_command, 30), ("generate", command, 60)):
        stdout, stderr = b"", b""
        result_code = None
        try:
            result = subprocess.run(argv, capture_output=True, timeout=timeout)
            stdout, stderr, result_code = result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired as error:
            stdout, stderr = error.stdout or b"", error.stderr or b""
            evidence["error"] = f"{stage} timeout"
        except OSError as error:
            evidence["error"] = str(error)
        (destination / f"{stage}.stdout.log").write_bytes(stdout)
        (destination / f"{stage}.stderr.log").write_bytes(stderr)
        evidence["stages"].append({"stage": stage, "command": argv, "exit_code": result_code})
        evidence["exit_code"] = result_code
        if result_code is None:
            break
        if result_code != 0:
            evidence["status"] = "FAIL"
            break
        if stage == "version" and not re.search(
                r"fastddsgen version " + re.escape(expected) + r"\s*$", (stdout + stderr).decode("utf-8", "replace")):
            evidence["error"] = f"Fast DDS {args.fastdds} requires Fast DDS-Gen {expected}"
            evidence["status"] = "FAIL"
            break
        if stage == "generate":
            evidence["status"] = "PASS"
    evidence["generated"] = {str(path.relative_to(destination)): sha256(path)
                             for path in sorted(destination.rglob("*"))
                             if path.is_file() and path.suffix in (".h", ".hpp", ".cxx", ".ipp")}
    if evidence["status"] == "PASS" and not evidence["generated"]:
        evidence["status"] = "FAIL"
    (destination / "generation.json").write_text(json.dumps(evidence, indent=2), encoding="utf-8")
    print(evidence["status"])
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[evidence["status"]]


if __name__ == "__main__":
    raise SystemExit(main())
