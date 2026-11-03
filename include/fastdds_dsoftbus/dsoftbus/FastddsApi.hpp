#pragma once

// Version-bound type frontends. The 2.14 profile is opt-in and never changes the
// default 3.6 binding or pretends that a vendor scatter/gather API exists there.
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/PortParameters.h>
#include <fastdds/rtps/attributes/PropertyPolicy.h>
#include <fastdds/rtps/transport/SenderResource.h>
#include <fastdds/rtps/transport/TransportInterface.h>
#include <fastdds/rtps/transport/TransportDescriptorInterface.h>
#include <fastdds/rtps/transport/TransportReceiverInterface.h>
#else
#include <fastdds/rtps/common/Locator.hpp>
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/PortParameters.hpp>
#include <fastdds/rtps/attributes/PropertyPolicy.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>
#include <fastdds/rtps/transport/TransportDescriptorInterface.hpp>
#include <fastdds/rtps/transport/NetworkBuffer.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>
#endif

namespace fastdds_dsoftbus::binding {
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
using Locator_t = eprosima::fastrtps::rtps::Locator_t;
using LocatorList_t = eprosima::fastrtps::rtps::LocatorList_t;
using LocatorSelector = eprosima::fastrtps::rtps::LocatorSelector;
using LocatorSelectorEntry = eprosima::fastrtps::rtps::LocatorSelectorEntry;
using PortParameters = eprosima::fastrtps::rtps::PortParameters;
using PropertyPolicy = eprosima::fastrtps::rtps::PropertyPolicy;
using SenderResource = eprosima::fastrtps::rtps::SenderResource;
// Project-owned borrowed slice: the old DDS frontend provides exactly one
// contiguous slice. Existing Submit performs the existing owned-copy admission.
struct NetworkBuffer {
    const void* buffer = nullptr;
    std::uint32_t size = 0;
    NetworkBuffer() = default;
    NetworkBuffer(const void* p, std::uint32_t n) : buffer(p), size(n) {}
};
#else
using Locator_t = eprosima::fastdds::rtps::Locator_t;
using LocatorList_t = eprosima::fastdds::rtps::LocatorList_t;
using LocatorSelector = eprosima::fastdds::rtps::LocatorSelector;
using LocatorSelectorEntry = eprosima::fastdds::rtps::LocatorSelectorEntry;
using PortParameters = eprosima::fastdds::rtps::PortParameters;
using PropertyPolicy = eprosima::fastdds::rtps::PropertyPolicy;
using SenderResource = eprosima::fastdds::rtps::SenderResource;
using NetworkBuffer = eprosima::fastdds::rtps::NetworkBuffer;
#endif
}
