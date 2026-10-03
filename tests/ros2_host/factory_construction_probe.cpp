// Loader/factory cause probe only: not a Runtime, SDK or communication model.
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/rtps/transport/TransportDescriptorInterface.h>
#include <atomic>
#include <cstdlib>
#include <memory>
#define PROBE_EXPORT __declspec(dllexport)
namespace {
std::atomic<unsigned> closes{0}, public_deletes{0}, factories{0};
struct RejectedTransport final : eprosima::fastdds::rtps::TransportDescriptorInterface {
    RejectedTransport() : TransportDescriptorInterface(65536, 1) {}
    eprosima::fastdds::rtps::TransportInterface* create_transport() const override {
        ++factories;
        return nullptr; // Force the real factory's autoenable failure path.
    }
    std::uint32_t min_send_buffer_size() const override { return 65536; }
};
}
extern "C" PROBE_EXPORT unsigned FastddsDsoftbusHookVersion() { return 21405; }
extern "C" PROBE_EXPORT int FastddsDsoftbusCreate(const char*, unsigned, void* raw, void** output) {
    *output=std::malloc(1);
    if(!*output)return 1;
    auto& qos=*static_cast<eprosima::fastdds::dds::DomainParticipantQos*>(raw);
    qos.transport().use_builtin_transports=false;
    qos.transport().user_transports.clear();
    qos.transport().user_transports.push_back(std::make_shared<RejectedTransport>());
    return 0;
}
extern "C" PROBE_EXPORT void FastddsDsoftbusAfterPublicDelete(void*) { ++public_deletes; }
extern "C" PROBE_EXPORT int FastddsDsoftbusClose(void* owner) { ++closes;std::free(owner);return 0; }
extern "C" PROBE_EXPORT unsigned ProbeCloseCount() { return closes; }
extern "C" PROBE_EXPORT unsigned ProbePublicDeleteCount() { return public_deletes; }
extern "C" PROBE_EXPORT unsigned ProbeTransportFactoryCount() { return factories; }
