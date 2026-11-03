#pragma once

// S1 interface/lifetime probe only. This is not a DSoftBus transport and must
// not be registered for participant discovery or used as a networking backend.
#include <fastdds/rtps/transport/TransportInterface.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace fastdds_dsoftbus_probe {

namespace rtps = eprosima::fastdds::rtps;
using Deadline = std::chrono::steady_clock::time_point;
// Probe-local tag; a production locator kind still requires S3 validation.
constexpr int32_t interface_probe_kind = 0x40000000;

struct ProbeSendRecord
{
    std::vector<rtps::octet> bytes;
    std::vector<rtps::Locator> targets;
    Deadline deadline;
    int32_t priority = 0;
};

struct ProbeSendObservation
{
    // Test observation storage, not a production asynchronous queue.
    std::mutex mutex;
    std::vector<ProbeSendRecord> records;
    std::atomic<unsigned> cleanup_count{0};
};

class ProbeSenderResource final : public rtps::SenderResource
{
public:
    ProbeSenderResource(std::shared_ptr<ProbeSendObservation> observation, uint32_t max_bytes);
    ~ProbeSenderResource() override;
    ProbeSenderResource(const ProbeSenderResource&) = delete;
    ProbeSenderResource& operator=(const ProbeSenderResource&) = delete;
    ProbeSenderResource(ProbeSenderResource&&) = delete;
    ProbeSenderResource& operator=(ProbeSenderResource&&) = delete;

    // Idempotent, serializes with record(). No SDK or worker thread exists here.
    void release();

private:
    bool record(const std::vector<NetworkBuffer>& buffers, uint32_t total_bytes,
            rtps::LocatorsIterator* begin, rtps::LocatorsIterator* end,
            const Deadline& deadline, int32_t priority);

    std::shared_ptr<ProbeSendObservation> observation_;
    uint32_t max_bytes_;
    std::mutex mutex_;
    bool released_ = false;
};

struct InterfaceProbeDescriptor final : rtps::TransportDescriptorInterface
{
    explicit InterfaceProbeDescriptor(uint32_t max_bytes = 4096);
    rtps::TransportInterface* create_transport() const override;
    uint32_t min_send_buffer_size() const override;

    rtps::Locator local;
    std::shared_ptr<ProbeSendObservation> observation;
};

class InterfaceProbeTransport final : public rtps::TransportInterface
{
public:
    explicit InterfaceProbeTransport(const InterfaceProbeDescriptor& descriptor);
    ~InterfaceProbeTransport() override;

    bool init(const rtps::PropertyPolicy* = nullptr, const uint32_t& = 0) override;
    bool IsInputChannelOpen(const rtps::Locator&) const override;
    bool IsLocatorSupported(const rtps::Locator&) const override;
    bool is_locator_allowed(const rtps::Locator&) const override;
    bool is_locator_reachable(const rtps::Locator&) override;
    rtps::Locator RemoteToMainLocal(const rtps::Locator&) const override;
    bool OpenOutputChannel(rtps::SendResourceList&, const rtps::Locator&) override;
    bool OpenInputChannel(const rtps::Locator&, rtps::TransportReceiverInterface*, uint32_t) override;
    // Call from outside OnDataReceived(). The receiver and this object must
    // outlive outstanding deliver() calls; recursive close/destruction from a
    // receiver callback is not supported by this S1 probe.
    bool CloseInputChannel(const rtps::Locator&) override;
    bool DoInputLocatorsMatch(const rtps::Locator&, const rtps::Locator&) const override;
    rtps::LocatorList NormalizeLocator(const rtps::Locator&) override;
    void select_locators(rtps::LocatorSelector&) const override;
    bool is_local_locator(const rtps::Locator&) const override;
    rtps::TransportDescriptorInterface* get_configuration() override;
    void AddDefaultOutputLocator(rtps::LocatorList&) override;
    bool getDefaultMetatrafficMulticastLocators(rtps::LocatorList&, uint32_t) const override;
    bool getDefaultMetatrafficUnicastLocators(rtps::LocatorList&, uint32_t) const override;
    bool getDefaultUnicastLocators(rtps::LocatorList&, uint32_t) const override;
    bool fillMetatrafficMulticastLocator(rtps::Locator&, uint32_t) const override;
    bool fillMetatrafficUnicastLocator(rtps::Locator&, uint32_t) const override;
    bool configureInitialPeerLocator(rtps::Locator&, const rtps::PortParameters&,
            uint32_t, rtps::LocatorList&) const override;
    bool fillUnicastLocator(rtps::Locator&, uint32_t) const override;
    uint32_t max_recv_buffer_size() const override;
    void shutdown() override;

    // Synchronous injection of simulated input. Receiver data is valid until
    // its callback returns. This API does not prove any SDK callback behavior.
    bool deliver(const rtps::octet* data, uint32_t size, const rtps::Locator& remote);

private:
    InterfaceProbeDescriptor descriptor_;
    mutable std::mutex mutex_;
    std::condition_variable drained_;
    rtps::TransportReceiverInterface* receiver_ = nullptr;
    uint32_t receive_capacity_ = 0;
    size_t in_flight_ = 0;
    bool open_ = false;
    bool closing_ = false;
    bool stopped_ = false;
};

} // namespace fastdds_dsoftbus_probe
