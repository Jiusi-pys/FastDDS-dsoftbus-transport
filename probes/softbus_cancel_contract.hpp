#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <vector>

namespace mdds_probe {
inline constexpr std::size_t cancel_record_bytes = 60000;
inline constexpr std::uint32_t cancel_record_limit = 4096;
inline constexpr std::int64_t cancel_blocked_us = 200000;
inline constexpr std::int64_t cancel_return_budget_us = 1000000;
inline constexpr std::int64_t cancel_hold_us = 2500000;
inline constexpr std::int64_t cancel_run_us = 7000000;
inline constexpr std::int64_t cancel_server_admission_us = 1000000;
inline constexpr std::int64_t cancel_server_run_us = 9000000;
inline constexpr std::size_t cancel_candidate_raw_record_max = 2U * 1024U * 1024U + 1U;
inline constexpr std::uint64_t cancel_traffic_limit = 245760000;
struct CancelWorkload {
    std::uint32_t hold_ms = 2500;
    std::size_t record_bytes = cancel_record_bytes;
    std::uint32_t max_records = cancel_record_limit;
    std::uint32_t settle_ms = 0;
    std::uint32_t server_admission_ms = 1000;
    std::uint32_t delivery_window_ms = 0; // Explicit diagnostic experiment, not cancellation.
    unsigned specified = 0;
};
// Strict, duplicate-rejecting numeric options. Validation runs before any token
// or Socket API entry. The RAW maximum is a diagnostic candidate, not a capacity
// qualification; SDK rejection at a tested boundary remains evidence.
bool SetCancelWorkloadOption(CancelWorkload&, std::string_view key, std::string_view value) noexcept;
bool ValidateCancelWorkload(const CancelWorkload&, bool bytes_profile) noexcept;

enum class CancelServerState { WaitingForPeer, PeerActive, PeerCompleted, ScheduleNotAdmitted, PeerWindowExpired };
struct CancelServerObservation {
    CancelServerState state = CancelServerState::WaitingForPeer;
    std::int64_t first_verified_bind_us = -1, peer_window_end_us = -1, peer_shutdown_us = -1;
};

// The peer's runtime begins before its Bind can cause this server's OnBind.
// Granting seven seconds from that causal event covers its original seven
// seconds without comparing two processes' monotonic clock values.
class CancelServerSchedule {
public:
    explicit CancelServerSchedule(std::uint32_t admission_ms = 1000);
    bool AdmitPeer(std::int64_t at);
    void ObservePeerShutdown(std::int64_t at);
    CancelServerObservation Snapshot(std::int64_t at) const;
private:
    mutable std::mutex mutex_;
    const std::int64_t admission_us_ = cancel_server_admission_us;
    std::int64_t first_verified_bind_us_ = -1, peer_shutdown_us_ = -1;
};
// These are fixture role budgets. They do not alter any SDK timeout or the
// candidate one-second cancellation requirement.
std::int64_t CancelRoleRunLimitUs(bool server, std::uint32_t server_admission_ms = 1000) noexcept;
std::int64_t CancelRequiredWatchdogUs(bool server, std::uint32_t server_admission_ms = 1000) noexcept;
bool CancelCleanupWithinDeadline(bool server, std::int64_t at, std::uint32_t server_admission_ms = 1000) noexcept;

struct CancelObservation {
    bool selected = false;
    bool workers_joined = false;
    bool failed = false;
    bool watchdog_terminated = false;
    bool shutdown_overlap_lost = false;
    std::uint32_t sequence = 0;
    std::uint64_t sender_tid = 0, shutdown_tid = 0;
    std::int64_t send_enter_us = -1, shutdown_request_us = -1;
    std::int64_t shutdown_call_enter_us = -1;
    std::int64_t send_return_us = -1, shutdown_return_us = -1;
    std::int64_t workers_joined_us = -1;
    int send_rc = 0;
    std::uint32_t completed_calls = 0, longest_sequence = 0;
    std::int64_t first_send_enter_us = -1, first_send_return_us = -1;
    std::int64_t longest_send_enter_us = -1, longest_send_return_us = -1, longest_send_us = -1;
    int longest_send_rc = 0;
};

enum class CancelSettleDecision { NotRequested, SkipContext, Wait, FailedSchedule };
// An observation window after actual joins, never a send timeout, cancellation
// extension, acknowledgment, or promise of receiver delivery.
CancelSettleDecision DecideCancelSettle(bool server, std::uint32_t settle_ms,
        const CancelObservation&, std::int64_t at) noexcept;

enum class CancelVerdict { NotExercised, TimelyReturnsObserved, Failed };
// Only same-process monotonic timestamps are compared. Even timely returns do
// not prove Shutdown caused the send to return, or remote callback quiescence.
CancelVerdict EvaluateCancelObservation(const CancelObservation& observation) noexcept;
const char* CancelVerdictName(CancelVerdict verdict) noexcept;

// Serializes selection with the sender's return publication. It observes a
// public SDK call interval, never claims the native kernel is blocked.
class CancelContract {
public:
    bool BeginSend(std::uint32_t sequence, std::uint64_t tid, std::int64_t at);
    bool FinishSend(std::uint32_t sequence, int rc, std::int64_t at);
    bool ClaimShutdown(std::uint64_t tid, std::int64_t at);
    // Final external boundary check, after all request logging and immediately
    // before the SDK call. No public API can make this atomic with native entry.
    bool EnterShutdownCall(std::int64_t at);
    bool ShutdownReturned(std::int64_t at);
    void Joined(std::int64_t at);
    void Fail();
    CancelObservation Snapshot() const;
private:
    mutable std::mutex mutex_;
    CancelObservation observation_;
    bool in_flight_ = false;
};

enum class CancelSendAdmission { Admitted, AdmissionClosed, Stopped, ContractRejected };
// Call only after input construction, immediately before the public call. The
// timestamp is an external admission marker, not an atomic native-entry fence.
CancelSendAdmission TryBeginCancelSend(CancelContract&, std::uint32_t sequence,
        std::uint64_t tid, std::int64_t at, bool stopped);

std::vector<std::uint8_t> MakeCancelRecord(std::string_view run_id, std::uint32_t sequence,
        std::size_t record_bytes = cancel_record_bytes);
bool ValidateCancelRecord(const void* data, std::size_t size, std::string_view run_id,
        std::uint32_t& sequence, std::size_t expected_bytes = cancel_record_bytes) noexcept;

struct CancelIntegrityObservation {
    std::uint32_t callbacks = 0, valid_records = 0, invalid_records = 0, duplicates = 0, unique_records = 0;
    std::uint64_t received_bytes = 0, content_hash = 14695981039346656037ULL;
    std::int64_t first_bad_ordinal = -1, first_bad_claimed_sequence = -1;
    std::size_t first_bad_length = 0;
    std::array<std::uint64_t, cancel_record_limit / 64> sequences{};
};
// Diagnostic records only. No reconstruction, delivery retry, or production
// framing. Each callback is checked as a complete message before it is counted.
class CancelIntegrity {
public:
    bool Observe(const void*, std::size_t, std::string_view, const CancelWorkload&, std::uint32_t& sequence);
    CancelIntegrityObservation Snapshot() const;
private:
    mutable std::mutex mutex_;
    CancelIntegrityObservation value_;
};
inline constexpr std::int64_t cancel_delivery_run_us = 14000000;
inline constexpr std::int64_t cancel_delivery_window_us = 3000000;
inline constexpr std::size_t cancel_delivery_receipt_bytes = 144;
enum class CancelDeliveryResult { Complete, DeliveryIncomplete, ObservedIntegrityFailed };
bool CancelDeliveryReceiptEligible(std::int64_t hold_released_us, std::int64_t now) noexcept;
bool CancelDeliveryWindowAdmitsRecord(bool closed, std::int64_t hold_released_us, std::int64_t ingress_us) noexcept;
CancelDeliveryResult EvaluateCancelDelivery(const CancelIntegrityObservation&, const CancelWorkload&) noexcept;
// A single diagnostic receipt, never a production frame or data retry. Its run,
// configuration and full count/bitmap payload must all match this experiment.
std::vector<std::uint8_t> MakeCancelDeliveryReceipt(std::string_view run_id, const CancelWorkload&,
        const CancelIntegrityObservation&);
bool ParseCancelDeliveryReceipt(const void*, std::size_t, std::string_view run_id, const CancelWorkload&,
        CancelIntegrityObservation&) noexcept;
} // namespace mdds_probe
