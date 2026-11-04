"""Fail-closed Host receive journal validation. No payload/RTPS causal claims."""
from collections import defaultdict


KINDS = {"CHARGE", "CAPACITY_REFUSAL", "QUEUE", "TAKE", "ENTER", "REQUEUE",
         "REFUND_RECORD", "REFUND_CAPTURE", "STAGE", "DECISION"}
PHASES = ("copying", "queued", "taken", "delivering")


def _fields(line):
    result = {}
    for token in line.split()[1:]:
        if "=" not in token:
            raise ValueError("malformed receive journal token")
        key, value = token.split("=", 1)
        if key in result:
            raise ValueError("duplicate receive journal field")
        result[key] = value
    return result


def validate_receive_capture(text, run, side, pid, *, profile="full_host", endpoints=None, expected_ingress=None):
    """Validate one postjoin capture, including all refunds and gate snapshots.

    full_host (default) requires independent callback/counter receipts and validated
    endpoint context. budget_only explicitly makes no full Host coverage claim.
    Callers separately require normal process/runtime close and exact binaries.
    Lifecycle callback arrival order is deliberately not used as budget order.
    """
    if profile not in ("full_host", "budget_only"):
        raise ValueError("unknown receive validation profile")
    if side not in ("A", "B"):
        raise ValueError("receive side must be A or B")
    headers, rows = [], []
    full_host_coverage = None
    for line in text.splitlines():
        if line.startswith("HOST_RX_CAPTURE "):
            headers.append(_fields(line))
        elif line.startswith("HOST_RX_EVENT "):
            rows.append(_fields(line))
        elif "FDDST_HOST_RX_CAPTURE_INCOMPLETE" in line:
            raise ValueError("receive journal exporter reported incomplete")
    if len(headers) != 1:
        raise ValueError("exactly one receive capture required")
    header = headers[0]
    for row in [header] + rows:
        if (row.get("schema"), row.get("run"), row.get("side"), row.get("pid")) != ("1", run, side, str(pid)):
            raise ValueError("receive journal identity mismatch")
    try:
        reserved, published, capacity = (int(header[x]) for x in ("reserved", "published", "capacity"))
        if header.get("complete") != "1" or not 0 < reserved == published == len(rows) <= capacity:
            raise ValueError("receive journal empty, overflowed or incomplete")
        if header.get("ordering") != "BUDGET_GATE_PER_SCOPE" or header.get("payload") != "NOT_CAPTURED":
            raise ValueError("unknown receive journal contract")
        numeric_rows = []
        for index, raw in enumerate(rows):
            if int(raw["index"]) != index or raw.get("kind") not in KINDS:
                raise ValueError("receive journal index/kind invalid")
            row = {key: (value if key in {"run", "side", "kind"} else int(value)) for key, value in raw.items()}
            numeric_rows.append(row)
        by_scope = defaultdict(list)
        for row in numeric_rows:
            if row["kind"] not in {"STAGE", "DECISION"}:
                if row["scope"] <= 0 or row["gate"] <= 0:
                    raise ValueError("unknown gate identity")
                by_scope[row["scope"]].append(row)
        if not by_scope and profile == "budget_only":
            raise ValueError("no authoritative budget events")
        charged = refunded = refusals = 0
        history, gate_history = {}, {}
        for scope, events in by_scope.items():
            active, seen = {}, set()
            # Every append happened under this original gate. Sorting is allowed
            # for deferred export; missing or duplicate gate numbers are not.
            for sequence, event in enumerate(sorted(events, key=lambda e: e["gate"]), 1):
                if event["gate"] != sequence:
                    raise ValueError("missing or duplicate budget gate event")
                gate_history[(scope, sequence)] = event
                kind, record = event["kind"], event["record"]
                if kind == "CHARGE":
                    if record <= 0 or record in seen or event["phase"] != 0 or event["size"] <= 0:
                        raise ValueError("charge identity reused or invalid")
                    seen.add(record)
                    active[record] = (0, event["size"], event["local"])
                    history[(scope, record)] = event
                    charged += 1
                elif kind == "CAPACITY_REFUSAL":
                    if record != 0:
                        raise ValueError("precharge refusal has record identity")
                    if event["application_copying_known"] != 1:
                        raise ValueError("capacity predicate snapshot incomplete")
                    predicates = (event["records"] == event["records_limit"],
                                  event["size"] > event["bytes_limit"] - event["bytes"],
                                  event["local_records"] == event["local_records_limit"],
                                  event["size"] > event["local_bytes_limit"] - event["local_bytes"],
                                  event["copying"] == event["copying_limit"],
                                  event["application_copying"] == event["application_copying_limit"])
                    mask = sum(1 << index for index, value in enumerate(predicates) if value)
                    if not mask or event["gate_capacity_mask"] != mask:
                        raise ValueError("refusal predicate mask does not match gate occupancy")
                    reason = 25 + next(index for index, value in enumerate(predicates) if value)
                    if event["gate_capacity_reason"] != reason:
                        raise ValueError("refusal first predicate reason mismatch")
                    refusals += 1
                else:
                    if record not in active:
                        raise ValueError("transition/refund without live original charge")
                    old, size, local = active[record]
                    if (size, local) != (event["size"], event["local"]):
                        raise ValueError("original charge identity changed")
                    if kind in {"REFUND_RECORD", "REFUND_CAPTURE"}:
                        if old != event["phase"] or (kind == "REFUND_CAPTURE" and old != 0):
                            raise ValueError("refund phase mismatch")
                        del active[record]
                        refunded += 1
                    else:
                        expected = {"QUEUE": (0, 1), "TAKE": (1, 2), "ENTER": (2, 3), "REQUEUE": (2, 1)}[kind]
                        if (old, event["phase"]) != expected:
                            raise ValueError("invalid authoritative phase transition")
                        active[record] = (expected[1], size, local)
                counts = [0] * 4
                byte_count = local_bytes = local_records = 0
                for phase, size, local in active.values():
                    counts[phase] += 1
                    byte_count += size
                    if local == event["local"]:
                        local_records += 1
                        local_bytes += size
                expected = dict(zip(PHASES, counts))
                expected.update(records=len(active), bytes=byte_count,
                                local_records=local_records, local_bytes=local_bytes)
                if any(event[key] != value for key, value in expected.items()):
                    raise ValueError("budget occupancy does not reconcile")
                if any(event[used] > event[limit] for used, limit in (
                        ("records", "records_limit"), ("bytes", "bytes_limit"),
                        ("local_records", "local_records_limit"), ("local_bytes", "local_bytes_limit"),
                        ("copying", "copying_limit"))):
                    raise ValueError("budget limit exceeded in journal")
            if active:
                raise ValueError("original budget records remain unrefunded at close")
        decisions = stages = 0
        for row in numeric_rows:
            if row["kind"] == "STAGE":
                stages += 1
                original = history.get((row["scope"], row["record"]))
                if not original or not 0 <= row["stage"] <= 6:
                    raise ValueError("lifecycle stage lacks immutable original record")
                for key in ("size", "correlation", "application", "receiver", "logical_token"):
                    if row[key] != original[key]:
                        raise ValueError("lifecycle record identity mismatch")
                if profile == "budget_only" and row["ingress"] and original["ingress"] and row["ingress"] != original["ingress"]:
                    raise ValueError("lifecycle callback joined unrelated ingress")
            elif row["kind"] == "DECISION":
                decisions += 1
                if row["layer"] not in (0, 1) or not 0 < row["branch"] < 15:
                    raise ValueError("unknown decision branch/layer")
                if row["scope"]:
                    original = gate_history.get((row["scope"], row["gate"]))
                    if not original or original["kind"] not in {"CHARGE", "CAPACITY_REFUSAL"}:
                        raise ValueError("decision lacks original admission gate")
                    if (row["record"], row["size"], row["correlation"], row["ingress"]) != (
                            original["record"], original["size"], original["correlation"], original["ingress"]):
                        raise ValueError("decision admission identity mismatch")
                    if row["capacity_known"]:
                        if original["kind"] != "CAPACITY_REFUSAL" or not row["capacity_mask"]:
                            raise ValueError("capacity decision gate mismatch")
                        if (row["capacity_mask"], row["reason"]) != (original["gate_capacity_mask"], original["gate_capacity_reason"]):
                            raise ValueError("capacity decision predicate differs from original gate")
                        for key in ("records", "bytes", "local_records", "local_bytes", "copying"):
                            if row["capacity_" + key] != original[key]:
                                raise ValueError("capacity decision differs from original gate snapshot")
                elif row["record"] or row["gate"]:
                    raise ValueError("partial unknown admission identity")
        if profile == "full_host":
            full_host_coverage = ValidateFullHost(text, run, side, pid, numeric_rows, history, gate_history, endpoints, expected_ingress)
    except (KeyError, TypeError, OverflowError) as exc:
        raise ValueError("missing or malformed receive observation") from exc
    return {"complete": True, "reconciled": True, "profile": profile, "full_host_complete": profile == "full_host", "coverage": full_host_coverage, "events": len(rows), "scopes": len(by_scope),
            "charged": charged, "refunded": refunded, "refusals": refusals,
            "decisions": decisions, "stages": stages, "payload_causality": "NOT_OBSERVED"}


