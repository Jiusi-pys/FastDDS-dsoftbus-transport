#pragma once
// Fast DDS 2.14 Transport that attaches a ROS process to its board's hub. Each instance has one random
// unicast address and one receive thread. It reconnects with the same address after a hub restart, so
// to RTPS a restart within the lease looks like a brief network outage.
#include "HubLifecycle.hpp"
#include "HubRouter.hpp"
#include <fastdds/rtps/transport/SenderResource.h>
#include <fastdds/rtps/transport/TransportDescriptorInterface.h>
#include <fastdds/rtps/transport/TransportInterface.h>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace fastdds_dsoftbus::ros2_hub {
namespace rt = eprosima::fastdds::rtps;
using Locator = eprosima::fastrtps::rtps::Locator_t;
using LocatorList = eprosima::fastrtps::rtps::LocatorList_t;

struct HubTransportDescriptor final : rt::TransportDescriptorInterface {
    explicit HubTransportDescriptor(std::string path)
        : TransportDescriptorInterface(static_cast<std::uint32_t>(kMaxPayloadBytes), 0), socket_path(std::move(path)) {}
    rt::TransportInterface* create_transport() const override;
    std::uint32_t min_send_buffer_size() const override { return maxMessageSize; }
    std::string socket_path;
};

class HubTransport final : public rt::TransportInterface {
public:
    explicit HubTransport(const HubTransportDescriptor& descriptor);
    ~HubTransport() override;

    bool init(const eprosima::fastrtps::rtps::PropertyPolicy* = nullptr, const std::uint32_t& = 0) override;
    bool IsInputChannelOpen(const Locator&) const override;
    bool IsLocatorSupported(const Locator&) const override;
    bool is_locator_allowed(const Locator&) const override;
    Locator RemoteToMainLocal(const Locator&) const override;
    bool transform_remote_locator(const Locator&, Locator&) const override;
    bool OpenOutputChannel(rt::SendResourceList&, const Locator&) override;
    bool OpenInputChannel(const Locator&, rt::TransportReceiverInterface*, std::uint32_t) override;
    bool CloseInputChannel(const Locator&) override;
    bool DoInputLocatorsMatch(const Locator&, const Locator&) const override;
    LocatorList NormalizeLocator(const Locator&) override;
    void select_locators(eprosima::fastrtps::rtps::LocatorSelector&) const override;
    bool is_local_locator(const Locator&) const override;
    rt::TransportDescriptorInterface* get_configuration() override { return &descriptor_; }
    void AddDefaultOutputLocator(LocatorList&) override {}
    bool getDefaultMetatrafficMulticastLocators(LocatorList&, std::uint32_t) const override;
    bool getDefaultMetatrafficUnicastLocators(LocatorList&, std::uint32_t) const override;
    bool getDefaultUnicastLocators(LocatorList&, std::uint32_t) const override;
    bool fillMetatrafficMulticastLocator(Locator&, std::uint32_t) const override;
    bool fillMetatrafficUnicastLocator(Locator&, std::uint32_t) const override;
    bool configureInitialPeerLocator(Locator&, const eprosima::fastrtps::rtps::PortParameters&, std::uint32_t, LocatorList&) const override;
    bool fillUnicastLocator(Locator&, std::uint32_t) const override;
    std::uint32_t max_recv_buffer_size() const override { return static_cast<std::uint32_t>(kMaxPayloadBytes); }
    // shutdown() stays the interface's no-op, as on UDP: Fast DDS calls it before it sends the Participant dispose,
    // and the dispose is what tells peers at once that this Participant left. The destructor closes the socket.

    bool Send(const std::uint8_t* data, std::uint32_t size, rt::LocatorsIterator* begin, rt::LocatorsIterator* end,
            const std::chrono::steady_clock::time_point& deadline);

private:
    Locator Own(std::uint32_t port) const;
    void Stop();
    /// A new socket by the given deadline: hello, then every group this process wants to be in. The
    /// deadline bounds the connect itself, not just the caller's retries, because a blocking connect to
    /// a hub that is listening and not accepting used to outlast every timeout in this class.
    bool Connect(const std::chrono::steady_clock::time_point& deadline);
    bool SendControl(int fd, std::uint32_t magic, std::uint32_t port) const;
    /// SendControl with a bounded retry. True only when the frame was written.
    bool SendControlBounded(int fd, std::uint32_t magic, std::uint32_t port) const;
    /// Write a JOIN for every group that is wanted and not yet synchronized on this socket. False when
    /// one could not be written, which makes the socket unfit to carry this process's membership.
    bool SyncMembership(int fd);
    /// Close this socket if it is still the current one, so the receive thread replaces it and
    /// resynchronizes membership from scratch. Membership that is merely wanted survives.
    void DropSocket(int fd);
    void ReceiveLoop();
    void Dispatch(const FrameHeader& header, const std::uint8_t* payload, std::size_t size);

    HubTransportDescriptor descriptor_;
    Address address_{};
    std::atomic<bool> stopping_{false};
    std::thread receiver_;

    mutable std::shared_mutex socket_gate_; // senders share it; replacing the socket is exclusive
    int fd_ = -1;

    mutable std::shared_mutex receivers_gate_; // Close waits for a running callback
    std::map<Locator, rt::TransportReceiverInterface*> receivers_;

    // Taken before receivers_gate_ and socket_gate_, by every path that changes membership or replaces
    // the socket. It is what stops a channel opened during a reconnection from being joined on the
    // socket that is going away and then missed by the one that replaces it.
    mutable std::mutex membership_gate_;
    Membership membership_;
};
}
