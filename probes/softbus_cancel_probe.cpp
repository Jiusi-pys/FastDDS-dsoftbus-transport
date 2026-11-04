// P0 finite public-Socket experiment, NEVER a production cancellation design.
// RAW concurrent SendStream/Shutdown may expose native races. Native failure is
// evidence of failure, not permission to detach a worker or ignore its result.
#include "softbus_cancel_contract.hpp"
#include "softbus_probe_profile.hpp"
#include "token_first_entry.hpp"
#include <accesstoken_kit.h>
#include <token_setproc.h>
#include <socket.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
using namespace fastdds_dsoftbus_probe;
using Clock = std::chrono::steady_clock;
constexpr char process_name[] = "fastdds_dsoftbus_probe";
constexpr char permission_name[] = "ohos.permission.DISTRIBUTED_DATASYNC";
constexpr char package_name[] = "com.kaihong.mdds";
static_assert(probe_bytes_data_type == DATA_TYPE_BYTES, "Bytes ABI mismatch");
static_assert(probe_raw_data_type == DATA_TYPE_RAW_STREAM, "Raw ABI mismatch");
static_assert(probe_encrypted_raw_data_type == DATA_TYPE_RAW_STREAM_ENCRYPED, "Encrypted Raw ABI mismatch");

struct Options {
    bool server = false;
    ProbeProfile profile = ProbeProfile::Bytes;
    std::uint64_t token = 0;
    CancelWorkload workload;
    std::string run_id, local_name, remote_name, peer_network, expected_name, expected_network;
};
std::uint64_t Tid() noexcept { return static_cast<std::uint64_t>(syscall(SYS_gettid)); }
bool Simple(std::string_view value, std::size_t maximum) noexcept {
    if (value.empty() || value.size() > maximum) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}
bool SocketName(std::string_view value) noexcept {
    return Simple(value, 63) && value.size() > 17 && value.substr(0, 17) == "com.kaihong.mdds.";
}
bool Network(std::string_view value) noexcept {
    if (value.size() != 64) return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9'))) return false;
    return true;
}
bool Parse(int argc, char** argv, Options& options) {
    bool mode = false, profile = false, token = false, preflight = false;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if ((key == "--server" || key == "--client") && !mode) { mode = true; options.server = key == "--server"; }
        else if (key == "--profile" && !profile && i + 1 < argc) {
            profile = true; if (!ParseProbeProfile(argv[++i], options.profile)) return false;
        } else if (key == "--expected-token" && !token && i + 1 < argc) {
            token = true; if (!ParseTokenArgument(argv[++i], options.token)) return false;
        } else if (key == "--preflight-confirmed" && !preflight) preflight = true;
        else if (key == "--hold-ms" || key == "--record-bytes" || key == "--max-records" || key == "--settle-ms" ||
                key == "--server-admission-ms" || key == "--delivery-window-ms") {
            if (i + 1 == argc || !SetCancelWorkloadOption(options.workload, key, argv[++i])) return false;
        }
        else {
            std::string* field = key == "--run-id" ? &options.run_id : key == "--local-name" ? &options.local_name :
                key == "--remote-listen-name" ? &options.remote_name : key == "--peer-network-id" ? &options.peer_network :
                key == "--expected-remote-initiator-name" ? &options.expected_name :
                key == "--expected-peer-network-id" ? &options.expected_network : nullptr;
            if (!field || !field->empty() || i + 1 == argc) return false;
            *field = argv[++i]; if (field->empty()) return false;
        }
    }
    if (!mode || !profile || !token || !preflight || !ValidateCancelWorkload(options.workload, options.profile == ProbeProfile::Bytes) ||
            !AdmitTokenEntry({TokenEntryMode::Record, true, true, options.token}) ||
            !Simple(options.run_id, 64) || !SocketName(options.local_name) || !Network(options.expected_network)) return false;
    if (options.workload.delivery_window_ms != 0 && options.workload.record_bytes !=
            (options.profile == ProbeProfile::Raw ? 2097152U : 2097124U)) return false;
    return options.server ? SocketName(options.expected_name) && options.remote_name.empty() && options.peer_network.empty() :
           options.expected_name.empty() && SocketName(options.remote_name) && Network(options.peer_network) &&
           options.peer_network == options.expected_network;
}

class CancelRuntime;
// Process-lifetime dispatcher and provider: no callback targets stack objects.
// The SDK has no verified callback-drain guarantee; never dlclose the provider.
std::atomic<CancelRuntime*> runtime{nullptr};
void Bound(int, PeerSocketInfo) noexcept;
void Bytes(int, const void*, std::uint32_t) noexcept;
void Stream(int, const StreamData*, const StreamData*, const StreamFrameInfo*) noexcept;
void Closed(int, ShutdownReason) noexcept;
void Error(int, std::int32_t) noexcept;
void Message(int, const void*, std::uint32_t) noexcept;

