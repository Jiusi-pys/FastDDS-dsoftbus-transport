#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_SENDER_RESOURCE_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_SENDER_RESOURCE_HPP

#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>
#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>

namespace fastdds_dsoftbus::dsoftbus {
enum class SenderAttachStatus
{
    Attached, AlreadyAttached, InvalidRuntime, RuntimeClosed, NoEgressRoutes,
    ReleasedEntryPresent, OverlappingOwnership, AllocationFailure, DomainGuardConflict, DomainGuardUnsupported
};
struct SenderAttachResult
{
    SenderAttachStatus status;
    bool ok() const noexcept
    { return status == SenderAttachStatus::Attached || status == SenderAttachStatus::AlreadyAttached; }
};

// A cached Sender may be created before the public DID callback. This handle
// binds exactly once to that view's real send path and never retargets a new view.
class RuntimeSenderBinding final {
public:
    RuntimeSenderBinding(EgressPlan::Snapshot plan,std::shared_ptr<ViewDomainGuard> guard)
        :plan_(std::move(plan)),guard_(std::move(guard)){}
    EgressPlan::Snapshot plan() const noexcept {return plan_;}
    const std::shared_ptr<ViewDomainGuard>& guard() const noexcept {return guard_;}
    std::shared_ptr<RuntimeSendPath> target() const noexcept {return std::atomic_load_explicit(&target_,std::memory_order_acquire);}
private:
    friend class DSoftBusSenderResource;
    std::function<RuntimeSendStatus(RuntimeSendPath::Clock::time_point)> prepare_;
    const EgressPlan::Snapshot plan_;
    const std::shared_ptr<ViewDomainGuard> guard_;
    std::mutex gate_;
    std::shared_ptr<RuntimeSendPath> target_;
};
class DSoftBusSenderResource final : public fastdds_dsoftbus::binding::SenderResource
{
public:
    using NetworkBuffer = fastdds_dsoftbus::binding::NetworkBuffer;
    // Caller serializes ALL access to this list, including other runtimes.
    // Per-list remote ownership must be disjoint across DSoftBus runtimes.
    static SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&,
            const std::shared_ptr<RuntimeSendPath>&);
    static SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&,
            const std::shared_ptr<RuntimeSendPath>&, const std::shared_ptr<ViewDomainGuard>&);
    static SenderAttachResult AttachPending(eprosima::fastdds::rtps::SendResourceList&,
            const std::shared_ptr<RuntimeSenderBinding>&);
    static bool SetPreparation(const std::shared_ptr<RuntimeSenderBinding>&,
            std::function<RuntimeSendStatus(RuntimeSendPath::Clock::time_point)>);
    static bool BindPending(const std::shared_ptr<RuntimeSenderBinding>&,const std::shared_ptr<RuntimeSendPath>&);
    ~DSoftBusSenderResource() override;
    // Only first transition returns true. After return this resource cannot
    // publish. A send already holding the gate may finish before Release gets
    // it. Does not close the runtime/other senders or revoke admitted records.
    bool Release();
    bool released() const;
    bool belongs_to(const RuntimeIdentityHandle&) const noexcept;
    std::optional<RuntimeSendReport> last_report() const;
    void add_locators_to_list(fastdds_dsoftbus::binding::LocatorList_t&) const override;
    DSoftBusSenderResource(const DSoftBusSenderResource&) = delete;
    DSoftBusSenderResource& operator=(const DSoftBusSenderResource&) = delete;
    DSoftBusSenderResource(DSoftBusSenderResource&&) = delete;
    DSoftBusSenderResource& operator=(DSoftBusSenderResource&&) = delete;
private:
    friend class RuntimeSendPathTestAccess;
    explicit DSoftBusSenderResource(std::shared_ptr<RuntimeSendPath>,std::shared_ptr<RuntimeSenderBinding> = {});
    bool Send(const std::vector<NetworkBuffer>&, std::uint32_t,
            RuntimeSendPath::Iterator*, RuntimeSendPath::Iterator*,
            const RuntimeSendPath::Clock::time_point&, std::int32_t);
    const std::shared_ptr<RuntimeSendPath> runtime_;
    const std::shared_ptr<RuntimeSenderBinding> pending_;
    const std::shared_ptr<ViewDomainGuard> domain_guard_;
    EgressPlan::Snapshot plan() const noexcept { return pending_?pending_->plan():runtime_->plan(); }
    mutable std::mutex gate_;
    bool released_ = false;
    std::optional<RuntimeSendReport> last_report_;
    // Test-only observer, installed before publication; nonthrowing/nonreentrant.
    std::function<void()> on_release_;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
