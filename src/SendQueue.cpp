#include <fastdds_dsoftbus/dsoftbus/SendQueue.hpp>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail {
// Explicit fields, independent of Locator_t padding/CDR representation.
struct SendPeerKey {
    std::int32_t kind = 0;
    std::uint32_t port = 0;
    EndpointId address{};
    bool operator<(const SendPeerKey& other) const noexcept {
        if (kind != other.kind) return kind < other.kind;
        if (port != other.port) return port < other.port;
        return address < other.address;
    }
    bool operator==(const SendPeerKey& other) const noexcept {
        return kind == other.kind && port == other.port && address == other.address;
    }
};
struct SendQueueState
{
    struct Count { std::size_t records = 0; std::size_t bytes = 0; };
    explicit SendQueueState(SendQueueLimits value);
    ~SendQueueState();
    const SendQueueLimits limits;
    std::mutex mutex;
    std::condition_variable changed;
    Count total;
    std::map<SendPeerKey, Count> peers;
    bool closed = false;
    std::size_t queued = 0;
    std::unique_ptr<OwnedSendRecord::Storage> head;
    OwnedSendRecord::Storage* tail = nullptr;
};

// Constructed without allocation immediately after a successful reservation.
// Storage declares this BEFORE its payload, so bytes die before their charge.
struct SendQueueCharge
{
    std::shared_ptr<SendQueueState> state;
    SendPeerKey peer;
    std::size_t bytes;
    SendQueueCharge() noexcept : peer{}, bytes(0) {}
    SendQueueCharge(std::shared_ptr<SendQueueState> owner, SendPeerKey key, std::size_t size) noexcept
        : state(std::move(owner)), peer(key), bytes(size) {}
    SendQueueCharge(SendQueueCharge&& other) noexcept
        : state(std::move(other.state)), peer(other.peer), bytes(other.bytes) {}
    SendQueueCharge(const SendQueueCharge&) = delete;
    SendQueueCharge& operator=(const SendQueueCharge&) = delete;
    SendQueueCharge& operator=(SendQueueCharge&&) = delete;
    ~SendQueueCharge()
    {
        if (!state) return;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto found = state->peers.find(peer);
            --found->second.records;
            found->second.bytes -= bytes;
            if (found->second.records == 0) state->peers.erase(found);
            --state->total.records;
            state->total.bytes -= bytes;
        }
        state->changed.notify_all();
    }
};
} // namespace detail

struct OwnedSendRecord::Storage
{
    // Construct Route directly: its copy and the empty payload's Debug-STL
    // iterator proxy may allocate. Do not turn those failures into terminate.
    Storage(detail::SendQueueCharge charge_value, const Route& route_value)
        : charge(std::move(charge_value)), route(route_value), payload(std::size_t{0}) {}
    detail::SendQueueCharge charge;
    Route route;
    std::uint64_t generation = 0;
    std::int32_t priority = 0;
    Clock::time_point deadline;
    std::vector<std::uint8_t> payload;
    // Intrusive ownership link: no allocation is required for publication or
    // detachment. Taken records always have a null next; Close drains iteratively.
    std::unique_ptr<Storage> next;
};

detail::SendQueueState::SendQueueState(SendQueueLimits value) : limits(value) {}
detail::SendQueueState::~SendQueueState() = default;

namespace {
bool SendQueueStringValid(const std::string& value, std::size_t maximum)
{
    return !value.empty() && value.size() <= maximum && value.find('\0') == std::string::npos;
}

bool SendQueueRouteValid(const Route& route, std::uint64_t generation, detail::SendPeerKey& peer)
{
    if (generation == 0 || route.route_version == 0 ||
            !LocatorCodec::IsValid(route.key.local_locator) ||
            !LocatorCodec::Decode(route.key.remote_locator, peer.address) ||
            (route.key.local_locator.port >> 24) != (route.key.remote_locator.port >> 24) ||
            route.fields.data_type != RecordDataType::Bytes) return false;
    peer.kind = route.key.remote_locator.kind;
    peer.port = route.key.remote_locator.port;
    const auto& fields = route.fields;
    if (!SendQueueStringValid(fields.remote_network_id, 64) ||
            !SendQueueStringValid(fields.remote_listen_name, 255) ||
            !SendQueueStringValid(fields.local_initiator_name, 255) ||
            fields.expected_remote_initiator_names.empty()) return false;
    for (std::size_t i = 0; i < fields.expected_remote_initiator_names.size(); ++i) {
        const auto& name = fields.expected_remote_initiator_names[i];
        if (!SendQueueStringValid(name, 255)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (fields.expected_remote_initiator_names[j] == name) return false;
        }
    }
    return true;
}

bool SendQueuePrefixValid(const SendBufferView* buffers, std::size_t count, std::size_t total)
{
    if (!buffers || total == 0) return false;
    // Subtract the required prefix instead of adding advertised buffer sizes:
    // no overflow, and no inspection of an unused (possibly invalid) suffix.
    for (std::size_t i = 0; i < count && total != 0; ++i) {
        const auto used = (std::min)(total, buffers[i].size);
        if (used != 0 && !buffers[i].data) return false;
        total -= used;
    }
    return total == 0;
}

bool SendQueueFits(const detail::SendQueueState::Count& count,
        std::size_t records_limit, std::size_t bytes_limit,
        std::size_t requested_records, std::size_t requested_bytes)
{
    return requested_records <= records_limit - count.records && requested_bytes <= bytes_limit - count.bytes;
}
} // namespace

