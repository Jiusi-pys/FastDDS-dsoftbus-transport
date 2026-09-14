#ifndef MDDS_DSOFTBUS_NATIVE_SEND_EXECUTION_HPP
#define MDDS_DSOFTBUS_NATIVE_SEND_EXECUTION_HPP

#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <mdds/dsoftbus/DSoftBusSenderResource.hpp>

namespace mdds::dsoftbus {
class NativeSendExecutionTestAccess;
namespace detail {
class EgressState;
class NativeSendWorkState;
class NativeSendInvocationState;
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
    explicit NativeSendInvocation(std::unique_ptr<detail::NativeSendInvocationState>) noexcept;
    std::unique_ptr<detail::NativeSendInvocationState> state_;
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
    NativeEgressUsage SnapshotUsage() const noexcept;
private:
    friend class NativeSendExecutionTestAccess;
    std::shared_ptr<detail::NativeState> state_;
};
} // namespace mdds::dsoftbus
#endif
