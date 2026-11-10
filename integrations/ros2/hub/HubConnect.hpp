#pragma once
// Connecting a hub client's local socket, bounded by a deadline and cancellable by a stop flag. POSIX
// only, and deliberately free of Fast DDS: the build host has no Fast DDS headers for this line, so
// keeping this here is what lets it be compiled for the OHOS target, and tested on Linux, on its own.
//
// A blocking connect is how a shutdown turns into a hang. connect() on an AF_UNIX SOCK_SEQPACKET socket
// returns ECONNREFUSED at once when nothing is listening, but a hub that is LISTENING AND NOT ACCEPTING
// leaves a blocking caller in the kernel for as long as it likes. SO_SNDTIMEO does not help: it is set
// after the connect and does not apply to it.
//
// So the socket is non-blocking for the connect. What that gives on AF_UNIX is NOT what it gives on TCP:
// there is no in-progress state to wait on. The kernel either completes the connection or refuses it,
// and a full backlog is refused with EAGAIN. Waiting for writability would prove nothing, because there
// is no connection being established -- an unconnected socket is writable, and SO_ERROR reads 0, so a
// poll-and-check-SO_ERROR shape would report success for a socket that is not connected at all.
// Retrying the connect is the only thing that can succeed, so that is what the deadline bounds.
// EINPROGRESS is still handled, so a socket type that does report it is not failed by mistake.
#include "HubLifecycle.hpp"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace fastdds_dsoftbus::ros2_hub {

// How long one wait inside the connect may last before the stop flag and the deadline are re-read:
// short enough that a shutdown is honoured promptly, long enough not to spin on a full backlog.
constexpr auto kCancelSlice = std::chrono::milliseconds(20);
constexpr int kSocketBufferBytes = 4 << 20;

/// Wait out a connect that really is in progress, for a socket type that reports one.
inline bool FinishInProgress(int fd, const std::chrono::steady_clock::time_point& deadline,
        const std::atomic<bool>& stopping)
{
    for (;;) {
        if (stopping.load()) { errno = ECANCELED; return false; }
        const int slice = PollSlice(deadline, std::chrono::steady_clock::now(), kCancelSlice);
        if (slice == 0) { errno = ETIMEDOUT; return false; }
        pollfd wait {fd, POLLOUT, 0};
        const int ready = ::poll(&wait, 1, slice);
        if (ready < 0) { if (errno == EINTR) continue; return false; }
        if (ready == 0) continue;  // the slice expired; the deadline and the stop flag are re-read above
        // poll() counts a descriptor with ANY event, so a hangup or an error arrives here as readiness.
        // Reporting those as a finished connect is how a dead socket gets used.
        if ((wait.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) { errno = ECONNRESET; return false; }
        int error = 0;
        socklen_t size = sizeof(error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0) return false;
        if (error != 0) { errno = error; return false; }
        return true;
    }
}

/// Connect by the deadline, or fail. errno is left describing why: ETIMEDOUT for the deadline,
/// ECANCELED when the stop flag was set, otherwise whatever the kernel last said.
inline bool ConnectWithin(int fd, const sockaddr_un& where,
        const std::chrono::steady_clock::time_point& deadline, const std::atomic<bool>& stopping)
{
    for (;;) {
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&where), sizeof(where)) == 0) return true;
        const int refused = errno;
        if (refused == EINTR) continue;
        if (refused == EINPROGRESS || refused == EALREADY) return FinishInProgress(fd, deadline, stopping);
        // EAGAIN on AF_UNIX means the listener's backlog is full: it is accepting too slowly, or not at
        // all. That is the case this whole function exists for, and the only cure is to ask again.
        if (refused != EAGAIN && refused != EWOULDBLOCK) { errno = refused; return false; }
        if (stopping.load()) { errno = ECANCELED; return false; }
        const int slice = PollSlice(deadline, std::chrono::steady_clock::now(), kCancelSlice);
        if (slice == 0) { errno = ETIMEDOUT; return false; }
        // A sleep, not a poll: no descriptor event says "the backlog drained". One slice at a time, so
        // the stop flag and the deadline are re-read that often and Stop() is never waited out.
        //
        // sleep_for, not nanosleep: a timespec needs tv_nsec below one second, so a kCancelSlice of a
        // second or more would make nanosleep fail with EINVAL at once and spin a core for the whole
        // deadline. That trap is 40 lines from the constant, and the constant has already been retuned.
        std::this_thread::sleep_for(std::chrono::milliseconds(slice));
    }
}

/// A connected client socket for the hub at `path`, or -1. Never blocks past `deadline`, and gives up as
/// soon as `stopping` is set.
inline int OpenHubSocket(const std::string& path, const std::chrono::steady_clock::time_point& deadline,
        const std::atomic<bool>& stopping)
{
    sockaddr_un where {};
    if (path.size() >= sizeof(where.sun_path)) return -1;
    where.sun_family = AF_UNIX;
    std::memcpy(where.sun_path, path.c_str(), path.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    if (!ConnectWithin(fd, where, deadline, stopping)) {
        const int failure = errno;
        ::close(fd);
        errno = failure;  // close() must not overwrite why the connect failed
        return -1;
    }
    // Back to blocking, because every send after this is a blocking send bounded by SO_SNDTIMEO below.
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) { ::close(fd); return -1; }
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
    // Control frames use a blocking send. This bound keeps a stopped hub from holding a channel open or close.
    const timeval limit {1, 0};
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
    return fd;
}
}
