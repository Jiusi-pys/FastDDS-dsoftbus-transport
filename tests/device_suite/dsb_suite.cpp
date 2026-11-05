// Cross-board Fast DDS over DSoftBus device suite (test code only).
// One source for both supported Fast DDS lines (2.14.5 and 3.6.2); see compat.hpp.
//
//   dsb_suite node   token=<id>
//       prints "DSB_NODE network=<networkId> udid=<udid>" for this board.
//   dsb_suite run <0|1> <workdir> key=value...
//       token=<id> run=<16 hex> peer_network=<id> peer_udid=<udid> local_udid=<udid>
//       [domain=151] [groups=core,qos,feature,security,statistics] [route=0|1|2] [stress=wsf0|wsf1]
//   dsb_suite restart-helper <request file>      (internal: peer process restart case)
//
// Both sides run the same case sequence and synchronize through a TRANSIENT_LOCAL barrier topic
// on the DSoftBus Participant itself. The host script only (1) passes peer node identities,
// (2) releases the first dial once both boards print DSB_LISTENING, and (3) for the route
// outage case drops IP traffic for 15 s. Output: DSB_CASE lines and one DSB_SUMMARY line.
#include "compat.hpp"
#include "sample_types.hpp"
#include "../stage1_native_successor/security_pki.hpp"
#include "../../integrations/ros2/autoload/DeviceDeployment.hpp"
#include <softbus_bus_center.h>
#if defined(FASTDDS_STATISTICS)
#include <fastdds/statistics/dds/domain/DomainParticipant.hpp>
#include <fastdds/statistics/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/statistics/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/statistics/topic_names.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <statistics/types/typesPubSubTypes.h>
#include <statistics/types/monitorservice_typesPubSubTypes.h>
#else
#include <statistics/types/typesPubSubTypes.hpp>
#include <statistics/types/monitorservice_typesPubSubTypes.hpp>
#endif
#endif
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace ds = fastdds_dsoftbus::dsoftbus;
namespace dd = fastdds_dsoftbus::device_deployment;
using namespace dsb;
using Clock = std::chrono::steady_clock;
static constexpr char kPackage[] = "com.kaihong.mdds";

[[noreturn]] static void fail(const std::string& what) { throw std::runtime_error(what); }
static void need(bool v, const char* what) { if (!v) fail(what); }
static std::map<std::string, std::string> kv(int argc, char** argv, int from)
{
    std::map<std::string, std::string> m;
    for (int i = from; i < argc; ++i) { std::string a = argv[i]; auto e = a.find('='); if (e != std::string::npos) m[a.substr(0, e)] = a.substr(e + 1); }
    return m;
}
static void set_token(const std::string& token) { dd::SetSelfToken(token); }

// ---------------------------------------------------------------- deployment / transport
using Deploy = dd::Deploy;
using Pumped = dd::PumpedRuntime;
static Deploy make_deploy(unsigned side, const std::string& run, std::uint32_t domain, const std::string& udid,
        const std::string& peer_udid, const std::string& peer_network)
{ return dd::MakeDeploy(kPackage, "dsb.suite", side, run, domain, udid, peer_udid, peer_network); }
static ds::DSoftBusTransportDescriptor configuration(const Deploy& q, bool feature_limits) { return dd::Configuration(q, feature_limits); }
static ds::phase0::PublicSocketConfiguration backend(const ds::DSoftBusTransportDescriptor& c) { return dd::Backend(kPackage, c); }
static void participant_locators(fd::DomainParticipantQos& qos, const ds::DSoftBusTransportDescriptor& c,
        std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> desc, bool initial_peer = true)
{
    qos.transport().use_builtin_transports = false; qos.transport().user_transports = {desc};
    qos.transport().max_msg_size_no_frag = c.maxMessageSize;
    auto& b = qos.wire_protocol().builtin;
    b.metatrafficUnicastLocatorList.clear(); b.metatrafficUnicastLocatorList.push_back(c.default_user_unicast.at(0));
    b.metatrafficMulticastLocatorList.clear(); b.initialPeersList.clear();
    if (initial_peer) b.initialPeersList.push_back(c.directory.routes.at(0).key.remote_locator);
    qos.wire_protocol().default_unicast_locator_list.clear(); qos.wire_protocol().default_unicast_locator_list.push_back(c.default_user_unicast.at(0));
    qos.wire_protocol().default_multicast_locator_list.clear();
}
static bool wait_file(const std::string& path, int seconds)
{
    const auto end = Clock::now() + std::chrono::seconds(seconds);
    while (access(path.c_str(), F_OK) != 0) { if (Clock::now() >= end) return false; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    return true;
}

// ---------------------------------------------------------------- suite context
struct Suite {
    unsigned me = 0, peer = 1; std::uint64_t run = 0; std::string run_hex, workdir; Deploy q;
    ds::DSoftBusTransportDescriptor config{65536};
    Pumped main; std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> descs;
    fd::DomainParticipantFactory* factory = nullptr; fd::DomainParticipant* participant = nullptr;
    fd::DomainParticipantQos qos; fd::Publisher* pub = nullptr; fd::Subscriber* sub = nullptr;
    fd::TypeSupport layer_type{new LayerType};
    fd::DataWriterQos wq; fd::DataReaderQos rq;
    fd::DataWriter* sync_w = nullptr; fd::DataReader* sync_r = nullptr; std::uint32_t stage = 0, peer_stage = 0;
    std::vector<fd::DomainParticipant*> extra;
    unsigned passed = 0, failed = 0;
    std::string tname(const std::string& what, unsigned side) const { return "dsb_" + run_hex + "_" + std::to_string(side) + "_" + what; }
    void check_pump() const { if (main.failed.load()) fail("transport pump failed"); }
    template<class P> bool waitp(P pred, int ms)
    {
        const auto end = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) { check_pump(); if (pred()) return true; if (Clock::now() >= end) return false; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }
    void report(const char* group, const std::string& name, bool pass, const std::string& detail)
    {
        (pass ? passed : failed)++;
        std::printf("DSB_CASE side=%u fastdds=%s group=%s case=%s pass=%d %s\n", me, DSB_FASTDDS_LINE, group, name.c_str(), int(pass), detail.c_str());
        std::fflush(stdout);
    }
    void note(const char* group, const std::string& name, const char* status, const std::string& reason)
    { std::printf("DSB_CASE side=%u fastdds=%s group=%s case=%s pass=%s reason=%s\n", me, DSB_FASTDDS_LINE, group, name.c_str(), status, reason.c_str()); std::fflush(stdout); }
    void topics(fd::DomainParticipant* p, const std::string& what, const std::string& type, fd::Topic*& tx, fd::Topic*& rx, const fd::TopicQos& tq = fd::TOPIC_QOS_DEFAULT)
    { tx = p->create_topic(tname(what, me), type, tq); rx = p->create_topic(tname(what, peer), type, tq); need(tx && rx, "create topics"); }
    bool matched(fd::DataWriter* w, fd::DataReader* r, int wn, int rn, int ms)
    {
        return waitp([&] { fd::PublicationMatchedStatus a; fd::SubscriptionMatchedStatus b;
            need(ok(w->get_publication_matched_status(a)) && ok(r->get_subscription_matched_status(b)), "matched status");
            return a.current_count == wn && b.current_count == rn; }, ms);
    }
    Layer lay(std::uint64_t r, std::uint32_t n) const { return layer(r, me, n); }
    void barrier()
    {
        const auto n = ++stage; auto v = lay(run + 500, n); need(write(sync_w, &v), "barrier write");
        const bool reached = waitp([&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(sync_r->take_next_sample(&x, &i))) break;
            if (i.valid_data && x.run == run + 500 && x.side == peer && x.ordinal > peer_stage) peer_stage = x.ordinal; } return peer_stage >= n; }, 90000);
        if (!reached) fail("barrier " + std::to_string(n) + " timed out (peer at " + std::to_string(peer_stage) + ")");
    }
    template<class Q> static Q keep_all(Q q) { q.history().kind = fd::KEEP_ALL_HISTORY_QOS; return q; }
    std::uint32_t take_layers(fd::DataReader* r, std::uint64_t want_run, std::uint32_t& next)
    {
        std::uint32_t got = 0;
        for (;;) { Layer x; fd::SampleInfo i; if (!ok(r->take_next_sample(&x, &i))) break;
            if (i.valid_data && layer_valid(x, want_run, peer) && x.ordinal == next) { ++next; ++got; } }
        return got;
    }
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> descriptor(const ds::DSoftBusTransportDescriptor& c)
    { auto d = main.rt->MakeStockDescriptor(c); need(bool(d), "MakeStockDescriptor"); descs.push_back(d); return d; }
    fd::DomainParticipant* extra_participant(fd::DomainParticipantQos q, fd::DomainParticipantListener* l = nullptr, std::uint32_t domain = 0)
    {
        auto* p = factory->create_participant(domain ? domain : q_domain(), q, l, fd::StatusMask::none()); extra.push_back(p);
        need(p != nullptr, "create extra participant"); return p;
    }
    std::uint32_t q_domain() const { return q.domain; }
    void drop(fd::DomainParticipant* p)
    {
        need(ok(p->delete_contained_entities()) && ok(factory->delete_participant(p)), "delete participant");
        for (auto& e : extra) if (e == p) e = nullptr;
    }
    fd::DomainParticipantQos named(const std::string& suffix)
    { fd::DomainParticipantQos q2 = qos; q2.name((std::string(qos.name().c_str()) + suffix).c_str()); return q2; }
    void with_descriptor(fd::DomainParticipantQos& q2) { q2.transport().user_transports = {descriptor(config)}; }
};

