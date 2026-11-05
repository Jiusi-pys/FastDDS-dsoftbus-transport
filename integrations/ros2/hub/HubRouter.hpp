#pragma once
// Wire format and routing for the per-board ROS 2 hub. Pure C++: no sockets, no Fast DDS, no SDK.
//
// Each ROS process attaches to its board's hub through a local socket. A process has a random 16-byte
// address (a unicast locator) and joins group locators, the same two kinds of locator RTPS already uses
// on UDP. The hub never parses RTPS: it routes on this header alone, and it may drop a frame, as a
// network may, so one slow process can never stall the others.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace fastdds_dsoftbus::ros2_hub {

using Address = std::array<std::uint8_t, 16>;

inline constexpr std::int32_t kLocatorKind = 0x4842;
inline constexpr std::uint32_t kFrameMagic = 0x46485542u; // an RTPS message for dst
inline constexpr std::uint32_t kHelloMagic = 0x4648454Cu; // first frame on a connection: src is the client address
inline constexpr std::uint32_t kJoinMagic = 0x46484A4Eu;  // port is a group to join
inline constexpr std::uint32_t kLeaveMagic = 0x46484C56u;

struct FrameHeader {
    std::uint32_t magic = kFrameMagic;
    std::uint32_t port = 0;
    Address dst{};
    Address src{};
};
inline constexpr std::size_t kHeaderBytes = 40;
static_assert(sizeof(FrameHeader) == kHeaderBytes, "the header is a wire format");
inline constexpr std::size_t kMaxRecordBytes = 65500; // the DSoftBus record limit used by the device suite
inline constexpr std::size_t kMaxPayloadBytes = kMaxRecordBytes - kHeaderBytes;

inline bool IsGroup(const Address& address)
{
    for (const auto byte : address) if (byte != 0) return false;
    return true;
}

// False for anything shorter than a header, oversized, or of unknown type.
inline bool ParseHeader(const std::uint8_t* data, std::size_t size, FrameHeader& out)
{
    if (size < kHeaderBytes || size > kMaxRecordBytes) return false;
    std::memcpy(&out, data, kHeaderBytes);
    return out.magic == kFrameMagic || out.magic == kHelloMagic || out.magic == kJoinMagic || out.magic == kLeaveMagic;
}

class Router {
public:
    using Client = int;
    static constexpr Client kLink = -1;

    // A reconnecting process keeps its address, so the new connection replaces the old one here even if
    // the old connection's close has not been seen yet.
    void Attach(Client client, const Address& address)
    {
        const auto existing = by_address_.find(address);
        if (existing != by_address_.end() && existing->second != client) Detach(existing->second);
        Detach(client); // a repeated hello must not leave the connection's old address routable
        by_address_[address] = client;
        address_of_[client] = address;
    }
    void Join(Client client, std::uint32_t port) { if (address_of_.count(client) != 0) groups_[port].insert(client); }
    void Leave(Client client, std::uint32_t port)
    {
        const auto found = groups_.find(port);
        if (found == groups_.end()) return;
        found->second.erase(client);
        if (found->second.empty()) groups_.erase(found);
    }
    void Detach(Client client)
    {
        const auto found = address_of_.find(client);
        if (found == address_of_.end()) return;
        by_address_.erase(found->second);
        address_of_.erase(found);
        for (auto group = groups_.begin(); group != groups_.end();) {
            group->second.erase(client);
            group = group->second.empty() ? groups_.erase(group) : std::next(group);
        }
    }
    bool Attached(Client client) const { return address_of_.count(client) != 0; }
    std::size_t Clients() const { return address_of_.size(); }

    // Where a frame goes. A frame from the link never returns to the link, so two hubs cannot loop it, and
    // a group frame does not return to its sender.
    std::vector<Client> Route(Client origin, const FrameHeader& header) const
    {
        std::vector<Client> targets;
        if (IsGroup(header.dst)) {
            const auto group = groups_.find(header.port);
            if (group != groups_.end())
                for (const auto client : group->second) if (client != origin) targets.push_back(client);
            if (origin != kLink) targets.push_back(kLink);
            return targets;
        }
        const auto client = by_address_.find(header.dst);
        if (client != by_address_.end()) { if (client->second != origin) targets.push_back(client->second); }
        else if (origin != kLink) targets.push_back(kLink);
        return targets;
    }

private:
    std::map<Address, Client> by_address_;
    std::map<Client, Address> address_of_;
    std::map<std::uint32_t, std::set<Client>> groups_;
};

// Frames waiting for a slow reader. The newest frame is dropped at the limit: older frames are already
// ordered and may be fragments of a sample RTPS can still complete.
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t limit_bytes) : limit_(limit_bytes) {}
    bool Push(std::vector<std::uint8_t> frame)
    {
        if (bytes_ + frame.size() > limit_) { ++dropped_; return false; }
        bytes_ += frame.size();
        frames_.push_back(std::move(frame));
        return true;
    }
    bool Empty() const { return frames_.empty(); }
    const std::vector<std::uint8_t>& Front() const { return frames_.front(); }
    void Pop() { bytes_ -= frames_.front().size(); frames_.pop_front(); }
    std::size_t Bytes() const { return bytes_; }
    std::uint64_t Dropped() const { return dropped_; }
private:
    std::size_t limit_, bytes_ = 0;
    std::uint64_t dropped_ = 0;
    std::deque<std::vector<std::uint8_t>> frames_;
};
}
