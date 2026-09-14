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
    std::unique_ptr<detail::InputChannelsState> state_;
    TestHooks hooks_;
};

} // namespace mdds::dsoftbus
#endif
