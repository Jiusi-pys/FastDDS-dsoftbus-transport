#ifndef MDDS_HOST_PROVIDER_LOOP_HPP
#define MDDS_HOST_PROVIDER_LOOP_HPP
#include <mdds/dsoftbus/HostFacadeBackend.hpp>
#include <mdds/dsoftbus/DSoftBusTransport.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <mdds/dsoftbus/ProviderAdapter.hpp>
namespace mdds::dsoftbus::host_test {
// Observation of the work actually selected by a shared runtime pump. It grants
// no authority and carries neither owners nor SDK generations.
struct HostSendObservation {
    ReceiverIdentity receiver{};
    bool selected=false, view_closing=false, admission_closed=false;
    NativeStatus registration_status=NativeStatus::NotFound;
};
struct HostLoopObservation {
    bool started=false, input_open=false, sealed=false, closed=false;
    std::size_t active_pumps=0;
    bool input_closing=false;
    std::uint64_t open_calls=0,close_calls=0,shutdown_calls=0;
    std::uint64_t first_close_order=0,first_shutdown_order=0;
    // Per-view egress ledgers: they describe the registration of the view that was
    // asked, and stay empty for a view that holds none.
    SendQueueUsage tx;
    AttachmentUsage receive; // Runtime ledger remains observable after input detachment.
    NativeEgressActivity send;
    NativeUsageSnapshot native;
    phase0::AdapterObservation adapter;
    phase0::BackendCloseResult close;
    std::optional<phase0::BackendCloseResult> failed_open_cleanup;
    std::size_t live_views=0;
    std::size_t view_controls=0;
    bool view_closing=false;
    bool shared_runtime=false;
    // Runtime-wide aggregate, deliberately named apart from the per-view tx/send
    // above: it sums the admitted-but-unsettled egress of every live view, so it is
    // never a substitute for one view's own figures.
    std::size_t runtime_admitted_outstanding=0;
    // The receiver this view serves now, and the one it retired when it closed its
    // input. A closed view reports an empty active identity rather than its retired
    // one, so a retired identity can never be read as live.
    ReceiverIdentity receiver_identity{};
    ReceiverIdentity retired_receiver_identity{};
};
// Owns the same table for Binding and NativeSendExecution; no table/execution/
// raw provider pointer is exposed. Driver joins pump threads before destruction.
class HostProviderLoop final : public HostFacadeBackend {
public:
    explicit HostProviderLoop(std::shared_ptr<phase0::NativeBackend>, std::shared_ptr<ProcessReceiveObserver> = {});
    ~HostProviderLoop() override;
    // Explicit same-channel assembly. Ordinary constructor remains exclusive.
    static std::shared_ptr<HostProviderLoop> CreateShared(std::shared_ptr<phase0::NativeBackend>,
            std::shared_ptr<ProcessReceiveObserver> = {});
    std::shared_ptr<HostProviderLoop> Join() noexcept;
    bool Start(const DSoftBusTransportDescriptor&) noexcept override;
    InputOpenResult Open(const Locator&, InputChannels::Receiver*) noexcept override;
    bool InputCloseAllowedOnThisThread() const noexcept override;
    bool CloseInput(const Locator&) noexcept override;
    SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&) override;
    bool Shutdown() noexcept override;
    NativeSendRunResult PumpSend() noexcept;
    NativeSendRunResult PumpSendObserved(HostSendObservation&) noexcept;
    phase0::BackendStatus PumpReceive() noexcept;
    phase0::BackendStatus CloseAccepted() noexcept;
    HostLoopObservation Observe() const noexcept;
    bool WaitSealed(std::chrono::steady_clock::time_point) const;
    bool WaitInputClosing(std::chrono::steady_clock::time_point) const;
private:
    friend class HostProviderLoopTestAccess;
    NativeSendRunResult PumpSendImpl(HostSendObservation*) noexcept;
#ifdef MDDS_HOST_BACKEND_TESTING
    void SetSharedSendHook(std::function<void(bool)>);
#endif
    void SetSharedControlHooks(std::function<void()> before_publish,std::function<void()> after_seal);
    struct State;
    struct View;
    HostProviderLoop(std::shared_ptr<State>,std::shared_ptr<View>);
    bool StartRuntime(const DSoftBusTransportDescriptor&) noexcept;
    InputOpenResult OpenRuntime(const Locator&,InputChannels::Receiver*) noexcept;
    bool ShutdownRuntime(bool owns_stop=false) noexcept;
    bool StartShared(const DSoftBusTransportDescriptor&) noexcept;
    InputOpenResult OpenShared(const Locator&,InputChannels::Receiver*) noexcept;
    bool CloseSharedInput(const Locator&) noexcept;
    bool ShutdownShared() noexcept;
    std::shared_ptr<State> state_;
    std::shared_ptr<View> view_;
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
