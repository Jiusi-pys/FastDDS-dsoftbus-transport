// Host tests for the hub client's membership bookkeeping and its deadline slicing. No sockets, Fast DDS
// or SDK, so these run on the build host; the socket work that uses them is POSIX and only a board can
// verify it.
#include "HubLifecycle.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace fastdds_dsoftbus::ros2_hub;
using Clock = std::chrono::steady_clock;

namespace {
int failures = 0;
void check(bool value, const char* what)
{
    if (value) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++failures;
}

bool same(const std::vector<std::uint32_t>& got, const std::vector<std::uint32_t>& want) { return got == want; }

void a_wanted_group_is_pending_until_its_join_is_written()
{
    Membership m;
    check(m.Want(7400), "a new group is news");
    check(!m.Want(7400), "the same group twice is not");
    check(m.Wanted(7400) && !m.Synchronized(7400), "wanted, not yet told to the hub");
    check(same(m.Pending(), {7400}), "pending until the join leaves");
    check(!m.InSync(), "a process with an unwritten join is not in sync");
    m.MarkSynchronized(7400);
    check(m.Synchronized(7400) && m.Pending().empty() && m.InSync(), "written, so nothing is pending");
}

void a_reconnection_makes_every_group_pending_again()
{
    // The failure this exists for: a new socket has been told nothing, and a client that kept its old
    // synchronized state would send no joins and then hear nothing, with no error anywhere.
    Membership m;
    m.Want(7400); m.Want(7650); m.Want(7900);
    for (const auto port : m.Pending()) m.MarkSynchronized(port);
    check(m.InSync() && m.SynchronizedCount() == 3, "all three joined");
    m.Reset();
    check(!m.InSync(), "a replaced socket is not in sync");
    check(same(m.Pending(), {7400, 7650, 7900}), "every group is pending again, in order");
    check(m.WantedCount() == 3 && m.SynchronizedCount() == 0, "wanting survives, having told does not");
}

void leaving_a_group_drops_what_the_hub_was_told()
{
    Membership m;
    m.Want(7400); m.MarkSynchronized(7400);
    check(m.Unwant(7400), "it was wanted");
    check(!m.Wanted(7400) && !m.Synchronized(7400), "neither wanted nor synchronized after a leave");
    check(m.Pending().empty() && m.InSync(), "a group nobody wants is not pending");
    check(!m.Unwant(7400), "leaving twice is not news");
    // A reconnection must not resurrect it: Reset only makes WANTED groups pending.
    m.Want(7650); m.MarkSynchronized(7650); m.Reset();
    check(same(m.Pending(), {7650}), "a left group does not come back on reconnect");
}

void a_group_left_before_its_join_was_written_stays_gone()
{
    // Open then Close in quick succession, with the socket replaced in between: the join for 7400 is
    // still unwritten when the leave arrives, and the reconnection must not then join it.
    Membership m;
    m.Want(7400);
    check(same(m.Pending(), {7400}), "pending");
    m.Unwant(7400);
    m.Reset();
    check(m.Pending().empty(), "an unwritten join for a group since left is not sent");
}

void synchronizing_a_group_nobody_wants_is_ignored()
{
    // A late success for a join whose group has since been left must not read as membership.
    Membership m;
    m.MarkSynchronized(7400);
    check(!m.Synchronized(7400), "a port that was never wanted cannot be synchronized");
    m.Want(7400); m.Unwant(7400); m.MarkSynchronized(7400);
    check(!m.Synchronized(7400) && m.Pending().empty(), "still nothing, after a leave");
}

void pending_is_ordered_and_covers_only_the_gap()
{
    Membership m;
    for (const auto port : {7900u, 7400u, 7650u}) m.Want(port);
    m.MarkSynchronized(7650);
    check(same(m.Pending(), {7400, 7900}), "ascending, and only what is unwritten");
    check(m.WantedCount() == 3 && m.SynchronizedCount() == 1, "counts are what they say");
}

void a_slice_never_waits_forever_and_never_waits_past_the_deadline()
{
    const auto now = Clock::now();
    const auto slice = std::chrono::milliseconds(100);
    // The bug this guards: poll() reads a negative timeout as no timeout at all, so a deadline already
    // past has to come back as 0 and be treated as expiry, never as a wait with no end. Note that an
    // exactly-now deadline cannot distinguish `deadline <= now` from `deadline < now`, because under a
    // millisecond truncates to 0 either way; that is why the assertion below is a property over a range
    // rather than a claim about one boundary, which would hold whatever the comparison said.
    check(PollSlice(now - std::chrono::seconds(5), now, slice) == 0, "a deadline long past has expired");
    check(PollSlice(now + std::chrono::milliseconds(30), now, slice) == 30, "less than a slice waits that long");
    check(PollSlice(now + std::chrono::seconds(5), now, slice) == 100, "more than a slice waits one slice");
    check(PollSlice(now + std::chrono::microseconds(500), now, slice) == 0,
          "under a millisecond rounds down, so the caller re-checks rather than spins on a negative wait");
    // Never negative and never longer than a slice, across the whole range a caller can present,
    // including a deadline so far off that a careless cast would overflow.
    bool bounded = true;
    for (int offset = -5000; offset <= 5000; offset += 137) {
        const int got = PollSlice(now + std::chrono::milliseconds(offset), now, slice);
        bounded = bounded && got >= 0 && got <= slice.count();
    }
    const int far = PollSlice(Clock::time_point::max(), now, slice);
    check(bounded, "a slice is never negative and never longer than one slice");
    check(far >= 0 && far <= slice.count(), "a deadline at the end of time still yields one slice");
    // A whole budget consumed one slice at a time reaches expiry and stops there.
    int taken = 0;
    const auto deadline = now + std::chrono::milliseconds(250);
    for (auto at = now; PollSlice(deadline, at, slice) > 0; at += std::chrono::milliseconds(PollSlice(deadline, at, slice)))
        ++taken;
    check(taken == 3, "250 ms in 100 ms slices is three waits, then expiry");
}

void a_control_frame_is_retried_a_bounded_number_of_times()
{
    std::atomic<bool> quiet{false};
    int tries = 0;
    check(SendWithRetry([&] { ++tries; return true; }, 3, quiet) && tries == 1,
          "a frame that goes first time is sent once");
    tries = 0;
    check(!SendWithRetry([&] { ++tries; return false; }, 3, quiet) && tries == 3,
          "a frame that never goes is tried exactly the bounded number of times");
    tries = 0;
    check(SendWithRetry([&] { return ++tries == 2; }, 3, quiet) && tries == 2,
          "a frame that goes on the second attempt stops there");
    // The stop flag is read between attempts, not only before the first: each attempt can cost
    // SO_SNDTIMEO, and a shutdown must not wait out three of them per group.
    std::atomic<bool> stopping{false};
    tries = 0;
    check(!SendWithRetry([&] { ++tries; stopping.store(true); return false; }, 5, stopping) && tries == 1,
          "a shutdown during the first attempt stops the retry");
    tries = 0;
    stopping.store(true);
    check(!SendWithRetry([&] { ++tries; return true; }, 3, stopping) && tries == 0,
          "a shutdown already asked for sends nothing at all");
}

void joining_stops_at_the_first_frame_that_will_not_go()
{
    // Once one frame will not go the socket is unfit, so the caller replaces it; sending the rest would
    // only delay that. And a port is synchronized only after its frame was written, so what is left
    // stays pending for the reconnection to join.
    Membership m;
    m.Want(7400); m.Want(7650); m.Want(7900);
    std::vector<std::uint32_t> sent;
    check(SyncPending(m, [&](std::uint32_t port) { sent.push_back(port); return true; }), "all three go");
    check(same(sent, {7400, 7650, 7900}) && m.InSync(), "in order, and nothing left pending");

    Membership partial;
    partial.Want(7400); partial.Want(7650); partial.Want(7900);
    sent.clear();
    const bool all = SyncPending(partial, [&](std::uint32_t port) {
        sent.push_back(port);
        return port != 7650;
    });
    check(!all, "a frame that would not go fails the sync");
    check(same(sent, {7400, 7650}), "and it stops there rather than trying the rest");
    check(partial.Synchronized(7400), "what was written counts");
    check(!partial.Synchronized(7650) && !partial.Synchronized(7900), "what was not written does not");
    check(same(partial.Pending(), {7650, 7900}), "the rest stay pending for the reconnection");

    Membership none;
    check(SyncPending(none, [](std::uint32_t) { return false; }), "nothing wanted needs nothing sent");
}
}

int main()
{
    a_wanted_group_is_pending_until_its_join_is_written();
    a_reconnection_makes_every_group_pending_again();
    leaving_a_group_drops_what_the_hub_was_told();
    a_group_left_before_its_join_was_written_stays_gone();
    synchronizing_a_group_nobody_wants_is_ignored();
    pending_is_ordered_and_covers_only_the_gap();
    a_slice_never_waits_forever_and_never_waits_past_the_deadline();
    a_control_frame_is_retried_a_bounded_number_of_times();
    joining_stops_at_the_first_frame_that_will_not_go();
    if (failures == 0) std::printf("hub lifecycle tests passed\n");
    return failures == 0 ? 0 : 1;
}
