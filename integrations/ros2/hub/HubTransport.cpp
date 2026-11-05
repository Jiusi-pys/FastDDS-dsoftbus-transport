#include "HubTransport.hpp"
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
constexpr int kSocketBufferBytes = 4 << 20;

int OpenSocket(const std::string& path)
{
    sockaddr_un where {};
    if (path.size() >= sizeof(where.sun_path)) return -1;
    where.sun_family = AF_UNIX;
    std::memcpy(where.sun_path, path.c_str(), path.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&where), sizeof(where)) != 0) { ::close(fd); return -1; }
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
    (void)::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kSocketBufferBytes, sizeof(kSocketBufferBytes));
    // Control frames use a blocking send. This bound keeps a stopped hub from holding a channel open or close.
    const timeval limit {1, 0};
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit));
    return fd;
}

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
    std::unique_lock<std::shared_mutex> lock(socket_gate_);
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

bool HubTransport::SendControl(int fd, std::uint32_t magic, std::uint32_t port) const
{
    FrameHeader header; header.magic = magic; header.port = port; header.src = address_;
    return ::send(fd, &header, kHeaderBytes, MSG_NOSIGNAL) == static_cast<ssize_t>(kHeaderBytes);
}

bool HubTransport::Connect()
{
    const int fd = OpenSocket(descriptor_.socket_path);
    if (fd < 0) return false;
    if (!SendControl(fd, kHelloMagic, 0)) { ::close(fd); return false; }
    {
        std::unique_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }
    // After the socket is published: a group opened from here on joins through it, and one opened earlier is in
    // this list. A join sent twice is harmless, a lost one would hide this process from discovery.
    std::shared_lock<std::shared_mutex> receivers(receivers_gate_);
    std::shared_lock<std::shared_mutex> socket(socket_gate_);
    for (const auto group : groups_) if (fd_ >= 0) (void)SendControl(fd_, kJoinMagic, group);
    return true;
}

bool HubTransport::init(const eprosima::fastrtps::rtps::PropertyPolicy*, const std::uint32_t&)
{
    const auto end = Clock::now() + kConnectLimit;
    while (!Connect()) {
        if (Clock::now() >= end) {
            std::fprintf(stderr, "FDDS_HUB_CLIENT_REFUSED reason=no_hub path=%s\n", descriptor_.socket_path.c_str());
            std::fflush(stderr);
            return false;
        }
        std::this_thread::sleep_for(kReconnectPause);
    }
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
            if (Connect()) {
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
            std::unique_lock<std::shared_mutex> lock(socket_gate_);
            if (fd_ == fd) { ::close(fd_); fd_ = -1; }
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
    {
        std::unique_lock<std::shared_mutex> lock(receivers_gate_);
        if (!receivers_.emplace(locator, receiver).second) return true;
        if (group) groups_.insert(locator.port);
    }
    if (group) {
        std::shared_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ >= 0) (void)SendControl(fd_, kJoinMagic, locator.port); // otherwise the reconnect joins it
    }
    return true;
}

bool HubTransport::CloseInputChannel(const Locator& locator)
{
    bool left = false;
    {
        std::unique_lock<std::shared_mutex> lock(receivers_gate_);
        if (receivers_.erase(locator) == 0) return false;
        if (IsGroup(AddressOf(locator))) { groups_.erase(locator.port); left = true; }
    }
    if (left) {
        std::shared_lock<std::shared_mutex> lock(socket_gate_);
        if (fd_ >= 0) (void)SendControl(fd_, kLeaveMagic, locator.port);
    }
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
