#pragma once
// Lifecycle state a hub client has to get right, with none of the machinery that makes it hard to test.
// Pure C++: no sockets, no Fast DDS, no SDK, nothing POSIX. The socket work that uses this lives in
// HubTransport.cpp and only a board can verify it; everything here is checked on the build host.
//
// Two things are kept here.
//
// Membership. A process joins group locators on the hub by sending a JOIN control frame, and leaves by
// sending a LEAVE. Three things can make the hub's idea of this process's membership differ from the
// process's own: a JOIN whose frame never left, a LEAVE whose frame never left, and a reconnection, after
// which the new socket has been told nothing at all. Holding what is WANTED apart from what has been
// SYNCHRONIZED is what lets a reconnection resynchronize instead of assume, and what stops a lost JOIN
// leaving a process that believes it is attached and hears nothing.
//
// Deadline slicing. A blocking wait that cannot be interrupted is how a shutdown turns into a hang. Waits
// here are taken in bounded slices so the caller re-checks its stop flag between them, and the arithmetic
// that decides each slice is separated out because it is where the off-by-one lives.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace fastdds_dsoftbus::ros2_hub {

class Membership {
public:
    /// Record that this process wants to be in a group. True when that is news.
    bool Want(std::uint32_t port) { return wanted_.insert(port).second; }

    /// Record that it no longer wants to be. True when it did. The port stops counting as synchronized,
    /// because what the hub has been told about it is now wrong either way.
    bool Unwant(std::uint32_t port)
    {
        synchronized_.erase(port);
        return wanted_.erase(port) != 0;
    }

    bool Wanted(std::uint32_t port) const { return wanted_.count(port) != 0; }
    bool Synchronized(std::uint32_t port) const { return synchronized_.count(port) != 0; }

    /// Groups wanted but not yet written on the current socket, in ascending order so a retry is
    /// deterministic and a test can say what will be sent.
    std::vector<std::uint32_t> Pending() const
    {
        std::vector<std::uint32_t> out;
        for (const auto port : wanted_)
            if (synchronized_.count(port) == 0) out.push_back(port);
        return out;
    }

    /// A JOIN frame for this port left on the current socket. Only a wanted port can be synchronized: a
    /// frame for a group that has since been left tells us nothing we want to remember.
    void MarkSynchronized(std::uint32_t port)
    {
        if (wanted_.count(port) != 0) synchronized_.insert(port);
    }

    bool InSync() const { return wanted_.size() == synchronized_.size(); }

    /// A new socket has been told nothing, so everything wanted is pending again. Called when the socket
    /// is replaced, which is the only moment at which synchronized state stops meaning anything.
    void Reset() { synchronized_.clear(); }

    std::size_t WantedCount() const { return wanted_.size(); }
    std::size_t SynchronizedCount() const { return synchronized_.size(); }

private:
    std::set<std::uint32_t> wanted_;
    std::set<std::uint32_t> synchronized_;
};

/// Milliseconds to wait next, for a wait that must stay interruptible.
///
/// Returns 0 whenever less than a whole millisecond is left, not only once the deadline has passed, and
/// a caller must treat 0 as expiry rather than passing it on: poll() reads a negative timeout as no
/// timeout at all, and 0 as do-not-wait, so a negative value reaching poll() is the hang this exists to
/// make impossible. Otherwise it is the time remaining, capped at `slice` so the caller re-reads its
/// stop flag that often. Expiring up to a millisecond early is deliberate and harmless.
inline int PollSlice(std::chrono::steady_clock::time_point deadline,
                     std::chrono::steady_clock::time_point now,
                     std::chrono::milliseconds slice)
{
    if (deadline <= now) return 0;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    return static_cast<int>((left < slice ? left : slice).count());
}

/// Send something a bounded number of times, giving up at once if a shutdown has been asked for.
///
/// A control frame is one header, so a retry is cheap; what must not happen is an unbounded retry, or a
/// retry that keeps a shutdown waiting. Each attempt can cost SO_SNDTIMEO, so the stop flag is read
/// between attempts rather than only before the first.
template <typename Send>
bool SendWithRetry(Send send, int attempts, const std::atomic<bool>& stopping)
{
    for (int attempt = 0; attempt < attempts; ++attempt) {
        if (stopping.load()) return false;
        if (send()) return true;
    }
    return false;
}

/// Join every group that is wanted and not yet synchronized, stopping at the first that cannot be sent.
///
/// Stopping at the first failure is deliberate: once one frame will not go, the socket is unfit to carry
/// this membership and the caller replaces it, so sending the rest would only delay that. A port is
/// marked synchronized only after its frame was written, so a failure leaves the rest still pending and
/// a reconnection joins them.
template <typename Join>
bool SyncPending(Membership& membership, Join join)
{
    for (const auto port : membership.Pending()) {
        if (!join(port)) return false;
        membership.MarkSynchronized(port);
    }
    return true;
}
}
