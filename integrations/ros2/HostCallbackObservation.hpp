#pragma once
#include <fastdds_dsoftbus/dsoftbus/HostReceiveJournal.hpp>
#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>
#include <array>
#include <atomic>
#include <cstdio>

// Independent of the receive observer: one immutable receipt around each actual
// Host OnBytes invocation. No payload, allocation, OS call, or owning handle.
namespace fastdds_dsoftbus::ros2_host_callback {
namespace rx=fastdds_dsoftbus::dsoftbus::host_rx_observation;
constexpr std::size_t capacity=8192;
struct Slot { rx::Ingress ingress{}; std::atomic<bool> entered{false},returned{false}; };
inline std::array<Slot,capacity> slots{};
inline std::atomic<std::size_t> reserved{0};
inline std::atomic<bool> overflow{false};
class HostCallbackObservation final {
    Slot* slot_=nullptr;
public:
    explicit HostCallbackObservation(rx::Ingress value) noexcept {
        if(!rx::Enabled())return;
        auto index=reserved.load(std::memory_order_relaxed);
        while(index<capacity&&!reserved.compare_exchange_weak(index,index+1,std::memory_order_relaxed)){}
        if(index>=capacity){overflow.store(true,std::memory_order_relaxed);return;}
        slot_=&slots[index];slot_->ingress=value;slot_->entered.store(true,std::memory_order_release);
    }
    ~HostCallbackObservation(){if(slot_)slot_->returned.store(true,std::memory_order_release);}
    HostCallbackObservation(const HostCallbackObservation&)=delete;
    HostCallbackObservation& operator=(const HostCallbackObservation&)=delete;
};
// Postjoin only. These counters come from existing Adapter/Binding observations,
// not from observer rows. Lost/retired bindings therefore fail reconciliation.
inline bool ExportCoverage(const char* run,unsigned side,unsigned long pid,std::uint64_t ingress,
        const fastdds_dsoftbus::dsoftbus::phase0::AdapterObservation& a) noexcept {
    if(!rx::Enabled())return true;
    const auto count=reserved.load();bool good=!overflow.load()&&count==ingress;
    for(std::size_t i=0;i<count;++i){const auto& s=slots[i];
        const bool entered=s.entered.load(std::memory_order_acquire),returned=s.returned.load(std::memory_order_acquire);
        good=good&&entered&&returned;const auto& v=s.ingress;
        if(std::fprintf(stdout,"HOST_CALLBACK_EVENT schema=1 run=%s side=%c pid=%lu index=%zu ingress=%llu socket=%d epoch=%llu handle=%llu size=%zu entered=%d returned=%d\n",run,side?'B':'A',pid,i,v.ordinal,v.socket,v.epoch,v.handle,v.size,int(entered),int(returned))<0)good=false;
    }
    if(std::fprintf(stdout,"HOST_CALLBACK_CAPTURE schema=1 run=%s side=%c pid=%lu capacity=%zu reserved=%zu ingress=%llu complete=%d\n",run,side?'B':'A',pid,capacity,count,ingress,int(good))<0)good=false;
    const auto& b=a.binding;const auto& l=a.listener;const auto& c=a.accepted;
    if(std::fprintf(stdout,"HOST_RX_COVERAGE schema=1 run=%s side=%c pid=%lu profile=EXCLUSIVE_BINDING ingress=%llu adapter_rejected=%zu active=%zu seen=%llu admitted=%zu refused_phase=%zu refused_native=%zu refused_receive=%zu\n",run,side?'B':'A',pid,ingress,a.ingress_rejected,a.ingress_active,b.ingress_seen+l.ingress_seen+c.ingress_seen,b.admitted+l.admitted+c.admitted,b.refused_phase+l.refused_phase+c.refused_phase,b.refused_native+l.refused_native+c.refused_native,b.refused_receive+l.refused_receive+c.refused_receive)<0)good=false;
    return std::fflush(stdout)==0&&std::ferror(stdout)==0&&good;
}
}
