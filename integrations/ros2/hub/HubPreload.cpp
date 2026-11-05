// LD_PRELOAD library that attaches an unmodified ROS 2 process to its board's hub. rmw_fastrtps builds each
// Participant from the Fast DDS default QoS, so setting the Transport there before main() is enough. Built-in
// transports are off, so a process with no hub fails to create its Participant; it cannot fall back to UDP or
// shared memory. The process loads no SDK and holds no token: the hub owns the DSoftBus socket.
#include "../autoload/AutoloadConfig.hpp"
#include "HubTransport.hpp"
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unistd.h>

namespace {
namespace fd = eprosima::fastdds::dds;
namespace ra = fastdds_dsoftbus::ros2_autoload;
namespace hub = fastdds_dsoftbus::ros2_hub;
constexpr int kRefusedExit = 71;

[[noreturn]] void Refuse(const std::string& why)
{
    std::fprintf(stderr, "FDDS_HUB_REFUSED reason=%s\n", why.c_str());
    std::fflush(stderr);
    _exit(kRefusedExit);
}

__attribute__((constructor)) void HubPreloadStart()
{
    const char* path = std::getenv("FASTDDS_DSOFTBUS_HUB");
    if (path == nullptr || path[0] != '/') Refuse("FASTDDS_DSOFTBUS_HUB must be an absolute path");
    const auto environment = ra::CheckRosEnvironment([](const char* n) { return std::getenv(n); }, std::nullopt);
    if (!environment.empty()) Refuse(environment);
    using ReturnCode = eprosima::fastrtps::types::ReturnCode_t;
    auto* factory = fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipantQos qos;
    // rmw_fastrtps calls this same once-only loader first; loading here keeps a later load from replacing the QoS.
    if (factory->load_profiles() != ReturnCode::RETCODE_OK || factory->get_default_participant_qos(qos) != ReturnCode::RETCODE_OK)
        Refuse("factory defaults");
    auto descriptor = std::make_shared<hub::HubTransportDescriptor>(path);
    qos.transport().use_builtin_transports = false;
    qos.transport().user_transports = {descriptor};
    qos.transport().max_msg_size_no_frag = descriptor->maxMessageSize;
    if (factory->set_default_participant_qos(qos) != ReturnCode::RETCODE_OK) Refuse("default QoS");
}
}
