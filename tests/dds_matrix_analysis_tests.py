import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("matrix_analyzer", ROOT / "scripts/analyze_dds_matrix.py")
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)
FIXTURES = json.loads((Path(__file__).parent / "matrix_trace_fixtures.json").read_text())
MATRIX_BYTES = base64.b64decode(FIXTURES["matrix_base64"])
assert hashlib.sha256(MATRIX_BYTES).hexdigest() == FIXTURES["matrix_sha256"]
MATRIX = json.loads(MATRIX_BYTES)

def renumber_trace(f):
    """Preserve physical ordering and update real controller phase references."""
    for side in "AB":
        order=0
        for line,row in enumerate(f["rows"][side],1):
            row["_line"]=line
            if row.get("_kind")=="MATRIX":order+=1;row["order"]=str(order)
        phases={r["phase"]:r for r in f["rows"][side] if r.get("event")=="PHASE_DONE"}
        for event in f["control"]:
            if event.get("event")=="CONTROL_PHASE_OBSERVED" and event["side"]==side:event["order"]=int(phases[event["phase"]]["order"])
    return f

def synthetic_native_contract_fixture(return_before_sample=False):
    """Synthetic record contract ONLY, built atop real M1 rows; never device evidence."""
    f=copy.deepcopy(FIXTURES["cases"]["M1"]);f["config"]["mode"]="BYTES_NATIVE"
    run=f["config"]["run"];spec=next(c for c in MATRIX["cases"] if c["case_id"]=="M1")
    guids={e["id"]:ANALYZER.endpoint_guid(run,e) for e in spec["endpoints"]}
    keys={"A":"a"*32,"B":"b"*32};ports={"A":"7301","B":"7302"}
    for side in "AB":
        other="B" if side=="A" else "A";pid=str(f["processes"][side]["pid"])
        rows=[r for r in f["rows"][side] if r.get("_kind")=="MATRIX"]
        next(r for r in rows if r.get("event")=="SCOPE")["scope"]="BYTES_NATIVE"
        def matrix(event,**values):
            return {"_kind":"MATRIX","_side":side,"event":event,"run":run,"case":"M1","side":side,"pid":pid,**{k:str(v) for k,v in values.items()}}
        def native(event,invocation="99",**values):
            return {"_kind":"EXPERIMENT_UNQUALIFIED","_side":side,"run":run,"event":event,"instance_id":"1","invocation":invocation,"socket":"5","owner":"accepted","rc":"0","len":"500","hash":"0123456789abcdef","receiver_lease_id":"21","local_kind":"167","local_key":keys[side],"local_port":ports[side],"peer_kind":"167","peer_key":keys[other],"peer_port":ports[other],**{k:str(v) for k,v in values.items()}}
        def data(event):
            return [{"_kind":"EXPERIMENT_DATA","_side":side,"run":run,"event":event,"instance_id":"1","invocation":"99","writer_guid":guids["A1.W1"],"sequence_hi":"0","sequence_lo":str(i),"native_len":"500","native_hash":"0123456789abcdef"} for i in range(1,11)]
        def app(stage):
            return {"_kind":"EXPERIMENT_APPLICATION_RX","_side":side,"stage":str(stage),"runtime_id":"1","instance_id":"1","application_incarnation":"11","receiver_incarnation":"21","logical_token":"31","invocation":"99","bytes":"500"}
        representative=next(e for e in spec["endpoints"] if e["board"]==side)
        prefix=[matrix("NATIVE_IDENTITY",participant=1,participant_guid=ANALYZER.endpoint_guid(run,representative,True),runtime=1,native_instance=1,application=11,receiver=21,token=31,local_port=ports[side]),
            matrix("PARTICIPANT_QOS",participant=1,domain=73,use_builtin=0,transport_count=1,native_descriptor_count=1,udp_descriptor_count=0,intraprocess=0,local_policy=1,metatraffic_multicast_count=0,default_multicast_count=0,metatraffic_unicast_count=1,default_unicast_count=1,publisher_partition_count=0,subscriber_partition_count=0,initial_peer_count=1,default_locators=f"167:{keys[side]}:{ports[side]}",metatraffic_locators=f"167:{keys[side]}:{ports[side]}",initial_peer_locators=f"167:{keys[other]}:{ports[other]}"),
            native("SendBytes_return","1",spdp=1,sedp_pub=side=="A",sedp_sub=side=="B"),native("FastDDS_receiver_return","2",spdp=1,sedp_pub=side=="B",sedp_sub=side=="A")]
        # Keep numeric diagnostics exactly like actual printf records.
        for row in prefix:
            for key in ("sedp_pub","sedp_sub"):
                if key in row:row[key]="1" if row[key]=="True" else "0"
        for row in rows:
            if row.get("event")=="REMOTE_QOS" and row.get("source")=="get_matched":
                peer=next(e for e in spec["endpoints"] if guids[e["id"]]==row["peer_guid"])
                row.update({"topic":f"mdds-matrix-{run}-T","type":"HelloWorld","reliability":"2","durability":"0","max_blocking_seconds":"1","max_blocking_nanosec":"0","ownership":"0","deadline_infinite":"1","lifespan_infinite":"1","liveliness":"0","lease_infinite":"1","partitions":"0","data_sharing":"3","history_present":"1","history_kind":"0","history_depth":"32","resources_present":"0"})
        insert=next(i for i,r in enumerate(rows) if r.get("event")=="ENTITY_CREATED" and r.get("kind")=="participant")+1
        rows[insert:insert]=prefix
        if side=="A":
            index=next(i for i,r in enumerate(rows) if r.get("event")=="DDS_WAIT")
            tx={"_kind":"EXPERIMENT_APPLICATION_TX","_side":side,"run":run,"stage":"TX_ACCEPTED","instance_id":"1","application_incarnation":"11","logical_token":"31","invocation":"99"}
            rows[index:index]=[tx,native("SendBytes_return"),*data("SendBytes_return")]
        else:
            before=[app(0),app(1),native("OnBytes_owned_copy"),*data("OnBytes_owned_copy"),app(3),app(4),native("FastDDS_receiver_enter"),*data("FastDDS_receiver_enter")]
            after=[native("FastDDS_receiver_return"),*data("FastDDS_receiver_return"),app(5)]
            index=next(i for i,r in enumerate(rows) if r.get("event")=="SAMPLE")
            rows[index:index]=before+(after if return_before_sample else [])
            if not return_before_sample:
                index=max(i for i,r in enumerate(rows) if r.get("event")=="SAMPLE")+1;rows[index:index]=after
        rows.append({"_kind":"STAGE","_side":side,"run":run,"stage":"MATRIX_NATIVE_FINALIZED","clean":"1","dds_clean":"1","runtime_clean":"1","process_budget_settled":"1"})
        order=0
        for line,row in enumerate(rows,1):
            row["_line"]=line
            if row["_kind"]=="MATRIX":order+=1;row["order"]=str(order)
        f["rows"][side]=rows
        done={r["phase"]:r for r in rows if r.get("event")=="PHASE_DONE"}
        for event in f["control"]:
            if event.get("event")=="CONTROL_PHASE_OBSERVED" and event["side"]==side:event["order"]=int(done[event["phase"]]["order"])
    return f

