#include "softbus_record_contract.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace fastdds_dsoftbus_probe {
namespace {
void IncrementSaturated(std::size_t& value) noexcept
{
    if (value != std::numeric_limits<std::size_t>::max()) ++value;
}
bool CopyField(const char* source, std::size_t maximum, std::string& out)
{
    if (!source) return false;
    std::size_t size = 0;
    while (size <= maximum && source[size] != '\0') ++size;
    if (size == 0 || size > maximum) return false;
    out.assign(source, size);
    return std::all_of(out.begin(), out.end(), [](unsigned char c){ return c >= 32 && c <= 126; });
}
std::uint8_t RecordByte(std::size_t index, std::size_t offset)
{
    // Deterministic test payload only: no adaptation header or framing.
    return static_cast<std::uint8_t>((offset * 131u + index * 37u + offset / 251u) & 255u);
}
}
RecordPeer CopyRecordPeer(const char* name, const char* network, const char* package, int type)
{
    RecordPeer peer;
    peer.data_type = type;
    peer.fields_valid = CopyField(name, 255, peer.name) && CopyField(network, 64, peer.network_id) &&
            peer.network_id.size() == 64;
    if (package) CopyField(package, 64, peer.package); // Never an authentication condition.
    return peer;
}
std::vector<std::uint8_t> MakeProbeRecord(std::size_t index)
{
    if (index >= record_probe_lengths.size()) throw std::out_of_range("record index");
    std::vector<std::uint8_t> bytes(record_probe_lengths[index]);
    for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = RecordByte(index, i);
    return bytes;
}
bool ValidateProbeRecord(std::size_t index, const std::vector<std::uint8_t>& bytes)
{
    if (index >= record_probe_lengths.size() || bytes.size() != record_probe_lengths[index]) return false;
    for (std::size_t i = 0; i < bytes.size(); ++i) if (bytes[i] != RecordByte(index, i)) return false;
    return true;
}
std::uint64_t RecordHash(const std::vector<std::uint8_t>& bytes)
{
    // Diagnostic FNV-1a, not a security/identity hash. Validation checks every byte.
    std::uint64_t hash = 14695981039346656037ull;
    for (auto byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
bool CopyRouteNetworkId(const char* bytes, std::size_t capacity, std::string& network)
{
    network.clear();
    if (!bytes || capacity < 65 || bytes[64] != '\0') return false;
    for (std::size_t i = 0; i < 64; ++i)
    {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (!((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || (c >= '0' && c <= '9'))) return false;
    }
    network.assign(bytes, 64);
    return true;
}
bool ValidateRouteNodeCount(int count, bool has_data)
{
    return count >= 0 && count <= 32 && (count == 0 || has_data);
}
bool AppendRouteNetworkId(const std::string& local, const char* bytes, std::size_t capacity,
        std::vector<std::string>& remotes)
{
    std::string network;
    if (remotes.size() >= 32 || !CopyRouteNetworkId(bytes, capacity, network) || network == local ||
            std::find(remotes.begin(), remotes.end(), network) != remotes.end()) return false;
    remotes.push_back(std::move(network));
    return true;
}
SoftbusRecordContract::SoftbusRecordContract(bool server, std::string name, std::string network, int type)
    : server_(server), expected_name_(std::move(name)), expected_network_(std::move(network)), bytes_type_(type)
{
    if (expected_name_.empty() || expected_network_.size() != 64) FailLocked("invalid_expected_peer");
}
SoftbusRecordContract::Owned* SoftbusRecordContract::Find(int socket)
{
    for (std::size_t i=0;i<owned_count_;++i) if (owned_[i].id == socket) return &owned_[i];
    return nullptr;
}
bool SoftbusRecordContract::AddOwned(int socket)
{
    if (socket <= 0) { FailLocked("invalid_socket"); return false; }
    if (Find(socket)) { FailLocked("duplicate_socket_id"); return false; }
    if (owned_count_ == record_owned_limit)
    {
        // Do not pretend retained-resource counters include this observation.
        // This fatal state is never cleared by later callbacks or cleanup.
        state_.fatal = true;
        if (state_.untracked_first_id == 0) state_.untracked_first_id = socket;
        IncrementSaturated(state_.untracked_count);
        FailLocked("owned_handle_overflow"); return false;
    }
    owned_[owned_count_++] = Owned{socket};
    return true;
}
bool SoftbusRecordContract::RegisterCreated(int socket)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (created_ != 0 || state_.closing || state_.failed) { FailLocked("duplicate_create"); return false; }
    if (!AddOwned(socket)) return false;
    created_ = socket;
    if (!server_) state_.connection = socket; // BINDING before the SDK call.
    return true;
}
bool SoftbusRecordContract::BindReturned(int socket, int result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bind_returned_ = true;
    auto* owned = Find(socket);
    if (server_ || socket != created_ || !owned || owned->closed || state_.closing || state_.failed || result != 0)
    {
        FailLocked("bind_not_usable"); return false;
    }
    state_.ready = true;
    cv_.notify_all();
    return true;
}
bool SoftbusRecordContract::BeginBind(int socket)
{
    std::lock_guard<std::mutex> lock(mutex_);
    IncrementSaturated(state_.bind_callbacks);
    if (state_.closing) IncrementSaturated(state_.late_callbacks);
    if (server_)
    {
        // Fixed storage: no string/queue allocation has happened in this path.
        // Every new retained accepted ID belongs to this listener, even if the
        // run has already failed or peer identification will fail later.
        if (!AddOwned(socket)) return false;
        if (state_.connection != 0) FailLocked("duplicate_peer_connection");
    }
    auto* entry = Find(socket);
    if (!entry || (!server_ && socket != state_.connection))
    {
        // A client has no listener and therefore cannot own unknown accepted IDs.
        FailLocked("unexpected_client_bind"); return false;
    }
    if (entry->bind_observed) { FailLocked("duplicate_bind_callback"); return false; }
    entry->bind_observed = true;
    if (state_.failed || state_.closing || entry->closed || entry->shutdown_claimed) return false;
    entry->bind_pending = true;
    return true;
}
void SoftbusRecordContract::FailBoundCopy(int socket)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* entry = Find(socket);
    if (entry) entry->bind_pending = false;
    IncrementSaturated(state_.bound_copy_failures);
    FailLocked(entry ? "bound_copy_failed" : "unowned_bound_copy_failure");
}
bool SoftbusRecordContract::Push(RecordEvent event)
{
    if (events_.size() == record_queue_count || event.bytes.size() > record_queue_bytes - state_.queued_bytes)
    {
        FailLocked("event_queue_overflow"); return false;
    }
    const auto length = event.bytes.size();
    events_.push_back(std::move(event));
    state_.queued_bytes += length;
    cv_.notify_all();
    return true;
}
void SoftbusRecordContract::CompleteBind(int socket, RecordPeer peer)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* entry = Find(socket);
    if (!entry || !entry->bind_pending) { FailLocked("bind_completion_without_capture"); return; }
    entry->bind_pending = false;
    // Shutdown/cleanup can race with the copy outside this lock. Completion
    // never revives a closed entry, nor clears fatal or any previous failure.
    if (entry->closed || entry->shutdown_claimed || state_.closing || state_.failed)
    { FailLocked("bind_closed_before_identification"); return; }
    const bool match = peer.fields_valid && peer.name == expected_name_ &&
            peer.network_id == expected_network_ && peer.data_type == bytes_type_;
    if (server_)
    {
        // A different accepted callback may have completed while we copied.
        if (state_.connection != 0) FailLocked("duplicate_peer_connection");
        if (!match) FailLocked("peer_identity_mismatch");
        if (!state_.failed && !state_.closing)
        {
            state_.connection = socket; state_.ready = true;
        }
    }
    else if (socket != state_.connection || !match)
    {
        // Synchronous client normally has no OnBind. Unknown IDs here are not
        // automatically adopted or closed: there is no local listener owner.
        FailLocked("unexpected_client_bind");
    }
    RecordEvent event;
    event.kind = RecordEventKind::Bound; event.socket = socket; event.at = Clock::now();
    event.peer = std::move(peer); event.peer_match = match;
    event.before_bind_return = !server_ && !bind_returned_;
    Push(std::move(event));
}
void SoftbusRecordContract::OnBytes(int socket, const void* data, std::size_t length)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++state_.byte_callbacks;
    auto* entry = Find(socket);
    if (state_.closing || (entry && entry->closed)) { ++state_.late_callbacks; return; }
    if (state_.failed) { ++state_.discarded_records; return; }
    if (!entry || socket != state_.connection || !data || length == 0 || length > record_queue_bytes)
    {
        FailLocked("invalid_bytes_callback"); return;
    }
    // Check capacity before copying any SDK bytes; callback never waits for Bind.
    if (events_.size() == record_queue_count || length > record_queue_bytes - state_.queued_bytes)
    {
        FailLocked("event_queue_overflow"); return;
    }
    RecordEvent event;
    event.kind = RecordEventKind::Bytes; event.socket = socket; event.at = Clock::now();
    event.before_bind_return = !server_ && !bind_returned_;
    if (event.before_bind_return) ++state_.before_bind_return;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    event.bytes.assign(bytes, bytes + length);
    Push(std::move(event));
}
void SoftbusRecordContract::OnShutdown(int socket, int reason)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++state_.shutdown_callbacks;
    if (state_.closing) ++state_.late_callbacks;
    auto* entry = Find(socket);
    if (!entry) { FailLocked("unknown_shutdown_socket"); return; }
    entry->closed = true;
    if (socket == state_.connection)
    {
        state_.ready = false; DiscardBytesLocked();
    }
    RecordEvent event;
    event.kind = RecordEventKind::Shutdown; event.socket = socket; event.code = reason; event.at = Clock::now();
    event.before_bind_return = !server_ && !bind_returned_;
    Push(std::move(event));
}
void SoftbusRecordContract::OnError(int socket, int code)
{
    std::lock_guard<std::mutex> lock(mutex_);
    FailLocked("sdk_error_callback");
    RecordEvent event;
    event.kind = RecordEventKind::Error; event.socket = socket; event.code = code; event.at = Clock::now();
    event.before_bind_return = !server_ && !bind_returned_;
    Push(std::move(event));
}
bool SoftbusRecordContract::WaitPop(RecordEvent& event, Clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_until(lock, deadline, [&]{ return !events_.empty() || state_.failed || state_.closing; });
    if (events_.empty()) return false;
    // A caller cannot accidentally deliver pre-Bind data while BINDING.
    if (events_.front().kind == RecordEventKind::Bytes && !state_.ready) return false;
    event = std::move(events_.front()); events_.pop_front();
    state_.queued_bytes -= event.bytes.size();
    return true;
}
void SoftbusRecordContract::DiscardBytesLocked()
{
    for (auto it = events_.begin(); it != events_.end();)
    {
        if (it->kind == RecordEventKind::Bytes)
        {
            ++state_.discarded_records; state_.queued_bytes -= it->bytes.size(); it = events_.erase(it);
        }
        else ++it;
    }
}
void SoftbusRecordContract::FailLocked(const char* reason)
{
    if (!state_.failed) state_.failure = reason;
    state_.failed = true; state_.ready = false;
    DiscardBytesLocked(); cv_.notify_all();
}
void SoftbusRecordContract::Fail(const char* reason)
{
    std::lock_guard<std::mutex> lock(mutex_); FailLocked(reason);
}
RecordShutdownTargets SoftbusRecordContract::ClaimShutdownTargets()
{
    std::lock_guard<std::mutex> lock(mutex_);
    state_.closing = true; state_.ready = false; DiscardBytesLocked();
    RecordShutdownTargets targets;
    // Accepted/client sockets first, listening resource last.
    for (std::size_t i=0;i<owned_count_;++i)
    {
        auto& entry=owned_[i];
        if ((!server_ || entry.id != created_) && !entry.shutdown_claimed)
        {
            entry.shutdown_claimed = true; targets.values[targets.count++]=entry.id; ++state_.shutdown_claimed;
        }
    }
    if (server_)
        for (std::size_t i=0;i<owned_count_;++i)
        {
            auto& entry=owned_[i];
            if (entry.id == created_ && !entry.shutdown_claimed)
            {
                entry.shutdown_claimed = true; targets.values[targets.count++]=entry.id; ++state_.shutdown_claimed;
            }
        }
    cv_.notify_all();
    return targets;
}
RecordSnapshot SoftbusRecordContract::Snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto snapshot = state_; snapshot.queued = events_.size(); snapshot.owned = owned_count_;
    return snapshot;
}
} // namespace fastdds_dsoftbus_probe
