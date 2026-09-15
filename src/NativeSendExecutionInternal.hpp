#ifndef MDDS_NATIVE_SEND_EXECUTION_INTERNAL_HPP
#define MDDS_NATIVE_SEND_EXECUTION_INTERNAL_HPP
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <mutex>
namespace mdds::dsoftbus::detail {
class NativeEgressBridge;
class RuntimeCeilingLease final
{
public:
    ~RuntimeCeilingLease() noexcept;
private:
    friend class NativeEgressBridge;
    friend class EgressState;
    friend class NativeGenerationAdapter;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    RuntimeCeilingLease(std::shared_ptr<NativeState> s,SendQueueLimits l) noexcept : state_(std::move(s)),limits_(l) {}
    const std::shared_ptr<NativeState> state_;
    const SendQueueLimits limits_;
    bool charged_=false; // Original native gate only.
};
class EgressState final
{
public:
    ~EgressState() noexcept;
private:
    friend class NativeEgressBridge;
    friend class ::mdds::dsoftbus::EgressRegistration;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    friend class ::mdds::dsoftbus::NativeSendExecutionTestAccess;
    friend class ::mdds::dsoftbus::NativeRevokeTicket;
    EgressState(std::shared_ptr<RuntimeCeilingLease> l,std::shared_ptr<RuntimeSendPath> r) noexcept
        : ceiling_(std::move(l)),runtime_(std::move(r)),identity_(runtime_->identity()) {}
    const std::shared_ptr<RuntimeCeilingLease> ceiling_;
    const std::shared_ptr<RuntimeSendPath> runtime_;
    const RuntimeIdentityHandle identity_;
    bool active_=false; // Original native gate only, not queue mutex.
    bool strong_revoked_=false, queue_barrier_done_=false;
    struct ControlSlot {
        bool occupied=false;
        std::weak_ptr<NativeSendInvocationState> control;
    };
    std::array<ControlSlot,2> controls_{};
    std::size_t admitted_outstanding_=0;
};
class NativeGenerationAdapter final : public ConnectionGenerationSource
{
public:
    bool TryResolve(const Route&,SendConnectionSnapshot&) const override;
private:
    friend class ::mdds::dsoftbus::NativeSendExecution;
    explicit NativeGenerationAdapter(std::shared_ptr<RuntimeCeilingLease> l) noexcept : ceiling_(std::move(l)) {}
    const std::shared_ptr<RuntimeCeilingLease> ceiling_;
};
class NativeSendWorkState final
{
private:
    friend class NativeSendInvocationState;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    friend class ::mdds::dsoftbus::NativeSendInvocation;
    explicit NativeSendWorkState(std::shared_ptr<EgressState> r) noexcept : registration_(std::move(r)) {}
    std::shared_ptr<EgressState> registration_;
    std::optional<OwnedSendRecord> record_;
};
class NativeSendResources final
{
public:
    ~NativeSendResources() noexcept;
private:
    friend class NativeEgressBridge;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    friend class ::mdds::dsoftbus::NativeSendInvocation;
    NativeSendResources() noexcept = default;
    std::unique_ptr<NativeSendWorkState> work_;
    std::shared_ptr<NativeSendProvider> provider_;
    NativeSendCallTicket ticket_;
    NativeSocketId socket_=-1;
};
class NativeSendInvocationState final
{
public:
    ~NativeSendInvocationState() noexcept;
private:
    friend class NativeEgressBridge;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    friend class ::mdds::dsoftbus::NativeSendInvocation;
    NativeSendInvocationState() noexcept = default;
    enum class Phase { Created, Reserved, Entered, Settling, Terminal };
    std::shared_ptr<EgressState> registration_;
    // Sole operational owner; moved under original native gate, never copied.
    std::shared_ptr<NativeSendResources> resources_;
    SendQueue::Clock::time_point deadline_{};
    Phase phase_=Phase::Created;
    std::size_t slot_=0;
};
// Concrete privileged bridge. Merely including this file cannot mint authority:
// all entry points, constructors and mutable fields are private, not convention.
// Defined alongside the private NativeState, with no runtime/queue method calls.
class NativeEgressBridge final
{
    friend class RuntimeCeilingLease;
    friend class EgressState;
    friend class NativeGenerationAdapter;
    friend class NativeSendInvocationState;
    friend class ::mdds::dsoftbus::NativeRevokeTicket;
    friend class ::mdds::dsoftbus::NativeSendExecution;
    friend class ::mdds::dsoftbus::NativeSendInvocation;
    friend class ::mdds::dsoftbus::NativeSendExecutionTestAccess;
    static std::shared_ptr<NativeState> Bind(NativeOwnerTable&) noexcept;
    static NativeStatus Reserve(RuntimeCeilingLease&,const NativeTableConfiguration&) noexcept;
    static NativeStatus Publish(EgressState&) noexcept;
    static NativeStatus Check(const std::shared_ptr<NativeState>&,const EgressState&) noexcept;
    static NativeStatus Revoke(const std::shared_ptr<NativeState>&,EgressState&) noexcept;
    static NativeEgressUsage Usage(const std::shared_ptr<NativeState>&) noexcept;
    static bool Resolve(const RuntimeCeilingLease&,const Route&,SendConnectionSnapshot&) noexcept;
    static NativeStatus Admit(const std::shared_ptr<NativeState>&,EgressState&,
            const RuntimeIdentityHandle&,const Route&,std::uint64_t,SendQueue::Clock::time_point,
            NativeSendCallTicket&,NativeSocketId&,
            const std::shared_ptr<NativeSendInvocationState>& = {}) noexcept;
    static NativeStatus Enter(NativeSendCallTicket&,SendQueue::Clock::time_point) noexcept;
    static NativeStatus EnterLocked(NativeSendCallTicket&,SendQueue::Clock::time_point) noexcept;
    static NativeStatus EnterInvocation(NativeSendInvocationState&,std::shared_ptr<NativeSendResources>&) noexcept;
    static void CancelInvocation(NativeSendInvocationState&,std::shared_ptr<NativeSendResources>&) noexcept;
    static void SettleInvocation(NativeSendInvocationState&) noexcept;
    static NativeStatus BeginRevoke(const std::shared_ptr<NativeState>&,
            const std::shared_ptr<EgressState>&,NativeRevokeTicket&) noexcept;
    static void QueueBarrier(EgressState&) noexcept;
    static NativeEgressActivity Observe(const std::shared_ptr<NativeState>&,const EgressState&) noexcept;
    static NativeCompletion Fault(NativeSendCallTicket&,NativeProviderFault,std::optional<std::int32_t>) noexcept;
    static std::mutex& TestGate(const std::shared_ptr<NativeState>&) noexcept;
};
}
#endif
