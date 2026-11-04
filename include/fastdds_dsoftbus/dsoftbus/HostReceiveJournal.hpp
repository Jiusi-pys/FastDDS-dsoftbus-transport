#pragma once
// Explicit Host diagnostic build only; no SDK identities or payload ownership.
#ifndef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
#error HostReceiveJournal requires the explicit Host observation build
#endif
#include <fastdds_dsoftbus/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace fastdds_dsoftbus::dsoftbus::host_rx_observation {
struct Ingress {
    std::uint64_t ordinal=0;
    std::int32_t socket=-1;
    std::uint64_t epoch=0,handle=0;
    std::size_t size=0;
};
inline thread_local Ingress ingress;
inline Ingress CurrentIngress() noexcept {return ingress;}
class ScopedIngress final {
    Ingress previous_;
public:
    explicit ScopedIngress(Ingress value) noexcept:previous_(ingress){ingress=value;}
    ~ScopedIngress(){ingress=previous_;}
    ScopedIngress(const ScopedIngress&)=delete;
    ScopedIngress& operator=(const ScopedIngress&)=delete;
};
enum class Kind {Charge,CapacityRefusal,Queue,Take,Enter,Requeue,RefundRecord,RefundCapture,Stage,Decision};
struct Event {
    Kind kind=Kind::Stage;
    std::uint64_t scope=0,record=0,gate=0,correlation=0;
    std::uint64_t application=0,receiver=0,logical_token=0;
    std::size_t local=0,size=0,records=0,bytes=0,copying=0,queued=0,taken=0,delivering=0;
    std::size_t local_records=0,local_bytes=0,records_limit=0,bytes_limit=0,local_records_limit=0,local_bytes_limit=0;
    std::size_t copying_limit=0,application_copying=0,application_copying_limit=0;
    std::uint32_t capacity_mask=0;
    int capacity_reason=0;
    bool application_copying_known=false;
    int phase=-1,stage=-1;
    Ingress ingress;
    ReceiveDecision decision;
};
static_assert(std::is_trivially_copyable<Event>::value,"journal must retain scalars only");
template<std::size_t Capacity> class Ledger final {
    struct Slot {Event value{};std::atomic<bool> ready{false};};
    std::array<Slot,Capacity> slots_{};
    std::atomic<std::uint64_t> reserved_{0};
    std::atomic<bool> invalid_{false};
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"bounded Host reservation requires lock-free atomics");
    void Invalidate() noexcept {invalid_.store(true,std::memory_order_relaxed);}
    void Append(const Event& value) noexcept {
        auto index=reserved_.load(std::memory_order_relaxed);
        for(;;) {
            if(index==(std::numeric_limits<std::uint64_t>::max)()){Invalidate();return;}
            if(reserved_.compare_exchange_weak(index,index+1,std::memory_order_relaxed))break;
        }
        if(index>=Capacity){Invalidate();return;}
        slots_[index].value=value;
        slots_[index].ready.store(true,std::memory_order_release);
    }
    std::uint64_t Reserved() const noexcept {return reserved_.load(std::memory_order_acquire);}
    bool Read(std::size_t index,Event& output) const noexcept {
        if(index>=Capacity||!slots_[index].ready.load(std::memory_order_acquire))return false;
        output=slots_[index].value;return true;
    }
    // Caller must first join every producer. This is not a live snapshot API.
    bool Complete() const noexcept {
        const auto n=Reserved();if(invalid_.load()||n>Capacity)return false;
        for(std::size_t i=0;i<n;++i)if(!slots_[i].ready.load(std::memory_order_acquire))return false;
        return Reserved()==n;
    }
};
inline constexpr std::size_t capacity=65536;
inline Ledger<capacity> journal;
inline std::atomic<int> configuration{-1};
inline std::atomic<std::uint64_t> scopes{0};
// Process lifetime setting, fixed before the first Runtime. Mixing enabled and
// disabled runtimes is rejected as diagnostic setup, never changed on callbacks.
inline bool Configure(bool enabled) noexcept {
    int expected=-1;
    if(configuration.compare_exchange_strong(expected,enabled?1:0))return true;
    return expected==(enabled?1:0);
}
inline bool Enabled() noexcept {return configuration.load(std::memory_order_relaxed)==1;}
inline std::uint64_t NewScope() noexcept {
    if(!Enabled())return 0;
    auto value=scopes.load(std::memory_order_relaxed);
    for(;;) {
        if(value==(std::numeric_limits<std::uint64_t>::max)()){journal.Invalidate();return 0;}
        if(scopes.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value+1;
    }
}
inline std::uint64_t Next(std::uint64_t& value) noexcept {
    if(value==(std::numeric_limits<std::uint64_t>::max)()){journal.Invalidate();return 0;}
    return ++value;
}
inline void Append(const Event& event) noexcept {if(Enabled())journal.Append(event);}
}