// ---------------------------------------------------------------- core group
static void core_cases(Suite& s, std::atomic<bool>& route_marker, unsigned route)
{
    const char* G = "core";
    {   // Three ordered RELIABLE samples each way plus acknowledgement.
        fd::Topic *tx, *rx; s.topics(s.participant, "core", s.layer_type.get_type_name(), tx, rx);
        auto* w = s.pub->create_datawriter(tx, s.wq); auto* r = s.sub->create_datareader(rx, s.rq); need(w && r, "core endpoints");
        const bool m = s.matched(w, r, 1, 1, 60000); s.barrier();
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 1, n); need(write(w, &v), "core write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(r, s.run + 1, next); return next == 4; }, 20000);
        const bool acked = ok(w->wait_for_acknowledgments(seconds(10)));
        s.report(G, "reliable_ordered_exchange", m && got && acked, "matched=" + std::to_string(m) + " received=" + std::to_string(next - 1) + " acked=" + std::to_string(acked));
    }
    s.barrier();
    {   // 150000-byte sample per direction (DATA_FRAG over the transport).
        fd::TypeSupport bt{new BlobType("FastDDSDSoftBusFrag", kFragBytes)}; need(ok(bt.register_type(s.participant)), "frag type");
        fd::Topic *tx, *rx; s.topics(s.participant, "frag", bt.get_type_name(), tx, rx);
        auto* w = s.pub->create_datawriter(tx, s.wq); auto* r = s.sub->create_datareader(rx, s.rq); need(w && r, "frag endpoints");
        const bool m = s.matched(w, r, 1, 1, 20000); s.barrier();
        auto mine = blob(s.run + 2, s.me, kFragBytes); need(write(w, &mine), "frag write");
        bool got = false; s.waitp([&] { for (;;) { Blob v; fd::SampleInfo i; if (!ok(r->take_next_sample(&v, &i))) break;
            if (i.valid_data && blob_valid(v, s.run + 2, s.peer, kFragBytes)) got = true; } return got; }, 20000);
        const bool acked = ok(w->wait_for_acknowledgments(seconds(10)));
        s.report(G, "fragmented_150000B", m && got && acked, "max_message=" + std::to_string(s.config.maxMessageSize) + " verified=" + std::to_string(got) + " acked=" + std::to_string(acked));
    }
    s.barrier();
    {   // Same-name Participant hot rebuild on the live Runtime; TRANSIENT_LOCAL sample after rebuild.
        const auto old_prefix = s.participant->guid().guidPrefix;
        need(ok(s.participant->delete_contained_entities()) && ok(s.factory->delete_participant(s.participant)), "delete main participant");
        s.participant = nullptr;
        s.qos.transport().user_transports = {s.descriptor(s.config)};
        s.participant = s.factory->create_participant(s.q.domain, s.qos); need(s.participant != nullptr, "recreate participant");
        need(ok(s.layer_type.register_type(s.participant)), "register layer");
        s.pub = s.participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT); s.sub = s.participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        need(s.pub && s.sub, "pub/sub");
        // Barrier endpoints are recreated with the Participant.
        fd::Topic *stx, *srx; s.topics(s.participant, "sync", s.layer_type.get_type_name(), stx, srx);
        fd::DataWriterQos swq = Suite::keep_all(s.wq); swq.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        fd::DataReaderQos srq = Suite::keep_all(s.rq); srq.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        s.sync_w = s.pub->create_datawriter(stx, swq); s.sync_r = s.sub->create_datareader(srx, srq); need(s.sync_w && s.sync_r, "sync endpoints");
        fd::TypeSupport bt{new BlobType("FastDDSDSoftBusFrag", kFragBytes)}; need(ok(bt.register_type(s.participant)), "frag type");
        fd::Topic *tx, *rx; s.topics(s.participant, "rebuild", bt.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.history().depth = 1; w.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        fd::DataReaderQos r = s.rq; r.history().depth = 1; r.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        auto* bw = s.pub->create_datawriter(tx, w); auto* br = s.sub->create_datareader(rx, r); need(bw && br, "rebuild endpoints");
        auto mine = blob(s.run + 3, s.me, kFragBytes); need(write(bw, &mine), "rebuild write");
        bool got = false; s.waitp([&] { for (;;) { Blob v; fd::SampleInfo i; if (!ok(br->take_next_sample(&v, &i))) break;
            if (i.valid_data && blob_valid(v, s.run + 3, s.peer, kFragBytes)) got = true; } return got; }, 60000);
        const bool acked = ok(bw->wait_for_acknowledgments(seconds(10)));
        s.report(G, "same_name_participant_hot_rebuild", got && acked && !(s.participant->guid().guidPrefix == old_prefix),
            "new_guid_prefix=1 transient_local_150000B=" + std::to_string(got) + " acked=" + std::to_string(acked));
    }
    s.barrier();
    if (route) {   // Route outage: 200 ms samples for 45 s (60 s for two outages); host drops IP for 15 s each.
        fd::Topic *tx, *rx; s.topics(s.participant, "route", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.history().depth = 16; fd::DataReaderQos r = s.rq; r.history().depth = 16;
        auto* ow = s.pub->create_datawriter(tx, w); auto* orr = s.sub->create_datareader(rx, r); need(ow && orr, "route endpoints");
        const bool m = s.matched(ow, orr, 1, 1, 20000); s.barrier();
        s.main.tolerate = true;
        const unsigned outages = route == 2 ? 2 : 1; const int64_t window_ms = outages == 2 ? 60000 : 45000;
        std::printf("DSB_ROUTE_WINDOW_BEGIN side=%u outages=%u\n", s.me, outages); std::fflush(stdout); route_marker = true;
        const auto t0 = Clock::now(); auto ms = [&] { return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count()); };
        std::uint32_t written = 0, received = 0, long_gaps = 0; int64_t max_gap = 0, last_rx = -1, last_gap_end = -1; auto next = t0;
        while (ms() < window_ms) {
            s.check_pump();
            if (Clock::now() >= next) { auto v = s.lay(s.run + 4, written + 1); if (write(ow, &v)) ++written; next += std::chrono::milliseconds(200); }
            for (;;) { Layer x; fd::SampleInfo i; if (!ok(orr->take_next_sample(&x, &i))) break; if (!i.valid_data || !layer_valid(x, s.run + 4, s.peer)) continue;
                const auto now = ms(); if (last_rx >= 0 && now - last_rx >= 8000) { ++long_gaps; last_gap_end = now; }
                if (last_rx >= 0 && now - last_rx > max_gap) max_gap = now - last_rx; last_rx = now; ++received; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const bool recovered = m && long_gaps >= outages && last_rx > last_gap_end && last_rx >= window_ms - 3000;
        s.report(G, outages == 2 ? "route_outage_twice_recovers" : "route_outage_recovers", recovered,
            "written=" + std::to_string(written) + " received=" + std::to_string(received) + " long_gaps=" + std::to_string(long_gaps) +
            " max_gap_ms=" + std::to_string(max_gap) + " last_rx_ms=" + std::to_string(last_rx) + " send_failures_during_outage=" + std::to_string(s.main.send_failures.load()));
        s.main.tolerate = false;
        s.barrier();
    }
}

// ---------------------------------------------------------------- QoS group
static void qos_cases(Suite& s)
{
    const char* G = "qos";
    fd::TypeSupport kt{new KeyedType}; need(ok(kt.register_type(s.participant)), "keyed type");
    {   // Keyed instances, dispose and unregister (no autodispose).
        fd::Topic *tx, *rx; s.topics(s.participant, "keyed", kt.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.history().depth = 4; w.writer_data_lifecycle().autodispose_unregistered_instances = false;
        fd::DataReaderQos r = s.rq; r.history().depth = 4;
        auto* kw = s.pub->create_datawriter(tx, w); auto* kr = s.sub->create_datareader(rx, r); need(kw && kr, "keyed endpoints");
        const bool m = s.matched(kw, kr, 1, 1, 10000); s.barrier();
        std::array<rtps::InstanceHandle_t, 3> hs{};
        for (std::uint32_t k = 1; k <= 3; ++k) { Keyed v; v.key = k; v.run = s.run; v.side = s.me; hs[k - 1] = kw->register_instance(&v);
            for (std::uint32_t n = 1; n <= 2; ++n) { v.seq = n; v.tag = k * 10 + n; need(ok(kw->write(&v, hs[k - 1])), "keyed write"); } }
        { Keyed v; v.run = s.run; v.side = s.me; v.key = 1; need(ok(kw->dispose(&v, hs[0])), "dispose"); v.key = 2; need(ok(kw->unregister_instance(&v, hs[1])), "unregister"); }
        std::array<unsigned, 3> got{}; bool disposed = false, nowriters = false; std::vector<rtps::InstanceHandle_t> handles;
        const bool done = m && s.waitp([&] { for (;;) { Keyed x; fd::SampleInfo i; if (!ok(kr->take_next_sample(&x, &i))) break;
            if (i.valid_data) { if (x.run == s.run && x.side == s.peer && x.key >= 1 && x.key <= 3 && x.tag == x.key * 10 + x.seq) { ++got[x.key - 1];
                if (std::find(handles.begin(), handles.end(), i.instance_handle) == handles.end()) handles.push_back(i.instance_handle); } }
            else { if (i.instance_state == fd::NOT_ALIVE_DISPOSED_INSTANCE_STATE) disposed = true; if (i.instance_state == fd::NOT_ALIVE_NO_WRITERS_INSTANCE_STATE) nowriters = true; } }
            return got[0] == 2 && got[1] == 2 && got[2] == 2 && disposed && nowriters; }, 8000);
        s.report(G, "keyed_instances_dispose_unregister", done && handles.size() == 3, "samples=" + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," +
            std::to_string(got[2]) + " instances=" + std::to_string(handles.size()) + " disposed=" + std::to_string(disposed) + " no_writers=" + std::to_string(nowriters));
        (void)kw->wait_for_acknowledgments(seconds(2));
    }
    s.barrier();
    {   // TRANSIENT_LOCAL late joiner.
        fd::Topic *tx, *rx; s.topics(s.participant, "late", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS; w.history().depth = 5;
        auto* lw = s.pub->create_datawriter(tx, w); need(lw, "late writer");
        for (std::uint32_t n = 1; n <= 5; ++n) { auto v = s.lay(s.run + 8, n); need(write(lw, &v), "late write"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        fd::DataReaderQos r = s.rq; r.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS; r.history().depth = 5;
        auto* lr = s.sub->create_datareader(rx, r); need(lr, "late reader");
        std::uint32_t next = 1; const bool got = s.waitp([&] { s.take_layers(lr, s.run + 8, next); return next == 6; }, 10000);
        s.report(G, "transient_local_late_joiner", got, "received_in_order=" + std::to_string(next - 1));
        (void)lw->wait_for_acknowledgments(seconds(3));
    }
    s.barrier();
    {   // KEEP_ALL reliable burst of 300 samples.
        fd::Topic *tx, *rx; s.topics(s.participant, "burst", s.layer_type.get_type_name(), tx, rx);
        auto* bw = s.pub->create_datawriter(tx, Suite::keep_all(s.wq)); auto* br = s.sub->create_datareader(rx, Suite::keep_all(s.rq)); need(bw && br, "burst endpoints");
        const bool m = s.matched(bw, br, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 300; ++n) { auto v = s.lay(s.run + 9, n); need(write(bw, &v), "burst write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(br, s.run + 9, next); return next == 301; }, 20000);
        const bool acked = ok(bw->wait_for_acknowledgments(seconds(10)));
        s.report(G, "keep_all_reliable_burst_300", got && acked, "received_in_order=" + std::to_string(next - 1) + " acked=" + std::to_string(acked));
    }
    s.barrier();
    {   // DEADLINE 300 ms.
        fd::Topic *tx, *rx; s.topics(s.participant, "deadline", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.deadline().period = millis(300); fd::DataReaderQos r = s.rq; r.deadline().period = millis(300);
        auto* dw = s.pub->create_datawriter(tx, w); auto* dr = s.sub->create_datareader(rx, r); need(dw && dr, "deadline endpoints");
        const bool m = s.matched(dw, dr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 5; ++n) { auto v = s.lay(s.run + 10, n); need(write(dw, &v), "deadline write"); std::this_thread::sleep_for(std::chrono::milliseconds(100)); }
        fd::RequestedDeadlineMissedStatus during{}; fd::OfferedDeadlineMissedStatus odur{};
        need(ok(dr->get_requested_deadline_missed_status(during)) && ok(dw->get_offered_deadline_missed_status(odur)), "deadline status");
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        fd::RequestedDeadlineMissedStatus after{}; fd::OfferedDeadlineMissedStatus oafter{};
        need(ok(dr->get_requested_deadline_missed_status(after)) && ok(dw->get_offered_deadline_missed_status(oafter)), "deadline status");
        std::uint32_t next = 1; s.take_layers(dr, s.run + 10, next);
        s.report(G, "deadline_300ms", m && next == 6 && during.total_count == 0 && odur.total_count == 0 && after.total_count >= 1 && oafter.total_count >= 1,
            "received=" + std::to_string(next - 1) + " requested_missed=" + std::to_string(during.total_count) + "->" + std::to_string(after.total_count) +
            " offered_missed=" + std::to_string(odur.total_count) + "->" + std::to_string(oafter.total_count));
    }
    s.barrier();
    {   // AUTOMATIC liveliness 1 s.
        fd::Topic *tx, *rx; s.topics(s.participant, "liveliness", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.liveliness().kind = fd::AUTOMATIC_LIVELINESS_QOS; w.liveliness().lease_duration = seconds(1); w.liveliness().announcement_period = millis(500);
        fd::DataReaderQos r = s.rq; r.liveliness().kind = fd::AUTOMATIC_LIVELINESS_QOS; r.liveliness().lease_duration = seconds(1);
        auto* lw = s.pub->create_datawriter(tx, w); auto* lr = s.sub->create_datareader(rx, r); need(lw && lr, "liveliness endpoints");
        auto alive = [&] { fd::LivelinessChangedStatus st; need(ok(lr->get_liveliness_changed_status(st)), "liveliness status"); return st.alive_count; };
        const bool up = s.matched(lw, lr, 1, 1, 10000) && s.waitp([&] { return alive() == 1; }, 5000);
        s.barrier(); need(ok(s.pub->delete_datawriter(lw)), "delete liveliness writer");
        const bool down = s.waitp([&] { return alive() == 0; }, 6000);
        s.report(G, "liveliness_automatic_1s", up && down, "alive_seen=" + std::to_string(up) + " not_alive_after_delete=" + std::to_string(down));
    }
    s.barrier();
    {   // EXCLUSIVE ownership.
        fd::Topic *tx, *rx; s.topics(s.participant, "ownership", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos low = s.wq; low.ownership().kind = fd::EXCLUSIVE_OWNERSHIP_QOS; low.ownership_strength().value = 10;
        fd::DataWriterQos high = low; high.ownership_strength().value = 20;
        fd::DataReaderQos r = s.rq; r.ownership().kind = fd::EXCLUSIVE_OWNERSHIP_QOS; r.history().depth = 32;
        auto* w1 = s.pub->create_datawriter(tx, low); auto* w2 = s.pub->create_datawriter(tx, high); auto* orr = s.sub->create_datareader(rx, r); need(w1 && w2 && orr, "ownership endpoints");
        const bool m = s.matched(w1, orr, 1, 2, 10000) && s.matched(w2, orr, 1, 2, 10000); s.barrier();
        { auto b = s.lay(s.run + 11, 3000); need(write(w2, &b), "owner write"); }
        const bool owned = s.waitp([&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(orr->take_next_sample(&x, &i))) break;
            if (i.valid_data && x.run == s.run + 11 && x.side == s.peer && x.ordinal == 3000) return true; } return false; }, 5000);
        s.barrier();
        for (std::uint32_t n = 1; n <= 10; ++n) { auto a = s.lay(s.run + 11, 1000 + n); auto b = s.lay(s.run + 11, 2000 + n);
            need(write(w1, &a) && write(w2, &b), "ownership write"); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        unsigned hi = 0, lo = 0;
        auto drain = [&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(orr->take_next_sample(&x, &i))) break; if (!i.valid_data || x.run != s.run + 11 || x.side != s.peer) continue;
            if (x.ordinal >= 2000) ++hi; else ++lo; } };
        (void)s.waitp([&] { drain(); return hi >= 10; }, 6000); std::this_thread::sleep_for(std::chrono::milliseconds(300)); drain();
        s.report(G, "ownership_exclusive_strength", m && owned && hi == 10 && lo == 0, "owner_established=" + std::to_string(owned) + " strength20=" + std::to_string(hi) + " strength10=" + std::to_string(lo));
    }
    s.barrier();
    {   // LIFESPAN 2 s.
        fd::Topic *tx, *rx; s.topics(s.participant, "lifespan", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.lifespan().duration = seconds(2); w.history().depth = 10;
        fd::DataReaderQos r = s.rq; r.lifespan().duration = seconds(2); r.history().depth = 10;
        auto* fw = s.pub->create_datawriter(tx, w); auto* fr = s.sub->create_datareader(rx, r); need(fw && fr, "lifespan endpoints");
        const bool m = s.matched(fw, fr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 12, n); need(write(fw, &v), "lifespan write"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(3500));
        unsigned stale = 0, fresh = 0;
        auto drain = [&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(fr->take_next_sample(&x, &i))) break; if (!i.valid_data || x.run != s.run + 12 || x.side != s.peer) continue;
            if (x.ordinal <= 3) ++stale; else ++fresh; } };
        drain(); s.barrier();
        for (std::uint32_t n = 4; n <= 5; ++n) { auto v = s.lay(s.run + 12, n); need(write(fw, &v), "lifespan write"); }
        (void)s.waitp([&] { drain(); return fresh >= 2; }, 3000);
        s.report(G, "lifespan_2s", m && stale == 0 && fresh == 2, "expired_batch_delivered=" + std::to_string(stale) + " fresh_delivered=" + std::to_string(fresh));
    }
    s.barrier();
    {   // PARTITIONS.
        fd::Topic *tx, *rx; s.topics(s.participant, "partition", s.layer_type.get_type_name(), tx, rx);
        fd::PublisherQos pq = fd::PUBLISHER_QOS_DEFAULT; pq.partition().push_back("pa");
        fd::SubscriberQos wrong_q = fd::SUBSCRIBER_QOS_DEFAULT; wrong_q.partition().push_back("pb");
        fd::SubscriberQos right_q = fd::SUBSCRIBER_QOS_DEFAULT; right_q.partition().push_back("pa");
        auto* ppub = s.participant->create_publisher(pq); auto* wsub = s.participant->create_subscriber(wrong_q); auto* rsub = s.participant->create_subscriber(right_q);
        need(ppub && wsub && rsub, "partition pub/sub");
        auto* pw = ppub->create_datawriter(tx, s.wq); auto* wrong = wsub->create_datareader(rx, s.rq); auto* right = rsub->create_datareader(rx, s.rq); need(pw && wrong && right, "partition endpoints");
        const bool m = s.waitp([&] { fd::SubscriptionMatchedStatus b; fd::PublicationMatchedStatus a;
            need(ok(right->get_subscription_matched_status(b)) && ok(pw->get_publication_matched_status(a)), "partition status"); return b.current_count == 1 && a.current_count == 1; }, 10000);
        s.barrier(); auto v = s.lay(s.run + 13, 1); need(write(pw, &v), "partition write");
        unsigned got_right = 0, got_wrong = 0;
        (void)s.waitp([&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(right->take_next_sample(&x, &i))) break; if (i.valid_data && x.run == s.run + 13 && x.side == s.peer) ++got_right; } return got_right >= 1; }, 4000);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        for (;;) { Layer x; fd::SampleInfo i; if (!ok(wrong->take_next_sample(&x, &i))) break; if (i.valid_data) ++got_wrong; }
        fd::SubscriptionMatchedStatus ws; need(ok(wrong->get_subscription_matched_status(ws)), "partition status");
        s.report(G, "partitions", m && got_right == 1 && got_wrong == 0 && ws.total_count == 0, "same_partition_delivered=" + std::to_string(got_right) +
            " other_partition_matches=" + std::to_string(ws.total_count) + " other_partition_delivered=" + std::to_string(got_wrong));
    }
    s.barrier();
    {   // BEST_EFFORT + KEEP_LAST.
        fd::Topic *tx, *rx; s.topics(s.participant, "besteffort", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.reliability().kind = fd::BEST_EFFORT_RELIABILITY_QOS; w.history().depth = 64;
        fd::DataReaderQos r = s.rq; r.reliability().kind = fd::BEST_EFFORT_RELIABILITY_QOS; r.history().depth = 64;
        auto* bw = s.pub->create_datawriter(tx, w); auto* br = s.sub->create_datareader(rx, r); need(bw && br, "best effort endpoints");
        const bool m = s.matched(bw, br, 1, 1, 10000); s.barrier();
        std::uint32_t last = 0, received = 0; bool order = true;
        auto drain = [&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(br->take_next_sample(&x, &i))) break; if (!i.valid_data || x.run != s.run + 23 || x.side != s.peer) continue;
            if (x.ordinal <= last) order = false; last = x.ordinal; ++received; } };
        for (std::uint32_t n = 1; n <= 50; ++n) { auto v = s.lay(s.run + 23, n); need(write(bw, &v), "best effort write"); drain(); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
        (void)s.waitp([&] { drain(); return last == 50; }, 3000);
        s.report(G, "best_effort_keep_last", m && order && received >= 45, "received=" + std::to_string(received) + "/50 in_order_no_dup=" + std::to_string(order));
    }
    s.barrier();
    {   // Reader KEEP_LAST depth 1.
        fd::Topic *tx, *rx; s.topics(s.participant, "depth1", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.history().depth = 16; fd::DataReaderQos r = s.rq; r.history().depth = 1;
        auto* dw = s.pub->create_datawriter(tx, w); auto* dr = s.sub->create_datareader(rx, r); need(dw && dr, "depth1 endpoints");
        const bool m = s.matched(dw, dr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 10; ++n) { auto v = s.lay(s.run + 24, n); need(write(dw, &v), "depth1 write"); }
        const bool acked = ok(dw->wait_for_acknowledgments(seconds(5))); s.barrier();
        unsigned count = 0; std::uint32_t lastn = 0;
        for (;;) { Layer x; fd::SampleInfo i; if (!ok(dr->take_next_sample(&x, &i))) break; if (i.valid_data && x.run == s.run + 24 && x.side == s.peer) { ++count; lastn = x.ordinal; } }
        s.report(G, "keep_last_depth1_reader", m && acked && count == 1 && lastn == 10, "kept=" + std::to_string(count) + " newest=" + std::to_string(lastn));
    }
    s.barrier();
    {   // Reader resource limits with KEEP_ALL RELIABLE back-pressure.
        fd::Topic *tx, *rx; s.topics(s.participant, "limits", s.layer_type.get_type_name(), tx, rx);
        fd::DataReaderQos r = Suite::keep_all(s.rq); r.resource_limits().max_samples = 5; r.resource_limits().max_instances = 1;
        r.resource_limits().max_samples_per_instance = 5; r.resource_limits().allocated_samples = 5;
        fd::DataWriterQos w = Suite::keep_all(s.wq); heartbeat_period(w) = millis(100);
        auto* lw = s.pub->create_datawriter(tx, w); auto* lr = s.sub->create_datareader(rx, r); need(lw && lr, "limits endpoints");
        const bool m = s.matched(lw, lr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 20; ++n) { auto v = s.lay(s.run + 25, n); need(write(lw, &v), "limits write"); }
        std::this_thread::sleep_for(std::chrono::milliseconds(800)); const auto peak = lr->get_unread_count();
        std::uint32_t next = 1; const bool got = s.waitp([&] { s.take_layers(lr, s.run + 25, next); return next == 21; }, 15000);
        const bool acked = ok(lw->wait_for_acknowledgments(seconds(15)));
        s.report(G, "reader_resource_limits_backpressure", m && got && peak == 5 && acked, "buffered_before_take=" + std::to_string(peak) + " received_in_order=" + std::to_string(next - 1));
    }
    s.barrier();
    {   // RELIABLE with positive ACKs disabled.
        fd::Topic *tx, *rx; s.topics(s.participant, "noacks", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = Suite::keep_all(s.wq); w.reliable_writer_qos().disable_positive_acks.enabled = true; w.reliable_writer_qos().disable_positive_acks.duration = seconds(1);
        fd::DataReaderQos r = Suite::keep_all(s.rq); reader_disable_positive_acks(r).enabled = true;
        auto* nw = s.pub->create_datawriter(tx, w); auto* nr = s.sub->create_datareader(rx, r); need(nw && nr, "noacks endpoints");
        const bool m = s.matched(nw, nr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 20; ++n) { auto v = s.lay(s.run + 29, n); need(write(nw, &v), "noacks write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(nr, s.run + 29, next); return next == 21; }, 10000);
        s.report(G, "reliable_disable_positive_acks", got, "received_in_order=" + std::to_string(next - 1) + " unpaced=1");
        s.barrier();
    }
    s.barrier();
    {   // 1 MiB sample.
        fd::TypeSupport lt{new BlobType("FastDDSDSoftBusLarge", kLargeBytes)}; need(ok(lt.register_type(s.participant)), "large type");
        fd::Topic *tx, *rx; s.topics(s.participant, "large", lt.get_type_name(), tx, rx);
        fd::DataWriterQos w = s.wq; w.history().depth = 1; fd::DataReaderQos r = s.rq; r.history().depth = 1;
        auto* lw = s.pub->create_datawriter(tx, w); auto* lr = s.sub->create_datareader(rx, r); need(lw && lr, "large endpoints");
        const bool m = s.matched(lw, lr, 1, 1, 10000); s.barrier();
        const auto t0 = Clock::now(); { auto v = blob(s.run + 30, s.me, kLargeBytes); need(write(lw, &v), "large write"); }
        bool got = false; s.waitp([&] { for (;;) { Blob v; fd::SampleInfo i; if (!ok(lr->take_next_sample(&v, &i))) break;
            if (i.valid_data && blob_valid(v, s.run + 30, s.peer, kLargeBytes)) got = true; } return got; }, 30000);
        const bool acked = ok(lw->wait_for_acknowledgments(seconds(30)));
        s.report(G, "large_sample_1MiB", m && got && acked, "verified=" + std::to_string(got) + " acked=" + std::to_string(acked) + " ms=" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count()));
    }
    s.barrier();
    {   // Twenty topics at once.
        constexpr unsigned K = 20; std::array<fd::DataWriter*, K> mw{}; std::array<fd::DataReader*, K> mr{};
        for (unsigned k = 0; k < K; ++k) { fd::Topic *tx, *rx; s.topics(s.participant, "many" + std::to_string(k), s.layer_type.get_type_name(), tx, rx);
            mw[k] = s.pub->create_datawriter(tx, s.wq); mr[k] = s.sub->create_datareader(rx, s.rq); need(mw[k] && mr[k], "many endpoints"); }
        const bool m = s.waitp([&] { for (unsigned k = 0; k < K; ++k) { fd::PublicationMatchedStatus a; fd::SubscriptionMatchedStatus b;
            need(ok(mw[k]->get_publication_matched_status(a)) && ok(mr[k]->get_subscription_matched_status(b)), "many status");
            if (a.current_count != 1 || b.current_count != 1) return false; } return true; }, 30000);
        s.barrier();
        for (unsigned k = 0; k < K; ++k) { auto v = s.lay(s.run + 31, k + 1); need(write(mw[k], &v), "many write"); }
        std::array<bool, K> got{}; unsigned total = 0;
        (void)s.waitp([&] { for (unsigned k = 0; k < K; ++k) { for (;;) { Layer x; fd::SampleInfo i; if (!ok(mr[k]->take_next_sample(&x, &i))) break;
            if (i.valid_data && x.run == s.run + 31 && x.side == s.peer && x.ordinal == k + 1 && !got[k]) { got[k] = true; ++total; } } } return total == K; }, 10000);
        s.report(G, "twenty_topics_discovery_and_delivery", m && total == K, "matched_all=" + std::to_string(m) + " delivered=" + std::to_string(total) + "/20");
    }
    s.barrier();
    {   // TRANSPORT_PRIORITY 23: Fast DDS >= 3.4 hands the value to SenderResource::send; the transport
        // records it per send record (no priority scheduling). Delivery must be unaffected.
        fd::Topic *tx, *rx; s.topics(s.participant, "priority", s.layer_type.get_type_name(), tx, rx);
        fd::DataWriterQos w = Suite::keep_all(s.wq); w.transport_priority().value = 23;
        auto* pw = s.pub->create_datawriter(tx, w); auto* pr = s.sub->create_datareader(rx, Suite::keep_all(s.rq)); need(pw && pr, "priority endpoints");
        const bool m = s.matched(pw, pr, 1, 1, 10000); s.barrier();
        for (std::uint32_t n = 1; n <= 10; ++n) { auto v = s.lay(s.run + 33, n); need(write(pw, &v), "priority write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(pr, s.run + 33, next); return next == 11; }, 10000);
        const bool acked = ok(pw->wait_for_acknowledgments(seconds(5)));
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        const char* path = "not_passed_to_transport_by_fastdds_2_14";
#elif FASTDDS_VERSION_MAJOR == 3 && FASTDDS_VERSION_MINOR < 4
        const char* path = "not_passed_to_transport_by_fastdds_3_0_to_3_3";
#else
        const char* path = "passed_to_SenderResource_send_recorded_not_scheduled";
#endif
        s.report(G, "transport_priority_23_delivery", got && acked, std::string("priority=23 ") + path + " received_in_order=" + std::to_string(next - 1));
    }
    s.barrier();
}

// ---------------------------------------------------------------- feature group
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
struct DiscoverySpy final : fd::DomainParticipantListener {
    std::string want_user, want_topic, want_group; std::atomic<bool> user{false}, topic{false}, group{false};
    template<class V> static bool eq(const V& v, const std::string& s)
    { if (v.size() != s.size()) return false; for (std::size_t i = 0; i < s.size(); ++i) if (v[i] != static_cast<unsigned char>(s[i])) return false; return true; }
    void on_participant_discovery(fd::DomainParticipant*, rtps::ParticipantDiscoveryInfo&& info) override
    { if (eq(info.info.m_userData.data_vec(), want_user)) user = true; }
    void on_publisher_discovery(fd::DomainParticipant*, rtps::WriterDiscoveryInfo&& info) override
    { if (eq(info.info.m_qos.m_topicData.data_vec(), want_topic)) topic = true; if (eq(info.info.m_qos.m_groupData.data_vec(), want_group)) group = true; }
};
#else
struct DiscoverySpy final : fd::DomainParticipantListener {
    std::string want_user, want_topic, want_group; std::atomic<bool> user{false}, topic{false}, group{false};
    template<class V> static bool eq(const V& v, const std::string& s)
    { if (v.size() != s.size()) return false; for (std::size_t i = 0; i < s.size(); ++i) if (v[i] != static_cast<unsigned char>(s[i])) return false; return true; }
    void on_participant_discovery(fd::DomainParticipant*, rtps::ParticipantDiscoveryStatus, const fd::ParticipantBuiltinTopicData& info, bool&) override
    { if (eq(info.user_data.data_vec(), want_user)) user = true; }
    void on_data_writer_discovery(fd::DomainParticipant*, rtps::WriterDiscoveryStatus, const fd::PublicationBuiltinTopicData& info, bool&) override
    { if (eq(info.topic_data.data_vec(), want_topic)) topic = true; if (eq(info.group_data.data_vec(), want_group)) group = true; }
};
#endif

static void participants_cases(Suite& s)
{
    const char* G = "feature";
    DiscoverySpy spy; spy.want_user = "fdds-dsb-user-" + std::to_string(s.peer); spy.want_topic = "fdds-dsb-topic-" + std::to_string(s.peer); spy.want_group = "fdds-dsb-group-" + std::to_string(s.peer);
    auto octets = [](const std::string& t) { return std::vector<rtps::octet>(t.begin(), t.end()); };
    // Second Participant, same domain and Runtime, with user/topic/group data, flow controller and SQLite persistence.
    fd::DomainParticipantQos q2 = s.named("_p2"); s.with_descriptor(q2);
    q2.user_data().data_vec(octets("fdds-dsb-user-" + std::to_string(s.me)));
    auto fc = std::make_shared<eprosima::fastdds::rtps::FlowControllerDescriptor>(); fc->name = "fdds_dsb_fc"; fc->max_bytes_per_period = 65536; fc->period_ms = 100;
    q2.flow_controllers().push_back(fc);
    const std::string db = s.workdir + "/persist_" + std::to_string(s.me) + ".db"; (void)unlink(db.c_str());
    q2.properties().properties().emplace_back("dds.persistence.plugin", "builtin.SQLITE3");
    q2.properties().properties().emplace_back("dds.persistence.sqlite3.filename", db);
    auto* p2 = s.extra_participant(q2, &spy); need(ok(s.layer_type.register_type(p2)), "register p2");
    fd::TopicQos tq = fd::TOPIC_QOS_DEFAULT; tq.topic_data().data_vec(octets("fdds-dsb-topic-" + std::to_string(s.me)));
    fd::PublisherQos pq = fd::PUBLISHER_QOS_DEFAULT; pq.group_data().data_vec(octets("fdds-dsb-group-" + std::to_string(s.me)));
    auto* pub2 = p2->create_publisher(pq); auto* sub2 = p2->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub2 && sub2, "p2 pub/sub");
    auto* tx = p2->create_topic(s.tname("p2", s.me), s.layer_type.get_type_name(), tq); auto* rx = p2->create_topic(s.tname("p2", s.peer), s.layer_type.get_type_name(), fd::TOPIC_QOS_DEFAULT);
    need(tx && rx, "p2 topics");
    auto* w2 = pub2->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r2 = sub2->create_datareader(rx, Suite::keep_all(s.rq)); need(w2 && r2, "p2 endpoints");
    const bool m = s.matched(w2, r2, 1, 1, 20000); s.barrier();
    for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 21, n); need(write(w2, &v), "p2 write"); }
    std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(r2, s.run + 21, next); return next == 4; }, 10000);
    s.report(G, "second_participant_same_domain", got, "matched=" + std::to_string(m) + " received=" + std::to_string(next - 1));
    const bool disc = s.waitp([&] { return spy.user.load() && spy.topic.load() && spy.group.load(); }, 8000);
    s.report(G, "discovery_user_topic_group_data", disc, "user_data=" + std::to_string(spy.user.load()) + " topic_data=" + std::to_string(spy.topic.load()) + " group_data=" + std::to_string(spy.group.load()));
    s.barrier();
    {   // ASYNCHRONOUS publish through a 64 KiB / 100 ms flow controller vs an unthrottled baseline.
        fd::TypeSupport bt{new BlobType("FastDDSDSoftBusFrag", kFragBytes)}; need(ok(bt.register_type(p2)), "p2 frag type");
        fd::Topic *atx, *arx; s.topics(p2, "async", bt.get_type_name(), atx, arx);
        fd::DataWriterQos aw = Suite::keep_all(s.wq); aw.publish_mode().kind = fd::ASYNCHRONOUS_PUBLISH_MODE; aw.publish_mode().flow_controller_name = "fdds_dsb_fc";
        auto* fw = pub2->create_datawriter(atx, aw); auto* fr = sub2->create_datareader(arx, Suite::keep_all(s.rq)); need(fw && fr, "async endpoints");
        const bool am = s.matched(fw, fr, 1, 1, 20000); s.barrier();
        const auto t0 = Clock::now(); for (std::uint32_t n = 1; n <= 6; ++n) { auto v = blob(s.run + 40 + n, s.me, kFragBytes); need(write(fw, &v), "async write"); }
        const bool acked = ok(fw->wait_for_acknowledgments(seconds(30)));
        const auto ack_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        std::uint32_t nf = 1; const bool fok = am && s.waitp([&] { for (;;) { Blob v; fd::SampleInfo i; if (!ok(fr->take_next_sample(&v, &i))) break;
            if (i.valid_data && blob_valid(v, s.run + 40 + nf, s.peer, kFragBytes)) ++nf; } return nf == 7; }, 30000);
        fd::Topic *btx, *brx; s.topics(p2, "async_free", bt.get_type_name(), btx, brx);
        fd::DataWriterQos bw = Suite::keep_all(s.wq); bw.publish_mode().kind = fd::ASYNCHRONOUS_PUBLISH_MODE;
        auto* bfw = pub2->create_datawriter(btx, bw); auto* bfr = sub2->create_datareader(brx, Suite::keep_all(s.rq)); need(bfw && bfr, "async free endpoints");
        const bool bm = s.matched(bfw, bfr, 1, 1, 20000); s.barrier();
        const auto t1 = Clock::now(); for (std::uint32_t n = 1; n <= 6; ++n) { auto v = blob(s.run + 60 + n, s.me, kFragBytes); need(write(bfw, &v), "async free write"); }
        const bool backed = ok(bfw->wait_for_acknowledgments(seconds(30)));
        const auto free_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t1).count();
        std::uint32_t nb = 1; (void)s.waitp([&] { for (;;) { Blob v; fd::SampleInfo i; if (!ok(bfr->take_next_sample(&v, &i))) break;
            if (i.valid_data && blob_valid(v, s.run + 60 + nb, s.peer, kFragBytes)) ++nb; } return nb == 7; }, 30000);
        const bool limited = ack_ms >= 1200 && bm && backed && nb == 7 && free_ms * 2 <= ack_ms;
        s.report(G, "async_publish_flow_controller", fok && acked && limited, "received_verified=" + std::to_string(nf - 1) + " write_to_ack_ms=" + std::to_string(ack_ms) +
            " unthrottled_ms=" + std::to_string(free_ms) + " limit=65536B/100ms");
    }
    s.barrier();
    {   // TRANSIENT durability with the SQLite persistence plugin: history survives writer recreation.
        fd::Topic *ptx, *prx; s.topics(p2, "persist", s.layer_type.get_type_name(), ptx, prx);
        fd::DataWriterQos pw = Suite::keep_all(s.wq); pw.durability().kind = fd::TRANSIENT_DURABILITY_QOS;
        pw.properties().properties().emplace_back("dds.persistence.guid", "fd.d5.b0.77.72.69.74.65.72.00.00.0" + std::to_string(s.me + 1) + "|0.0.1.3");
        auto* w1 = pub2->create_datawriter(ptx, pw); need(w1, "persist writer");
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 22, n); need(write(w1, &v), "persist write"); }
        need(ok(pub2->delete_datawriter(w1)), "delete persist writer");
        auto* w2b = pub2->create_datawriter(ptx, pw); need(w2b, "persist writer 2");
        for (std::uint32_t n = 4; n <= 5; ++n) { auto v = s.lay(s.run + 22, n); need(write(w2b, &v), "persist write"); }
        s.barrier();
        fd::DataReaderQos pr = Suite::keep_all(s.rq); pr.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        auto* rr = sub2->create_datareader(prx, pr); need(rr, "persist reader");
        std::uint32_t np = 1; const bool pok = s.waitp([&] { s.take_layers(rr, s.run + 22, np); return np == 6; }, 15000);
        s.report(G, "transient_durability_sqlite_persistence", pok, "received_in_order=" + std::to_string(np - 1) + " writer_recreated=1");
        (void)w2b->wait_for_acknowledgments(seconds(3));
    }
    s.barrier();
    {   // Four Participants on one Runtime.
        std::array<fd::DomainParticipant*, 2> px{}; std::array<fd::DataWriter*, 2> wx{}; std::array<fd::DataReader*, 2> rxs{};
        for (unsigned k = 0; k < 2; ++k) {
            fd::DomainParticipantQos qx = s.named("_p" + std::to_string(3 + k)); s.with_descriptor(qx);
            px[k] = s.extra_participant(qx); need(ok(s.layer_type.register_type(px[k])), "register px");
            auto* pubx = px[k]->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* subx = px[k]->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pubx && subx, "px pub/sub");
            fd::Topic *t1, *t2; s.topics(px[k], "p" + std::to_string(3 + k), s.layer_type.get_type_name(), t1, t2);
            wx[k] = pubx->create_datawriter(t1, Suite::keep_all(s.wq)); rxs[k] = subx->create_datareader(t2, Suite::keep_all(s.rq)); need(wx[k] && rxs[k], "px endpoints"); }
        const bool m4 = s.matched(wx[0], rxs[0], 1, 1, 20000) && s.matched(wx[1], rxs[1], 1, 1, 20000); s.barrier();
        for (unsigned k = 0; k < 2; ++k) for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 35 + k, n); need(write(wx[k], &v), "px write"); }
        std::array<std::uint32_t, 2> nx{1, 1};
        const bool g4 = m4 && s.waitp([&] { for (unsigned k = 0; k < 2; ++k) s.take_layers(rxs[k], s.run + 35 + k, nx[k]); return nx[0] == 4 && nx[1] == 4; }, 10000);
        const bool a4 = g4 && ok(wx[0]->wait_for_acknowledgments(seconds(5))) && ok(wx[1]->wait_for_acknowledgments(seconds(5)));
        s.report(G, "four_participants_one_runtime", g4 && a4, "participants=4 received=" + std::to_string(nx[0] - 1) + "," + std::to_string(nx[1] - 1) + " acked=" + std::to_string(a4));
        s.barrier(); for (unsigned k = 2; k-- > 0;) s.drop(px[k]); s.barrier();
    }
    s.drop(p2);
}

