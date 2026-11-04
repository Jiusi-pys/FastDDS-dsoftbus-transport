#pragma once

#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/rtps/transport/TransportDescriptorInterface.h>
#include <memory>
#include <vector>

namespace fastdds_dsoftbus::stock_transport {

enum class BootstrapResult {
    Ok, InvalidConfiguration, AlreadyInstalled, ParticipantsActive,
    DefaultsChanged, FactoryError
};

// Process-start configuration through public Fast DDS APIs. No create/delete
// interception, DDS return-code translation, endpoint policy or worker thread.
// The caller serializes these operations with all Participant creation/deletion
// and owns a single configured domain. Runtime and its pump remain caller-owned.
// Participant-owned Transport destruction is responsible for native shutdown,
// even while the factory's default QoS still retains this descriptor.
class StockTransportBootstrap final {
public:
    explicit StockTransportBootstrap(eprosima::fastdds::dds::DomainParticipantFactory&);
    StockTransportBootstrap(const StockTransportBootstrap&) = delete;
    StockTransportBootstrap& operator=(const StockTransportBootstrap&) = delete;

    BootstrapResult Install(
            eprosima::fastdds::dds::DomainId_t domain,
            std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>,
            const std::vector<eprosima::fastrtps::rtps::Locator_t>& local,
            const std::vector<eprosima::fastrtps::rtps::Locator_t>& peers);
    // Explicitly restore only this registration's unchanged default QoS after
    // successful public Participant deletion. Does not call Runtime::Close or
    // manufacture a public deletion result. A refusal changes no factory state.
    BootstrapResult Release();
    bool installed() const noexcept { return installed_; }

private:
    eprosima::fastdds::dds::DomainParticipantFactory& factory_;
    eprosima::fastdds::dds::DomainId_t domain_ = 0;
    eprosima::fastdds::dds::DomainParticipantQos previous_, selected_;
    bool installed_ = false;
};
}
