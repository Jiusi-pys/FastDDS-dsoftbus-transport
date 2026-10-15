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
    // Used only by the separate connection-install ledger; IPv4 in host order.
    const char* role="NONE";
    std::uint32_t local_address=0,peer_address=0;
    unsigned local_port=0,peer_port=0;
    int local_result=-1,peer_result=-1,local_error=0,peer_error=0;
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
inline constexpr std::size_t connection_capacity=64;
inline Ledger<connection_capacity> connections;
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
// Called while the newly installed handle is protected by the Host gate, before
// its receiver starts. Queries neither send traffic nor change socket options.
inline void Install(int id,std::uint64_t epoch,SOCKET handle,const char* role) noexcept {
    Record(Connection("CONNECTION_INSTALL",id,epoch,handle));
    if(!enabled)return;
    const int saved=WSAGetLastError();
    auto e=Connection("ENDPOINTS",id,epoch,handle);e.role=role;
    sockaddr_in local{},peer{};int local_size=sizeof(local),peer_size=sizeof(peer);
    e.local_result=getsockname(handle,reinterpret_cast<sockaddr*>(&local),&local_size);
    e.local_error=e.local_result==SOCKET_ERROR?WSAGetLastError():0;
    if(e.local_result==0&&local_size==sizeof(local)&&local.sin_family==AF_INET){e.local_address=ntohl(local.sin_addr.s_addr);e.local_port=ntohs(local.sin_port);}
    else if(e.local_result==0)e.local_result=-2; // unsupported address, never infer an identity
    if(role[0]!='L'){
        e.peer_result=getpeername(handle,reinterpret_cast<sockaddr*>(&peer),&peer_size);
        e.peer_error=e.peer_result==SOCKET_ERROR?WSAGetLastError():0;
        if(e.peer_result==0&&peer_size==sizeof(peer)&&peer.sin_family==AF_INET){e.peer_address=ntohl(peer.sin_addr.s_addr);e.peer_port=ntohs(peer.sin_port);}
        else if(e.peer_result==0)e.peer_result=-2;
    }
    e.sequence=Point();e.thread=GetCurrentThreadId();connections.Append(e);WSASetLastError(saved);
}
inline bool ExportConnections(const char* run,unsigned side) noexcept {
    const auto reserved=connections.Reserved();std::uint64_t published=0;bool ok=true;
    for(std::size_t index=0;index<connection_capacity&&index<reserved;++index){Event e;if(!connections.Read(index,e))continue;++published;
        if(std::fprintf(stdout,"HOST_CONNECTION_EVENT schema=1 run=%s side=%c pid=%lu seq=%llu socket=%d epoch=%llu handle=%llu role=%s local_address=%u local_port=%u peer_address=%u peer_port=%u local_result=%d local_error=%d peer_result=%d peer_error=%d\n",
            run,side?'B':'A',GetCurrentProcessId(),e.sequence,e.socket,e.epoch,e.handle,e.role,e.local_address,e.local_port,e.peer_address,e.peer_port,e.local_result,e.local_error,e.peer_result,e.peer_error)<0)ok=false;
    }
    const auto stored=reserved<connection_capacity?reserved:connection_capacity;
    const bool complete=reserved<=connection_capacity&&published==reserved&&ok;
    if(std::fprintf(stdout,"HOST_CONNECTION_CAPTURE schema=1 run=%s side=%c pid=%lu capacity=%zu reserved=%llu published=%llu overflow=%llu unpublished=%llu complete=%d clock=PROCESS_LOCAL tuple_reuse=UNKNOWN\n",
        run,side?'B':'A',GetCurrentProcessId(),connection_capacity,reserved,published,reserved-stored,stored-published,int(complete))<0)ok=false;
    return complete&&ok;
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
    const bool connections_ok=ExportConnections(run,side);
    return std::fflush(stdout)==0&&std::ferror(stdout)==0&&complete&&output_ok&&connections_ok;
}
}