# Full Host qualification is intentionally restricted to one exclusive Binding
# decision per forwarded callback. Shared fanout or unobservable early native
# drops fail closed, rather than silently reducing the coverage denominator.
def ValidateFullHost(text, run, side, pid, rows, history, gates, endpoints, expected_ingress):
    def records(prefix):
        found = [_fields(line) for line in text.splitlines() if line.startswith(prefix + " ")]
        for row in found:
            if tuple(row.get(k) for k in ("schema", "run", "side", "pid")) != ("1", run, side, str(pid)):
                raise ValueError("independent callback identity")
        return found
    footers, coverage = records("HOST_CALLBACK_CAPTURE"), records("HOST_RX_COVERAGE")
    receipts = records("HOST_CALLBACK_EVENT")
    if len(footers) != 1 or len(coverage) != 1:
        raise ValueError("full Host requires independent callback and final Binding coverage")
    footer, cov = footers[0], coverage[0]
    count = int(footer["ingress"])
    if (footer["complete"] != "1" or int(footer["capacity"]) != 8192
            or not 0 < count == int(footer["reserved"]) == len(receipts) <= 8192
            or int(cov["ingress"]) != count or cov["profile"] != "EXCLUSIVE_BINDING"
            or int(cov["active"]) != 0 or (expected_ingress is not None and count != expected_ingress)):
        raise ValueError("independent callback denominator mismatch")
    if endpoints is None:
        raise ValueError("full Host requires validated connection endpoints")
    endpoint_keys = [(int(e["socket"]), int(e["epoch"]), int(e["handle"])) for e in endpoints]
    if not endpoint_keys or len(set(endpoint_keys)) != len(endpoint_keys):
        raise ValueError("ambiguous endpoint identity")
    callbacks = {}
    for index, receipt in enumerate(receipts):
        identity = tuple(int(receipt[k]) for k in ("ingress", "socket", "epoch", "handle", "size"))
        ordinal, socket, epoch, handle, size = identity
        if (int(receipt["index"]) != index or receipt["entered"] != "1" or receipt["returned"] != "1"
                or ordinal in callbacks or not 1 <= ordinal <= count or socket < 0 or epoch <= 0
                or handle <= 0 or not 0 < size <= 65536 or (socket, epoch, handle) not in endpoint_keys):
            raise ValueError("invalid independent callback/connection receipt")
        callbacks[ordinal] = identity
    def original_identity(row):
        return tuple(row[k] for k in ("ingress", "ingress_socket", "ingress_epoch", "ingress_handle", "ingress_size"))
    decisions = defaultdict(list)
    stages = defaultdict(list)
    budget = defaultdict(list)
    for row in rows:
        kind = row["kind"]
        if kind in ("CHARGE", "CAPACITY_REFUSAL", "DECISION"):
            identity = original_identity(row)
            if callbacks.get(identity[0]) != identity or row["size"] != identity[4]:
                raise ValueError("original receive identity differs from independent callback")
        if kind == "DECISION":
            if row["decision_socket"] != row["ingress_socket"]:
                raise ValueError("decision socket differs from callback")
            decisions[row["ingress"]].append(row)
        elif kind == "STAGE":
            # Current TLS may belong to an overtaking callback, or be empty.
            # Ownership is exclusively the immutable original scope/record.
            stages[(row["scope"], row["record"])].append(row["stage"])
        elif row["record"]:
            budget[(row["scope"], row["record"])].append(row)
    if set(decisions) != set(callbacks):
        raise ValueError("missing callback decisions")
    observed = dict(seen=0, admitted=0, refused_phase=0, refused_native=0, refused_receive=0, adapter_rejected=0)
    binding_groups = defaultdict(list)
    adapter_sequences = []
    record_decisions = {}
    refusal_decisions = set()
    for ordinal, values in decisions.items():
        adapters = [v for v in values if v["layer"] == 1]
        bindings = [v for v in values if v["layer"] == 0]
        if len(adapters) != 1 or len(adapters) + len(bindings) != len(values):
            raise ValueError("exactly one Adapter decision per original callback required")
        a = adapters[0]
        branch = a["branch"]
        statuses = {8: 6, 9: 5, 10: 15, 11: 10, 12: 9, 13: 13, 14: 0}
        if (branch not in statuses or a["status_known"] != 1 or a["status"] != statuses[branch]
                or any(a[k] for k in ("bridge_called", "refused", "refused_total", "scope", "record", "gate", "binding_record", "result_known", "capacity_known", "reason", "admission_stage"))
                or a["result"] != -1 or len(bindings) != int(branch == 14)):
            raise ValueError("Adapter branch contract/Binding coverage")
        adapter_sequences.append(a["decision_sequence"])
        observed["adapter_rejected"] += int(branch != 14)
        for b in bindings:
            branch = b["branch"]
            if not 1 <= branch <= 7 or b["role"] != a["role"]:
                raise ValueError("Binding branch/layer contract")
            observed["seen"] += 1
            category = ("refused_phase" if branch == 1 else "refused_receive" if branch in (2, 7)
                        else "refused_native" if branch in (3, 4, 5) else "admitted")
            observed[category] += 1
            if (b["bridge_called"] != int(branch in (6, 7)) or b["refused"] != int(branch in (2, 7))
                    or b["status_known"] != int(branch != 1)
                    or (branch not in (2, 7) and b["refused_total"] != 0)):
                raise ValueError("Binding refusal/status flags")
            binding_groups[(b["binding_record"], b["role"], b["decision_socket"])].append(b)
            if branch < 6:
                if (any(b[k] for k in ("scope", "record", "gate", "result_known", "capacity_known", "reason", "admission_stage"))
                        or b["result"] != -1 or (branch == 1 and b["status"] != -1)
                        or (branch == 2 and b["status"] != 43) or (branch == 5 and b["status"] != 13)
                        or (branch in (3, 4) and not 0 < b["status"] <= 50)):
                    raise ValueError("pre-bridge Binding refusal contract")
                continue
            result_status = {0:0,1:12,2:12,3:29,4:3,5:4,6:37,7:29,8:2,9:20,10:3,11:49}
            if (b["result_known"] != 1 or b["result"] not in result_status
                    or b["status"] != result_status[b["result"]] or (b["result"] == 0) != (branch == 6)
                    or not 1 <= b["admission_stage"] <= 10 or not 1 <= b["reason"] <= 40):
                raise ValueError("bridge result/status contract")
            if branch == 6 and (b["reason"], b["admission_stage"], b["capacity_known"]) != (39,10,0):
                raise ValueError("accepted queue contract")
            # Exact bridge branches available from this Host Runtime path.
            contracts = {8: {(6,4)}, 9: {(6,4),(7,4)}, 10: {(7,5)},
                         16: {(6,7)}, 17: {(6,10)}, 18: {(7,7)}, 19: {(7,1)},
                         20: {(7,9)}, 21: {(7,3)}, 22: {(7,3)}, 23: {(7,2)},
                         24: {(7,11),(10,11)}, 31: {(7,8)}, 32: {(8,4)},
                         33: {(8,8)}, 34: {(9,7)}, 35: {(10,7)}, 36: {(10,7)},
                         37: {(10,7)}, 38: {(10,7)}, 39: {(10,0)}}
            contracts.update({reason: {(7,6)} for reason in range(25,31)})
            if (b["admission_stage"],b["result"]) not in contracts.get(b["reason"],set()):
                raise ValueError("bridge reason/stage/result contract")
            is_capacity = 25 <= b["reason"] <= 30
            if b["capacity_known"] != int(is_capacity) or (is_capacity and b["result"] != 6):
                raise ValueError("capacity refusal flags/result")
            if b["scope"]:
                original = gates[(b["scope"], b["gate"])]
                if original_identity(original) != original_identity(b):
                    raise ValueError("decision/gate full original identity")
                if original["kind"] == "CHARGE":
                    if b["reason"] not in (24,32,33,34,35,36,37,38,39):
                        raise ValueError("precharge refusal claimed an allocated reservation")
                    key = (b["scope"], b["record"])
                    if key in record_decisions:
                        raise ValueError("duplicate record decision")
                    record_decisions[key] = b
                else:
                    key = (b["scope"], b["gate"])
                    if not is_capacity or key in refusal_decisions:
                        raise ValueError("duplicate or unclassified capacity refusal")
                    refusal_decisions.add(key)
            elif branch == 6 or is_capacity or b["reason"] in (32,33,34,35,36,37,38) or b["admission_stage"] >= 8:
                raise ValueError("missing authoritative gate for acceptance/capacity")
    if sorted(adapter_sequences) != list(range(1,count+1)):
        raise ValueError("Adapter sequence coverage")
    for values in binding_groups.values():
        sequences = sorted(v["decision_sequence"] for v in values)
        if sequences != list(range(1,len(values)+1)):
            raise ValueError("Binding sequence coverage")
        totals = sorted(v["refused_total"] for v in values if v["refused"])
        if totals != list(range(1,len(totals)+1)):
            raise ValueError("Binding refusal total coverage")
    if any(int(cov[k]) != value for k,value in observed.items()):
        raise ValueError("final independent Adapter/Binding counters disagree")
    if set(record_decisions) != set(history) or refusal_decisions != {k for k,e in gates.items() if e["kind"] == "CAPACITY_REFUSAL"}:
        raise ValueError("missing decision for original gate")
    for key, original in history.items():
        kinds = [e["kind"] for e in budget[key]]
        d = record_decisions[key]
        actual = stages.get(key, [])
        expected = {0}
        if "REFUND_RECORD" in kinds:
            expected.add(6) # storage, distinct from budget refund
        if "QUEUE" in kinds:
            expected.update((1,2))
            if d["branch"] != 6:
                raise ValueError("queued record was not accepted")
        elif d["branch"] == 6:
            raise ValueError("accepted record never queued")
        elif d["reason"] not in (31,32,33):
            expected.add(1) # owned copy precedes native/commit gate refusal
        if "ENTER" in kinds:
            expected.update((3,4,5)) # this Host Runtime PumpReceive runs admitted work
        if len(actual) != len(set(actual)) or set(actual) != expected:
            raise ValueError("missing/duplicate lifecycle stage for actual record outcome")

    return {"ingress": count, "independent_counters": observed, "binding_profile": "EXCLUSIVE_BINDING",
            "original_identity": "ORDINAL_SOCKET_EPOCH_HANDLE_SIZE", "lifecycle_ownership": "IMMUTABLE_SCOPE_RECORD"}
