import importlib.util
import json
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("a2matrix", Path(__file__).resolve().parents[1] /
                                             "scripts/standards/run_xcdr2_matrix.py")
matrix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(matrix)


def peer(role="writer", version="2.14.5", **changes):
    row = dict(case="identical_xcdr2", role=role, fastdds=version, transport="udp4", expectation="delivery",
               cleanup_ok=True, peer_alive=True, passed=True, matched=True, received=42)
    row.update(changes)
    return SimpleNamespace(returncode=0, stdout=("DDS_STANDARDS_RESULT " + json.dumps(row)).encode(), stderr=b"")


class MatrixEvidenceTests(unittest.TestCase):
    def test_remote_commands_do_not_share_the_launcher_input(self):
        with patch.object(matrix.subprocess,"run",return_value=peer()) as execute:
            matrix.ssh("boardA","exact command")
        self.assertIn("-n",execute.call_args.args[0])
        self.assertEqual(execute.call_args.args[0][-1],"exact command")

    def test_task_ssh_config_is_passed_as_a_single_argument(self):
        with patch.object(matrix, "SSH_CONFIG", Path("task directory/ssh config")), \
                patch.object(matrix.subprocess, "run", return_value=peer()) as execute:
            matrix.ssh("boardA", "exact command")
        argv = execute.call_args.args[0]
        self.assertEqual(argv[argv.index("-F") + 1], str(Path("task directory/ssh config")))
        self.assertIn("-n", argv)

    def test_missing_task_ssh_config_is_rejected_before_remote_execution(self):
        with tempfile.TemporaryDirectory() as parent:
            argv = ["matrix", "--a", "boardA", "--b", "boardB", "--candidate", "/candidate",
                    "--stock", "/stock", "--modern", "/modern", "--out", str(Path(parent)/"out"),
                    "--ssh-config", str(Path(parent)/"missing")]
            with patch.object(sys, "argv", argv), patch.object(matrix, "ssh") as execute, \
                    patch.object(matrix, "SSH_CONFIG", None), patch.object(sys, "stderr"):
                with self.assertRaises(SystemExit) as error:
                    matrix.main()
            self.assertEqual(error.exception.code, 2)
            execute.assert_not_called()

    def run_result(self, writer, reader, mode="on", pair="2.14.5:3.6.2", remote=False):
        with tempfile.TemporaryDirectory() as output:
            args = SimpleNamespace(a="boardA", b="boardB", candidate="/candidate", stock="/stock",
                                   modern="/modern", out=Path(output))
            def execute(board, command):
                return writer if " writer " in command else reader
            with patch.object(matrix, "run_remote_peer" if remote else "ssh", side_effect=execute), patch.object(matrix.time, "sleep"):
                return matrix.run_case(args, mode, pair, False, "xcdr2", 210)

    def test_delivery_requires_real_received_samples(self):
        self.assertEqual(self.run_result(peer(), peer("reader", "3.6.2.0", received=0))["validation"], "FAIL")

    def test_false_probe_result_cannot_pass(self):
        self.assertEqual(self.run_result(peer(), peer("reader", "3.6.2.0", passed=False))["validation"], "FAIL")

    def test_cleanup_and_peer_control_required(self):
        for key in ("cleanup_ok", "peer_alive", "matched"):
            self.assertEqual(self.run_result(peer(), peer("reader", "3.6.2.0", **{key:False}))["validation"], "FAIL")

    def test_success_has_commands_and_observations(self):
        row = self.run_result(peer(), peer("reader", "3.6.2.0"))
        self.assertEqual(row["validation"], "PASS")
        self.assertIn("FASTDDS_XCDR2_DYNAMIC_FRAMING=1", row["peers"]["reader"]["command"])

    def test_expected_stock_failure_is_not_delivery_pass(self):
        reader = peer("reader", "3.6.2.0", passed=False,received=0); reader.returncode = 1
        row = self.run_result(peer(),reader,"off")
        self.assertEqual(row["validation"],"PASS")
        self.assertEqual(row["delivery_status"],"FAIL")

    def test_unrelated_stock_failure_rejected(self):
        reader = peer("reader", "2.14.5", passed=False); reader.returncode=2
        self.assertEqual(self.run_result(peer(version="3.6.2.0"),reader,"off","3.6.2:2.14.5")["validation"],"FAIL")

    def test_stock_value_mismatch_is_retained_as_failure(self):
        reader = SimpleNamespace(returncode=2, stdout=b"DDS_STANDARDS_NOTE force_type_validation=0\n",
                                 stderr=b"DDS_STANDARDS_ERROR sample value mismatch\n")
        row = self.run_result(peer(version="3.6.2.0"),reader,"off","3.6.2:2.14.5")
        self.assertEqual(row["validation"],"PASS")
        self.assertEqual(row["delivery_status"],"FAIL")
        self.assertIsNone(row["peers"]["reader"]["observation"])

    def test_non_probe_json_not_accepted(self):
        self.assertIsNone(matrix.observations(b'{"cleanup_ok":true}'))

    def test_both_reported_versions_modern_cannot_pass_as_mixed(self):
        row = self.run_result(peer(version="3.6.2.0"), peer("reader", "3.6.2.0"))
        self.assertEqual(row["validation"], "FAIL")
        self.assertEqual(row["delivery_status"], "FAIL")
        self.assertFalse(row["peers"]["writer"]["identity_ok"])

    def test_missing_identity_fields_cannot_pass(self):
        for role in ("writer", "reader"):
            for key in ("case", "role", "fastdds", "transport", "expectation"):
                with self.subTest(role=role, field=key):
                    writer, reader = peer(), peer("reader", "3.6.2.0")
                    bad = writer if role == "writer" else reader
                    row = json.loads(bad.stdout.split(b" ", 1)[1]); del row[key]
                    bad.stdout = ("DDS_STANDARDS_RESULT " + json.dumps(row)).encode()
                    self.assertEqual(self.run_result(writer, reader)["validation"], "FAIL")

    def test_mismatched_identity_fields_cannot_pass(self):
        changes = {"case":"identical_xcdr1", "role":"unrequested", "fastdds":"9.9.9",
                   "transport":"fake", "expectation":"rejection"}
        for role in ("writer", "reader"):
            for key, value in changes.items():
                with self.subTest(role=role, field=key):
                    writer, reader = peer(), peer("reader", "3.6.2.0")
                    bad = writer if role == "writer" else reader
                    row = json.loads(bad.stdout.split(b" ", 1)[1]); row[key] = value
                    bad.stdout = ("DDS_STANDARDS_RESULT " + json.dumps(row)).encode()
                    self.assertEqual(self.run_result(writer, reader)["validation"], "FAIL")

    def test_multiple_or_malformed_receipts_cannot_hide_behind_a_valid_one(self):
        for prefix in (peer("reader", "3.6.2.0").stdout, b"DDS_STANDARDS_RESULT {broken"):
            reader = peer("reader", "3.6.2.0")
            reader.stdout = prefix + b"\n" + reader.stdout
            self.assertEqual(self.run_result(peer(), reader)["validation"], "FAIL")

    def test_sample_count_requires_a_positive_integer(self):
        for count in (True, "42", None, -1, 1.5):
            with self.subTest(count=count):
                self.assertEqual(self.run_result(peer(), peer("reader", "3.6.2.0", received=count))["validation"], "FAIL")

    def test_no_delivery_control_still_requires_reader_identity(self):
        reader = peer("reader", "2.14.5", passed=False, received=0); reader.returncode = 1
        self.assertEqual(self.run_result(peer(), reader, "off")["validation"], "FAIL")

    def test_reader_exception_control_still_requires_writer_identity(self):
        reader = SimpleNamespace(returncode=2, stdout=b"", stderr=b"DDS_STANDARDS_ERROR sample value mismatch\n")
        self.assertEqual(self.run_result(peer(), reader, "off", "3.6.2:2.14.5")["validation"], "FAIL")

    def test_reader_exception_does_not_accept_a_contradictory_receipt(self):
        reader = peer("reader", "9.9.9"); reader.returncode = 2
        reader.stderr = b"DDS_STANDARDS_ERROR sample value mismatch\n"
        self.assertEqual(self.run_result(peer(version="3.6.2.0"), reader, "off", "3.6.2:2.14.5")["validation"], "FAIL")

    def test_timeout_retains_partial_output_and_cannot_pass(self):
        failure = subprocess.TimeoutExpired("ssh",35,output=b"partial stdout",stderr=b"partial stderr")
        with patch.object(matrix,"ssh",side_effect=failure):
            receipt = matrix.run_remote_peer("boardA","exact command")
        self.assertTrue(receipt.timed_out)
        self.assertEqual(receipt.returncode,124)
        self.assertEqual(receipt.stdout,b"partial stdout")
        self.assertIn(b"partial stderr",receipt.stderr)
        reader = peer("reader", "3.6.2.0"); reader.timed_out = False
        self.assertEqual(self.run_result(receipt,reader,remote=True)["validation"],"BLOCKED")

    def test_default_serial_schedule_keeps_the_entire_matrix(self):
        with tempfile.TemporaryDirectory() as parent:
            output = Path(parent)/"matrix"
            argv = ["matrix","--a","boardA","--b","boardB","--candidate","/candidate",
                    "--stock","/stock","--modern","/modern","--out",str(output)]
            executor = matrix.futures.ThreadPoolExecutor
            with patch.object(sys,"argv",argv), patch.object(matrix,"ssh",return_value=peer()), \
                    patch.object(matrix,"run_case",return_value={"validation":"PASS","gate_on":False}) as case, \
                    patch.object(matrix.futures,"ThreadPoolExecutor",wraps=executor) as pool:
                self.assertEqual(matrix.main(),0)
            self.assertEqual(case.call_count,36)
            pool.assert_not_called()
            record = json.loads((output/"results.json").read_text())
            self.assertEqual(len(record["cases"]),36)
            self.assertEqual(record["status"],"PASS")

    def test_serial_timeout_stops_before_starting_the_next_pair(self):
        with tempfile.TemporaryDirectory() as parent:
            output = Path(parent)/"matrix"
            argv = ["matrix","--a","boardA","--b","boardB","--candidate","/candidate",
                    "--stock","/stock","--modern","/modern","--out",str(output)]
            with patch.object(sys,"argv",argv), patch.object(matrix,"ssh",return_value=peer()), \
                    patch.object(matrix,"run_case",return_value={"validation":"BLOCKED","gate_on":False}) as case:
                self.assertEqual(matrix.main(),2)
            self.assertEqual(case.call_count,1)
            self.assertEqual(json.loads((output/"results.json").read_text())["status"],"BLOCKED")


if __name__ == "__main__":
    unittest.main()
