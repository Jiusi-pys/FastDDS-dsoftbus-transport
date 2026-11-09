#include "HubTransport.hpp"
#include "HubConnect.hpp"
#include <fastdds/rtps/common/LocatorSelector.hpp>
#include <fastdds/rtps/common/LocatorSelectorEntry.hpp>
#include <fastdds/rtps/common/PortParameters.h>
#include <fastdds/rtps/transport/TransportReceiverInterface.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>

namespace fastdds_dsoftbus::ros2_hub {
namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kConnectLimit = std::chrono::seconds(5);
constexpr auto kReconnectPause = std::chrono::milliseconds(100);
// kCancelSlice, kSocketBufferBytes, ConnectWithin and OpenHubSocket live in HubConnect.hpp. They are
// POSIX only and free of Fast DDS, so they can be compiled for the target on their own, which is the
// only compile this code can get on a build host with no Fast DDS headers for this line.
constexpr int kControlAttempts = 3;  // a control frame is one header; a retry is cheap and bounded

Locator GroupLocator(std::uint32_t port)
{
    Locator value;
    value.kind = kLocatorKind; value.port = port;
    std::memset(value.address, 0, sizeof(value.address));
    return value;
}

Address AddressOf(const Locator& locator)
{
    Address value{};
    std::memcpy(value.data(), locator.address, value.size());
    return value;
}

class HubSender final : public eprosima::fastrtps::rtps::SenderResource {
public:
    explicit HubSender(HubTransport& transport) : eprosima::fastrtps::rtps::SenderResource(kLocatorKind), transport_(&transport)
    {
        clean_up = [] {};
        send_lambda_ = [this](const eprosima::fastrtps::rtps::octet* data, std::uint32_t size, rt::LocatorsIterator* begin,
                rt::LocatorsIterator* end, const Clock::time_point& deadline) { return transport_->Send(data, size, begin, end, deadline); };
    }
    bool belongs_to(const HubTransport& transport) const { return transport_ == &transport; }
private:
    HubTransport* transport_;
};
}

rt::TransportInterface* HubTransportDescriptor::create_transport() const { return new HubTransport(*this); }

HubTransport::HubTransport(const HubTransportDescriptor& descriptor)
    : rt::TransportInterface(kLocatorKind), descriptor_(descriptor)
{
    // Non-zero: the all-zero address is the group address. Random: a restarted process is a new endpoint
    // on the hub, as it is to RTPS, and needs no allocation protocol.
    do {
        std::size_t filled = 0;
        while (filled < address_.size()) {
            const auto got = ::getrandom(address_.data() + filled, address_.size() - filled, 0);
            if (got > 0) filled += static_cast<std::size_t>(got);
        }
    } while (IsGroup(address_));
}

HubTransport::~HubTransport() { Stop(); }

void HubTransport::Stop()
{
    if (stopping_.exchange(true)) return;
    {
        std::shared_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR); // wakes the receive thread
    }
    if (receiver_.joinable()) receiver_.join();
    // membership_gate_ after the join, never across it, and before socket_gate_ as everywhere else: the
    // invariant that a retired socket never keeps claiming it was told anything holds without exception.
    std::lock_guard<std::mutex> membership(membership_gate_);
    std::unique_lock<std::shared_mutex> lock(socket_gate_);
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; membership_.Reset(); }
}

bool HubTransport::SendControl(int fd, std::uint32_t magic, std::uint32_t port) const
{
    FrameHeader header; header.magic = magic; header.port = port; header.src = address_;
    return ::send(fd, &header, kHeaderBytes, MSG_NOSIGNAL) == static_cast<ssize_t>(kHeaderBytes);
}

bool HubTransport::SendControlBounded(int fd, std::uint32_t magic, std::uint32_t port) const
{
    // Each attempt can cost SO_SNDTIMEO, so the stop flag is read between them: a shutdown must not
    // wait out three timeouts per group before the receive thread can be joined.
    return SendWithRetry([&] { return SendControl(fd, magic, port); }, kControlAttempts, stopping_);
}

bool HubTransport::SyncMembership(int fd)
{
    // membership_gate_ is held by every caller. This is the failure the audit found: a join whose frame
    // never left used to be discarded, leaving a process that believed it was attached, heard nothing,
    // and reported no error anywhere. SyncPending holds the policy, so a host test pins it.
    std::uint32_t failed = 0;
    const bool all = SyncPending(membership_, [&](std::uint32_t port) {
        if (SendControlBounded(fd, kJoinMagic, port)) return true;
        failed = port;
        return false;
    });
    if (!all) {
        std::fprintf(stderr, "FDDS_HUB_CLIENT_JOIN_FAILED port=%u\n", failed);
        std::fflush(stderr);
    }
    return all;
}

