// Tests for the hub client's connect, against real AF_UNIX SOCK_SEQPACKET sockets. POSIX only: this is
// the half of the hub lifecycle that no amount of pure logic can stand in for, because what it has to
// get right is kernel behaviour.
//
// Two kernel facts this code depends on are asserted here rather than assumed, because both were got
// wrong first time:
//   * a non-blocking AF_UNIX connect to a listener whose backlog is full fails with EAGAIN, not
//     EINPROGRESS, and there is no in-progress connection to wait on. So the deadline has to bound a
//     retry of connect() itself.
//   * shutdown() wakes a thread blocked in recv(); close() does not. DropSocket relies on the former,
//     because it runs on an application thread while the receive thread is inside recv().
#include "HubConnect.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
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

long ms_since(Clock::time_point from)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - from).count();
}

/// Where to put the test sockets. /tmp is read-only on an OpenHarmony board, so the directory has to be
/// given there; S2_SOCKET_DIR is how the device run passes /data/local/tmp.
std::string socket_dir()
{
    const char* given = ::getenv("S2_SOCKET_DIR");
    return (given != nullptr && given[0] != '\0') ? std::string(given) : std::string("/tmp");
}

sockaddr_un address_of(const std::string& path)
{
    sockaddr_un where {};
    where.sun_family = AF_UNIX;
    std::memcpy(where.sun_path, path.c_str(), path.size() + 1);
    return where;
}

/// A listening socket that never accepts, like a hub that is up and wedged.
struct Listener {
    std::string path;
    int fd = -1;

    explicit Listener(const char* name, int backlog)
        : path(socket_dir() + "/" + name + "." + std::to_string(::getpid()))
    {
        ::unlink(path.c_str());
        fd = ::socket(AF_UNIX, SOCK_SEQPACKET, 0);
        const auto where = address_of(path);
        if (::bind(fd, reinterpret_cast<const sockaddr*>(&where), sizeof(where)) != 0) { fd = -1; return; }
        if (::listen(fd, backlog) != 0) fd = -1;
    }
    ~Listener()
    {
        if (fd >= 0) ::close(fd);
        ::unlink(path.c_str());
    }
};

/// Connect until the listener's backlog refuses one, so the next attempt is the case under test.
std::vector<int> fill_backlog(const std::string& path)
{
    std::vector<int> held;
    const auto where = address_of(path);
    for (int guard = 0; guard < 4096; ++guard) {
        const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
        if (fd < 0) break;
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&where), sizeof(where)) == 0) {
            held.push_back(fd);
            continue;
        }
        ::close(fd);
        break;
    }
    return held;
}

void a_full_backlog_refuses_with_eagain_not_einprogress()
{
    // The fact the whole design turns on. If this ever reports EINPROGRESS, the retry loop in
    // ConnectWithin is the wrong shape and FinishInProgress is the right one.
    Listener hub("s2hub_eagain", 1);
    check(hub.fd >= 0, "the test listener was created");
    auto held = fill_backlog(hub.path);
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    const auto where = address_of(hub.path);
    const int rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&where), sizeof(where));
    const int failed = errno;
    check(rc != 0, "a full backlog does not accept another connection");
    check(failed == EAGAIN || failed == EWOULDBLOCK,
          "a full AF_UNIX backlog refuses with EAGAIN; EINPROGRESS would mean a different design");
    ::close(fd);
    for (const int one : held) ::close(one);
}

void a_wedged_hub_fails_by_the_deadline_instead_of_hanging()
{
    Listener hub("s2hub_deadline", 1);
    auto held = fill_backlog(hub.path);
    std::atomic<bool> stopping{false};
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    const auto started = Clock::now();
    const bool got = ConnectWithin(fd, address_of(hub.path), started + std::chrono::milliseconds(200), stopping);
    const int failed = errno;
    const long took = ms_since(started);
    check(!got, "a hub that is listening and not accepting does not connect");
    check(failed == ETIMEDOUT, "it fails with ETIMEDOUT, so the caller can tell the deadline expired");
    check(took >= 150, "it actually waited for the deadline rather than failing at once");
    check(took < 2000, "and it returned at the deadline rather than hanging");
    ::close(fd);
    for (const int one : held) ::close(one);
}

