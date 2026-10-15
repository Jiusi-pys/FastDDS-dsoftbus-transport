#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
// Windows Host seam only. No Runtime, DDS or SDK dependency. Observation order
// is process-local; intervals bracket calls, not kernel linearization points.
#include <winsock2.h>
#include <windows.h>
#undef SendMessage
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace fastdds_dsoftbus::ros2_host::io_observation {
template<class Map> bool ConfigurationEnabled(const Map& values) {
    const auto entry=values.find("host_io_observation");
    if(entry==values.end()||entry->second=="0")return false;
    if(entry->second=="1")return true;
    throw std::runtime_error("host_io_observation requires 0 or 1");
}
struct Event {
    const char* kind="UNKNOWN";
    const char* phase="NONE";
    const char* termination="NONE";
    std::uint64_t sequence=0,begin=0,last_call_begin=0,send=0,epoch=0,handle=0;
    unsigned long thread=0;
    int socket=-1,requested=0,result=0,error=0,calls=0,partial=0;
    bool result_known=false,error_known=false;
};
template<std::size_t Capacity> class Ledger {
    struct Slot { Event event{}; std::atomic<bool> ready{false}; };
    std::array<Slot,Capacity> slots_{};
    std::atomic<std::uint64_t> reserved_{0};
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,"Host observation requires lock-free reservation");
    static_assert(std::atomic<bool>::is_always_lock_free,"Host observation requires lock-free publication");
    void Append(Event value) noexcept {
        const auto index=reserved_.fetch_add(1,std::memory_order_relaxed);
        if(index>=Capacity)return;
        slots_[index].event=value;
        slots_[index].ready.store(true,std::memory_order_release);
    }
    std::uint64_t Reserved() const noexcept{return reserved_.load(std::memory_order_acquire);}
    bool Read(std::size_t index,Event& out) const noexcept {
        if(index>=Capacity||!slots_[index].ready.load(std::memory_order_acquire))return false;
        out=slots_[index].event;return true;
    }
};
inline constexpr std::size_t capacity=16384;
inline Ledger<capacity> ledger;
inline bool enabled=false; // configured once before starting Host threads
inline std::atomic<std::uint64_t> sequence{0},send_ids{0};
inline std::uint64_t Point() noexcept{return enabled?sequence.fetch_add(1,std::memory_order_relaxed)+1:0;}
inline void Record(Event value) noexcept {
    if(!enabled)return;
    // Preserve the caller's error state, including when this follows a syscall.
    const int saved=WSAGetLastError();
    value.sequence=Point();value.thread=GetCurrentThreadId();
    ledger.Append(value);WSASetLastError(saved);
}
inline Event Connection(const char* kind,int id,std::uint64_t epoch,SOCKET handle) noexcept {
    Event event;event.kind=kind;event.socket=id;event.epoch=epoch;event.handle=static_cast<std::uint64_t>(handle);return event;
}
// One record per completed prefix/body transfer, not per successful syscall.
// A failure retains the exact failed request and immediate error. Successful
// partial calls are counted; their individual kernel order is intentionally absent.
template<class Call,class Error> bool Transfer(char* data,int size,bool write,Event event,Call call,Error last_error) {
    event.begin=Point();const int original=size;int total=0;
    while(size>0){
        event.last_call_begin=Point();
        const int result=call(data,size);
        const int error=result==SOCKET_ERROR?last_error():0; // first operation after failure
        ++event.calls;event.requested=size;event.result=result;event.result_known=true;
        if(result<=0){event.error_known=result==SOCKET_ERROR;event.error=error;
            event.termination=result==SOCKET_ERROR?"SOCKET_ERROR":(write?"ZERO_PROGRESS":"EOF");Record(event);return false;}
        if(result<size)++event.partial;
        total+=result;data+=result;size-=result;
    }
    event.requested=original;event.result=total;event.result_known=true;Record(event);return true;
}
// Only after the sender and accept/read threads have joined. No hot-path I/O, allocation, mutex or lookup.
inline bool Export(const char* run,unsigned side) noexcept {
    if(!enabled)return true;
    const auto reserved=ledger.Reserved();std::uint64_t published=0;bool output_ok=true;
    for(std::size_t index=0;index<capacity&&index<reserved;++index){Event e;if(!ledger.Read(index,e))continue;++published;
        if(std::fprintf(stdout,"HOST_IO_EVENT schema=1 run=%s side=%c pid=%lu seq=%llu begin=%llu last_call_begin=%llu thread=%lu kind=%s phase=%s termination=%s send=%llu socket=%d epoch=%llu handle=%llu requested=%d result_known=%d result=%d wsa_known=%d wsa=%d calls=%d partial=%d\n",
            run,side?'B':'A',GetCurrentProcessId(),e.sequence,e.begin,e.last_call_begin,e.thread,e.kind,e.phase,e.termination,e.send,e.socket,e.epoch,e.handle,e.requested,int(e.result_known),e.result,int(e.error_known),e.error,e.calls,e.partial)<0)output_ok=false;
    }
    const auto stored=reserved<capacity?reserved:capacity;
    const bool complete=reserved<=capacity&&published==reserved&&output_ok;
    if(std::fprintf(stdout,"HOST_IO_CAPTURE schema=1 run=%s side=%c capacity=%zu reserved=%llu published=%llu overflow=%llu unpublished=%llu complete=%d ordering=PROCESS_OBSERVATION_ONLY runtime_owner=NOT_OBSERVED successful_syscalls=AGGREGATED sdk_epoch=NOT_PROVEN\n",run,side?'B':'A',capacity,reserved,published,reserved-stored,stored-published,int(complete))<0)output_ok=false;
    return std::fflush(stdout)==0&&std::ferror(stdout)==0&&complete&&output_ok;
}
}
