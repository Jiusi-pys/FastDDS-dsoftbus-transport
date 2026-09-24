"""Offline oracle tests. All generated records are synthetic, never device proof."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("performance_analyzer", ROOT / "scripts/analyze_dds_performance.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)
FIXTURE = json.loads((Path(__file__).parent / "performance_trace_fixtures.json").read_text(encoding="utf-8"))
PLAN = FIXTURE["plan"]
NOT_ATTEMPTED, ATTEMPTED, VALIDATED, TIMEOUT = 1, 2, 4, 8
LATE, MISSED, ECHO, AFTER, INVALID = 16, 32, 64, 128, 256


def synthetic_window(mode="TRACE", repeat=1):
    """Generate every frozen expected row, not a result-driven expected set."""
    config = {"schema": 1, "run": "0123456789abcdef", "mode": mode, "repeat": repeat,
              "scope": "HOST_REFERENCE_UDP", "request_guid": "a" * 32,
              "response_guid": "b" * 32, "clock_unit": "ns",
              "measurement_plan_sha256": ANALYZER.PLAN_SHA256, "log_schema_sha256": "1" * 64,
              "build_manifest_sha256": "2" * 64, "source_state_sha256": "3" * 64,
              "executable_sha256": "4" * 64,
              "package_artifacts": [{"name": "perf_reference.exe", "sha256": "4" * 64}]}
    rows = {side: [] for side in "AB"}
    for side in "AB":
        for endpoint in ("request_writer", "response_reader") if side == "A" else ("request_reader", "response_writer"):
            rows[side].append({"_kind": "PERF_QOS", "run": config["run"], "side": side,
                "endpoint": endpoint, "effective": copy.deepcopy(PLAN["qos"]), "representation": "XCDR1"})
        rows[side].append({"_kind": "PERF_COUNTERS", "run": config["run"], "side": side,
            "record_overflow": 0, "counter_overflow": 0, "log_overflow_bytes": 0,
            "wrong_run": 0, "wrong_source": 0, "invalid": 0, "out_of_range": 0,
            "lost_records": 0, "lost_error_details": 0, "errors": 0,
            "storage_bytes": 3670048, "inbound_capacity": 1048576, "outbound_capacity": 262176,
            "native_error_total": 0, "native_error_detail_overflow": 0,
            "rx_admission_prefix_overflow": 0})
    cases = {c["id"]: c for c in PLAN["cases"]}
    order = PLAN["order"]["TRACE" if mode == "TRACE" else f"MEASURE_{repeat}"]
    stage = 0
    for case_id in order:
        case = cases[case_id]
        for phase in (["FORMAL"] if mode == "TRACE" else ["WARMUP", "FORMAL"]):
            stage += 1
            count = case["trace_count"] if mode == "TRACE" else case["warmup_count"] if phase == "WARMUP" else case.get("formal_count", case.get("planned_slots"))
            start = stage * 100_000_000_000
            duration = (2 if mode == "TRACE" else 10 if phase == "FORMAL" else max(1, count / case.get("requested_hz", case.get("maximum_hz")))) * 1_000_000_000
            duration = int(duration)
            for sequence in range(1, count + 1):
                before = start + (sequence - 1) * (1_000_000_000 // case.get("requested_hz", case.get("maximum_hz")))
                sn = stage * 10000 + sequence
                base = {"_kind": "PERF_SAMPLE", "run": config["run"], "mode": mode, "repeat": repeat,
                    "case": case_id, "phase": phase, "sequence": sequence,
                    "business_bytes": case["business_bytes"], "vector_bytes": case["vector_bytes"],
                    "serialized_bytes": case["serialized_xcdr1_bytes"], "duplicates": 0,
                    "write_before_ns": None, "write_after_ns": None, "write_rc": None,
                    "writer_guid": None, "writer_sn": None, "first_take_ns": None,
                    "source_guid": None, "source_sn": None, "echo_before_ns": None,
                    "echo_after_ns": None, "echo_rc": None, "echo_guid": None, "echo_sn": None}
                a = dict(base, side="A", flags=ATTEMPTED, write_before_ns=before,
                    write_after_ns=before + 50, write_rc=0, writer_guid="a" * 32, writer_sn=sn)
                b = dict(base, side="B", flags=VALIDATED, first_take_ns=before + 5000,
                    source_guid="a" * 32, source_sn=sn)
                if case["kind"] == "RTT":
                    a.update(flags=ATTEMPTED | VALIDATED, first_take_ns=before + sequence * 1000,
                             source_guid="b" * 32, source_sn=sn)
                    b.update(flags=VALIDATED | ECHO, echo_before_ns=before + 5100,
                             echo_after_ns=before + 5200, echo_rc=0, echo_guid="b" * 32, echo_sn=sn)
                rows["A"].append(a)
                rows["B"].append(b)
            for side in "AB":
                rows[side].append({"_kind": "PERF_SUMMARY", "run": config["run"], "side": side,
                    "mode": mode, "repeat": repeat, "stage": stage, "case": case_id, "phase": phase,
                    "planned": count, "window_start_ns": start if side == "A" else start + 5000,
                    "window_end_ns": start + duration if side == "A" else start + duration + 5000,
                    "phase_deadline_ns": start + 30_000_000_000 if side == "A" else start + 30_000_005_000,
                    "observation_deadline_ns": start + duration + 2_000_000_000 if side == "A" else start + duration + 2_000_005_000,
                    "clock_unit": "ns", "complete": True, "ack_rc": 0})
    return config, rows


def sample(rows, side, case="RTT_1K", sequence=1, phase="FORMAL"):
    return next(r for r in rows[side] if r["_kind"] == "PERF_SAMPLE" and r["case"] == case and r["phase"] == phase and r["sequence"] == sequence)


def synthetic_native(mode="TRACE", repeat=1):
    config, rows = synthetic_window(mode, repeat)
    config["scope"] = "BYTES_NATIVE"
    config.update(profile="BYTES_NATIVE", rx_drop="OFF", main_sha256="4" * 64,
                  package_artifacts=[{"name": "fastdds_socket_experiment", "sha256": "4" * 64}])
    identities = {}
    for side in "AB":
        peer = "B" if side == "A" else "A"
        ident = {"_kind": "PERF_IDENTITY", "run": config["run"], "side": side,
            "runtime": 1, "native_instance": 2, "application": 3, "receiver": 4, "token": 5,
            "local_kind": 167, "local_key": side.lower() * 32, "local_port": 7301 if side == "A" else 7302,
            "peer_kind": 167, "peer_key": peer.lower() * 32, "peer_port": 7302 if side == "A" else 7301}
        identities[side] = ident
        rows[side].append(ident)
        rows[side].append({"_kind": "PERF_OBSERVATION", "run": config["run"], "side": side,
                          "scope": "CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC",
                          "suppressed": 100 if mode == "MEASURE" else 0, "counter_overflow": False})
    invocation = 0
    for case in PLAN["cases"]:
        phase = "FORMAL" if mode == "TRACE" else "WARMUP"
        for sequence in range(1, case["trace_count"] + 1 if mode == "TRACE" else 2):
            for source, target, writer, sn in [("A", "B", config["request_guid"], sample(rows, "A", case["id"], sequence, phase)["writer_sn"])] + (
                    [("B", "A", config["response_guid"], sample(rows, "B", case["id"], sequence, phase)["echo_sn"])] if case["kind"] == "RTT" else []):
                sample_size = case["serialized_xcdr1_bytes"]
                fragment_size = 56000
                count = (sample_size + fragment_size - 1) // fragment_size if sample_size > 60000 else 1
                for fragment in range(1, count + 1):
                    invocation += 1
                    digest = f"{invocation:016x}"
                    length = min(fragment_size, sample_size - (fragment - 1) * fragment_size) + 100
                    def native(side, event):
                        ident = identities[side]
                        return {"_kind": "EXPERIMENT_UNQUALIFIED", "run": config["run"], "event": event,
                            "instance_id": "2", "invocation": str(invocation), "rc": "0", "len": str(length),
                            "hash": digest, "socket": "7", "owner": "accepted", "receiver_lease_id": "4",
                            **{k: str(ident[k]) for k in ("local_kind", "local_key", "local_port", "peer_kind", "peer_key", "peer_port")}}
                    def data(event):
                        item = {"_kind": "EXPERIMENT_DATA_FRAG" if count > 1 else "EXPERIMENT_DATA",
                            "run": config["run"], "event": event, "instance_id": "2", "invocation": str(invocation),
                            "rc": "0", "writer_guid": writer, "sequence_hi": str(sn >> 32), "sequence_lo": str(sn & 0xffffffff),
                            "native_hash": digest, "native_len": str(length)}
                        if count > 1:
                            item.update(fragment_start=str(fragment), fragment_count="1", fragment_end=str(fragment),
                                fragment_size=str(fragment_size), sample_size=str(sample_size), payload_bounds_known="1")
                        return item
                    rows[source].append({"_kind": "EXPERIMENT_APPLICATION_TX", "run": config["run"], "stage": "TX_ACCEPTED",
                        "runtime_id": "1", "instance_id": "2", "application_incarnation": "3", "logical_token": "5", "invocation": str(invocation)})
                    rows[source].extend([native(source, "SendBytes_return"), data("SendBytes_return")])
                    for stage in (0, 1):
                        rows[target].append({"_kind": "EXPERIMENT_APPLICATION_RX", "stage": str(stage), "runtime_id": "1",
                            "instance_id": "2", "application_incarnation": "3", "receiver_incarnation": "4", "logical_token": "5",
                            "invocation": str(invocation), "bytes": str(length)})
                    rows[target].extend([native(target, "OnBytes_owned_copy"), data("OnBytes_owned_copy")])
                    for stage in (3, 4):
                        rows[target].append({"_kind": "EXPERIMENT_APPLICATION_RX", "stage": str(stage), "runtime_id": "1",
                            "instance_id": "2", "application_incarnation": "3", "receiver_incarnation": "4", "logical_token": "5",
                            "invocation": str(invocation), "bytes": str(length)})
                    for event in ("FastDDS_receiver_enter", "FastDDS_receiver_return"):
                        rows[target].extend([native(target, event), data(event)])
                    rows[target].append({"_kind": "EXPERIMENT_APPLICATION_RX", "stage": "5", "runtime_id": "1",
                        "instance_id": "2", "application_incarnation": "3", "receiver_incarnation": "4", "logical_token": "5",
                        "invocation": str(invocation), "bytes": str(length)})
    for side in "AB":
        for line, row in enumerate(rows[side], 1): row["_line"] = line
    return config, rows


class NativeWitnessTests(unittest.TestCase):
    def analyze(self, config, rows):
        return ANALYZER.analyze_records(config, rows, PLAN)

    def test_trace_all_38_delivery_identities_and_full_fragments(self):
        result = self.analyze(*synthetic_native())
        self.assertTrue(result["native_proof_pass"], result["native"])
        self.assertEqual(len(result["native"]["joins"]), 38)
        self.assertFalse(result["device_tested"], "synthetic witness cannot clear outer device guards")

    def test_measure_only_9_predeclared_warmup_identities(self):
        result = self.analyze(*synthetic_native("MEASURE", 1))
        self.assertTrue(result["native_proof_pass"], result["native"])
        self.assertEqual(len(result["native"]["joins"]), 9)
        self.assertEqual(result["formal_native_detail_scope"], "aggregate_only_no_full_chain_claim")

    def test_fragment_hole_wrong_lease_hash_or_old_run_fails(self):
        for mutation in ("fragment_hole", "wrong_lease", "hash", "old_run"):
            config, rows = synthetic_native()
            target = next(r for r in rows["B"] if r["_kind"] == "EXPERIMENT_DATA_FRAG" and r["event"] == "FastDDS_receiver_enter")
            if mutation == "fragment_hole": rows["B"].remove(target)
            elif mutation == "hash": target["native_hash"] = "f" * 16
            elif mutation == "old_run": target["run"] = "fedcba9876543210"
            else:
                row = next(r for r in rows["B"] if r["_kind"] == "EXPERIMENT_UNQUALIFIED" and r["event"] == "FastDDS_receiver_enter" and r["invocation"] == target["invocation"])
                row["receiver_lease_id"] = "999"
            result = self.analyze(config, rows)
            with self.subTest(mutation=mutation): self.assertFalse(result["native_proof_pass"])

    def test_measure_formal_cannot_replace_missing_warmup_identity(self):
        config, rows = synthetic_native("MEASURE", 1)
        sn = sample(rows, "A", phase="WARMUP")["writer_sn"]
        replacement = sample(rows, "A", phase="FORMAL")["writer_sn"]
        for side in "AB":
            for row in rows[side]:
                if row.get("writer_guid") == config["request_guid"] and row.get("sequence_lo") == str(sn):
                    row["sequence_lo"] = str(replacement)
        self.assertFalse(self.analyze(config, rows)["native_proof_pass"])


class EvidenceTests(unittest.TestCase):
    def analyze(self, config, rows, **kwargs):
        return ANALYZER.analyze_records(config, rows, PLAN, require_native=False, **kwargs)

    def test_full_frozen_trace_is_host_only(self):
        result = self.analyze(*synthetic_window())
        self.assertTrue(result["core_pass"], result["errors"])
        self.assertFalse(result["native_proof_pass"])
        self.assertFalse(result["device_tested"])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["p50_ns"], 2000)
        self.assertEqual(result["cases"]["LOAD_1K"]["goodput_bytes_per_second"], 5120)

    def test_host_reference_requires_explicit_cli_choice(self):
        config, rows = synthetic_window()
        self.assertFalse(ANALYZER.analyze_records(config, rows, PLAN)["pass"])

    def test_clock_units_and_negative_or_zero_rtt_rejected(self):
        for mutation in ("unit", "negative", "zero"):
            config, rows = synthetic_window()
            row = sample(rows, "A")
            if mutation == "unit": config["clock_unit"] = "us"
            else: row["first_take_ns"] = row["write_before_ns"] + (-1 if mutation == "negative" else 0)
            with self.subTest(mutation=mutation):
                self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_wrong_source_old_run_bad_payload_never_goodput(self):
        for field, value in (("source_guid", "c" * 32), ("run", "fedcba9876543210"), ("vector_bytes", 999), ("flags", INVALID)):
            config, rows = synthetic_window()
            sample(rows, "B", "LOAD_1K")[field] = value
            result = self.analyze(config, rows)
            with self.subTest(field=field):
                self.assertFalse(result["core_pass"])
                self.assertLess(result["cases"]["LOAD_1K"]["unique_reader_samples"], 10)

    def test_duplicate_consolidated_record_and_nonremoving_read_rejected(self):
        for mutation in ("duplicate", "read"):
            config, rows = synthetic_window()
            if mutation == "duplicate": rows["B"].append(copy.deepcopy(sample(rows, "B", "LOAD_1K")))
            else: sample(rows, "B", "LOAD_1K")["operation"] = "read"
            with self.subTest(mutation=mutation):
                self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_failed_request_with_delivery_excluded_from_rtt(self):
        config, rows = synthetic_window()
        sample(rows, "A")["write_rc"] = 10
        result = self.analyze(config, rows)
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["count"], 2)
        self.assertEqual(result["cases"]["RTT_1K"]["write_failed"], 1)
        self.assertEqual(result["cases"]["RTT_1K"]["delivered_after_failed_write"], 1)

    def test_missing_expected_and_missing_timeout_are_incomplete(self):
        config, rows = synthetic_window()
        rows["A"].remove(sample(rows, "A"))
        self.assertFalse(self.analyze(config, rows)["complete_evidence"])
        config, rows = synthetic_window()
        a = sample(rows, "A")
        a.update(flags=ATTEMPTED, first_take_ns=None, source_guid=None, source_sn=None)
        result = self.analyze(config, rows)
        self.assertFalse(result["complete_evidence"])
        self.assertTrue(any("timeout" in e for e in result["errors"]))

    def test_late_is_separate_and_timeout_stops_new_rtt(self):
        config, rows = synthetic_window()
        a = sample(rows, "A")
        a["flags"] |= TIMEOUT | LATE
        result = self.analyze(config, rows)
        self.assertFalse(result["core_pass"])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["count"], 2)
        self.assertEqual(result["cases"]["RTT_1K"]["timed_out"], 1)

    def test_unmarked_response_after_request_timeout_is_not_success(self):
        config, rows = synthetic_window()
        row = sample(rows, "A")
        row["first_take_ns"] = row["write_before_ns"] + 2_000_000_001
        result = self.analyze(config, rows)
        self.assertFalse(result["core_pass"])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["count"], 2)

    def test_equal_request_or_phase_deadline_is_not_success(self):
        for boundary in ("request", "phase", "observation"):
            config, rows = synthetic_window()
            row = sample(rows, "A")
            if boundary == "request": row["first_take_ns"] = row["write_before_ns"] + 2_000_000_000
            else:
                summary = next(r for r in rows["A"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "RTT_1K")
                summary["phase_deadline_ns" if boundary == "phase" else "observation_deadline_ns"] = row["first_take_ns"]
            result = self.analyze(config, rows)
            with self.subTest(boundary=boundary):
                self.assertFalse(result["core_pass"])
                self.assertLess(result["cases"]["RTT_1K"]["rtt"]["count"], 3)

    def test_load_equal_fixed_or_observation_deadline_uses_immutable_boundary(self):
        for boundary in ("fixed", "observation"):
            config, rows = synthetic_window()
            row = sample(rows, "B", "LOAD_1K")
            summary = next(r for r in rows["B"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "LOAD_1K")
            row["first_take_ns"] = summary["window_end_ns" if boundary == "fixed" else "observation_deadline_ns"]
            row["flags"] = VALIDATED | LATE
            summary["observation_end_ns"] = row["first_take_ns"] + 1_000_000_000
            result = self.analyze(config, rows)
            self.assertEqual(result["cases"]["LOAD_1K"]["unique_reader_samples"], 9)
            if boundary == "observation":
                self.assertFalse(result["core_pass"], "nominal deadline equality must require AFTER_OBSERVATION")
                self.assertEqual(result["cases"]["LOAD_1K"]["after_observation"], 1)

    def test_after_observation_flags_are_counted_separately_from_goodput(self):
        config, rows = synthetic_window()
        row = sample(rows, "B", "LOAD_1K")
        summary = next(r for r in rows["B"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "LOAD_1K")
        row.update(first_take_ns=summary["observation_deadline_ns"], flags=VALIDATED | LATE | AFTER)
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        self.assertEqual(result["cases"]["LOAD_1K"]["late"], 1)
        self.assertEqual(result["cases"]["LOAD_1K"]["after_observation"], 1)
        self.assertEqual(result["cases"]["LOAD_1K"]["goodput_bytes_per_second"], 4608)

    def test_absolute_clock_zero_is_valid_but_zero_rtt_is_clock_anomaly(self):
        for side, case in (("A", "RTT_1K"), ("B", "LOAD_1K")):
            config, rows = synthetic_window()
            summary = next(r for r in rows[side] if r["_kind"] == "PERF_SUMMARY" and r["case"] == case)
            offset = summary["window_start_ns"]
            for row in rows[side]:
                if row.get("case") == case:
                    for field in ("write_before_ns", "write_after_ns", "first_take_ns", "echo_before_ns", "echo_after_ns",
                                  "window_start_ns", "window_end_ns", "phase_deadline_ns", "observation_deadline_ns"):
                        if isinstance(row.get(field), int): row[field] -= offset
            result = self.analyze(config, rows)
            with self.subTest(side=side): self.assertTrue(result["core_pass"], result["errors"])
        config, rows = synthetic_window()
        row = sample(rows, "A")
        row.update(first_take_ns=row["write_before_ns"], flags=row["flags"] | 512)
        next(r for r in rows["A"] if r["_kind"] == "PERF_COUNTERS")["invalid_clock"] = 1
        result = self.analyze(config, rows)
        self.assertFalse(result["core_pass"])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["count"], 2)
        self.assertFalse(any("sample_flags" in error for error in result["errors"]))

    def test_actual_write_bytes_counted_even_if_attempt_flag_corrupt(self):
        for side, bit in (("A", ATTEMPTED), ("B", ECHO)):
            config, rows = synthetic_window()
            row = sample(rows, side)
            row["flags"] &= ~bit
            result = self.analyze(config, rows)
            with self.subTest(side=side):
                self.assertFalse(result["core_pass"])
                self.assertEqual(result["submitted_vector_reservations_bytes"], 1656000)

    def test_late_load_requires_actual_end_and_never_increases_goodput(self):
        config, rows = synthetic_window()
        b = sample(rows, "B", "LOAD_1K")
        summary = next(r for r in rows["B"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "LOAD_1K")
        b.update(flags=VALIDATED | LATE, first_take_ns=summary["window_end_ns"] + 1)
        result = self.analyze(config, rows)
        self.assertEqual(result["cases"]["LOAD_1K"]["unique_reader_samples"], 9)
        self.assertEqual(result["cases"]["LOAD_1K"]["late"], 1)
        self.assertEqual(result["cases"]["LOAD_1K"]["goodput_bytes_per_second"], 4608)

    def test_formal_observation_counter_overflow_incomplete(self):
        config, rows = synthetic_window()
        rows["A"].append({"_kind": "PERF_OBSERVATION", "run": config["run"], "side": "A",
                         "scope": "CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC", "counter_overflow": True})
        self.assertFalse(self.analyze(config, rows)["complete_evidence"])

    def add_actual_diagnostic_rows(self, config, rows):
        observed = copy.deepcopy(FIXTURE["device_diagnostic_scope_fixture"]["rows"])
        for row in observed:
            row.update(run=config["run"], side="A")
        rows["A"].extend(observed)
        return observed

    def test_actual_device_diagnostic_scopes_are_separate_from_execution_scope(self):
        config, rows = synthetic_window()
        self.add_actual_diagnostic_rows(config, rows)
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        instrument = result["instrumentation"]["A"]
        self.assertEqual(instrument["diagnostic_totals"]["sdk_send_failures"], 0)
        self.assertEqual(instrument["diagnostic_totals"]["nonzero_event_values"], 3)
        self.assertEqual(instrument["adapter_snapshots"][-1]["scope"], "BOUNDARY_SAMPLE_NOT_HIGH_WATER")
        self.assertEqual(instrument["diagnostic_totals"]["rx_admission_prefix_overflow"], 0)

    def test_diagnostic_kind_scope_whitelist_is_strict(self):
        for kind in ("PERF_OBSERVATION", "PERF_ADAPTER"):
            for value in (None, "BYTES_NATIVE", "EXACT_HIGH_WATER", "CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC" if kind == "PERF_ADAPTER" else "BOUNDARY_SAMPLE_NOT_HIGH_WATER"):
                config, rows = synthetic_window()
                observed = self.add_actual_diagnostic_rows(config, rows)
                row = next(r for r in observed if r["_kind"] == kind)
                if value is None: row.pop("scope")
                else: row["scope"] = value
                with self.subTest(kind=kind, value=value):
                    self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_diagnostic_scope_cannot_be_used_on_business_rows(self):
        config, rows = synthetic_window()
        sample(rows, "A")["scope"] = "CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC"
        result = self.analyze(config, rows)
        self.assertFalse(result["core_pass"])
        self.assertTrue(any("record_scope" in error for error in result["errors"]))

    def test_actual_prefix_overflow_stays_diagnostic_but_snapshot_overflow_fails(self):
        config, rows = synthetic_window()
        observed = self.add_actual_diagnostic_rows(config, rows)
        next(r for r in observed if r["_kind"] == "PERF_ADAPTER")["rx_admission_overflow"] = 2000
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        self.assertEqual(result["instrumentation"]["A"]["diagnostic_totals"]["rx_admission_prefix_overflow"], 2000)
        self.assertEqual(result["cases"]["LOAD_1K"]["missing_reader_samples"], 0)
        next(r for r in observed if r["_kind"] == "PERF_OBSERVATION_STORAGE")["overflow"] = True
        self.assertFalse(self.analyze(config, rows)["complete_evidence"])

    def actual_resource_case(self):
        config, rows = synthetic_window()
        actual = copy.deepcopy(FIXTURE["device_resource_boundary_fixture"]["rows"])
        summary = next(r for r in rows["A"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "RTT_1K")
        cpu = next(r for r in actual if r["_kind"] == "PERF_SUMMARY")
        for field in ("cpu_wall_start_ns", "cpu_wall_end_ns", "cpu_start_ns", "cpu_end_ns"):
            summary[field] = cpu[field]
        for row in actual:
            if row["_kind"] == "PERF_RESOURCE":
                row.update(run=config["run"], scope=config["scope"], stage=summary["stage"])
                rows["A"].append(row)
        return config, rows, summary

    def test_actual_rss_thread_fd_boundaries_mapped_without_changing_cpu_interval(self):
        config, rows, summary = self.actual_resource_case()
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        measured = result["cases"]["RTT_1K"]["resource_intervals"]["A"]
        self.assertEqual(measured["rss_start_bytes"], 29802496)
        self.assertEqual(measured["rss_end_bytes"], 29929472)
        self.assertEqual((measured["threads_start"], measured["threads_end"]), (15, 16))
        self.assertEqual((measured["fd_start"], measured["fd_end"]), (17, 17))
        expected_cpu = 100 * (summary["cpu_end_ns"] - summary["cpu_start_ns"]) / (summary["cpu_wall_end_ns"] - summary["cpu_wall_start_ns"])
        self.assertEqual(measured["cpu_percent"], expected_cpu)
        self.assertEqual(measured["cpu_wall_end_ns"], summary["cpu_wall_end_ns"])
        self.assertEqual(measured["resource_sample_wall_end_ns"], 46225144071079)
        self.assertGreater(measured["resource_sample_wall_end_ns"], measured["cpu_wall_end_ns"])
        self.assertIn("outside_active_cpu_interval", measured["resource_sample_scope"])

    def test_missing_or_unknown_resource_value_is_unobserved_not_zero(self):
        config, rows, _ = self.actual_resource_case()
        after = next(r for r in rows["A"] if r["_kind"] == "PERF_RESOURCE" and r["label"] == "stage_after")
        after["rss_bytes"] = after["fd_count"] = after["threads"] = None
        result = self.analyze(config, rows)
        measured = result["cases"]["RTT_1K"]["resource_intervals"]["A"]
        self.assertEqual(measured["rss_start_bytes"], 29802496)
        for field in ("rss_end_bytes", "fd_end", "threads_end"):
            self.assertIsNone(measured[field])
        self.assertIsNone(result["cases"]["RTT_1K"]["resource_intervals"]["B"]["rss_end_bytes"])

    def test_resource_stage_or_label_cannot_cross_join_another_case(self):
        config, rows, summary = self.actual_resource_case()
        after = next(r for r in rows["A"] if r["_kind"] == "PERF_RESOURCE" and r["label"] == "stage_after")
        after["stage"] = summary["stage"] + 1
        result = self.analyze(config, rows)
        measured = result["cases"]["RTT_1K"]["resource_intervals"]["A"]
        self.assertEqual(measured["rss_start_bytes"], 29802496)
        self.assertIsNone(measured["rss_end_bytes"])
        before = next(r for r in rows["A"] if r["_kind"] == "PERF_RESOURCE" and r["label"] == "stage_before")
        rows["A"].append(copy.deepcopy(before))
        self.assertFalse(self.analyze(config, rows)["complete_evidence"])

    def test_future_stage_rejected_counter_fails(self):
        config, rows = synthetic_window()
        next(r for r in rows["B"] if r["_kind"] == "PERF_COUNTERS")["inactive"] = 1
        self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_declared_summary_cannot_inflate_validated_count(self):
        config, rows = synthetic_window()
        summary = next(r for r in rows["B"] if r["_kind"] == "PERF_SUMMARY" and r["case"] == "LOAD_1K")
        summary["valid_unique"] = 11
        self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_scope_mixing_and_failed_cleanup_are_retained_failures(self):
        for mutation in ("scope", "cleanup"):
            config, rows = synthetic_window()
            if mutation == "scope": sample(rows, "A")["scope"] = "BYTES_NATIVE"
            else: next(r for r in rows["A"] if r["_kind"] == "PERF_COUNTERS")["cleanup_failure"] = "dds_delete"
            with self.subTest(mutation=mutation): self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_actual_rate_and_skips_are_not_requested_rate(self):
        config, rows = synthetic_window()
        a = sample(rows, "A", "LOAD_1K", sequence=2)
        a.update(flags=NOT_ATTEMPTED | MISSED, write_before_ns=None, write_after_ns=None, write_rc=None, writer_guid=None, writer_sn=None)
        b = sample(rows, "B", "LOAD_1K", sequence=2)
        b.update(flags=NOT_ATTEMPTED, first_take_ns=None, source_guid=None, source_sn=None)
        result = self.analyze(config, rows)
        case = result["cases"]["LOAD_1K"]
        self.assertEqual(case["requested_hz"], 100)
        self.assertEqual(case["attempt_hz"], 4.5)
        self.assertEqual(case["missed_slots"], 1)
        self.assertEqual(case["goodput_bytes_per_second"], 4608)

    def test_load_no_data_is_unobserved_and_not_zero(self):
        config, rows = synthetic_window()
        for row in rows["B"]:
            if row.get("case") == "LOAD_1K":
                if row["_kind"] == "PERF_SAMPLE": row.update(flags=NOT_ATTEMPTED, first_take_ns=None, source_guid=None, source_sn=None)
                else: row.update(window_start_ns=None, window_end_ns=None, no_first_sample=True)
        result = self.analyze(config, rows)
        self.assertIsNone(result["cases"]["LOAD_1K"]["goodput_bytes_per_second"])

    def test_cross_board_clock_change_does_not_change_rtt(self):
        config, rows = synthetic_window()
        for row in rows["B"]:
            for key in ("first_take_ns", "echo_before_ns", "echo_after_ns", "window_start_ns", "window_end_ns",
                        "phase_deadline_ns", "observation_deadline_ns"):
                if isinstance(row.get(key), int): row[key] += 50_000_000_000
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["mean_ns"], 2000)

    def test_bad_qos_and_measurement_overflow_fail(self):
        for mutation in ("qos", "overflow", "capacity"):
            config, rows = synthetic_window()
            if mutation == "qos": rows["A"][0]["effective"]["max_samples"] = 32
            else:
                counters = next(r for r in rows["A"] if r["_kind"] == "PERF_COUNTERS")
                counters["log_overflow_bytes" if mutation == "overflow" else "inbound_capacity"] = 1 if mutation == "overflow" else 1048577
            with self.subTest(mutation=mutation): self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_failed_qos_readback_and_wrong_participant_identity_fail(self):
        config, rows = synthetic_window()
        rows["A"][0]["readback"] = False
        self.assertFalse(self.analyze(config, rows)["core_pass"])
        config, rows = synthetic_window()
        rows["A"].append({"_kind": "PERF_IDENTITY", "run": config["run"], "side": "A",
            "request_guid": "c" * 32, "response_guid": config["response_guid"], "participant_readback": False})
        self.assertFalse(self.analyze(config, rows)["core_pass"])

    def test_native_diagnostic_prefix_overflow_is_not_business_loss(self):
        config, rows = synthetic_window()
        counters = next(r for r in rows["A"] if r["_kind"] == "PERF_COUNTERS")
        counters.update(rx_admission_prefix_overflow=2000, native_error_detail_overflow=5)
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"])
        self.assertEqual(result["diagnostics"]["A"]["rx_admission_prefix_overflow"], 2000)

    def test_measure_formal_statistics_exclude_warmup(self):
        config, rows = synthetic_window("MEASURE", 1)
        row = sample(rows, "A", phase="WARMUP")
        row["first_take_ns"] = row["write_before_ns"] + 1_000_000_000
        result = self.analyze(config, rows)
        self.assertTrue(result["core_pass"], result["errors"][:20])
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["count"], 500)
        self.assertEqual(result["cases"]["RTT_1K"]["rtt"]["max_ns"], 500000)

    def test_repeated_writer_sn_rejected(self):
        config, rows = synthetic_window()
        sample(rows, "A", sequence=2)["writer_sn"] = sample(rows, "A")["writer_sn"]
        sample(rows, "B", sequence=2)["source_sn"] = sample(rows, "B")["source_sn"]
        self.assertFalse(self.analyze(config, rows)["core_pass"])


class OutputTests(unittest.TestCase):
    def test_offline_analysis_cannot_replace_preserved_external_failure(self):
        config, rows = synthetic_window()
        with tempfile.TemporaryDirectory() as temp:
            window = Path(temp)
            (window / "window.json").write_text(json.dumps(config), encoding="utf-8")
            (window / "plan.json").write_text(json.dumps(PLAN), encoding="utf-8")
            for side in "AB":
                (window / f"{side}.raw.stdout.log").write_text("\n".join(
                    r["_kind"] + " " + json.dumps({k: v for k, v in r.items() if not k.startswith("_")})
                    for r in rows[side]), encoding="utf-8")
            execution = {"run": config["run"], "scope": config["scope"], "pass": False, "watchdog": False,
                         "phase_controller": {"pass": False, "first_failure": "premature_duplicate_closed"},
                         "log_overflow_bytes": {"A": 0, "B": 0}}
            source = window / "result.json"
            source.write_text(json.dumps(execution), encoding="utf-8")
            original = source.read_bytes()
            result = ANALYZER.analyze_window(window, window / "plan.json", require_native=False)
            self.assertTrue(result["core_pass"], result["errors"])
            self.assertTrue(result["analysis_pass"])
            self.assertFalse(result["pass"])
            self.assertEqual(result["external_execution_record"]["status"], "FAIL")
            self.assertEqual(result["external_execution_record"]["phase_controller"], execution["phase_controller"])
            self.assertEqual(source.read_bytes(), original)

    def test_parser_rejects_duplicate_json_fields_and_bad_json(self):
        for text in ('PERF_SAMPLE {"run":"x","run":"y"}', 'PERF_SAMPLE {broken}',
                     'PERF_CONTROL event=READY event=OTHER'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                ANALYZER.parse_text(text, "A")

    def test_parser_keeps_integer_nanoseconds_and_unknown(self):
        row = ANALYZER.parse_text('PERF_SAMPLE {"first_take_ns":9007199254740993,"write_rc":null}', "A")[0]
        self.assertEqual(row["first_take_ns"], 9007199254740993)
        self.assertIsNone(row["write_rc"])

    def test_repeat_pooling_uses_samples_not_mean_of_p99(self):
        reports = []
        for repeat, values in enumerate(([1] * 100, [100], [200]), 1):
            report = ANALYZER.analyze_records(*synthetic_window("MEASURE", repeat), PLAN, require_native=False)
            report["run"] = f"{repeat:016x}"
            report["cases"]["RTT_1K"]["success_rtt_ns"] = values
            report["cases"]["RTT_1K"]["rtt"] = ANALYZER.statistics_ns(values)
            reports.append(report)
        result = ANALYZER.summarize_reports(reports)
        rtt = result["cases"]["RTT_1K"]
        self.assertEqual(rtt["pooled_rtt"]["count"], 102)
        self.assertEqual(rtt["pooled_rtt"]["p99_ns"], 100)
        self.assertEqual(rtt["repeat_p99_range_ns"], [1, 200])
        self.assertEqual([r["rtt"]["count"] for r in rtt["repeats"]], [100, 1, 1])

    def test_repeat_pooling_rejects_same_run_or_mixed_scope(self):
        reports = [ANALYZER.analyze_records(*synthetic_window("MEASURE", i), PLAN, require_native=False) for i in (1, 2, 3)]
        with self.assertRaises(ValueError): ANALYZER.summarize_reports(reports)
        for i, report in enumerate(reports): report["run"] = f"{i+1:016x}"
        reports[2]["scope"] = "BYTES_NATIVE"
        with self.assertRaises(ValueError): ANALYZER.summarize_reports(reports)

    def known_native_repeats(self):
        reports = []
        for repeat in (1, 2, 3):
            config, rows = synthetic_native("MEASURE", repeat)
            run = f"{repeat:016x}"
            config["run"] = run
            for side in "AB":
                for row in rows[side]:
                    if "run" in row: row["run"] = run
            result = ANALYZER.analyze_records(config, rows, PLAN)
            self.assertTrue(result["pass"], result)
            reports.append(result)
        return reports

    def test_pool_requires_same_known_build_contract_but_distinct_raw_logs(self):
        reports = self.known_native_repeats()
        for i, report in enumerate(reports, 1):
            report["source_sha256"] = {"A.raw.stdout.log": str(i) * 64}
            report["source_window"] = f"/synthetic/run-{i}"
        result = ANALYZER.summarize_reports(reports)
        self.assertTrue(result["pass"])
        self.assertEqual(result["measurement_identity"], reports[0]["measurement_identity"])
        self.assertEqual(result["measurement_identity"]["source_state_sha256"], "3" * 64)

    def test_pool_rejects_changed_or_missing_declared_identity(self):
        original = self.known_native_repeats()
        fields = ("plan_sha256", "log_schema_sha256", "build_manifest_sha256",
                  "source_state_sha256", "executable_sha256", "configuration_sha256", "package_artifacts")
        for field in fields:
            for mutation in ("missing", "different"):
                reports = copy.deepcopy(original)
                if mutation == "missing": reports[1].pop(field, None)
                elif field == "package_artifacts": reports[1][field] = [{"name": "fastdds_socket_experiment", "sha256": "a" * 64}]
                else: reports[1][field] = "a" * 64
                with self.subTest(field=field, mutation=mutation), self.assertRaises(ValueError):
                    ANALYZER.summarize_reports(reports)
        # Both declarations can be internally consistent yet identify a
        # different build; this must fail the cross-repeat comparison too.
        for field in fields:
            reports = copy.deepcopy(original)
            nested = "measurement_plan_sha256" if field == "plan_sha256" else field
            value = [{"name": "fastdds_socket_experiment", "sha256": "a" * 64}] if field == "package_artifacts" else "a" * 64
            reports[1][field] = value
            reports[1]["measurement_identity"][nested] = value
            with self.subTest(consistent_but_different=field), self.assertRaises(ValueError):
                ANALYZER.summarize_reports(reports)

    def test_native_missing_source_identity_not_passed(self):
        config, rows = synthetic_native()
        config.pop("source_state_sha256")
        report = ANALYZER.analyze_records(config, rows, PLAN)
        self.assertTrue(report["core_pass"])
        self.assertFalse(report["pass"])
        self.assertFalse(report["measurement_identity_complete"])

    def test_host_unknown_build_receipt_individual_pass_but_pool_refused(self):
        reports = []
        for repeat in (1, 2, 3):
            config, rows = synthetic_window("MEASURE", repeat)
            config["build_manifest_sha256"] = config["source_state_sha256"] = None
            report = ANALYZER.analyze_records(config, rows, PLAN, require_native=False)
            self.assertTrue(report["pass"])
            self.assertFalse(report["measurement_identity_complete"])
            report["run"] = f"{repeat:016x}"
            reports.append(report)
        with self.assertRaises(ValueError): ANALYZER.summarize_reports(reports)

    def test_cli_writes_new_json_csv_and_never_overwrites(self):
        config, rows = synthetic_window()
        with tempfile.TemporaryDirectory() as temp:
            window = Path(temp)
            (window / "window.json").write_text(json.dumps(config), encoding="utf-8")
            (window / "plan.json").write_text(json.dumps(PLAN), encoding="utf-8")
            for side in "AB":
                (window / f"{side}.raw.stdout.log").write_text("\n".join(
                    r["_kind"] + " " + json.dumps({k: v for k, v in r.items() if not k.startswith("_")})
                    for r in rows[side]), encoding="utf-8")
            output = window / "analysis.json"
            command = [sys.executable, str(ROOT / "scripts/analyze_dds_performance.py"), str(window),
                       "--plan", str(window / "plan.json"), "--output", str(output), "--allow-host-reference"]
            first = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0, first.stderr + first.stdout)
            result = json.loads(output.read_text())
            self.assertTrue(result["core_pass"])
            self.assertFalse(result["device_tested"])
            csv_path = window / "analysis.csv"
            self.assertTrue(csv_path.is_file())
            self.assertIn("p99_ns", csv_path.read_text())
            original = output.read_bytes()
            second = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(second.returncode, 0)
            self.assertEqual(output.read_bytes(), original)


class StatisticsTests(unittest.TestCase):
    def test_nearest_rank_not_interpolation(self):
        stats = ANALYZER.statistics_ns([1, 2, 3, 100])
        self.assertEqual(stats, {"count": 4, "min_ns": 1, "mean_ns": 26.5,
            "p50_ns": 2, "p95_ns": 100, "p99_ns": 100, "max_ns": 100})

    def test_empty_population_is_unobserved(self):
        stats = ANALYZER.statistics_ns([])
        self.assertEqual(stats["count"], 0)
        self.assertIsNone(stats["p99_ns"])
        self.assertIsNone(stats["mean_ns"])

    def test_bad_and_zero_time_are_not_silent_samples(self):
        for samples in ([-1], [0], [True], [1.2], [None]):
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                ANALYZER.statistics_ns(samples)

    def test_resource_cpu_single_core_100_and_kib_rss(self):
        result = ANALYZER.resource_interval(
            {"wall_ns": 100, "cpu_ns": 100, "rss_kb": 100, "threads": 2, "fd": 3},
            {"wall_ns": 200, "cpu_ns": 250, "rss_kb": 200, "threads": 4, "fd": 5})
        self.assertEqual(result["cpu_percent"], 150)
        self.assertEqual(result["rss_end_bytes"], 204800)

    def test_resource_bytes_are_not_converted_from_kib_twice(self):
        result = ANALYZER.resource_interval({"rss_bytes": 29802496, "threads": 15, "fd_count": 17},
                                             {"rss_bytes": 29929472, "threads": 16, "fd_count": 17})
        self.assertEqual(result["rss_end_bytes"], 29929472)
        self.assertEqual((result["threads_start"], result["threads_end"]), (15, 16))
        self.assertEqual((result["fd_start"], result["fd_end"]), (17, 17))

    def test_resources_missing_are_not_zero(self):
        result = ANALYZER.resource_interval({}, {})
        self.assertIsNone(result["cpu_percent"])
        self.assertIsNone(result["rss_end_bytes"])

    def test_invalid_resource_intervals_unobserved(self):
        for end in ({"wall_ns": 1, "cpu_ns": 2}, {"wall_ns": 3, "cpu_ns": 0}):
            self.assertIsNone(ANALYZER.resource_interval({"wall_ns": 1, "cpu_ns": 1}, end)["cpu_percent"])


class PlanTests(unittest.TestCase):
    def test_frozen_budget_includes_both_rtt_directions_and_warmup(self):
        result = ANALYZER.validate_plan(PLAN)
        self.assertEqual(result["measure_vector_bytes"], 75805344)
        self.assertEqual(result["trace_vector_bytes"], 1656000)
        self.assertEqual(result["task_vector_bytes"], 229072032)

    def test_changed_expectations_rejected_before_results(self):
        mutations = [("formal_count", 501), ("business_bytes", 1023), ("warmup_count", 0)]
        for key, value in mutations:
            bad = copy.deepcopy(PLAN)
            bad["cases"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                ANALYZER.validate_plan(bad)

    def test_changed_cap_or_cache_is_not_accepted(self):
        for key, value in (("per_window_cap", 268435456), ("task_cap", 1073741824)):
            bad = copy.deepcopy(PLAN)
            bad["budgets"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                ANALYZER.validate_plan(bad)
        bad = copy.deepcopy(PLAN)
        bad["qos"]["max_samples"] = 1000
        with self.assertRaises(ValueError):
            ANALYZER.validate_plan(bad)


if __name__ == "__main__":
    unittest.main()
