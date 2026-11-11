"""Regression checks for evidence acceptance, independent of a DDS installation."""
import importlib.util
import json
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[1] / "scripts/standards/run_type_baseline.py"
SPEC = importlib.util.spec_from_file_location("standards_runner", SOURCE)
runner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(runner)
sys.path.insert(0, str(SOURCE.parent))
import generate_types


class EvidenceTests(unittest.TestCase):
    def invoke(self, stdout, exit_code=0):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe identity")
            process = subprocess.CompletedProcess([], exit_code, stdout.encode(), b"")
            with patch.object(runner.subprocess, "run", return_value=process):
                return runner.run_peer(executable, "3.6.2", "reader", "identical_xcdr2", 171,
                                       "topic", 1, [], Path(directory) / "record")

    def observation(self, **overrides):
        record = dict(case="identical_xcdr2", role="reader", fastdds="3.6.2.0",
                      transport="udp4", expectation="delivery", basis="xtypes",
                      matched=True, received=3, passed=True, cleanup_ok=True, peer_alive=True)
        record.update(overrides)
        return "DDS_STANDARDS_RESULT " + json.dumps(record)

    def test_accepts_actual_upstream_version_string(self):
        self.assertEqual(self.invoke(self.observation())["status"], "PASS")

    def test_rejects_wrong_version(self):
        self.assertEqual(self.invoke(self.observation(fastdds="3.6.1.0"))["status"], "FAIL")

    def test_rejects_wrong_role(self):
        self.assertEqual(self.invoke(self.observation(role="writer"))["status"], "FAIL")

    def test_rejects_duplicate_observations(self):
        self.assertEqual(self.invoke(self.observation() + "\n" + self.observation())["status"], "FAIL")

    def test_rejects_nonzero_exit_with_success_observation(self):
        self.assertEqual(self.invoke(self.observation(), 1)["status"], "FAIL")

    def test_loader_failure_is_blocked(self):
        self.assertEqual(self.invoke("", 0xC0000135)["status"], "BLOCKED")

    def test_rejects_empty_output(self):
        self.assertEqual(self.invoke("")["status"], "FAIL")

    def test_rejects_a_probe_that_redeclares_what_the_case_should_do(self):
        """CONSIDER 6: the probe must not be able to grade itself.

        `identical_xcdr2` is a delivery case in the runner's own table. A probe
        claiming it is a rejection case would otherwise be able to turn a
        non-delivery into a PASS, which AGENTS.md section 5.3 forbids.
        """
        record = self.invoke(self.observation(expectation="rejection"))
        self.assertEqual(record["status"], "FAIL")
        self.assertIn("requires", record["error"])

    def test_rejects_a_probe_that_omits_its_expectation(self):
        record = self.invoke(self.observation(expectation=None))
        self.assertEqual(record["status"], "FAIL")

    def test_records_the_gate_state_on_every_peer(self):
        """SHOULD_FIX 3: results must say which configuration produced them."""
        with patch.dict(runner.os.environ, {runner.GATE_VARIABLE: "1"}):
            record = self.invoke(self.observation())
        self.assertEqual(record["gate_value"], "1")
        self.assertTrue(record["gate_on"])
        environment = dict(runner.os.environ)
        environment.pop(runner.GATE_VARIABLE, None)
        with patch.dict(runner.os.environ, environment, clear=True):
            record = self.invoke(self.observation())
        self.assertIsNone(record["gate_value"])
        self.assertFalse(record["gate_on"])

    def test_rejects_cleanup_failure(self):
        self.assertEqual(self.invoke(self.observation(cleanup_ok=False))["status"], "FAIL")

    def test_rejects_missing_concurrent_peer_control(self):
        self.assertEqual(self.invoke(self.observation(peer_alive=False))["status"], "FAIL")

    def test_no_peer_is_blocked(self):
        self.assertEqual(self.invoke(self.observation(peer_alive=False, passed=False), 3)["status"], "BLOCKED")

    def test_invalid_json_retains_raw_logs(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe")
            stdout = b"diagnostic\nDDS_STANDARDS_RESULT {broken\n"
            stderr = b"native diagnostic\n"
            with patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout, stderr)):
                result = runner.run_peer(executable, "3.6.2", "reader", "xml", 171, "topic", 1,
                                         [], Path(directory) / "record")
            self.assertEqual(result["status"], "FAIL")
            self.assertEqual(Path(result["stdout"]).read_bytes(), stdout)
            self.assertEqual(Path(result["stderr"]).read_bytes(), stderr)

    def test_generator_timeout_retains_partial_output(self):
        with tempfile.TemporaryDirectory() as directory:
            jar = Path(directory) / "generator.jar"
            jar.write_bytes(b"generator fixture")
            destination = Path(directory) / "generated"
            arguments = ["generate_types.py", "--java", sys.executable, "--generator", str(jar),
                         "--generator-sha256", runner.sha256(jar), "--fastdds", "3.6.2", "--out", str(destination)]
            version = subprocess.CompletedProcess([], 0, b"fastddsgen version 4.3.0\n", b"")
            expired = subprocess.TimeoutExpired([], 60, output=b"partial generated output\n", stderr=b"partial error\n")
            with patch.object(sys, "argv", arguments), patch.object(generate_types.subprocess, "run", side_effect=[version, expired]):
                self.assertEqual(generate_types.main(), 2)
            result = json.loads((destination / "generation.json").read_text(encoding="utf-8"))
            self.assertEqual(result["status"], "BLOCKED")
            self.assertIsNone(result["exit_code"])
            self.assertEqual((destination / "generate.stdout.log").read_bytes(), b"partial generated output\n")
            self.assertEqual((destination / "generate.stderr.log").read_bytes(), b"partial error\n")