static void dds_api_cases(Suite& s)
{
    const char* G = "feature";
    {   // Request/reply correlation through related_sample_identity.
        fd::Topic *qtx, *qrx, *ptx, *prx; s.topics(s.participant, "request", s.layer_type.get_type_name(), qtx, qrx); s.topics(s.participant, "reply", s.layer_type.get_type_name(), ptx, prx);
        auto* qw = s.pub->create_datawriter(qtx, Suite::keep_all(s.wq)); auto* qr = s.sub->create_datareader(qrx, Suite::keep_all(s.rq));
        auto* pw = s.pub->create_datawriter(ptx, Suite::keep_all(s.wq)); auto* pr = s.sub->create_datareader(prx, Suite::keep_all(s.rq)); need(qw && qr && pw && pr, "rr endpoints");
        const bool m = s.matched(qw, qr, 1, 1, 10000) && s.matched(pw, pr, 1, 1, 10000); s.barrier();
        std::array<rtps::SampleIdentity, 3> ids{};
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 26, n); rtps::WriteParams p; need(write(qw, &v, p), "request write"); ids[n - 1] = p.sample_identity(); }
        unsigned served = 0, correlated = 0, wrong = 0;
        const bool done = m && s.waitp([&] {
            for (;;) { Layer x; fd::SampleInfo i; if (!ok(qr->take_next_sample(&x, &i))) break; if (!i.valid_data || x.run != s.run + 26 || x.side != s.peer) continue;
                auto v = s.lay(s.run + 27, x.ordinal + 100); rtps::WriteParams p; p.related_sample_identity(i.sample_identity); need(write(pw, &v, p), "reply write"); ++served; }
            for (;;) { Layer x; fd::SampleInfo i; if (!ok(pr->take_next_sample(&x, &i))) break; if (!i.valid_data || x.run != s.run + 27 || x.side != s.peer) continue;
                if (x.ordinal >= 101 && x.ordinal <= 103 && i.related_sample_identity == ids[x.ordinal - 101]) ++correlated; else ++wrong; }
            return served == 3 && correlated == 3; }, 10000);
        (void)pw->wait_for_acknowledgments(seconds(3));
        s.report(G, "request_reply_related_sample_identity", done && wrong == 0, "served=" + std::to_string(served) + " correlated=" + std::to_string(correlated) + " mismatched=" + std::to_string(wrong));
    }
    s.barrier();
    {   // WaitSet with StatusCondition and ReadCondition.
        fd::Topic *tx, *rx; s.topics(s.participant, "waitset", s.layer_type.get_type_name(), tx, rx);
        auto* ww = s.pub->create_datawriter(tx, s.wq); auto* wr = s.sub->create_datareader(rx, s.rq); need(ww && wr, "waitset endpoints");
        fd::StatusCondition& sc = wr->get_statuscondition(); need(ok(sc.set_enabled_statuses(fd::StatusMask::data_available())), "status condition");
        fd::ReadCondition* rc = wr->create_readcondition(fd::NOT_READ_SAMPLE_STATE, fd::ANY_VIEW_STATE, fd::ANY_INSTANCE_STATE); need(rc, "read condition");
        fd::WaitSet ws; need(ok(ws.attach_condition(sc)) && ok(ws.attach_condition(*rc)), "attach");
        const bool m = s.matched(ww, wr, 1, 1, 10000); s.barrier();
        { auto v = s.lay(s.run + 28, 1); need(write(ww, &v), "waitset write"); }
        fd::ConditionSeq active; const bool woke = ok(ws.wait(active, seconds(5))) && !active.empty();
        bool rc_seen = false; for (auto* c : active) if (c == rc) rc_seen = true;
        if (!rc_seen) rc_seen = s.waitp([&] { return rc->get_trigger_value(); }, 2000);
        Layer x; fd::SampleInfo i; const bool read = ok(wr->read_next_sample(&x, &i)) && i.valid_data && x.run == s.run + 28 && x.side == s.peer;
        const bool states = read && i.sample_state == fd::NOT_READ_SAMPLE_STATE && i.view_state == fd::NEW_VIEW_STATE && i.instance_state == fd::ALIVE_INSTANCE_STATE;
        const bool cleared = !rc->get_trigger_value() && wr->get_unread_count() == 0;
        need(ok(ws.detach_condition(*rc)) && ok(ws.detach_condition(sc)) && ok(wr->delete_readcondition(rc)), "detach");
        (void)ww->wait_for_acknowledgments(seconds(3));
        s.report(G, "waitset_status_and_read_condition", m && woke && rc_seen && states && cleared, "woke=" + std::to_string(woke) + " read_condition=" + std::to_string(rc_seen) +
            " states_not_read_new_alive=" + std::to_string(states) + " cleared_after_read=" + std::to_string(cleared));
    }
    s.barrier();
    {   // DataWriter loans on a plain type.
        fd::TypeSupport pt{new PlainType}; need(ok(pt.register_type(s.participant)), "plain type");
        fd::Topic *tx, *rx; s.topics(s.participant, "loans", pt.get_type_name(), tx, rx);
        auto* lw = s.pub->create_datawriter(tx, Suite::keep_all(s.wq)); auto* lr = s.sub->create_datareader(rx, Suite::keep_all(s.rq)); need(lw && lr, "loan endpoints");
        const bool m = s.matched(lw, lr, 1, 1, 10000); s.barrier();
        bool loaned = true;
        for (std::uint32_t n = 1; n <= 5; ++n) { void* raw = nullptr; if (!ok(lw->loan_sample(raw)) || !raw) { loaned = false; break; }
            plain_fill(*static_cast<Plain*>(raw), s.run + 37, s.me, n); need(write(lw, raw), "loan write"); }
        std::uint32_t next = 1; bool valid = true, returned = true;
        const bool got = m && loaned && s.waitp([&] { fd::LoanableSequence<Plain> data; fd::SampleInfoSeq infos;
            if (ok(lr->take(data, infos, 4))) { for (fd::LoanableCollection::size_type k = 0; k < infos.length(); ++k) { if (!infos[k].valid_data) continue;
                if (plain_valid(data[k], s.run + 37, s.peer, next)) ++next; else valid = false; }
                returned = ok(lr->return_loan(data, infos)) && returned; }
            return next == 6 || !valid; }, 10000);
        const bool acked = got && ok(lw->wait_for_acknowledgments(seconds(5)));
        s.report(G, "datawriter_loans_plain_type", got && valid && returned && acked, "data_sharing=OFF loaned_writes=" + std::to_string(loaned ? 5 : 0) +
            " received_verified=" + std::to_string(next - 1) + " loans_returned=" + std::to_string(returned));
    }
    s.barrier();
    {   // Static endpoint discovery from XML.
        const auto pname = [&](unsigned side) { return std::string("dsb_") + s.run_hex + "_" + std::to_string(side) + "_sedp"; };
        const std::string type = s.layer_type.get_type_name();
        const auto ep = [&](const char* kind, unsigned id, const std::string& topic) { return std::string("<") + kind + "><userId>" + std::to_string(id) + "</userId><entityID>" +
            std::to_string(id) + "</entityID><topicName>" + topic + "</topicName><topicDataType>" + type + "</topicDataType><topicKind>NO_KEY</topicKind>"
            "<reliabilityQos>RELIABLE_RELIABILITY_QOS</reliabilityQos><durabilityQos>VOLATILE_DURABILITY_QOS</durabilityQos></" + kind + ">"; };
        const std::string xml = "data://<?xml version=\"1.0\" encoding=\"utf-8\"?><staticdiscovery><participant><name>" + pname(s.peer) + "</name>" +
            ep("writer", 110 + s.peer, s.tname("sedp", s.peer)) + ep("reader", 120 + s.peer, s.tname("sedp", s.me)) + "</participant></staticdiscovery>";
        fd::DomainParticipantQos q5 = s.qos; q5.name(pname(s.me).c_str()); s.with_descriptor(q5);
        auto& dc = q5.wire_protocol().builtin.discovery_config; dc.use_SIMPLE_EndpointDiscoveryProtocol = false; dc.use_STATIC_EndpointDiscoveryProtocol = true; dc.static_edp_xml_config(xml.c_str());
        auto* p5 = s.extra_participant(q5); need(ok(s.layer_type.register_type(p5)), "sedp type");
        auto* pub5 = p5->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub5 = p5->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub5 && sub5, "sedp pub/sub");
        fd::Topic *tx, *rx; s.topics(p5, "sedp", type, tx, rx);
        fd::DataWriterQos w5 = Suite::keep_all(s.wq); w5.endpoint().user_defined_id = static_cast<int16_t>(110 + s.me); w5.endpoint().entity_id = static_cast<int16_t>(110 + s.me);
        fd::DataReaderQos r5 = Suite::keep_all(s.rq); r5.endpoint().user_defined_id = static_cast<int16_t>(120 + s.me); r5.endpoint().entity_id = static_cast<int16_t>(120 + s.me);
        auto* sw5 = pub5->create_datawriter(tx, w5); auto* sr5 = sub5->create_datareader(rx, r5); need(sw5 && sr5, "sedp endpoints");
        const bool m = s.matched(sw5, sr5, 1, 1, 30000); s.barrier();
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 38, n); need(write(sw5, &v), "sedp write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(sr5, s.run + 38, next); return next == 4; }, 10000);
        const bool acked = got && ok(sw5->wait_for_acknowledgments(seconds(5)));
        s.report(G, "static_endpoint_discovery_xml", got && acked, "edp=STATIC matched=" + std::to_string(m) + " received=" + std::to_string(next - 1));
        s.barrier(); s.drop(p5);
    }
    s.barrier();
}

