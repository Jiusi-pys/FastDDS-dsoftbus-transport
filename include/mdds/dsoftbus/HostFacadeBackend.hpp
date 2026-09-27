#ifndef MDDS_HOST_FACADE_BACKEND_HPP
#define MDDS_HOST_FACADE_BACKEND_HPP
#if !defined(MDDS_HOST_BACKEND_TESTING) && !defined(MDDS_PUBLIC_SOCKET_DDS_EXPERIMENT)
#error "Host facade injection requires the dedicated Host test library"
#endif
#if defined(MDDS_HOST_BACKEND_TESTING) && defined(MDDS_PUBLIC_SOCKET_DDS_EXPERIMENT)
#error "FACADE_VARIANTS_EXCLUSIVE"
#endif
#include <mdds/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <mdds/dsoftbus/DSoftBusSenderResource.hpp>
#include <mdds/dsoftbus/InputChannels.hpp>
namespace mdds::dsoftbus {
// Shared injected facade contract: Host tests or exclusive DDS experiment.
// Not available to the ordinary default backend. Single-channel close/reopen.
class HostFacadeBackend {
public:
    virtual ~HostFacadeBackend() = default;
    // Failure rolls back only this attempted start. False grants no shutdown
    // authority over a backend that another facade has already started.
    virtual bool Start(const DSoftBusTransportDescriptor&) noexcept = 0;
    virtual InputOpenResult Open(const Locator&, InputChannels::Receiver*) noexcept = 0;
    // Must not wait or acquire a control gate; consulted before facade locks.
    virtual bool InputCloseAllowedOnThisThread() const noexcept = 0;
    virtual bool CloseInput(const Locator&) noexcept = 0;
    virtual SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&) = 0;
    virtual bool Shutdown() noexcept = 0;
};
}
#endif
