#include "PerformanceObservation.hpp"
#include "ExperimentalTransport.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <type_traits>
namespace fastdds_dsoftbus::experiment {
namespace {
PerformanceObservation process_observation;
std::atomic<bool> observation_enabled{false};
static_assert(std::is_trivially_destructible<PerformanceObservation>::value,
        "Observer storage has no exit-time destructor or native resource lifetime");
std::uint64_t AddChecked(std::atomic<std::uint64_t>& counter,std::uint64_t value,
        std::atomic<bool>& overflow) noexcept {
    auto old=counter.load(std::memory_order_relaxed);
    for (;;) {
        const bool excess=value>std::numeric_limits<std::uint64_t>::max()-old;
        const auto next=excess?std::numeric_limits<std::uint64_t>::max():old+value;
        if(counter.compare_exchange_weak(old,next,std::memory_order_relaxed)) {
            if(excess)overflow.store(true,std::memory_order_relaxed);
            return old;
        }
    }
}
}
void PerformanceObservation::Add(std::size_t i,std::uint64_t n) noexcept {AddChecked(counters_[i],n,overflow_);}
void PerformanceObservation::SetQuiet(bool quiet) noexcept {quiet_.store(quiet,std::memory_order_release);}
bool PerformanceObservation::Quiet() const noexcept {return quiet_.load(std::memory_order_acquire);}
void PerformanceObservation::Span(const char* event,std::uint64_t id,std::uint64_t instance,
        int socket,int value,bool has_value,std::size_t bytes,std::uint64_t receiver) noexcept {
    Add(0,1);if(Quiet())Add(1,1);
    if(std::strcmp(event,"SendBytes_enter")==0){Add(5,1);Add(6,bytes);}
    else if(std::strcmp(event,"SendBytes_return")==0){Add(7,1);if(value==0)Add(8,bytes);else{Add(9,bytes);Add(10,1);}}
    else if(std::strcmp(event,"OnBytes_owned_copy")==0){Add(11,1);Add(12,bytes);}
    // This deliberately preserves all nonzero values, including positive Socket
    // handles. Only the known SendBytes return above is labelled a send failure.
    if(has_value&&value!=0) {
        const auto ticket=AddChecked(counters_[13],1,overflow_);
        if(ticket<detail_capacity) {
            auto& slot=details_[static_cast<std::size_t>(ticket)];auto& out=slot.value;
            out.invocation=id;out.instance=instance;out.receiver=receiver;out.bytes=bytes;
            out.socket=socket;out.value=value;
            out.steady_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            std::size_t n=0;while(event[n]&&n+1<sizeof(out.event)){out.event[n]=event[n];++n;}
            out.event[n]=0;if(event[n])overflow_.store(true,std::memory_order_relaxed);
            slot.ready.store(true,std::memory_order_release);
        }
    }
}
void PerformanceObservation::Text(unsigned kind) noexcept {
    if(kind<3)Add(2+kind,1);else overflow_.store(true,std::memory_order_relaxed);
    if(Quiet())Add(1,1);
}
PerformanceObservationSnapshot PerformanceObservation::Snapshot() const noexcept {
    PerformanceObservationSnapshot s;
    s.spans=counters_[0].load();s.suppressed=counters_[1].load();
    s.ingress=counters_[2].load();s.application_tx=counters_[3].load();s.application_rx=counters_[4].load();
    s.send_calls=counters_[5].load();s.send_attempt_bytes=counters_[6].load();s.send_returns=counters_[7].load();
    s.send_success_bytes=counters_[8].load();s.send_failed_bytes=counters_[9].load();s.send_failures=counters_[10].load();
    s.owned_copy_messages=counters_[11].load();s.owned_copy_bytes=counters_[12].load();s.nonzero_values=counters_[13].load();
    for(const auto& slot:details_)if(slot.ready.load(std::memory_order_acquire))++s.error_details_stored;
    s.error_details_overflow=s.nonzero_values>detail_capacity?s.nonzero_values-detail_capacity:0;
    s.counter_overflow=overflow_.load();s.quiet=Quiet();return s;
}
bool PerformanceObservation::ErrorRecord(std::size_t n,PerformanceErrorRecord& result) const noexcept {
    if(n>=detail_capacity||!details_[n].ready.load(std::memory_order_acquire))return false;
    result=details_[n].value;return true;
}
bool EnablePerformanceObservation() noexcept {
    try {
        const auto s=GetProcessStats();
        if(s.active_states||s.exposed_entries||s.tombstone_entries)return false;
        bool expected=false;return observation_enabled.compare_exchange_strong(expected,true,std::memory_order_release);
    } catch(...) {return false;}
}
PerformanceObservation* CurrentPerformanceObservation() noexcept {
    return observation_enabled.load(std::memory_order_acquire)?&process_observation:nullptr;
}
}
