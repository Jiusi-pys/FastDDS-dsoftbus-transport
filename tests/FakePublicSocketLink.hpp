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
}