OwnedSendRecord::OwnedSendRecord(std::unique_ptr<Storage> storage) noexcept : storage_(std::move(storage)) {}
OwnedSendRecord::~OwnedSendRecord() = default;
OwnedSendRecord::OwnedSendRecord(OwnedSendRecord&&) noexcept = default;
OwnedSendRecord& OwnedSendRecord::operator=(OwnedSendRecord&&) noexcept = default;
const std::uint8_t* OwnedSendRecord::data() const noexcept { return storage_ ? storage_->payload.data() : nullptr; }
std::size_t OwnedSendRecord::size() const noexcept { return storage_ ? storage_->payload.size() : 0; }
const Route& OwnedSendRecord::route() const
{
    if (!storage_) throw std::logic_error("Route requested from a moved-from send record");
    return storage_->route;
}
std::uint64_t OwnedSendRecord::generation() const noexcept { return storage_ ? storage_->generation : 0; }
std::int32_t OwnedSendRecord::priority() const noexcept { return storage_ ? storage_->priority : 0; }
OwnedSendRecord::Clock::time_point OwnedSendRecord::deadline() const noexcept
{
    return storage_ ? storage_->deadline : Clock::time_point{};
}

SendQueue::SendQueue(SendQueueLimits limits) : SendQueue(limits, {}) {}
SendQueue::SendQueue(SendQueueLimits limits, TestHooks hooks) : hooks_(std::move(hooks))
{
    if (limits.peer_records == 0 || limits.peer_bytes == 0 || limits.global_records == 0 ||
            limits.global_bytes == 0 || limits.max_record_bytes == 0)
        throw std::invalid_argument("SendQueue limits must all be nonzero");
    state_ = std::make_shared<detail::SendQueueState>(limits);
}
SendQueue::~SendQueue() { Close(); }
SendQueue::Clock::time_point SendQueue::Now() const noexcept { return hooks_.now ? hooks_.now() : Clock::now(); }

SendAdmission SendQueue::Enqueue(const Route& route, std::uint64_t generation,
        std::int32_t priority, Clock::time_point original_deadline,
        const SendBufferView* buffers, std::size_t buffer_count, std::size_t total_bytes)
{
    const SendTargetView target{&route, generation};
    // No pre-copy or separate clock budget in this compatibility entry.
    return EnqueueBatch(&target, 1, priority, original_deadline, buffers, buffer_count, total_bytes);
}

SendAdmission SendQueue::EnqueueBatch(const SendTargetView* targets, std::size_t target_count,
        std::int32_t priority, Clock::time_point original_deadline,
        const SendBufferView* buffers, std::size_t buffer_count, std::size_t total_bytes)
