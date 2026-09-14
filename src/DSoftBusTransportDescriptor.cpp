#include <mdds/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <mdds/dsoftbus/DSoftBusTransport.hpp>
#include <cstdio>
#include <limits>

namespace mdds::dsoftbus {
DSoftBusTransportDescriptor::DSoftBusTransportDescriptor(std::uint32_t bound)
    : TransportDescriptorInterface(bound, 1),
      directory{std::vector<LocalBinding>(std::size_t{0}), std::vector<Route>(std::size_t{0})},
      egress_selections(std::size_t{0}), default_metatraffic_unicast(std::size_t{0}), default_user_unicast(std::size_t{0})
{ send_limits.max_record_bytes = bound; }
eprosima::fastdds::rtps::TransportInterface* DSoftBusTransportDescriptor::create_transport() const
{
    try { return new DSoftBusTransport(*this); }
    catch (const std::bad_alloc&) {
        std::fputs("MDDS DSoftBus factory: AllocationFailure\n", stderr); std::fflush(stderr); return nullptr;
    }
}
std::uint32_t DSoftBusTransportDescriptor::min_send_buffer_size() const
{
    return send_limits.max_record_bytes <= (std::numeric_limits<std::uint32_t>::max)()
            ? static_cast<std::uint32_t>(send_limits.max_record_bytes) : 0;
}
} // namespace mdds::dsoftbus
