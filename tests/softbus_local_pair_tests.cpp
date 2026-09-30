#include "softbus_local_pair.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace fastdds_dsoftbus_probe;
namespace {
const std::string network(64, 'a');
void Check(bool yes, const char* why) { if (!yes) throw std::runtime_error(why); }
RecordPeer Peer() { return {"dial", network, "pkg", 2, true}; }
void Ready(LocalPairProbeContract& pair) {
    Check(pair.Created(LocalSide::Listener, 11), "listener owned");
    Check(pair.Created(LocalSide::Client, 12), "client owned");
    Check(pair.Accepted(13, Peer()), "accepted captured by listener table");
    Check(pair.Channel(LocalSide::Client).BindReturned(12, 0), "client bind returned");
}
}
int main() {
    const std::pair<const char*, std::function<void()>> cases[]{
        {"LOCAL-01 distinct native IDs and tables", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2); Ready(pair);
            Check(pair.Channel(LocalSide::Listener).Snapshot().connection == 13, "accepted route");
            Check(pair.Channel(LocalSide::Client).Snapshot().connection == 12, "outbound route");
        }},
        {"LOCAL-02 copied callback bytes survive overwrite", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2); Ready(pair);
            auto bytes = MakeProbeRecord(0);
            pair.Channel(LocalSide::Listener).OnBytes(13, bytes.data(), bytes.size());
            std::fill(bytes.begin(), bytes.end(), 0);
            RecordEvent event;
            auto& server = pair.Channel(LocalSide::Listener);
            Check(server.WaitPop(event, SoftbusRecordContract::Clock::now()), "bind event");
            Check(server.WaitPop(event, SoftbusRecordContract::Clock::now()), "owned data event");
            Check(ValidateProbeRecord(0, event.bytes), "all bytes preserved");
            pair.Channel(LocalSide::Client).OnBytes(12, event.bytes.data(), event.bytes.size());
            std::fill(event.bytes.begin(), event.bytes.end(), 0);
            Check(pair.Channel(LocalSide::Client).WaitPop(event, SoftbusRecordContract::Clock::now()) &&
                    ValidateProbeRecord(0, event.bytes), "reply through different native callback table");
        }},
        {"LOCAL-03 aliased native handles rejected without double shutdown", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2);
            Check(pair.Created(LocalSide::Listener, 11), "listener owned");
            Check(!pair.Created(LocalSide::Client, 11) && pair.Failed(), "alias is explicit failure");
            const auto ids = pair.ClaimShutdownTargets();
            Check(ids.size() == 1 && ids.values[0] == 11, "one close responsibility");
        }},
        {"LOCAL-04 every observed owned socket closed once", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2); Ready(pair);
            const auto ids = pair.ClaimShutdownTargets();
            Check(ids.size() == 3, "listener accepted outbound are all independent");
            for (int id : {11,12,13}) Check(std::count(ids.begin(), ids.end(), id) == 1, "unique ID close");
            Check(pair.ClaimShutdownTargets().empty(), "no duplicate close");
        }},
        {"LOCAL-05 wrong peer retained for ordinary cleanup", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2);
            Check(pair.Created(LocalSide::Listener, 11) && pair.Created(LocalSide::Client, 12), "created");
            auto peer = Peer(); peer.name = "wrong";
            Check(!pair.Accepted(13, peer) && pair.Failed(), "peer mismatch fails");
            Check(pair.ClaimShutdownTargets().size() == 3, "metadata failure does not lose ownership");
        }},
        {"LOCAL-06 a foreign-table callback is not silently rerouted", [] {
            LocalPairProbeContract pair("listen", "dial", network, 2); Ready(pair);
            auto bytes = MakeProbeRecord(0);
            pair.Channel(LocalSide::Client).OnBytes(13, bytes.data(), bytes.size());
            Check(pair.Failed(), "no guessing table from numeric handle");
        }}
    };
    int failed = 0;
    for (const auto& test : cases) try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception& e) { ++failed; std::cout << "FAIL " << test.first << ": " << e.what() << '\n'; }
    return failed ? 1 : 0;
}
