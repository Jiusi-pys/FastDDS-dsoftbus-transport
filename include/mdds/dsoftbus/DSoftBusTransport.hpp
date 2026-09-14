#ifndef MDDS_DSOFTBUS_TRANSPORT_HPP
#define MDDS_DSOFTBUS_TRANSPORT_HPP

#include <mdds/dsoftbus/DSoftBusTransportDescriptor.hpp>
#include <mdds/dsoftbus/DSoftBusSenderResource.hpp>
#include <mdds/dsoftbus/InputChannels.hpp>

namespace mdds::dsoftbus {
enum class FacadeMode { NativeUnavailable, HostModel };
enum class FacadeState { Constructed, Initializing, ModelReady, Failed, Closing, Closed };
enum class FacadeError {
    None, InvalidLimits, DirectoryRejected, EgressRejected, InvalidDefaultLocal,
    UnsupportedNoFragmentRequest, NativeBackendUnavailable, AllocationFailure,
    InvalidState, InvalidLocator, InsufficientReceiverCapacity, InputConflict,
    InputClosing, SenderAttachRejected
};
struct FacadeDiagnostic {
    FacadeError error = FacadeError::None;
    const char* operation = "none"; // Static operation name, never caller-owned text.
    std::size_t entry = static_cast<std::size_t>(-1);
    DirectoryIssue directory_issue{};
    EgressIssue egress_issue{};
};
class DSoftBusTransportTestAccess;

class DSoftBusTransport final : public eprosima::fastdds::rtps::TransportInterface
{
public:
    using LocatorList = eprosima::fastdds::rtps::LocatorList;
    using SendResourceList = eprosima::fastdds::rtps::SendResourceList;
    using Selector = eprosima::fastdds::rtps::LocatorSelector;
    using SelectorEntry = eprosima::fastdds::rtps::LocatorSelectorEntry;
    using Receiver = eprosima::fastdds::rtps::TransportReceiverInterface;
    explicit DSoftBusTransport(const DSoftBusTransportDescriptor&);
    ~DSoftBusTransport() override;
    // Pre-publication control: no concurrent init/destruction. A concurrent
    // shutdown prevents late ready publication; it does not join construction.
    bool init(const eprosima::fastdds::rtps::PropertyPolicy* = nullptr, const std::uint32_t& = 0) override;
    FacadeMode mode() const noexcept;
    FacadeState state() const;
    FacadeDiagnostic last_error() const;
    bool IsInputChannelOpen(const Locator&) const override;
    bool IsLocatorSupported(const Locator&) const override;
    bool is_locator_allowed(const Locator&) const override;
    bool is_locator_reachable(const Locator&) override;
    Locator RemoteToMainLocal(const Locator&) const override;
    using TransportInterface::transform_remote_locator;
    bool transform_remote_locator(const Locator&, Locator&) const override;
    bool OpenOutputChannel(SendResourceList&, const Locator&) override;
    bool OpenOutputChannels(SendResourceList&, const SelectorEntry&) override;
    // Aggregate resources belong to their list owner, not one endpoint's locators.
    void CloseOutputChannels(SendResourceList&, const SelectorEntry&) override;
    // ALL input-control calls and destruction are external-control operations.
    // No Open/Close/shutdown/destruction on any synchronous receiver callback
    // stack, including another channel. Retain objects throughout member calls.
    bool OpenInputChannel(const Locator&, Receiver*, std::uint32_t) override;
    bool CloseInputChannel(const Locator&) override;
    bool DoInputLocatorsMatch(const Locator&, const Locator&) const override;
    LocatorList NormalizeLocator(const Locator&) override;
    void select_locators(Selector&) const override;
    bool is_local_locator(const Locator&) const override;
    // Stable report view; treat as read-only. Mutation NEVER reconfigures the
    // separately frozen live configuration. Concurrent view mutation unsupported.
    eprosima::fastdds::rtps::TransportDescriptorInterface* get_configuration() override;
    void AddDefaultOutputLocator(LocatorList&) override;
    bool getDefaultMetatrafficMulticastLocators(LocatorList&, std::uint32_t) const override;
    bool getDefaultMetatrafficUnicastLocators(LocatorList&, std::uint32_t) const override;
    bool getDefaultUnicastLocators(LocatorList&, std::uint32_t) const override;
    bool fillMetatrafficMulticastLocator(Locator&, std::uint32_t) const override;
    bool fillMetatrafficUnicastLocator(Locator&, std::uint32_t) const override;
    bool configureInitialPeerLocator(Locator&, const eprosima::fastdds::rtps::PortParameters&,
            std::uint32_t, LocatorList&) const override;
    bool fillUnicastLocator(Locator&, std::uint32_t) const override;
    std::uint32_t max_recv_buffer_size() const override;
    bool is_localhost_allowed() const override;
    void update_network_interfaces() override;
    // Each returned call has crossed both real core barriers. No SDK drain,
    // cancellation or revocation of taken records is implied.
    void shutdown() override;
private:
    friend class DSoftBusTransportTestAccess;
    struct HostModelTag {};
    struct LocalSlot {
        explicit LocalSlot(const Locator& value) : local(value) {}
        const Locator local;
        std::mutex control;
        std::atomic<bool> committed_open{false};
    };
    struct Ready {
        Ready(EgressPlan::Snapshot, const DSoftBusTransportDescriptor&,
                std::shared_ptr<const ConnectionGenerationSource>);
        const EgressPlan::Snapshot plan;
        std::unique_ptr<InputChannels> inputs;
        std::shared_ptr<RuntimeSendPath> runtime;
        std::vector<std::unique_ptr<LocalSlot>> slots;
        LocalSlot* FindLocal(const Locator&) const noexcept;
    };
    struct TestHooks {
        std::function<void()> before_ready_publish; // May throw; no registrations yet.
        std::function<void()> after_core_open; // Outside lifecycle lock; nonthrowing.
        std::function<void()> after_close_mark; // Outside lifecycle lock; nonthrowing.
        std::function<void()> after_shutdown_request; // Outside lifecycle lock; nonthrowing.
    };
    DSoftBusTransport(const DSoftBusTransportDescriptor&, HostModelTag,
            std::shared_ptr<const ConnectionGenerationSource>);
    std::shared_ptr<Ready> ReadyLease() const;
    void Diagnose(FacadeDiagnostic) const;
    bool InitFailure(FacadeDiagnostic);
    bool AppendDefaults(LocatorList&, const std::vector<Locator>&) const;
    const DSoftBusTransportDescriptor frozen_;
    DSoftBusTransportDescriptor report_;
    const FacadeMode mode_;
    const std::shared_ptr<const ConnectionGenerationSource> generations_;
    mutable std::mutex lifecycle_;
    FacadeState state_ = FacadeState::Constructed;
    mutable FacadeDiagnostic diagnostic_;
    std::shared_ptr<Ready> ready_; // Retained across EVERY shutdown until destruction.
    TestHooks hooks_;
};
} // namespace mdds::dsoftbus
#endif
