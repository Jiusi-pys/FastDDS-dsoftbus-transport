#pragma once

#include "softbus_record_contract.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace fastdds_dsoftbus_probe {

enum class LifecycleAdmission { Owner, Alias, Invalid, Conflict, Full };
enum class LifecycleEventKind {
    Created, Alias, Accepted, Ready, CloseRequested, ShutdownClaimed, ShutdownStarted, ShutdownReturned,
    CallbackEnter, CallbackExit, Bytes, Discarded, Failure, WorkerToken, CallBegin,
    CallReturn, WorkerJoined, CallbackHeld, CallbackReleased, PeerShutdown, PeerError,
    NegotiateHeld, NegotiateReleased
};
const char* LifecycleEventName(LifecycleEventKind kind) noexcept;
struct LifecycleEvent {
    std::uint64_t sequence = 0, tid = 0;
    std::chrono::steady_clock::time_point at{};
    std::chrono::system_clock::time_point wall{};
    LifecycleEventKind kind = LifecycleEventKind::Failure;
    int socket = 0, code = 0;
    std::size_t bytes = 0, active_callbacks = 0;
    bool closing = false;
};
struct LifecycleSnapshot {
    bool failed = false, fatal = false, closing = false, ready = false;
    const char* reason = "none";
    int connection = 0, untracked_id = 0;
    std::size_t owned = 0, aliases = 0, shutdown_claimed = 0, shutdown_returned = 0;
    std::size_t active_callbacks = 0, callback_count = 0, received = 0, discarded = 0;
    std::size_t queued = 0, queued_bytes = 0, events = 0, peer_shutdown = 0;
};

// Short-lived experiment ownership only. Never calls an SDK or declares SDK drain.
class LifecycleContract {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t owner_limit = 16, event_limit = 512;
    LifecycleContract(std::string expected_name, std::string expected_network, int bytes_type);
    LifecycleAdmission RegisterSocket(int socket);
    bool Accept(int socket, RecordPeer peer);
    bool SetReady(int socket);
    void EnterCallback(int socket, std::uint64_t tid = 0) noexcept;
    void LeaveCallback(int socket, std::uint64_t tid = 0) noexcept;
    void Receive(int socket, const void* bytes, std::size_t length) noexcept;
    bool Pop(RecordEvent& event);
    void PeerShutdown(int socket, int reason) noexcept;
    void PeerError(int socket, int code) noexcept;
    Clock::time_point RequestClose(Clock::time_point absolute_run_deadline,
            std::chrono::milliseconds close_budget = std::chrono::milliseconds(3000));
    int ClaimShutdown();
    bool ShutdownReturned(int socket, std::uint64_t tid = 0);
    void Report(LifecycleEventKind kind, int socket = 0, int code = 0,
            std::size_t bytes = 0, std::uint64_t tid = 0) noexcept;
    void Fail(const char* reason, bool fatal = false, int untracked = 0) noexcept;
    LifecycleSnapshot Snapshot() const;
    bool Wait(const std::function<bool(const LifecycleSnapshot&)>& predicate, Clock::time_point deadline);
    bool EventAt(std::size_t index, LifecycleEvent& event) const;
    Clock::time_point Start() const noexcept { return start_; }
private:
    struct Owner { int socket = 0; bool accepted = false, claimed = false, returned = false; };
    Owner* Find(int socket) noexcept;
    void FailLocked(const char* reason, bool fatal = false, int untracked = 0) noexcept;
    void EventLocked(LifecycleEventKind kind, int socket, int code, std::size_t bytes,
            std::uint64_t tid) noexcept;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::string expected_name_, expected_network_;
    int bytes_type_;
    const Clock::time_point start_ = Clock::now();
    Clock::time_point close_deadline_{};
    LifecycleSnapshot state_;
    std::array<Owner, owner_limit> owners_{};
    std::array<LifecycleEvent, event_limit> events_{};
    std::deque<RecordEvent> records_;
};

class LifecycleCallbackLease {
public:
    LifecycleCallbackLease(LifecycleContract& contract, int socket, std::uint64_t tid = 0) noexcept;
    ~LifecycleCallbackLease();
    LifecycleCallbackLease(const LifecycleCallbackLease&) = delete;
    LifecycleCallbackLease& operator=(const LifecycleCallbackLease&) = delete;
private:
    LifecycleContract& contract_;
    int socket_;
    std::uint64_t tid_;
};

class LifecycleThreadFactory {
public:
    virtual ~LifecycleThreadFactory() = default;
    virtual std::thread Create(std::function<void()> job);
};

// A joinable worker must be retained on timeout. Destructor is not cancellation.
class ManagedLifecycleWorker {
public:
    bool Start(LifecycleThreadFactory& factory, std::function<void()> job) noexcept;
    bool JoinUntil(std::chrono::steady_clock::time_point deadline);
    bool Finished() const noexcept { return finished_.load(); }
    bool Joinable() const noexcept { return worker_.joinable(); }
    bool Threw() const noexcept { return threw_.load(); }
private:
    std::thread worker_;
    std::atomic<bool> finished_{true}, threw_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace fastdds_dsoftbus_probe
