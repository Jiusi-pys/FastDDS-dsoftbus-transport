#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_RUNTIME_SEND_PATH_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_RUNTIME_SEND_PATH_HPP

#include <fastdds_dsoftbus/dsoftbus/SendQueue.hpp>
#include <fastdds/rtps/common/LocatorsIterator.hpp>
#include <fastdds_dsoftbus/dsoftbus/FastddsApi.hpp>

#include <atomic>
#include <mutex>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail { struct RuntimeIdentity; class NativeEgressBridge; }
class DSoftBusSenderResource;
class RuntimeSendPathTestAccess;

struct EgressSelection
{
    Locator remote_locator;
    Locator local_locator;
    std::uint64_t expected_route_version = 0;
};
enum class EgressError
{
    None, DirectoryRejected, InvalidSelection, DuplicateSelection,
    UnknownSelection, StaleSelection, AmbiguousEgress, AllocationFailure
};
struct EgressIssue
{
    EgressError code = EgressError::None;
    std::size_t selection_index = static_cast<std::size_t>(-1);
    std::size_t route_index = static_cast<std::size_t>(-1);
    DirectoryIssue directory_issue{};
};
struct EgressBuildResult;

class EgressPlan final
{
public:
    using Snapshot = std::shared_ptr<const EgressPlan>;
    // Inputs remain valid and unchanged during Build. All selected values and
    // the validated directory are owned immutable copies after success.
    static EgressBuildResult Build(const DirectoryConfiguration&,
            const std::vector<EgressSelection>& selections);
    const Route* Find(const Locator& remote) const noexcept;
    std::size_t size() const noexcept;
    EndpointDirectory::Snapshot directory() const noexcept;
    const std::vector<Locator>& selected_locals() const noexcept;
    EgressPlan(const EgressPlan&) = delete;
    EgressPlan& operator=(const EgressPlan&) = delete;
private:
    friend class DSoftBusSenderResource;
    EgressPlan(EndpointDirectory::Snapshot, const std::vector<Route>&, const std::vector<Locator>&);
    EndpointDirectory::Snapshot directory_;
    const std::vector<Route> routes_;
    const std::vector<Locator> locals_;
};
struct EgressBuildResult { EgressPlan::Snapshot value; EgressIssue issue; };

struct SendConnectionSnapshot
{
    RouteKey key;
    std::uint64_t route_version = 0;
    std::uint64_t generation = 0;
};
class ConnectionGenerationSource
{
public:
    virtual ~ConnectionGenerationSource() = default;
    // Thread-safe, nonblocking lookup of an already-known snapshot. No SDK,
    // Bind/network wait, permission writes or sender/runtime reentry. This
    // value is NOT a Socket lease or proof of native callback quiescence.
    virtual bool TryResolve(const Route& selected_route, SendConnectionSnapshot& output) const = 0;
};

using RuntimeIdentityHandle = std::shared_ptr<const detail::RuntimeIdentity>;
enum class RuntimeSendStatus
{
    Accepted, Released, RuntimeClosed, DeadlineExpired, InvalidInput, InvalidDestination,
    NoOwnedTargets, UnresolvedConnection, InvalidConnectionSnapshot, PreparationFailed, QueueRejected
};
struct RuntimeSendReport
{
    RuntimeSendStatus status = RuntimeSendStatus::InvalidInput;
    std::size_t visited = 0, owned_unique = 0, duplicates = 0, ignored = 0;
    std::optional<SendAdmission> queue_result;
};

class RuntimeSendPath final
{
public:
    using Clock = SendQueue::Clock;
    using NetworkBuffer = fastdds_dsoftbus::binding::NetworkBuffer;
    using Iterator = eprosima::fastdds::rtps::LocatorsIterator;
    RuntimeSendPath(EgressPlan::Snapshot, SendQueueLimits,
            std::shared_ptr<const ConnectionGenerationSource>);
    ~RuntimeSendPath();
    RuntimeSendPath(const RuntimeSendPath&) = delete;
    RuntimeSendPath& operator=(const RuntimeSendPath&) = delete;
    RuntimeIdentityHandle identity() const noexcept;
    EgressPlan::Snapshot plan() const noexcept;
    bool stop_requested() const noexcept;
    // Inputs/finite nonblocking iterator range remain valid throughout this
    // call; begin is consumed. Only this runtime's nonempty owned batch is
    // admitted. The original deadline is never renewed; the queue's 20 ms cap
    // applies only to queue admission, NOT the complete caller duration.
    // Direct Submit is runtime-scoped; a SenderResource's Release only gates
    // calls through that resource, not direct calls or other resources.
    RuntimeSendReport Submit(const std::vector<NetworkBuffer>&, std::uint32_t total_bytes,
            Iterator* begin, Iterator* end, Clock::time_point original_deadline, std::int32_t priority);
    std::optional<OwnedSendRecord> TryTake();
    SendQueueUsage SnapshotUsage() const;
    // Every caller crosses the queue publication barrier; this does not drain
    // any SDK worker or revoke taken records. Keep the object alive during calls.
    void Close() noexcept;
private:
    friend class DSoftBusSenderResource;
    friend class RuntimeSendPathTestAccess;
    friend class detail::NativeEgressBridge;
    // Single atomic arbitration: a producer either enters before Freeze or
    // cannot enter. These narrow operations never drain or destroy records.
    static constexpr std::uint64_t closed_bit_ = std::uint64_t{1} << 63;
    void FreezeAdmissions() noexcept { producer_state_.fetch_or(closed_bit_); }
    std::uint64_t ProducerCount() const noexcept { return producer_state_.load() & (closed_bit_ - 1); }
    bool TryEnterProducer() noexcept;
    struct TestHooks
    {
        std::function<Clock::time_point()> now; // Nonthrowing and monotonic.
        std::function<void()> before_attach_commit; // Outside attachment gate.
        std::function<void()> after_close_request; // Outside attachment gate; nonthrowing.
    };
    RuntimeSendPath(EgressPlan::Snapshot, SendQueueLimits,
            std::shared_ptr<const ConnectionGenerationSource>, std::unique_ptr<SendQueue>, TestHooks);
    Clock::time_point Now() const noexcept;
    const EgressPlan::Snapshot plan_;
    const std::shared_ptr<const ConnectionGenerationSource> generations_;
    const RuntimeIdentityHandle identity_;
    const std::size_t max_record_bytes_;
    std::unique_ptr<SendQueue> queue_;
    mutable std::mutex attachment_mutex_;
    std::atomic<std::uint64_t> producer_state_{0};
    TestHooks hooks_;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