class MatrixAnalysisTests(unittest.TestCase):
    def fixture(self, case="M1"):
        return copy.deepcopy(FIXTURES["cases"][case])

    def analyze(self, f, require_native=False):
        return ANALYZER.analyze_records(f["config"], f["rows"], f["control"], MATRIX,
            require_native=require_native, processes=f["processes"], consumed=f["consumed"])

    def reject(self, f):
        result = self.analyze(f)
        self.assertFalse(result["core_pass"], result)
        self.assertFalse(result["pass"], result)
        self.assertTrue(result["errors"], result)

    def test_actual_reference_corpus(self):
        for case in FIXTURES["cases"]:
            with self.subTest(case=case):
                result = self.analyze(self.fixture(case))
                self.assertTrue(result["core_pass"], result)
                self.assertTrue(result["pass"], result)
                self.assertFalse(result["native_proof_pass"])

    def test_native_is_required_by_default(self):
        f = self.fixture()
        result = ANALYZER.analyze_records(f["config"],f["rows"],f["control"],MATRIX,
            processes=f["processes"],consumed=f["consumed"])
        self.assertFalse(result["pass"])
        self.assertEqual(result["status"],"UNVERIFIED_NATIVE_PROOF")

    def test_R1_complete_R2_missing(self):
        f=self.fixture();f["rows"]["B"]=[r for r in f["rows"]["B"] if not(r.get("event")=="SAMPLE" and r.get("endpoint")=="B1.R2" and r.get("ordinal")=="10")];self.reject(f)

    def test_union_of_readers_cannot_hide_individual_missing(self):
        f=self.fixture();f["rows"]["B"]=[r for r in f["rows"]["B"] if not(r.get("event")=="SAMPLE" and ((r["endpoint"]=="B1.R1")==(int(r["ordinal"])%2==0)))];self.reject(f)

    def test_writer_same_ordinal_conflation(self):
        f=self.fixture("M3");writers=[r["writer_guid"] for r in f["rows"]["A"] if r.get("event")=="WRITE_RETURN"]
        for r in f["rows"]["B"]:
            if r.get("event")=="SAMPLE":r["writer_guid"]=writers[0]
        self.reject(f)

    def test_wrong_writer_guid(self):
        f=self.fixture();next(r for r in f["rows"]["B"] if r.get("event")=="SAMPLE")["writer_guid"]="f"*32;self.reject(f)

    def test_wrong_reader_guid(self):
        f=self.fixture();next(r for r in f["rows"]["B"] if r.get("event")=="SAMPLE")["reader_guid"]="f"*32;self.reject(f)

    def test_historical_discovery_not_current_matching(self):
        f=self.fixture("L2")
        for r in f["rows"]["B"]:
            if r.get("event")=="MATCH_SNAPSHOT" and r.get("phase")=="CHANGE":r["source"]="historical_discovery"
        self.reject(f)

    def test_later_snapshots_cannot_be_cached_initial_sets(self):
        f=self.fixture()
        for side in "AB":
            rows=[]
            for row in f["rows"][side]:
                if row.get("event")=="MATCH_SNAPSHOT" and row.get("phase")!="INITIAL":continue
                rows.append(row)
                if row.get("event")=="MATCH_SNAPSHOT":
                    for phase in ("CHANGE","FINAL","FINAL"):
                        cached=copy.deepcopy(row);cached["phase"]=phase;rows.append(cached)
            f["rows"][side]=rows
        self.reject(renumber_trace(f))

    def test_change_snapshot_requires_consumed_change_gate(self):
        f=self.fixture()
        for side in "AB":
            rows=f["rows"][side];stale=[r for r in rows if r.get("event")=="MATCH_SNAPSHOT" and r.get("phase")=="CHANGE"]
            rows[:]=[r for r in rows if r not in stale]
            index=next(i for i,r in enumerate(rows) if r.get("event")=="PHASE_DONE" and r.get("phase")=="INITIAL")
            rows[index:index]=stale
        self.reject(renumber_trace(f))

    def test_post_final_snapshot_is_required(self):
        f=self.fixture()
        for side in "AB":
            post=int(next(r for r in f["rows"][side] if r.get("event")=="PHASE_DONE" and r.get("phase")=="POST")["order"])
            f["rows"][side]=[r for r in f["rows"][side] if not(r.get("event")=="MATCH_SNAPSHOT" and r.get("phase")=="FINAL" and int(r["order"])<post)]
        self.reject(renumber_trace(f))

    def test_observation_final_snapshot_is_required(self):
        f=self.fixture()
        for side in "AB":
            observation=int(next(r for r in f["rows"][side] if r.get("event")=="OBSERVATION")["order"])
            f["rows"][side]=[r for r in f["rows"][side] if not(r.get("event")=="MATCH_SNAPSHOT" and r.get("phase")=="FINAL" and int(r["order"])>observation)]
        self.reject(renumber_trace(f))

    def test_observation_snapshot_must_follow_completed_interval(self):
        f=self.fixture()
        for side in "AB":
            rows=f["rows"][side];observation=int(next(r for r in rows if r.get("event")=="OBSERVATION")["order"])
            stale=[r for r in rows if r.get("event")=="MATCH_SNAPSHOT" and r.get("phase")=="FINAL" and int(r["order"])>observation]
            rows[:]=[r for r in rows if r not in stale]
            index=next(i for i,r in enumerate(rows) if r.get("event")=="CONTROL_CONSUMED" and r.get("phase")=="OBSERVE")+1
            rows[index:index]=stale
        self.reject(renumber_trace(f))

    def test_transient_forbidden_match_cannot_disappear_into_final_snapshot(self):
        f=self.fixture("I1");rows=f["rows"]["A"]
        forbidden=next(r["guid"] for r in f["rows"]["B"] if r.get("event")=="ENTITY_CREATED" and r.get("endpoint")=="B1.R2")
        match=copy.deepcopy(next(r for r in rows if r.get("event")=="MATCH_EVENT" and r.get("change")=="1"))
        match.update(peer_guid=forbidden,current="2",change="1");unmatch=copy.deepcopy(match);unmatch.update(current="1",change="-1")
        index=next(i for i,r in enumerate(rows) if r.get("event")=="CONTROL_CONSUMED" and r.get("phase")=="OBSERVE")+1
        rows[index:index]=[match,unmatch];self.reject(renumber_trace(f))

    def test_forbidden_unmatch_during_business_is_also_evidence_of_wrong_edge(self):
        f=self.fixture("I1");rows=f["rows"]["A"]
        forbidden=next(r["guid"] for r in f["rows"]["B"] if r.get("event")=="ENTITY_CREATED" and r.get("endpoint")=="B1.R2")
        event=copy.deepcopy(next(r for r in rows if r.get("event")=="MATCH_EVENT"))
        event.update(peer_guid=forbidden,current="0",change="-1")
        index=next(i for i,r in enumerate(rows) if r.get("event")=="CONTROL_CONSUMED" and r.get("phase")=="OBSERVE")+1
        rows.insert(index,event);self.reject(renumber_trace(f))

    def test_sample_receipt_after_pre_completion_still_fails(self):
        f=self.fixture();rows=f["rows"]["B"]
        sample=next(r for r in rows if r.get("event")=="SAMPLE" and r.get("endpoint")=="B1.R2" and r.get("ordinal")=="10")
        rows.remove(sample);index=next(i for i,r in enumerate(rows) if r.get("event")=="PHASE_DONE" and r.get("phase")=="PRE")+1
        rows.insert(index,sample);self.reject(renumber_trace(f))

    def test_negative_absence_requires_positive_control(self):
        f=self.fixture("Q_BE_REL")
        f["rows"]["A"]=[r for r in f["rows"]["A"] if not(r.get("event")=="WRITE_RETURN" and r.get("endpoint")=="A1.WC")]
        f["rows"]["B"]=[r for r in f["rows"]["B"] if not(r.get("event")=="SAMPLE" and r.get("endpoint")=="B1.RC")];self.reject(f)

    def test_wait_success_is_not_delivery(self):
        f=self.fixture();f["rows"]["B"]=[r for r in f["rows"]["B"] if r.get("event")!="SAMPLE"];self.reject(f)

    def test_application_resend_cannot_masquerade_as_history(self):
        f=self.fixture("L2");row=copy.deepcopy(next(r for r in f["rows"]["A"] if r.get("event")=="WRITE_RETURN"));row["phase"]="POST";row["sn_lo"]="100";row["order"]="999";row["_line"]=999;f["rows"]["A"].append(row);self.reject(f)

    def test_NO_APPLICATION_cannot_be_adopted(self):
        f=self.fixture();run=f["config"]["run"]
        f["rows"]["B"].extend([
            {"_kind":"EXPERIMENT_RX_ADMISSION","_line":10000,"_side":"B","run":run,"reason":"NO_APPLICATION","instance_id":"1","invocation":"99"},
            {"_kind":"EXPERIMENT_UNQUALIFIED","_line":10001,"_side":"B","run":run,"event":"FastDDS_receiver_enter","instance_id":"1","invocation":"99"}]);self.reject(f)

    def test_forged_printed_complete_does_not_help(self):
        f=self.fixture();f["rows"]["B"]=[r for r in f["rows"]["B"] if r.get("event")!="SAMPLE"]
        for r in f["rows"]["B"]:
            if r.get("event")=="LEDGER":r["complete"]="1";r["missing"]="0"
        self.reject(f)

    def test_local_qos_mismatch(self):
        f=self.fixture();next(r for r in f["rows"]["A"] if r.get("event")=="LOCAL_QOS")["max_blocking_ms"]="100";self.reject(f)

    def test_short_observation(self):
        f=self.fixture();next(r for r in f["rows"]["B"] if r.get("event")=="OBSERVATION")["duration_ms"]="1999";self.reject(f)

    def test_incompatible_status_must_name_reliability(self):
        f=self.fixture("Q_BE_REL")
        for r in f["rows"]["B"]:
            if r.get("event")=="INCOMPATIBLE_STATUS":r["reliability_policy_count"]="0"
        self.reject(f)

    def test_control_release_before_pre_done(self):
        f=self.fixture("L2");events=f["control"];event=next(r for r in events if r.get("event")=="CONTROL_RELEASE_BEGIN" and r.get("phase")=="CHANGE");events.remove(event);events.insert(0,event);self.reject(f)

    def test_original_sequence_mapping_required(self):
        f=self.fixture();next(r for r in f["rows"]["B"] if r.get("event")=="SAMPLE")["sn_lo"]="1234";self.reject(f)

    def test_case_nonce_required(self):
        f=self.fixture();next(r for r in f["rows"]["B"] if r.get("event")=="SAMPLE")["run"]="0"*16;self.reject(f)

    def test_synthetic_native_record_contract_both_callback_orders(self):
        for earlier in (False,True):
            with self.subTest(return_before_sample=earlier):
                result=self.analyze(synthetic_native_contract_fixture(earlier),True)
                self.assertTrue(result["pass"],result)
                self.assertEqual(len(result["native"]["sample_joins"]),20)

    def test_synthetic_native_wrong_receiver_lease(self):
        f=synthetic_native_contract_fixture()
        for r in f["rows"]["B"]:
            if r.get("event")=="FastDDS_receiver_enter":r["receiver_lease_id"]="999"
        result=self.analyze(f,True);self.assertTrue(result["core_pass"],result);self.assertFalse(result["native_proof_pass"],result)

    def test_synthetic_native_wrong_peer_full_locator(self):
        f=synthetic_native_contract_fixture()
        for r in f["rows"]["A"]:
            if r.get("event")=="SendBytes_return" and r.get("invocation")=="99":r["peer_port"]="7303"
        result=self.analyze(f,True);self.assertTrue(result["core_pass"],result);self.assertFalse(result["native_proof_pass"],result)

    def test_synthetic_native_failed_sdk_send(self):
        f=synthetic_native_contract_fixture()
        for r in f["rows"]["A"]:
            if r.get("event")=="SendBytes_return" and r.get("invocation")=="99":r["rc"]="-1"
        result=self.analyze(f,True);self.assertFalse(result["native_proof_pass"],result)

    def test_synthetic_native_wrong_captured_application(self):
        f=synthetic_native_contract_fixture()
        for r in f["rows"]["B"]:
            if r.get("_kind")=="EXPERIMENT_APPLICATION_RX" and r.get("stage")=="1":r["application_incarnation"]="12"
        result=self.analyze(f,True);self.assertFalse(result["native_proof_pass"],result)

if __name__ == "__main__":
    unittest.main()
