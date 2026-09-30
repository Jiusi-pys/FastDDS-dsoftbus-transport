#pragma once
#include <string>
#include <cstdint>
namespace fastdds_dsoftbus::ros2_host {
struct LinkConfiguration {
    unsigned side=0;
    std::uint16_t port=0, peer_port=0;
    std::string run,package,local_listener,peer_listener,peer_outgoing;
    bool forward=true;
};
void ConfigureLink(LinkConfiguration);
bool WaitPeerListener(unsigned milliseconds);
void StopLink();
struct LinkObservation {std::uint64_t sends,ingress,send_errors;};
LinkObservation ObserveLink();
}