// ContentFilteredTopic (DDS-SQL with parameters) on a DynamicType carrying type information.
static void cft_case(Suite& s)
{
    const char* G = "feature";
    DynRecord rec("FastDDSDSoftBusCft", false); need(bool(rec.type), "cft dynamic type");
    fd::TypeSupport ct = rec.support(true); need(ok(ct.register_type(s.participant)), "cft type");
    fd::Topic *tx, *rx; s.topics(s.participant, "cft", ct.get_type_name(), tx, rx);
    auto* cw = s.pub->create_datawriter(tx, Suite::keep_all(s.wq)); need(cw, "cft writer"); s.barrier();
    // Local filter changes are staggered between the boards: remote reader filters are created on
    // the discovery thread, and the DDS-SQL expression pool is unlocked (see the CFT race report).
    auto staged = [&](auto fn) { for (unsigned turn = 0; turn < 2; ++turn) { if (turn == s.me) fn(); std::this_thread::sleep_for(std::chrono::milliseconds(1000)); s.barrier(); } };
    fd::ContentFilteredTopic* cft = nullptr; fd::DataReader* cr = nullptr;
    staged([&] { cft = s.participant->create_contentfilteredtopic(s.tname("cft_view", s.peer), rx, "n > %0 AND n < %1", {"3", "8"}); need(cft, "create cft");
        cr = s.sub->create_datareader(cft, Suite::keep_all(s.rq)); need(cr, "cft reader"); });
    auto wd = rec.create(), rd = rec.create(); need(bool(wd) && bool(rd), "cft data");
    auto send = [&](std::uint32_t a, std::uint32_t b) { for (std::uint32_t n = a; n <= b; ++n) { need(DynRecord::set(wd, s.me, n), "cft set"); need(write(cw, DynRecord::ptr(wd)), "cft write"); } };
    std::vector<std::uint32_t> got; bool foreign = false;
    auto drain = [&] { for (;;) { fd::SampleInfo i; if (!ok(cr->take_next_sample(DynRecord::ptr(rd), &i))) break; if (!i.valid_data) continue; std::uint32_t sd = 0, n = 0;
        need(DynRecord::get(rd, sd, n), "cft get"); if (sd != s.peer) foreign = true; else got.push_back(n); } };
    const bool m = s.matched(cw, cr, 1, 1, 15000); s.barrier(); send(1, 10);
    (void)s.waitp([&] { drain(); return got.size() >= 4; }, 6000); (void)cw->wait_for_acknowledgments(seconds(3)); s.barrier(); drain();
    const auto first = got; got.clear();
    staged([&] { need(ok(cft->set_expression_parameters({"10", "13"})), "set parameters"); });
    send(11, 15); (void)s.waitp([&] { drain(); return got.size() >= 2; }, 6000); (void)cw->wait_for_acknowledgments(seconds(3)); s.barrier(); drain();
    auto list = [](const std::vector<std::uint32_t>& v) { std::string r; for (auto x : v) r += (r.empty() ? "" : ",") + std::to_string(x); return r.empty() ? std::string("-") : r; };
    s.report(G, "content_filtered_topic_sql", m && !foreign && first == std::vector<std::uint32_t>{4, 5, 6, 7} && got == std::vector<std::uint32_t>{11, 12},
        "params=3,8 got=" + list(first) + " params=10,13 got=" + list(got));
    staged([&] { need(ok(s.sub->delete_datareader(cr)) && ok(s.participant->delete_contentfilteredtopic(cft)), "delete cft"); });
    DynRecord::release(wd); DynRecord::release(rd);
    s.barrier();
}

