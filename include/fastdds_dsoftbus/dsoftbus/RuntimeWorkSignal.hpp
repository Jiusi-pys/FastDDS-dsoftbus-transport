#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>

namespace fastdds_dsoftbus::dsoftbus {
enum class WorkWaitResult { Work, Deadline, StopRequested, Closed, GenerationExhausted, UnsupportedConfiguration };

// Transport progress notification, never a queue, delivery receipt or SDK drain.
// One owner may have multiple waiters. Retain this object during every call.
class RuntimeWorkSignal final {
public:
    using Clock = std::chrono::steady_clock;
    std::uint64_t Generation() const noexcept {
        std::lock_guard<std::mutex> lock(gate_);
        return generation_;
    }
    void Notify() noexcept {
        {
            std::lock_guard<std::mutex> lock(gate_);
            if (generation_ == (std::numeric_limits<std::uint64_t>::max)()) exhausted_ = true;
            else ++generation_;
        }
        changed_.notify_all();
    }
    void RequestStop() noexcept {
        { std::lock_guard<std::mutex> lock(gate_); stop_ = true; }
        changed_.notify_all();
    }
    void Close() noexcept {
        { std::lock_guard<std::mutex> lock(gate_); closed_ = true; }
        changed_.notify_all();
    }
    // Snapshot before draining work. Pass that snapshot here only after BOTH
    // queues are drained to no work. Publication during that drain changes the
    // predicate even if notify preceded this wait. The only timer is an actual
    // queued record's expiry in this real steady-clock domain; max means none.
    WorkWaitResult Wait(std::uint64_t& seen, Clock::time_point deadline = Clock::time_point::max()) {
        std::unique_lock<std::mutex> lock(gate_);
        const auto ready = [&] { return generation_ != seen || stop_ || closed_ || exhausted_; };
        if (deadline == Clock::time_point::max()) changed_.wait(lock, ready);
        else if (!changed_.wait_until(lock, deadline, ready)) return WorkWaitResult::Deadline;
        seen = generation_;
        if (closed_) return WorkWaitResult::Closed;
        if (stop_) return WorkWaitResult::StopRequested;
        if (exhausted_) return WorkWaitResult::GenerationExhausted;
        return WorkWaitResult::Work;
    }
private:
    mutable std::mutex gate_;
    std::condition_variable changed_;
    std::uint64_t generation_ = 0;
    bool stop_ = false, closed_ = false, exhausted_ = false;
};
}
