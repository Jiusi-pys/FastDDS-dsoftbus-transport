#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fastdds_dsoftbus::experiment {
// Observation only: owns no application, native handle, lease or payload.
struct PerformanceErrorRecord {
    std::uint64_t invocation=0, instance=0, receiver=0, bytes=0;
    std::int64_t steady_ns=0;
    int socket=-1, value=0;
    char event[48]{};
};
struct PerformanceObservationSnapshot {
    std::uint64_t spans=0, suppressed=0, ingress=0, application_tx=0, application_rx=0;
    std::uint64_t send_calls=0, send_attempt_bytes=0, send_returns=0;
    std::uint64_t send_success_bytes=0, send_failed_bytes=0, send_failures=0;
    std::uint64_t owned_copy_messages=0, owned_copy_bytes=0;
    // Nonzero event values are not automatically SDK failures (Socket handles).
    std::uint64_t nonzero_values=0, error_details_stored=0, error_details_overflow=0;
    bool counter_overflow=false, quiet=false;
};
class PerformanceObservation final {
public:
    static constexpr std::size_t detail_capacity=128;
    void SetQuiet(bool) noexcept;
    bool Quiet() const noexcept;
    void Span(const char* event,std::uint64_t invocation,std::uint64_t instance,
            int socket,int value,bool has_value,std::size_t bytes,std::uint64_t receiver) noexcept;
    void Text(unsigned kind) noexcept; // 0=ingress, 1=application TX, 2=application RX
    PerformanceObservationSnapshot Snapshot() const noexcept;
    bool ErrorRecord(std::size_t,PerformanceErrorRecord&) const noexcept;
private:
    struct Slot { PerformanceErrorRecord value{}; std::atomic<bool> ready{false}; };
    std::atomic<bool> quiet_{false}, overflow_{false};
    std::array<std::atomic<std::uint64_t>,14> counters_{};
    std::array<Slot,detail_capacity> details_{};
    void Add(std::size_t,std::uint64_t) noexcept;
};
// Bound once before any native runtime acquisition. No reset/rebinding API.
// Fixed trivially-destructible process storage remains valid until process exit.
bool EnablePerformanceObservation() noexcept;
PerformanceObservation* CurrentPerformanceObservation() noexcept;
}