// TypeLookup: each side publishes a DynamicType the peer never registers; the peer resolves it remotely.
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
struct TLSpy final : fd::DomainParticipantListener {
    std::string want; std::mutex m; std::optional<eprosima::fastrtps::types::TypeInformation> info; std::string type_name;
    eprosima::fastrtps::types::DynamicType_ptr remote; std::string remote_name;
    void on_type_information_received(fd::DomainParticipant*, const eprosima::fastrtps::string_255 topic, const eprosima::fastrtps::string_255 type,
            const eprosima::fastrtps::types::TypeInformation& ti) override
    { std::lock_guard<std::mutex> l(m); if (topic.to_string() != want || info) return; info = ti; type_name = type.to_string(); }
};
#else
struct TLSpy final : fd::DomainParticipantListener {
    std::string want; std::mutex m; std::optional<fd::xtypes::TypeInformation> info; std::string type_name;
    void on_data_writer_discovery(fd::DomainParticipant*, rtps::WriterDiscoveryStatus st, const fd::PublicationBuiltinTopicData& d, bool&) override
    {
        if (st != rtps::WriterDiscoveryStatus::DISCOVERED_WRITER) return;
        std::lock_guard<std::mutex> l(m); if (d.topic_name.to_string() != want || info) return;
        info = d.type_information.type_information; type_name = d.type_name.to_string();
    }
};
#endif
static void typelookup_case(Suite& s)
{
    const char* G = "feature";
    TLSpy tl; tl.want = s.tname("tl", s.peer);
    fd::DomainParticipantQos q7 = s.named("_tl"); s.with_descriptor(q7);
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    q7.wire_protocol().builtin.typelookup_config.use_client = true; q7.wire_protocol().builtin.typelookup_config.use_server = true;
#endif
    auto* p7 = s.extra_participant(q7, &tl);
    DynRecord mine("FastDDSDSoftBusTypeLookup_" + std::to_string(s.me), true); need(bool(mine.type), "tl type");
    fd::TypeSupport ts = mine.support(true); need(ok(ts.register_type(p7)), "tl register");
    auto* pub7 = p7->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub7 = p7->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub7 && sub7, "tl pub/sub");
    auto* t7 = p7->create_topic(s.tname("tl", s.me), ts.get_type_name(), fd::TOPIC_QOS_DEFAULT); need(t7, "tl topic");
    auto* w7 = pub7->create_datawriter(t7, Suite::keep_all(s.wq)); need(w7, "tl writer");
    const bool info = s.waitp([&] { std::lock_guard<std::mutex> l(tl.m); return bool(tl.info); }, 30000);
    std::string path = "none", rname; std::optional<DynRecord> remote;
    if (info) {
        std::string tn; {std::lock_guard<std::mutex> l(tl.m); tn = tl.type_name;} rname = tn;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        namespace ft = eprosima::fastrtps::types;
        eprosima::fastrtps::types::TypeInformation ti; {std::lock_guard<std::mutex> l(tl.m); ti = *tl.info;}
        std::function<void(const std::string&, const ft::DynamicType_ptr)> on_remote = [&tl](const std::string& name, const ft::DynamicType_ptr type)
        { std::lock_guard<std::mutex> l2(tl.m); tl.remote = type; tl.remote_name = name; };
        const bool before = ft::TypeObjectFactory::get_instance()->get_type_object(&ti.complete().typeid_with_size().type_id()) != nullptr;
        const auto rc = p7->register_remote_type(ti, tn, on_remote);
        if (ok(rc)) { fd::TypeSupport found = p7->find_type(tn); auto* dps = found ? dynamic_cast<ft::DynamicPubSubType*>(found.get()) : nullptr;
            if (dps) { std::lock_guard<std::mutex> l(tl.m); tl.remote = dps->GetDynamicType(); tl.remote_name = tn; }
            path = before ? "typelookup_before_register" : "resolved_at_register"; }
        else if (rc == rc_no_data()) path = "typelookup_reply_callback";
        if (s.waitp([&] { std::lock_guard<std::mutex> l(tl.m); return bool(tl.remote); }, 30000)) { std::lock_guard<std::mutex> l(tl.m); remote.emplace(tl.remote); }
#else
        // XTypes 1.3: the remote TypeObject reaches the process-wide registry through the TypeLookup
        // service (discovery carries only TypeInformation); build the DynamicType from it.
        fd::xtypes::TypeInformation ti; {std::lock_guard<std::mutex> l(tl.m); ti = *tl.info;}
        fd::xtypes::TypeObject obj; auto& reg = fd::DomainParticipantFactory::get_instance()->type_object_registry();
        const auto& tid = ti.complete().typeid_with_size().type_id();
        if (s.waitp([&] { return ok(reg.get_type_object(tid, obj)); }, 30000)) {
            auto builder = fd::DynamicTypeBuilderFactory::get_instance()->create_type_w_type_object(obj);
            if (builder) { auto t = builder->build(); if (t) { remote.emplace(t); path = "xtypes_type_lookup_registry"; } } }
#endif
    }
    fd::DataReader* r7 = nullptr;
    if (remote) { fd::TypeSupport rts = remote->support(false); if (!p7->find_type(rname)) need(ok(rts.register_type(p7, rname)), "tl remote register");
        auto* rt7 = p7->create_topic(s.tname("tl", s.peer), rname, fd::TOPIC_QOS_DEFAULT); need(rt7, "tl remote topic");
        r7 = sub7->create_datareader(rt7, Suite::keep_all(s.rq)); need(r7, "tl reader"); }
    const bool m = remote && s.matched(w7, r7, 1, 1, 30000);
    s.barrier();
    auto wd = mine.create(); need(bool(wd), "tl data");
    for (std::uint32_t n = 1; n <= 3; ++n) { need(DynRecord::set(wd, s.me, n) && DynRecord::set_text(wd, "tl-" + std::to_string(s.me) + "-" + std::to_string(n)), "tl set");
        need(write(w7, DynRecord::ptr(wd)), "tl write"); }
    std::uint32_t next = 1; bool texts = true;
    if (m) { auto rd = remote->create(); need(bool(rd), "tl remote data");
        (void)s.waitp([&] { for (;;) { fd::SampleInfo i; if (!ok(r7->take_next_sample(DynRecord::ptr(rd), &i))) break; if (!i.valid_data) continue;
            std::uint32_t sd = 0, n = 0; std::string t; need(DynRecord::get(rd, sd, n) && DynRecord::get_text(rd, t), "tl get");
            if (sd == s.peer && n == next) { if (t != "tl-" + std::to_string(s.peer) + "-" + std::to_string(n)) texts = false; ++next; } } return next == 4; }, 10000);
        DynRecord::release(rd); }
    DynRecord::release(wd);
    const bool acked = ok(w7->wait_for_acknowledgments(seconds(5)));
    s.report(G, "type_lookup_remote_type", info && remote && m && next == 4 && texts && acked, "type_information=" + std::to_string(info) + " resolved=" + std::to_string(bool(remote)) +
        " path=" + path + " remote_type=" + rname + " received=" + std::to_string(next - 1) + " fields_ok=" + std::to_string(texts));
    s.barrier(); s.drop(p7);
    s.barrier();
}