class CancelRuntime {
public:
    explicit CancelRuntime(Options value) : options(std::move(value)), profile(*DescribeProbeProfile(options.profile)),
        server_schedule(options.workload.server_admission_ms) {}
    bool Delivery() const noexcept { return options.workload.delivery_window_ms != 0; }
    bool CleanupWithinDeadline(std::int64_t at) const noexcept {
        return Delivery() ? at >= 0 && at <= cancel_delivery_run_us :
            CancelCleanupWithinDeadline(options.server, at, options.workload.server_admission_ms);
    }
    std::int64_t Now() const noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
    }
    void Log(const char* event, int socket = 0, int code = 0, std::uint32_t sequence = 0) const noexcept {
        // One stdio call per line: no interleaved per-field writes from workers.
        std::fprintf(stdout, "event=%s run_id=%s profile=%s role=%s pid=%ld tid=%llu steady_us=%lld socket=%d code=%d sequence=%u\n",
                event, options.run_id.c_str(), profile.name, options.server ? "server" : "client", static_cast<long>(getpid()),
                static_cast<unsigned long long>(Tid()), static_cast<long long>(Now()), socket, code, sequence);
        std::fflush(stdout);
    }
    void Invocation(const char* event, std::uint32_t sequence, std::int64_t enter,
            std::int64_t returned, int rc) const noexcept {
        std::fprintf(stdout, "event=%s run_id=%s profile=%s role=%s pid=%ld invocation_id=%s:%ld:%u sequence=%u send_enter_us=%lld send_return_us=%lld send_duration_us=%lld rc=%d clock_scope=LOCAL_ONLY boundary=EXTERNAL_MARKER\n",
                event, options.run_id.c_str(), profile.name, options.server ? "server" : "client", static_cast<long>(getpid()),
                options.run_id.c_str(), static_cast<long>(getpid()), sequence, sequence,
                static_cast<long long>(enter), static_cast<long long>(returned),
                static_cast<long long>(returned >= enter && enter >= 0 ? returned - enter : -1), rc);
    }
    void CallbackEntered() noexcept {
        const auto count = ++active_callbacks;
        auto highest = peak_callbacks.load();
        while (count > highest && !peak_callbacks.compare_exchange_weak(highest, count)) {}
    }
    void Fail(const char* reason) noexcept {
        failed = true; stop = true;
        if (!failure_logged.exchange(true)) Log(reason);
        try { contract.Fail(); } catch (...) { callback_exception = true; }
    }
    bool Owns(int socket) const noexcept {
        return socket > 0 && (socket == created.load() || socket == accepted.load());
    }
    bool WorkerIdentity() noexcept {
        const auto actual = GetSelfTokenID();
        Log("worker_token", 0, actual == options.token ? 0 : -1);
        if (actual == 0 || actual != options.token) { Fail("worker_token_mismatch"); return false; }
        return true;
    }
    static bool Equal(const char* text, std::string_view expected, std::size_t maximum) noexcept {
        if (!text) return false;
        const auto length = strnlen(text, maximum);
        return length < maximum && length == expected.size() && std::memcmp(text, expected.data(), length) == 0;
    }
    void OnBound(int socket, PeerSocketInfo peer) {
        // Capture the one permitted accepted ID before checking peer metadata.
        // A second different ID is a fatal, explicitly untracked condition;
        // never run Shutdown inside this SDK callback or claim clean shutdown.
        if (!options.server || socket <= 0) { Fail("unexpected_bind_callback"); return; }
        int expected = 0;
        if (!accepted.compare_exchange_strong(expected, socket) && expected != socket) {
            int empty = 0;
            if (untracked_id.compare_exchange_strong(empty, socket)) Log("untracked_accepted_socket", socket);
            Fail("accepted_socket_limit_exceeded"); return;
        }
        if (!Equal(peer.name, options.expected_name, 256) || !Equal(peer.networkId, options.expected_network, 65) ||
                static_cast<int>(peer.dataType) != profile.peer_data_type) { Fail("peer_metadata_mismatch"); return; }
        if (!server_schedule.AdmitPeer(Now())) { Fail("SCHEDULE_NOT_ADMITTED"); return; }
        peer_ready = true; Log("OnBind", socket);
    }
    void OnData(int socket, const void* data, std::size_t size, ProbeSendMethod method) {
        if (Delivery() && !options.server) {
            if (!Owns(socket) || !peer_ready || method != profile.send_method || receipt_callbacks.fetch_add(1) != 0) {
                Fail("delivery_receipt_unexpected_or_duplicate"); return;
            }
            CancelIntegrityObservation decoded;
            if (!ParseCancelDeliveryReceipt(data, size, options.run_id, options.workload, decoded)) {
                Fail("delivery_receipt_invalid"); return;
            }
            { std::lock_guard<std::mutex> lock(delivery_gate); delivery_snapshot = decoded; }
            Log("delivery_receipt_verified", socket, static_cast<int>(size));
            receipt_received = true; // Publishes decoded fields to ordinary cleanup thread.
            return;
        }
        if (!options.server || !Owns(socket) || !peer_ready || profile.send_method != method) {
            Fail("unexpected_data_callback"); return;
        }
        const auto number = received.fetch_add(1);
        std::uint32_t sequence = 0;
        bool valid = false;
        if (Delivery()) {
            // Linearization: window assignment and the entire record's
            // validation/bitmap settlement share this gate with the cutoff
            // snapshot. No partially accounted callback can enter a receipt.
            std::lock_guard<std::mutex> lock(delivery_gate);
            if (!CancelDeliveryWindowAdmitsRecord(delivery_window_closed,hold_return_us,Now())) {
                const bool exact = delivery_late_integrity.Observe(data,size,options.run_id,options.workload,sequence);
                if (exact && (integrity.Snapshot().sequences[sequence/64] & (std::uint64_t{1}<<(sequence%64))))
                    ++delivery_cross_window_duplicates;
                Log(exact?"delivery_late_record_exact":"delivery_late_record_invalid",socket,static_cast<int>(size),sequence);
                return;
            }
            valid = integrity.Observe(data,size,options.run_id,options.workload,sequence);
        } else valid = integrity.Observe(data,size,options.run_id,options.workload,sequence);
        if (!valid) {
            if (Delivery()) { Log("delivery_record_invalid",socket,static_cast<int>(size),sequence); return; }
            Fail(number >= options.workload.max_records ? "callback_count_limit_exceeded" : "record_integrity_failed"); return;
        }
        if (Delivery()) Log("delivery_record_exact",socket,static_cast<int>(size),sequence);
        if (!hold_claimed.exchange(true)) {
            hold_enter_us = Now(); hold_tid = Tid(); hold_sequence = sequence;
            Log("callback_hold_enter", socket, 0, sequence);
            // The FIRST actual SDK data callback self-releases at the explicit
            // local hold deadline (default 2500ms, control 0ms, experiments
            // 4500ms and the explicit 6000ms native-poll fault condition).
            // No local close signal or remote action can extend this hold.
            std::this_thread::sleep_until(start + std::chrono::microseconds(hold_enter_us.load()) +
                    std::chrono::milliseconds(options.workload.hold_ms));
            hold_return_us = Now(); hold_released = true;
            Log("callback_hold_release", socket, 0, sequence);
        }
    }
    void OnClosed(int socket, int reason) {
        if (!Owns(socket)) { Fail("unknown_shutdown_callback"); return; }
        // Only the admitted peer's remote closure ends its observation window.
        // A callback caused by our own cleanup cannot manufacture completion.
        if (options.server && socket == accepted.load() && !stop) server_schedule.ObservePeerShutdown(Now());
        if (shutdown_events.fetch_add(1) == 0) peer_closed_us = Now();
        if (shutdown_events.load() <= 2) Log("OnShutdown", socket, reason);
    }
    void OnError(int socket, int code) {
        if (!Owns(socket)) { Fail("unknown_error_callback"); return; }
        if (error_events.fetch_add(1) == 0) Log("OnError", socket, code);
        if (!stop) Fail("sdk_error_before_shutdown");
    }
    bool Run() {
        tasks_before_provider = CountDirectory("/proc/self/task");
        fds_before_provider = CountDirectory("/proc/self/fd");
        provider = dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so", RTLD_NOW | RTLD_LOCAL);
        if (!provider) throw std::runtime_error("provider_load_failed");
        create = reinterpret_cast<decltype(&Socket)>(dlsym(provider, "Socket"));
        listen = reinterpret_cast<decltype(&Listen)>(dlsym(provider, "Listen"));
        bind = reinterpret_cast<decltype(&Bind)>(dlsym(provider, "Bind"));
        send_bytes = reinterpret_cast<decltype(&SendBytes)>(dlsym(provider, "SendBytes"));
        send_stream = reinterpret_cast<decltype(&SendStream)>(dlsym(provider, "SendStream"));
        shutdown = reinterpret_cast<decltype(&Shutdown)>(dlsym(provider, "Shutdown"));
        if (!create || !listen || !bind || !send_bytes || !send_stream || !shutdown)
            throw std::runtime_error("provider_symbols_missing");
        listener = {}; listener.OnBind = Bound; listener.OnBytes = Bytes; listener.OnStream = Stream;
        listener.OnShutdown = Closed; listener.OnError = Error; listener.OnMessage = Message;
        SocketInfo info{}; info.name = const_cast<char*>(options.local_name.c_str());
        info.pkgName = const_cast<char*>(package_name); info.dataType = static_cast<TransDataType>(profile.socket_data_type);
        if (!options.server) { info.peerName = const_cast<char*>(options.remote_name.c_str());
            info.peerNetworkId = const_cast<char*>(options.peer_network.c_str()); }
        Log("Socket_enter"); const int socket = create(info); created = socket; Log("Socket_return", socket);
        if (socket <= 0) { Fail("socket_failed"); return false; }
        QosTV qos[] = {{QOS_TYPE_MIN_BW, 80}, {QOS_TYPE_MAX_WAIT_TIMEOUT, 2000}};
        Log(options.server ? "Listen_enter" : "Bind_enter", socket);
        const int rc = options.server ? listen(socket, qos, 2, &listener) : bind(socket, qos, 2, &listener);
        Log(options.server ? "Listen_return" : "Bind_return", socket, rc);
        if (rc != 0) { Fail("listen_or_bind_failed"); return false; }
        if (options.server) {
            std::printf("stage=Listen action=RETURN rc=0 run_id=%s profile=%s\n", options.run_id.c_str(), profile.name);
            Log("server_ready", socket);
            if (Delivery()) return RunDeliveryServer();
            while (!failed) {
                const auto schedule = server_schedule.Snapshot(Now());
                if (schedule.state == CancelServerState::ScheduleNotAdmitted) {
                    Fail("SCHEDULE_NOT_ADMITTED"); return false;
                }
                if (schedule.state == CancelServerState::PeerWindowExpired) {
                    Fail("PEER_WINDOW_EXPIRED"); return false;
                }
                if (schedule.state == CancelServerState::PeerCompleted && hold_released) return true;
                if (!CancelCleanupWithinDeadline(true, Now(), options.workload.server_admission_ms)) {
                    Fail("server_cleanup_deadline_reached"); return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            return false;
        }
        peer_ready = true;
        try {
            if (Delivery()) controller_done = true;
            else controller = std::thread([this, socket] { Control(socket); });
            sender = std::thread([this, socket] { Send(socket); });
        } catch (...) { Fail("worker_creation_failed"); return false; }
        while (Now() < cancel_run_us - cancel_return_budget_us && (!sender_done || !controller_done)) {
            const auto state = contract.Snapshot();
            if (state.selected && !state.shutdown_overlap_lost && Now() - state.shutdown_request_us > cancel_return_budget_us &&
                    (state.send_return_us < 0 || state.shutdown_return_us < 0)) Fail("cancellation_deadline_missed");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (!sender_done || !controller_done) Fail("work_deadline_with_live_workers");
        stop = true;
        return !failed;
    }
    bool RunDeliveryServer() {
        while (!failed && CleanupWithinDeadline(Now())) {
            if (!peer_ready && server_schedule.Snapshot(Now()).state == CancelServerState::ScheduleNotAdmitted) {
                Fail("SCHEDULE_NOT_ADMITTED"); return false;
            }
            if (peer_closed_us >= 0) { Fail("delivery_peer_closed_before_window_end"); return false; }
            const auto now = Now();
            if (hold_released && CancelDeliveryReceiptEligible(hold_return_us,now)) {
                // The observation window belongs solely to this process clock.
                // Even 64 early arrivals cannot shorten its predetermined end.
                {
                    std::lock_guard<std::mutex> lock(delivery_gate);
                    delivery_window_closed = true;
                    delivery_snapshot = integrity.Snapshot();
                    delivery_window_end_us = Now();
                }
                Log("delivery_window_ended",accepted.load());
                auto receipt = MakeCancelDeliveryReceipt(options.run_id,options.workload,delivery_snapshot);
                Log("delivery_receipt_send_enter",accepted.load(),static_cast<int>(receipt.size()));
                const int rc = NativeSend(accepted.load(),receipt);
                Log("delivery_receipt_send_return",accepted.load(),rc);
                if (rc != 0) { Fail("delivery_receipt_send_failed"); return false; }
                receipt_sent = true;
                // Receipt send success is not remote receipt. Client verifies
                // it before initiating Shutdown; no server-initiated early close.
                while (!failed && peer_closed_us < 0 && CleanupWithinDeadline(Now()))
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                if (peer_closed_us < 0) { Fail("delivery_peer_close_not_observed"); return false; }
                return !failed;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        Fail("delivery_window_not_completed_within_fixture"); return false;
    }
    bool Close() {
        stop = true;
        callbacks_at_cleanup = active_callbacks.load();
        // Never detach, abandon or count live workers as successful cleanup.
        // A hung SDK join deliberately remains subject to the declared role's
        // external watchdog. Watchdog/native-crash termination is FAIL.
        if (sender.joinable()) { sender.join(); Log("sender_joined"); }
        if (controller.joinable()) { controller.join(); Log("controller_joined"); }
        contract.Joined(Now());
        Log("workers_joined_sdk_send_and_selected_shutdown_settled");
        if (Delivery() && !options.server && !failed) {
            // The data worker has actually joined. Ordinary native cleanup
            // remains postponed until the server's window-end receipt arrives.
            while (!failed && !receipt_received && peer_closed_us < 0 && CleanupWithinDeadline(Now()))
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!receipt_received) Fail(peer_closed_us >= 0 ? "delivery_peer_closed_without_receipt" : "delivery_receipt_missing");
        }
        auto settled_state = contract.Snapshot();
        settled_state.failed = settled_state.failed || failed.load() || callback_exception.load();
        const auto settle_at = Now();
        const auto settle = DecideCancelSettle(options.server, options.workload.settle_ms, settled_state, settle_at);
        if (settle == CancelSettleDecision::FailedSchedule) {
            Fail("FAILED_SCHEDULE_settle_does_not_fit_original_deadline");
        } else if (settle == CancelSettleDecision::Wait) {
            // Both owned workers have actually joined. This fixture observation
            // window is outside every SDK invocation and cancellation metric.
            // It does not acknowledge or guarantee remote delivery.
            settle_enter_us = settle_at;
            std::fprintf(stdout, "event=settle_enter run_id=%s pid=%ld steady_us=%lld requested_ms=%u absolute_client_deadline_us=7000000 delivery_guarantee=NONE\n",
                    options.run_id.c_str(), static_cast<long>(getpid()), static_cast<long long>(settle_enter_us), options.workload.settle_ms);
            std::this_thread::sleep_until(start + std::chrono::microseconds(settle_enter_us) +
                    std::chrono::milliseconds(options.workload.settle_ms));
            settle_return_us = Now();
            std::fprintf(stdout, "event=settle_return run_id=%s pid=%ld steady_us=%lld entered_us=%lld delivery_guarantee=NONE\n",
                    options.run_id.c_str(), static_cast<long>(getpid()), static_cast<long long>(settle_return_us),
                    static_cast<long long>(settle_enter_us));
            if (!CancelCleanupWithinDeadline(false, settle_return_us)) Fail("FAILED_SCHEDULE_settle_exceeded_original_deadline");
        } else if (settle == CancelSettleDecision::SkipContext) {
            std::fprintf(stdout, "event=settle_skipped run_id=%s pid=%ld server=%d selected=%d failed=%d workers_joined=%d delivery_guarantee=NONE\n",
                    options.run_id.c_str(), static_cast<long>(getpid()), options.server, settled_state.selected,
                    settled_state.failed, settled_state.workers_joined);
        }
        if (shutdown) {
            const int peer = accepted.load(), local = created.load();
            if (peer > 0 && peer != local && !accepted_shutdown.exchange(true)) {
                Log("cleanup_shutdown_enter", peer); shutdown(peer); Log("cleanup_shutdown_return", peer);
            }
            if (local > 0 && !created_shutdown.exchange(true)) {
                Log("cleanup_shutdown_enter", local); shutdown(local); Log("cleanup_shutdown_return", local);
            }
        }
        while (active_callbacks != 0 && CleanupWithinDeadline(Now()))
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (active_callbacks != 0 || !CleanupWithinDeadline(Now()) || callback_exception)
            Fail("cleanup_deadline_or_callback_failure");
        cleanup_observed_us = Now();
        tasks_after_cleanup = CountDirectory("/proc/self/task");
        fds_after_cleanup = CountDirectory("/proc/self/fd");
        Log("cleanup_local_observation_complete");
        return !failed;
    }
    void PrintSummary(bool restored, bool entry_success) {
        auto state = contract.Snapshot();
        if (failed || callback_exception || !restored || !entry_success) state.failed = true;
        const auto verdict = EvaluateCancelObservation(state);
        const auto schedule = server_schedule.Snapshot(Now());
        const char* schedule_result = "N/A";
        if (options.server) switch (schedule.state) {
            case CancelServerState::WaitingForPeer: schedule_result = "WAITING_FOR_PEER"; break;
            case CancelServerState::PeerActive: schedule_result = "PEER_ACTIVE"; break;
            case CancelServerState::PeerCompleted: schedule_result = "PEER_COMPLETED"; break;
            case CancelServerState::ScheduleNotAdmitted: schedule_result = "SCHEDULE_NOT_ADMITTED"; break;
            case CancelServerState::PeerWindowExpired: schedule_result = "PEER_WINDOW_EXPIRED"; break;
        }
        const char* result = options.server ? (state.failed || !hold_released ||
                schedule.state != CancelServerState::PeerCompleted ? "FAIL" : "HOLD_OBSERVED") : CancelVerdictName(verdict);
        if (Delivery()) {
            const bool receipt_valid = options.server ? receipt_sent.load() : receipt_received.load();
            CancelIntegrityObservation delivery;
            CancelIntegrityObservation late;
            unsigned cross_window_duplicates=0;
            {
                std::lock_guard<std::mutex> lock(delivery_gate);
                delivery = delivery_snapshot; late = delivery_late_integrity.Snapshot();
                cross_window_duplicates = delivery_cross_window_duplicates;
            }
            const auto delivery_result = EvaluateCancelDelivery(delivery,options.workload);
            result = state.failed || !receipt_valid ? "DELIVERY_EXPERIMENT_FAILED" :
                delivery_result == CancelDeliveryResult::ObservedIntegrityFailed ? "OBSERVED_INTEGRITY_FAILED" :
                delivery_result == CancelDeliveryResult::Complete ? "DELIVERY_COMPLETE" : "DELIVERY_INCOMPLETE";
            const bool late_integrity_failed = options.server && (late.invalid_records || late.duplicates || cross_window_duplicates);
            if (late_integrity_failed && !state.failed && receipt_valid) result="OBSERVED_INTEGRITY_FAILED";
            std::fprintf(stdout,"event=delivery_summary run_id=%s role=%s result=%s receipt_valid=%d receipt_callbacks=%u observed_record_integrity=%s window_us=3000000 window_anchor=SERVER_LOCAL_HOLD_RELEASE window_end_us=%lld hold_release_local_us=%lld window_records=%u missing=%u invalid=%u duplicates=%u window_sequence_bitmap=%016llx cancellation=NOT_EXERCISED selected=0 receipt_is_resource_settlement=0\n",
                options.run_id.c_str(),options.server?"server":"client",result,receipt_valid,receipt_callbacks.load(),
                !receipt_valid?"UNKNOWN":delivery.invalid_records || delivery.duplicates || late_integrity_failed?"FAIL":"EXACT_OBSERVED_RECORDS",
                static_cast<long long>(delivery_window_end_us),static_cast<long long>(options.server?hold_return_us.load():-1),
                delivery.unique_records,options.workload.max_records-delivery.unique_records,
                delivery.invalid_records,delivery.duplicates,static_cast<unsigned long long>(delivery.sequences[0]));
            schedule_result = "N/A_DELIVERY_EXPERIMENT";
            if (options.server) {
                std::fprintf(stdout,"event=delivery_late_manifest run_id=%s callbacks=%u unique=%u invalid=%u duplicates=%u cross_window_duplicates=%u sequence_bitmap=%016llx first_bad_ordinal=%lld first_bad_claimed_sequence=%lld first_bad_length=%zu included_in_receipt=0\n",
                    options.run_id.c_str(),late.callbacks,late.unique_records,late.invalid_records,late.duplicates,
                    cross_window_duplicates,static_cast<unsigned long long>(late.sequences[0]),
                    static_cast<long long>(late.first_bad_ordinal),static_cast<long long>(late.first_bad_claimed_sequence),late.first_bad_length);
            }
        }
        std::fprintf(stdout, "event=schedule_summary run_id=%s role=%s schedule=%s first_verified_bind_us=%lld peer_window_end_us=%lld peer_shutdown_us=%lld role_total_us=%lld clock_scope=LOCAL_ONLY\n",
                options.run_id.c_str(), options.server ? "server" : "client", schedule_result,
                static_cast<long long>(schedule.first_verified_bind_us), static_cast<long long>(schedule.peer_window_end_us),
                static_cast<long long>(schedule.peer_shutdown_us),
                static_cast<long long>(Delivery()?cancel_delivery_run_us:CancelRoleRunLimitUs(options.server, options.workload.server_admission_ms)));
        const auto shutdown_sdk_us = state.shutdown_call_enter_us >= 0 && state.shutdown_return_us >= state.shutdown_call_enter_us ?
                state.shutdown_return_us - state.shutdown_call_enter_us : -1;
        if (!options.server) {
            Invocation("first_invocation_summary", 0, state.first_send_enter_us, state.first_send_return_us, first_send_rc.load());
            Invocation("maximum_returned_invocation", state.longest_sequence,
                    state.longest_send_enter_us, state.longest_send_return_us, state.longest_send_rc);
            std::fprintf(stdout, "event=send_coverage run_id=%s pid=%ld completed_calls=%u attempted_bytes=%llu native_pressure=NOT_PROVEN admission=NO_ADAPTER_QUEUE sdk_return_is_remote_delivery=0\n",
                    options.run_id.c_str(), static_cast<long>(getpid()), state.completed_calls,
                    static_cast<unsigned long long>(state.completed_calls) * options.workload.record_bytes);
        }
        const auto records = integrity.Snapshot();
        std::fprintf(stdout, "event=integrity_manifest run_id=%s pid=%ld role=%s expected_length=%zu callbacks=%u valid_records=%u invalid_records=%u unique_records=%u duplicates=%u received_bytes=%llu first_bad_ordinal=%lld first_bad_claimed_sequence=%lld first_bad_length=%zu arrival_fnv1a64=%016llx validation=EXACT_LENGTH_AND_EVERY_BYTE bitmap_word_order=ASCENDING_SEQUENCE_LSB_FIRST sequence_bitmap=",
                options.run_id.c_str(), static_cast<long>(getpid()), options.server ? "server" : "client", options.workload.record_bytes,
                records.callbacks, records.valid_records, records.invalid_records, records.unique_records, records.duplicates,
                static_cast<unsigned long long>(records.received_bytes), static_cast<long long>(records.first_bad_ordinal),
                static_cast<long long>(records.first_bad_claimed_sequence), records.first_bad_length,
                static_cast<unsigned long long>(records.content_hash));
        for (std::size_t i = 0; i < (options.workload.max_records + 63U) / 64U; ++i)
            std::fprintf(stdout, "%s%016llx", i == 0 ? "" : ":", static_cast<unsigned long long>(records.sequences[i]));
        std::fputc('\n', stdout);
        std::fprintf(stdout, "event=resource_observation run_id=%s pid=%ld steady_us=%lld tasks_before_provider=%d tasks_after_cleanup=%d fds_before_provider=%d fds_after_cleanup=%d callback_peak=%u selected_shutdown_observed=%d callbacks_at_shutdown_return=%u callbacks_at_cleanup=%u callbacks_now=%u first_shutdown_callback_us=%lld workers_joined_us=%lld cleanup_observed_us=%lld sdk_callback_drain=NOT_PROVEN sdk_resource_settlement=NOT_PROVEN provider_lifetime=PROCESS\n",
                options.run_id.c_str(), static_cast<long>(getpid()), static_cast<long long>(Now()),
                tasks_before_provider, tasks_after_cleanup, fds_before_provider, fds_after_cleanup,
                peak_callbacks.load(), state.shutdown_return_us >= 0, callbacks_at_shutdown_return.load(), callbacks_at_cleanup, active_callbacks.load(),
                static_cast<long long>(peer_closed_us.load()), static_cast<long long>(state.workers_joined_us),
                static_cast<long long>(cleanup_observed_us));
        std::fprintf(stdout, "result=%s run_id=%s role=%s profile=%s selected=%d sequence=%u send_enter_us=%lld send_return_us=%lld shutdown_request_us=%lld shutdown_call_enter_us=%lld shutdown_return_us=%lld shutdown_sdk_us=%lld shutdown_overlap_lost=%d sender_tid=%llu shutdown_tid=%llu send_rc=%d sends=%u received=%u workers_joined=%d workers_joined_us=%lld workers_joinable=%d active_callbacks=%u hold_enter_us=%lld hold_return_us=%lld hold_tid=%llu hold_sequence=%u token_restored=%d untracked_id=%d cleanup=OBSERVED_KNOWN_IDS_ONLY call_boundary=EXTERNAL_MARKER_SCHEDULING_WINDOW native_kernel_blocked=NOT_PROVEN causality=NOT_PROVEN before_remote_hold_release=NOT_PROVEN production_quiescence=NOT_VERIFIED transport=NOT_IMPLEMENTED\n",
                result, options.run_id.c_str(), options.server ? "server" : "client", profile.name, state.selected, state.sequence,
                static_cast<long long>(state.send_enter_us), static_cast<long long>(state.send_return_us),
                static_cast<long long>(state.shutdown_request_us), static_cast<long long>(state.shutdown_call_enter_us),
                static_cast<long long>(state.shutdown_return_us), static_cast<long long>(shutdown_sdk_us), state.shutdown_overlap_lost,
                static_cast<unsigned long long>(state.sender_tid), static_cast<unsigned long long>(state.shutdown_tid), state.send_rc,
                sends.load(), received.load(), state.workers_joined, static_cast<long long>(state.workers_joined_us),
                sender.joinable() || controller.joinable(), active_callbacks.load(),
                static_cast<long long>(hold_enter_us.load()), static_cast<long long>(hold_return_us.load()),
                static_cast<unsigned long long>(hold_tid.load()), hold_sequence.load(), restored, untracked_id.load());
        exit_code = Delivery() ? (std::strcmp(result,"DELIVERY_COMPLETE")==0 ? 0 : 6) :
            std::strcmp(result, "FAIL") == 0 ? 6 : std::strcmp(result, "NOT_EXERCISED") == 0 ? 2 : 0;
        std::fflush(stdout);
    }
    Options options;
    const ProbeProfileDescription profile;
    CancelContract contract;
    CancelIntegrity integrity;
    CancelServerSchedule server_schedule;
    std::atomic<unsigned> active_callbacks{0};
    std::atomic<bool> callback_exception{false};
    int exit_code = 6;
private:
    static int CountDirectory(const char* path) noexcept {
        DIR* directory = opendir(path);
        if (!directory) return -1;
        int count = 0; bool complete = false;
        for (;;) {
            errno = 0; const auto* item = readdir(directory);
            if (!item) { complete = errno == 0; break; }
            if (std::strcmp(item->d_name, ".") != 0 && std::strcmp(item->d_name, "..") != 0) ++count;
        }
        if (closedir(directory) != 0) complete = false;
        return complete ? count : -1;
    }
    int NativeSend(int socket, std::vector<std::uint8_t>& bytes) {
        if (profile.send_method == ProbeSendMethod::Bytes)
            return send_bytes(socket, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        StreamData data{}; data.buf = reinterpret_cast<char*>(bytes.data()); data.bufLen = static_cast<int>(bytes.size());
        StreamData ext{}; StreamFrameInfo frame{};
        return send_stream(socket, &data, &ext, &frame);
    }
    void Send(int socket) noexcept {
        try {
            if (WorkerIdentity()) for (std::uint32_t sequence = 0; sequence < options.workload.max_records && !stop &&
                    Now() < cancel_run_us - cancel_return_budget_us; ++sequence) {
                // This one owned record survives through the synchronous
                // SDK return. No queue of borrowed buffers and no detached send.
                auto bytes = MakeCancelRecord(options.run_id, sequence, options.workload.record_bytes);
                if (sequence == 0) Log("first_send_preparing", socket, 0, sequence);
                // Constructing a large owned record can cross the loop guard's
                // admission cutoff. Re-read the clock after that work, with no
                // logging or allocation before the actual call on the admitted
                // path. Already admitted SDK calls still must finish and join.
                const auto tid = Tid();
                const bool stopped = stop.load();
                const auto admission_at = Now();
                const auto admission = TryBeginCancelSend(contract, sequence, tid, admission_at, stopped);
                if (admission != CancelSendAdmission::Admitted) {
                    if (admission == CancelSendAdmission::AdmissionClosed)
                        Log("admission_closed_before_sdk", socket, 0, sequence);
                    break;
                }
                const int rc = NativeSend(socket, bytes);
                const auto returned = Now();
                contract.FinishSend(sequence, rc, returned); ++sends;
                if (sequence == 0) first_send_rc = rc;
                const auto state = contract.Snapshot();
                if (state.selected) {
                    Invocation("selected_send_return", sequence, state.send_enter_us, returned, rc); break;
                }
                if (rc != 0) { Fail("send_failed_without_selected_cancellation"); break; }
            }
        } catch (...) { Fail("send_worker_exception"); }
        sender_done = true;
    }
    void Control(int socket) noexcept {
        try {
            if (WorkerIdentity()) while (!stop && !sender_done && Now() < cancel_run_us - cancel_return_budget_us) {
                if (contract.ClaimShutdown(Tid(), Now())) {
                    stop = true;
                    const auto selected = contract.Snapshot();
                    std::fprintf(stdout, "event=selected_request run_id=%s profile=%s pid=%ld invocation_id=%s:%ld:%u sequence=%u send_enter_us=%lld shutdown_request_us=%lld sender_tid=%llu shutdown_tid=%llu still_in_flight_at_selection=1 clock_scope=LOCAL_ONLY\n",
                            options.run_id.c_str(), profile.name, static_cast<long>(getpid()), options.run_id.c_str(), static_cast<long>(getpid()), selected.sequence, selected.sequence,
                            static_cast<long long>(selected.send_enter_us), static_cast<long long>(selected.shutdown_request_us),
                            static_cast<unsigned long long>(selected.sender_tid), static_cast<unsigned long long>(selected.shutdown_tid));
                    // Final check after request logging. Between this external
                    // marker and the SDK call: no printf, Snapshot or allocation.
                    // An unavoidable scheduler window remains; this is not a
                    // native kernel-blocking or causal cancellation assertion.
                    if (contract.EnterShutdownCall(Now())) {
                        // Only a call that will execute consumes cleanup ownership.
                        created_shutdown = true;
                        shutdown(socket);
                        contract.ShutdownReturned(Now());
                        callbacks_at_shutdown_return = active_callbacks.load();
                        const auto returned = contract.Snapshot();
                        std::fprintf(stdout, "event=selected_shutdown_interval run_id=%s pid=%ld invocation_id=%s:%ld:%u shutdown_request_us=%lld shutdown_call_enter_us=%lld shutdown_return_us=%lld clock_scope=LOCAL_ONLY\n",
                                options.run_id.c_str(), static_cast<long>(getpid()), options.run_id.c_str(), static_cast<long>(getpid()), selected.sequence,
                                static_cast<long long>(returned.shutdown_request_us), static_cast<long long>(returned.shutdown_call_enter_us),
                                static_cast<long long>(returned.shutdown_return_us));
                        Log("selected_shutdown_return", socket, 0, selected.sequence);
                    } else {
                        // Ordinary Close still owns this ID and calls Shutdown
                        // once after joining the workers; it is not cancellation.
                        Log("selected_shutdown_skipped_overlap_lost", socket, 0, selected.sequence);
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (...) { Fail("control_worker_exception"); }
        controller_done = true;
    }
    Clock::time_point start = Clock::now();
    void* provider = nullptr;
    decltype(&Socket) create = nullptr;
    decltype(&Listen) listen = nullptr;
    decltype(&Bind) bind = nullptr;
    decltype(&SendBytes) send_bytes = nullptr;
    decltype(&SendStream) send_stream = nullptr;
    decltype(&Shutdown) shutdown = nullptr;
    ISocketListener listener{};
    std::thread sender, controller;
    std::atomic<int> created{0}, accepted{0};
    std::atomic<int> untracked_id{0};
    std::atomic<bool> created_shutdown{false}, accepted_shutdown{false};
    std::atomic<bool> failed{false}, failure_logged{false}, stop{false}, peer_ready{false};
    std::atomic<bool> sender_done{false}, controller_done{false}, hold_claimed{false}, hold_released{false};
    std::atomic<unsigned> sends{0}, received{0}, shutdown_events{0}, error_events{0};
    std::atomic<unsigned> peak_callbacks{0}, callbacks_at_shutdown_return{0};
    std::atomic<int> first_send_rc{0};
    std::atomic<std::int64_t> peer_closed_us{-1};
    unsigned callbacks_at_cleanup = 0;
    int tasks_before_provider = -1, tasks_after_cleanup = -1, fds_before_provider = -1, fds_after_cleanup = -1;
    std::int64_t cleanup_observed_us = -1;
    std::int64_t settle_enter_us = -1, settle_return_us = -1;
    std::atomic<std::int64_t> hold_enter_us{-1}, hold_return_us{-1};
    std::atomic<std::uint64_t> hold_tid{0};
    std::atomic<std::uint32_t> hold_sequence{0};
    std::atomic<unsigned> receipt_callbacks{0};
    std::atomic<bool> receipt_received{false},receipt_sent{false};
    std::mutex delivery_gate;
    CancelIntegrity delivery_late_integrity;
    bool delivery_window_closed=false;
    unsigned delivery_cross_window_duplicates=0;
    CancelIntegrityObservation delivery_snapshot;
    std::int64_t delivery_window_end_us=-1;
};

template<class Action> void Callback(Action action) noexcept {
    auto* value = runtime.load(); if (!value) return;
    value->CallbackEntered();
    try { action(*value); } catch (...) { value->callback_exception = true; value->Fail("callback_exception"); }
    --value->active_callbacks;
}
void Bound(int socket, PeerSocketInfo peer) noexcept { Callback([&](CancelRuntime& value) { value.OnBound(socket, peer); }); }
void Bytes(int socket, const void* data, std::uint32_t size) noexcept {
    Callback([&](CancelRuntime& value) { value.OnData(socket, data, size, ProbeSendMethod::Bytes); });
}
void Stream(int socket, const StreamData* data, const StreamData*, const StreamFrameInfo*) noexcept {
    Callback([&](CancelRuntime& value) {
        if (!ValidateProbeStreamFrame(data != nullptr, data ? data->buf : nullptr, data ? data->bufLen : 0, cancel_candidate_raw_record_max)) {
            if (value.Delivery()) {
                // Malformed callbacks use the same window/late ownership gate;
                // they must not alter an already frozen receipt's accounting.
                value.OnData(socket,nullptr,data && data->bufLen>0?static_cast<std::size_t>(data->bufLen):0,ProbeSendMethod::Stream);
                value.Log("invalid_stream_frame_length",socket,data?data->bufLen:-1);
                return;
            }
            std::uint32_t ignored = 0;
            // Preserve malformed callback length evidence without dereferencing
            // a buffer rejected by the public StreamData structural check.
            value.integrity.Observe(nullptr, data && data->bufLen > 0 ? static_cast<std::size_t>(data->bufLen) : 0,
                    value.options.run_id, value.options.workload, ignored);
            value.Log("invalid_stream_frame_length", socket, data ? data->bufLen : -1);
            value.Fail("invalid_stream_frame"); return;
        }
        value.OnData(socket, data->buf, static_cast<std::size_t>(data->bufLen), ProbeSendMethod::Stream);
    });
}
void Closed(int socket, ShutdownReason reason) noexcept { Callback([&](CancelRuntime& value) { value.OnClosed(socket, reason); }); }
void Error(int socket, std::int32_t code) noexcept { Callback([&](CancelRuntime& value) { value.OnError(socket, code); }); }
void Message(int, const void*, std::uint32_t) noexcept { Callback([](CancelRuntime& value) { value.Fail("unexpected_message"); }); }

class CancelTokenOperations final : public TokenEntryOperations {
public:
    explicit CancelTokenOperations(Options value) : options(std::move(value)) {}
    TaskCheckpoint ObserveTasks() override {
        TaskCheckpoint result; result.process_id = static_cast<std::uint64_t>(getpid()); result.current_tid = Tid();
        DIR* directory = opendir("/proc/self/task"); result.opened = directory != nullptr;
        if (directory) {
            bool valid = true;
            for (;;) {
                errno = 0; const auto* item = readdir(directory);
                if (!item) { result.complete = valid && errno == 0; break; }
                if (std::strcmp(item->d_name, ".") == 0 || std::strcmp(item->d_name, "..") == 0) continue;
                std::uint64_t tid = 0; const char* end = item->d_name + std::strlen(item->d_name);
                const auto parsed = std::from_chars(item->d_name, end, tid);
                if (parsed.ec != std::errc{} || parsed.ptr != end || tid == 0) valid = false;
                if (result.count == 0) result.sole_tid = tid;
                if (result.count < 2) ++result.count;
            }
            if (closedir(directory) != 0) result.complete = false;
        }
        std::printf("stage=local_thread_checkpoint count=%zu single_main=%d\n", result.count, IsSingleMainTask(result));
        return result;
    }
    std::uint64_t GetSelf() override { return GetSelfTokenID(); }
    int SetSelf(std::uint64_t token) override { return SetSelfTokenID(token); }
    OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override {
        using namespace OHOS::Security::AccessToken;
        const auto id = static_cast<AccessTokenID>(token);
        const auto lookup = AccessTokenKit::GetNativeTokenId(process_name);
        const auto type = AccessTokenKit::GetTokenTypeFlag(id);
        NativeTokenInfo info{}; const int rc = AccessTokenKit::GetNativeTokenInfo(id, info);
        std::vector<std::int32_t> states;
        const int query = AccessTokenKit::VerifyAccessToken(id, std::vector<std::string>{permission_name}, states, true);
        std::printf("stage=identity phase=%s lookup_match=%d type=%d info_rc=%d query_rc=%d state_count=%zu\n",
                phase, lookup == id, static_cast<int>(type), rc, query, states.size());
        return {lookup, static_cast<int>(type), static_cast<int>(TOKEN_NATIVE), rc, std::move(info.processName),
            static_cast<int>(info.apl), static_cast<int>(APL_NORMAL), query, std::move(states), static_cast<int>(PERMISSION_GRANTED)};
    }
    void Diagnostic(const char* stage, std::uint64_t first, std::uint64_t second, int code) override {
        std::printf("stage=%s first=%llu second=%llu code=%d scope=CURRENT_MAIN_THREAD_ONLY\n", stage,
                static_cast<unsigned long long>(first), static_cast<unsigned long long>(second), code);
    }
    bool EnterProvider() override {
        value = new CancelRuntime(options); runtime.store(value); return value->Run();
    }
    bool CloseProvider() override { return value ? value->Close() : true; }
    Options options;
    CancelRuntime* value = nullptr;
};
int Run(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Options options;
    if (!Parse(argc, argv, options)) {
        std::fprintf(stderr, "Usage: softbus_cancel_probe --server|--client --profile bytes|raw|raw-encrypted --run-id ID "
            "--local-name com.kaihong.mdds.NAME --expected-peer-network-id HEX64 --expected-token OWNED_ID --preflight-confirmed "
            "[--expected-remote-initiator-name NAME | --remote-listen-name NAME --peer-network-id HEX64] "
            "[--hold-ms 0..4500|6000] [--record-bytes 80..60000(BYTES)|2097153(RAW)] [--max-records 1..4096] "
            "[--settle-ms 0..1000] [--server-admission-ms 1000|3000] [--delivery-window-ms 3000]\n");
        return 64;
    }
    if (options.workload.delivery_window_ms != 0)
        std::printf("scope=NATIVE_SOCKET_DELIVERY_DIAGNOSTIC run_id=%s profile=%s role=%s hold_ms=4500 delivery_window_ms=3000 fixture_total_us=14000000 external_watchdog_us=17000000 data_admission_us=6000000 cancellation=NOT_EXERCISED receipt=DIAGNOSTIC_ONLY bytes_per_record=%zu max_records=64 max_traffic_bytes=%llu permission_writes=NONE\n",
            options.run_id.c_str(),DescribeProbeProfile(options.profile)->name,options.server?"server":"client",
            options.workload.record_bytes,static_cast<unsigned long long>(options.workload.record_bytes)*options.workload.max_records);
    else std::printf("scope=NATIVE_SOCKET_CANCEL_DIAGNOSTIC run_id=%s profile=%s role=%s hold_us=%llu settle_ms=%u blocked_us=200000 cancellation_us=1000000 run_us=%lld client_run_us=7000000 client_active_us=6000000 server_run_us=%lld server_admission_us=%llu server_peer_window_us=7000000 external_watchdog_us=%lld required_server_watchdog_us=%lld required_client_watchdog_us=10000000 fixture_schedule_is_native_timeout=0 max_records=%u max_traffic_bytes=%llu bytes_per_record=%zu fixed_traffic_budget=245760000 candidate_capacity=NOT_QUALIFIED permission_writes=NONE preflight=OPERATOR_ASSERTED native_concurrent_shutdown_safety=NOT_VERIFIED\n",
            options.run_id.c_str(), DescribeProbeProfile(options.profile)->name, options.server ? "server" : "client",
            static_cast<unsigned long long>(options.workload.hold_ms) * 1000, options.workload.settle_ms,
            static_cast<long long>(CancelRoleRunLimitUs(options.server, options.workload.server_admission_ms)),
            static_cast<long long>(CancelRoleRunLimitUs(true, options.workload.server_admission_ms)),
            static_cast<unsigned long long>(options.workload.server_admission_ms) * 1000,
            static_cast<long long>(CancelRequiredWatchdogUs(options.server, options.workload.server_admission_ms)),
            static_cast<long long>(CancelRequiredWatchdogUs(true, options.workload.server_admission_ms)), options.workload.max_records,
            static_cast<unsigned long long>(options.workload.max_records) * options.workload.record_bytes, options.workload.record_bytes);
    CancelTokenOperations operations(std::move(options)); TokenFirstEntry entry(operations);
    const bool success = entry.Run({TokenEntryMode::Record, true, true, operations.options.token});
    if (!operations.value) { std::printf("result=FAIL provider_entered=0 token_restored=%d\n", entry.RestoreSucceeded()); return 6; }
    operations.value->PrintSummary(entry.RestoreSucceeded(), success);
    return operations.value->exit_code;
}
} // namespace
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
    catch (...) { std::fprintf(stderr, "result=FAIL reason=uncaught_probe_exception\n"); return 6; }
}
