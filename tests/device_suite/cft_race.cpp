// Content-filter race reproducer with the stock UDPv4/TCPv4 transports (no DSoftBus), for both
// Fast DDS lines. Board A: DataWriter (writer-side filtering) + local ContentFilteredTopic churn;
// board B: churn of remote filtered DataReaders (readers < 0: unfiltered control).
// usage: cft_race <A|B> <udp|tcp> <peer_ip> <seconds> <local_churn 0|1> <readers> [wsf 0|1]
#include "compat.hpp"
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.h>
#include <fastrtps/utils/IPLocator.h>
using eprosima::fastrtps::rtps::IPLocator;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPLocator.hpp>
using eprosima::fastdds::rtps::IPLocator;
#endif
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace dsb;
int main(int argc, char** argv)
{
    if (argc < 7) { std::fprintf(stderr, "usage: cft_race <A|B> <udp|tcp> <peer_ip> <seconds> <local_churn> <readers> [wsf]\n"); return 2; }
    const bool a = std::string(argv[1]) == "A"; const std::string kind = argv[2], peer = argv[3]; const int secs = std::atoi(argv[4]);
    const bool local_churn = std::atoi(argv[5]) != 0; const int readers = std::atoi(argv[6]); const bool wsf = argc < 8 || std::atoi(argv[7]) != 0;
    fd::DomainParticipantQos q; q.name(a ? "cft_race_A" : "cft_race_B"); q.transport().use_builtin_transports = false;
    rtps::Locator_t pl;
    if (kind == "udp") { q.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        pl.kind = LOCATOR_KIND_UDPv4; IPLocator::setIPv4(pl, peer); q.wire_protocol().builtin.initialPeersList.push_back(pl); }
    else { auto t = std::make_shared<eprosima::fastdds::rtps::TCPv4TransportDescriptor>(); t->add_listener_port(a ? 5100 : 5101); q.transport().user_transports.push_back(t);
        if (!a) { pl.kind = LOCATOR_KIND_TCPv4; IPLocator::setIPv4(pl, peer); IPLocator::setPhysicalPort(pl, 5100); IPLocator::setLogicalPort(pl, 7400 + 250 * 77 + 10);
            q.wire_protocol().builtin.initialPeersList.push_back(pl); } }
    auto* f = fd::DomainParticipantFactory::get_instance(); auto* p = f->create_participant(77, q); if (!p) { std::fprintf(stderr, "participant\n"); return 3; }
    DynRecord rec("CftRaceType", false); fd::TypeSupport ts = rec.support(true); ts.register_type(p);
    auto* topic = p->create_topic("cft_race_topic", ts.get_type_name(), fd::TOPIC_QOS_DEFAULT);
    auto* pub = p->create_publisher(fd::PUBLISHER_QOS_DEFAULT); auto* sub = p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
    fd::DataReaderQos rq; rq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS; rq.data_sharing().off();
    fd::DataWriterQos wq; wq.reliability().kind = fd::RELIABLE_RELIABILITY_QOS; wq.data_sharing().off();
    if (!wsf) wq.writer_resource_limits().reader_filters_allocation.maximum = 0; // workaround: readers filter
    std::atomic<bool> stop{false}; std::atomic<unsigned long> writes{0}, local_iter{0}, remote_iter{0};
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(secs); std::thread writer; fd::DataWriter* w = nullptr;
    if (a) {
        w = pub->create_datawriter(topic, wq);
        writer = std::thread([&] { auto d = rec.create(); unsigned n = 0;
            while (!stop) { DynRecord::set(d, 0, ++n % 20); (void)write(w, DynRecord::ptr(d)); ++writes; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            DynRecord::release(d); });
        auto last = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < end) {
            if (local_churn) { auto* cft = p->create_contentfilteredtopic("local_cft_" + std::to_string(local_iter.load()), topic, "n > %0", {"5"});
                if (cft) { auto* r = sub->create_datareader(cft, rq); cft->set_expression_parameters({"6"}); if (r) sub->delete_datareader(r); p->delete_contentfilteredtopic(cft); }
                ++local_iter; }
            else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (std::chrono::steady_clock::now() - last > std::chrono::seconds(5)) { last = std::chrono::steady_clock::now(); fd::PublicationMatchedStatus ms; w->get_publication_matched_status(ms);
                std::printf("CFTRACE A writes=%lu local_iter=%lu matched_total=%d current=%d\n", writes.load(), local_iter.load(), ms.total_count, ms.current_count); std::fflush(stdout); }
        }
    } else {
        while (std::chrono::steady_clock::now() < end) {
            if (readers < 0) { std::vector<fd::DataReader*> plain; for (int k = 0; k < -readers; ++k) plain.push_back(sub->create_datareader(topic, rq));
                std::this_thread::sleep_for(std::chrono::milliseconds(30)); for (auto* r : plain) if (r) sub->delete_datareader(r); ++remote_iter;
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            std::vector<std::pair<fd::ContentFilteredTopic*, fd::DataReader*>> v;
            for (int k = 0; k < readers; ++k) { auto* cft = p->create_contentfilteredtopic("remote_cft_" + std::to_string(remote_iter.load()) + "_" + std::to_string(k), topic, "n > %0", {std::to_string(k)});
                v.push_back({cft, cft ? sub->create_datareader(cft, rq) : nullptr}); }
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            for (auto& e : v) { if (e.second) sub->delete_datareader(e.second); if (e.first) p->delete_contentfilteredtopic(e.first); }
            ++remote_iter; if (remote_iter % 100 == 0) { std::printf("CFTRACE B remote_iter=%lu\n", remote_iter.load()); std::fflush(stdout); }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    stop = true; if (writer.joinable()) writer.join();
    int mt = -1; if (w) { fd::PublicationMatchedStatus ms; w->get_publication_matched_status(ms); mt = ms.total_count; }
    std::printf("CFTRACE_DONE fastdds=%s role=%s transport=%s local_churn=%d wsf=%d writes=%lu local_iter=%lu remote_iter=%lu writer_matched_total=%d\n",
        DSB_FASTDDS_LINE, argv[1], kind.c_str(), int(local_churn), int(wsf), writes.load(), local_iter.load(), remote_iter.load(), mt);
    std::fflush(stdout); std::_Exit(0);
}