void HubTransport::DropSocket(int fd)
{
    // Shut the socket down; do NOT close it. close() does not wake a thread already blocked in recv()
    // on the same descriptor, and this runs on an application thread while the receive thread sits in
    // exactly that call. Closing here would wedge the receive thread for ever: no reconnection, so the
    // group is never rejoined and Send fails from then on; no disconnect visible to the hub, which goes
    // on routing frames into a buffer nobody reads; and Stop() would find no socket left to shut down
    // and join for ever -- the unbounded join this change exists to remove. It would also free the
    // descriptor number for immediate reuse while the receive thread still holds it.
    //
    // shutdown() does wake it. The receive thread then closes the descriptor and clears the membership
    // it can no longer vouch for, which is the one place that happens.
    std::shared_lock<std::shared_mutex> lock(socket_gate_);
    if (fd_ == fd && fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);
}

bool HubTransport::Connect(const Clock::time_point& deadline)
{
    const int fd = OpenHubSocket(descriptor_.socket_path, deadline, stopping_);
    if (fd < 0) return false;
    if (!SendControlBounded(fd, kHelloMagic, 0)) { ::close(fd); return false; }
    std::lock_guard<std::mutex> membership(membership_gate_);
    {
        std::unique_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
        // A new socket has been told nothing, whatever the old one knew.
        membership_.Reset();
    }
    // The socket is published first, so a group opened from here on joins through it; one opened earlier
    // is wanted and is joined now. membership_gate_ is what keeps those two from interleaving.
    std::shared_lock<std::shared_mutex> socket(socket_gate_);
    if (SyncMembership(fd)) return true;
    // This socket cannot carry this process's membership, so it is replaced rather than used: a
    // half-joined connection would hide the process from discovery with nothing reported.
    socket.unlock();
    DropSocket(fd);
    return false;
}

bool HubTransport::init(const eprosima::fastrtps::rtps::PropertyPolicy*, const std::uint32_t&)
{
    // One budget for the connect and for the retries together. Before, this deadline bounded only the
    // retries, and a single connect could sit in the kernel past it without ever being checked.
    const auto end = Clock::now() + kConnectLimit;
    while (!Connect(end)) {
        if (stopping_.load()) return false;
        if (Clock::now() >= end) {
            // Not necessarily absent: the hub may have accepted and then refused a control frame, in
            // which case SyncMembership has already said which group on the line above.
            std::fprintf(stderr, "FDDS_HUB_CLIENT_REFUSED reason=no_usable_hub path=%s\n",
                    descriptor_.socket_path.c_str());
            std::fflush(stderr);
            return false;
        }
        std::this_thread::sleep_for(kReconnectPause);
    }
    if (stopping_.load()) return false;  // Stop() ran during the connect: no thread nothing will join
    receiver_ = std::thread([this] { ReceiveLoop(); });
    return true;
}

void HubTransport::Dispatch(const FrameHeader& header, const std::uint8_t* payload, std::size_t size)
{
    Locator local;
    local.kind = kLocatorKind; local.port = header.port;
    std::memcpy(local.address, header.dst.data(), header.dst.size());
    Locator remote;
    remote.kind = kLocatorKind; remote.port = header.port;
    std::memcpy(remote.address, header.src.data(), header.src.size());
    std::shared_lock<std::shared_mutex> lock(receivers_gate_);
    const auto found = receivers_.find(local);
    if (found != receivers_.end())
        found->second->OnDataReceived(payload, static_cast<std::uint32_t>(size), local, remote);
}

void HubTransport::ReceiveLoop()
{
    std::vector<std::uint8_t> buffer(kMaxRecordBytes);
    bool reported = false;
    while (!stopping_.load()) {
        int fd;
        { std::shared_lock<std::shared_mutex> lock(socket_gate_); fd = fd_; }
        if (fd < 0) {
            // A deadline per attempt. The loop itself ends when Stop() sets stopping_, which is checked
            // above and inside the connect, so the receive thread cannot outlive a shutdown.
            if (Connect(Clock::now() + kConnectLimit)) {
                std::fprintf(stderr, "FDDS_HUB_CLIENT_RECONNECTED\n"); std::fflush(stderr);
                reported = false;
            } else {
                std::this_thread::sleep_for(kReconnectPause);
            }
            continue;
        }
        const auto got = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (got <= 0) {
            if (got < 0 && errno == EINTR) continue;
            if (stopping_.load()) break;
            if (!reported) { std::fprintf(stderr, "FDDS_HUB_CLIENT_DISCONNECTED\n"); std::fflush(stderr); reported = true; }
            // membership_gate_ first, the order every path that replaces the socket uses. Retiring a
            // socket without it would leave membership claiming that groups had been told to a socket
            // that is gone, which is what makes the fd_ == fd comparisons elsewhere safe.
            std::lock_guard<std::mutex> membership(membership_gate_);
            std::unique_lock<std::shared_mutex> lock(socket_gate_);
            if (fd_ == fd) { ::close(fd_); fd_ = -1; membership_.Reset(); }
            continue;
        }
        FrameHeader header;
        if (ParseHeader(buffer.data(), static_cast<std::size_t>(got), header) && header.magic == kFrameMagic)
            Dispatch(header, buffer.data() + kHeaderBytes, static_cast<std::size_t>(got) - kHeaderBytes);
    }
}

