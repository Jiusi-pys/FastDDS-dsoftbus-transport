#pragma once
#ifndef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
#error "Exclusive DDS experiment variant required"
#endif
#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <functional>
#include <string>
#include <array>
namespace eprosima::fastdds::dds {class DataWriter;}
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
struct Config {
    std::string run, peer_network;
    std::string scenario="SMALL";
    std::string service_prefix="com.kaihong.mdds.p";
    unsigned side=0, delete_first=1, cycles=1;
};
struct Result {
    bool success=false,drained=false;
    unsigned factories=0,writes=0,takes=0;
    std::uint64_t absence_after_drop_us=0;
    std::string first_failure,participant,writer,reader,remote_writer,remote_reader;
    std::array<std::string,3> shared_writers,shared_readers,shared_remote_writers,shared_remote_readers;
};
using Control=std::function<bool(const char*)>;
bool IsolateProcess() noexcept; // Call once, after token selection, before factory threads.
bool Valid(const Config&) noexcept;
Result Run(const Config&,const Control&) noexcept;
void DumpFrames() noexcept;
std::size_t FrameOverflow() noexcept;
std::size_t FrameCount() noexcept;
std::size_t FrameLimit() noexcept;
bool DropRegressionRecord(std::int32_t,const std::uint8_t*,std::size_t) noexcept;
bool ExpectedTerminal(const NativeSendRunResult&,const host_test::HostLoopObservation&,bool) noexcept;
#ifdef FASTDDS_DSOFTBUS_DDS_EXPERIMENT_TESTING
void SetAfterWaitHook(std::function<void(eprosima::fastdds::dds::DataWriter*)>);
void SetAfterFrameSelectionForTest(std::function<void()>);
unsigned FrameCycleForTest(std::size_t);
void SetSharedDeleteFailureForTest(bool);
bool DropIsolationObserved() noexcept;
#endif
}