void a_stop_flag_cancels_the_retry()
{
    // Named for what it covers: there is no connect in progress on AF_UNIX, so what gets cancelled is
    // the EAGAIN retry loop, not FinishInProgress — which this suite does not reach on this platform.
    Listener hub("s2hub_cancel", 1);
    auto held = fill_backlog(hub.path);
    std::atomic<bool> stopping{false};
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        stopping.store(true);
    });
    const auto started = Clock::now();
    // A deadline far beyond the cancellation, so only the stop flag can end this.
    const bool got = ConnectWithin(fd, address_of(hub.path), started + std::chrono::seconds(30), stopping);
    const int failed = errno;
    const long took = ms_since(started);
    stopper.join();
    check(!got, "a cancelled connect does not report success");
    check(failed == ECANCELED, "it says it was cancelled, not that it timed out");
    check(took < 2000, "it gave up when asked, not at its deadline");
    ::close(fd);
    for (const int one : held) ::close(one);
}

void a_retry_connects_once_the_backlog_drains()
{
    // The success half of the retry, and the one kernel assumption left resting on reading: POSIX leaves
    // a socket's state unspecified after a failed connect, and on TCP retrying the same descriptor is
    // famously wrong. If AF_UNIX behaved that way too, every hub restart under load would turn into a
    // hard FDDS_HUB_CLIENT_REFUSED, and the deadline-bounded retry would only ever time out.
    Listener hub("s2hub_drain", 1);
    check(hub.fd >= 0, "the test listener was created");
    auto held = fill_backlog(hub.path);
    check(!held.empty(), "the backlog was filled, so the first connect below must be refused");
    std::atomic<bool> stopping{false};
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0);
    // The backlog drains part way through the attempt, as a hub that resumes accepting would drain it.
    std::thread accepter([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const int taken = ::accept(hub.fd, nullptr, nullptr);
        if (taken >= 0) ::close(taken);
    });
    const auto started = Clock::now();
    const bool got = ConnectWithin(fd, address_of(hub.path), started + std::chrono::seconds(5), stopping);
    const long took = ms_since(started);
    accepter.join();
    check(got, "the retry connects on the same descriptor once the backlog drains");
    check(took >= 100, "it really did have to wait for the drain rather than connecting at once");
    check(took < 3000, "and it connected well inside the deadline");
    if (fd >= 0) ::close(fd);
    for (const int one : held) ::close(one);
}

void a_hub_that_accepts_connects()
{
    Listener hub("s2hub_ok", 4);
    std::atomic<bool> stopping{false};
    const int fd = OpenHubSocket(hub.path, Clock::now() + std::chrono::seconds(5), stopping);
    check(fd >= 0, "a listening hub with room in its backlog connects");
    if (fd >= 0) {
        // OpenHubSocket must hand back a blocking socket: every send after it is a blocking send.
        const int flags = ::fcntl(fd, F_GETFL, 0);
        check(flags >= 0 && (flags & O_NONBLOCK) == 0, "the socket is back in blocking mode");
        ::close(fd);
    }
}

void nothing_listening_fails_at_once()
{
    std::atomic<bool> stopping{false};
    const std::string absent = socket_dir() + "/s2hub_absent." + std::to_string(::getpid());
    ::unlink(absent.c_str());
    const auto started = Clock::now();
    const int fd = OpenHubSocket(absent, Clock::now() + std::chrono::seconds(10), stopping);
    const long took = ms_since(started);
    check(fd < 0, "there is no hub to connect to");
    check(took < 1000, "an absent hub is refused at once rather than waited out to the deadline");
}

void shutdown_wakes_a_blocked_receive()
{
    // What DropSocket depends on. close() on this descriptor would NOT wake the reader, which is why
    // DropSocket shuts the socket down and leaves the close to the thread that was reading.
    int pair[2] = {-1, -1};
    check(::socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0, "a socket pair was created");
    std::atomic<bool> returned{false};
    std::thread reader([&] {
        char buffer[8];
        (void)::recv(pair[0], buffer, sizeof(buffer), 0);
        returned.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    check(!returned.load(), "the reader is blocked, so this test is measuring what it means to");
    ::shutdown(pair[0], SHUT_RDWR);
    reader.join();
    check(returned.load(), "shutdown() woke the blocked recv(); DropSocket relies on exactly this");
    ::close(pair[0]);
    ::close(pair[1]);
}
}

int main()
{
    a_full_backlog_refuses_with_eagain_not_einprogress();
    a_wedged_hub_fails_by_the_deadline_instead_of_hanging();
    a_stop_flag_cancels_the_retry();
    a_retry_connects_once_the_backlog_drains();
    a_hub_that_accepts_connects();
    nothing_listening_fails_at_once();
    shutdown_wakes_a_blocked_receive();
    if (failures == 0) std::printf("hub connect tests passed\n");
    return failures == 0 ? 0 : 1;
}
