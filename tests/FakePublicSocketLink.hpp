#pragma once
#include "HostParticipantEvidence.hpp"
#include <memory>
#include <functional>

namespace public_socket_fake {
struct QosObservation {unsigned count=0;std::array<int,8> type{},value{};};
struct Snapshot {
    unsigned sockets[2]{},listens[2]{},binds[2]{},sends[2]{},forwards[2]{},shutdowns[2]{},notified[2]{};
    unsigned capacity_waits=0,capacity_timeouts=0;
    unsigned rejected_candidates=0,invalid=0;
    std::size_t queued=0,bytes=0,record_peak=0,byte_peak=0,live=0;
    QosObservation listen_qos[2],bind_qos[2]; // Actual borrowed C parameters, bounded copy.
};
void Configure(std::shared_ptr<host_dds::Trace>,bool forwarding);
void SetListenerNames(const std::array<std::string,2>&);
// Inactive Fake rows only; preserves counters, old callback values and numeric
// IDs. This is fixture naming, not a production reconnect/drain guarantee.
void SetInitiatorNames(const std::array<std::string,2>&);
struct SavedBytesCallback {
    std::int32_t socket=-1;
    void (*function)(std::int32_t,const void*,std::uint32_t)=nullptr;
};
SavedBytesCallback SaveAcceptedBytes(int side);
void SetPacketObserver(std::function<void(int,const std::uint8_t*,std::size_t)>);
void UseBoundedBackpressure();
void SetSendObserver(std::function<void(int,int)>);
// Declared detail window. Lifecycle events, error records and every counter stay
// unconditional; only per-packet detail is gated. Required or sampled rows can
// still overflow the unchanged 8192 limit, which remains a test failure.
void SetFrameDetail(bool);
bool PumpOne();
Snapshot Observe();
bool RejectExtraCandidate(int side);
void LateBytes(int side);
bool UnsupportedNegotiation2(int side);
void DelayBindCallbacks();
void ReleaseBindCallbacks();
void EnableFreshReplacementIds();
void DisconnectDirection(int from,bool outgoing_first);
// Route loss model: every live outgoing/accepted connection receives the original
// OnShutdown with `reason` (e.g. SHUTDOWN_REASON_LINK_DOWN) and is then retired.
// Listeners stay live. Returns the number of connections ended.
unsigned LinkDown(int reason);
int CurrentSocket(int side,int role);
enum class InjectedOperation {Socket,Listen,Bind};
void FailNext(InjectedOperation,std::int32_t);
void SetShutdownBarrier(std::function<void()>);
// Host-only precise first-Bind scheduling; callable runs without the Fake gate.
void SetBindBarrier(std::function<void()>);
// Original listener shutdown callback, without inventing an accepted-connection end.
void StopListener(int side);
// Direct invocation of the original listener function value. Test-supplied
// peer fields are not synthesized from the production domain codec.
bool ProbeCandidate(int side,std::int32_t id,const std::string& name,const std::string& network,
        const std::string& package,int data_type);
}
