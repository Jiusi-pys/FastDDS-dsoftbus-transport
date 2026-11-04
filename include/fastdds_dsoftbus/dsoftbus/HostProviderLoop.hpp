#ifndef FASTDDS_DSOFTBUS_HOST_PROVIDER_LOOP_HPP
#define FASTDDS_DSOFTBUS_HOST_PROVIDER_LOOP_HPP
#include <fastdds_dsoftbus/dsoftbus/HostFacadeBackend.hpp>
#include <fastdds_dsoftbus/dsoftbus/DSoftBusTransport.hpp>
#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>
#include <functional>
#include <array>
#include <optional>
#include <fastdds_dsoftbus/dsoftbus/HostRuntimeShutdownPoint.hpp>
namespace fastdds_dsoftbus::dsoftbus::host_test {
// Observation of the work actually selected by a shared runtime pump. It grants
// no authority and carries neither owners nor SDK generations.
struct HostSendObservation {
    ReceiverIdentity receiver{};
    bool selected=false, view_closing=false, admission_closed=false;
    NativeStatus registration_status=NativeStatus::NotFound;
};
struct HostLoopObservation {
    bool started=false, input_open=false, sealed=false, closed=false;
    bool shutdown_observer_fault=false;
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
// Diagnostic values only: no owner/lease/entrance or mutating capability.
// Available means metadata was sampled, never that communication is ready.
enum class DomainCircuitObservationStatus { Disabled, NotStarted, Available };
struct DomainCircuitObservation {
    std::uint32_t domain=0; // Zero is valid; optional entry presence identifies absence.
    std::optional<phase0::AdapterObservation> adapter;
    std::optional<NativeUsageSnapshot> native;
};
struct DomainCircuitObservations {
    DomainCircuitObservationStatus status=DomainCircuitObservationStatus::Disabled;
    bool root_closed=false, terminal_adapters_available=false;
    std::size_t count=0;
    std::array<std::optional<DomainCircuitObservation>,2> entries{};
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
    // The explicit owner is not a Participant/receiver. Start it before joining
    // views; it retains live native anchors even when there are no views.
    static std::shared_ptr<HostProviderLoop> CreateOwnedShared(std::shared_ptr<phase0::NativeBackend>);
    // Exact assembly owner without a native lifetime pin. Start prepares only
    // metadata. Failed initial reservations can refund an unused seed; after a
    // facade is published, the last view/reservation permanently closes it.
    static std::shared_ptr<HostProviderLoop> CreateParticipantShared(std::shared_ptr<phase0::NativeBackend>,
            std::shared_ptr<ProcessReceiveObserver> = {});
    // Persistent views retain their exact owner, so dropping a descriptor/Runtime
    // cannot destroy the native owner while a borrowed view still exists.
    // A process-pinned owner admits at most two active or borrowed Participant
    // views, sharing its native channel through independent receiver leases.
    // Ordinary CreateShared assembly keeps the same existing two-view bound.
    std::shared_ptr<HostProviderLoop> Join(std::shared_ptr<HostProviderLoop> owner = {}) noexcept;
    // Internal factory transaction commit: complete facade construction, then
    // commit before returning its original owned pointer. No init/SDK claim.
    bool PublishFacade() noexcept;
    // Internal assembly seam, not installed. The owning Runtime supplies its
    // optional coordination gate here; the loop arms it for exactly one
    // Initialize() call on the calling thread. Refused once the loop has started.
    bool SetPreConnectGate(std::function<bool()>);
    // Single-thread assembly only, before Start. Immutable once installed;
    // ordinary shared/pinned runtimes and replacement are refused. The exact
    // normal Participant owner retains this pre-start observer contract.
    bool SetBeforeNativeClose(std::function<void(const RuntimeShutdownPoint&)>);
    bool Start(const DSoftBusTransportDescriptor&) noexcept override;
    InputOpenResult Open(const Locator&, InputChannels::Receiver*) noexcept override;
    bool InputCloseAllowedOnThisThread() const noexcept override;
    bool CloseInput(const Locator&) noexcept override;
    SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&) override;
    SenderAttachResult AttachWithDomainGuard(eprosima::fastdds::rtps::SendResourceList&,
            const std::shared_ptr<ViewDomainGuard>&) override;
    bool Shutdown() noexcept override;
    NativeSendRunResult PumpSend() noexcept;
    NativeSendRunResult PumpSendObserved(HostSendObservation&) noexcept;
    phase0::BackendStatus PumpReceive() noexcept;
    std::uint64_t WorkGeneration() const noexcept;
    WorkWaitResult WaitForWork(std::uint64_t& generation);
    void RequestPumpStop() noexcept;
    phase0::BackendStatus CloseAccepted() noexcept;
    phase0::BackendStatus RequestNormalEnd(RecordRole) noexcept;
    NativeStatus ObserveNodeOffline(const std::string& network_id) noexcept;
    NativeRouteSnapshot ObserveRoute() const noexcept;
    bool SetBoundedReplacementGate(std::function<bool()>);
    void SealBoundedAdmission() noexcept;
    HostLoopObservation Observe() const noexcept;
    // Non-atomic across groups. Adapter values are available only after a
    // successful root close/drain; active or failed close leaves them nullopt.
    // Pins metadata briefly; never calls SDK or takes a native-control mutex.
    DomainCircuitObservations ObserveDomainCircuits() const noexcept;
    bool EnableDomainCircuits();
    bool RequiresDomainBinding() const noexcept override;
    bool BindDomain(std::uint32_t,const std::shared_ptr<ViewDomainGuard>&) noexcept override;
    bool WaitSealed(std::chrono::steady_clock::time_point) const;
    bool WaitInputClosing(std::chrono::steady_clock::time_point) const;
private:
    friend class HostProviderLoopTestAccess;
    NativeSendRunResult PumpSendImpl(HostSendObservation*) noexcept;
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
    void SetSharedSendHook(std::function<void(bool)>);
#endif
    void SetSharedControlHooks(std::function<void()> before_publish,std::function<void()> after_seal);
    struct State;
    struct Circuit;
    static std::shared_ptr<Circuit> EnsureCircuit(const std::shared_ptr<State>&,std::uint32_t);
    struct View;
    HostProviderLoop(std::shared_ptr<State>,std::shared_ptr<View>);
    bool StartRuntime(const DSoftBusTransportDescriptor&) noexcept;
    InputOpenResult OpenRuntime(const Locator&,InputChannels::Receiver*) noexcept;
    bool ShutdownRuntime(bool owns_stop=false) noexcept;
    bool StartShared(const DSoftBusTransportDescriptor&) noexcept;
    InputOpenResult OpenShared(const Locator&,InputChannels::Receiver*) noexcept;
    bool CloseSharedInput(const Locator&) noexcept;
    bool ShutdownShared() noexcept;
    bool ShutdownOwner() noexcept;
    std::shared_ptr<State> state_;
    std::shared_ptr<View> view_;
    std::shared_ptr<HostProviderLoop> owner_keepalive_;
};
}
namespace fastdds_dsoftbus::dsoftbus {
class HostFacadeTestAccess {
public:
    static std::unique_ptr<DSoftBusTransport> Create(const DSoftBusTransportDescriptor&,
            std::shared_ptr<HostFacadeBackend>);
};
}
#endif
