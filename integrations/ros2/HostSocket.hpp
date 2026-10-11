#pragma once
#include <string>
#include <cstdint>
namespace fastdds_dsoftbus::ros2_host {
struct LinkConfiguration {
    unsigned side=0;
    std::uint16_t port=0, peer_port=0;
    std::string run,package,local_listener,peer_listener,peer_outgoing;
    bool forward=true;
    bool bounded_reaccept=false;
    std::string reaccept_control;
    bool route_lifecycle=false;
    std::string peer_udid;
    // Explicit Host negative fixture only; empty keeps opaque forwarding.
    std::string fragment_control;
};
void ConfigureLink(LinkConfiguration);
bool WaitPeerListener(unsigned milliseconds);
void StopLink();
struct LinkObservation {
    std::uint64_t sends,ingress,send_errors;
    bool threads_joined=false;
    std::uint64_t fragment_drops=0;
};
LinkObservation ObserveLink();
// Test scheduling only. End the invocation's original Host outgoing connection,
// then await its actual read-thread terminal notification outside provider gates.
bool EndOriginalSendForHostTest(std::int32_t);
// Host fixture scheduling/connection-ending only; never rebuilds a Runtime.
bool ReacceptControlStep();
void ReacceptControlReceipt(const char* action);
void NodeControlStep();
}