bool HubTransport::Send(const std::uint8_t* data, std::uint32_t size, rt::LocatorsIterator* begin, rt::LocatorsIterator* end,
        const Clock::time_point& deadline)
{
    if (size > kMaxPayloadBytes) return false;
    bool all = true;
    std::shared_lock<std::shared_mutex> lock(socket_gate_);
    for (auto& it = *begin; it != *end; ++it) {
        const Locator& target = *it;
        if (target.kind != kLocatorKind) continue;
        if (fd_ < 0) { all = false; continue; } // like a network outage: RTPS retries reliable data
        FrameHeader header;
        header.port = target.port; header.dst = AddressOf(target); header.src = address_;
        iovec parts[2] = {{&header, kHeaderBytes}, {const_cast<std::uint8_t*>(data), size}};
        msghdr message {};
        message.msg_iov = parts; message.msg_iovlen = 2;
        for (;;) {
            if (::sendmsg(fd_, &message, MSG_DONTWAIT | MSG_NOSIGNAL) >= 0) break;
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) { all = false; break; }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (left <= 0) { all = false; break; }
            pollfd wait {fd_, POLLOUT, 0};
            (void)::poll(&wait, 1, static_cast<int>(left > 1000 ? 1000 : left));
        }
    }
    return all;
}

Locator HubTransport::Own(std::uint32_t port) const
{
    Locator value;
    value.kind = kLocatorKind; value.port = port;
    std::memcpy(value.address, address_.data(), address_.size());
    return value;
}

bool HubTransport::IsLocatorSupported(const Locator& locator) const { return locator.kind == kLocatorKind; }
bool HubTransport::is_locator_allowed(const Locator& locator) const { return IsLocatorSupported(locator); }
bool HubTransport::IsInputChannelOpen(const Locator& locator) const
{
    std::shared_lock<std::shared_mutex> lock(receivers_gate_);
    return receivers_.count(locator) != 0;
}
Locator HubTransport::RemoteToMainLocal(const Locator& remote) const { return Own(remote.port); }
bool HubTransport::transform_remote_locator(const Locator& remote, Locator& out) const
{
    if (!IsLocatorSupported(remote)) return false;
    out = remote;
    return true;
}
bool HubTransport::DoInputLocatorsMatch(const Locator& a, const Locator& b) const { return a == b; }
LocatorList HubTransport::NormalizeLocator(const Locator& locator)
{
    LocatorList list;
    list.push_back(locator);
    return list;
}
bool HubTransport::is_local_locator(const Locator& locator) const
{
    return IsLocatorSupported(locator) && AddressOf(locator) == address_;
}

bool HubTransport::OpenOutputChannel(rt::SendResourceList& list, const Locator& locator)
{
    if (!IsLocatorSupported(locator)) return false;
    for (const auto& resource : list) {
        const auto* sender = dynamic_cast<const HubSender*>(resource.get());
        if (sender != nullptr && sender->belongs_to(*this)) return true;
    }
    list.emplace_back(std::make_unique<HubSender>(*this));
    return true;
}

