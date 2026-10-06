#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_SEND_QUEUE_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_SEND_QUEUE_HPP

#include <fastdds_dsoftbus/dsoftbus/EndpointDirectory.hpp>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail { struct SendQueueState; }
class SendQueueTestAccess;

struct SendBufferView
{
    const void* data = nullptr;
    std::size_t size = 0;
};

struct SendTargetView
{
    // Borrowed only for the duration of EnqueueBatch; never stored in a record.
    const Route* route = nullptr;
    std::uint64_t generation = 0;
    // New submissions capture the current authority version; old queued work
    // never refreshes its original tag. Zero preserves static-fixture behavior.
    std::uint64_t route_version = 0;
};

struct SendQueueLimits
{
    // Experimental payload limits, NOT an RSS or SDK capability guarantee.
    // Peer budgets count complete remote endpoints (kind/address/profile/slot).
    std::size_t peer_records = 32;
    std::size_t peer_bytes = 256 * 1024;
    std::size_t global_records = 128;
    std::size_t global_bytes = 1024 * 1024;
    // Required from the caller's independently established record capability.
    std::size_t max_record_bytes = 0;
};

enum class SendAdmission
{
    Accepted, InvalidRoute, InvalidBuffers, TooLarge, DeadlineExpired, Closed, CopyFailed
};

struct SendQueueUsage
{
    // Includes reservations, queued records AND records already taken out.
    std::size_t records = 0;
    std::size_t bytes = 0;
    std::size_t peers = 0;
    std::size_t queued = 0;
    bool closed = false;
};

class OwnedSendRecord final
{
public:
    using Clock = std::chrono::steady_clock;
    ~OwnedSendRecord();
    OwnedSendRecord(OwnedSendRecord&&) noexcept;
    OwnedSendRecord& operator=(OwnedSendRecord&&) noexcept;
    OwnedSendRecord(const OwnedSendRecord&) = delete;
    OwnedSendRecord& operator=(const OwnedSendRecord&) = delete;

    // Borrowed immutable views: valid only while this non-moved-from record
    // lives. No payload owner/copy is exported; SDK lifetime remains external.
    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;
    const Route& route() const;
    std::uint64_t generation() const noexcept;
    std::int32_t priority() const noexcept;
    Clock::time_point deadline() const noexcept;

private:
    friend class SendQueue;
    friend struct detail::SendQueueState;
    struct Storage;
    explicit OwnedSendRecord(std::unique_ptr<Storage> storage) noexcept;
    std::unique_ptr<Storage> storage_;
};

class SendQueue final
{
public:
    using Clock = OwnedSendRecord::Clock;
    explicit SendQueue(SendQueueLimits limits);
    ~SendQueue();
    SendQueue(const SendQueue&) = delete;
    SendQueue& operator=(const SendQueue&) = delete;
    SendQueue(SendQueue&&) = delete;
    SendQueue& operator=(SendQueue&&) = delete;

    // Single-target admission only, not SDK submission or delivery. Inputs
    // must stay valid and unchanged throughout the call. Route is a trusted
    // directory result; validation here does not authenticate/resolve peers.
    // Copy exactly total_bytes from the buffer prefix; ignore unused suffixes.
    // Entry + 20 ms and the original deadline bound one admission attempt.
    SendAdmission Enqueue(const Route& route, std::uint64_t generation,
            std::int32_t priority, Clock::time_point original_deadline,
            const SendBufferView* buffers, std::size_t buffer_count,
            std::size_t total_bytes);

    // Atomic admission of a nonempty set of distinct remote identities. All
    // routes, views and buffers must remain valid and unchanged for this call.
    // Each target owns its own payload/lease. Accept all targets or publish
    // none; this is NOT atomic SDK submission/delivery. One entry + 20 ms /
    // original-deadline cutoff covers validation, waiting and all copies.
    SendAdmission EnqueueBatch(const SendTargetView* targets, std::size_t target_count,
            std::int32_t priority, Clock::time_point original_deadline,
            const SendBufferView* buffers, std::size_t buffer_count,
            std::size_t total_bytes);

    // Nonblocking. Discard records whose ORIGINAL deadline has expired.
    // FIFO is successful publication order, not reservation arrival order.
    std::optional<OwnedSendRecord> TryTake();
    // Idempotent; rejects later publication and discards queued work. Taken
    // records retain their bytes and capacity lease, even after queue death.
    void Close() noexcept;
    SendQueueUsage SnapshotUsage() const;

    // All member calls require the owner to keep this object alive. A shared
    // accounting state does NOT permit destruction concurrent with a call.
private:
    friend class SendQueueTestAccess;
    struct TestHooks
    {
        // Explicit deterministic tests only. now is nonthrowing/monotonic;
        // on_wait is nonblocking and must not reenter this queue (lock held).
        std::function<Clock::time_point()> now;
        std::function<void()> on_wait;
        std::function<void()> before_copy;
        std::function<void()> after_copy;
    };
    SendQueue(SendQueueLimits limits, TestHooks hooks);
    Clock::time_point Now() const noexcept;
    std::shared_ptr<detail::SendQueueState> state_;
    TestHooks hooks_;
};

} // namespace fastdds_dsoftbus::dsoftbus
#endif
