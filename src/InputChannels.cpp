#include <mdds/dsoftbus/InputChannels.hpp>

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mdds::dsoftbus {
namespace detail {
struct InputCoreIdentity final {};

struct InputChannelEntry final
{
    enum class Phase { Open, Closing, Closed };
    InputChannelEntry(const Locator& locator, InputChannels::Receiver* target)
        : local(locator), receiver(target) {}
    const Locator local;
    // All mutable entry fields are protected by the owning core's mutex.
    InputChannels::Receiver* receiver;
    Phase phase = Phase::Open;
    std::size_t in_flight = 0;
};

struct InputChannelsState final
{
    InputChannelsState(EndpointDirectory::Snapshot snapshot, std::size_t limit)
        : directory(std::move(snapshot)), max_record_bytes(limit),
          identity(std::make_shared<const InputCoreIdentity>()),
          // MSVC Debug default vector construction is noexcept but allocates
          // an iterator proxy. The count constructor propagates its OOM.
          entries(std::size_t{0}) {}
    const EndpointDirectory::Snapshot directory;
    const std::size_t max_record_bytes;
    const std::shared_ptr<const InputCoreIdentity> identity;
    mutable std::mutex mutex;
    std::condition_variable changed;
    // Only current Open/Closing entries are retained; tickets own any history.
    std::vector<std::shared_ptr<InputChannelEntry>> entries;
    std::size_t in_flight = 0;
    bool disabled = false;
    bool drained = false;

    auto Find(const Locator& local)
    {
        return std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return entry->local == local;
        });
    }
};
} // namespace detail

namespace {
// Construction copies only a shared_ptr, so acquiring a lease cannot allocate
// or throw between incrementing the counters and installing the scope guard.
class InputDeliveryLease final
{
public:
    InputDeliveryLease(detail::InputChannelsState& state,
            const std::shared_ptr<detail::InputChannelEntry>& entry) noexcept
        : state_(state), entry_(entry) {}
    ~InputDeliveryLease()
    {
        std::lock_guard<std::mutex> lock(state_.mutex);
        --entry_->in_flight;
        --state_.in_flight;
        state_.changed.notify_all();
    }
    InputDeliveryLease(const InputDeliveryLease&) = delete;
    InputDeliveryLease& operator=(const InputDeliveryLease&) = delete;
private:
    detail::InputChannelsState& state_;
    const std::shared_ptr<detail::InputChannelEntry> entry_;
};
} // namespace

InputChannels::InputChannels(EndpointDirectory::Snapshot directory, std::size_t limit)
    : InputChannels(std::move(directory), limit, {}) {}

InputChannels::InputChannels(EndpointDirectory::Snapshot directory,
        std::size_t limit, TestHooks hooks) : hooks_(std::move(hooks))
{
    if (!directory || limit == 0 || limit > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("InputChannels requires a directory and a 1..UINT32_MAX record limit");
    }
    state_ = std::make_unique<detail::InputChannelsState>(std::move(directory), limit);
}

InputChannels::~InputChannels() { DisableAll(); }

InputOpenResult InputChannels::Open(const Locator& local, Receiver* receiver)
{
    if (!receiver) return InputOpenResult::NullReceiver;
    auto& state = *state_;
    try {
        const auto configured = state.directory->FindLocal(local);
        if (!configured.value) return InputOpenResult::InvalidLocal;
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.disabled) return InputOpenResult::Disabled;
        const auto found = state.Find(local);
        if (found != state.entries.end()) {
            if ((*found)->phase != detail::InputChannelEntry::Phase::Open) return InputOpenResult::Closing;
            return (*found)->receiver == receiver ? InputOpenResult::AlreadyOpen : InputOpenResult::Conflict;
        }
        for (const auto& entry : state.entries) {
            if (entry->receiver == receiver) return InputOpenResult::Conflict;
        }
        auto entry = std::make_shared<detail::InputChannelEntry>(configured.value->local_locator, receiver);
        if (hooks_.before_publish) hooks_.before_publish();
        // shared_ptr move is nonthrowing; vector publication has the strong
        // guarantee, so allocation failure cannot leave a partial registration.
        state.entries.push_back(std::move(entry));
        return InputOpenResult::Opened;
    } catch (const std::bad_alloc&) {
        return InputOpenResult::AllocationFailure;
    }
}

