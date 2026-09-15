#ifndef MDDS_DSOFTBUS_INPUT_CHANNELS_HPP
#define MDDS_DSOFTBUS_INPUT_CHANNELS_HPP

#include <mdds/dsoftbus/EndpointDirectory.hpp>
#include <fastdds/rtps/transport/TransportReceiverInterface.hpp>

#include <functional>
#include <memory>

namespace mdds::dsoftbus {
namespace detail {
struct InputCoreIdentity;
struct InputChannelEntry;
struct InputChannelsState;
}
class InputChannelsTestAccess;

// Opaque channel-incarnation evidence, not peer authentication. Retaining a
// ticket retains identity/entry metadata, never ownership of the raw receiver.
// External dispatchers must bound their retained tickets and queued bytes.
class InputChannelTicket final
{
public:
    InputChannelTicket() noexcept = default;
private:
    friend class InputChannels;
    friend class InputChannelsTestAccess;
    std::shared_ptr<const detail::InputCoreIdentity> identity_;
    std::shared_ptr<detail::InputChannelEntry> entry_;
};

enum class InputOpenResult
{
    Opened, AlreadyOpen, InvalidLocal, NullReceiver, Conflict, Closing,
    Disabled, AllocationFailure
};

enum class InputDeliveryResult
{
    Delivered, InvalidTicket, InvalidRoute, InvalidBuffer, TooLarge,
    Closed, Disabled, CallbackFailure, AllocationFailure
};

struct InputChannelUsage
{
    std::size_t open_channels = 0;
    std::size_t closing_channels = 0;
    std::size_t in_flight = 0;
    bool disabled = false;
    bool drained = false;
};

// One already-admitted use of the original receiver. This is NOT a queued
// identity pin: acquire only at final delivery admission. Close/DisableAll and
// destruction wait for it even if Deliver has not started. Keep the receiver
// alive until that drain completes. Never synchronously drain on its own stack.
// A lease is exclusively owned: no concurrent Deliver, move or destruction.
class InputReceiverLease final
{
public:
    InputReceiverLease() noexcept = default;
    ~InputReceiverLease();
    InputReceiverLease(InputReceiverLease&&) noexcept;
    InputReceiverLease& operator=(InputReceiverLease&&) noexcept;
    InputReceiverLease(const InputReceiverLease&) = delete;
    InputReceiverLease& operator=(const InputReceiverLease&) = delete;
    explicit operator bool() const noexcept { return bool(state_); }
    // Consumes the permit on every outcome. No current-receiver lookup, no
    // retry, no locks around OnDataReceived; borrows expire at return/unwind.
    InputDeliveryResult Deliver(const Locator& remote, const void* data, std::size_t size);
private:
    friend class InputChannels;
    explicit InputReceiverLease(InputDeliveryResult failure) noexcept : failure_(failure) {}
    InputReceiverLease(std::shared_ptr<detail::InputChannelsState>,
            std::shared_ptr<detail::InputChannelEntry>) noexcept;
    void Release() noexcept;
    InputDeliveryResult InvokeValidated(const Locator&, const void*, std::size_t);
    std::shared_ptr<detail::InputChannelsState> state_;
    std::shared_ptr<detail::InputChannelEntry> entry_;
    InputDeliveryResult failure_ = InputDeliveryResult::InvalidTicket;
};

class InputChannels final
{
public:
    using Receiver = eprosima::fastdds::rtps::TransportReceiverInterface;

    // Throws invalid_argument for a null snapshot or limit outside 1..UINT32_MAX.
    InputChannels(EndpointDirectory::Snapshot directory, std::size_t max_record_bytes);
    ~InputChannels();
    InputChannels(const InputChannels&) = delete;
    InputChannels& operator=(const InputChannels&) = delete;
    InputChannels(InputChannels&&) = delete;
    InputChannels& operator=(InputChannels&&) = delete;

    // One raw receiver per configured canonical local. Same local/pointer is
    // idempotent; aliases across locals conflict until the old entry drains.
    // Allocation failure leaves all existing registrations intact.
    InputOpenResult Open(const Locator& local, Receiver* receiver);
    InputChannelTicket Capture(const Locator& local) const;

    // Nonallocating short admission against this exact core/entry incarnation.
    // The returned lease increments the same real counters used by Close.
    // Releasing/replacing it must happen outside higher-level coordination locks.
    InputReceiverLease TryAcquireReceiver(const InputChannelTicket& ticket);

    // Bytes remain valid and unchanged until this synchronous call returns.
    // All core locks are released before the real OnDataReceived call.
    // Delivered means only callback completion, not RTPS/sample acceptance.
    // CallbackFailure may follow partial receiver effects; it is never retried.
    // AllocationFailure during route lookup invokes no receiver and acquires
    // no in-flight lease. It is distinct from an invalid configured route.
    InputDeliveryResult Deliver(const InputChannelTicket& ticket,
            const Locator& remote, const void* data, std::size_t size);

    // External control path only: neither these calls nor destruction may run
    // synchronously on ANY receiver callback stack (including other channels).
    // Close captures the current entry and drains only that incarnation. Its
    // completion linearizes under lock after drain/receiver clearing; a prior
    // waiter cannot affect a replacement. A NEW key-only Close after reopen
    // targets the new registration; delayed control tasks must preserve identity
    // or be serialized by the future transport control plane.
    void Close(const Locator& local);
    // Permanent admission stop; EVERY caller waits for all accepted callbacks,
    // including entries already Closing. This is not SDK callback cancellation.
    void DisableAll();
    InputChannelUsage SnapshotUsage() const;

    // The owner keeps this object alive throughout every member call; concurrent
    // destruction is unsupported. It keeps raw receivers alive through their
    // full accepted registration/delivery set, including across other cores.
    // This core never deletes a receiver. Close has no artificial timeout.
private:
    friend class InputChannelsTestAccess;
    struct TestHooks
    {
        // Fault injection is nonblocking and nonreentrant (publication lock held).
        std::function<void()> before_publish;
        // Synchronization hooks run without core locks and must not throw.
        std::function<void(const Locator&)> after_close_capture;
        std::function<void()> after_disable_admission;
    };
    InputChannels(EndpointDirectory::Snapshot directory,
            std::size_t max_record_bytes, TestHooks hooks);
    std::shared_ptr<detail::InputChannelsState> state_;
    TestHooks hooks_;
};

} // namespace mdds::dsoftbus
#endif
