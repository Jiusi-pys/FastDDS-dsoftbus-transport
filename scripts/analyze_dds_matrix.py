"""Evidence-only DDS matrix oracle. Never substitutes host UDP for native proof.

Expectations come from the immutable, design-reviewed CASE_MATRIX.json. Printed
PASS/valid/complete/expected fields are not the sample or current-match oracle.
All ordering is local log order or the host release causal chain, never a
subtraction of clocks from different boards. Outer guards/load remain separate.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re

MATRIX_SHA256 = "3f51d3a4c5031e0b016d328497b1bae47b4d06082862b7229557fd3c6f4a8259"
PHASES = ("INITIAL", "PRE", "CHANGE", "POST", "OBSERVE", "CLEANUP")
RX_EVENTS = ("OnBytes_owned_copy", "FastDDS_receiver_enter", "FastDDS_receiver_return")

def integer(row, name, default=-1):
    try:
        return int(row.get(name, default))
    except (TypeError, ValueError):
        return default

def fnv64(payload):
    value = 14695981039346656037
    for byte in payload.encode("ascii"):
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:016x}"

def endpoint_guid(run, endpoint, participant=False):
    prefix = bytearray.fromhex(run)
    prefix[0] ^= 0xA1 if endpoint["board"] == "A" else 0xB2
    prefix.extend((0x44, 0x55, 0x41, endpoint["participant"]))
    if participant:
        prefix.extend((0, 0, 1, 0xC1))
    else:
        prefix.extend(int(endpoint["entity_id"]).to_bytes(3, "big"))
        prefix.append(3 if endpoint["kind"] == "writer" else 4)
    return prefix.hex()

def parse_text(text, side):
    rows = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not (line.startswith(("MATRIX ", "MATRIX_REFERENCE ", "EXPERIMENT_")) or line.startswith("stage=")):
            continue
        first, _, rest = line.partition(" ")
        kind = "STAGE" if first.startswith("stage=") else first
        tokens = line.split() if kind == "STAGE" else rest.split()
        row = {"_kind": kind, "_line": line_number, "_side": side}
        for token in tokens:
            if "=" not in token:
                continue
            key, value = token.split("=", 1)
            if key in row:
                raise ValueError(f"duplicate field {side}:{line_number}:{key}")
            row[key] = value
        rows.append(row)
    return rows

def _native_proof(config, records, spec, guids, sample_rows, phase_done):
    errors, joins = [], []
    def need(ok, reason):
        if not ok:
            errors.append(reason)
        return ok
    if config.get("mode") not in ("BYTES_NATIVE", "NATIVE"):
        return {"pass": False, "status": "UNVERIFIED_NATIVE_PROOF", "errors": ["host_reference_is_not_native"], "sample_joins": []}
    identities, native_rows, participant_qos = {}, {}, {}
    by_id = {e["id"]: e for e in spec["endpoints"]}
    expected_parts = {side: {e["participant"] for e in spec["endpoints"] if e["board"] == side} for side in "AB"}
    for side in "AB":
        side_rows = records[side]
        native_rows[side] = [r for r in side_rows if r.get("_kind") == "EXPERIMENT_UNQUALIFIED"]
        for part in expected_parts[side]:
            found = [r for r in side_rows if r.get("_kind")=="MATRIX" and r.get("event") == "NATIVE_IDENTITY" and integer(r,"participant") == part]
            if not need(len(found) == 1, f"native_identity:{side}{part}"):
                continue
            ident = found[0]
            identities[side, part] = ident
            representative=next(e for e in spec["endpoints"] if e["board"]==side and e["participant"]==part)
            need(ident.get("participant_guid")==endpoint_guid(config["run"],representative,True),f"native_participant_guid:{side}{part}")
            for key in ("runtime", "native_instance", "application", "receiver", "token", "local_port"):
                need(integer(ident,key) > 0, f"native_identity_field:{side}{part}:{key}")
            qos = [r for r in side_rows if r.get("event") == "PARTICIPANT_QOS" and integer(r,"participant") == part]
            if need(len(qos) == 1, f"native_participant_qos:{side}{part}"):
                participant_qos[side,part]=qos[0]
                facts = {"use_builtin":0,"transport_count":1,"native_descriptor_count":1,"udp_descriptor_count":0,
                    "intraprocess":0,"local_policy":1,"metatraffic_multicast_count":0,"default_multicast_count":0,
                    "metatraffic_unicast_count":1,"default_unicast_count":1,"publisher_partition_count":0,"subscriber_partition_count":0}
                domain = next(e["domain"] for e in spec["endpoints"] if e["board"]==side and e["participant"]==part)
                facts["domain"] = domain
                facts["initial_peer_count"] = len({e["participant"] for e in spec["endpoints"] if e["board"]!=side and e["domain"]==domain})
                for key,value in facts.items():
                    need(integer(qos[0],key)==value,f"native_participant_qos:{side}{part}:{key}")
                need(qos[0].get("default_locators") == qos[0].get("metatraffic_locators") and bool(qos[0].get("default_locators")), f"native_wire_readback:{side}{part}")
        for row in [r for r in side_rows if r.get("event")=="REMOTE_QOS" and r.get("source")=="get_matched"]:
            peer=next((e for e in spec["endpoints"] if guids[e["id"]]==row.get("peer_guid")),None)
            if not need(peer is not None,f"native_remote_qos_identity:{side}"):
                continue
            facts={"ownership":0,"deadline_infinite":1,"lifespan_infinite":1,"liveliness":0,"lease_infinite":1,"partitions":0,"data_sharing":3,
                "reliability":2 if peer["reliability"]=="RELIABLE" else 1,"durability":1 if peer["durability"]=="TRANSIENT_LOCAL" else 0}
            omitted=peer["kind"]=="reader" and peer["reliability"]=="BEST_EFFORT"
            facts.update({"max_blocking_seconds":0 if omitted else 1,"max_blocking_nanosec":100000000 if omitted else 0})
            for key,value in facts.items():need(integer(row,key)==value,f"native_remote_qos:{side}:{peer['id']}:{key}")
            need(row.get("type")=="HelloWorld" and row.get("topic")==f"mdds-matrix-{config['run']}-{peer['topic_suffix']}",f"native_remote_topic:{side}:{peer['id']}")
            if integer(row,"history_present")==1:need(integer(row,"history_kind")==0 and integer(row,"history_depth")==32,f"native_remote_history:{peer['id']}")
            if integer(row,"resources_present")==1:
                for key,value in {"max_samples":64,"max_instances":1,"max_samples_per_instance":64,"allocated_samples":64}.items():need(integer(row,key)==value,f"native_remote_resources:{peer['id']}:{key}")
            if integer(row,"publish_mode_present")==1:need(integer(row,"publish_mode")==0,f"native_remote_publish_mode:{peer['id']}")
        for snapshot in [r for r in side_rows if r.get("event")=="MATCH_SNAPSHOT" and r.get("peers")!="none"]:
            for peer in snapshot.get("peers","").split(","):
                need(any(r.get("event")=="REMOTE_QOS" and r.get("source")=="get_matched" and r.get("endpoint")==snapshot.get("endpoint") and r.get("peer_guid")==peer and integer(r,"order")<integer(snapshot,"order") for r in side_rows),f"native_matched_qos_missing:{side}:{peer}")
        finalized = [r for r in side_rows if r.get("stage") in ("MATRIX_NATIVE_FINALIZED", "MC_NATIVE_FINALIZE")]
        need(len(finalized)==1 and all(integer(finalized[0],k)==1 for k in ("clean","dds_clean","runtime_clean","process_budget_settled")), f"native_finalize:{side}")
        for part in expected_parts[side]:
            ident=identities.get((side,part))
            if not ident:
                continue
            instance=ident["native_instance"]
            traffic=[r for r in native_rows[side] if r.get("instance_id")==instance and r.get("rc")=="0"]
            need(any(r.get("event")=="SendBytes_return" and integer(r,"spdp")>0 for r in traffic),f"native_spdp_tx:{side}{part}")
            need(any(r.get("event")=="FastDDS_receiver_return" and integer(r,"spdp")>0 for r in traffic),f"native_spdp_rx:{side}{part}")
            local_writer=any(e["board"]==side and e["participant"]==part and e["kind"]=="writer" for e in spec["endpoints"])
            tx_kind="sedp_pub" if local_writer else "sedp_sub"
            rx_kind="sedp_sub" if local_writer else "sedp_pub"
            need(any(r.get("event")=="SendBytes_return" and integer(r,tx_kind)>0 for r in traffic),f"native_sedp_tx:{side}{part}")
            need(any(r.get("event")=="FastDDS_receiver_return" and integer(r,rx_kind)>0 for r in traffic),f"native_sedp_rx:{side}{part}")
    def data_in(side, event, instance, invocation, writer, hi, lo, length, digest):
        return any(r.get("_kind")=="EXPERIMENT_DATA" and r.get("event")==event and r.get("instance_id")==instance and
            r.get("invocation")==invocation and r.get("writer_guid")==writer and integer(r,"sequence_hi")==hi and
            integer(r,"sequence_lo")==lo and integer(r,"native_len")==length and r.get("native_hash")==digest for r in records[side])
    for (side,part),qos in participant_qos.items():
        domain=integer(qos,"domain")
        expected={q["default_locators"] for (other,_),q in participant_qos.items() if other!=side and integer(q,"domain")==domain}
        actual=qos.get("initial_peer_locators","").split(",")
        need(len(actual)==len(set(actual)) and set(actual)==expected,f"native_initial_peer_routes:{side}{part}")
    for side in "AB":
        local=[q.get("default_locators") for (owner,_),q in participant_qos.items() if owner==side]
        need(len(local)==len(set(local)),"native_distinct_participant_locators:"+side)
    for sample in sample_rows:
        reader=by_id[sample["endpoint"]]
        writer=next((e for e in spec["endpoints"] if guids[e["id"]]==sample.get("writer_guid")),None)
        if not writer:
            continue
        side,source=reader["board"],writer["board"]
        rid=identities.get((side,reader["participant"]));wid=identities.get((source,writer["participant"]))
        label=f"{reader['id']}:{writer['id']}:{sample.get('ordinal')}"
        if not rid or not wid:
            need(False,"native_sample_identity:"+label);continue
        hi,lo=integer(sample,"sn_hi"),integer(sample,"sn_lo")
        historical=integer(sample,"ordinal") in spec["late_history_before_post"].get(reader["id"],{}).get(writer["id"],[])
        late_begin=next((r for r in records[side] if r.get("event")=="ENTITY_CREATE_BEGIN" and r.get("endpoint")==reader["id"]),{})
        candidates=[]
        for enter in native_rows[side]:
            if not(enter.get("event")=="FastDDS_receiver_enter" and enter.get("instance_id")==rid["native_instance"] and
                enter.get("receiver_lease_id")==rid["receiver"] and enter.get("local_port")==rid["local_port"] and enter.get("rc")=="0" and integer(enter,"_line")<integer(sample,"_line")):
                continue
            inv=enter.get("invocation");length=integer(enter,"len");digest=enter.get("hash")
            chain={event:[r for r in native_rows[side] if r.get("event")==event and r.get("instance_id")==rid["native_instance"] and r.get("invocation")==inv] for event in RX_EVENTS}
            if not all(len(chain[event])==1 for event in RX_EVENTS):
                continue
            owned,_,returned=(chain[event][0] for event in RX_EVENTS)
            same_fields=("socket","owner","len","hash","local_kind","local_key","local_port","peer_kind","peer_key","peer_port","receiver_lease_id")
            if not all(all(row.get(k)==enter.get(k) for k in same_fields) for row in (owned,returned)):
                continue
            if not(integer(owned,"_line")<integer(enter,"_line")<integer(sample,"_line") and integer(enter,"_line")<integer(returned,"_line")):
                continue
            if historical and integer(enter,"_line")<=integer(late_begin,"_line"):
                continue
            actual_local=":".join(enter.get("local_"+key,"") for key in ("kind","key","port"))
            if actual_local!=participant_qos.get((side,reader["participant"]),{}).get("default_locators"):
                continue
            if not all(data_in(side,event,rid["native_instance"],inv,sample["writer_guid"],hi,lo,length,digest) for event in RX_EVENTS):
                continue
            app=[r for r in records[side] if r.get("_kind")=="EXPERIMENT_APPLICATION_RX" and r.get("instance_id")==rid["native_instance"] and r.get("invocation")==inv]
            if not all(sum(r.get("stage")==str(stage) for r in app)==1 for stage in (0,1,3,4,5)) or not all(r.get("runtime_id")==rid["runtime"] and r.get("application_incarnation")==rid["application"] and r.get("receiver_incarnation")==rid["receiver"] and r.get("logical_token")==rid["token"] and integer(r,"bytes")==length for r in app):
                continue
            sends=[]
            for tx in native_rows[source]:
                if not(tx.get("event")=="SendBytes_return" and tx.get("rc")=="0" and tx.get("instance_id")==wid["native_instance"] and tx.get("local_port")==wid["local_port"] and integer(tx,"len")==length and tx.get("hash")==digest):
                    continue
                actual_source=":".join(tx.get("local_"+key,"") for key in ("kind","key","port"))
                if actual_source!=participant_qos.get((source,writer["participant"]),{}).get("default_locators"):
                    continue
                if not all(tx.get("peer_"+key)==enter.get("local_"+key) and tx.get("local_"+key)==enter.get("peer_"+key) for key in ("kind","key","port")):
                    continue
                if not data_in(source,"SendBytes_return",wid["native_instance"],tx.get("invocation"),sample["writer_guid"],hi,lo,length,digest):
                    continue
                upper=[r for r in records[source] if r.get("_kind")=="EXPERIMENT_APPLICATION_TX" and r.get("instance_id")==wid["native_instance"] and r.get("invocation")==tx.get("invocation") and r.get("stage")=="TX_ACCEPTED" and r.get("application_incarnation")==wid["application"] and r.get("logical_token")==wid["token"]]
                if len(upper)!=1:
                    continue
                if historical and (integer(upper[0],"_line")<=integer(phase_done[source,"PRE"],"_line") or integer(tx,"_line")<=integer(phase_done[source,"PRE"],"_line")):
                    continue
                sends.append(tx)
            if sends:
                candidates.append((enter,returned,sends))
        if need(bool(candidates),"native_sample_chain:"+label):
            enter,returned,sends=candidates[0]
            joins.append({"reader":reader["id"],"writer":writer["id"],"sn_hi":hi,"sn_lo":lo,"ordinal":integer(sample,"ordinal"),
                "rx_instance":rid["native_instance"],"rx_invocation":enter["invocation"],"receiver":rid["receiver"],"application":rid["application"],
                "native_hash":enter["hash"],"native_len":integer(enter,"len"),"successful_tx_invocations":[r["invocation"] for r in sends],
                "qualifying_rx_candidates":[c[0]["invocation"] for c in candidates],"association_scope":"GUID_SN_FRAME_ORIGINAL_ROUTE_NOT_EXCLUSIVE_PACKET_CAUSE"})
    return {"pass":not errors,"status":"OBSERVED_NATIVE_CHAIN" if not errors else "UNVERIFIED_NATIVE_PROOF","errors":errors,
        "sample_joins":joins,"sdk_internal_settlement":"UNOBSERVED","guards_and_load":"EXTERNAL_WRAPPER_REQUIRED",
        "history_interpretation":"original single application write plus fresh upper/native transmission and late Reader delivery; not a claim of exclusive SDK-internal causality"}

def analyze_records(config, records_by_side, control_events, matrix_document, *,
                    require_native=True, processes=None, consumed=None):
    errors, reader_stats = [], {}
    def need(ok, reason):
        if not ok:
            errors.append(reason)
        return ok
    run=config.get("run","");case_id=config.get("case_id",config.get("case"))
    spec=next((c for c in matrix_document.get("cases",[]) if c.get("case_id")==case_id),None)
    if not need(bool(re.fullmatch(r"[0-9a-f]{16}",run)),"invalid_run") or not need(spec is not None,"unknown_case"):
        return {"pass":False,"core_pass":False,"native_proof_pass":False,"status":"FAIL","errors":errors}
    need(config.get("matrix_sha256")==MATRIX_SHA256,"frozen_matrix_identity")
    endpoints={e["id"]:e for e in spec["endpoints"]};guids={key:endpoint_guid(run,e) for key,e in endpoints.items()}
    by_guid={guid:endpoints[key] for key,guid in guids.items()}
    rows={side:list(records_by_side.get(side,[])) for side in "AB"}
    matrix={side:[r for r in rows[side] if r.get("_kind")=="MATRIX"] for side in "AB"}
    done,gates={},{}
    for side in "AB":
        own=matrix[side];need(bool(own),"missing_matrix_rows:"+side)
        pid=None;previous=0;last_line=0
        for row in rows[side]:
            line=integer(row,"_line");need(line>last_line,"raw_line_order:"+side);last_line=line
            if "run" in row:need(row["run"]==run,"wrong_run:"+side)
        for row in own:
            need(row.get("run")==run and row.get("case")==case_id and row.get("side")==side,"record_identity:"+side)
            value=integer(row,"pid");need(value>0 and (pid is None or value==pid),"process_identity:"+side);pid=value
            order=integer(row,"order");need(order>previous,"matrix_order:"+side);previous=order
            need(row.get("event") not in ("FIRST_FAILURE","CLEANUP_FAILURE"),"reported_failure:"+side+":"+row.get("predicate",""))
        phases=[r for r in own if r.get("event")=="PHASE_DONE"]
        need([r.get("phase") for r in phases]==list(PHASES),"phase_sequence:"+side)
        done.update({(side,r.get("phase")):r for r in phases})
        releases=[r for r in own if r.get("event")=="CONTROL_CONSUMED"]
        need([r.get("phase") for r in releases]==list(PHASES[1:]),"gate_sequence:"+side)
        gates.update({(side,r.get("phase")):r for r in releases})
        for index,phase in enumerate(PHASES[1:],1):
            need(integer(done.get((side,PHASES[index-1]),{}),"order")<integer(gates.get((side,phase),{}),"order")<integer(done.get((side,phase),{}),"order"),"phase_gate_order:"+side+":"+phase)
        if processes is not None:
            process=processes.get(side,{})
            need(process.get("exit_code")==0 and not process.get("signal") and not process.get("error"),"process_exit:"+side)
            need(integer(process,"pid")==pid,"process_pid_receipt:"+side)
        else:need(False,"missing_process_receipt:"+side)
        if consumed is None:need(False,"missing_consumed_receipts:"+side)
        else:
            for phase in PHASES[1:]:need(consumed.get(side,{}).get(phase)==f"MATRIX_RELEASE {run} {case_id} {phase}\n","consumed_receipt:"+side+":"+phase)
        observed=[r for r in own if r.get("event")=="OBSERVATION"]
        need(len(observed)==1 and integer(observed[0],"duration_ms")>=spec["observation_ms"],"full_observation:"+side)
        if observed:
            need(integer(gates.get((side,"OBSERVE"),{}),"order")<integer(observed[0],"order")<integer(done.get((side,"OBSERVE"),{}),"order"),"observation_order:"+side)
        scopes=[r for r in own if r.get("event")=="SCOPE"]
        need(len(scopes)==1 and scopes[0].get("scope")==config.get("mode") and scopes[0].get("table_sha256")==MATRIX_SHA256,"scope:"+side)
        for row in own:
            if row.get("event")=="DDS_INSTANCE_STATE":
                need(integer(row,"order")>integer(done.get((side,"OBSERVE"),{}),"order") and integer(row,"instance_state") in (2,4),"unexpected_instance_state:"+side)
        terminal={(r.get("instance_id"),r.get("invocation")) for r in rows[side] if r.get("reason")=="NO_APPLICATION"}
        for row in rows[side]:
            if row.get("event") in RX_EVENTS or (row.get("_kind")=="EXPERIMENT_APPLICATION_RX" and integer(row,"stage")>=1):
                need((row.get("instance_id"),row.get("invocation")) not in terminal,"NO_APPLICATION_adopted:"+side)
    # Host controller receipts prove cross-board barriers without comparing board clocks.
    host_seen={side:-1 for side in "AB"};begun=[];returned=Counter();last_host=-1
    for event in control_events:
        need(event.get("run")==run and event.get("case")==case_id,"controller_identity")
        timestamp=integer(event,"host_monotonic_ns");need(timestamp>=last_host,"controller_host_order");last_host=timestamp
        kind=event.get("event");phase=event.get("phase");side=event.get("side")
        if kind=="CONTROL_PHASE_OBSERVED":
            index=PHASES.index(phase) if phase in PHASES else -1
            need(side in host_seen and index==host_seen.get(side,-9)+1,"controller_phase_order")
            if side in host_seen:host_seen[side]=index
            local=done.get((side,phase),{})
            need(integer(event,"order")==integer(local,"order") and str(event.get("pid"))==str(local.get("pid")),"controller_phase_not_actual")
        elif kind=="CONTROL_RELEASE_BEGIN":
            index=len(begun)+1
            need(index<len(PHASES) and phase==PHASES[index] and event.get("prerequisite")==PHASES[index-1] and all(value>=index-1 for value in host_seen.values()),"release_without_both_prerequisites")
            begun.append(phase)
        elif kind=="CONTROL_RELEASE_RETURN":
            need(phase in begun and side in ("A","B") and event.get("result")=="PASS","release_return")
            returned[side,phase]+=1
        else:need(False,"controller_failure_or_unknown:"+str(kind))
    need(begun==list(PHASES[1:]) and all(value==5 for value in host_seen.values()),"controller_incomplete")
    need(all(returned[side,phase]==1 for side in "AB" for phase in PHASES[1:]),"controller_release_receipts")
    # Actual creation, per-endpoint QoS, and stable identities.
    created={}
    for key,e in endpoints.items():
        side=e["board"];own=matrix[side]
        begin=[r for r in own if r.get("event")=="ENTITY_CREATE_BEGIN" and r.get("endpoint")==key]
        actual=[r for r in own if r.get("event")=="ENTITY_CREATED" and r.get("endpoint")==key]
        if need(len(begin)==1 and len(actual)==1,"entity_creation_count:"+key):
            row=actual[0];created[key]=row
            need(row.get("valid")=="1","entity_reported_readback_failure:"+key)
            need(row.get("guid")==guids[key] and row.get("kind")==e["kind"] and row.get("type")==e["type"] and integer(row,"domain")==e["domain"] and row.get("topic")==f"mdds-matrix-{run}-{e['topic_suffix']}","entity_actual_configuration:"+key)
            need(row.get("phase")==e["create_phase"] and begin[0].get("phase")==e["create_phase"] and integer(begin[0],"order")<integer(row,"order"),"entity_create_phase:"+key)
            if e["create_phase"]=="CHANGE":
                need(integer(gates.get((side,"CHANGE"),{}),"order")<integer(begin[0],"order")<integer(done.get((side,"CHANGE"),{}),"order"),"late_create_barrier:"+key)
        qos=[r for r in own if r.get("event")=="LOCAL_QOS" and r.get("endpoint")==key]
        if need(len(qos)==1,"local_qos_count:"+key):
            wanted={"reliability":e["reliability"],"durability":e["durability"],"history":"KEEP_LAST","depth":"32","max_samples":"64","max_instances":"1","max_samples_per_instance":"64","allocated_samples":"64","max_blocking_ms":"1000","data_sharing":"OFF","ownership":"SHARED","deadline":"INFINITE","lifespan":"INFINITE","liveliness":"AUTOMATIC","lease":"INFINITE","publish_mode":"SYNCHRONOUS" if e["kind"]=="writer" else "NA"}
            for field,value in wanted.items():need(qos[0].get(field)==value,"local_qos:"+key+":"+field)
            need(qos[0].get("readback")=="PASS","local_qos_readback_failure:"+key)
    # A snapshot is checked from actual peer GUIDs against the canonical edges.
    for side in "AB":
        snapshots=[r for r in matrix[side] if r.get("event")=="MATCH_SNAPSHOT"]
        for row in snapshots:
            key=row.get("endpoint");e=endpoints.get(key);phase=row.get("phase")
            if not need(e is not None and e["board"]==side and phase in ("INITIAL","CHANGE","FINAL"),"snapshot_endpoint_phase"):
                continue
            edges=spec["expected_match_edges_initial"] if phase=="INITIAL" else spec["expected_match_edges_final"]
            wanted={guids[b if a==key else a] for a,b in edges if key in (a,b)}
            peers=[] if row.get("peers")=="none" else row.get("peers","").split(",")
            need(row.get("source")=="get_matched" and row.get("guid")==guids[key] and len(peers)==len(set(peers)) and set(peers)==wanted,"current_match_graph:"+key+":"+str(phase))
            need(not(phase=="INITIAL" and e["create_phase"]=="CHANGE") and not(phase!="INITIAL" and key in spec["delete_at_change"]),"snapshot_inactive_endpoint:"+key+":"+phase)
        for key,e in endpoints.items():
            if e["board"]!=side:continue
            phases=("INITIAL",) if key in spec["delete_at_change"] else (("CHANGE","FINAL") if e["create_phase"]=="CHANGE" else ("INITIAL","CHANGE","FINAL"))
            for phase in phases:
                found=[r for r in snapshots if r.get("endpoint")==key and r.get("phase")==phase]
                need(bool(found),"missing_current_snapshot:"+key+":"+phase)
                created_order=integer(created.get(key,{}),"order")
                if phase=="INITIAL":
                    windows=[("INITIAL",created_order,integer(done.get((side,"INITIAL"),{}),"order"))]
                elif phase=="CHANGE":
                    windows=[("CHANGE",max(created_order,integer(gates.get((side,"CHANGE"),{}),"order")),integer(done.get((side,"CHANGE"),{}),"order"))]
                else:
                    observation=max((integer(r,"order") for r in matrix[side] if r.get("event")=="OBSERVATION"),default=-1)
                    windows=[
                        ("POST",max(created_order,integer(gates.get((side,"POST"),{}),"order")),integer(done.get((side,"POST"),{}),"order")),
                        ("OBSERVE",max(created_order,observation),integer(done.get((side,"OBSERVE"),{}),"order")),
                    ]
                # Each snapshot must be current within its actual phase; FINAL
                # additionally needs independent POST and completed-observation
                # checks. Relabelled initial or pre-interval sets cannot qualify.
                for window,lower,upper in windows:
                    need(any(lower<integer(row,"order")<upper for row in found),"missing_snapshot_window:"+key+":"+window)
                for row in found:
                    need(any(lower<integer(row,"order")<upper for _,lower,upper in windows),"snapshot_freshness:"+key+":"+phase)
        for row in matrix[side]:
            if row.get("event")=="MATCH_EVENT" and (integer(row,"change")>0 or integer(row,"order")<integer(done.get((side,"OBSERVE"),{}),"order")):
                key=row.get("endpoint");peer=by_guid.get(row.get("peer_guid"))
                allowed={tuple(edge) for edge in spec["expected_match_edges_initial"]+spec["expected_match_edges_final"]}
                pair=(key,peer["id"]) if endpoints.get(key,{}).get("kind")=="writer" and peer else (peer["id"],key) if peer else ()
                need(pair in allowed,"unexpected_match_event:"+str(key))
        # Discovery can include unrelated endpoints without proving an edge.
        for key,e in endpoints.items():
            if e["board"]==side:continue
            initial=e["create_phase"]=="INITIAL"
            boundary="INITIAL" if initial else "CHANGE"
            discovered=[r for r in matrix[side] if r.get("event")=="DISCOVERY" and r.get("guid")==guids[key] and r.get("local")=="0" and r.get("change") in ("DISCOVERED","UPDATED") and integer(r,"order")<integer(done.get((side,boundary),{}),"order")]
            need(bool(discovered),"raw_discovery_missing:"+key+":"+side)
            remote=[r for r in matrix[side] if r.get("event")=="REMOTE_QOS" and r.get("source")=="discovery" and r.get("guid")==guids[key]]
            need(bool(remote),"remote_qos_missing:"+key)
            for row in remote:
                reliability=2 if e["reliability"]=="RELIABLE" else 1;durability=1 if e["durability"]=="TRANSIENT_LOCAL" else 0
                need(integer(row,"reliability")==reliability and integer(row,"durability")==durability,"remote_qos_kinds:"+key)
                omitted=e["kind"]=="reader" and reliability==1
                need(integer(row,"max_blocking_seconds")== (0 if omitted else 1) and integer(row,"max_blocking_nanosec")== (100000000 if omitted else 0),"remote_max_blocking:"+key)
                if omitted:need(row.get("max_blocking_source")=="OMITTED_BE_READER_DECODER_DEFAULT","remote_omission_reason:"+key)
    # Original writes are unique by logical Writer/ordinal and actual GUID/SN.
    write_rows=[r for side in "AB" for r in matrix[side] if r.get("event")=="WRITE_RETURN"]
    writes={};written_sequences=set();actual_writes=Counter()
    for row in write_rows:
        key=row.get("endpoint");e=endpoints.get(key);ordinal=integer(row,"ordinal");phase=row.get("phase")
        if not need(e is not None and e["kind"]=="writer","unknown_write_endpoint"):
            continue
        need(row.get("_side")==e["board"],"write_process_side:"+key)
        wanted=spec["pre_writes"] if phase=="PRE" else spec["post_writes"] if phase=="POST" else {}
        need(ordinal in wanted.get(key,[]),"unexpected_write_phase_ordinal:"+key)
        actual_writes[key,ordinal]+=1
        identity=(row.get("writer_guid"),integer(row,"sn_hi"),integer(row,"sn_lo"))
        need(identity[0]==guids[key] and identity[1]>=0 and (identity[1]>0 or identity[2]>0) and identity not in written_sequences,"write_identity_or_duplicate:"+key)
        written_sequences.add(identity)
        if (key,ordinal) in writes:need(False,"application_resend:"+key+":"+str(ordinal))
        writes[key,ordinal]=row
        payload=f"matrix|{run}|{case_id}|{key}|{ordinal}"
        need(row.get("payload_hash")==fnv64(payload) and integer(row,"payload_len")==len(payload) and integer(row,"rc")==0,"write_payload_or_return:"+key)
        side=e["board"]
        need(integer(gates.get((side,phase),{}),"order")<integer(row,"order")<integer(done.get((side,phase),{}),"order"),"write_barrier:"+key)
    expected_writes=Counter((key,ordinal) for field in ("pre_writes","post_writes") for key,ordinals in spec[field].items() for ordinal in ordinals)
    need(actual_writes==expected_writes,"exact_original_write_set")
    sample_rows=[r for side in "AB" for r in matrix[side] if r.get("event")=="SAMPLE"]
    observed=defaultdict(Counter);seen_sequences=set()
    for row in sample_rows:
        reader=row.get("endpoint");r=endpoints.get(reader);writer=by_guid.get(row.get("writer_guid"));ordinal=integer(row,"ordinal")
        if not need(r is not None and r["kind"]=="reader","unknown_reader"):
            continue
        need(row.get("_side")==r["board"],"sample_process_side:"+reader)
        stats=reader_stats.setdefault(reader,{"valid_unique":0,"missing":0,"duplicate":0,"wrong_writer":0,"wrong_reader":0,"unexpected_ordinal":0,"payload_mismatch":0,"invalid_sample":0,"callback_after_delete":0})
        valid=True
        if row.get("reader_guid")!=guids[reader]:stats["wrong_reader"]+=1;valid=False;need(False,"wrong_reader_guid:"+reader)
        if not writer or writer["kind"]!="writer":stats["wrong_writer"]+=1;need(False,"wrong_writer_guid:"+reader);continue
        key=writer["id"];identity=(reader,row.get("writer_guid"),integer(row,"sn_hi"),integer(row,"sn_lo"))
        if identity in seen_sequences or observed[reader][key,ordinal]:stats["duplicate"]+=1;valid=False;need(False,"duplicate_delivery:"+reader)
        seen_sequences.add(identity);observed[reader][key,ordinal]+=1
        wanted=spec["per_reader_final_samples"][reader]
        if key not in wanted:stats["wrong_writer"]+=1;valid=False;need(False,"unexpected_writer_for_reader:"+reader)
        elif ordinal not in wanted[key]:stats["unexpected_ordinal"]+=1;valid=False;need(False,"unexpected_ordinal:"+reader)
        original=writes.get((key,ordinal),{})
        if integer(row,"sn_hi")!=integer(original,"sn_hi") or integer(row,"sn_lo")!=integer(original,"sn_lo") or integer(row,"valid_data")!=1:
            stats["invalid_sample"]+=1;valid=False;need(False,"sample_original_sequence:"+reader)
        payload=f"matrix|{run}|{case_id}|{key}|{ordinal}"
        if row.get("payload_hash")!=fnv64(payload) or integer(row,"payload_len")!=len(payload) or integer(row,"payload_valid")!=1:
            stats["payload_mismatch"]+=1;valid=False;need(False,"sample_payload:"+reader)
        deletes=[d for d in matrix[r["board"]] if d.get("event")=="READER_DELETE_RETURN" and d.get("endpoint")==reader and integer(d,"rc")==0]
        if any(integer(row,"order")>integer(d,"order") for d in deletes):stats["callback_after_delete"]+=1;valid=False;need(False,"callback_after_delete:"+reader)
        begin=[b for b in matrix[r["board"]] if b.get("event")=="ENTITY_CREATE_BEGIN" and b.get("endpoint")==reader]
        need(bool(begin) and integer(row,"order")>integer(begin[0],"order"),"sample_before_reader_create:"+reader)
        if valid:stats["valid_unique"]+=1
    for reader,wanted in spec["per_reader_final_samples"].items():
        stats=reader_stats.setdefault(reader,{"valid_unique":0,"missing":0,"duplicate":0,"wrong_writer":0,"wrong_reader":0,"unexpected_ordinal":0,"payload_mismatch":0,"invalid_sample":0,"callback_after_delete":0})
        expected=Counter((writer,ordinal) for writer,ordinals in wanted.items() for ordinal in ordinals)
        stats["missing"]=sum((expected-observed[reader]).values());need(observed[reader]==expected,"independent_reader_exact_set:"+reader)
        side=endpoints[reader]["board"]
        for phase in ("PRE","CHANGE","POST","OBSERVE"):
            boundary=integer(done.get((side,phase),{}),"order")
            actual=Counter((by_guid[r["writer_guid"]]["id"],integer(r,"ordinal")) for r in sample_rows if r.get("endpoint")==reader and r.get("writer_guid") in by_guid and integer(r,"order")<boundary)
            expected_phase=expected.copy()
            if phase in ("PRE","CHANGE"):
                expected_phase=Counter((w,o) for w,o in expected if o in spec["pre_writes"].get(w,[]))
                if phase=="PRE" and endpoints[reader]["create_phase"]=="CHANGE":expected_phase=Counter()
            need(actual==expected_phase,"phase_reader_set:"+reader+":"+phase)
        # Diagnostic counters are reconciled with raw samples, not accepted as
        # an alternative to them. A forged complete=1 cannot repair omissions.
        for ledger in [r for r in matrix[side] if r.get("event")=="LEDGER" and r.get("endpoint")==reader]:
            actual=Counter((by_guid[r["writer_guid"]]["id"],integer(r,"ordinal")) for r in sample_rows if r.get("endpoint")==reader and r.get("writer_guid") in by_guid and integer(r,"order")<integer(ledger,"order"))
            wanted_at_row=expected.copy()
            if ledger.get("phase") in ("PRE","CHANGE"):
                wanted_at_row=Counter((w,o) for w,o in expected if o in spec["pre_writes"].get(w,[]))
                if ledger.get("phase")=="PRE" and endpoints[reader]["create_phase"]=="CHANGE":wanted_at_row=Counter()
            need(integer(ledger,"valid_unique")==len(actual) and integer(ledger,"missing")==sum((wanted_at_row-actual).values()) and integer(ledger,"complete")==int(actual==wanted_at_row),"ledger_diagnostic_mismatch:"+reader)
            for counter in ("duplicate","wrong_writer","wrong_reader","unexpected_ordinal","payload_mismatch","invalid_sample","callback_after_delete"):
                need(integer(ledger,counter)==0,"ledger_error_counter:"+reader+":"+counter)
    # DDS waits are operations, never delivery receipts.
    for field,phase,edges in (("pre_writes","PRE",spec["expected_match_edges_initial"]),("post_writes","POST",spec["expected_match_edges_final"])):
        for writer,ordinals in spec[field].items():
            e=endpoints[writer];waits=[r for r in matrix[e["board"]] if r.get("event")=="DDS_WAIT" and r.get("endpoint")==writer and r.get("phase")==phase]
            required=e["reliability"]=="RELIABLE" and bool(ordinals) and any(w==writer and endpoints[r]["reliability"]=="RELIABLE" for w,r in edges)
            need(len(waits)==1 and waits[0].get("state")==("Called" if required else "NotCalled") and (integer(waits[0],"rc")==0 if required else "rc" not in waits[0]),"dds_wait_policy:"+writer+":"+phase)
    for reader in spec["delete_at_change"]:
        side=endpoints[reader]["board"];deletions=[r for r in matrix[side] if r.get("event")=="READER_DELETE_RETURN" and r.get("endpoint")==reader]
        need(len(deletions)==1 and integer(deletions[0],"rc")==0 and integer(gates.get((side,"CHANGE"),{}),"order")<integer(deletions[0],"order")<integer(done.get((side,"CHANGE"),{}),"order"),"delete_change_boundary:"+reader)
    if case_id=="Q_BE_REL":
        for side,key in (("A","A1.W1"),("B","B1.R1")):
            states=[r for r in matrix[side] if r.get("event")=="INCOMPATIBLE_STATUS" and r.get("endpoint")==key]
            need(any(integer(r,"last_policy")==11 and integer(r,"reliability_policy_count")>=1 and integer(r,"total")>=1 for r in states),"reliability_incompatibility:"+side)
    core_pass=not errors
    native=_native_proof(config,rows,spec,guids,sample_rows,done) if core_pass else {"pass":False,"status":"UNVERIFIED_NATIVE_PROOF","errors":["core_failed"],"sample_joins":[]}
    native_pass=core_pass and native["pass"]
    passed=core_pass and (native_pass if require_native else config.get("mode")=="HOST_REFERENCE_UDP" or native_pass)
    status="FAIL" if not core_pass else "NATIVE_CHAIN_OBSERVED_EXTERNAL_GATES_REQUIRED" if native_pass else "UNVERIFIED_NATIVE_PROOF" if require_native else "HOST_REFERENCE_CORE_PASS"
    return {"schema":1,"run":run,"case":case_id,"pass":passed,"core_pass":core_pass,"native_proof_pass":native_pass,"status":status,
        "errors":errors,"per_reader":reader_stats,"actual_write_operations":len(write_rows),"actual_sample_deliveries":len(sample_rows),
        "native":native,"device_tested":native_pass,"sdk_internal_settlement":"UNOBSERVED","qualification":"NOT_QUALIFIED",
        "external_device_guards_and_load":"NOT_ASSESSED_BY_THIS_ANALYZER"}

def analyze_window(window, matrix_path, require_native=True):
    window,matrix_path=Path(window),Path(matrix_path)
    matrix_bytes=matrix_path.read_bytes()
    if hashlib.sha256(matrix_bytes).hexdigest()!=MATRIX_SHA256:
        raise ValueError("case matrix bytes differ from the design-reviewed freeze")
    config=json.loads((window/"window.json").read_text(encoding="utf-8-sig"))
    records={};source_hashes={}
    for side in "AB":
        candidates=[window/(side+suffix) for suffix in (".raw.stdout.log",".stdout.log",".log")]
        source=next((p for p in candidates if p.is_file()),None)
        if source is None:raise ValueError("missing raw stdout for "+side)
        raw=source.read_bytes();records[side]=parse_text(raw.decode("utf-8",errors="strict"),side)
        source_hashes[source.name]=hashlib.sha256(raw).hexdigest()
    control_path=window/"control.jsonl";control=[json.loads(line) for line in control_path.read_text().splitlines() if line.strip()]
    result_path=window/"result.json";result=json.loads(result_path.read_text()) if result_path.is_file() else {}
    consumed={}
    for side in "AB":
        directory=window/(side+"-control")
        if not directory.is_dir():directory=Path(config.get("control_dirs",{}).get(side,str(directory)))
        consumed[side]={p.stem:p.read_text(encoding="utf-8") for p in directory.glob("*.consumed")}
    analysis=analyze_records(config,records,control,json.loads(matrix_bytes),require_native=require_native,processes=result.get("processes"),consumed=consumed)
    analysis["source_sha256"]=source_hashes;analysis["matrix_sha256"]=MATRIX_SHA256
    return analysis

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("window",type=Path);parser.add_argument("--matrix",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--allow-host-reference",action="store_true",help="Explicitly allow only host-reference core evidence; never grants native proof")
    args=parser.parse_args()
    try:
        result=analyze_window(args.window,args.matrix,not args.allow_host_reference)
    except (OSError,ValueError,KeyError,TypeError) as error:
        result={"pass":False,"core_pass":False,"native_proof_pass":False,"status":"FAIL","errors":[str(error)]}
    with args.output.open("x",encoding="utf-8") as file:json.dump(result,file,indent=2);file.write("\n")
    print(json.dumps({"pass":result["pass"],"status":result["status"],"errors":result.get("errors",[])[:12]}))
    return 0 if result["pass"] else 1

if __name__=="__main__":
    raise SystemExit(main())
