#include "softbus_cancel_contract.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace fastdds_dsoftbus_probe;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
CancelObservation Timely() {
    CancelObservation result;
    result.selected = result.workers_joined = true;
    result.sequence = 3; result.sender_tid = 10; result.shutdown_tid = 11;
    result.send_enter_us = 100; result.shutdown_request_us = 200100;
    result.shutdown_call_enter_us = 200200;
    result.send_return_us = 210100; result.shutdown_return_us = 220100; result.send_rc = -9;
    result.workers_joined_us = 230100;
    return result;
}
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"CANCEL-01 no actual in-flight interval is NOT_EXERCISED", [] {
            CancelObservation value; value.workers_joined = true;
            Require(EvaluateCancelObservation(value) == CancelVerdict::NotExercised, "no pressure is not success");
        }},
        {"CANCEL-02 both returns and independent thread required", [] {
            auto value = Timely();
            Require(EvaluateCancelObservation(value) == CancelVerdict::TimelyReturnsObserved, "observe both returns");
            value.send_rc = 0;
            Require(EvaluateCancelObservation(value) == CancelVerdict::TimelyReturnsObserved, "SDK success is not causal cancellation proof");
            value.shutdown_tid = value.sender_tid;
            Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "same thread is not independent cancellation");
        }},
        {"CANCEL-03 exact deadline and just over boundary", [] {
            auto value = Timely();
            value.send_return_us = value.shutdown_request_us + cancel_return_budget_us;
            value.shutdown_return_us = value.send_return_us;
            value.workers_joined_us = value.send_return_us;
            Require(EvaluateCancelObservation(value) == CancelVerdict::TimelyReturnsObserved, "inclusive boundary");
            ++value.send_return_us;
            Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "late send fails");
            --value.send_return_us; ++value.shutdown_return_us;
            Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "late Shutdown fails");
        }},
        {"CANCEL-04 missing returns joins and watchdog never pass", [] {
            for (int choice = 0; choice != 6; ++choice) {
                auto value = Timely();
                if (choice == 0) value.send_return_us = -1;
                if (choice == 1) value.shutdown_return_us = -1;
                if (choice == 2) value.workers_joined = false;
                if (choice == 3) value.watchdog_terminated = true;
                if (choice == 4) value.failed = true;
                if (choice == 5) value.send_return_us = value.send_enter_us - 1;
                Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "incomplete evidence rejected");
            }
        }},
        {"CANCEL-05 threshold measures one SDK call, not aggregate", [] {
            CancelContract value;
            Require(value.BeginSend(0, 10, 0) && value.FinishSend(0, 0, 150000), "first fast send");
            Require(!value.ClaimShutdown(11, 210000), "already returned call cannot qualify");
            Require(value.BeginSend(1, 10, 210000), "second send");
            Require(!value.ClaimShutdown(11, 409999), "199999us does not qualify");
            Require(value.ClaimShutdown(11, 410000), "single 200000us interval qualifies");
            Require(!value.ClaimShutdown(12, 420000), "Shutdown claimed once");
            Require(value.EnterShutdownCall(410100), "call boundary confirms in-flight send");
            Require(value.FinishSend(1, -1, 420000) && value.ShutdownReturned(430000), "publish returns");
            value.Joined(440000);
            auto snapshot = value.Snapshot();
            Require(snapshot.sequence == 1 && snapshot.sender_tid == 10 && snapshot.shutdown_tid == 11,
                    "selected call identity preserved");
            Require(EvaluateCancelObservation(snapshot) == CancelVerdict::TimelyReturnsObserved, "selected evidence complete");
            Require(!value.BeginSend(2, 10, 440000), "no new sends after shutdown selection");
        }},
        {"CANCEL-06 reject wrong completion and invalid sequence", [] {
            CancelContract value;
            Require(!value.BeginSend(cancel_record_limit, 1, 0), "finite send count");
            CancelContract wrong;
            Require(wrong.BeginSend(0, 1, 0), "valid begin");
            Require(!wrong.FinishSend(1, 0, 10) && wrong.Snapshot().failed, "wrong call return cannot settle a send");
        }},
        {"CANCEL-07 payload owns exact bounded records and run binding", [] {
            auto bytes = MakeCancelRecord("run-7", 1023); std::uint32_t sequence = 8;
            Require(bytes.size() == 60000 && ValidateCancelRecord(bytes.data(), bytes.size(), "run-7", sequence), "full owned record");
            Require(sequence == 1023, "sequence decoded");
            Require(!ValidateCancelRecord(bytes.data(), bytes.size(), "run-8", sequence), "cross-run rejected");
            bytes.back() ^= 1;
            Require(!ValidateCancelRecord(bytes.data(), bytes.size(), "run-7", sequence), "truncation or corruption not silently accepted");
            Require(!ValidateCancelRecord(nullptr, 60000, "run-7", sequence), "null rejected");
        }},
        {"CANCEL-08 record lengths and IDs are finite", [] {
            for (const auto& id : {std::string{}, std::string(65, 'x')}) {
                bool threw = false; try { (void)MakeCancelRecord(id, 0); } catch (const std::invalid_argument&) { threw = true; }
                Require(threw, "invalid run ID rejected");
            }
            bool threw = false; try { (void)MakeCancelRecord("ok", 4096); } catch (const std::invalid_argument&) { threw = true; }
            Require(threw, "out of budget sequence rejected");
        }},
        {"CANCEL-09 workers must join within the same cancellation deadline", [] {
            auto value = Timely();
            value.workers_joined_us = value.shutdown_request_us + cancel_return_budget_us;
            Require(EvaluateCancelObservation(value) == CancelVerdict::TimelyReturnsObserved, "join deadline inclusive");
            ++value.workers_joined_us;
            Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "late join cannot prove bounded worker lifetime");
            value.workers_joined_us = value.shutdown_return_us - 1;
            Require(EvaluateCancelObservation(value) == CancelVerdict::Failed, "join cannot precede shutdown return");
        }},
        {"CANCEL-10 bounded second workload accepts 4095 and rejects 4096", [] {
            auto bytes = MakeCancelRecord("round-two", 4095);
            std::uint32_t sequence = 0;
            Require(bytes.size() == 60000 && ValidateCancelRecord(bytes.data(), bytes.size(), "round-two", sequence) &&
                    sequence == 4095, "last allowed record roundtrips with strict run binding");
            Require(!ValidateCancelRecord(bytes.data(), bytes.size(), "other-run", sequence), "new limit does not relax run ID");
            CancelContract accepted;
            Require(accepted.BeginSend(4095, 10, 0) && accepted.FinishSend(4095, 0, 1), "last bounded send admitted");
            CancelContract rejected;
            Require(!rejected.BeginSend(4096, 10, 0), "first out of budget send refused");
            bool threw = false;
            try { (void)MakeCancelRecord("round-two", 4096); } catch (const std::invalid_argument&) { threw = true; }
            Require(threw, "out of budget record cannot be produced");
            bytes[12] = 0; bytes[13] = 16; bytes[14] = 0; bytes[15] = 0;
            Require(!ValidateCancelRecord(bytes.data(), bytes.size(), "round-two", sequence), "out of budget wire sequence rejected");
            auto evidence = Timely(); evidence.sequence = 4095;
            Require(EvaluateCancelObservation(evidence) == CancelVerdict::TimelyReturnsObserved, "last call evidence supported");
            evidence.sequence = 4096;
            Require(EvaluateCancelObservation(evidence) == CancelVerdict::Failed, "out of budget evidence rejected");
        }},
        {"CANCEL-11 completed between selection and call is NOT_EXERCISED", [] {
            CancelContract value;
            Require(value.BeginSend(0, 10, 0) && value.ClaimShutdown(11, 200000), "select actual interval");
            Require(value.FinishSend(0, 0, 200100), "send finishes before control reaches SDK boundary");
            Require(!value.EnterShutdownCall(200200), "skip selected Shutdown after overlap is lost");
            value.Joined(200300);
            const auto state = value.Snapshot();
            Require(state.shutdown_overlap_lost && state.shutdown_call_enter_us < 0, "no invented native call interval");
            Require(EvaluateCancelObservation(state) == CancelVerdict::NotExercised, "selection alone is not cancellation overlap");
        }},
        {"CANCEL-12 request-to-call delay consumes the original deadline", [] {
            CancelContract value;
            Require(value.BeginSend(0, 10, 0) && value.ClaimShutdown(11, 200000), "request starts budget");
            Require(value.EnterShutdownCall(1190000), "late call still observes a live send");
            Require(value.FinishSend(0, -1, 1195000) && value.ShutdownReturned(1201000), "short SDK call but late total completion");
            value.Joined(1202000);
            Require(EvaluateCancelObservation(value.Snapshot()) == CancelVerdict::Failed,
                    "1000ms budget must not restart at actual SDK entry");
        }},
        {"CANCEL-13 missing actual call entry cannot return or classify timely", [] {
            auto state = Timely(); state.shutdown_call_enter_us = -1;
            Require(EvaluateCancelObservation(state) == CancelVerdict::Failed, "missing SDK entry is incomplete evidence");
            CancelContract value;
            Require(value.BeginSend(0, 10, 0) && value.ClaimShutdown(11, 200000), "valid selected request");
            Require(!value.ShutdownReturned(200100) && value.Snapshot().failed, "return without SDK call rejected");
        }},
        {"CANCEL-14 first verified peer admission includes exact one second", [] {
            for (const std::int64_t at : {0LL, 999999LL, 1000000LL}) {
                CancelServerSchedule schedule;
                Require(schedule.AdmitPeer(at), "peer admitted through inclusive one second boundary");
                const auto state = schedule.Snapshot(at);
                Require(state.state == CancelServerState::PeerActive && state.first_verified_bind_us == at &&
                        state.peer_window_end_us == at + cancel_run_us, "full unclamped peer window granted");
            }
        }},
        {"CANCEL-15 late or absent peer is explicitly not admitted", [] {
            CancelServerSchedule absent;
            Require(absent.Snapshot(1000000).state == CancelServerState::WaitingForPeer, "inclusive admission still open");
            Require(absent.Snapshot(1000001).state == CancelServerState::ScheduleNotAdmitted, "no peer cannot hold server forever");
            CancelServerSchedule late;
            Require(!late.AdmitPeer(1000001), "late peer rejected rather than shortening experiment");
            const auto state = late.Snapshot(1000001);
            Require(state.state == CancelServerState::ScheduleNotAdmitted && state.peer_window_end_us == -1,
                    "late arrival never receives a clamped successful window");
        }},
        {"CANCEL-16 duplicate verified bind cannot renew the window", [] {
            CancelServerSchedule schedule;
            Require(schedule.AdmitPeer(100000), "first bind admitted");
            Require(schedule.AdmitPeer(900000), "duplicate accepted without granting a new window");
            const auto state = schedule.Snapshot(900000);
            Require(state.first_verified_bind_us == 100000 && state.peer_window_end_us == 7100000,
                    "first verified bind permanently owns deadline");
        }},
        {"CANCEL-17 causal server window covers the original client total bound", [] {
            // An artificial common timeline is ONLY a deterministic test model.
            // Native code never computes or compares a cross-process offset.
            for (const std::int64_t client_start : {0LL, 100000LL, 500000LL}) {
                const std::int64_t server_bind = client_start + 100000;
                CancelServerSchedule schedule;
                Require(schedule.AdmitPeer(server_bind), "causal verified bind admitted");
                const auto end = schedule.Snapshot(server_bind).peer_window_end_us;
                Require(end >= client_start + cancel_run_us, "server cannot expire before original client lifetime");
                Require(end > client_start + cancel_run_us - cancel_return_budget_us, "client six second active window covered");
            }
        }},
        {"CANCEL-18 expiry is failure evidence and timely peer closure is distinct", [] {
            CancelServerSchedule expired;
            Require(expired.AdmitPeer(500000), "peer admitted");
            Require(expired.Snapshot(7499999).state == CancelServerState::PeerActive, "entire peer window remains open");
            Require(expired.Snapshot(7500000).state == CancelServerState::PeerWindowExpired, "expiry is not hold success");
            expired.ObservePeerShutdown(7500001);
            Require(expired.Snapshot(7500001).state == CancelServerState::PeerWindowExpired, "late close cannot hide expiry");
            CancelServerSchedule completed;
            Require(completed.AdmitPeer(500000), "another admitted peer");
            completed.ObservePeerShutdown(2000000);
            completed.ObservePeerShutdown(3000000);
            const auto state = completed.Snapshot(8000000);
            Require(state.state == CancelServerState::PeerCompleted && state.peer_shutdown_us == 2000000,
                    "first timely peer shutdown remains completion after window ends");
        }},
        {"CANCEL-19 role specific cleanup deadlines are strict and unchanged for client", [] {
            Require(CancelCleanupWithinDeadline(false, 7000000) && !CancelCleanupWithinDeadline(false, 7000001),
                    "client still has exactly original seven second total budget");
            Require(CancelCleanupWithinDeadline(true, 9000000) && !CancelCleanupWithinDeadline(true, 9000001),
                    "server cleanup ends at nine seconds beneath external ten second watchdog");
            Require(!CancelCleanupWithinDeadline(true, -1), "invalid local timestamp rejected");
            CancelServerSchedule latest;
            Require(latest.AdmitPeer(cancel_server_admission_us), "last admitted peer");
            Require(latest.Snapshot(cancel_server_admission_us).peer_window_end_us + cancel_return_budget_us ==
                    cancel_server_run_us, "even last admission leaves one second for cleanup");
        }},
        {"CANCEL-20 workload defaults unchanged and explicit bounded hold controls", [] {
            CancelWorkload baseline;
            Require(baseline.hold_ms == 2500 && baseline.record_bytes == 60000 && baseline.max_records == 4096 &&
                    ValidateCancelWorkload(baseline, true), "original workload preserved");
            for (const char* hold : {"0", "2500", "4500"}) {
                CancelWorkload value;
                Require(SetCancelWorkloadOption(value, "--hold-ms", hold) && ValidateCancelWorkload(value, true), "bounded controls accepted");
                Require(!SetCancelWorkloadOption(value, "--hold-ms", hold), "duplicate option rejected");
            }
            CancelWorkload excessive;
            Require(SetCancelWorkloadOption(excessive, "--hold-ms", "4501") && !ValidateCancelWorkload(excessive, true), "hold cannot exceed bounded experiment");
        }},
        {"CANCEL-21 strict numeric grammar and total workload bound", [] {
            for (const char* text : {"", "-1", "+1", " 1", "1 ", "1x", "18446744073709551616"}) {
                CancelWorkload value;
                Require(!SetCancelWorkloadOption(value, "--max-records", text), "bad numeric input rejected");
            }
            CancelWorkload candidate;
            Require(SetCancelWorkloadOption(candidate, "--record-bytes", "2097152") &&
                    SetCancelWorkloadOption(candidate, "--max-records", "117") && ValidateCancelWorkload(candidate, false), "RAW candidate within fixed traffic budget");
            Require(!ValidateCancelWorkload(candidate, true), "unqualified BYTES larger than known diagnostic payload rejected");
            candidate.max_records = 118;
            Require(!ValidateCancelWorkload(candidate, false), "traffic budget cannot be exceeded by larger records");
            candidate.record_bytes = 80; candidate.max_records = 1;
            Require(ValidateCancelWorkload(candidate, true), "minimum diagnostic record valid");
            candidate.record_bytes = 79; Require(!ValidateCancelWorkload(candidate, false), "header cannot be truncated by configuration");
            candidate.record_bytes = 80; candidate.max_records = 0; Require(!ValidateCancelWorkload(candidate, false), "zero work rejected");
            candidate.max_records = 4097; Require(!ValidateCancelWorkload(candidate, false), "sequence bitmap bound preserved");
            candidate.max_records = 1; candidate.record_bytes = 2097153;
            Require(ValidateCancelWorkload(candidate, false), "RAW M plus one reaches SDK only as a bounded negative probe");
            candidate.record_bytes = 2097154;
            Require(!ValidateCancelWorkload(candidate, false), "candidate maximum is finite");
        }},
        {"CANCEL-22 every delivered record has exact configured length and content", [] {
            for (std::size_t length : {80U, 60000U, 2097124U, 2097152U}) {
                const auto data = MakeCancelRecord("lengths", 0, length); std::uint32_t sequence = 99;
                Require(data.size() == length && ValidateCancelRecord(data.data(), length, "lengths", sequence, length), "configured record roundtrip");
                Require(!ValidateCancelRecord(data.data(), length - 1, "lengths", sequence, length), "short callback cannot masquerade as complete record");
                Require(!ValidateCancelRecord(data.data(), length, "lengths", sequence, length + 1), "expected length must match");
                auto corrupt = data; corrupt.back() ^= 1;
                Require(!ValidateCancelRecord(corrupt.data(), length, "lengths", sequence, length), "last byte checked");
            }
        }},
        {"CANCEL-23 integrity manifest identifies duplicates missing and corrupt records", [] {
            CancelWorkload work; work.record_bytes = 100; work.max_records = 4;
            CancelIntegrity integrity; std::uint32_t sequence = 99;
            const auto zero = MakeCancelRecord("manifest", 0, 100), two = MakeCancelRecord("manifest", 2, 100);
            Require(integrity.Observe(two.data(), two.size(), "manifest", work, sequence) && sequence == 2, "out of order complete record accepted");
            Require(integrity.Observe(zero.data(), zero.size(), "manifest", work, sequence), "another complete record");
            Require(integrity.Observe(two.data(), two.size(), "manifest", work, sequence), "duplicate content independently checked");
            Require(!integrity.Observe(two.data(), two.size() - 1, "manifest", work, sequence), "truncation rejected");
            const auto state = integrity.Snapshot();
            Require(state.callbacks == 4 && state.valid_records == 3 && state.unique_records == 2 && state.duplicates == 1 &&
                    state.invalid_records == 1 && state.sequences[0] == 5, "manifest retains complete record identities, never counts lengths only");
            Require(state.first_bad_ordinal == 3 && state.first_bad_claimed_sequence == 2 && state.first_bad_length == 99 &&
                    state.received_bytes == 399 && state.content_hash != 14695981039346656037ULL, "failure and exact received bytes recorded");
        }},
        {"CANCEL-24 maximum returned call duration is measured without cancellation selection", [] {
            CancelContract calls;
            Require(calls.BeginSend(0, 10, 100) && calls.FinishSend(0, 0, 5100), "first return");
            Require(calls.BeginSend(1, 10, 6000) && calls.FinishSend(1, -7, 196000), "longest below selection threshold");
            Require(calls.BeginSend(2, 10, 197000) && calls.FinishSend(2, 0, 198000), "last shorter return");
            calls.Joined(199000); const auto state = calls.Snapshot();
            Require(state.completed_calls == 3 && state.first_send_enter_us == 100 && state.first_send_return_us == 5100 &&
                    state.longest_sequence == 1 && state.longest_send_enter_us == 6000 && state.longest_send_return_us == 196000 &&
                    state.longest_send_us == 190000 && state.longest_send_rc == -7, "all-call maximum preserved with same-clock interval");
            Require(EvaluateCancelObservation(state) == CancelVerdict::NotExercised, "bounded observed returns alone do not qualify pressure or cancellation");
        }},
        {"CANCEL-25 failed numeric parses cannot consume option ownership", [] {
            CancelWorkload work;
            Require(!SetCancelWorkloadOption(work, "--unknown", "1") && work.specified == 0, "unknown option cannot mutate workload");
            Require(!SetCancelWorkloadOption(work, "--hold-ms", "4294967296") && work.specified == 0 && work.hold_ms == 2500,
                    "overflow does not change default or claim key");
            Require(SetCancelWorkloadOption(work, "--hold-ms", "4500") && work.hold_ms == 4500, "valid parse after invalid is deterministic");
            Require(!SetCancelWorkloadOption(work, "--hold-ms", "0") && work.hold_ms == 4500, "duplicate cannot replace explicitly selected experiment");
        }},
        {"CANCEL-26 missing storage and out of workload sequence remain invalid records", [] {
            CancelWorkload work; work.record_bytes = 80; work.max_records = 2;
            CancelIntegrity invalid; std::uint32_t sequence = 99;
            Require(!invalid.Observe(nullptr, 80, "bad", work, sequence), "null cannot be a valid complete record");
            auto bytes = MakeCancelRecord("bad", 2, 80);
            Require(!invalid.Observe(bytes.data(), bytes.size(), "bad", work, sequence), "global sequence ceiling is not configured workload permission");
            const auto state = invalid.Snapshot();
            Require(state.callbacks == 2 && state.invalid_records == 2 && state.valid_records == 0 && state.first_bad_ordinal == 0 &&
                    state.first_bad_claimed_sequence == -1 && state.sequences[0] == 0, "invalid storage and sequence never populate manifest");
        }},
        {"CANCEL-27 source boundary plus one stays a negative-test candidate", [] {
            auto bytes = MakeCancelRecord("negative-boundary", 0, 2097153); std::uint32_t sequence = 99;
            Require(ValidateCancelRecord(bytes.data(), bytes.size(), "negative-boundary", sequence, 2097153),
                    "diagnostic encoding does not invent native capacity rejection before API");
            for (const std::size_t size : {0U, 79U, 2097154U}) {
                bool threw = false; try { (void)MakeCancelRecord("bound", 0, size); } catch (const std::invalid_argument&) { threw = true; }
                Require(threw, "producer own diagnostic limits enforced");
            }
        }},
        {"CANCEL-28 settle option is explicit strict bounded and default zero", [] {
            CancelWorkload defaults;
            Require(defaults.settle_ms == 0 && ValidateCancelWorkload(defaults, true), "existing workload has no new wait");
            for (const char* text : {"0", "1", "1000"}) {
                CancelWorkload work;
                Require(SetCancelWorkloadOption(work, "--settle-ms", text) && ValidateCancelWorkload(work, false), "bounded explicit settle accepted");
                Require(!SetCancelWorkloadOption(work, "--settle-ms", text), "duplicate settle option rejected");
            }
            for (const char* text : {"", "-1", "+1", " 1", "1 ", "1x", "4294967296"}) {
                CancelWorkload work;
                Require(!SetCancelWorkloadOption(work, "--settle-ms", text) && work.settle_ms == 0, "invalid input cannot add wait");
            }
            CancelWorkload excessive;
            Require(SetCancelWorkloadOption(excessive, "--settle-ms", "1001") && !ValidateCancelWorkload(excessive, false), "settle cannot exceed one second");
        }},
        {"CANCEL-29 settle requires joined non-cancelled successful client", [] {
            CancelObservation value; value.workers_joined = true; value.workers_joined_us = 300;
            value.send_enter_us = 100; value.send_return_us = 200;
            Require(DecideCancelSettle(false, 1000, value, 400) == CancelSettleDecision::Wait, "complete unselected client can observe delivery window");
            Require(DecideCancelSettle(false, 0, value, 400) == CancelSettleDecision::NotRequested, "default no-op");
            Require(DecideCancelSettle(true, 1000, value, 400) == CancelSettleDecision::SkipContext, "server accepts matching config but never adds wait");
            for (unsigned condition = 0; condition != 5; ++condition) {
                auto blocked = value;
                if (condition == 0) blocked.selected = true;
                if (condition == 1) blocked.failed = true;
                if (condition == 2) blocked.workers_joined = false;
                if (condition == 3) blocked.send_return_us = -1;
                if (condition == 4) blocked.watchdog_terminated = true;
                Require(DecideCancelSettle(false, 1000, blocked, 400) == CancelSettleDecision::SkipContext, "settle never delays active/cancelled/failed native work");
            }
        }},
        {"CANCEL-30 settle must fit original total deadline without clamp", [] {
            CancelObservation value; value.workers_joined = true; value.workers_joined_us = 1;
            Require(DecideCancelSettle(false, 1000, value, 6000000) == CancelSettleDecision::Wait, "exact remaining one-second window fits");
            Require(DecideCancelSettle(false, 1000, value, 6000001) == CancelSettleDecision::FailedSchedule, "one microsecond short fails instead of shortening window");
            Require(DecideCancelSettle(false, 1, value, 6999000) == CancelSettleDecision::Wait, "exact millisecond boundary");
            Require(DecideCancelSettle(false, 1, value, 6999001) == CancelSettleDecision::FailedSchedule, "deadline not extended");
            Require(DecideCancelSettle(false, 1001, value, 2) == CancelSettleDecision::FailedSchedule, "defensive duration limit");
            Require(DecideCancelSettle(false, 1, value, -1) == CancelSettleDecision::FailedSchedule, "invalid clock rejected");
            value.workers_joined_us = 100;
            Require(DecideCancelSettle(false, 1, value, 99) == CancelSettleDecision::FailedSchedule, "window cannot begin before observed join");
        }},
        {"CANCEL-31 settle scheduling cannot rewrite SDK or cancellation evidence", [] {
            const auto selected = Timely();
            Require(DecideCancelSettle(false, 1000, selected, selected.workers_joined_us) == CancelSettleDecision::SkipContext,
                    "selected cancellation receives no observation delay");
            Require(EvaluateCancelObservation(selected) == CancelVerdict::TimelyReturnsObserved, "immutable original evidence remains classified independently");
            auto failed = selected; failed.send_return_us = failed.shutdown_request_us + cancel_return_budget_us + 1;
            failed.workers_joined_us = failed.send_return_us;
            Require(DecideCancelSettle(false, 1000, failed, failed.workers_joined_us) == CancelSettleDecision::SkipContext &&
                    EvaluateCancelObservation(failed) == CancelVerdict::Failed, "old one-second failure cannot become green through settle");
        }},
        {"CANCEL-32 post-construction admission closes exactly at six seconds", [] {
            CancelContract before;
            Require(TryBeginCancelSend(before, 0, 10, 5999999, false) == CancelSendAdmission::Admitted &&
                    before.Snapshot().send_enter_us == 5999999, "last pre-cut admission retains its original marker");
            for (const auto at : {6000000LL, 6000001LL, 6000855LL}) {
                CancelContract closed;
                Require(TryBeginCancelSend(closed, 0, 10, at, false) == CancelSendAdmission::AdmissionClosed &&
                        closed.Snapshot().send_enter_us == -1 && closed.Snapshot().completed_calls == 0,
                        "at or past cutoff never creates a native send invocation");
            }
        }},
        {"CANCEL-33 owned input construction crossing cutoff performs zero native calls", [] {
            std::int64_t clock = 5999000; unsigned native_calls = 0;
            Require(clock < cancel_run_us - cancel_return_budget_us, "loop guard initially open");
            CancelContract contract;
            {
                auto owned = MakeCancelRecord("construction-crossing", 0, 2097152);
                clock = 6000855; // Construction consumed the remaining admission window.
                if (TryBeginCancelSend(contract, 0, 10, clock, false) == CancelSendAdmission::Admitted) ++native_calls;
                Require(!owned.empty(), "prepared data exists until admission decision");
            }
            Require(native_calls == 0 && contract.Snapshot().send_enter_us < 0 && !contract.Snapshot().failed,
                    "late local preparation releases owned buffer without fabricating native return or failure");
        }},
        {"CANCEL-34 pre-cut admitted native call still requires actual completion", [] {
            CancelContract contract;
            Require(TryBeginCancelSend(contract, 0, 10, 5999999, false) == CancelSendAdmission::Admitted, "native call admitted before cut");
            Require(EvaluateCancelObservation(contract.Snapshot()) == CancelVerdict::Failed, "open admission interval cannot masquerade as joined completion");
            Require(contract.FinishSend(0, 0, 6056845), "real return after cutoff still settles the owned invocation");
            contract.Joined(6056900);
            const auto completed = contract.Snapshot();
            Require(completed.completed_calls == 1 && completed.send_return_us == 6056845 && completed.workers_joined,
                    "admission closure never discards native completion obligations");
            Require(EvaluateCancelObservation(completed) == CancelVerdict::NotExercised, "one short completed call is not selected cancellation proof");
            auto original_failure = Timely();
            original_failure.send_return_us = original_failure.shutdown_request_us + cancel_return_budget_us + 1;
            original_failure.workers_joined_us = original_failure.send_return_us;
            Require(EvaluateCancelObservation(original_failure) == CancelVerdict::Failed, "original selected one-second failure remains unchanged");
        }},
        {"CANCEL-35 stopped input is not admitted and contract rejection is preserved", [] {
            CancelContract stopped;
            Require(TryBeginCancelSend(stopped, 0, 10, 100, true) == CancelSendAdmission::Stopped &&
                    stopped.Snapshot().send_enter_us == -1, "stop during preparation prevents new SDK entry");
            Require(TryBeginCancelSend(stopped, 0, 10, 6000000, true) == CancelSendAdmission::AdmissionClosed,
                    "cutoff crossing remains explicitly observable even after control set stop");
            CancelContract invalid;
            Require(TryBeginCancelSend(invalid, cancel_record_limit, 10, 100, false) == CancelSendAdmission::ContractRejected &&
                    invalid.Snapshot().failed, "admission helper cannot bypass sequence/contract invariants");
        }},
        {"CANCEL-36 six-second receiver pause is one explicit additional fault condition", [] {
            CancelWorkload work;
            Require(SetCancelWorkloadOption(work, "--hold-ms", "6000") && ValidateCancelWorkload(work, true) &&
                    ValidateCancelWorkload(work, false), "specific six-second receive pause admitted for the poll hypothesis");
            for (const std::uint32_t rejected : {4501U, 4502U, 5000U, 5999U, 6001U, 7000U}) {
                work.hold_ms = rejected;
                Require(!ValidateCancelWorkload(work, false), "new fault condition does not open an arbitrary larger range");
            }
            for (const std::uint32_t retained : {0U, 2500U, 4500U}) {
                work.hold_ms = retained;
                Require(ValidateCancelWorkload(work, true), "existing controls remain unchanged");
            }
        }},
        {"CANCEL-37 longer remote pause never increases cancellation or lifetime budgets", [] {
            const CancelWorkload defaults;
            Require(defaults.hold_ms == 2500 && defaults.settle_ms == 0, "original default fixture unchanged");
            Require(cancel_blocked_us == 200000 && cancel_return_budget_us == 1000000 &&
                    cancel_run_us == 7000000 && cancel_server_run_us == 9000000 &&
                    cancel_server_admission_us == 1000000, "all operation deadlines remain the original values");
            CancelServerSchedule expired;
            Require(expired.AdmitPeer(0) && expired.Snapshot(7000000).state == CancelServerState::PeerWindowExpired,
                    "long pause cannot renew or waive server peer-window failure");
            auto old_failure = Timely();
            old_failure.send_return_us = old_failure.shutdown_request_us + 1393675;
            old_failure.workers_joined_us = old_failure.send_return_us;
            Require(EvaluateCancelObservation(old_failure) == CancelVerdict::Failed,
                    "old measured candidate cancellation failure remains failure");
        }},
        {"CANCEL-38 server admission option allows only explicit one or three seconds", [] {
            const CancelWorkload defaults;
            Require(defaults.server_admission_ms == 1000 && ValidateCancelWorkload(defaults, true), "existing default schedule retained");
            for (const char* text : {"1000", "3000"}) {
                CancelWorkload work;
                Require(SetCancelWorkloadOption(work, "--server-admission-ms", text) && ValidateCancelWorkload(work, true) &&
                        ValidateCancelWorkload(work, false), "both profiles accept the explicit fixture choice");
                Require(!SetCancelWorkloadOption(work, "--server-admission-ms", text), "duplicate option cannot replace the schedule");
            }
            for (const char* text : {"0", "1001", "2000", "2999", "3001", "4294967295"}) {
                CancelWorkload work;
                Require(SetCancelWorkloadOption(work, "--server-admission-ms", text) && !ValidateCancelWorkload(work, false), "other fixture durations rejected before provider entry");
            }
            for (const char* text : {"", "-1", "+3000", "3000x", " 3000", "4294967296"}) {
                CancelWorkload work;
                Require(!SetCancelWorkloadOption(work, "--server-admission-ms", text), "malformed option rejected");
            }
        }},
        {"CANCEL-39 observed 1.35 second bind remains rejected by default and admitted only explicitly", [] {
            CancelServerSchedule baseline;
            Require(!baseline.AdmitPeer(1350000) && baseline.Snapshot(1350000).state == CancelServerState::ScheduleNotAdmitted,
                    "historical default schedule failure cannot be relabeled");
            CancelServerSchedule explicit_window(3000);
            Require(explicit_window.AdmitPeer(1350000), "explicit three-second fixture reaches delayed native path");
            const auto state = explicit_window.Snapshot(1350000);
            Require(state.state == CancelServerState::PeerActive && state.first_verified_bind_us == 1350000 &&
                    state.peer_window_end_us == 8350000, "full seven-second peer window starts at causal first bind");
        }},
        {"CANCEL-40 three-second admission boundary and duplicate binding cannot reset the anchor", [] {
            CancelServerSchedule boundary(3000);
            Require(boundary.Snapshot(3000000).state == CancelServerState::WaitingForPeer && boundary.AdmitPeer(3000000),
                    "inclusive first bind at three seconds");
            Require(boundary.Snapshot(3000000).peer_window_end_us == 10000000 && boundary.AdmitPeer(9000000) &&
                    boundary.Snapshot(9000000).peer_window_end_us == 10000000, "duplicate callback cannot extend original peer window");
            Require(boundary.Snapshot(10000000).state == CancelServerState::PeerWindowExpired, "peer expiry still fails");
            CancelServerSchedule late(3000);
            Require(!late.AdmitPeer(3000001) && late.Snapshot(3000001).state == CancelServerState::ScheduleNotAdmitted,
                    "one microsecond late is not silently clamped or admitted");
            CancelServerSchedule absent(3000);
            Require(absent.Snapshot(3000001).state == CancelServerState::ScheduleNotAdmitted, "no peer cannot keep fixture alive indefinitely");
        }},
        {"CANCEL-41 server role budgets vary explicitly while client budgets never change", [] {
            Require(CancelRoleRunLimitUs(true, 1000) == 9000000 && CancelRoleRunLimitUs(true, 3000) == 11000000,
                    "server total is admission plus seven-second peer plus one-second cleanup");
            Require(CancelRequiredWatchdogUs(true, 1000) == 10000000 && CancelRequiredWatchdogUs(true, 3000) == 13000000,
                    "required external server watchdog reported per fixture");
            Require(CancelRoleRunLimitUs(false, 1000) == 7000000 && CancelRoleRunLimitUs(false, 3000) == 7000000 &&
                    CancelRequiredWatchdogUs(false, 3000) == 10000000, "client total and watchdog unchanged under either fixture");
            Require(CancelCleanupWithinDeadline(true, 11000000, 3000) && !CancelCleanupWithinDeadline(true, 11000001, 3000) &&
                    !CancelCleanupWithinDeadline(false, 7000001, 3000), "actual cleanup uses role-specific finite deadline");
            Require(cancel_return_budget_us == 1000000 && cancel_blocked_us == 200000 &&
                    cancel_run_us - cancel_return_budget_us == 6000000, "SDK cancellation and client admission untouched");
        }},
        {"CANCEL-42 invalid schedule cannot create an unbounded role", [] {
            for (const std::uint32_t duration : {0U, 1001U, 2999U, 3001U, 4294967295U}) {
                bool threw = false;
                try { CancelServerSchedule invalid(duration); } catch (const std::invalid_argument&) { threw = true; }
                Require(threw && CancelRoleRunLimitUs(true, duration) < 0 && CancelRequiredWatchdogUs(true, duration) < 0 &&
                        !CancelCleanupWithinDeadline(true, 0, duration), "invalid fixture fails explicitly without overflow or fallback");
            }
        }},
        {"DELIVERY-01 fixed delivery experiment is separate and explicitly admitted", [] {
            CancelWorkload work;
            work.hold_ms=4500; work.record_bytes=2097152; work.max_records=64; work.server_admission_ms=3000;
            Require(SetCancelWorkloadOption(work,"--delivery-window-ms","3000") && ValidateCancelWorkload(work,false),
                    "delivery window option must exist instead of early ordinary cleanup");
            Require(!ValidateCancelWorkload(work,true) && !SetCancelWorkloadOption(work,"--delivery-window-ms","3000"),
                    "RAW-only diagnostic and duplicate option rejected");
            work.delivery_window_ms=2999; Require(!ValidateCancelWorkload(work,false),"arbitrary window rejected");
            work.delivery_window_ms=3000; work.settle_ms=1;
            Require(!ValidateCancelWorkload(work,false),"legacy settle cannot replace causal delivery receipt");
            CancelWorkload zero;
            Require(SetCancelWorkloadOption(zero,"--delivery-window-ms","0") && !ValidateCancelWorkload(zero,false),
                    "explicit zero cannot silently disable a requested delivery experiment");
        }},
        {"DELIVERY-02 no receipt or cleanup trigger before release plus full local window", [] {
            Require(!CancelDeliveryReceiptEligible(-1,9000000),"no observed release cannot start window");
            Require(!CancelDeliveryReceiptEligible(4500000,4500000) && !CancelDeliveryReceiptEligible(4500000,7499999),
                    "sender returning while hold active or window running is not completion");
            Require(CancelDeliveryReceiptEligible(4500000,7500000),"full three seconds required locally");
            Require(cancel_return_budget_us==1000000 && cancel_blocked_us==200000 && cancel_run_us==7000000,
                    "original cancellation constants untouched");
        }},
        {"DELIVERY-03 exact observed records and complete delivery are distinct", [] {
            CancelWorkload work; work.max_records=64; work.record_bytes=2097152;
            CancelIntegrityObservation seen; seen.callbacks=seen.valid_records=seen.unique_records=1;
            seen.received_bytes=work.record_bytes; seen.sequences[0]=1;
            Require(EvaluateCancelDelivery(seen,work)==CancelDeliveryResult::DeliveryIncomplete,
                    "one intact record out of sixty-four is missing delivery, not corruption");
            seen.invalid_records=1;
            Require(EvaluateCancelDelivery(seen,work)==CancelDeliveryResult::ObservedIntegrityFailed,"malformed callback separate");
            seen.invalid_records=0; seen.duplicates=1;
            Require(EvaluateCancelDelivery(seen,work)==CancelDeliveryResult::ObservedIntegrityFailed,"duplicate separate");
            seen.duplicates=0; seen.callbacks=seen.valid_records=seen.unique_records=64;
            seen.received_bytes=64ULL*work.record_bytes; seen.sequences[0]=~0ULL;
            Require(EvaluateCancelDelivery(seen,work)==CancelDeliveryResult::Complete,"all identities and bytes observed");
            seen.sequences[0]^=1;
            Require(EvaluateCancelDelivery(seen,work)==CancelDeliveryResult::DeliveryIncomplete,"counts alone cannot hide missing identity");
        }},
        {"DELIVERY-04 receipt carries exact run configuration and protected observations", [] {
            CancelWorkload work; work.hold_ms=4500; work.record_bytes=2097124; work.max_records=64;
            work.server_admission_ms=3000; work.delivery_window_ms=3000;
            CancelIntegrityObservation seen; seen.callbacks=seen.valid_records=seen.unique_records=1;
            seen.received_bytes=work.record_bytes; seen.sequences[0]=1;
            auto receipt=MakeCancelDeliveryReceipt("receipt-run",work,seen); CancelIntegrityObservation decoded;
            Require(receipt.size()==cancel_delivery_receipt_bytes &&
                    ParseCancelDeliveryReceipt(receipt.data(),receipt.size(),"receipt-run",work,decoded) &&
                    decoded.unique_records==1 && decoded.sequences[0]==1 && decoded.received_bytes==work.record_bytes,
                    "failure receipt remains a valid diagnostic, not fake complete delivery");
            for(std::size_t i=0;i<receipt.size();++i) {
                receipt[i]^=1;
                Require(!ParseCancelDeliveryReceipt(receipt.data(),receipt.size(),"receipt-run",work,decoded),"every corrupted receipt byte rejected");
                receipt[i]^=1;
            }
            Require(!ParseCancelDeliveryReceipt(receipt.data(),receipt.size()-1,"receipt-run",work,decoded) &&
                    !ParseCancelDeliveryReceipt(receipt.data(),receipt.size(),"wrong-run",work,decoded),"truncated or wrong-run control rejected");
            auto data=MakeCancelRecord("receipt-run",0,work.record_bytes);
            Require(!ParseCancelDeliveryReceipt(data.data(),data.size(),"receipt-run",work,decoded),"ordinary data is never a control receipt");
            work.record_bytes=2097152;
            Require(!ParseCancelDeliveryReceipt(receipt.data(),receipt.size(),"receipt-run",work,decoded),"profile workload mismatch rejected");
        }},
        {"DELIVERY-05 cross-record splice fails full payload check at unchanged length", [] {
            auto first=MakeCancelRecord("splice",0,1024),second=MakeCancelRecord("splice",1,1024);
            std::copy(second.begin()+512,second.end(),first.begin()+512); std::uint32_t seq=99;
            Require(!ValidateCancelRecord(first.data(),first.size(),"splice",seq,1024),
                    "not just shorter callbacks: adjacent-record boundary corrupts identity-derived payload");
        }},
        {"DELIVERY-06 window assignment freezes at ingress including exact cutoff", [] {
            Require(CancelDeliveryWindowAdmitsRecord(false,-1,100),"first held callback belongs to observation");
            Require(CancelDeliveryWindowAdmitsRecord(false,4500000,7499999),"one microsecond before cut belongs to window");
            Require(!CancelDeliveryWindowAdmitsRecord(false,4500000,7500000) &&
                    !CancelDeliveryWindowAdmitsRecord(false,4500000,7500001),"at or after cut belongs to late statistics even before controller runs");
            Require(!CancelDeliveryWindowAdmitsRecord(true,4500000,7499999),"frozen receipt never reopens");
            Require(!CancelDeliveryWindowAdmitsRecord(false,4500000,-1),"invalid local clock rejected");
        }}
    };
    unsigned failures = 0;
    for (const auto& test : tests) { try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& error) { ++failures; std::cout << "FAIL " << test.first << ": " << error.what() << '\n'; } }
    return failures == 0 ? 0 : 1;
}