class ShadowedLibraryTests(unittest.TestCase):
    """Guards the defect that invalidated the 2026-11-10 mixed-version evidence.

    The device orchestration gave a Fast DDS 2.14.5 process a search path with
    Fast CDR 2.3.7 ahead of Fast CDR 2.2.5. Both ship `libfastcdr.so.2`, so the
    loader substituted an incompatible ABI and the process died in CDR
    serialization with SIGSEGV. Reproduced 20/20 with no peer running, against
    0/20 on the same binary with its own library directory alone.
    """

    def layout(self, directory):
        root = Path(directory)
        v214, v36, support = root / "v214", root / "v36", root / "support"
        for path in (v214, v36, support):
            path.mkdir()
        (v214 / "libfastcdr.so.2").write_bytes(b"fastcdr-2.2.5")
        (v36 / "libfastcdr.so.2").write_bytes(b"fastcdr-2.3.7")
        (v214 / "libfastrtps.so.2.14").write_bytes(b"fastrtps-2.14.5")
        (v36 / "libfastdds.so.3.6").write_bytes(b"fastdds-3.6.2")
        (v214 / "libssl.so.3").write_bytes(b"openssl-3.3.2")
        (support / "libssl.so.3").write_bytes(b"openssl-3.3.2")
        return v214, v36, support

    def test_reports_differing_content_under_one_soname(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, v36, _ = self.layout(directory)
            conflicts = runner.shadowed_libraries([v36, v214])
            self.assertEqual([entry[0] for entry in conflicts], ["libfastcdr.so.2"])
            soname, first, first_digest, second, second_digest = conflicts[0]
            self.assertEqual(first, v36 / "libfastcdr.so.2")
            self.assertEqual(second, v214 / "libfastcdr.so.2")
            self.assertNotEqual(first_digest, second_digest)

    def test_accepts_identical_support_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, _, support = self.layout(directory)
            self.assertEqual(runner.shadowed_libraries([v214, support]), [])

    def test_accepts_a_single_isolated_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, _, _ = self.layout(directory)
            self.assertEqual(runner.shadowed_libraries([v214]), [])

    def test_reports_windows_dll_shadowing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            for path in (first, second):
                path.mkdir()
            (first / "fastcdr-2.2.dll").write_bytes(b"fastcdr-2.2.5")
            (second / "fastcdr-2.2.dll").write_bytes(b"fastcdr-2.3.7")
            self.assertEqual([entry[0] for entry in runner.shadowed_libraries([first, second])],
                             ["fastcdr-2.2.dll"])

    def test_main_refuses_a_shadowing_runtime_path(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, v36, _ = self.layout(directory)
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe identity")
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "2.14.5", "--reader-version", "3.6.2",
                         "--writer-runtime-dir", str(v36), "--writer-runtime-dir", str(v214),
                         "--out", str(Path(directory) / "evidence")]
            with patch.object(sys, "argv", arguments):
                with self.assertRaises(SystemExit) as raised:
                    runner.main()
            # argparse errors exit 2, and no evidence directory may be created.
            self.assertEqual(raised.exception.code, 2)
            self.assertFalse((Path(directory) / "evidence").exists())

    def test_acknowledgement_suppresses_one_name(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, v36, _ = self.layout(directory)
            self.assertEqual(runner.shadowed_libraries([v36, v214], ["libfastcdr.so.2"]), [])
            # An unrelated acknowledgement must not suppress the real conflict.
            self.assertEqual([e[0] for e in runner.shadowed_libraries([v36, v214], ["libssl.so.3"])],
                             ["libfastcdr.so.2"])
            # A truncated stem is not the loader key, so it must not suppress it.
            self.assertEqual([e[0] for e in runner.shadowed_libraries([v36, v214], ["libfastcdr.so"])],
                             ["libfastcdr.so.2"])

    def test_distinct_sonames_are_not_a_conflict(self):
        """Regression: libfastcdr.so.2 and libfastcdr.so.3 do not shadow.

        Keying on a truncated stem collapsed these into a false conflict, which
        pushed the operator towards acknowledging a broad name, and so towards
        suppressing a real ABI mismatch sharing that stem.
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            for path in (first, second):
                path.mkdir()
            (first / "libfastcdr.so.2").write_bytes(b"fastcdr-2.2.5")
            (second / "libfastcdr.so.3").write_bytes(b"fastcdr-3.0.0")
            self.assertEqual(runner.shadowed_libraries([first, second]), [])

    def test_pinned_acknowledgement_tracks_the_loaded_copy(self):
        """A pinned acknowledgement must not outlive the library it was written for."""
        with tempfile.TemporaryDirectory() as directory:
            v214, v36, _ = self.layout(directory)
            loaded = runner.sha256(v36 / "libfastcdr.so.2")
            hidden = runner.sha256(v214 / "libfastcdr.so.2")
            # Pinned to the copy that actually loads: suppressed.
            self.assertEqual(
                runner.shadowed_libraries([v36, v214], ["libfastcdr.so.2=" + loaded[:12]]), [])
            # Pinned to the copy that is hidden rather than loaded: still reported.
            self.assertEqual(
                [e[0] for e in runner.shadowed_libraries([v36, v214], ["libfastcdr.so.2=" + hidden[:12]])],
                ["libfastcdr.so.2"])
            # Pinned to a digest belonging to neither: still reported.
            self.assertEqual(
                [e[0] for e in runner.shadowed_libraries([v36, v214], ["libfastcdr.so.2=deadbeef"])],
                ["libfastcdr.so.2"])

    def test_posix_platform_runtimes_ignored_but_cxx_runtime_is_not(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            for path in (first, second):
                path.mkdir()
            for path, content in ((first, b"one"), (second, b"two")):
                (path / "libc.so.6").write_bytes(content)
                (path / "ld-musl-aarch64.so.1").write_bytes(content)
                (path / "libpthread.so.0").write_bytes(content)
                # A pinned C++ ABI boundary, and a library whose name merely
                # begins with the same letters as libc.
                (path / "libstdc++.so.6").write_bytes(content)
                (path / "libcrypto.so.3").write_bytes(content)
            self.assertEqual(sorted(e[0] for e in runner.shadowed_libraries([first, second])),
                             ["libcrypto.so.3", "libstdc++.so.6"])

    def test_acknowledgement_is_recorded_in_the_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            v214, v36, _ = self.layout(directory)
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe identity")
            out = Path(directory) / "evidence"
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "2.14.5", "--reader-version", "2.14.5",
                         "--writer-runtime-dir", str(v36), "--writer-runtime-dir", str(v214),
                         "--allow-shadowed", "libfastcdr.so.2", "--cases", "identical_xcdr2",
                         "--seconds", "1", "--out", str(out)]
            process = subprocess.CompletedProcess([], 3, b"", b"")
            with patch.object(sys, "argv", arguments), patch.object(runner.subprocess, "run", return_value=process):
                runner.main()
            evidence = json.loads((out / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(evidence["acknowledged_shadowed_libraries"],
                             [{"library": "libfastcdr.so.2", "pinned_digest": None, "pinned": False}])
            detail = evidence["shadowed_library_detail"]["writer"]
            self.assertEqual([entry["library"] for entry in detail], ["libfastcdr.so.2"])
            # The evidence must name which copy actually loads, not merely that a clash exists.
            self.assertEqual(detail[0]["loaded"], str(v36 / "libfastcdr.so.2"))
            self.assertEqual(detail[0]["shadowed"], str(v214 / "libfastcdr.so.2"))
            self.assertNotEqual(detail[0]["loaded_sha256"], detail[0]["shadowed_sha256"])

    def test_ignores_compiler_redistributables_but_not_abi_libraries(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            for path in (first, second):
                path.mkdir()
            for path, content in ((first, b"build-one"), (second, b"build-two")):
                # Several copies of these live in any Windows toolchain environment.
                (path / "msvcp140.dll").write_bytes(content)
                (path / "vcruntime140_1.dll").write_bytes(content)
                (path / "concrt140.dll").write_bytes(content)
                (path / "api-ms-win-crt-runtime-l1-1-0.dll").write_bytes(content)
                # These carry a pinned ABI and must still be reported.
                (path / "fastcdr-2.2.dll").write_bytes(content)
                (path / "libc++_shared.so").write_bytes(content)
            self.assertEqual(sorted(entry[0] for entry in runner.shadowed_libraries([first, second])),
                             ["fastcdr-2.2.dll", "libc++_shared.so"])


class TerminationTests(unittest.TestCase):
    """A crash must never be filed as an ordinary non-match.

    The 2026-11-10 device evidence recorded a SIGSEGV alongside type-matching
    failures, which pointed the investigation at the Fast DDS dynamic type path
    instead of at the library search order that actually caused it.
    """

    def test_ordinary_probe_exits_are_not_crashes(self):
        for code in (0, 1, 2, 3, 255):
            self.assertIsNone(runner.describe_termination(code), code)
        self.assertIsNone(runner.describe_termination(None))

    def test_sigsegv_is_named_and_mapped_to_shell_139(self):
        termination = runner.describe_termination(-11)
        self.assertEqual(termination["kind"], "signal")
        self.assertEqual(termination["signal"], 11)
        self.assertEqual(termination["shell_exit_code"], 139)

    def test_windows_exception_codes_are_crashes(self):
        termination = runner.describe_termination(0xC0000005)
        self.assertEqual(termination["kind"], "windows_exception")
        self.assertEqual(termination["name"], "EXCEPTION_ACCESS_VIOLATION")
        self.assertEqual(runner.describe_termination(0xC0000374)["name"], "STATUS_HEAP_CORRUPTION")

    def test_crashed_peer_is_recorded_as_crashed(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe identity")
            process = subprocess.CompletedProcess([], -11, b"", b"")
            with patch.object(runner.subprocess, "run", return_value=process):
                record = runner.run_peer(executable, "2.14.5", "reader", "identical_xcdr2", 171,
                                         "topic", 1, [], Path(directory) / "record")
            self.assertTrue(record["crashed"])
            self.assertEqual(record["termination"]["name"], "SIGSEGV")
            self.assertEqual(record["status"], "FAIL")

    def test_loader_failure_is_still_blocked_not_crashed(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "peer.exe"
            executable.write_bytes(b"probe identity")
            process = subprocess.CompletedProcess([], 0xC0000135, b"", b"")
            with patch.object(runner.subprocess, "run", return_value=process):
                record = runner.run_peer(executable, "2.14.5", "reader", "identical_xcdr2", 171,
                                         "topic", 1, [], Path(directory) / "record")
            self.assertNotIn("crashed", record)
            self.assertEqual(record["status"], "BLOCKED")


class RuntimeLibraryClassificationTests(unittest.TestCase):
    """D13: a runtime suffix must be final or followed only by version numbers."""

    def test_loadable_names_are_libraries(self):
        for name in ("libfastcdr.so.2", "libfastrtps.so.2.14", "libc++_shared.so",
                     "fastrtps-2.14.dll", "libfoo.so"):
            self.assertTrue(runner.is_runtime_library(name), name)

    def test_disabled_and_unrelated_names_are_not_libraries(self):
        # These contain a runtime suffix but are not loadable, so comparing them
        # as libraries would produce a conflict the loader can never see.
        for name in ("foo.dll.bak", "libfoo.so.disabled", "libfoo.so.2.orig", "notalib.txt"):
            self.assertFalse(runner.is_runtime_library(name), name)

    def test_a_disabled_copy_does_not_create_a_conflict(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a", root / "b"
            for path in (first, second):
                path.mkdir()
            (first / "libfastcdr.so.2").write_bytes(b"fastcdr-2.2.5")
            (second / "libfastcdr.so.2.bak").write_bytes(b"something-else")
            self.assertEqual(runner.shadowed_libraries([first, second]), [])


class AcknowledgementParsingTests(unittest.TestCase):
    """D10: the pinned form must not degrade to unpinned without saying so."""

    def test_valid_forms(self):
        self.assertEqual(runner.parse_acknowledgements(["fastrtps-2.14.dll"]),
                         {"fastrtps-2.14.dll": None})
        self.assertEqual(runner.parse_acknowledgements(["libfastcdr.so.2=113F8EF5"]),
                         {"libfastcdr.so.2": "113f8ef5"})

    def test_empty_name_is_refused(self):
        for entry in ("", "=abcdef12", "   "):
            with self.assertRaises(ValueError):
                runner.parse_acknowledgements([entry])

    def test_empty_digest_after_separator_is_refused(self):
        # This used to silently become a fully unpinned acknowledgement.
        with self.assertRaises(ValueError):
            runner.parse_acknowledgements(["fastrtps-2.14.dll="])

    def test_short_or_non_hex_digest_is_refused(self):
        # One hex character matches roughly one digest in sixteen.
        with self.assertRaises(ValueError):
            runner.parse_acknowledgements(["fastrtps-2.14.dll=d"])
        with self.assertRaises(ValueError):
            runner.parse_acknowledgements(["fastrtps-2.14.dll=abcdef1"])
        with self.assertRaises(ValueError):
            runner.parse_acknowledgements(["fastrtps-2.14.dll=zzzzzzzz"])

    def test_duplicate_entry_cannot_silently_widen_a_pin(self):
        with self.assertRaises(ValueError):
            runner.parse_acknowledgements(["a.dll=abcdef12", "a.dll"])
        # Repeating an identical entry is harmless.
        self.assertEqual(runner.parse_acknowledgements(["a.dll=abcdef12", "a.dll=abcdef12"]),
                         {"a.dll": "abcdef12"})


class EvidenceInventoryTests(unittest.TestCase):
    """D9: the recorded inventory and the enforced decision must agree."""

    def test_versioned_shared_objects_are_hashed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            libs = root / "lib"
            libs.mkdir()
            # A plain `file.suffix` test sees `.2`, `.14` and `.3`, not `.so`,
            # so these were previously recorded with no digest at all.
            for name in ("libfastcdr.so.2", "libfastrtps.so.2.14", "libcrypto.so.3",
                         "libc++_shared.so", "fastrtps-2.14.dll"):
                (libs / name).write_bytes(b"content of " + name.encode())
            (libs / "notes.txt").write_bytes(b"not a library")
            executable = root / "peer.exe"
            executable.write_bytes(b"probe identity")
            out = root / "evidence"
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "2.14.5", "--reader-version", "2.14.5",
                         "--writer-runtime-dir", str(libs), "--reader-runtime-dir", str(libs),
                         "--cases", "identical_xcdr2", "--seconds", "1", "--out", str(out)]
            process = subprocess.CompletedProcess([], 3, b"", b"")
            with patch.object(sys, "argv", arguments), patch.object(runner.subprocess, "run", return_value=process):
                runner.main()
            evidence = json.loads((out / "results.json").read_text(encoding="utf-8"))
            recorded = {Path(path).name for path in evidence["runtime_files"]["writer"]}
            self.assertEqual(recorded, {"libfastcdr.so.2", "libfastrtps.so.2.14", "libcrypto.so.3",
                                        "libc++_shared.so", "fastrtps-2.14.dll"})
            self.assertTrue(all(len(digest) == 64 for digest in evidence["runtime_files"]["writer"].values()))


class CrashAggregationTests(unittest.TestCase):
    """D11: a crash must not be hidden behind a BLOCKED headline verdict."""

    def test_a_crashed_peer_makes_the_case_and_the_run_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "peer.exe"
            executable.write_bytes(b"probe identity")
            out = root / "evidence"
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "2.14.5", "--reader-version", "2.14.5",
                         "--cases", "identical_xcdr2", "--seconds", "1", "--out", str(out)]
            # One peer segfaults, the other merely reports no peer (exit 3).
            # Before the fix the BLOCKED peer promoted the whole run to BLOCKED.
            results = [subprocess.CompletedProcess([], -11, b"", b""),
                       subprocess.CompletedProcess([], 3, b"", b"")]
            with patch.object(sys, "argv", arguments), \
                    patch.object(runner.subprocess, "run", side_effect=results):
                code = runner.main()
            evidence = json.loads((out / "results.json").read_text(encoding="utf-8"))
            self.assertTrue(evidence["cases"][0]["crashed"])
            self.assertEqual(evidence["cases"][0]["status"], "FAIL")
            self.assertEqual(evidence["status"], "FAIL")
            self.assertEqual(code, 1)
            self.assertTrue(any(peer.get("crashed") for peer in evidence["cases"][0]["peers"]))


class RejectionControlTests(unittest.TestCase):
    """CONSIDER 5: every rejection case needs the live-delivery control."""

    def run_cases(self, cases, control_passes):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "peer.exe"
            executable.write_bytes(b"probe identity")
            out = root / "evidence"
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "3.6.2", "--reader-version", "3.6.2",
                         "--cases"] + list(cases) + ["--seconds", "1", "--out", str(out)]

            def observation(case, role):
                expectation = runner.CASE_EXPECTATION[case]
                negative = expectation == "rejection"
                record = dict(case=case, role=role, fastdds="3.6.2.0", transport="udp4",
                              expectation=expectation, basis="xtypes",
                              matched=not negative, received=0 if negative else 3,
                              passed=True, cleanup_ok=True, peer_alive=True)
                return ("DDS_STANDARDS_RESULT " + json.dumps(record)).encode()

            def fake_run(command, **kwargs):
                case, role = command[1], command[2]
                if case == runner.CONTROL_CASE and not control_passes:
                    return subprocess.CompletedProcess(command, 1, b"", b"")
                return subprocess.CompletedProcess(command, 0, observation(case, role), b"")

            with patch.object(sys, "argv", arguments), patch.object(runner.subprocess, "run", fake_run):
                runner.main()
            return json.loads((out / "results.json").read_text(encoding="utf-8"))

    def test_xml_is_blocked_when_the_delivery_control_fails(self):
        evidence = self.run_cases(["xml"], control_passes=False)
        statuses = {case["case"]: case["status"] for case in evidence["cases"]}
        self.assertEqual(statuses[runner.CONTROL_CASE], "FAIL")
        # Previously only `incompatible_member` was gated on the control, so a
        # rejection case could report PASS in a run where nothing was delivered.
        self.assertEqual(statuses["xml"], "BLOCKED")

    def test_rejection_cases_pass_when_the_control_passes(self):
        evidence = self.run_cases(["xml", "incompatible_member"], control_passes=True)
        statuses = {case["case"]: case["status"] for case in evidence["cases"]}
        self.assertEqual(statuses[runner.CONTROL_CASE], "PASS")
        self.assertEqual(statuses["xml"], "PASS")
        self.assertEqual(statuses["incompatible_member"], "PASS")

    def test_evidence_declares_its_own_case_expectations(self):
        evidence = self.run_cases(["xml"], control_passes=True)
        self.assertEqual(evidence["case_expectations"]["xml"], "rejection")
        self.assertEqual(evidence["case_expectations"]["appendable_added_member"], "delivery")
        self.assertEqual(evidence["gate_variable"], runner.GATE_VARIABLE)


def runner_case_order(cases):
    return list(dict.fromkeys([runner.CONTROL_CASE] + list(cases)))


class ControlCaseGuardTests(unittest.TestCase):
    """The control case must be a delivery case, unconditionally."""

    def test_control_case_is_a_delivery_case(self):
        self.assertEqual(runner.CASE_EXPECTATION[runner.CONTROL_CASE], "delivery")

    def test_guard_survives_python_dash_O(self):
        """A bare `assert` would be stripped by `python -O`, so it must not be one.

        The guard protects the rejection-case precondition: if the control case
        were itself a rejection case, no run would have a live-delivery control.
        """
        source = (Path(runner.__file__).read_text(encoding="utf-8")
                  if getattr(runner, "__file__", None) else SOURCE.read_text(encoding="utf-8"))
        self.assertNotIn('assert CASE_EXPECTATION[CONTROL_CASE]', source)
        self.assertIn("raise RuntimeError", source)

    def test_a_rejection_control_case_is_refused_at_import(self):
        import importlib.util
        with tempfile.TemporaryDirectory() as directory:
            broken = Path(directory) / "broken_runner.py"
            broken.write_text(
                SOURCE.read_text(encoding="utf-8").replace(
                    'CONTROL_CASE = "identical_xcdr2"',
                    'CONTROL_CASE = "incompatible_member"', 1),
                encoding="utf-8")
            spec = importlib.util.spec_from_file_location("broken_runner", broken)
            module = importlib.util.module_from_spec(spec)
            with self.assertRaises(RuntimeError) as raised:
                spec.loader.exec_module(module)
            self.assertIn("must be a delivery case", str(raised.exception))


class MetatrafficPortRecordTests(unittest.TestCase):
    """A run where no peer was ever seen must be diagnosable after the fact.

    Fast DDS derives the RTPS metatraffic port as 7400 + 250 * domain. When that
    port falls inside an OS-reserved range, discovery never happens and every
    case reports `peer_alive: false`. Two passes misdiagnosed exactly that as a
    lingering-participant flake, so the port is now recorded.
    """

    def test_domain_and_metatraffic_port_are_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            executable = root / "peer.exe"
            executable.write_bytes(b"probe identity")
            out = root / "evidence"
            arguments = ["run_type_baseline.py", "--writer", str(executable), "--reader", str(executable),
                         "--writer-version", "2.14.5", "--reader-version", "2.14.5",
                         "--cases", "identical_xcdr2", "--seconds", "1",
                         "--domain", "226", "--out", str(out)]
            process = subprocess.CompletedProcess([], 3, b"", b"")
            with patch.object(sys, "argv", arguments), patch.object(runner.subprocess, "run", return_value=process):
                runner.main()
            evidence = json.loads((out / "results.json").read_text(encoding="utf-8"))
            self.assertEqual(evidence["domain"], 226)
            # 63900 fell inside a Windows excluded UDP range on the host where
            # this was diagnosed, which is why the field exists.
            self.assertEqual(evidence["metatraffic_port"], 7400 + 250 * 226)
            self.assertEqual(evidence["metatraffic_port"], 63900)

CHECKER = SOURCE.parent / "check_docs_consistency.py"
_CHECKER_SPEC = importlib.util.spec_from_file_location("check_docs_consistency", CHECKER)
docs_check = importlib.util.module_from_spec(_CHECKER_SPEC)
_CHECKER_SPEC.loader.exec_module(docs_check)


class DocumentationConsistencyTests(unittest.TestCase):
    """Check the numbers in docs/standards against the sources that establish them."""

    def setUp(self):
        if not docs_check.DOCS.is_dir():
            self.skipTest("docs/standards not present")

    def test_the_suite_count_is_measurable(self):
        count, problems = docs_check.measure_suite_count()
        self.assertEqual(problems, [])
        self.assertGreater(count, 0)

    def test_unit_count_matches_the_suite(self):
        """If this fails after adding or removing a test, update the documents."""
        count, problems = docs_check.measure_suite_count()
        self.assertEqual(problems, [])
        self.assertEqual(docs_check.check_unit_count(count), [])

    def test_ctest_is_one_of_one(self):
        self.assertEqual(docs_check.check_ctest_count(), [])

    def test_domain_port_arithmetic(self):
        self.assertEqual(docs_check.check_port_arithmetic(), [])

    def test_domain_references_are_real_domains(self):
        self.assertEqual(docs_check.check_domain_references(), [])

    def test_every_cited_digest_is_declared(self):
        """Also covers attribution: each digest in its own artifact's context."""
        self.assertEqual(docs_check.check_digests(), [])

    def test_reserved_port_table_columns_agree(self):
        self.assertEqual(docs_check.check_reserved_port_table(), [])

    def test_patched_matrix_counts_match_the_evidence(self):
        if not docs_check.DEVICE_PATCHED.is_dir():
            self.skipTest("patched-matrix evidence not present")
        self.assertEqual(docs_check.check_patched_matrix(), [])

    def test_schema_census_matches_the_retained_files(self):
        if not docs_check.BUILD_ROOT.is_dir():
            self.skipTest("build evidence root not present")
        self.assertEqual(docs_check.check_schema_census(), [])

    def test_each_document_carries_its_required_claims(self):
        self.assertEqual(docs_check.check_required_claims(), [])


class DocumentationGuardSelfTests(unittest.TestCase):
    """Prove each documentation guard can actually fail.

    A guard that cannot fail is worse than no guard, because it retires the
    vigilance it replaced. Two earlier versions of these guards were defective
    in the same way: the guard could not reach the fault, and its validation
    could not reach the guard. One used an escape that became a backspace byte
    and matched nothing; its successor compared a digest only once its first
    eight characters already matched, and its self-tests mutated only the final
    character, so neither could see the hole.

    Each case copies the real documents, injects one fault, and asserts the
    named guard reports it. The real documents are never written to. The digest
    cases deliberately corrupt several positions, including the first.
    """

    def mutated(self, directory, old, new, everywhere=False):
        """Copy the documents into `directory` with one substitution."""
        target = Path(directory)
        target.mkdir(parents=True, exist_ok=True)
        applied = 0
        for document in docs_check.documents():
            text = document.read_text(encoding="utf-8")
            if old in text and (everywhere or applied == 0):
                applied += text.count(old) if everywhere else 1
                text = text.replace(old, new) if everywhere else text.replace(old, new, 1)
            (target / document.name).write_text(text, encoding="utf-8")
        self.assertTrue(applied, "mutation anchor not in the real documents: %r" % old[:60])
        return target

    def setUp(self):
        if not docs_check.DOCS.is_dir():
            self.skipTest("docs/standards not present")
        self.count, problems = docs_check.measure_suite_count()
        self.assertEqual(problems, [])

    def test_unit_count_guard_fires_on_the_ratio_form(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "%d/%d PASS" % (self.count, self.count), "99/99 PASS")
            self.assertTrue(docs_check.check_unit_count(self.count, docs))

    def test_unit_count_guard_fires_on_the_prose_form(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "->   %d passed" % self.count, "->   99 passed")
            self.assertTrue(docs_check.check_unit_count(self.count, docs))

    def test_unit_count_guard_reports_when_the_prose_form_is_removed(self):
        """Deleting the only prose instance must report, not fall silent."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "->   %d passed" % self.count, "-> ok")
            problems = docs_check.check_unit_count(self.count, docs)
            self.assertTrue(any("N passed" in problem for problem in problems))

    def test_ctest_guard_fires(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "CTest 1/1", "CTest 53/53")
            self.assertTrue(docs_check.check_ctest_count(docs))

    def test_port_arithmetic_guard_fires(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 230 | 64900 |", "| 230 | 64901 |")
            self.assertTrue(docs_check.check_port_arithmetic(docs))

    def test_domain_guard_fires_on_the_real_defect(self):
        """The exact defect: a unit-test count substituted over a domain in prose."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "219, 222, 225 and 230", "219, 222, 225 and 62")
            problems = docs_check.check_domain_references(docs)
            self.assertTrue(problems)
            self.assertIn("62", " ".join(problems))

    def test_domain_guard_fires_above_the_ceiling(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "219, 222, 225 and 230", "219, 222, 225 and 519")
            self.assertTrue(docs_check.check_domain_references(docs))

    def test_digest_guard_fires_wherever_the_corruption_sits(self):
        """Including inside the first characters, which an earlier guard could not see."""
        actual = docs_check.sha256(docs_check.PATCH)
        for position in (0, 3, 7, 8, 31, 63):
            flipped = "0" if actual[position] != "0" else "1"
            wrong = actual[:position] + flipped + actual[position + 1:]
            self.assertNotEqual(wrong, actual)
            with tempfile.TemporaryDirectory() as directory:
                docs = self.mutated(directory, actual, wrong, everywhere=True)
                self.assertTrue(docs_check.check_digests(docs),
                                "a digest corrupted at position %d went unreported" % position)

    def test_digest_guard_fires_on_a_truncation_diverging_early(self):
        actual = docs_check.sha256(docs_check.PATCH)
        truncated = actual[:16]
        for position in (0, 3, 7, 15):
            flipped = "0" if truncated[position] != "0" else "1"
            wrong = truncated[:position] + flipped + truncated[position + 1:]
            with tempfile.TemporaryDirectory() as directory:
                docs = self.mutated(directory, truncated, wrong, everywhere=True)
                self.assertTrue(docs_check.check_digests(docs),
                                "a truncated digest wrong at position %d went unreported" % position)

    def test_digest_guard_fires_on_an_undeclared_digest(self):
        """A hash added to the documents must be declared with its provenance."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 230 | 64900 | free | PASS |",
                                "| 230 | 64900 | free | PASS |\n\nStray hash: `cafebabe0123456789abcdef`.")
            self.assertTrue(docs_check.check_digests(docs))

    # Mutation self-tests for the guards added for the five limitations the
    # 2026-11-10 gate-4 review declared. Each guard is shown to fail on a
    # deliberately corrupted document or artifact, to pass on the real ones
    # (DocumentationConsistencyTests), and to fire independently of the
    # corpus-level guards.

    def synthetic_matrix(self, directory, received=(129, 130, 130, 130)):
        """A patched-matrix evidence directory with a known shape."""
        evidence = Path(directory) / "matrix"
        evidence.mkdir()
        gateon = ["AtoB-on-appendable_added_member-r.out",
                  "BtoA-on-appendable_added_member-r.out",
                  "AtoB-on-mutable_added_member-r.out",
                  "BtoA-on-mutable_added_member-r.out"]
        for name, value in zip(gateon, received):
            (evidence / name).write_text(
                'DDS_STANDARDS_RESULT {"case":"x","received":%d}\n' % value,
                encoding="utf-8")
        for index in range(44 - len(gateon)):
            (evidence / ("AtoB-off-filler_%02d-w.out" % index)).write_text(
                "", encoding="utf-8")
        f9 = Path(directory) / "f9"
        f9.mkdir()
        (f9 / "gateon-reader-B.out").write_text(
            'DDS_STANDARDS_RESULT {"case":"x","received":251}\n', encoding="utf-8")
        return evidence, f9

    def synthetic_build(self, directory, v1_count=25, gate_off=False, gate_on=True):
        """A build evidence root with a known schema census and gate pair."""
        root = Path(directory) / "build"
        named = list(docs_check.SCHEMA_V1_EVIDENCE)
        for index in range(v1_count - len(named)):
            named.append("extra-%02d" % index)
        for name in named:
            target = root / name
            target.mkdir(parents=True)
            (target / "results.json").write_text('{"schema_version": 1}',
                                                 encoding="utf-8")
        for name, value in (("review-fix-gate-off", gate_off),
                            ("review-fix-gate-on", gate_on)):
            target = root / name
            target.mkdir(parents=True)
            (target / "results.json").write_text(
                json.dumps({"schema_version": 2, "gate_on": value}), encoding="utf-8")
        return root

    def test_attribution_guard_fires_on_a_prose_swap(self):
        """The libfastcdr digest presented as libfastrtps's must report."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "`libfastrtps.so.2.14` `416179dd29e033d9",
                                "`libfastrtps.so.2.14` `113f8ef51c667c29")
            problems = docs_check.check_digests(docs)
            self.assertTrue(any("attribute" in problem for problem in problems))

    def test_attribution_guard_fires_on_a_table_row_swap(self):
        """The Fast CDR 2.3.7 digest in the 2.2.5 row must report."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory,
                                "| `2.14.5/lib/libfastcdr.so.2` | `113f8ef51c667c29",
                                "| `2.14.5/lib/libfastcdr.so.2` | `d735d942b10df5e6")
            problems = docs_check.check_digests(docs)
            self.assertTrue(any("attribute" in problem for problem in problems))

    def test_attribution_guard_fires_on_a_patch_version_swap(self):
        """The as-built patch hash in the disclosure row must report."""
        as_built = docs_check.DECLARED_DIGESTS["patch, as built and deployed"]
        disclosure = docs_check.DECLARED_DIGESTS["patch, after the gate-scope disclosure"]
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, disclosure, as_built)
            problems = docs_check.check_digests(docs)
            self.assertTrue(any("attribute" in problem for problem in problems))

    def test_reserved_table_guard_fires_on_a_free_row_marked_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 230 | 64900 | free | PASS |",
                                "| 230 | 64900 | free | BLOCKED |")
            self.assertTrue(docs_check.check_reserved_port_table(docs))

    def test_reserved_table_guard_fires_on_a_reserved_row_marked_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 226 | 63900 | 63898-63997 | BLOCKED |",
                                "| 226 | 63900 | 63898-63997 | PASS |")
            self.assertTrue(docs_check.check_reserved_port_table(docs))

    def test_reserved_table_guard_fires_on_a_port_outside_its_range(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 231 | 65150 | 65105-65204 | BLOCKED |",
                                "| 231 | 65150 | 65160-65204 | BLOCKED |")
            self.assertTrue(docs_check.check_reserved_port_table(docs))

    def test_domain_guard_now_checks_232(self):
        """With the 232 table row gone, the '231 and 232' list must report.

        232 was blanket-exempt before, so this corruption passed; the cap
        uses ('capped at 232', '0..232') must stay silent, which the
        real-document run asserts.
        """
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 232 | 65400 | 65316-65415 | BLOCKED |", "")
            problems = docs_check.check_domain_references(docs)
            self.assertTrue(any("232" in problem for problem in problems))

    def test_matrix_guard_accepts_a_true_synthetic_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence, f9 = self.synthetic_matrix(directory)
            self.assertEqual(docs_check.check_patched_matrix(None, evidence, f9), [])

    def test_matrix_guard_fires_on_a_corrupted_artifact(self):
        """A received count the documents do not claim must report."""
        with tempfile.TemporaryDirectory() as directory:
            evidence, f9 = self.synthetic_matrix(directory, received=(129, 130, 130, 999))
            self.assertTrue(docs_check.check_patched_matrix(None, evidence, f9))

    def test_matrix_guard_fires_on_a_wrong_received_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "129 received", "133 received")
            self.assertTrue(docs_check.check_patched_matrix(docs))

    def test_matrix_guard_fires_on_a_wrong_run_count(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "22 cross-board case runs",
                                "23 cross-board case runs")
            self.assertTrue(docs_check.check_patched_matrix(docs))

    def test_matrix_guard_reports_missing_evidence(self):
        self.assertTrue(docs_check.check_patched_matrix(
            None, Path("does-not-exist-anywhere"), None))

    def test_census_guard_accepts_a_true_synthetic_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.synthetic_build(directory)
            self.assertEqual(docs_check.check_schema_census(None, root), [])

    def test_census_guard_fires_when_the_census_drifts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.synthetic_build(directory, v1_count=26)
            self.assertTrue(docs_check.check_schema_census(None, root))

    def test_census_guard_fires_on_a_flipped_gate_record(self):
        with tempfile.TemporaryDirectory() as directory:
            root = self.synthetic_build(directory, gate_on=False)
            self.assertTrue(docs_check.check_schema_census(None, root))

    def test_census_guard_fires_on_a_wrong_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "25 of them at the", "26 of them at the")
            root = self.synthetic_build(Path(directory) / "evidence")
            self.assertTrue(docs_check.check_schema_census(docs, root))

    def test_reserved_table_guard_fires_on_a_malformed_result_cell(self):
        """A Result cell that is neither PASS nor BLOCKED must report, not skip."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "| 226 | 63900 | 63898-63997 | BLOCKED |",
                                "| 226 | 63900 | 63898-63997 | BLOCKED (retest) |")
            problems = docs_check.check_reserved_port_table(docs)
            self.assertTrue(any("neither PASS nor BLOCKED" in p for p in problems))

    def test_received_claims_accept_the_f9_rerun_phrasing(self):
        """Rephrasing the f9 'received: 251' as '251 received' must stay silent."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "`received: 251`", "`251 received`")
            self.assertEqual(docs_check.check_patched_matrix(docs), [])

    def test_received_claims_accept_the_mixed_rerun_values(self):
        """A count the mixed-default re-run recorded (514) must stay silent."""
        if not docs_check.MIXED_DEFAULT.is_dir():
            self.skipTest("mixed-default evidence not present")
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "`received: 251`",
                                "`received: 251` and `received: 514`")
            self.assertEqual(docs_check.check_patched_matrix(docs), [])

    def test_received_claims_still_fire_on_an_unrecorded_value(self):
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "`received: 251`", "`received: 515`")
            self.assertTrue(docs_check.check_patched_matrix(docs))

    def test_matrix_guard_fires_on_an_odd_out_count(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence, f9 = self.synthetic_matrix(directory)
            (evidence / "stray-extra-file.out").write_text("", encoding="utf-8")
            problems = docs_check.check_patched_matrix(None, evidence, f9)
            self.assertTrue(any("odd number" in p for p in problems))

    def test_matrix_guard_fires_on_malformed_json(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence, f9 = self.synthetic_matrix(directory)
            (evidence / "AtoB-on-appendable_added_member-r.out").write_text(
                "DDS_STANDARDS_RESULT {not json}\n", encoding="utf-8")
            problems = docs_check.check_patched_matrix(None, evidence, f9)
            self.assertTrue(any("malformed JSON" in p for p in problems))

    def test_missing_evidence_is_marked_for_the_optional_skip_path(self):
        """Only a missing evidence root carries the MISSING_EVIDENCE prefix."""
        problems = docs_check.check_patched_matrix(None, Path("does-not-exist"), None)
        self.assertTrue(problems)
        self.assertTrue(all(p.startswith(docs_check.MISSING_EVIDENCE) for p in problems))
        with tempfile.TemporaryDirectory() as directory:
            problems = docs_check.check_schema_census(None, Path(directory))
            self.assertTrue(problems)
            self.assertTrue(all(p.startswith(docs_check.MISSING_EVIDENCE)
                                for p in problems))
        with tempfile.TemporaryDirectory() as directory:
            root = self.synthetic_build(directory, v1_count=26)
            self.assertTrue(any(not p.startswith(docs_check.MISSING_EVIDENCE)
                                for p in docs_check.check_schema_census(None, root)))

    def test_per_document_ctest_loss_fires_while_the_corpus_guard_stays_silent(self):
        """Losing one document's only CTest claim must report per-document."""
        with tempfile.TemporaryDirectory() as directory:
            docs = self.mutated(directory, "CTest 1/1 per version",
                                "CTest checks per version")
            self.assertEqual(docs_check.check_ctest_count(docs), [])
            problems = docs_check.check_required_claims(docs)
            self.assertTrue(any("ctest" in problem for problem in problems))

    def test_guards_report_on_an_empty_document_set(self):
        """Absence of documents must not read as consistency."""
        with tempfile.TemporaryDirectory() as directory:
            empty = Path(directory)
            for name, guard in docs_check.SELF_CONTAINED_GUARDS.items():
                self.assertTrue(guard(empty), "guard %r passed vacuously on no documents" % name)
            self.assertTrue(docs_check.check_unit_count(self.count, empty))


if __name__ == "__main__":
    unittest.main()