// A second DDS domain concurrently in the same process; each domain has its own Runtime.
static void second_domain_case(Suite& s)
{
    const char* G = "feature";
    Deploy q1 = s.q; q1.domain = s.q.domain + 1; auto c1 = configuration(q1, true);
    Pumped d1; d1.rt = std::make_unique<ds::experimental::Runtime>(backend(c1), ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
    std::atomic<bool> go{false}, gated{false};
    need(d1.rt->SetPreConnectGate([&] { gated = true; const auto end = Clock::now() + std::chrono::seconds(40);
        while (!go.load()) { if (Clock::now() >= end) return false; std::this_thread::sleep_for(std::chrono::milliseconds(20)); } return true; }), "domain1 gate");
    std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> descs{d1.rt->MakeStockDescriptor(c1)}; need(bool(descs[0]), "domain1 descriptor");
    d1.start();
    fd::DomainParticipantQos q3 = s.named("_d1"); participant_locators(q3, c1, descs[0]);
    fd::DomainParticipant* p = nullptr; std::thread creator([&] { p = s.factory->create_participant(q1.domain, q3); });
    (void)s.waitp([&] { return gated.load(); }, 15000); s.barrier(); go = true; creator.join();
    bool okay = false; std::string detail = "participant=" + std::to_string(p != nullptr) + " domain=" + std::to_string(q1.domain);
    s.barrier();
    if (p) {
        need(ok(s.layer_type.register_type(p)), "domain1 type");
        auto* pub3 = p->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub3 = p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub3 && sub3, "domain1 pub/sub");
        fd::Topic *tx, *rx; s.topics(p, "domain1", s.layer_type.get_type_name(), tx, rx);
        auto* w3 = pub3->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r3 = sub3->create_datareader(rx, Suite::keep_all(s.rq)); need(w3 && r3, "domain1 endpoints");
        const bool m = s.matched(w3, r3, 1, 1, 30000); s.barrier();
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 32, n); need(write(w3, &v), "domain1 write"); }
        std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(r3, s.run + 32, next); return next == 4; }, 10000);
        // Once every sample is acknowledged this domain has nothing left to deliver. The peer may pass the
        // barrier below and close first, so a later send can reach a channel that is already gone
        // (measured: SOFTBUS_TRANS_TDC_GET_INFO_FAILED during the barrier). Only failures up to here fail
        // the case; later send failures are counted in the detail.
        const bool acked = ok(w3->wait_for_acknowledgments(seconds(5)));
        d1.tolerate = true;
        const bool exchange_failed = d1.failed.load();
        s.barrier();
        const bool deleted = ok(p->delete_contained_entities()) && ok(s.factory->delete_participant(p));
        const bool closed = deleted && d1.stop_and_close(descs);
        okay = got && acked && closed && !exchange_failed && !d1.failed.load();
        detail += " matched=" + std::to_string(m) + " received=" + std::to_string(next - 1) + " acked=" + std::to_string(acked) +
            " exchange_failed=" + std::to_string(exchange_failed) +
            " failure_source=" + std::to_string(d1.failure_source.load()) + " failure_value=" + std::to_string(d1.failure_value.load()) +
            " teardown_send_failures=" + std::to_string(d1.send_failures.load()) +
            " second_runtime_closed=" + std::to_string(closed);
    } else { s.barrier(); s.barrier(); go = true; d1.stop_and_close(descs); }
    s.report(G, "second_domain_concurrent", okay, detail);
    s.barrier();
}

// Peer OS-process restart: side 1 runs the peer in a helper process, kills it (SIGKILL), starts a new one.
struct RestartRequest { std::string run_hex, local_udid, peer_udid, peer_network, status, go, tx, rx; unsigned side = 0, peer = 1, incarnation = 0; std::uint32_t domain = 0; std::uint64_t run = 0; };
static void save_request(const std::string& path, const RestartRequest& r)
{
    FILE* f = std::fopen(path.c_str(), "w"); need(f != nullptr, "restart request");
    std::fprintf(f, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%u %u %u %u %llu\n", r.run_hex.c_str(), r.local_udid.c_str(), r.peer_udid.c_str(), r.peer_network.c_str(),
        r.status.c_str(), r.go.c_str(), r.tx.c_str(), r.rx.c_str(), r.side, r.peer, r.incarnation, r.domain, static_cast<unsigned long long>(r.run));
    std::fclose(f);
}
static RestartRequest load_request(const std::string& path)
{
    std::ifstream in(path); RestartRequest r; std::getline(in, r.run_hex); std::getline(in, r.local_udid); std::getline(in, r.peer_udid); std::getline(in, r.peer_network);
    std::getline(in, r.status); std::getline(in, r.go); std::getline(in, r.tx); std::getline(in, r.rx);
    unsigned long long run = 0; in >> r.side >> r.peer >> r.incarnation >> r.domain >> run; r.run = run; return r;
}
static void put_status(const std::string& path, const std::string& text)
{ const std::string tmp = path + ".tmp"; if (FILE* f = std::fopen(tmp.c_str(), "w")) { std::fputs(text.c_str(), f); std::fclose(f); std::rename(tmp.c_str(), path.c_str()); } }
static int restart_helper(const std::string& req)
{
    const auto r = load_request(req);
    auto chk = [&](bool v, const char* where) { if (!v) { put_status(r.status, std::string("fail ") + where); _exit(3); } };
    Deploy q = make_deploy(r.side, r.run_hex, r.domain, r.local_udid, r.peer_udid, r.peer_network); auto c = configuration(q, true);
    Pumped pm; pm.rt = std::make_unique<ds::experimental::Runtime>(backend(c), ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
    chk(pm.rt->SetPreConnectGate([&r] { put_status(r.status, "listening"); return wait_file(r.go, 60); }), "gate");
    std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> descs{pm.rt->MakeStockDescriptor(c)}; chk(bool(descs[0]), "descriptor");
    pm.start();
    auto* f = fd::DomainParticipantFactory::get_instance(); fd::DomainParticipantQos qos; qos.name(("dsb_restart_" + std::to_string(r.incarnation)).c_str());
    participant_locators(qos, c, descs[0]);
    qos.wire_protocol().builtin.discovery_config.leaseDuration = seconds(3); qos.wire_protocol().builtin.discovery_config.leaseDuration_announcementperiod = seconds(1);
    auto* p = f->create_participant(q.domain, qos); chk(p != nullptr, "participant");
    fd::TypeSupport type{new LayerType}; chk(ok(type.register_type(p)), "type");
    auto* pub = p->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub = p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); chk(pub && sub, "pubsub");
    auto* tx = p->create_topic(r.tx, type.get_type_name(), fd::TOPIC_QOS_DEFAULT); auto* rx = p->create_topic(r.rx, type.get_type_name(), fd::TOPIC_QOS_DEFAULT); chk(tx && rx, "topics");
    fd::DataWriterQos wq; wq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS; wq.history().kind = fd::KEEP_ALL_HISTORY_QOS; wq.data_sharing().off();
    fd::DataReaderQos rq; rq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS; rq.history().kind = fd::KEEP_ALL_HISTORY_QOS; rq.data_sharing().off();
    auto* w = pub->create_datawriter(tx, wq); auto* rd = sub->create_datareader(rx, rq); chk(w && rd, "endpoints");
    auto until = [](auto pred, int ms) { const auto end = Clock::now() + std::chrono::milliseconds(ms); while (Clock::now() < end) { if (pred()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(20)); } return pred(); };
    chk(until([&] { fd::PublicationMatchedStatus a; fd::SubscriptionMatchedStatus b; w->get_publication_matched_status(a); rd->get_subscription_matched_status(b);
        return a.current_count >= 1 && b.current_count >= 1; }, 45000), "match");
    for (std::uint32_t n = 1; n <= 3; ++n) { auto v = layer(r.run, r.side, n); chk(write(w, &v), "write"); }
    std::uint32_t next = 1;
    chk(until([&] { for (;;) { Layer x; fd::SampleInfo i; if (!ok(rd->take_next_sample(&x, &i))) break; if (i.valid_data && layer_valid(x, r.run, r.peer) && x.ordinal == next) ++next; } return next == 4; }, 20000), "receive");
    chk(ok(w->wait_for_acknowledgments(seconds(5))), "ack");
    std::ostringstream prefix; prefix << p->guid().guidPrefix;
    put_status(r.status, "exchanged received=3 pid=" + std::to_string(getpid()) + " prefix=" + prefix.str());
    if (r.incarnation == 1) for (;;) std::this_thread::sleep_for(std::chrono::seconds(1)); // killed by the parent, no teardown
    std::this_thread::sleep_for(std::chrono::seconds(1));
    chk(ok(p->delete_contained_entities()) && ok(f->delete_participant(p)), "teardown");
    const bool closed = pm.stop_and_close(descs);
    put_status(r.status, "done received=3 closed=" + std::to_string(closed) + " pid=" + std::to_string(getpid()) + " prefix=" + prefix.str());
    return closed ? 0 : 4;
}
static void restart_case(Suite& s)
{
    const char* G = "feature";
    Deploy q2 = s.q; q2.domain = s.q.domain + 2; const std::uint64_t base = s.run + 50; std::string detail; bool okay = false;
    if (s.me == 0) {
        auto c2 = configuration(q2, true);
        Pumped hs; hs.rt = std::make_unique<ds::experimental::Runtime>(backend(c2), ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
        std::atomic<bool> go{false}, entered{false};
        need(hs.rt->SetPreConnectGate([&] { entered = true; const auto end = Clock::now() + std::chrono::seconds(60);
            while (!go.load()) { if (Clock::now() >= end) return false; std::this_thread::sleep_for(std::chrono::milliseconds(20)); } return true; }), "restart gate");
        std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> descs{hs.rt->MakeStockDescriptor(c2)}; need(bool(descs[0]), "restart descriptor");
        hs.start();
        fd::DomainParticipantQos q8 = s.named("_restart"); participant_locators(q8, c2, descs[0]);
        q8.wire_protocol().builtin.discovery_config.leaseDuration = seconds(3); q8.wire_protocol().builtin.discovery_config.leaseDuration_announcementperiod = seconds(1);
        fd::DomainParticipant* p = nullptr; std::thread creator([&] { p = s.factory->create_participant(q2.domain, q8); });
        (void)s.waitp([&] { return entered.load(); }, 20000);
        s.barrier(); s.barrier(); go = true; creator.join(); need(p != nullptr, "restart participant");
        need(ok(s.layer_type.register_type(p)), "restart type");
        auto* pub8 = p->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub8 = p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub8 && sub8, "restart pub/sub");
        fd::Topic *tx, *rx; s.topics(p, "restart", s.layer_type.get_type_name(), tx, rx);
        auto* w8 = pub8->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r8 = sub8->create_datareader(rx, Suite::keep_all(s.rq)); need(w8 && r8, "restart endpoints");
        auto counts = [&](int want) { fd::PublicationMatchedStatus a; fd::SubscriptionMatchedStatus c; need(ok(w8->get_publication_matched_status(a)) && ok(r8->get_subscription_matched_status(c)), "restart status");
            return a.current_count == want && c.current_count == want; };
        auto exchange = [&](unsigned inc) { if (!s.waitp([&] { return counts(1); }, 60000)) return 0u;
            for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(base + inc, n); need(write(w8, &v), "restart write"); }
            std::uint32_t next = 1; (void)s.waitp([&] { s.take_layers(r8, base + inc, next); return next == 4; }, 20000);
            (void)w8->wait_for_acknowledgments(seconds(5)); return next - 1; };
        const unsigned got1 = exchange(1);
        s.barrier(); s.barrier();
        const bool gone = s.waitp([&] { return counts(0); }, 20000);
        s.barrier();
        const unsigned got2 = exchange(2);
        s.barrier(); s.barrier();
        const bool deleted = ok(p->delete_contained_entities()) && ok(s.factory->delete_participant(p));
        const bool pump_ok = !hs.failed.load(); const bool closed = deleted && hs.stop_and_close(descs);
        okay = got1 == 3 && gone && got2 == 3 && pump_ok && closed;
        detail = "role=LIVE_PROCESS inc1_received=" + std::to_string(got1) + " unmatched_after_kill=" + std::to_string(gone) + " inc2_received=" + std::to_string(got2) +
            " runtime_closed=" + std::to_string(closed);
    } else {
        auto spawn = [&](unsigned inc, const std::string& status) -> pid_t {
            RestartRequest r; r.run_hex = s.run_hex; r.local_udid = s.q.udid; r.peer_udid = s.q.peer_udid; r.peer_network = s.q.peer_network; r.status = status;
            r.go = s.workdir + "/restart_go_" + std::to_string(inc); r.tx = s.tname("restart", s.me); r.rx = s.tname("restart", s.peer);
            r.side = s.me; r.peer = s.peer; r.incarnation = inc; r.domain = q2.domain; r.run = base + inc;
            const std::string req = s.workdir + "/restart_req_" + std::to_string(inc); (void)unlink(status.c_str()); (void)unlink(r.go.c_str()); save_request(req, r);
            const pid_t pid = fork();
            if (pid == 0) { execl("/proc/self/exe", "dsb_suite", "restart-helper", req.c_str(), static_cast<char*>(nullptr)); _exit(127); }
            return pid; };
        auto read_status = [](const std::string& path) { std::string t; if (FILE* f = std::fopen(path.c_str(), "r")) { char b[256]{}; if (std::fgets(b, sizeof(b), f)) t = b; std::fclose(f); } return t; };
        auto wait_status = [&](const std::string& path, const char* prefix, int ms) { std::string t;
            (void)s.waitp([&] { t = read_status(path); return t.rfind(prefix, 0) == 0 || t.rfind("fail", 0) == 0; }, ms); return t; };
        const std::string st1 = s.workdir + "/restart_1.status", st2 = s.workdir + "/restart_2.status";
        s.barrier();
        const pid_t pid1 = spawn(1, st1); (void)wait_status(st1, "listening", 30000);
        s.barrier(); put_status(s.workdir + "/restart_go_1", "go");
        const std::string r1 = wait_status(st1, "exchanged", 70000);
        s.barrier();
        int ws1 = 0; const bool killed = pid1 > 0 && kill(pid1, SIGKILL) == 0 && waitpid(pid1, &ws1, 0) == pid1 && WIFSIGNALED(ws1) && WTERMSIG(ws1) == SIGKILL;
        s.barrier(); s.barrier();
        const pid_t pid2 = spawn(2, st2); put_status(s.workdir + "/restart_go_2", "go");
        const std::string r2a = wait_status(st2, "exchanged", 70000);
        s.barrier();
        const std::string r2 = wait_status(st2, "done", 30000); int ws2 = 0;
        const bool exited = pid2 > 0 && waitpid(pid2, &ws2, 0) == pid2 && WIFEXITED(ws2) && WEXITSTATUS(ws2) == 0;
        s.barrier();
        auto field = [](const std::string& t, const std::string& k) { const auto i = t.find(k + "="); if (i == std::string::npos) return std::string();
            const auto j = t.find_first_of(" \n", i); return t.substr(i + k.size() + 1, j == std::string::npos ? std::string::npos : j - i - k.size() - 1); };
        const bool new_process = !field(r2, "pid").empty() && field(r1, "pid") != field(r2, "pid");
        const bool new_participant = !field(r2, "prefix").empty() && field(r1, "prefix") != field(r2, "prefix");
        okay = r1.rfind("exchanged", 0) == 0 && killed && r2a.rfind("exchanged", 0) == 0 && r2.rfind("done", 0) == 0 && field(r2, "closed") == "1" && exited && new_process && new_participant;
        detail = "role=RESTARTED_PROCESS killed_SIGKILL=" + std::to_string(killed) + " inc2_exit0=" + std::to_string(exited) + " new_pid=" + std::to_string(new_process) +
            " new_guid_prefix=" + std::to_string(new_participant) + " inc1_status=" + std::string(r1.empty() ? "-" : r1.substr(0, r1.find(' ')));
    }
    s.report(G, "peer_process_restart", okay, detail);
    s.barrier();
}

