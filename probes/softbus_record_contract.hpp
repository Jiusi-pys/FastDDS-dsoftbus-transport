#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace mdds_probe {
inline constexpr std::array<std::size_t, 5> record_probe_lengths{{32, 255, 4096, 16384, 60000}};
inline constexpr std::size_t record_queue_count = 32;
inline constexpr std::size_t record_queue_bytes = 256 * 1024;
inline constexpr std::size_t record_owned_limit = 15;

struct RecordPeer {
    std::string name;
    std::string network_id;
    std::string package;
    int data_type = 0;
    bool fields_valid = false;
};

// Copies SDK-owned strings before callback return. Package is diagnostic only.
RecordPeer CopyRecordPeer(const char* name, const char* network, const char* package, int data_type);
std::vector<std::uint8_t> MakeProbeRecord(std::size_t index);
bool ValidateProbeRecord(std::size_t index, const std::vector<std::uint8_t>& bytes);
std::uint64_t RecordHash(const std::vector<std::uint8_t>& bytes);

// Read-only route-query validation. 32 is this experiment's observation cap,
// not a DSoftBus product limit. No device names/UDIDs or token data are retained.
bool CopyRouteNetworkId(const char* bytes, std::size_t capacity, std::string& network);
bool ValidateRouteNodeCount(int count, bool has_data);
bool AppendRouteNetworkId(const std::string& local, const char* bytes, std::size_t capacity,
        std::vector<std::string>& remotes);

enum class RecordEventKind { Bound, Bytes, Shutdown, Error };
struct RecordEvent {
    RecordEventKind kind = RecordEventKind::Error;
    int socket = 0;
    int code = 0;
    std::chrono::steady_clock::time_point at{};
    RecordPeer peer;
    std::vector<std::uint8_t> bytes;
    bool peer_match = false;
    bool before_bind_return = false;
};
struct RecordSnapshot {
    bool failed = false;
    bool fatal = false;
    bool ready = false;
    bool closing = false;
    const char* failure = "none";
    int connection = 0;
    std::size_t queued = 0;
    std::size_t queued_bytes = 0;
    std::size_t owned = 0;
    std::size_t shutdown_claimed = 0;
    std::size_t bind_callbacks = 0;
    std::size_t byte_callbacks = 0;
    std::size_t shutdown_callbacks = 0;
    std::size_t late_callbacks = 0;
    std::size_t discarded_records = 0;
    std::size_t before_bind_return = 0;
    std::size_t bound_copy_failures = 0;
    int untracked_first_id = 0;
    // Saturating callback-observation count, NOT a count of unique untracked IDs.
    std::size_t untracked_count = 0;
};

// Allocation-independent transfer to the ordinary SDK-calling control thread.
// This accounts for retained IDs only: fatal/untracked diagnostics are separate.
struct RecordShutdownTargets {
    std::array<int, record_owned_limit> values{};
    std::size_t count = 0;
    std::size_t size() const noexcept { return count; }
    bool empty() const noexcept { return count == 0; }
    const int* begin() const noexcept { return values.data(); }
    const int* end() const noexcept { return values.data() + count; }
};

// One process / one listener namespace, no reconnect or ID reuse experiment.
// Methods never call the SDK. All queued data has independent ownership.
class SoftbusRecordContract {
public:
    using Clock = std::chrono::steady_clock;
    SoftbusRecordContract(bool server, std::string expected_name, std::string expected_network, int bytes_type);
    bool RegisterCreated(int socket);
    bool BindReturned(int socket, int result);
    // The callback MUST capture the ID here before any potentially allocating
    // peer copy. false means do not copy/admit; a server's new retained ID still
    // has ordinary-thread cleanup responsibility even on a failed/closing run.
    bool BeginBind(int socket);
    void CompleteBind(int socket, RecordPeer peer);
    void FailBoundCopy(int socket);
    void OnBytes(int socket, const void* data, std::size_t length);
    void OnShutdown(int socket, int reason);
    void OnError(int socket, int code);
    bool WaitPop(RecordEvent& event, Clock::time_point deadline);
    void Fail(const char* reason);
    // Transfers each owned positive ID's unique Shutdown responsibility once.
    // Repeated calls catch late new accepted IDs without repeating old IDs.
    RecordShutdownTargets ClaimShutdownTargets();
    RecordSnapshot Snapshot() const;

private:
    struct Owned {
        int id = 0;
        bool closed = false;
        bool shutdown_claimed = false;
        bool bind_pending = false;
        bool bind_observed = false;
    };
    Owned* Find(int socket);
    bool AddOwned(int socket);
    bool Push(RecordEvent event);
    void FailLocked(const char* reason);
    void DiscardBytesLocked();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool server_;
    std::string expected_name_;
    std::string expected_network_;
    int bytes_type_;
    int created_ = 0;
    bool bind_returned_ = false;
    RecordSnapshot state_;
    std::array<Owned, record_owned_limit> owned_{};
    std::size_t owned_count_ = 0;
    std::deque<RecordEvent> events_;
};
} // namespace mdds_probe
