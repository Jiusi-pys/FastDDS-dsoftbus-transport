#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_TRANSPORT_DESCRIPTOR_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_TRANSPORT_DESCRIPTOR_HPP

#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>
#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>

namespace fastdds_dsoftbus::dsoftbus {
struct DSoftBusTransportDescriptor final : eprosima::fastdds::rtps::TransportDescriptorInterface
{
    explicit DSoftBusTransportDescriptor(std::uint32_t max_record_bytes);
    DSoftBusTransportDescriptor(const DSoftBusTransportDescriptor&) = default;
    DSoftBusTransportDescriptor& operator=(const DSoftBusTransportDescriptor&) = default;
    DirectoryConfiguration directory;
    std::vector<EgressSelection> egress_selections;
    std::vector<Locator> default_metatraffic_unicast;
    std::vector<Locator> default_user_unicast;
    SendQueueLimits send_limits;
    eprosima::fastdds::rtps::TransportInterface* create_transport() const override;
    std::uint32_t min_send_buffer_size() const override;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