bool HubTransport::OpenInputChannel(const Locator& locator, rt::TransportReceiverInterface* receiver, std::uint32_t)
{
    if (!IsLocatorSupported(locator)) return false;
    const bool group = IsGroup(AddressOf(locator));
    if (!group && AddressOf(locator) != address_) return false;
    // After Stop() there is no receive thread, so nothing would ever reconnect and join this. Saying so
    // is better than returning true and leaving the caller holding a channel that cannot be served.
    if (stopping_.load()) return false;
    // membership_gate_ before receivers_gate_, the order every path here uses.
    std::unique_lock<std::mutex> membership(membership_gate_, std::defer_lock);
    if (group) membership.lock();
    {
        std::unique_lock<std::shared_mutex> lock(receivers_gate_);
        if (!receivers_.emplace(locator, receiver).second) return true;
        if (group) membership_.Want(locator.port);
    }
    if (!group) return true;
    int fd = -1;
    { std::shared_lock<std::shared_mutex> lock(socket_gate_); fd = fd_; }
    if (fd < 0) return true;  // no socket to tell: the group is wanted, and the reconnect joins it
    {
        std::shared_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ == fd && SyncMembership(fd)) return true;
    }
    // The bounded retry did not get the join out. The socket is dropped so the receive thread replaces
    // it and joins every wanted group again: the membership is recorded, so it is established on
    // reconnect rather than silently lost, which is what used to happen here.
    DropSocket(fd);
    return true;
}

bool HubTransport::CloseInputChannel(const Locator& locator)
{
    const bool group = IsGroup(AddressOf(locator));
    std::unique_lock<std::mutex> membership(membership_gate_, std::defer_lock);
    if (group) membership.lock();
    bool left = false;
    {
        std::unique_lock<std::shared_mutex> lock(receivers_gate_);
        if (receivers_.erase(locator) == 0) return false;
        if (group) left = membership_.Unwant(locator.port);
    }
    if (!left) return true;
    int fd = -1;
    { std::shared_lock<std::shared_mutex> lock(socket_gate_); fd = fd_; }
    if (fd < 0) return true;  // nothing to tell: a reconnect joins only what is still wanted
    // During a shutdown SendWithRetry declines before the first attempt, and reporting that as a lost
    // LEAVE would put a frame nobody sent into the log an operator reads as evidence of one.
    if (stopping_.load()) return true;
    {
        std::shared_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ != fd || SendControlBounded(fd, kLeaveMagic, locator.port)) return true;
    }
    std::fprintf(stderr, "FDDS_HUB_CLIENT_LEAVE_FAILED port=%u\n", locator.port);
    std::fflush(stderr);
    // The hub would keep sending this group's frames to a process with no receiver for them. Replacing
    // the socket resynchronizes membership from what is still wanted, which no longer includes this.
    DropSocket(fd);
    return true;
}

void HubTransport::select_locators(eprosima::fastrtps::rtps::LocatorSelector& selector) const
{
    auto& entries = selector.transport_starts();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto* entry = entries[i];
        if (!entry->transport_should_process || !entry->enabled) continue;
        bool added = false;
        for (std::size_t index = 0; index < entry->unicast.size(); ++index) {
            const auto& remote = entry->unicast[index];
            if (!IsLocatorSupported(remote)) continue;
            // Readers of one Participant share a locator, and one send reaches them all. Compared directly, because
            // the generic check reads these addresses as IPv6.
            bool selected = false;
            selector.for_each([&](const Locator& value) { if (value == remote) selected = true; });
            if (selected) continue;
            if (entry->state.unicast.push_back(index)) added = true;
        }
        // Group locators are used only when an entry has no unicast locator: every unicast locator is one
        // process, so data for one reader does not reach the others.
        for (std::size_t index = 0; !added && index < entry->multicast.size(); ++index) {
            if (!IsLocatorSupported(entry->multicast[index])) continue;
            if (entry->state.multicast.push_back(index)) added = true;
        }
        if (added) selector.select(i);
    }
}

bool HubTransport::getDefaultMetatrafficMulticastLocators(LocatorList& list, std::uint32_t port) const
{
    list.push_back(GroupLocator(port));
    return true;
}
bool HubTransport::getDefaultMetatrafficUnicastLocators(LocatorList& list, std::uint32_t port) const
{
    list.push_back(Own(port));
    return true;
}
bool HubTransport::getDefaultUnicastLocators(LocatorList& list, std::uint32_t port) const
{
    list.push_back(Own(port));
    return true;
}
bool HubTransport::fillMetatrafficMulticastLocator(Locator& locator, std::uint32_t port) const
{
    if (locator.port == 0) locator.port = port;
    return true;
}
bool HubTransport::fillMetatrafficUnicastLocator(Locator& locator, std::uint32_t port) const
{
    if (locator.port == 0) locator.port = port;
    return true;
}
bool HubTransport::fillUnicastLocator(Locator& locator, std::uint32_t port) const
{
    if (locator.port == 0) locator.port = port;
    return true;
}
bool HubTransport::configureInitialPeerLocator(Locator& locator, const eprosima::fastrtps::rtps::PortParameters&, std::uint32_t, LocatorList& list) const
{
    list.push_back(locator);
    return true;
}
}
