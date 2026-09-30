#include <fastdds_dsoftbus/dsoftbus/DSoftBusSenderResource.hpp>

#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
DSoftBusSenderResource::DSoftBusSenderResource(std::shared_ptr<RuntimeSendPath> runtime)
    : SenderResource(LocatorCodec::kind), runtime_(std::move(runtime))
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
#else
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
                if (existing->runtime_->plan_->Find(route.key.remote_locator)) return SenderAttachStatus::OverlappingOwnership;
            }
        }
        return std::nullopt;
    };
    // Both scans are required: the first avoids allocations for an idempotent
    // attach; the second shares the final publication/stop-request gate.
    {
        std::lock_guard<std::mutex> lock(runtime->attachment_mutex_);
        if (runtime->stop_requested()) return {SenderAttachStatus::RuntimeClosed};
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
bool DSoftBusSenderResource::Send(const std::vector<NetworkBuffer>& buffers, std::uint32_t bytes,
        RuntimeSendPath::Iterator* begin, RuntimeSendPath::Iterator* end,
        const RuntimeSendPath::Clock::time_point& deadline, std::int32_t priority)
{
    const bool expired_before_gate = runtime_->Now() >= deadline;
    std::lock_guard<std::mutex> lock(gate_);
    if (released_) last_report_ = RuntimeSendReport{RuntimeSendStatus::Released};
    else if (expired_before_gate || runtime_->Now() >= deadline)
        last_report_ = RuntimeSendReport{RuntimeSendStatus::DeadlineExpired};
    else last_report_ = runtime_->Submit(buffers, bytes, begin, end, deadline, priority);
    return last_report_->status == RuntimeSendStatus::Accepted;
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
{ return runtime_->identity() == identity; }
std::optional<RuntimeSendReport> DSoftBusSenderResource::last_report() const
{ std::lock_guard<std::mutex> lock(gate_); return last_report_; }
void DSoftBusSenderResource::add_locators_to_list(fastdds_dsoftbus::binding::LocatorList_t& locals) const
{
    std::lock_guard<std::mutex> lock(gate_);
    if (!released_) for (const auto& local : runtime_->plan()->selected_locals()) locals.push_back(local);
}
} // namespace fastdds_dsoftbus::dsoftbus
