// Host tests for the hub's routing rules and bounded queue. No sockets, Fast DDS or SDK.
#include "HubRouter.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace fastdds_dsoftbus::ros2_hub;

namespace {
int failures = 0;
void check(bool value, const char* what)
{
    if (value) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    ++failures;
}
Address make(std::uint8_t value) { Address a{}; a[0] = value; return a; }
FrameHeader to(const Address& dst, std::uint32_t port = 7400)
{
    FrameHeader header; header.dst = dst; header.port = port; return header;
}
bool has(const std::vector<Router::Client>& list, Router::Client client) { return std::find(list.begin(), list.end(), client) != list.end(); }

void group_reaches_local_members_and_link()
{
    Router r;
    r.Attach(1, make(1)); r.Attach(2, make(2)); r.Attach(3, make(3));
    r.Join(1, 7400); r.Join(2, 7400); r.Join(3, 7650);
    const auto targets = r.Route(1, to(Address{}));
    check(targets.size() == 2 && has(targets, 2) && has(targets, Router::kLink), "group: other member and link only");
    check(!has(targets, 1), "group: not back to the sender");
    check(!has(targets, 3), "group: another domain's port is separate");
}

void frames_from_the_link_stay_local()
{
    Router r;
    r.Attach(1, make(1)); r.Join(1, 7400);
    check(!has(r.Route(Router::kLink, to(Address{})), Router::kLink), "group from link is not returned to it");
    const auto unknown = r.Route(Router::kLink, to(make(9)));
    check(unknown.empty(), "unknown unicast from link is dropped, so two hubs cannot loop it");
}

void unicast_is_exact()
{
    Router r;
    r.Attach(1, make(1)); r.Attach(2, make(2));
    const auto local = r.Route(1, to(make(2)));
    check(local.size() == 1 && local[0] == 2, "local unicast goes to that process only");
    const auto remote = r.Route(1, to(make(9)));
    check(remote.size() == 1 && remote[0] == Router::kLink, "unknown address belongs to the peer board");
    check(r.Route(1, to(make(1))).empty(), "unicast to self is not looped");
}

void detach_removes_every_trace()
{
    Router r;
    r.Attach(1, make(1)); r.Join(1, 7400);
    r.Detach(1);
    check(r.Clients() == 0, "detached");
    check(r.Route(2, to(Address{})).size() == 1, "group no longer lists it");
    check(r.Route(Router::kLink, to(make(1))).empty(), "address no longer resolves");
    r.Detach(1); // a repeated close must be harmless
    r.Join(1, 7400);
    check(r.Route(Router::kLink, to(Address{})).empty(), "join before attach is ignored");
}

void reconnect_replaces_the_old_connection()
{
    Router r;
    r.Attach(1, make(5)); r.Join(1, 7400);
    r.Attach(2, make(5)); r.Join(2, 7400);
    const auto targets = r.Route(Router::kLink, to(make(5)));
    check(targets.size() == 1 && targets[0] == 2, "same address now reaches the new connection");
    check(r.Route(Router::kLink, to(Address{})).size() == 1, "old connection left its groups");
    r.Detach(1); // the old connection's close arrives late
    check(r.Route(Router::kLink, to(make(5))).size() == 1, "a late close does not remove the new connection");
}

void second_hello_replaces_the_address()
{
    Router r;
    r.Attach(1, make(1));
    r.Attach(1, make(2));
    check(r.Clients() == 1, "one connection, one address");
    check(r.Route(Router::kLink, to(make(1))).empty(), "the old address no longer resolves");
    check(r.Route(Router::kLink, to(make(2))).size() == 1, "the new address does");
}

void leave_is_per_group()
{
    Router r;
    r.Attach(1, make(1)); r.Join(1, 7400); r.Join(1, 7650);
    r.Leave(1, 7400);
    check(r.Route(Router::kLink, to(Address{}, 7400)).empty(), "left 7400");
    check(r.Route(Router::kLink, to(Address{}, 7650)).size() == 1, "still in 7650");
    r.Leave(1, 9999); // unknown group
}

void parse_rejects_bad_input()
{
    FrameHeader header;
    std::vector<std::uint8_t> bytes(kHeaderBytes, 0);
    check(!ParseHeader(bytes.data(), bytes.size(), header), "unknown magic");
    check(!ParseHeader(bytes.data(), kHeaderBytes - 1, header), "short frame");
    FrameHeader good;
    std::memcpy(bytes.data(), &good, kHeaderBytes);
    check(ParseHeader(bytes.data(), bytes.size(), header), "valid frame");
    check(!ParseHeader(bytes.data(), kMaxRecordBytes + 1, header), "oversized frame");
    check(kMaxPayloadBytes + kHeaderBytes == kMaxRecordBytes, "a full frame fits one DSoftBus record");
}

void queue_drops_newest_and_recovers()
{
    BoundedQueue q(10);
    check(q.Push(std::vector<std::uint8_t>(6)), "first fits");
    check(!q.Push(std::vector<std::uint8_t>(6)), "over the limit is dropped");
    check(q.Dropped() == 1 && q.Bytes() == 6, "drop is counted and costs nothing");
    check(q.Push(std::vector<std::uint8_t>(4)), "a smaller frame still fits");
    q.Pop(); q.Pop();
    check(q.Empty() && q.Bytes() == 0, "bytes return to zero");
    check(q.Push(std::vector<std::uint8_t>(10)), "the limit itself fits");
}
}

int main()
{
    group_reaches_local_members_and_link();
    frames_from_the_link_stay_local();
    unicast_is_exact();
    detach_removes_every_trace();
    reconnect_replaces_the_old_connection();
    second_hello_replaces_the_address();
    leave_is_per_group();
    parse_rejects_bad_input();
    queue_drops_newest_and_recovers();
    if (failures == 0) std::printf("hub router tests passed\n");
    return failures == 0 ? 0 : 1;
}
