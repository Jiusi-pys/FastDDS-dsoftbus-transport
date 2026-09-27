#pragma once
#ifndef MDDS_PUBLIC_SOCKET_DDS_EXPERIMENT
#error "Exclusive DDS experiment variant required"
#endif
#include <mdds/dsoftbus/HostProviderLoop.hpp>
#include <mdds/dsoftbus/PublicSocketBackend.hpp>
#include <functional>
#include <string>
namespace eprosima::fastdds::dds {class DataWriter;}
namespace mdds::dsoftbus::dds_experiment {
struct Config {
    std::string run, peer_network;
    std::string service_prefix="com.kaihong.mdds.p";
    unsigned side=0, delete_first=1;
};
struct Result {
    bool success=false,drained=false;
    unsigned factories=0,writes=0,takes=0;
    std::string first_failure,participant,writer,reader,remote_writer,remote_reader;
};
using Control=std::function<bool(const char*)>;
bool IsolateProcess() noexcept; // Call once, after token selection, before factory threads.
bool Valid(const Config&) noexcept;
Result Run(const Config&,const Control&) noexcept;
void DumpFrames() noexcept;
std::size_t FrameOverflow() noexcept;
std::size_t FrameCount() noexcept;
bool ExpectedTerminal(const NativeSendRunResult&,const host_test::HostLoopObservation&,bool) noexcept;
#ifdef MDDS_DDS_EXPERIMENT_TESTING
void SetAfterWaitHook(std::function<void(eprosima::fastdds::dds::DataWriter*)>);
#endif
}
