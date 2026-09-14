#ifndef MDDS_DSOFTBUS_SENDER_RESOURCE_HPP
#define MDDS_DSOFTBUS_SENDER_RESOURCE_HPP

#include <mdds/dsoftbus/RuntimeSendPath.hpp>
#include <fastdds/rtps/transport/TransportInterface.hpp>

namespace mdds::dsoftbus {
enum class SenderAttachStatus
{
    Attached, AlreadyAttached, InvalidRuntime, RuntimeClosed, NoEgressRoutes,
    ReleasedEntryPresent, OverlappingOwnership, AllocationFailure
};
struct SenderAttachResult
{
    SenderAttachStatus status;
    bool ok() const noexcept
    { return status == SenderAttachStatus::Attached || status == SenderAttachStatus::AlreadyAttached; }
};

class DSoftBusSenderResource final : public eprosima::fastdds::rtps::SenderResource
{
public:
    // Caller serializes ALL access to this list, including other runtimes.
    // Per-list remote ownership must be disjoint across DSoftBus runtimes.
    static SenderAttachResult Attach(eprosima::fastdds::rtps::SendResourceList&,
            const std::shared_ptr<RuntimeSendPath>&);
    ~DSoftBusSenderResource() override;
    // Only first transition returns true. After return this resource cannot
    // publish. A send already holding the gate may finish before Release gets
    // it. Does not close the runtime/other senders or revoke admitted records.
    bool Release();
    bool released() const;
    bool belongs_to(const RuntimeIdentityHandle&) const noexcept;
    std::optional<RuntimeSendReport> last_report() const;
    void add_locators_to_list(eprosima::fastdds::rtps::LocatorList_t&) const override;
    DSoftBusSenderResource(const DSoftBusSenderResource&) = delete;
    DSoftBusSenderResource& operator=(const DSoftBusSenderResource&) = delete;
    DSoftBusSenderResource(DSoftBusSenderResource&&) = delete;
    DSoftBusSenderResource& operator=(DSoftBusSenderResource&&) = delete;
private:
    friend class RuntimeSendPathTestAccess;
    explicit DSoftBusSenderResource(std::shared_ptr<RuntimeSendPath>);
    bool Send(const std::vector<NetworkBuffer>&, std::uint32_t,
            RuntimeSendPath::Iterator*, RuntimeSendPath::Iterator*,
            const RuntimeSendPath::Clock::time_point&, std::int32_t);
    const std::shared_ptr<RuntimeSendPath> runtime_;
    mutable std::mutex gate_;
    bool released_ = false;
    std::optional<RuntimeSendReport> last_report_;
    // Test-only observer, installed before publication; nonthrowing/nonreentrant.
    std::function<void()> on_release_;
};
} // namespace mdds::dsoftbus
#endif