try
{
    const auto entry = Now();
    const auto window = std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds(20));
    const auto capped = entry > Clock::time_point::max() - window ? Clock::time_point::max() : entry + window;
    const auto admission_deadline = (std::min)(original_deadline, capped);
    const auto state = state_;
    if (!targets || target_count == 0) return SendAdmission::InvalidRoute;
    if (total_bytes > state->limits.max_record_bytes || total_bytes > state->limits.peer_bytes ||
            total_bytes > state->limits.global_bytes || target_count > state->limits.global_records ||
            (total_bytes != 0 && target_count > state->limits.global_bytes / total_bytes))
        return SendAdmission::TooLarge;
    if (!SendQueuePrefixValid(buffers, buffer_count, total_bytes)) return SendAdmission::InvalidBuffers;
    const auto batch_bytes = target_count * total_bytes; // Proven bounded by global_bytes above.

    // Inactive charges also hold the validated stable peer keys. Allocate the
    // entire scratch space before taking any capacity; activation cannot throw.
    // The explicit size constructor is allowed to throw if a Debug-STL
    // iterator proxy allocation fails, unlike its noexcept default constructor.
    std::vector<detail::SendQueueCharge> charges(std::size_t{0});
    if (target_count > charges.max_size()) return SendAdmission::TooLarge;
    try {
        charges.reserve(target_count);
        for (std::size_t i = 0; i < target_count; ++i) {
            detail::SendPeerKey peer{};
            if (!targets[i].route || !SendQueueRouteValid(*targets[i].route, targets[i].generation, peer))
                return SendAdmission::InvalidRoute;
            for (const auto& existing : charges) {
                if (existing.peer == peer) return SendAdmission::InvalidRoute;
            }
            charges.emplace_back(nullptr, peer, total_bytes);
        }
    } catch (const std::bad_alloc&) {
        return SendAdmission::CopyFailed;
    }
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        for (;;) {
            if (state->closed) return SendAdmission::Closed;
            if (Now() >= admission_deadline) return SendAdmission::DeadlineExpired;
            bool fits = SendQueueFits(state->total, state->limits.global_records,
                    state->limits.global_bytes, target_count, batch_bytes);
            for (const auto& charge : charges) {
                const auto found = state->peers.find(charge.peer);
                const detail::SendQueueState::Count empty;
                const auto& count = found == state->peers.end() ? empty : found->second;
                if (!SendQueueFits(count, state->limits.peer_records, state->limits.peer_bytes, 1, total_bytes)) {
                    fits = false;
                    break;
                }
            }
            if (fits) {
                // Insert every required map node before activating ANY charge.
                // Under this mutex zero-count nodes can only be our staging.
                try {
                    for (const auto& charge : charges) state->peers.try_emplace(charge.peer);
                } catch (const std::bad_alloc&) {
                    for (const auto& charge : charges) {
                        const auto found = state->peers.find(charge.peer);
                        if (found != state->peers.end() && found->second.records == 0) state->peers.erase(found);
                    }
                    return SendAdmission::CopyFailed;
                }
                for (auto& charge : charges) {
                    auto& count = state->peers.find(charge.peer)->second;
                    ++count.records;
                    count.bytes += total_bytes;
                    charge.state = state;
                }
                state->total.records += target_count;
                state->total.bytes += batch_bytes;
                break;
            }
            if (hooks_.on_wait) hooks_.on_wait();
            state->changed.wait_until(lock, admission_deadline);
        }
    }
    // An unpublished chain is always destroyed iteratively and outside the
    // state mutex, including partial-copy failures, Close and deadline races.
    struct StagedChain
    {
        std::unique_ptr<OwnedSendRecord::Storage> head;
        OwnedSendRecord::Storage* tail = nullptr;
        ~StagedChain()
        {
            while (head) {
                auto next = std::move(head->next);
                head.reset();
                head = std::move(next);
            }
        }
    } staged;
    try {
        for (std::size_t target = 0; target < target_count; ++target) {
            if (hooks_.before_copy) hooks_.before_copy();
            auto storage = std::make_unique<OwnedSendRecord::Storage>(std::move(charges[target]), *targets[target].route);
            storage->generation = targets[target].generation;
            storage->priority = priority;
            storage->deadline = original_deadline;
            storage->payload.resize(total_bytes);
            std::size_t copied = 0;
            for (std::size_t i = 0; i < buffer_count && copied != total_bytes; ++i) {
                const auto used = (std::min)(total_bytes - copied, buffers[i].size);
                if (used != 0) std::memcpy(storage->payload.data() + copied, buffers[i].data, used);
                copied += used;
            }
            if (hooks_.after_copy) hooks_.after_copy();
            auto* tail = storage.get();
            if (staged.tail) staged.tail->next = std::move(storage);
            else staged.head = std::move(storage);
            staged.tail = tail;
        }
    } catch (...) {
        return SendAdmission::CopyFailed;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed) return SendAdmission::Closed;
        if (Now() >= admission_deadline) return SendAdmission::DeadlineExpired;
        if (state->tail) state->tail->next = std::move(staged.head);
        else state->head = std::move(staged.head);
        state->tail = staged.tail;
        state->queued += target_count;
        // The WHOLE batch linearizes here; no observer can see a staged prefix.
    }
    return SendAdmission::Accepted;
}
catch (const std::bad_alloc&)
{
    // Includes construction of the pre-reservation scratch vector itself.
    return SendAdmission::CopyFailed;
}

std::optional<OwnedSendRecord> SendQueue::TryTake()
{
    for (;;) {
        std::unique_ptr<OwnedSendRecord::Storage> storage;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->closed || !state_->head) return std::nullopt;
            storage = std::move(state_->head);
            state_->head = std::move(storage->next);
            if (!state_->head) state_->tail = nullptr;
            --state_->queued;
        }
        if (Now() >= storage->deadline) continue; // Outside lock: refund and wake.
        return OwnedSendRecord(std::move(storage));
    }
}

void SendQueue::Close() noexcept
{
    std::unique_ptr<OwnedSendRecord::Storage> discarded;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->closed) return;
        state_->closed = true;
        discarded = std::move(state_->head);
        state_->tail = nullptr;
        state_->queued = 0;
    }
    state_->changed.notify_all();
    // No lease destructor under the mutex, no allocation for a discard list,
    // and no recursively destroyed chain even for large configured limits.
    while (discarded) {
        auto next = std::move(discarded->next);
        discarded.reset();
        discarded = std::move(next);
    }
}

SendQueueUsage SendQueue::SnapshotUsage() const
{
    std::lock_guard<std::mutex> lock(state_->mutex);
    return {state_->total.records, state_->total.bytes, state_->peers.size(), state_->queued, state_->closed};
}
} // namespace fastdds_dsoftbus::dsoftbus
