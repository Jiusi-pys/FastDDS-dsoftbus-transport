#ifndef MDDS_DSOFTBUS_NATIVE_SEND_EXECUTION_HPP
#define MDDS_DSOFTBUS_NATIVE_SEND_EXECUTION_HPP

#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <mdds/dsoftbus/DSoftBusSenderResource.hpp>

namespace mdds::dsoftbus {
class NativeSendExecutionTestAccess;
class ProcessCoordinator;
namespace detail {
class EgressState;
class NativeSendWorkState;
class NativeSendInvocationState;
class NativeSendResources;
}

// Model seam only. Send is synchronous: all immutable borrows expire at return
// or unwind. No hidden asynchronous use, invocation destruction or move is
// permitted from Send. This interface is not a verified Socket provider.
class NativeSendProvider
{
public:
    virtual ~NativeSendProvider() = default;
    virtual std::int32_t Send(NativeSocketId, const Route&, const std::uint8_t*,
            std::size_t, SendQueue::Clock::time_point, std::int32_t priority) = 0;
};
enum class NativeSendOutcome { NotEntered, ReturnedSdkCode, ReturnedInvalidCode, ReturnedByException };
struct NativeSendRunResult
{
    NativeStatus status = NativeStatus::NotImplemented;
    NativeSendOutcome outcome = NativeSendOutcome::NotEntered;
    std::optional<std::int32_t> actual_return_code;
};
struct NativeEgressUsage
{
    std::size_t runtimes = 0, records = 0, bytes = 0;
    static constexpr std::size_t runtime_limit = 16, record_limit = 2048, byte_limit = 16 * 1024 * 1024;
};
struct NativeEgressActivity
{
    NativeStatus status = NativeStatus::InvalidHandle;
    std::size_t admitted_outstanding = 0;
    std::uint64_t submission_inflight = 0;
    bool admission_closed = false, queue_barrier_crossed = false, drained = false;
};

// BeginRevoke freezes only. Progress, destruction and move assignment perform
// queue closure and cancellation cleanup OUTSIDE all coordinator/native gates.
// They do not wait for provider return. Never await one's own execution/cleanup.
// As with an invocation, accesses to one ticket object must be serialized.
class NativeRevokeTicket final
{
public:
    NativeRevokeTicket() noexcept = default;
    ~NativeRevokeTicket() noexcept;
    NativeRevokeTicket(NativeRevokeTicket&&) noexcept;
    NativeRevokeTicket& operator=(NativeRevokeTicket&&) noexcept;
    NativeRevokeTicket(const NativeRevokeTicket&) = delete;
    NativeRevokeTicket& operator=(const NativeRevokeTicket&) = delete;
    void Progress() noexcept;
private:
    friend class NativeSendExecution;
    friend class detail::NativeEgressBridge;
    std::shared_ptr<detail::EgressState> registration_;
    std::array<std::shared_ptr<detail::NativeSendInvocationState>, 2> controls_{};
    std::array<std::shared_ptr<detail::NativeSendResources>, 2> resources_{};
};

// Copies refer to one registration; no public runtime pointer or raw record.
class EgressRegistration
{
public:
    EgressRegistration() noexcept = default;
    explicit operator bool() const noexcept;
    RuntimeIdentityHandle identity() const noexcept;
    SendQueueUsage SnapshotUsage() const;
private:
    friend class NativeSendExecution;
    friend class NativeSendExecutionTestAccess;
    explicit EgressRegistration(std::shared_ptr<detail::EgressState>) noexcept;
    std::shared_ptr<detail::EgressState> state_;
};
class NativeSendWork
{
public:
    NativeSendWork() noexcept;
    ~NativeSendWork();
    NativeSendWork(NativeSendWork&&) noexcept;
    NativeSendWork& operator=(NativeSendWork&&) noexcept;
    NativeSendWork(const NativeSendWork&) = delete;
    NativeSendWork& operator=(const NativeSendWork&) = delete;
    explicit operator bool() const noexcept;
private:
    friend class NativeSendExecution;
    explicit NativeSendWork(std::unique_ptr<detail::NativeSendWorkState>) noexcept;
    std::unique_ptr<detail::NativeSendWorkState> state_;
};
class NativeSendInvocation
{
public:
    NativeSendInvocation() noexcept;
    ~NativeSendInvocation();
    NativeSendInvocation(NativeSendInvocation&&) noexcept;
    NativeSendInvocation& operator=(NativeSendInvocation&&) noexcept;
    NativeSendInvocation(const NativeSendInvocation&) = delete;
    NativeSendInvocation& operator=(const NativeSendInvocation&) = delete;
    // Caller exclusively owns this object throughout Run; no concurrent Run,
    // move or destruction. Revoke/owner close may run on other objects.
    NativeSendRunResult Run() noexcept;
private:
    friend class NativeSendExecution;
    explicit NativeSendInvocation(std::shared_ptr<detail::NativeSendInvocationState>) noexcept;
    std::shared_ptr<detail::NativeSendInvocationState> state_;
};
class NativeSendExecution final
{
public:
    explicit NativeSendExecution(NativeOwnerTable&) noexcept;
    NativeSendExecution(const NativeSendExecution&) = delete;
    NativeSendExecution& operator=(const NativeSendExecution&) = delete;
    NativeSendExecution(NativeSendExecution&&) = delete;
    NativeSendExecution& operator=(NativeSendExecution&&) = delete;
    // Destruction drops only the original State lease, never closes owners.
    NativeResult<EgressRegistration> CreateRuntime(const NativeTableConfiguration&,
            const std::vector<EgressSelection>&, SendQueueLimits) const noexcept;
    // Registration value pins its lifetime across concurrent control calls.
    // Caller must serialize every access to its SendResourceList.
    SenderAttachResult Attach(EgressRegistration, eprosima::fastdds::rtps::SendResourceList&) const;
    NativeResult<NativeSendWork> Take(EgressRegistration) const noexcept;
    // Failure preserves work, bytes and original deadline, including Busy.
    NativeResult<NativeSendInvocation> TryAdmit(NativeSendWork&,
            std::shared_ptr<NativeSendProvider>) const noexcept;
    NativeStatus Revoke(EgressRegistration) const noexcept;
    // Strong application boundary; legacy Revoke retains its N2 contract.
    NativeResult<NativeRevokeTicket> BeginRevoke(EgressRegistration) const noexcept;
    NativeEgressActivity Observe(EgressRegistration) const noexcept;
    NativeEgressUsage SnapshotUsage() const noexcept;
private:
    friend class NativeSendExecutionTestAccess;
    friend class ProcessCoordinator;
    // Coordinator supplies only its already-reserved application local set.
    // The unchanged full native configuration is validated before projection.
    NativeResult<EgressRegistration> CreateRuntimeForLocals(const NativeTableConfiguration&,
            const std::vector<Locator>&,const std::vector<EgressSelection>&,SendQueueLimits) const noexcept;
    NativeResult<EgressRegistration> CreateRuntimeImpl(const NativeTableConfiguration&,
            const std::vector<Locator>*,const std::vector<EgressSelection>&,SendQueueLimits) const noexcept;
    std::shared_ptr<detail::NativeState> state_;
};
} // namespace mdds::dsoftbus
#endif