// Discovery Server: side 0 SERVER, side 1 CLIENT reaching it only through the server list (no initial peers).
static void discovery_server_case(Suite& s)
{
    const char* G = "feature";
    fd::DomainParticipantQos q4 = s.named("_ds"); auto c4 = s.config; c4.bind_declared_domain_at_open = true;
    q4.transport().user_transports = {s.descriptor(c4)}; q4.wire_protocol().builtin.initialPeersList.clear();
    auto& dc = q4.wire_protocol().builtin.discovery_config;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    static constexpr char server_prefix[] = "44.53.01.5f.46.44.44.53.44.53.42.31";
    if (s.me == 0) { dc.discoveryProtocol = rtps::DiscoveryProtocol_t::SERVER; std::istringstream in(server_prefix); in >> q4.wire_protocol().prefix; }
    else { dc.discoveryProtocol = rtps::DiscoveryProtocol_t::CLIENT; rtps::RemoteServerAttributes srv; need(srv.ReadguidPrefix(server_prefix), "server prefix");
        srv.metatrafficUnicastLocatorList.push_back(s.config.directory.routes.at(0).key.remote_locator); dc.m_DiscoveryServers.push_back(srv); }
    const auto server_kind = rtps::DiscoveryProtocol_t::SERVER, client_kind = rtps::DiscoveryProtocol_t::CLIENT;
#else
    // Fast DDS 3.x: servers are addressed by locator only; no GUID prefix is required.
    if (s.me == 0) dc.discoveryProtocol = rtps::DiscoveryProtocol::SERVER;
    else { dc.discoveryProtocol = rtps::DiscoveryProtocol::CLIENT; dc.m_DiscoveryServers.push_back(s.config.directory.routes.at(0).key.remote_locator); }
    const auto server_kind = rtps::DiscoveryProtocol::SERVER, client_kind = rtps::DiscoveryProtocol::CLIENT;
#endif
    s.barrier();
    fd::DomainParticipant* p4 = nullptr;
    if (s.me == 0) p4 = s.extra_participant(q4);
    s.barrier();
    if (s.me == 1) p4 = s.extra_participant(q4);
    need(ok(s.layer_type.register_type(p4)), "ds type");
    const auto eff = p4->get_qos().wire_protocol().builtin.discovery_config.discoveryProtocol;
    const bool role_ok = s.me == 0 ? eff == server_kind : eff == client_kind;
    auto* pub4 = p4->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub4 = p4->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub4 && sub4, "ds pub/sub");
    fd::Topic *tx, *rx; s.topics(p4, "ds", s.layer_type.get_type_name(), tx, rx);
    auto* w4 = pub4->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r4 = sub4->create_datareader(rx, Suite::keep_all(s.rq)); need(w4 && r4, "ds endpoints");
    const bool m = s.matched(w4, r4, 1, 1, 30000); s.barrier();
    for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 34, n); need(write(w4, &v), "ds write"); }
    std::uint32_t next = 1; const bool got = m && s.waitp([&] { s.take_layers(r4, s.run + 34, next); return next == 4; }, 10000);
    const bool acked = ok(w4->wait_for_acknowledgments(seconds(5)));
    s.report(G, "discovery_server", got && acked && role_ok, std::string("role=") + (s.me == 0 ? "SERVER" : "CLIENT") + " initial_peers=0 effective_role=" +
        std::to_string(role_ok) + " matched=" + std::to_string(m) + " received=" + std::to_string(next - 1));
    s.barrier();
    if (s.me == 1) s.drop(p4); // the client goes first
    s.barrier();
    if (s.me == 0) s.drop(p4);
    s.barrier();
}

// ---------------------------------------------------------------- Security
#if HAVE_SECURITY
struct AuthSpy final : fd::DomainParticipantListener {
    std::atomic<int> authorized{0}, unauthorized{0};
    void onParticipantAuthentication(fd::DomainParticipant*, rtps::ParticipantAuthenticationInfo&& info) override
    { if (info.status == rtps::ParticipantAuthenticationInfo::AUTHORIZED_PARTICIPANT) ++authorized; else ++unauthorized; }
};
static void security_cases(Suite& s)
{
    const char* G = "security";
    const std::string pki = s.workdir + "/pki"; (void)mkdir(pki.c_str(), 0700);
    for (const auto& f : stage1_pki::kFiles) { FILE* o = std::fopen((pki + "/" + f.first).c_str(), "w"); need(o != nullptr, "pki file"); std::fputs(f.second, o); std::fputs("\n", o); std::fclose(o); }
    auto secure = [&](const std::string& suffix, const std::string& ca, const std::string& id) {
        fd::DomainParticipantQos q9 = s.named(suffix); auto& pr = q9.properties().properties();
        pr.emplace_back("dds.sec.auth.plugin", "builtin.PKI-DH");
        pr.emplace_back("dds.sec.auth.builtin.PKI-DH.identity_ca", "file://" + pki + "/" + ca);
        pr.emplace_back("dds.sec.auth.builtin.PKI-DH.identity_certificate", "file://" + pki + "/" + id + ".pem");
        pr.emplace_back("dds.sec.auth.builtin.PKI-DH.private_key", "file://" + pki + "/" + id + "_key.pem");
        pr.emplace_back("dds.sec.access.plugin", "builtin.Access-Permissions");
        pr.emplace_back("dds.sec.access.builtin.Access-Permissions.permissions_ca", "file://" + pki + "/ca.pem");
        pr.emplace_back("dds.sec.access.builtin.Access-Permissions.governance", "file://" + pki + "/governance.p7s");
        pr.emplace_back("dds.sec.access.builtin.Access-Permissions.permissions", "file://" + pki + "/permissions" + std::to_string(s.me) + ".p7s");
        pr.emplace_back("dds.sec.crypto.plugin", "builtin.AES-GCM-GMAC");
        s.with_descriptor(q9); return q9; };
    {   // PKI-DH authentication, Access-Permissions and AES-GCM-GMAC with everything ENCRYPT.
        AuthSpy spy; auto* p9 = s.extra_participant(secure("_sec", "ca.pem", "id" + std::to_string(s.me)), &spy);
        need(ok(s.layer_type.register_type(p9)), "sec type"); fd::TypeSupport fts{new BlobType("FastDDSDSoftBusFrag", kFragBytes)}; need(ok(fts.register_type(p9)), "sec frag type");
        auto* pub9 = p9->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub9 = p9->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub9 && sub9, "sec pub/sub");
        fd::Topic *tx, *rx, *ftx, *frx; s.topics(p9, "sec", s.layer_type.get_type_name(), tx, rx); s.topics(p9, "sec_frag", fts.get_type_name(), ftx, frx);
        auto* w9 = pub9->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r9 = sub9->create_datareader(rx, Suite::keep_all(s.rq));
        auto* fw9 = pub9->create_datawriter(ftx, Suite::keep_all(s.wq)); auto* fr9 = sub9->create_datareader(frx, Suite::keep_all(s.rq)); need(w9 && r9 && fw9 && fr9, "sec endpoints");
        auto* dt9 = p9->create_topic(s.tname("f_sec_denied", s.me), s.layer_type.get_type_name(), fd::TOPIC_QOS_DEFAULT); need(dt9, "denied topic");
        const bool denied = pub9->create_datawriter(dt9, Suite::keep_all(s.wq)) == nullptr;
        const bool m = s.matched(w9, r9, 1, 1, 40000) && s.matched(fw9, fr9, 1, 1, 40000); s.barrier();
        for (std::uint32_t n = 1; n <= 3; ++n) { auto v = s.lay(s.run + 70, n); need(write(w9, &v), "sec write"); }
        { auto v = blob(s.run + 71, s.me, kFragBytes); need(write(fw9, &v), "sec frag write"); }
        std::uint32_t next = 1; bool frag = false;
        const bool got = m && s.waitp([&] { s.take_layers(r9, s.run + 70, next);
            for (;;) { Blob v; fd::SampleInfo i; if (!ok(fr9->take_next_sample(&v, &i))) break; if (i.valid_data && blob_valid(v, s.run + 71, s.peer, kFragBytes)) frag = true; }
            return next == 4 && frag; }, 20000);
        const bool acked = ok(w9->wait_for_acknowledgments(seconds(5))) && ok(fw9->wait_for_acknowledgments(seconds(5)));
        s.report(G, "security_pki_dh_access_aes_gcm", got && acked && denied && spy.authorized.load() >= 1, "authorized=" + std::to_string(spy.authorized.load()) +
            " received=" + std::to_string(next - 1) + " fragmented_150000B=" + std::to_string(frag) + " deny_rule_blocks_writer=" + std::to_string(denied) +
            " protection=rtps,discovery,liveliness,metadata,data:ENCRYPT");
        s.barrier(); s.drop(p9); s.barrier();
    }
    {   // Side 1 presents an identity from an untrusted CA: authentication fails on both sides.
        AuthSpy spy; const bool rogue = s.me == 1;
        auto* p10 = s.extra_participant(secure("_sec2", rogue ? "rogue_ca.pem" : "ca.pem", rogue ? "rogue1" : "id0"), &spy);
        need(ok(s.layer_type.register_type(p10)), "rogue type");
        auto* pub10 = p10->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub10 = p10->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pub10 && sub10, "rogue pub/sub");
        fd::Topic *tx, *rx; s.topics(p10, "sec_rogue", s.layer_type.get_type_name(), tx, rx);
        auto* w10 = pub10->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r10 = sub10->create_datareader(rx, Suite::keep_all(s.rq)); need(w10 && r10, "rogue endpoints");
        s.barrier();
        const bool rejected = s.waitp([&] { return spy.unauthorized.load() >= 1; }, 30000); std::this_thread::sleep_for(std::chrono::seconds(3));
        fd::PublicationMatchedStatus a; fd::SubscriptionMatchedStatus b; need(ok(w10->get_publication_matched_status(a)) && ok(r10->get_subscription_matched_status(b)), "rogue status");
        s.report(G, "security_rejects_untrusted_identity", rejected && a.total_count == 0 && b.total_count == 0 && spy.authorized.load() == 0,
            std::string("identity=") + (rogue ? "rogue_ca" : "trusted_ca") + " unauthorized_events=" + std::to_string(spy.unauthorized.load()) +
            " matched_ever=" + std::to_string(a.total_count + b.total_count));
        s.barrier(); s.drop(p10); s.barrier();
    }
}
#endif