InputChannelTicket InputChannels::Capture(const Locator& local) const
{
    auto& state = *state_;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.disabled) return {};
    const auto found = state.Find(local);
    if (found == state.entries.end() || (*found)->phase != detail::InputChannelEntry::Phase::Open) return {};
    InputChannelTicket ticket;
    ticket.identity_ = state.identity;
    ticket.entry_ = *found;
    return ticket;
}

InputDeliveryResult InputChannels::Deliver(const InputChannelTicket& ticket,
        const Locator& remote, const void* data, std::size_t size)
{
    auto& state = *state_;
    if (!data || size == 0) return InputDeliveryResult::InvalidBuffer;
    if (size > std::numeric_limits<std::uint32_t>::max() || size > state.max_record_bytes) {
        return InputDeliveryResult::TooLarge;
    }
    if (ticket.identity_ != state.identity || !ticket.entry_) return InputDeliveryResult::InvalidTicket;
    // The entry local is immutable even when closed; neither route lookup nor
    // the following membership check can retarget a historical ticket.
    const auto entry = ticket.entry_;
    try {
        if (!state.directory->ResolveOutbound({entry->local, remote}).value) return InputDeliveryResult::InvalidRoute;
    } catch (const std::bad_alloc&) {
        // ResolveOutbound returns an owned Route, whose strings/aliases may
        // allocate. This failure precedes the delivery lease and any callback.
        return InputDeliveryResult::AllocationFailure;
    }
    std::unique_lock<std::mutex> lock(state.mutex);
    if (state.disabled) return InputDeliveryResult::Disabled;
    if (entry->phase != detail::InputChannelEntry::Phase::Open || !entry->receiver ||
            std::find(state.entries.begin(), state.entries.end(), entry) == state.entries.end()) {
        return InputDeliveryResult::Closed;
    }
    ++entry->in_flight;
    ++state.in_flight;
    InputDeliveryLease lease(state, entry);
    auto* receiver = entry->receiver;
    lock.unlock();
    try {
        receiver->OnDataReceived(static_cast<const std::uint8_t*>(data),
                static_cast<std::uint32_t>(size), entry->local, remote);
        return InputDeliveryResult::Delivered;
    } catch (...) {
        // Partial callback effects are possible. Release the lease without
        // reporting success, rolling back the receiver, or retrying delivery.
        return InputDeliveryResult::CallbackFailure;
    }
}

void InputChannels::Close(const Locator& local)
{
    auto& state = *state_;
    std::unique_lock<std::mutex> lock(state.mutex);
    const auto found = state.Find(local);
    if (found == state.entries.end()) return;
    // Copy the shared entry BEFORE allowing another closer to erase its slot.
    const auto entry = *found;
    entry->phase = detail::InputChannelEntry::Phase::Closing;
    lock.unlock();
    if (hooks_.after_close_capture) hooks_.after_close_capture(entry->local);
    lock.lock();
    state.changed.wait(lock, [&] { return entry->in_flight == 0; });
    entry->receiver = nullptr;
    entry->phase = detail::InputChannelEntry::Phase::Closed;
    // A concurrent closer may have completed and Open may have replaced it.
    // Erase by shared entry identity, never by rediscovering the local key.
    const auto current = std::find(state.entries.begin(), state.entries.end(), entry);
    if (current != state.entries.end()) state.entries.erase(current);
    state.changed.notify_all();
}

void InputChannels::DisableAll()
{
    auto& state = *state_;
    std::unique_lock<std::mutex> lock(state.mutex);
    state.disabled = true;
    for (const auto& entry : state.entries) entry->phase = detail::InputChannelEntry::Phase::Closing;
    lock.unlock();
    if (hooks_.after_disable_admission) hooks_.after_disable_admission();
    lock.lock();
    // A disabled flag alone does not prove that any callback has completed.
    state.changed.wait(lock, [&] { return state.in_flight == 0; });
    for (const auto& entry : state.entries) {
        entry->receiver = nullptr;
        entry->phase = detail::InputChannelEntry::Phase::Closed;
    }
    state.entries.clear();
    state.drained = true;
    state.changed.notify_all();
}

InputChannelUsage InputChannels::SnapshotUsage() const
{
    auto& state = *state_;
    std::lock_guard<std::mutex> lock(state.mutex);
    InputChannelUsage result;
    for (const auto& entry : state.entries) {
        if (entry->phase == detail::InputChannelEntry::Phase::Open) ++result.open_channels;
        else ++result.closing_channels;
    }
    result.in_flight = state.in_flight;
    result.disabled = state.disabled;
    result.drained = state.drained;
    return result;
}
} // namespace mdds::dsoftbus
