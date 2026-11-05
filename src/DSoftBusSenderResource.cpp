#include <fastdds_dsoftbus/dsoftbus/DSoftBusSenderResource.hpp>

#include <utility>
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
#include "../probes/PublicSocketRuntimeTrace.hpp"
extern "C" void FastddsDsoftbusObserveSenderSubmission(std::uint64_t sender,
        const void* data,std::size_t bytes,bool decode_complete,int status,int queue_result,
        bool route_known,bool route_state_known,bool route_available,std::uint64_t route_revision) noexcept;
#endif

namespace fastdds_dsoftbus::dsoftbus {
DSoftBusSenderResource::DSoftBusSenderResource(std::shared_ptr<RuntimeSendPath> runtime,std::shared_ptr<RuntimeSenderBinding> pending)
    : SenderResource(LocatorCodec::kind), runtime_(std::move(runtime)),pending_(std::move(pending)),
      domain_guard_(pending_?pending_->guard():runtime_->DomainGuard())
{
    // Installed exactly once. Base send reads it outside our resource gate:
    // Release MUST NOT clear/reassign either std::function callback.
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    send_lambda_ = [this](const eprosima::fastrtps::rtps::octet* data, std::uint32_t bytes,
            RuntimeSendPath::Iterator* begin, RuntimeSendPath::Iterator* end,
            const RuntimeSendPath::Clock::time_point& deadline) {
        try {
            return Send(std::vector<NetworkBuffer>{NetworkBuffer(data, bytes)},
                    bytes, begin, end, deadline, 0);
        } catch (const std::bad_alloc&) {
            return false;
        }
    };
#elif FASTDDS_VERSION_MAJOR == 3 && FASTDDS_VERSION_MINOR < 4
    // Fast DDS 3.0-3.3 (ROS 2 Kilted ships 3.2.x): scatter/gather without a transport priority.
    send_buffers_lambda_ = [this](const std::vector<NetworkBuffer>& buffers, std::uint32_t bytes,
            RuntimeSendPath::Iterator* begin, RuntimeSendPath::Iterator* end,
            const RuntimeSendPath::Clock::time_point& deadline) {
        return Send(buffers, bytes, begin, end, deadline, 0);
    };
#else
    // Fast DDS >= 3.4 (3.6.x for ROS 2 Lyrical/Rolling) also passes the writer's transport priority.
    send_lambda_ = [this](const std::vector<NetworkBuffer>& buffers, std::uint32_t bytes,
            RuntimeSendPath::Iterator* begin, RuntimeSendPath::Iterator* end,
            const RuntimeSendPath::Clock::time_point& deadline, std::int32_t priority) {
        return Send(buffers, bytes, begin, end, deadline, priority);
    };
#endif
}
DSoftBusSenderResource::~DSoftBusSenderResource() { Release(); }
SenderAttachResult DSoftBusSenderResource::Attach(eprosima::fastdds::rtps::SendResourceList& resources,
        const std::shared_ptr<RuntimeSendPath>& runtime)
{ return Attach(resources, runtime, {}); }
SenderAttachResult DSoftBusSenderResource::Attach(eprosima::fastdds::rtps::SendResourceList& resources,
        const std::shared_ptr<RuntimeSendPath>& runtime, const std::shared_ptr<ViewDomainGuard>& guard)
{
    if (!runtime) return {SenderAttachStatus::InvalidRuntime};
    if (runtime->stop_requested()) return {SenderAttachStatus::RuntimeClosed};
    if (runtime->plan_->size() == 0) return {SenderAttachStatus::NoEgressRoutes};
    const auto inspect = [&]() -> std::optional<SenderAttachStatus> {
        for (const auto& resource : resources) {
            const auto* existing = dynamic_cast<const DSoftBusSenderResource*>(resource.get());
            if (!existing) continue;
            if (existing->belongs_to(runtime->identity_))
                return existing->released() ? SenderAttachStatus::ReleasedEntryPresent : SenderAttachStatus::AlreadyAttached;
            for (const auto& route : runtime->plan_->routes_) {
                if (existing->plan()->Find(route.key.remote_locator)) return SenderAttachStatus::OverlappingOwnership;
            }
        }
        return std::nullopt;
    };
    // Both scans are required: the first avoids allocations for an idempotent
    // attach; the second shares the final publication/stop-request gate.
    {
        std::lock_guard<std::mutex> lock(runtime->attachment_mutex_);
        if (runtime->stop_requested()) return {SenderAttachStatus::RuntimeClosed};
        if (!runtime->BindDomainGuardLocked(guard)) return {SenderAttachStatus::DomainGuardConflict};
        if (const auto status = inspect()) return {*status};
    }
    try {
        auto resource = std::unique_ptr<DSoftBusSenderResource>(new DSoftBusSenderResource(runtime));
        if (runtime->hooks_.before_attach_commit) runtime->hooks_.before_attach_commit();
        {
            std::lock_guard<std::mutex> lock(runtime->attachment_mutex_);
            if (runtime->stop_requested()) return {SenderAttachStatus::RuntimeClosed};
            if (const auto status = inspect()) return {*status};
            resources.push_back(std::move(resource));
        }
        return {SenderAttachStatus::Attached};
    } catch (const std::bad_alloc&) {
        return {SenderAttachStatus::AllocationFailure};
    }
}
bool DSoftBusSenderResource::SetPreparation(const std::shared_ptr<RuntimeSenderBinding>& pending,
        std::function<RuntimeSendStatus(RuntimeSendPath::Clock::time_point)> prepare) {
    if(!pending||!prepare)return false;
    std::lock_guard<std::mutex> lock(pending->gate_);
    if(pending->prepare_)return false;
    pending->prepare_=std::move(prepare);return true;
}
bool DSoftBusSenderResource::BindPending(const std::shared_ptr<RuntimeSenderBinding>& pending,
        const std::shared_ptr<RuntimeSendPath>& runtime) {
    if(!pending||!pending->guard_||!runtime)return false;
    std::lock_guard<std::mutex> bind(pending->gate_);
    if(const auto old=pending->target())return old==runtime;
    if(pending->guard_->Snapshot().state==ViewDomainState::Closed||runtime->stop_requested())return false;
    std::lock_guard<std::mutex> attach(runtime->attachment_mutex_);
    if(!runtime->BindDomainGuardLocked(pending->guard_))return false;
    std::atomic_store_explicit(&pending->target_,runtime,std::memory_order_release);return true;
}
SenderAttachResult DSoftBusSenderResource::AttachPending(eprosima::fastdds::rtps::SendResourceList& resources,
        const std::shared_ptr<RuntimeSenderBinding>& pending) {
    if(!pending||!pending->guard()||!pending->plan())return {SenderAttachStatus::InvalidRuntime};
    const auto inspect=[&]() -> std::optional<SenderAttachStatus> {
        for(const auto& resource:resources) {
            const auto* old=dynamic_cast<const DSoftBusSenderResource*>(resource.get());if(!old)continue;
            if(old->pending_==pending)return old->released()?SenderAttachStatus::ReleasedEntryPresent:SenderAttachStatus::AlreadyAttached;
            for(const auto& route:pending->plan()->routes_)if(old->plan()->Find(route.key.remote_locator))return SenderAttachStatus::OverlappingOwnership;
        }
        return std::nullopt;
    };
    if(pending->guard()->Snapshot().state==ViewDomainState::Closed)return {SenderAttachStatus::RuntimeClosed};
    if(const auto result=inspect())return {*result};
    try {
        auto resource=std::unique_ptr<DSoftBusSenderResource>(new DSoftBusSenderResource({},pending));
        if(pending->guard()->Snapshot().state==ViewDomainState::Closed)return {SenderAttachStatus::RuntimeClosed};
        if(const auto result=inspect())return {*result};
        resources.push_back(std::move(resource));return {SenderAttachStatus::Attached};
    }catch(const std::bad_alloc&){return {SenderAttachStatus::AllocationFailure};}
}
bool DSoftBusSenderResource::Send(const std::vector<NetworkBuffer>& buffers, std::uint32_t bytes,
        RuntimeSendPath::Iterator* begin, RuntimeSendPath::Iterator* end,
        const RuntimeSendPath::Clock::time_point& deadline, std::int32_t priority)
{
    auto runtime=pending_?pending_->target():runtime_;
    const bool expired_before_gate = (runtime?runtime->Now():RuntimeSendPath::Clock::now()) >= deadline;
    std::unique_lock<std::mutex> lock(gate_);
    auto prepared=RuntimeSendStatus::Accepted;
    // Snapshot readiness is not authority to enqueue. The real permit below is
    // acquired only after setup; no sender/binding/root gate crosses an SDK call.
    if(!released_&&!expired_before_gate&&pending_&&domain_guard_&&
            domain_guard_->Snapshot().state==ViewDomainState::Ready) {
        lock.unlock();
        try {
            std::function<RuntimeSendStatus(RuntimeSendPath::Clock::time_point)> prepare;
            {std::lock_guard<std::mutex> binding(pending_->gate_);prepare=pending_->prepare_;}
            if(prepare)prepared=prepare(deadline);
        }catch(...){prepared=RuntimeSendStatus::PreparationFailed;}
        lock.lock();runtime=pending_->target();
    }
    auto domain_permit = domain_guard_ ? domain_guard_->TryEnter(DomainOperation::Submission) : ViewDomainGuard::Permit{};
    if (released_) last_report_ = RuntimeSendReport{RuntimeSendStatus::Released};
    else if (expired_before_gate || (runtime?runtime->Now():RuntimeSendPath::Clock::now()) >= deadline)
        last_report_ = RuntimeSendReport{RuntimeSendStatus::DeadlineExpired};
    else if (!domain_permit) {
        const auto status = domain_permit.status();
        last_report_ = RuntimeSendReport{status == DomainAdmission::Awaiting ? RuntimeSendStatus::DomainAwaiting :
            status == DomainAdmission::Refused ? RuntimeSendStatus::DomainRefused :
            status == DomainAdmission::Closed ? RuntimeSendStatus::DomainClosed : RuntimeSendStatus::DomainCounterExhausted};
    } else if(prepared!=RuntimeSendStatus::Accepted) last_report_=RuntimeSendReport{prepared};
    else if(!runtime) last_report_=RuntimeSendReport{RuntimeSendStatus::DomainAwaiting};
    else last_report_ = runtime->Submit(buffers, bytes, begin, end, deadline, priority);
    const RuntimeSendReport original=*last_report_;
    lock.unlock();
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
    // Process-local diagnostic token only. Never an owner, admission capability,
    // device identity or SDK event epoch. Capture keeps no payload pointer.
    runtime_probe::TraceEvent("Sender_submit_result",reinterpret_cast<std::uintptr_t>(this),
            -1,-1,static_cast<int>(original.status),bytes);
    // The observer reads only a complete original single-buffer frame. Other
    // layouts remain explicitly uncorrelated; do not flatten/copy/reframe them.
    const bool complete=buffers.size()==1&&buffers.front().buffer&&buffers.front().size>=bytes;
    FastddsDsoftbusObserveSenderSubmission(reinterpret_cast<std::uintptr_t>(this),
            complete?buffers.front().buffer:nullptr,bytes,complete,static_cast<int>(original.status),
            original.queue_result?static_cast<int>(*original.queue_result):-1,
            original.unresolved_route.has_value(),original.unresolved_route&&original.unresolved_route->route_state_known,
            original.unresolved_route&&original.unresolved_route->route_available,
            original.unresolved_route?original.unresolved_route->route_version:0);
#endif
    return original.status == RuntimeSendStatus::Accepted;
}
bool DSoftBusSenderResource::Release()
{
    std::lock_guard<std::mutex> lock(gate_);
    if (released_) return false;
    released_ = true;
    if (on_release_) on_release_();
    return true;
}
bool DSoftBusSenderResource::released() const { std::lock_guard<std::mutex> lock(gate_); return released_; }
bool DSoftBusSenderResource::belongs_to(const RuntimeIdentityHandle& identity) const noexcept
{ return runtime_ && runtime_->identity() == identity; }
std::optional<RuntimeSendReport> DSoftBusSenderResource::last_report() const
{ std::lock_guard<std::mutex> lock(gate_); return last_report_; }
void DSoftBusSenderResource::add_locators_to_list(fastdds_dsoftbus::binding::LocatorList_t& locals) const
{
    std::lock_guard<std::mutex> lock(gate_);
    if (!released_) for (const auto& local : plan()->selected_locals()) locals.push_back(local);
}
} // namespace fastdds_dsoftbus::dsoftbus