// ---------------------------------------------------------------- Statistics
#if defined(FASTDDS_STATISTICS)
static void statistics_case(Suite& s)
{
    const char* G = "statistics";
    namespace st = eprosima::fastdds::statistics;
    fd::DomainParticipantQos q11 = s.named("_stat"); s.with_descriptor(q11);
    auto* p11 = s.extra_participant(q11); auto* sp = st::dds::DomainParticipant::narrow(p11); need(sp != nullptr, "statistics narrow");
    const char* topics[] = {st::DATA_COUNT_TOPIC, st::RTPS_SENT_TOPIC, st::DISCOVERY_TOPIC, st::PHYSICAL_DATA_TOPIC, st::PUBLICATION_THROUGHPUT_TOPIC};
    for (const char* t : topics) need(ok(sp->enable_statistics_datawriter(t, st::dds::STATISTICS_DATAWRITER_QOS)), "enable statistics writer");
    const bool monitor = ok(sp->enable_monitor_service());
    const auto mine = p11->guid().guidPrefix;
    auto foreign = [&](const st::detail::GUID_s& g) { return std::memcmp(g.guidPrefix().value().data(), mine.value, 12) != 0; };
    fd::TypeSupport t_count(new st::EntityCountPubSubType), t_traffic(new st::Entity2LocatorTrafficPubSubType), t_disc(new st::DiscoveryTimePubSubType),
        t_phys(new st::PhysicalDataPubSubType), t_mon(new st::MonitorServiceStatusDataPubSubType);
    for (auto* t : {&t_count, &t_traffic, &t_disc, &t_phys, &t_mon}) need(ok(t->register_type(p11)), "statistics type");
    auto* sub11 = p11->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); auto* pub11 = p11->create_publisher(fd::PUBLISHER_QOS_DEFAULT); need(sub11 && pub11, "statistics pub/sub");
    auto reader = [&](const char* topic, fd::TypeSupport& t) { fd::Topic* tp = nullptr;
        if (auto* found = p11->find_topic(topic, seconds(0))) tp = found; else tp = p11->create_topic(topic, t.get_type_name(), fd::TOPIC_QOS_DEFAULT);
        need(tp != nullptr, "statistics topic"); fd::DataReaderQos rq2 = st::dds::STATISTICS_DATAREADER_QOS; rq2.data_sharing().off();
        auto* r = sub11->create_datareader(tp, rq2); need(r != nullptr, "statistics reader"); return r; };
    auto* r_count = reader(st::DATA_COUNT_TOPIC, t_count); auto* r_traffic = reader(st::RTPS_SENT_TOPIC, t_traffic);
    auto* r_disc = reader(st::DISCOVERY_TOPIC, t_disc); auto* r_phys = reader(st::PHYSICAL_DATA_TOPIC, t_phys); auto* r_mon = reader(st::MONITOR_SERVICE_TOPIC, t_mon);
    need(ok(s.layer_type.register_type(p11)), "stat layer type");
    fd::Topic *tx, *rx; s.topics(p11, "stat", s.layer_type.get_type_name(), tx, rx);
    auto* w11 = pub11->create_datawriter(tx, Suite::keep_all(s.wq)); auto* r11 = sub11->create_datareader(rx, Suite::keep_all(s.rq)); need(w11 && r11, "stat endpoints");
    const bool m = s.matched(w11, r11, 1, 1, 30000); s.barrier();
    for (std::uint32_t n = 1; n <= 20; ++n) { auto v = s.lay(s.run + 72, n); need(write(w11, &v), "stat write"); }
    const auto dsb_kind = s.config.default_user_unicast.at(0).kind;
    std::uint32_t app = 0, count = 0, traffic = 0, traffic_dsb = 0, disc = 0, phys = 0, mon = 0;
    (void)s.waitp([&] {
        for (;;) { Layer x; fd::SampleInfo i; if (!ok(r11->take_next_sample(&x, &i))) break; if (i.valid_data && x.run == s.run + 72 && x.side == s.peer) ++app; }
        for (;;) { st::EntityCount v; fd::SampleInfo i; if (!ok(r_count->take_next_sample(&v, &i))) break; if (i.valid_data && foreign(v.guid())) ++count; }
        for (;;) { st::Entity2LocatorTraffic v; fd::SampleInfo i; if (!ok(r_traffic->take_next_sample(&v, &i))) break;
            if (i.valid_data && foreign(v.src_guid())) { ++traffic; if (v.dst_locator().kind() == dsb_kind) ++traffic_dsb; } }
        for (;;) { st::DiscoveryTime v; fd::SampleInfo i; if (!ok(r_disc->take_next_sample(&v, &i))) break; if (i.valid_data && foreign(v.local_participant_guid())) ++disc; }
        for (;;) { st::PhysicalData v; fd::SampleInfo i; if (!ok(r_phys->take_next_sample(&v, &i))) break; if (i.valid_data && foreign(v.participant_guid())) ++phys; }
        for (;;) { st::MonitorServiceStatusData v; fd::SampleInfo i; if (!ok(r_mon->take_next_sample(&v, &i))) break; if (i.valid_data && foreign(v.local_entity())) ++mon; }
        return app == 20 && count > 0 && traffic_dsb > 0 && disc > 0 && phys > 0 && mon > 0; }, 30000);
    s.report(G, "statistics_and_monitor_service", m && app == 20 && count > 0 && traffic_dsb > 0 && disc > 0 && phys > 0 && monitor && mon > 0,
        "app_received=" + std::to_string(app) + " peer_data_count=" + std::to_string(count) + " peer_rtps_sent=" + std::to_string(traffic) +
        " dst_locator_kind_dsoftbus=" + std::to_string(traffic_dsb) + " peer_discovery=" + std::to_string(disc) + " peer_physical_data=" + std::to_string(phys) +
        " monitor_service=" + std::to_string(monitor) + " peer_monitor_status=" + std::to_string(mon));
    s.barrier(); s.drop(p11); s.barrier();
}
#endif

// ---------------------------------------------------------------- content-filter race stress over DSoftBus
static void cft_stress(Suite& s, bool wsf)
{
    fd::DomainParticipantQos qs = s.named("_race"); s.with_descriptor(qs);
    auto* ps = s.extra_participant(qs);
    auto* pubs = ps->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* subs = ps->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(pubs && subs, "race pub/sub");
    DynRecord rec("FastDDSDSoftBusCftRace", false); fd::TypeSupport ct = rec.support(true); need(ok(ct.register_type(ps)), "race type");
    auto* topic = ps->create_topic("dsb_cft_race_" + s.run_hex, ct.get_type_name(), fd::TOPIC_QOS_DEFAULT); need(topic, "race topic");
    std::atomic<bool> stop{false}; std::atomic<unsigned long> writes{0}, iters{0}; std::thread wt; fd::DataWriter* cw = nullptr;
    s.barrier();
    const auto end = Clock::now() + std::chrono::seconds(30);
    if (s.me == 0) {
        fd::DataWriterQos cq = s.wq; if (!wsf) cq.writer_resource_limits().reader_filters_allocation.maximum = 0;
        cw = pubs->create_datawriter(topic, cq); need(cw, "race writer");
        wt = std::thread([&] { auto d = rec.create(); unsigned n = 0;
            while (!stop) { DynRecord::set(d, 0, ++n % 20); (void)write(cw, DynRecord::ptr(d)); ++writes; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            DynRecord::release(d); });
        while (Clock::now() < end) { auto* cft = ps->create_contentfilteredtopic("local_race_" + std::to_string(iters.load()), topic, "n > %0", {"5"});
            if (cft) { auto* r = subs->create_datareader(cft, s.rq); cft->set_expression_parameters({"6"}); if (r) subs->delete_datareader(r); ps->delete_contentfilteredtopic(cft); } ++iters; }
        stop = true; wt.join();
    } else {
        while (Clock::now() < end) { std::vector<std::pair<fd::ContentFilteredTopic*, fd::DataReader*>> v;
            for (int k = 0; k < 6; ++k) { auto* cft = ps->create_contentfilteredtopic("remote_race_" + std::to_string(iters.load()) + "_" + std::to_string(k), topic, "n > %0", {std::to_string(k)});
                v.push_back({cft, cft ? subs->create_datareader(cft, s.rq) : nullptr}); }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            for (auto& e : v) { if (e.second) subs->delete_datareader(e.second); if (e.first) ps->delete_contentfilteredtopic(e.first); }
            ++iters; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }
    int mt = -1; if (cw) { fd::PublicationMatchedStatus st; need(ok(cw->get_publication_matched_status(st)), "race status"); mt = st.total_count; }
    s.report("stress", "cft_race_stress_dsoftbus", true, std::string("writer_side_filtering=") + (wsf ? "1" : "0") + " role=" + (s.me == 0 ? "A_writer_local_churn" : "B_remote_filtered_readers") +
        " seconds=30 writes=" + std::to_string(writes.load()) + " iterations=" + std::to_string(iters.load()) + " writer_matched_total=" + std::to_string(mt) + " survived=1");
    s.barrier(); s.drop(ps); s.barrier();
}

// ---------------------------------------------------------------- entry points
static int node_info(const std::map<std::string, std::string>& a)
{
    set_token(a.count("token") ? a.at("token") : "");
    NodeBasicInfo info{}; need(GetLocalNodeDeviceInfo(kPackage, &info) == 0, "GetLocalNodeDeviceInfo");
    char udid[UDID_BUF_LEN] = {};
    need(GetNodeKeyInfo(kPackage, info.networkId, NODE_KEY_UDID, reinterpret_cast<std::uint8_t*>(udid), UDID_BUF_LEN) == 0, "GetNodeKeyInfo");
    std::printf("DSB_NODE network=%s udid=%s name=%s\n", info.networkId, udid, info.deviceName); return 0;
}
static bool group_on(const std::string& groups, const char* g) { return groups == "all" || ("," + groups + ",").find(std::string(",") + g + ",") != std::string::npos; }
static int run_suite(unsigned side, const std::string& workdir, const std::map<std::string, std::string>& a)
{
    auto arg = [&](const char* k, const char* d) { auto it = a.find(k); return it == a.end() ? std::string(d) : it->second; };
    set_token(arg("token", ""));
    Suite s; s.me = side; s.peer = 1 - side; s.workdir = workdir; s.run_hex = arg("run", "0123456789abcdef");
    need(s.run_hex.size() == 16, "run must be 16 hex digits");
    s.run = std::strtoull(s.run_hex.substr(0, 12).c_str(), nullptr, 16) & 0xffffffffffull;
    s.q = make_deploy(side, s.run_hex, static_cast<std::uint32_t>(std::stoul(arg("domain", "151"))), arg("local_udid", ""), arg("peer_udid", ""), arg("peer_network", ""));
    need(!s.q.udid.empty() && !s.q.peer_udid.empty() && !s.q.peer_network.empty(), "local_udid/peer_udid/peer_network required");
    const std::string groups = arg("groups", "all"); const unsigned route = static_cast<unsigned>(std::stoul(arg("route", "0"))); const std::string stress = arg("stress", "");
    std::printf("DSB_START side=%u fastdds=%s run=%s domain=%u groups=%s route=%u stress=%s\n", side, DSB_FASTDDS_LINE, s.run_hex.c_str(), s.q.domain, groups.c_str(), route, stress.c_str());
    std::fflush(stdout);
    s.config = configuration(s.q, true);
    s.main.rt = std::make_unique<ds::experimental::Runtime>(backend(s.config), ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
    // The first dial waits until the host has seen both listeners (DSB_LISTENING) and created workdir/go.
    need(s.main.rt->SetPreConnectGate([&] { std::printf("DSB_LISTENING side=%u\n", side); std::fflush(stdout); return wait_file(workdir + "/go", 180); }), "gate");
    s.factory = fd::DomainParticipantFactory::get_instance();
    static constexpr char xml[] = "<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
    need(ok(s.factory->load_XML_profiles_string(xml, sizeof(xml) - 1)), "library settings");
    s.qos.name(("dsb_" + s.run_hex + "_" + std::to_string(side)).c_str());
    participant_locators(s.qos, s.config, s.descriptor(s.config));
    s.main.start();
    bool aborted = false; std::string why;
    try {
        s.participant = s.factory->create_participant(s.q.domain, s.qos); need(s.participant != nullptr, "create participant");
        need(ok(s.layer_type.register_type(s.participant)), "register layer type");
        s.pub = s.participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT); s.sub = s.participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT); need(s.pub && s.sub, "pub/sub");
        s.wq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS; s.rq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        s.wq.durability().kind = fd::VOLATILE_DURABILITY_QOS; s.rq.durability().kind = fd::VOLATILE_DURABILITY_QOS;
        s.wq.history().depth = 8; s.rq.history().depth = 8; s.wq.data_sharing().off(); s.rq.data_sharing().off();
        fd::Topic *stx, *srx; s.topics(s.participant, "sync", s.layer_type.get_type_name(), stx, srx);
        fd::DataWriterQos swq = Suite::keep_all(s.wq); swq.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        fd::DataReaderQos srq = Suite::keep_all(s.rq); srq.durability().kind = fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        s.sync_w = s.pub->create_datawriter(stx, swq); s.sync_r = s.sub->create_datareader(srx, srq); need(s.sync_w && s.sync_r, "sync endpoints");
        s.barrier();
        std::atomic<bool> route_marker{false};
        if (stress.empty()) {
            if (group_on(groups, "core")) core_cases(s, route_marker, route);
            if (group_on(groups, "qos")) qos_cases(s);
            if (group_on(groups, "feature")) {
                participants_cases(s); s.barrier(); dds_api_cases(s); cft_case(s); typelookup_case(s);
                second_domain_case(s); restart_case(s); discovery_server_case(s);
            }
#if HAVE_SECURITY
            if (group_on(groups, "security")) security_cases(s);
#else
            if (group_on(groups, "security")) s.note("security", "dds_security", "NOT_APPLICABLE", "fastdds_built_without_SECURITY");
#endif
#if defined(FASTDDS_STATISTICS)
            if (group_on(groups, "statistics")) statistics_case(s);
#else
            if (group_on(groups, "statistics")) s.note("statistics", "statistics_module", "NOT_APPLICABLE", "fastdds_built_without_FASTDDS_STATISTICS");
#endif
            if (group_on(groups, "feature")) {
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
                for (const char* t : {"time_based_filter", "destination_order_by_source_timestamp", "presentation_coherent_ordered_access", "latency_budget", "durability_service"})
                    s.note("feature", t, "NOT_APPLICABLE", "not_implemented_in_fastdds_2_14");
#else
                for (const char* t : {"time_based_filter", "destination_order_by_source_timestamp", "presentation_coherent_ordered_access", "latency_budget", "durability_service"})
                    s.note("feature", t, "NOT_APPLICABLE", "not_implemented_in_fastdds_3");
#endif
                for (const char* t : {"data_sharing", "shared_memory_transport", "zero_copy_loans", "intraprocess_delivery"})
                    s.note("feature", t, "NOT_APPLICABLE", "same_host_only_not_a_network_transport_path");
            }
        } else {
            cft_stress(s, stress != "wsf0");
        }
        s.barrier();
    } catch (const std::exception& e) { aborted = true; why = e.what(); ++s.failed; }
    if (aborted) { std::printf("DSB_ABORT side=%u reason=%s\n", side, why.c_str()); std::fflush(stdout); }
    // From here the peer may already be tearing down: its closed sockets make our heartbeats fail.
    s.main.tolerate = true;
    bool deleted = true;
    for (auto it = s.extra.rbegin(); it != s.extra.rend(); ++it) if (*it) { try { deleted = ok((*it)->delete_contained_entities()) && ok(s.factory->delete_participant(*it)) && deleted; } catch (...) { deleted = false; } }
    if (s.participant) { try { deleted = ok(s.participant->delete_contained_entities()) && ok(s.factory->delete_participant(s.participant)) && deleted; } catch (...) { deleted = false; } }
    const bool closed = deleted && s.main.stop_and_close(s.descs);
    std::printf("DSB_SUMMARY side=%u fastdds=%s passed=%u failed=%u aborted=%d deleted=%d runtime_closed=%d\n", side, DSB_FASTDDS_LINE, s.passed, s.failed, int(aborted), int(deleted), int(closed));
    std::fflush(stdout);
    std::_Exit(s.failed == 0 && closed ? 0 : 1);
}
int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "node") return node_info(kv(argc, argv, 2));
        if (mode == "restart-helper" && argc > 2) return restart_helper(argv[2]);
        if (mode == "run" && argc > 3) return run_suite(static_cast<unsigned>(std::stoul(argv[2])) & 1u, argv[3], kv(argc, argv, 4));
    } catch (const std::exception& e) { std::printf("DSB_ERROR %s\n", e.what()); return 2; }
    std::fprintf(stderr, "usage: dsb_suite node token=N | run <0|1> <workdir> key=value...\n"); return 2;
}
