"""Offline DDS bounded-performance evidence analysis; native gates remain external.

Times are integer nanoseconds from one process's monotonic clock. No cross-board
subtraction, half-RTT estimate, transport byte goodput, or averaged percentiles.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import hashlib
import json
import math
from pathlib import Path
import re

PLAN_SHA256 = "dce5de2da33010dcd60df84363f2012d645a8d394d10c0e1fdb7a4c8c61bc1ef"
PLAN_CANONICAL_SHA256 = "50d8bbd304d8520a97c646f07bc9cdd26f6f3ef63ee989091e3c00ff9990f45f"
MAX_U64 = (1 << 64) - 1
NOT_ATTEMPTED, ATTEMPTED, VALIDATED, TIMED_OUT = 1, 2, 4, 8
LATE, MISSED_SLOT, ECHO_RESERVED, AFTER_OBSERVATION, INVALID = 16, 32, 64, 128, 256
INVALID_CLOCK = 512
QOS_FIELDS = ("reliability", "durability", "history", "max_samples", "max_instances",
              "max_samples_per_instance", "allocated_samples", "publish_mode",
              "max_blocking_time_s", "data_sharing", "builtin_transports", "intraprocess")
DIAGNOSTIC_SCOPES = {
    "PERF_OBSERVATION": "CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC",
    "PERF_ADAPTER": "BOUNDARY_SAMPLE_NOT_HIGH_WATER",
}


def natural(value):
    return isinstance(value, int) and not isinstance(value, bool) and 0 <= value <= MAX_U64


def statistics_ns(samples):
    values = list(samples)
    if any(not natural(x) or x == 0 for x in values):
        raise ValueError("latency must be positive integer nanoseconds")
    values.sort()
    result = {"count": len(values), "min_ns": None, "mean_ns": None,
              "p50_ns": None, "p95_ns": None, "p99_ns": None, "max_ns": None}
    if values:
        result.update(min_ns=values[0], mean_ns=sum(values) / len(values), max_ns=values[-1])
        for p in (50, 95, 99):
            result[f"p{p}_ns"] = values[(p * len(values) + 99) // 100 - 1]
    return result


def resource_interval(start, end):
    result = {"cpu_percent": None, "cpu_convention": "single_core_100_percent",
              "rss_start_bytes": None, "rss_end_bytes": None,
              "rss_scope": "benchmark_process_only", "threads_start": None, "threads_end": None,
              "fd_start": None, "fd_end": None}
    clocks = [start.get("wall_ns"), end.get("wall_ns"), start.get("cpu_ns"), end.get("cpu_ns")]
    if all(natural(value) for value in clocks) and clocks[1] > clocks[0] and clocks[3] >= clocks[2]:
        result["cpu_percent"] = 100 * (clocks[3] - clocks[2]) / (clocks[1] - clocks[0])
    for label, row in (("start", start), ("end", end)):
        if natural(row.get("rss_bytes")):
            result[f"rss_{label}_bytes"] = row["rss_bytes"]
        elif natural(row.get("rss_kb")):
            result[f"rss_{label}_bytes"] = row["rss_kb"] * 1024
        if natural(row.get("threads")):
            result[f"threads_{label}"] = row["threads"]
        descriptor_count = row.get("fd_count", row.get("fd"))
        if natural(descriptor_count):
            result[f"fd_{label}"] = descriptor_count
    return result


def validate_plan(plan):
    canonical = json.dumps(plan, sort_keys=True, separators=(",", ":")).encode()
    if hashlib.sha256(canonical).hexdigest() != PLAN_CANONICAL_SHA256:
        raise ValueError("plan differs from independently reviewed revision 3")
    measure = trace = 0
    for case in plan["cases"]:
        direction_count = 2 if case["kind"] == "RTT" else 1
        count = case.get("formal_count", case.get("planned_slots"))
        measure += (count + case["warmup_count"]) * case["vector_bytes"] * direction_count
        trace += case["trace_count"] * case["vector_bytes"] * direction_count
    if measure != 75805344 or trace != 1656000:
        raise ValueError("frozen vector reservation mismatch")
    return {"measure_vector_bytes": measure, "trace_vector_bytes": trace,
            "task_vector_bytes": trace + 3 * measure}


def parse_text(text, side):
    """Parse only documented measurement and native-witness prefixes, fail closed."""
    rows = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.startswith(("PERF_", "EXPERIMENT_", "stage=")):
            continue
        prefix, _, rest = line.partition(" ")
        if rest.startswith("{"):
            def unique_pairs(pairs):
                result = {}
                for key, value in pairs:
                    if key in result:
                        raise ValueError(f"duplicate JSON field {side}:{line_number}:{key}")
                    result[key] = value
                return result
            row = json.loads(rest, object_pairs_hook=unique_pairs)
            if not isinstance(row, dict):
                raise ValueError(f"record must be object {side}:{line_number}")
        else:
            row = {}
            for token in (line if prefix.startswith("stage=") else rest).split():
                if "=" in token:
                    key, value = token.split("=", 1)
                    if key in row:
                        raise ValueError(f"duplicate field {side}:{line_number}:{key}")
                    row[key] = value
        if "_kind" in row or "_side" in row or "_line" in row:
            raise ValueError(f"reserved parser metadata {side}:{line_number}")
        row.update(_kind=prefix, _side=side, _line=line_number)
        rows.append(row)
    return rows


def flags(row, bit):
    return natural(row.get("flags")) and bool(row["flags"] & bit)


def positive_ns(value):
    return natural(value) and 0 < value <= (1 << 63) - 1


def timestamp_ns(value):
    return natural(value) and value <= (1 << 63) - 1


def guid(value):
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{32}", value) and value != "0" * 32


def rate(count, begin, end):
    return count * 1_000_000_000 / (end - begin) if timestamp_ns(begin) and timestamp_ns(end) and end > begin else None


def digest_json(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def hash_identity(value):
    return isinstance(value, str) and bool(re.fullmatch(r"[0-9a-f]{64}", value))


def package_identity(artifacts):
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("package_artifacts_unobserved")
    result = []
    for row in artifacts:
        if (not isinstance(row, dict) or not isinstance(row.get("name"), str)
                or not re.fullmatch(r"[A-Za-z0-9_.+-]+", row["name"])
                or not hash_identity(row.get("sha256"))):
            raise ValueError("invalid_package_artifact_identity")
        result.append({"name": row["name"], "sha256": row["sha256"]})
    if len({row["name"] for row in result}) != len(result):
        raise ValueError("duplicate_package_artifact_identity")
    return sorted(result, key=lambda row: row["name"])


def measurement_identity(config, plan, plan_sha256):
    """Propagate pre-run selected-build declarations, never infer compiled source.

    Paths and run-local observations are deliberately excluded. Validating the
    selected files, deployed process maps and guards remains the external gate.
    """
    errors = []
    contract = config.get("task_contract", {})
    selection = config.get("build_selection", {})
    if not isinstance(contract, dict) or not isinstance(selection, dict):
        raise ValueError("invalid_pre_run_contract_or_build_selection")

    def declared(label, *values):
        supplied = [value for value in values if value is not None]
        if not supplied:
            errors.append(label + ":UNOBSERVED")
            return None
        if any(not hash_identity(value) for value in supplied):
            errors.append(label + ":invalid_sha256")
            return None
        if len(set(supplied)) != 1:
            errors.append(label + ":inconsistent_pre_run_declarations")
        return supplied[0]

    identity = {"identity_schema": 1, "execution_scope": config.get("scope")}
    identity["measurement_plan_sha256"] = declared("measurement_plan_sha256", config.get("measurement_plan_sha256"),
        config.get("matrix_sha256"), config.get("case_matrix_sha256"), contract.get("measurement_plan_sha256"),
        contract.get("case_matrix_sha256"), selection.get("case_matrix_sha256"))
    if identity["measurement_plan_sha256"] is not None and identity["measurement_plan_sha256"] != plan_sha256:
        errors.append("measurement_plan_sha256:differs_from_supplied_plan")
    identity["log_schema_sha256"] = declared("log_schema_sha256", config.get("log_schema_sha256"), contract.get("log_schema_sha256"))
    identity["build_manifest_sha256"] = declared("build_manifest_sha256", config.get("build_manifest_sha256"), selection.get("manifest_sha256"))
    identity["source_state_sha256"] = declared("source_state_sha256", config.get("source_state_sha256"), selection.get("source_state_sha256"))
    identity["executable_sha256"] = declared("executable_sha256", config.get("executable_sha256"), config.get("main_sha256"))
    try:
        identity["package_artifacts"] = package_identity(config.get("package_artifacts"))
        if identity["executable_sha256"] not in {row["sha256"] for row in identity["package_artifacts"]}:
            errors.append("executable_sha256:not_in_declared_package")
        if "artifacts" in selection:
            selected = selection["artifacts"]
            selected_rows = [{"name": name, "sha256": item.get("sha256")} for name, item in selected.items()] if isinstance(selected, dict) else None
            if package_identity(selected_rows) != identity["package_artifacts"]:
                errors.append("package_artifacts:differs_from_selected_build")
    except (ValueError, TypeError, AttributeError) as error:
        identity["package_artifacts"] = None
        errors.append(str(error))
    configuration = {"contract_schema": 1, "execution_scope": config.get("scope"), "mode": config.get("mode"),
        "profile": config.get("profile", config.get("scope")), "rx_drop": config.get("rx_drop"),
        "clock_unit": config.get("clock_unit"), "host_contract": config.get("scope") == "HOST_CONTRACT",
        "contract_counts": config.get("contract_counts"), "contract_window_ns": config.get("contract_window_ns"),
        "type": plan["type"], "cases": plan["cases"], "qos": plan["qos"], "timing": plan["timing"],
        "load": plan["load"], "warmup": plan["warmup"], "observation": plan["observation"], "budgets": plan["budgets"]}
    identity["configuration_sha256"] = digest_json(configuration)
    return identity, errors


def report_measurement_identity(report):
    """Reject missing/stale aliases as well as a differing nested identity."""
    identity = report.get("measurement_identity")
    if (report.get("schema") != 1 or not isinstance(identity, dict) or identity.get("identity_schema") != 1
            or not report.get("measurement_identity_complete") or report.get("measurement_identity_errors")):
        raise ValueError("repeat_measurement_identity_missing_or_incomplete")
    if identity.get("execution_scope") != report.get("scope"):
        raise ValueError("repeat_identity_scope_mismatch")
    for field in ("measurement_plan_sha256", "log_schema_sha256", "build_manifest_sha256", "source_state_sha256", "executable_sha256", "configuration_sha256"):
        alias = "plan_sha256" if field == "measurement_plan_sha256" else field
        if not hash_identity(identity.get(field)) or report.get(alias) != identity[field]:
            raise ValueError("repeat_identity_missing_or_inconsistent:" + field)
    if package_identity(report.get("package_artifacts")) != identity.get("package_artifacts"):
        raise ValueError("repeat_identity_package_mismatch")
    return identity


def analyze_records(config, rows, plan, require_native=True, processes=None, plan_sha256=PLAN_SHA256):
    """Analyze a complete frozen window; all result counts derive from raw rows.

    The config is a pre-run manifest. Native external provenance/cleanup gates are
    reported separately and cannot be supplied by measurement PASS text.
    """
    budget = validate_plan(plan)
    identity, identity_errors = measurement_identity(config, plan, plan_sha256)
    errors, completeness_errors, cases, diagnostics = [], [], {}, {}

    def need(condition, reason, incomplete=False):
        if not condition:
            errors.append(reason)
            if incomplete:
                completeness_errors.append(reason)
        return bool(condition)

    run, mode, repeat = config.get("run"), config.get("mode"), config.get("repeat")
    need(isinstance(run, str) and bool(re.fullmatch(r"[0-9a-f]{16}", run)), "invalid_run")
    need(mode in ("TRACE", "MEASURE"), "invalid_mode")
    need((mode == "TRACE" and repeat == 1) or (mode == "MEASURE" and repeat in (1, 2, 3)), "invalid_repeat")
    need(config.get("clock_unit") == "ns", "clock_unit_must_be_ns")
    need(guid(config.get("request_guid")) and guid(config.get("response_guid"))
         and config.get("request_guid") != config.get("response_guid"), "exact_writer_guids_required")
    contract_scope = config.get("scope") == "HOST_CONTRACT"
    native_scope = config.get("scope") == "BYTES_NATIVE"
    need(config.get("scope") in ("HOST_REFERENCE_UDP", "HOST_CONTRACT", "BYTES_NATIVE"), "explicit_execution_scope")
    if contract_scope:
        need(config.get("contract_window_ns") == 200000000, "frozen_host_contract_window")
    records, summaries, invalid_keys, seen_writer_sn = {}, {}, set(), {"A": set(), "B": set()}
    resource_boundaries = defaultdict(list)
    plan_cases = {case["id"]: case for case in plan["cases"]}
    phases = ["FORMAL"] if mode == "TRACE" else ["WARMUP", "FORMAL"]
    expected = {}
    for case_id, case in plan_cases.items():
        for phase in phases:
            count = case["trace_count"] if mode == "TRACE" else case["warmup_count"] if phase == "WARMUP" else case.get("formal_count", case.get("planned_slots"))
            if contract_scope:
                # Counts must come from an explicit pre-run host-only manifest;
                # they never relax the frozen native plan or its reservation.
                count = config.get("contract_counts", {}).get(case_id, {}).get(phase)
                frozen_contract_count = 2 if phase == "WARMUP" else case["trace_count"]
                if not need(natural(count) and count == frozen_contract_count, "host_contract_count:" + case_id + ":" + phase):
                    count = 0
            expected[case_id, phase] = count

    for side in "AB":
        side_rows = rows.get(side, [])
        for row in side_rows:
            kind = row.get("_kind")
            if not kind or not kind.startswith("PERF_") or kind == "PERF_REFERENCE":
                continue
            identity_ok = need(row.get("run") == run and row.get("side") == side, "record_identity:" + side, True)
            if kind in DIAGNOSTIC_SCOPES:
                identity_ok &= need(row.get("scope") == DIAGNOSTIC_SCOPES[kind], "diagnostic_scope:" + side + ":" + kind, True)
                for field, expected_value in (("mode", mode), ("repeat", repeat)):
                    if field in row:
                        identity_ok &= need(row[field] == expected_value, "diagnostic_execution_identity:" + side + ":" + field, True)
            elif "scope" in row:
                identity_ok &= need(row["scope"] == config.get("scope"), "record_scope:" + side, True)
            if kind == "PERF_SAMPLE":
                key = side, row.get("case"), row.get("phase"), row.get("sequence")
                shape_ok = need(row.get("mode") == mode and row.get("repeat") == repeat, "sample_mode_repeat:" + str(key), True)
                count = expected.get((key[1], key[2]), 0)
                shape_ok &= need(natural(key[3]) and 1 <= key[3] <= count, "unexpected_sample:" + str(key), True)
                shape_ok &= need(natural(row.get("flags")) and row["flags"] < 1024, "sample_flags:" + str(key), True)
                shape_ok &= need(natural(row.get("duplicates")), "duplicate_counter:" + str(key), True)
                shape_ok &= need(row.get("operation", "take") == "take", "nonremoving_read_is_not_unique_take:" + str(key))
                if key in records:
                    need(False, "duplicate_consolidated_record:" + str(key), True)
                    invalid_keys.add(key)
                else:
                    records[key] = row
                if not identity_ok or not shape_ok:
                    invalid_keys.add(key)
            elif kind in ("PERF_SUMMARY", "PERF_PHASE"):
                key = side, row.get("case"), row.get("phase")
                if key in summaries:
                    need(False, "duplicate_phase_summary:" + str(key), True)
                summaries[key] = row
                need(row.get("mode") == mode and row.get("repeat") == repeat, "summary_mode_repeat:" + str(key), True)
                need(row.get("planned") == expected.get((key[1], key[2])), "summary_planned:" + str(key), True)
                need(row.get("clock_unit", config.get("clock_unit")) == "ns", "summary_clock_unit:" + str(key))
            elif kind == "PERF_OBSERVATION":
                need(row.get("counter_overflow") in (False, 0), "observer_counter_overflow:" + side, True)
                # These observations cannot manufacture exact queue high-water
                # or wire-byte metrics; native nonzero handles are not failures.
                need(row.get("queue_hwm_exact") is None and row.get("wire_bytes") is None,
                     "observer_unobserved_metric_scope:" + side)
            elif kind == "PERF_OBSERVATION_STORAGE":
                need(row.get("overflow") is False, "observer_storage_overflow:" + side, True)
            elif kind == "PERF_RESOURCE" and row.get("label") in ("stage_before", "stage_after"):
                resource_identity_ok = need(row.get("mode") == mode and row.get("repeat") == repeat
                    and natural(row.get("stage")), "resource_stage_identity:" + side, True)
                if identity_ok and resource_identity_ok:
                    resource_boundaries[side, row["stage"], row["label"]].append(row)
            elif kind == "PERF_IDENTITY":
                if "participant_readback" in row:
                    need(row["participant_readback"] is True, "participant_readback_failed:" + side)
                    need(row.get("request_guid") == config.get("request_guid") and row.get("response_guid") == config.get("response_guid"),
                         "participant_writer_identities:" + side)

        counters = [r for r in side_rows if r.get("_kind") == "PERF_COUNTERS"]
        if need(len(counters) == 1, "one_final_counters:" + side, True):
            counter = counters[0]
            diagnostics[side] = {k: v for k, v in counter.items() if not k.startswith("_")}
            counter = dict(counter)
            if "measurement_record_overflow" in counter:
                counter["record_overflow"] = counter["measurement_record_overflow"]
            for field in ("record_overflow", "counter_overflow", "log_overflow_bytes", "lost_records", "lost_error_details"):
                value = counter.get(field)
                if field in ("record_overflow", "counter_overflow") and isinstance(value, bool):
                    value = int(value)
                need(natural(value) and value == 0, "measurement_overflow:" + side + ":" + field, True)
            for field in ("wrong_run", "wrong_source", "invalid", "out_of_range"):
                need(natural(counter.get(field)) and counter[field] == 0, "correctness_counter:" + side + ":" + field)
            if "inactive" in counter:
                need(counter["inactive"] == 0, "future_or_inactive_identity:" + side)
            if "invalid_clock" in counter:
                need(natural(counter["invalid_clock"]) and counter["invalid_clock"] == 0, "invalid_clock_counter:" + side)
            for field in ("first_failure", "cleanup_failure"):
                if field in counter:
                    need(counter[field] == "", "fixture_failure:" + side + ":" + field + ":" + str(counter[field]))
            need(natural(counter.get("storage_bytes")) and 0 < counter["storage_bytes"] <= 4194304, "measurement_storage_cap:" + side, True)
            need(counter.get("inbound_capacity") == 1048576 and counter.get("outbound_capacity") == 262176, "actual_payload_capacity:" + side, True)
            # Native diagnostic detail retention is not the business ledger. Its
            # overflow is retained verbatim, with no fabricated business loss.
        qos = [r for r in side_rows if r.get("_kind") == "PERF_QOS"]
        endpoints = ("request_writer", "response_reader") if side == "A" else ("request_reader", "response_writer")
        need(len(qos) == 2 and {q.get("endpoint") for q in qos} == set(endpoints), "exact_qos_endpoints:" + side)
        for q in qos:
            actual = q.get("effective", q)
            if "readback" in q:
                need(q["readback"] is True, "effective_qos_readback_failed:" + side)
            for field in QOS_FIELDS:
                if field == "publish_mode" and q.get("endpoint", "").endswith("reader"):
                    continue
                need(actual.get(field) == plan["qos"][field], "effective_qos:" + side + ":" + str(q.get("endpoint")) + ":" + field)
            need(q.get("representation") == "XCDR1", "effective_representation:" + side)

    vector_reserved = 0
    side_vector_reserved = {"A": 0, "B": 0}
    for case_id, case in plan_cases.items():
        phase_metrics = {}
        for phase in phases:
            count = expected[case_id, phase]
            a_summary = summaries.get(("A", case_id, phase), {})
            b_summary = summaries.get(("B", case_id, phase), {})
            for side in "AB":
                need((side, case_id, phase) in summaries, "missing_phase_summary:" + str((side, case_id, phase)), True)
            metrics = dict(planned=count, attempted=0, write_ok=0, write_failed=0,
                received=0, unique_reader_samples=0, missing_reader_samples=0,
                timed_out=0, late=0, after_observation=0, missed_slots=0,
                not_attempted=0, duplicates=0, invalid_samples=0,
                delivered_after_failed_write=0, echo_attempted=0, echo_failed=0,
                requested_hz=case.get("requested_hz", case.get("maximum_hz")), success_rtt_ns=[])
            stopped = False
            last_before, last_slot = None, None
            for sequence in range(1, count + 1):
                key_a, key_b = ("A", case_id, phase, sequence), ("B", case_id, phase, sequence)
                a, b = records.get(key_a, {}), records.get(key_b, {})
                for key, row in ((key_a, a), (key_b, b)):
                    need(bool(row), "missing_sample_record:" + str(key), True)
                    if not row:
                        invalid_keys.add(key)
                        continue
                    sizes_ok = all(row.get(field) == value for field, value in (
                        ("business_bytes", case["business_bytes"]), ("vector_bytes", case["vector_bytes"]),
                        ("serialized_bytes", case["serialized_xcdr1_bytes"])))
                    if not need(sizes_ok, "sample_payload_sizes:" + str(key)):
                        invalid_keys.add(key)
                    if flags(row, INVALID):
                        need(False, "invalid_payload:" + str(key))
                        invalid_keys.add(key)
                    if flags(row, INVALID_CLOCK):
                        need(False, "invalid_clock_record:" + str(key))
                        invalid_keys.add(key)
                    metrics["duplicates"] += row.get("duplicates", 0) if natural(row.get("duplicates")) else 0
                attempted = flags(a, ATTEMPTED)
                request_write_observed = any(a.get(field) is not None for field in ("write_before_ns", "write_after_ns", "write_rc"))
                if attempted or request_write_observed:
                    vector_reserved += case["vector_bytes"]
                    side_vector_reserved["A"] += case["vector_bytes"]
                if attempted:
                    metrics["attempted"] += 1
                    need(not flags(a, NOT_ATTEMPTED | MISSED_SLOT), "attempt_and_skip_conflict:" + str(key_a))
                    need(not stopped, "rtt_submission_after_timeout:" + str(key_a))
                    before, after = a.get("write_before_ns"), a.get("write_after_ns")
                    clock_ok = need(timestamp_ns(before) and timestamp_ns(after) and after >= before,
                                    "invalid_write_clock:" + str(key_a))
                    if not clock_ok:
                        invalid_keys.add(key_a)
                    need(isinstance(a.get("write_rc"), int) and not isinstance(a.get("write_rc"), bool), "write_rc_missing:" + str(key_a), True)
                    metrics["write_ok" if a.get("write_rc") == 0 else "write_failed"] += 1
                    if clock_ok:
                        need(last_before is None or before > last_before, "request_clock_order:" + str(key_a))
                        last_before = before
                        if case["kind"] == "RTT":
                            deadline = a_summary.get("phase_deadline_ns")
                            need(timestamp_ns(deadline) and before < deadline, "rtt_write_outside_phase_deadline:" + str(key_a))
                        if case["kind"] == "LOAD" and timestamp_ns(a_summary.get("window_start_ns")):
                            start, end = a_summary["window_start_ns"], a_summary.get("window_end_ns")
                            pacing_hz = ((count * 1_000_000_000 + config["contract_window_ns"] - 1) // config["contract_window_ns"]
                                         if contract_scope else metrics["requested_hz"])
                            period = 1_000_000_000 // pacing_hz
                            slot = (before - start) // period
                            need(timestamp_ns(end) and start <= before < end and slot == sequence - 1 and (last_slot is None or slot > last_slot), "load_absolute_slot_no_catchup:" + str(key_a))
                            last_slot = slot
                    writer_identity = a.get("writer_guid"), a.get("writer_sn")
                    if a.get("write_rc") == 0:
                        need(writer_identity[0] == config.get("request_guid") and positive_ns(writer_identity[1]), "request_writer_identity:" + str(key_a))
                        if not need(writer_identity not in seen_writer_sn["A"], "reused_request_writer_SN:" + str(key_a)):
                            invalid_keys.add(key_a)
                        seen_writer_sn["A"].add(writer_identity)
                elif a:
                    metrics["not_attempted"] += 1
                    need(flags(a, NOT_ATTEMPTED), "not_attempted_flag_missing:" + str(key_a), True)
                    need(all(a.get(f) is None for f in ("write_before_ns", "write_after_ns", "write_rc")), "unattempted_has_write:" + str(key_a))
                metrics["missed_slots"] += int(flags(a, MISSED_SLOT))
                timed_out = flags(a, TIMED_OUT)
                metrics["timed_out"] += int(timed_out)
                if timed_out and case["kind"] == "RTT":
                    stopped = True
                b_received = timestamp_ns(b.get("first_take_ns"))
                b_valid = b_received and flags(b, VALIDATED) and key_b not in invalid_keys
                if b_received:
                    need(flags(b, VALIDATED), "unvalidated_take:" + str(key_b))
                    source_ok = (b.get("source_guid") == config.get("request_guid") == a.get("writer_guid")
                                 and positive_ns(b.get("source_sn")) and b.get("source_sn") == a.get("writer_sn") and attempted)
                    if not need(source_ok, "request_source_identity:" + str(key_b)):
                        b_valid = False
                if b_valid and case["kind"] == "LOAD":
                    take, start, end = b["first_take_ns"], b_summary.get("window_start_ns"), b_summary.get("window_end_ns")
                    observation_deadline = b_summary.get("observation_deadline_ns")
                    after_observation = timestamp_ns(observation_deadline) and take >= observation_deadline
                    outside_fixed_window = timestamp_ns(end) and take >= end
                    if after_observation or flags(b, AFTER_OBSERVATION):
                        metrics["after_observation"] += 1
                        need(after_observation and flags(b, AFTER_OBSERVATION), "after_observation_deadline_flag:" + str(key_b))
                    if outside_fixed_window or after_observation or flags(b, LATE | AFTER_OBSERVATION):
                        metrics["late"] += 1
                        need(flags(b, LATE), "late_deadline_flag_missing:" + str(key_b))
                        need((timestamp_ns(end) and take >= end) or (end is None and b_summary.get("no_first_sample") is True), "late_inside_reader_window:" + str(key_b))
                    elif need(timestamp_ns(start) and timestamp_ns(end) and start <= take < end,
                              "reader_fixed_window_membership:" + str(key_b)):
                        metrics["unique_reader_samples"] += 1
                elif b_valid:
                    metrics["unique_reader_samples"] += 1
                if not b_valid:
                    metrics["missing_reader_samples"] += 1
                if b_received and not b_valid:
                    metrics["invalid_samples"] += 1
                echo_write_observed = any(b.get(field) is not None for field in ("echo_before_ns", "echo_after_ns", "echo_rc"))
                if flags(b, ECHO_RESERVED) or echo_write_observed:
                    vector_reserved += case["vector_bytes"]
                    side_vector_reserved["B"] += case["vector_bytes"]
                    need(flags(b, ECHO_RESERVED), "echo_write_without_reservation_flag:" + str(key_b))
                if flags(b, ECHO_RESERVED):
                    metrics["echo_attempted"] += 1
                    need(case["kind"] == "RTT" and b_valid, "echo_requires_valid_request:" + str(key_b))
                    echo_clock_ok = need(timestamp_ns(b.get("echo_before_ns")) and timestamp_ns(b.get("echo_after_ns"))
                         and timestamp_ns(b.get("first_take_ns")) and b["echo_after_ns"] >= b["echo_before_ns"] >= b["first_take_ns"], "echo_local_clock:" + str(key_b))
                    deadline = b_summary.get("phase_deadline_ns")
                    if not need(echo_clock_ok and timestamp_ns(deadline) and b["echo_before_ns"] < deadline,
                                "echo_outside_phase_deadline:" + str(key_b)):
                        b_valid = False
                    observation_deadline = b_summary.get("observation_deadline_ns")
                    if echo_clock_ok and timestamp_ns(observation_deadline) and b["echo_before_ns"] >= observation_deadline:
                        need(False, "echo_after_observation_deadline:" + str(key_b))
                        b_valid = False
                    if b.get("echo_rc") != 0:
                        metrics["echo_failed"] += 1
                if case["kind"] == "RTT":
                    received = timestamp_ns(a.get("first_take_ns"))
                    metrics["received"] += int(received)
                    metrics["late"] += int(flags(a, LATE))
                    metrics["after_observation"] += int(flags(a, AFTER_OBSERVATION))
                    valid_response = received and flags(a, VALIDATED) and key_a not in invalid_keys
                    if received:
                        source_ok = (a.get("source_guid") == config.get("response_guid") == b.get("echo_guid")
                                     and positive_ns(a.get("source_sn")) and a.get("source_sn") == b.get("echo_sn")
                                     and flags(b, ECHO_RESERVED) and b_valid)
                        if not need(source_ok, "echo_source_identity:" + str(key_a)):
                            valid_response = False
                        local_rtt = a["first_take_ns"] - a["write_before_ns"] if timestamp_ns(a.get("write_before_ns")) else None
                        if not need(positive_ns(local_rtt), "invalid_A_local_rtt_clock:" + str(key_a)):
                            valid_response = False
                        if positive_ns(local_rtt):
                            deadline = a["write_before_ns"] + case["request_timeout_s"] * 1_000_000_000
                            phase_deadline = a_summary.get("phase_deadline_ns")
                            if timestamp_ns(phase_deadline):
                                deadline = min(deadline, phase_deadline)
                            else:
                                need(False, "missing_rtt_phase_deadline:" + str(key_a), True)
                                valid_response = False
                            if a["first_take_ns"] >= deadline:
                                need(flags(a, TIMED_OUT) and flags(a, LATE | AFTER_OBSERVATION), "unmarked_response_after_timeout:" + str(key_a))
                                valid_response = False
                            observation_deadline = a_summary.get("observation_deadline_ns")
                            if timestamp_ns(observation_deadline) and a["first_take_ns"] >= observation_deadline:
                                need(flags(a, LATE) and flags(a, AFTER_OBSERVATION), "unmarked_response_after_observation:" + str(key_a))
                                valid_response = False
                        if flags(a, VALIDATED) and a.get("write_rc") != 0:
                            metrics["delivered_after_failed_write"] += 1
                    if attempted and a.get("write_rc") == 0 and not received:
                        need(timed_out, "missing_timeout_record:" + str(key_a), True)
                    if valid_response and a.get("write_rc") == 0 and not flags(a, TIMED_OUT | LATE | AFTER_OBSERVATION | INVALID | INVALID_CLOCK):
                        metrics["success_rtt_ns"].append(local_rtt)
            metrics["rtt"] = statistics_ns(metrics["success_rtt_ns"])
            for side, summary in (("A", a_summary), ("B", b_summary)):
                local = [records.get((side, case_id, phase, sequence), {}) for sequence in range(1, count + 1)]
                derived = {"attempted": sum(flags(r, ATTEMPTED) for r in local),
                    "write_ok": sum(r.get("write_rc") == 0 for r in local),
                    "valid_unique": sum(flags(r, VALIDATED) for r in local),
                    "timed_out": sum(flags(r, TIMED_OUT) for r in local),
                    "late": sum(flags(r, LATE) for r in local),
                    "after_observation": sum(flags(r, AFTER_OBSERVATION) for r in local),
                    "missed_slots": sum(flags(r, MISSED_SLOT) for r in local),
                    "not_attempted": sum(flags(r, NOT_ATTEMPTED) for r in local)}
                derived["invalid_clock_count"] = sum(flags(r, INVALID_CLOCK) for r in local)
                derived["missing"] = count - derived["valid_unique"]
                for field, value in derived.items():
                    if field in summary:
                        need(summary[field] == value, "summary_raw_count_mismatch:" + side + ":" + case_id + ":" + phase + ":" + field, True)
            metrics["attempt_hz"] = rate(metrics["attempted"], a_summary.get("window_start_ns"), a_summary.get("window_end_ns"))
            metrics["write_success_hz"] = rate(metrics["write_ok"], a_summary.get("window_start_ns"), a_summary.get("window_end_ns"))
            metrics["goodput_bytes_per_second"] = None
            if case["kind"] == "LOAD":
                start, end = b_summary.get("window_start_ns"), b_summary.get("window_end_ns")
                if metrics["unique_reader_samples"]:
                    duration = (2 * 1_000_000_000 if mode == "TRACE" else 10 * 1_000_000_000 if phase == "FORMAL"
                                else max(1_000_000_000, count * 1_000_000_000 // metrics["requested_hz"]))
                    if contract_scope:
                        duration = config.get("contract_window_ns")
                    need(timestamp_ns(start) and timestamp_ns(end) and end - start == duration, "fixed_reader_window_duration:" + case_id + ":" + phase)
                    metrics["goodput_bytes_per_second"] = rate(metrics["unique_reader_samples"] * case["business_bytes"], start, end)
                else:
                    need(start is None and end is None and b_summary.get("no_first_sample") is True, "no_data_reader_interval_unobserved:" + case_id + ":" + phase)
            metrics["reader_business_bytes"] = metrics["unique_reader_samples"] * case["business_bytes"]
            metrics["A_window"] = {k: a_summary.get(k) for k in ("window_start_ns", "window_end_ns")}
            metrics["B_window"] = {k: b_summary.get(k) for k in ("window_start_ns", "window_end_ns")}
            metrics["resource_intervals"] = {}
            for side, summary in (("A", a_summary), ("B", b_summary)):
                samples = {}
                for boundary in ("before", "after"):
                    found = resource_boundaries[side, summary.get("stage"), "stage_" + boundary]
                    need(len(found) <= 1, "duplicate_resource_boundary:" + side + ":" + case_id + ":" + phase + ":" + boundary, True)
                    samples[boundary] = found[0] if len(found) == 1 else {}
                # CPU excludes heavy /proc reads. The stage_after resource
                # snapshot is captured only after bilateral DONE/flush, and may
                # therefore be substantially later than the local CPU end.
                measured = resource_interval(
                    {**samples["before"], "wall_ns": summary.get("cpu_wall_start_ns"), "cpu_ns": summary.get("cpu_start_ns")},
                    {**samples["after"], "wall_ns": summary.get("cpu_wall_end_ns"), "cpu_ns": summary.get("cpu_end_ns")})
                measured.update(cpu_wall_start_ns=summary.get("cpu_wall_start_ns"), cpu_wall_end_ns=summary.get("cpu_wall_end_ns"),
                    resource_sample_wall_start_ns=samples["before"].get("wall_ns"),
                    resource_sample_wall_end_ns=samples["after"].get("wall_ns"),
                    resource_start_label="stage_before" if samples["before"] else None,
                    resource_end_label="stage_after" if samples["after"] else None,
                    resource_sample_scope="outside_active_cpu_interval; stage_after_captured_after_bilateral_DONE_flush; wall_ns_after_proc_reads; non_atomic")
                metrics["resource_intervals"][side] = measured
            metrics["load_status"] = ("LOAD_NOT_SUSTAINED" if case["kind"] == "LOAD" and (metrics["missed_slots"] or metrics["write_failed"] or metrics["not_attempted"]) else "OBSERVED")
            metrics["measurement_outcome"] = ("INCOMPLETE" if
                (case["kind"] == "RTT" and metrics["rtt"]["count"] != count)
                or (case["kind"] == "LOAD" and metrics["goodput_bytes_per_second"] is None)
                else metrics["load_status"])
            phase_metrics[phase] = metrics
        cases[case_id] = dict(phase_metrics.get("FORMAL", {}), kind=case["kind"], business_bytes=case["business_bytes"], phases=phase_metrics)
    nominal = budget["trace_vector_bytes" if mode == "TRACE" else "measure_vector_bytes"]
    for side in "AB":
        counter = diagnostics.get(side, {})
        if "submitted_vector_bytes" in counter:
            need(counter["submitted_vector_bytes"] == side_vector_reserved[side], "raw_submission_bytes_counter_mismatch:" + side, True)
    need(vector_reserved <= nominal and vector_reserved <= plan["budgets"]["per_window_cap"], "submitted_vector_budget_exceeded", True)
    core_pass = not errors
    native = native_witness(config, rows, records, expected, plan) if native_scope else {
        "pass": False, "status": "NOT_NATIVE_EVIDENCE", "errors": [], "joins": []}
    native_pass = core_pass and native_scope and native["pass"]
    passed = core_pass and (native_pass if require_native or native_scope else True) and (not identity_errors if native_scope else True)
    resources = {side: [r for r in rows.get(side, []) if r.get("_kind") == "PERF_RESOURCE"] for side in "AB"}
    instrumentation = {side: {"observation_snapshots": [r for r in rows.get(side, []) if r.get("_kind") == "PERF_OBSERVATION"],
        "adapter_snapshots": [r for r in rows.get(side, []) if r.get("_kind") == "PERF_ADAPTER"],
        "observation_storage": [r for r in rows.get(side, []) if r.get("_kind") == "PERF_OBSERVATION_STORAGE"],
        "rx_admission_snapshots": [r for r in rows.get(side, []) if r.get("_kind") == "EXPERIMENT_RX_ADMISSION_SUMMARY"],
        "rx_admission_reason_counts": [r for r in rows.get(side, []) if r.get("_kind") == "EXPERIMENT_RX_ADMISSION_REASON_COUNT"],
        "native_final_summaries": [r for r in rows.get(side, []) if r.get("_kind") == "EXPERIMENT_UNQUALIFIED_SUMMARY"],
        "scope": "raw_nonatomic_diagnostics; byte_counters_are_not_application_goodput; prefix_overflow_is_not_business_loss"} for side in "AB"}
    for side, evidence in instrumentation.items():
        observation = evidence["observation_snapshots"][-1] if evidence["observation_snapshots"] else {}
        adapter = evidence["adapter_snapshots"][-1] if evidence["adapter_snapshots"] else {}
        evidence["diagnostic_totals"] = {
            "scope": "last_cumulative_or_boundary_snapshot_not_sum_of_snapshots",
            "sdk_send_failures": observation.get("send_failures"),
            "nonzero_event_values": observation.get("nonzero_values"),
            "nonzero_event_values_are_not_sdk_failure_count": True,
            "native_error_detail_overflow": observation.get("error_details_overflow"),
            "tx_rejected": adapter.get("tx_rejected"),
            "rx_admission_prefix_overflow": adapter.get("rx_admission_overflow"),
            "prefix_overflow_is_not_business_loss": True,
        }
    return {"schema": 1, "run": run, "mode": mode, "repeat": repeat, "scope": config.get("scope"),
        "pass": passed, "core_pass": core_pass, "complete_evidence": not completeness_errors,
        "status": "FAIL" if not core_pass else "UNVERIFIED_MEASUREMENT_IDENTITY" if native_scope and identity_errors else "NATIVE_WITNESS_OBSERVED_EXTERNAL_GATES_REQUIRED" if native_pass else "UNVERIFIED_NATIVE_PROOF" if require_native else "HOST_CONTRACT_PASS" if contract_scope else "HOST_REFERENCE_CORE_PASS",
        "errors": errors, "completeness_errors": completeness_errors, "cases": cases, "diagnostics": diagnostics,
        "resources": resources, "instrumentation": instrumentation, "submitted_vector_reservations_bytes": vector_reserved,
        "submitted_vector_reservations_by_side": side_vector_reserved,
        "planned_vector_bytes": nominal, "budget": budget, "native": native,
        "native_proof_pass": native_pass, "device_tested": False, "qualification": "NOT_QUALIFIED",
        "queue_hwm_scope": "sampled_boundary_maximum_lower_bound; exact_HWM_UNOBSERVED",
        "formal_native_detail_scope": "aggregate_only_no_full_chain_claim" if mode == "MEASURE" else "bounded_TRACE",
        "sdk_internal_settlement": "UNOBSERVED", "external_source_package_guards_cleanup": "NOT_ASSESSED_BY_THIS_ANALYZER",
        "processes": processes, "latency_scope": "A_local_successful_request_to_valid_unique_echo_RTT_only",
        "measurement_identity": identity, "measurement_identity_complete": not identity_errors,
        "measurement_identity_errors": identity_errors,
        "measurement_identity_status": "PRE_RUN_DECLARATIONS_CONSISTENT_EXTERNAL_BINDING_REQUIRED" if not identity_errors else "UNOBSERVED_OR_INCONSISTENT",
        "plan_sha256": identity["measurement_plan_sha256"],
        **{key: identity[key] for key in ("log_schema_sha256", "build_manifest_sha256", "source_state_sha256", "executable_sha256", "configuration_sha256", "package_artifacts")}}


def native_witness(config, rows, records, expected, plan):
    """Join observed frames using GUID/SN, whole-message hash/length and lease.

    This proves bounded same-run observations, not exclusive packet causality or
    SDK internal settlement. Retransmitted ranges may supply coverage but are
    never counted as additional delivered application samples.
    """
    errors, joins, identities = [], [], {}
    data_index, native_index, app_index = defaultdict(list), defaultdict(list), defaultdict(list)

    def integer(value):
        try:
            if isinstance(value, bool):
                return None
            return int(value)
        except (TypeError, ValueError, OverflowError):
            return None

    def string(value):
        return str(value) if value is not None else ""

    def need(condition, reason):
        if not condition:
            errors.append(reason)
        return bool(condition)

    def sn(row):
        high, low = integer(row.get("sequence_hi")), integer(row.get("sequence_lo"))
        if high is None or low is None or not 0 <= high < (1 << 31) or not 0 <= low < (1 << 32):
            return None
        return (high << 32) | low

    def signature(row):
        fields = ("_kind", "writer_guid", "sequence_hi", "sequence_lo", "native_hash", "native_len")
        if row.get("_kind") == "EXPERIMENT_DATA_FRAG":
            fields += ("fragment_start", "fragment_count", "fragment_end", "fragment_size", "sample_size", "payload_bounds_known")
        return tuple(string(row.get(field)) for field in fields)

    for side in "AB":
        found = [r for r in rows.get(side, []) if r.get("_kind") == "PERF_IDENTITY"]
        if need(len(found) == 1, "native_identity_required:" + side):
            ident = found[0]
            identities[side] = ident
            for field in ("runtime", "native_instance", "application", "receiver", "token", "local_kind", "local_port", "peer_kind", "peer_port"):
                need(positive_ns(integer(ident.get(field))), "native_identity_field:" + side + ":" + field)
            for field in ("local_key", "peer_key"):
                need(guid(ident.get(field)), "native_route_key:" + side + ":" + field)
        for row in rows.get(side, []):
            kind = row.get("_kind", "")
            if kind.startswith("EXPERIMENT_") and "run" in row:
                need(row["run"] == config["run"], "native_wrong_run:" + side)
            if kind in ("EXPERIMENT_DATA", "EXPERIMENT_DATA_FRAG") and row.get("run") == config["run"]:
                data_index[side, row.get("event"), row.get("writer_guid"), sn(row)].append(row)
            elif kind == "EXPERIMENT_UNQUALIFIED" and row.get("run") == config["run"]:
                native_index[side, row.get("event"), string(row.get("instance_id")), string(row.get("invocation"))].append(row)
            elif kind in ("EXPERIMENT_APPLICATION_TX", "EXPERIMENT_APPLICATION_RX"):
                app_index[side, kind, string(row.get("instance_id")), string(row.get("invocation"))].append(row)
        observations = [r for r in rows.get(side, []) if r.get("_kind") == "PERF_OBSERVATION"]
        if config["mode"] == "MEASURE":
            need(any(integer(r.get("suppressed")) is not None and integer(r.get("suppressed")) > 0 for r in observations), "measure_formal_suppression_observed:" + side)
    if len(identities) != 2:
        return {"pass": False, "status": "UNVERIFIED_NATIVE_PROOF", "errors": errors, "joins": []}

    def original_native(row, ident):
        return (integer(row.get("rc")) == 0
                and string(row.get("instance_id")) == string(ident["native_instance"])
                and all(string(row.get(key)) == string(ident.get(key)) for key in (
                    "local_kind", "local_key", "local_port", "peer_kind", "peer_key", "peer_port")))

    def packet_join(tx_data, source, target):
        source_id, target_id = identities[source], identities[target]
        length, digest = integer(tx_data.get("native_len")), tx_data.get("native_hash")
        if length is None or not 0 < length <= plan["budgets"]["existing_native_message_max"] or not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{16}", digest):
            return None
        writer, sequence = tx_data.get("writer_guid"), sn(tx_data)
        tx_candidates = native_index[source, "SendBytes_return", string(tx_data.get("instance_id")), string(tx_data.get("invocation"))]
        sends = [r for r in tx_candidates if original_native(r, source_id) and integer(r.get("len")) == length and r.get("hash") == digest]
        if len(sends) != 1:
            return None
        send = sends[0]
        uppers = app_index[source, "EXPERIMENT_APPLICATION_TX", string(source_id["native_instance"]), string(send.get("invocation"))]
        if not any(r.get("stage") == "TX_ACCEPTED" and all(string(r.get(k)) == string(source_id.get(v)) for k, v in (
                ("runtime_id", "runtime"), ("application_incarnation", "application"), ("logical_token", "token"))) for r in uppers):
            return None
        sig = signature(tx_data)
        for enter_data in data_index[target, "FastDDS_receiver_enter", writer, sequence]:
            if signature(enter_data) != sig:
                continue
            instance, invocation = string(enter_data.get("instance_id")), string(enter_data.get("invocation"))
            if instance != string(target_id["native_instance"]):
                continue
            chain = {event: native_index[target, event, instance, invocation] for event in (
                "OnBytes_owned_copy", "FastDDS_receiver_enter", "FastDDS_receiver_return")}
            if any(len(chain[event]) != 1 for event in chain):
                continue
            owned, enter, returned = (chain[event][0] for event in chain)
            if not all(original_native(r, target_id) and integer(r.get("len")) == length and r.get("hash") == digest
                       and string(r.get("receiver_lease_id")) == string(target_id["receiver"]) for r in (owned, enter, returned)):
                continue
            if not all(all(string(r.get(k)) == string(enter.get(k)) for k in ("socket", "owner")) for r in (owned, returned)):
                continue
            if not (integer(owned.get("_line")) is not None and integer(enter.get("_line")) is not None
                    and integer(returned.get("_line")) is not None and owned["_line"] < enter["_line"] < returned["_line"]):
                continue
            if not all(string(send.get("local_" + field)) == string(enter.get("peer_" + field))
                       and string(send.get("peer_" + field)) == string(enter.get("local_" + field)) for field in ("kind", "key", "port")):
                continue
            if not all(any(signature(r) == sig and string(r.get("instance_id")) == instance and string(r.get("invocation")) == invocation
                           for r in data_index[target, event, writer, sequence]) for event in chain):
                continue
            app = app_index[target, "EXPERIMENT_APPLICATION_RX", instance, invocation]
            if not all(any(integer(r.get("stage")) == stage and integer(r.get("bytes")) == length
                           and all(string(r.get(k)) == string(target_id.get(v)) for k, v in (
                               ("runtime_id", "runtime"), ("application_incarnation", "application"),
                               ("receiver_incarnation", "receiver"), ("logical_token", "token"))) for r in app) for stage in (0, 1, 3, 4, 5)):
                continue
            return {"native_len": length, "native_hash": digest, "tx_invocation": send["invocation"],
                    "rx_invocation": invocation, "rx_instance": instance, "receiver_lease": target_id["receiver"]}
        return None

    for case in plan["cases"]:
        case_id = case["id"]
        phase = "FORMAL" if config["mode"] == "TRACE" else "WARMUP"
        count = expected.get((case_id, phase), 0) if config["mode"] == "TRACE" else 1
        for sequence in range(1, count + 1):
            a = records.get(("A", case_id, phase, sequence), {})
            b = records.get(("B", case_id, phase, sequence), {})
            paths = [("A", "B", a.get("writer_guid"), a.get("writer_sn"))]
            if case["kind"] == "RTT":
                paths.append(("B", "A", b.get("echo_guid"), b.get("echo_sn")))
            for source, target, writer, sample_sn in paths:
                label = f"{case_id}:{phase}:{sequence}:{source}->{target}"
                packets, ranges = [], defaultdict(list)
                has_data = False
                for tx_data in data_index[source, "SendBytes_return", writer, sample_sn]:
                    packet = packet_join(tx_data, source, target)
                    if packet is None:
                        continue
                    if tx_data["_kind"] == "EXPERIMENT_DATA":
                        # A single DATA body cannot prove the large fragmented
                        # sample; its serialized content exceeds the SDK cap.
                        if case["serialized_xcdr1_bytes"] <= packet["native_len"]:
                            has_data = True
                            packets.append(packet)
                    else:
                        start, end, n, width, size = (integer(tx_data.get(k)) for k in (
                            "fragment_start", "fragment_end", "fragment_count", "fragment_size", "sample_size"))
                        if (None in (start, end, n, width, size) or width <= 0 or n <= 0 or start < 1
                                or end != start + n - 1 or size != case["serialized_xcdr1_bytes"]
                                or end > (size + width - 1) // width or integer(tx_data.get("payload_bounds_known")) != 1):
                            continue
                        ranges[width].append((start, end))
                        packets.append(dict(packet, fragment_start=start, fragment_end=end, fragment_size=width))
                complete_fragment_size = None
                for width, intervals in ranges.items():
                    reach = 0
                    for start, end in sorted(intervals):
                        if start > reach + 1:
                            break
                        reach = max(reach, end)
                    if reach == (case["serialized_xcdr1_bytes"] + width - 1) // width:
                        complete_fragment_size = width
                        break
                if need(has_data or complete_fragment_size is not None, "native_sample_chain_or_fragment_coverage:" + label):
                    joins.append({"case": case_id, "phase": phase, "sequence": sequence, "source": source, "target": target,
                        "writer_guid": writer, "writer_sn": sample_sn, "serialized_bytes": case["serialized_xcdr1_bytes"],
                        "complete_fragment_size": complete_fragment_size, "packets": packets})
    return {"pass": not errors, "status": "BOUNDED_NATIVE_WITNESS_OBSERVED" if not errors else "UNVERIFIED_NATIVE_PROOF",
            "errors": errors, "joins": joins, "selection": "TRACE_ALL" if config["mode"] == "TRACE" else "SAME_RUN_WARMUP_SEQUENCE_1_PER_CASE",
            "formal_detail_coverage": "NOT_CLAIMED" if config["mode"] == "MEASURE" else "BOUNDED_TRACE",
            "association_scope": "GUID_SN_original_lease_route_and_whole_message_hash_length; not_exclusive_SDK_cause",
            "sdk_internal_settlement": "UNOBSERVED", "external_gates": "REQUIRED"}


def summarize_reports(reports):
    if (len(reports) != 3 or {r.get("repeat") for r in reports} != {1, 2, 3}
            or any(r.get("mode") != "MEASURE" for r in reports)
            or len({r.get("run") for r in reports}) != 3
            or len({r.get("scope") for r in reports}) != 1):
        raise ValueError("summary requires three distinct same-scope MEASURE runs, repeats 1/2/3")
    reports = sorted(reports, key=lambda r: r["repeat"])
    identities = [report_measurement_identity(report) for report in reports]
    if any(identity != identities[0] for identity in identities[1:]):
        raise ValueError("repeat_measurement_contract_or_selected_build_identity_differs")
    case_sets = [set(r["cases"]) for r in reports]
    if any(s != case_sets[0] for s in case_sets):
        raise ValueError("repeat case sets differ")
    cases = {}
    count_fields = ("planned", "attempted", "write_ok", "write_failed", "received", "unique_reader_samples",
                    "missing_reader_samples", "timed_out", "late", "after_observation", "missed_slots",
                    "not_attempted", "duplicates", "invalid_samples", "delivered_after_failed_write")
    for case in reports[0]["cases"]:
        repeats = []
        pooled = []
        for report in reports:
            own = report["cases"][case]
            pooled.extend(own["success_rtt_ns"])
            repeats.append({"run": report["run"], "repeat": report["repeat"], "pass": report["pass"],
                "rtt": statistics_ns(own["success_rtt_ns"]),
                "counts": {f: own[f] for f in count_fields},
                "attempt_hz": own["attempt_hz"], "write_success_hz": own["write_success_hz"],
                "goodput_bytes_per_second": own["goodput_bytes_per_second"],
                "errors": report["errors"]})
        p99 = [r["rtt"]["p99_ns"] for r in repeats if r["rtt"]["p99_ns"] is not None]
        goodputs = [r["goodput_bytes_per_second"] for r in repeats if r["goodput_bytes_per_second"] is not None]
        cases[case] = {"pooled_rtt": statistics_ns(pooled), "sample_weighting": "raw_success_count",
                       "repeat_p99_range_ns": [min(p99), max(p99)] if p99 else None,
                       "repeat_goodput_range_bytes_per_second": [min(goodputs), max(goodputs)] if goodputs else None,
                       "repeats": repeats}
    return {"schema": 1, "kind": "REPEAT_SUMMARY", "scope": reports[0]["scope"],
            "pass": all(r["pass"] for r in reports), "status": "REPEATS_REPORTED",
            "cases": cases, "runs": [r["run"] for r in reports], "device_tested": False,
            "qualification": "NOT_QUALIFIED", "errors": [e for r in reports for e in r["errors"]],
            "pooling": "raw_success_samples_only; percentiles_never_averaged; failures_retained_per_repeat",
            "measurement_identity": identities[0], "measurement_identity_complete": True,
            "measurement_identity_errors": [],
            "external_source_package_guards_cleanup": "NOT_ASSESSED_BY_THIS_ANALYZER"}


def analyze_window(window, plan_path, require_native=True):
    window = Path(window)
    plan_bytes = Path(plan_path).read_bytes()
    plan = json.loads(plan_bytes)
    validate_plan(plan)
    config = json.loads((window / "window.json").read_text(encoding="utf-8-sig"))
    # The fixture's four entity GUIDs are deterministic before execution. Derive
    # omitted manifest values from its run nonce, never from observed samples.
    if isinstance(config.get("run"), str) and re.fullmatch(r"[0-9a-f]{16}", config["run"]):
        for field, side, entity in (("request_guid", "A", 1), ("response_guid", "B", 3)):
            prefix = bytearray.fromhex(config["run"])
            prefix[0] ^= 0xa1 if side == "A" else 0xb2
            prefix.extend((0x44, 0x55, 0x41, 1))
            config.setdefault(field, (prefix + entity.to_bytes(3, "big") + bytes([3])).hex())
    rows, hashes = {}, {}
    for side in "AB":
        path = window / f"{side}.raw.stdout.log"
        raw = path.read_bytes()
        if len(raw) > plan["observation"]["stdout_budget_per_process_bytes"]:
            raise ValueError(f"retained_stdout_exceeds_frozen_cap:{side}")
        hashes[path.name] = hashlib.sha256(raw).hexdigest()
        rows[side] = parse_text(raw.decode("utf-8"), side)
    result_path = window / "result.json"
    execution = json.loads(result_path.read_text(encoding="utf-8-sig")) if result_path.exists() else {}
    processes = execution.get("processes")
    for side in "AB":
        if side in execution.get("log_overflow_bytes", {}):
            for row in rows[side]:
                if row.get("_kind") == "PERF_COUNTERS":
                    row["log_overflow_bytes"] = execution["log_overflow_bytes"][side]
    input_plan_sha256 = hashlib.sha256(plan_bytes).hexdigest()
    report = analyze_records(config, rows, plan, require_native=require_native, processes=processes, plan_sha256=input_plan_sha256)
    hashes["window.json"] = hashlib.sha256((window / "window.json").read_bytes()).hexdigest()
    report.update(source_sha256=hashes, input_plan_sha256=input_plan_sha256,
                  reviewed_plan_sha256=PLAN_SHA256, plan_canonical_sha256=PLAN_CANONICAL_SHA256,
                  source_window=str(window.resolve()))
    outer_path = window / "window-summary.json"
    outer = json.loads(outer_path.read_text(encoding="utf-8-sig")) if outer_path.exists() else {}
    controller = execution.get("phase_controller")
    external_failed = (execution.get("pass") is False
        or isinstance(controller, dict) and controller.get("pass") is False
        or bool(outer.get("first_failure")) or bool(outer.get("cleanup_failures")))
    report["analysis_pass"] = report["pass"]
    report["analysis_status"] = report["status"]
    if external_failed:
        report["pass"] = False
        report["status"] = "OFFLINE_ANALYSIS_PASS_EXTERNAL_EXECUTION_FAIL" if report["analysis_pass"] else "OFFLINE_ANALYSIS_FAIL_EXTERNAL_EXECUTION_FAIL"
    if result_path.exists():
        hashes["result.json"] = hashlib.sha256(result_path.read_bytes()).hexdigest()
    if outer_path.exists():
        hashes["window-summary.json"] = hashlib.sha256(outer_path.read_bytes()).hexdigest()
    report["external_execution_record"] = {"present": bool(execution), "run": execution.get("run"),
        "pass": execution.get("pass"), "status": "FAIL" if external_failed else "REPORTED_PASS_EXTERNAL_GUARDS_NOT_REASSESSED" if execution.get("pass") is True else "UNOBSERVED",
        "scope": execution.get("scope"), "watchdog": execution.get("watchdog"),
        "failure": execution.get("failure"), "processes": processes, "phase_controller": controller,
        "window_summary_present": bool(outer), "outer_first_failure": outer.get("first_failure"),
        "cleanup_failures": outer.get("cleanup_failures")}
    return report


def write_report(report, output):
    output = Path(output)
    csv_path = output.with_suffix(".csv")
    if output == csv_path or output.exists() or csv_path.exists():
        raise FileExistsError("output JSON and derived CSV must both be new files")
    output.parent.mkdir(parents=True, exist_ok=True)
    fields = ["run", "repeat", "case", "phase", "kind", "planned", "attempted", "write_ok", "write_failed",
              "unique_reader_samples", "missing_reader_samples", "timed_out", "late", "after_observation",
              "missed_slots", "not_attempted", "duplicates", "requested_hz", "attempt_hz", "write_success_hz",
              "goodput_bytes_per_second", "count", "min_ns", "mean_ns", "p50_ns", "p95_ns", "p99_ns", "max_ns"]
    csv_rows = []
    for case_id, case in report.get("cases", {}).items():
        if report.get("kind") == "REPEAT_SUMMARY":
            for repeat in case["repeats"]:
                csv_rows.append(dict(repeat["counts"], **repeat["rtt"], run=repeat["run"], repeat=repeat["repeat"],
                    case=case_id, phase="FORMAL", goodput_bytes_per_second=repeat["goodput_bytes_per_second"],
                    attempt_hz=repeat["attempt_hz"], write_success_hz=repeat["write_success_hz"]))
        else:
            for phase, metrics in case["phases"].items():
                csv_rows.append(dict(metrics, **metrics["rtt"], run=report["run"], repeat=report["repeat"],
                                     case=case_id, phase=phase, kind=case["kind"]))
    # Exclusive creation prevents silently replacing a previous evidence report.
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    with csv_path.open("x", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in csv_rows:
            writer.writerow({key: "NA" if value is None else value for key, value in row.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("window", nargs="?", type=Path)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-host-reference", action="store_true")
    parser.add_argument("--summarize", type=Path, nargs=3, metavar="REPORT")
    args = parser.parse_args()
    try:
        if args.summarize:
            if args.window or args.plan:
                parser.error("--summarize is separate from window/--plan")
            report = summarize_reports([json.loads(p.read_text(encoding="utf-8-sig")) for p in args.summarize])
        else:
            if args.window is None or args.plan is None:
                parser.error("window and --plan are required")
            report = analyze_window(args.window, args.plan, require_native=not args.allow_host_reference)
    except (OSError, ValueError, KeyError, TypeError) as error:
        report = {"schema": 1, "pass": False, "core_pass": False, "native_proof_pass": False,
                  "complete_evidence": False, "device_tested": False, "status": "FAIL", "errors": [str(error)]}
    try:
        write_report(report, args.output)
    except (OSError, ValueError) as error:
        print(json.dumps({"pass": False, "status": "OUTPUT_REFUSED", "errors": [str(error)]}))
        return 2
    print(json.dumps({"pass": report["pass"], "status": report["status"], "errors": report.get("errors", [])[:12]}))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
