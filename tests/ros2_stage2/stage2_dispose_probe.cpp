// Tells a clean Participant exit from a lost one. ROS removes nodes through ros_discovery_info before the Participant
// goes, so a ROS-level graph check cannot see whether the Participant's own dispose reached the peer. Fast DDS can:
// REMOVED_PARTICIPANT means the dispose arrived, DROPPED_PARTICIPANT means the lease ran out.
//
//   stage2_dispose_probe stay <domain>     waits for a peer to appear and go, prints how it went
//   stage2_dispose_probe leave <domain>    waits for a peer to appear, then deletes its Participant
//
// It uses the default Participant QoS, so the preload library selects the Transport as for any ROS process.
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
namespace fd = eprosima::fastdds::dds;
using Status = eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::DISCOVERY_STATUS;
using Clock = std::chrono::steady_clock;

class Watcher final : public fd::DomainParticipantListener {
public:
    void on_participant_discovery(fd::DomainParticipant*, eprosima::fastrtps::rtps::ParticipantDiscoveryInfo&& info) override
    {
        if (info.status == Status::DISCOVERED_PARTICIPANT) discovered = true;
        else if (info.status == Status::REMOVED_PARTICIPANT) removed = true;
        else if (info.status == Status::DROPPED_PARTICIPANT) dropped = true;
    }
    std::atomic<bool> discovered{false}, removed{false}, dropped{false};
};

template <typename Predicate>
bool wait(Predicate predicate, int seconds)
{
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    while (Clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}
}

int main(int argc, char** argv)
{
    if (argc != 3) { std::fprintf(stderr, "usage: stage2_dispose_probe stay|leave <domain>\n"); return 2; }
    const bool stay = std::strcmp(argv[1], "stay") == 0;
    Watcher watcher;
    auto* factory = fd::DomainParticipantFactory::get_instance();
    auto* participant = factory->create_participant(static_cast<fd::DomainId_t>(std::atoi(argv[2])), fd::PARTICIPANT_QOS_DEFAULT, &watcher);
    if (participant == nullptr) { std::printf("S2_DISPOSE result=no_participant\n"); std::fflush(stdout); return 3; }
    const bool discovered = wait([&] { return watcher.discovered.load(); }, 60);
    int code = discovered ? 0 : 4;
    if (stay) {
        const auto start = Clock::now();
        const bool gone = wait([&] { return watcher.removed.load() || watcher.dropped.load(); }, 45);
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        std::printf("S2_DISPOSE discovered=%d gone=%d removed=%d dropped=%d seconds=%.2f\n", discovered, gone, watcher.removed.load(), watcher.dropped.load(), seconds);
        std::fflush(stdout);
        if (!watcher.removed.load()) code = 5;
    } else {
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    participant->set_listener(nullptr);
    if (factory->delete_participant(participant) != eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK) code = 6;
    return code;
}
