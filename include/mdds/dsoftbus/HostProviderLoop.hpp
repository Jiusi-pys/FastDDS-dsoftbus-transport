#ifndef MDDS_HOST_PROVIDER_LOOP_HPP
#define MDDS_HOST_PROVIDER_LOOP_HPP
#include <mdds/dsoftbus/HostFacadeBackend.hpp>
#include <mdds/dsoftbus/DSoftBusTransport.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <mdds/dsoftbus/ProviderAdapter.hpp>
namespace mdds::dsoftbus::host_test {
struct HostLoopObservation {
    bool started=false, input_open=false, sealed=false, closed=false;
    std::size_t active_pumps=0;
    bool input_closing=false;
    std::uint64_t open_calls=0,close_calls=0,shutdown_calls=0;
    std::uint64_t first_close_order=0,first_shutdown_order=0;
    SendQueueUsage tx;
    AttachmentUsage receive; // Runtime ledger remains observable after input detachment.
    NativeEgressActivity send;
    phase0::AdapterObservation adapter;
    phase0::BackendCloseResult close;
    std::optional<phase0::BackendCloseResult> failed_open_cleanup;
};
// Owns the same table for Binding and NativeSendExecution; no table/execution/
// raw provider pointer is exposed. Driver joins pump threads before destruction.
class HostProviderLoop final : public HostFacadeBackend {
public:
    explicit HostProviderLoop(std::shared_ptr<phase0::NativeBackend>, std::shared_ptr<ProcessReceiveObserver> = {});
    ~HostProviderLoop() override;
    bool Start(const DSoftBusTransportDescriptor&) noexcept override;
    InputOpenResult Open(const Locator&, InputChannels::Receiver*) noexcept override;
    bool InputCloseAllowedOnThisThread() const noexcept override;
    bool CloseInput(const Locator&) noexcept override;
    SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&) override;
    bool Shutdown() noexcept override;
    NativeSendRunResult PumpSend() noexcept;
    phase0::BackendStatus PumpReceive() noexcept;
    HostLoopObservation Observe() const noexcept;
    bool WaitSealed(std::chrono::steady_clock::time_point) const;
    bool WaitInputClosing(std::chrono::steady_clock::time_point) const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
namespace mdds::dsoftbus {
class HostFacadeTestAccess {
public:
    static std::unique_ptr<DSoftBusTransport> Create(const DSoftBusTransportDescriptor&,
            std::shared_ptr<HostFacadeBackend>);
};
}
#endif
