#include "PortableFixture.hpp"
#include "../DualParticipantPolicy.hpp"
#include "../DualValidation.hpp"
#include "../ListenerPolicy.hpp"
#include "KeyedValidator.hpp"
#include "DSoftBusKeyedSamplePubSubTypes.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/core/LoanableSequence.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
namespace fastdds_dsoftbus::experiment::keyed
{
namespace dds = eprosima::fastdds::dds;
namespace rtps = eprosima::fastdds::rtps;
using Clock = std::chrono::steady_clock;
namespace
{
std::string Hex(const std::uint8_t *p, std::size_t n)
{
    static constexpr char d[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i)
    {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}
std::string Guid(const rtps::GUID_t &g)
{
    const auto b = DualBytes(g);
    return Hex(b.data(), b.size());
}
std::string Handle(const dds::InstanceHandle_t &h) { return Hex(h.value, 16); }
std::string Join(const std::set<std::string> &s)
{
    std::string x;
    for (const auto &a : s)
    {
        if (!x.empty())
            x += ',';
        x += a;
    }
    return x.empty() ? "none" : x;
}
std::string Hash(const std::array<std::uint8_t, 32> &b)
{
    std::uint64_t h = 14695981039346656037ull;
    for (auto c : b)
    {
        h ^= c;
        h *= 1099511628211ull;
    }
    std::ostringstream o;
    o << std::hex << std::setfill('0') << std::setw(16) << h;
    return o.str();
}
std::string RunHex(std::uint64_t x)
{
    std::ostringstream o;
    o << std::hex << std::setw(16) << std::setfill('0') << x;
    return o.str();
}
int Pid()
{
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}
bool SafeRun(const std::string &s)
{
    return s.size() == 16 && std::all_of(s.begin(), s.end(), [](char c)
                                         { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
const std::set<std::string> cases{"K1", "K2", "K3", "K4_TRUE", "K4_FALSE", "H1",
                                  "H2", "H3", "R1", "R2",      "R3"};
} // namespace
struct KeyedFixture::Impl
{
    struct WriterListener : dds::DataWriterListener
    {
        Impl &o;
        unsigned id;
        WriterListener(Impl &owner, unsigned n) : o(owner), id(n) {}
        void on_publication_matched(dds::DataWriter *, const dds::PublicationMatchedStatus &s) override
        {
            o.Log("MATCH_EVENT", "endpoint=W" + std::to_string(id) + " guid=" + o.Expected(true, id) +
                                     " peer_guid=" + Handle(s.last_subscription_handle) +
                                     " current=" + std::to_string(s.current_count) +
                                     " change=" + std::to_string(s.current_count_change));
        }
    };
    struct ReaderListener : dds::DataReaderListener
    {
        Impl &o;
        explicit ReaderListener(Impl &owner) : o(owner) {}
        void on_subscription_matched(dds::DataReader *, const dds::SubscriptionMatchedStatus &s) override
        {
            o.Log("MATCH_EVENT", "endpoint=R1 guid=" + o.Expected(false, 11) +
                                     " peer_guid=" + Handle(s.last_publication_handle) +
                                     " current=" + std::to_string(s.current_count) +
                                     " change=" + std::to_string(s.current_count_change));
        }
    };
    struct DiscoveryListener : dds::DomainParticipantListener
    {
        Impl &o;
        explicit DiscoveryListener(Impl &owner) : o(owner) {}
        void on_data_writer_discovery(dds::DomainParticipant *, rtps::WriterDiscoveryStatus s,
                                      const dds::PublicationBuiltinTopicData &d, bool &ignore) override
        {
            ignore = false;
            o.Discovered("writer", s == rtps::WriterDiscoveryStatus::REMOVED_WRITER, d);
        }
        void on_data_reader_discovery(dds::DomainParticipant *, rtps::ReaderDiscoveryStatus s,
                                      const dds::SubscriptionBuiltinTopicData &d, bool &ignore) override
        {
            ignore = false;
            o.Discovered("reader", s == rtps::ReaderDiscoveryStatus::REMOVED_READER, d);
        }
    };
    struct Writer
    {
        dds::DataWriter *ptr = nullptr;
        std::unique_ptr<WriterListener> listener;
        std::map<unsigned, dds::InstanceHandle_t> handles;
        std::set<std::pair<unsigned, unsigned>> written;
    };
    Options opts;
    dds::TypeSupport type{new DSoftBusKeyedSamplePubSubType()};
    dds::DomainParticipant *participant = nullptr;
    dds::Publisher *publisher = nullptr;
    dds::Subscriber *subscriber = nullptr;
    dds::Topic *topic = nullptr;
    dds::DataReader *reader = nullptr;
    std::map<unsigned, Writer> writers;
    std::unique_ptr<ReaderListener> rl;
    std::unique_ptr<DiscoveryListener> dl;
    std::shared_ptr<Control> control;
    std::unique_ptr<KeyedValidator> ledger;
    mutable std::mutex log_mutex, failure_mutex, discovery_mutex;
    std::string failure, cleanup_failure, phase = "INITIAL";
    std::set<std::string> discovered;
    std::uint64_t order = 0, operation = 0, run_value = 0;
    unsigned next_release = 0, loans = 0;
    bool reference = false, opened = false, closed = false;
    std::atomic<bool> cleaning{false};
    std::map<unsigned, unsigned> state;
    std::map<unsigned, std::pair<int, int>> generations;
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
    struct EvidenceFailure
    {
    };
    enum class EvidenceFault
    {
        None,
        Format,
        Log,
        Payload
    };
    EvidenceFault evidence_fault = EvidenceFault::None;
    bool graph_mismatch = false;
    bool test_factory_preconfigured = false;
    unsigned real_loan_returns = 0;
    int last_loan_rc = -1;
    void InjectEvidence(EvidenceFault site)
    {
        if (evidence_fault == site)
        {
            evidence_fault = EvidenceFault::None;
            throw EvidenceFailure{};
        }
    }
#endif
    std::vector<std::string> phases{"PRE", "CHANGE", "POST", "OBSERVE", "CLEANUP"};
    explicit Impl(Options o) : opts(std::move(o))
    {
        if (!SafeRun(opts.run) || !cases.count(opts.case_id) || opts.control_directory.empty())
        {
            Fail("invalid_options");
            return;
        }
        run_value = std::stoull(opts.run, nullptr, 16);
        dl = std::make_unique<DiscoveryListener>(*this);
        rl = std::make_unique<ReaderListener>(*this);
        std::set<std::string> guids{Expected(true, 1)};
        if (opts.case_id == "K2")
            guids.insert(Expected(true, 2));
        ledger = std::make_unique<KeyedValidator>(guids);
        auto allowed = ExpectedSet(1, 4);
        if (Lifecycle() || opts.case_id == "R2")
            allowed = ExpectedSet(1, 2);
        if (opts.case_id == "R1")
            allowed = ExpectedSet(1, 1);
        if (opts.case_id == "R3")
            allowed = ExpectedSet(1, 2, {17, 34, 51});
        if (opts.case_id == "H1")
            allowed = ExpectedSet(3, 5);
        if (opts.case_id == "K2")
        {
            allowed = ExpectedSet(1, 3, {17});
            for (unsigned i = 1; i <= 3; ++i)
                allowed.emplace(Expected(true, 2), 17, i);
        }
        ledger->AllowOnly(std::move(allowed));
    }
    bool Lifecycle() const
    {
        return opts.case_id == "K3" || opts.case_id == "K4_TRUE" || opts.case_id == "K4_FALSE";
    }
    std::string Expected(bool writer, unsigned n) const
    {
        auto b = DualIdentity(opts.run, writer, 1, EndpointKind::Participant);
        b[12] = b[13] = 0;
        b[14] = static_cast<std::uint8_t>(n);
        b[15] = writer ? 2 : 7;
        return Hex(b.data(), 16);
    }
    std::string Topic() const { return "mdds-keyed-" + opts.run + "-T"; }
    void Log(const std::string &event, const std::string &fields = "")
    {
        std::lock_guard<std::mutex> l(log_mutex);
        const char *prefix = (event == "PHASE_DONE" || event == "CONTROL_CONSUMED" ||
                              event == "FIRST_FAILURE" || event == "CLEANUP_FAILURE")
                                 ? "MATRIX"
                                 : "KEYED";
        std::printf("%s event=%s run=%s case=%s side=%s pid=%d order=%llu %s\n", prefix, event.c_str(),
                    opts.run.c_str(), opts.case_id.c_str(), opts.board_a ? "A" : "B", Pid(),
                    static_cast<unsigned long long>(++order), fields.c_str());
        std::fflush(stdout);
    }
    bool Fail(const std::string &why)
    {
        bool first = false;
        {
            std::lock_guard<std::mutex> l(failure_mutex);
            if (failure.empty())
            {
                failure = why;
                first = true;
            }
        }
        if (first)
            Log("FIRST_FAILURE", "stage=" + phase + " predicate=" + why);
        return false;
    }
    bool CleanupFail(const std::string &why)
    {
        bool first = false;
        {
            std::lock_guard<std::mutex> l(failure_mutex);
            if (cleanup_failure.empty())
            {
                cleanup_failure = why;
                first = true;
            }
        }
        if (first)
            Log("CLEANUP_FAILURE", "predicate=" + why);
        return false;
    }
    bool Good() const
    {
        std::lock_guard<std::mutex> l(failure_mutex);
        return failure.empty();
    }
    template <class F> bool Wait(F f, const std::string &why)
    {
        const auto end = Clock::now() + std::chrono::seconds(20);
        while (Good() && Clock::now() < end)
        {
            if (f())
                return true;
            if (!Abort())
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return Fail(why);
    }
    bool Abort()
    {
        std::ifstream f(opts.control_directory + "/ABORT.release", std::ios::binary);
        if (!f)
            return Good();
        const std::string v((std::istreambuf_iterator<char>(f)), {});
        return Fail(v == "MATRIX_ABORT " + opts.run + " " + opts.case_id + "\n" ? "controller_abort"
                                                                                : "malformed_abort");
    }
    bool Release(const std::string &p)
    {
        if (next_release >= phases.size() || phases[next_release] != p)
            return Fail("release_internal_order");
        const bool ok = Wait(
            [&]
            {
                for (unsigned i = 0; i < phases.size(); ++i)
                {
                    std::ifstream f(opts.control_directory + "/" + phases[i] + ".release", std::ios::binary);
                    if (!f)
                        continue;
                    const std::string v((std::istreambuf_iterator<char>(f)), {});
                    if (i != next_release ||
                        v != "MATRIX_RELEASE " + opts.run + " " + opts.case_id + " " + phases[i] + "\n")
                    {
                        Fail("release_order_or_content");
                        return false;
                    }
                }
                const auto path = opts.control_directory + "/" + p;
                std::ifstream f(path + ".release");
                if (!f)
                    return false;
                f.close();
                std::ifstream consumed(path + ".consumed");
                if (consumed)
                {
                    Fail("duplicate_release");
                    return false;
                }
                if (std::rename((path + ".release").c_str(), (path + ".consumed").c_str()))
                {
                    Fail("consume_release");
                    return false;
                }
                ++next_release;
                Log("CONTROL_CONSUMED", "phase=" + p);
                return true;
            },
            "release_timeout_" + p);
        if (ok)
            phase = p;
        return ok;
    }
    bool Done()
    {
        Log("LOAN_ACCOUNTING", "phase=" + phase + " loans_outstanding=" + std::to_string(loans));
        if (loans)
            return Fail("loan_outstanding_at_phase_boundary");
        Log("PHASE_DONE", "phase=" + phase + (phase == "CLEANUP" ? " cleanup_scope=DDS" : ""));
        return Good();
    }
    template <class Q> void Qos(Q &q, bool writer, unsigned entity)
    {
        q.reliability().kind = dds::RELIABLE_RELIABILITY_QOS;
        q.reliability().max_blocking_time = {1, 0};
        q.durability().kind =
            opts.case_id == "H1" ? dds::TRANSIENT_LOCAL_DURABILITY_QOS : dds::VOLATILE_DURABILITY_QOS;
        q.history().kind =
            ((writer && opts.case_id == "H1") || (!writer && opts.case_id == "H2") || opts.case_id == "R1")
                ? dds::KEEP_LAST_HISTORY_QOS
                : dds::KEEP_ALL_HISTORY_QOS;
        q.history().depth = 2;
        auto &r = q.resource_limits();
        r.max_samples = 64;
        r.max_instances = 4;
        r.max_samples_per_instance = 16;
        r.allocated_samples = 64;
        if (opts.case_id == "H3")
        {
            r.max_samples = 16;
            r.max_instances = 4;
            r.max_samples_per_instance = 4;
            r.allocated_samples = 16;
        }
        if (opts.case_id == "R3")
        {
            r.max_samples = 12;
            r.max_instances = 3;
            r.max_samples_per_instance = 4;
            r.allocated_samples = 12;
        }
        if (opts.case_id == "R1")
        {
            r.max_samples = 8;
            r.max_instances = 2;
            r.max_samples_per_instance = 2;
            r.allocated_samples = 8;
        }
        if (opts.case_id == "R2" && writer)
        {
            r.max_samples = 8;
            r.max_instances = 2;
            r.max_samples_per_instance = 4;
            r.allocated_samples = 8;
        }
        q.ownership().kind = dds::SHARED_OWNERSHIP_QOS;
        q.data_sharing().off();
        q.deadline().period = dds::c_TimeInfinite;
        q.lifespan().duration = dds::c_TimeInfinite;
        q.liveliness().kind = dds::AUTOMATIC_LIVELINESS_QOS;
        q.liveliness().lease_duration = dds::c_TimeInfinite;
        q.endpoint().entity_id = entity;
    }
    template <class Q> bool QosValid(const Q &q, bool writer, unsigned entity)
    {
        Q expected;
        Qos(expected, writer, entity);
        return q.reliability() == expected.reliability() && q.durability() == expected.durability() &&
               q.history() == expected.history() && q.resource_limits() == expected.resource_limits() &&
               q.ownership() == expected.ownership() && q.data_sharing().kind() == dds::OFF &&
               q.endpoint().entity_id == entity;
    }
    template <class Q> void QosLog(const Q &q, const std::string &endpoint, bool valid)
    {
        const auto &r = q.resource_limits();
        Log("LOCAL_QOS",
            "phase=" + phase + " endpoint=" + endpoint + " reliability=" +
                std::to_string(q.reliability().kind) + " durability=" + std::to_string(q.durability().kind) +
                " history=" + std::to_string(q.history().kind) + " depth=" +
                std::to_string(q.history().depth) + " max_samples=" + std::to_string(r.max_samples) +
                " max_instances=" + std::to_string(r.max_instances) +
                " max_samples_per_instance=" + std::to_string(r.max_samples_per_instance) +
                " allocated_samples=" + std::to_string(r.allocated_samples) +
                " ownership=" + std::to_string(q.ownership().kind) +
                " data_sharing=" + std::to_string(q.data_sharing().kind()) +
                " max_blocking_seconds=" + std::to_string(q.reliability().max_blocking_time.seconds) +
                " valid=" + std::to_string(valid));
    }
    template <class T> bool Remote(const T &q) const
    {
        return std::string(q.topic_name.c_str()) == Topic() &&
               std::string(q.type_name.c_str()) == "DSoftBusKeyedSample" &&
               q.reliability.kind == dds::RELIABLE_RELIABILITY_QOS &&
               q.durability.kind == (opts.case_id == "H1" ? dds::TRANSIENT_LOCAL_DURABILITY_QOS
                                                          : dds::VOLATILE_DURABILITY_QOS) &&
               q.ownership.kind == dds::SHARED_OWNERSHIP_QOS && q.partition.names().empty();
    }
    template <class T> void Discovered(const std::string &kind, bool removed, const T &data)
    {
        const auto guid = Guid(data.guid);
        const auto local_prefix =
            DualGuid(DualIdentity(opts.run, opts.board_a, 1, EndpointKind::Participant)).guidPrefix;
        const bool local = data.guid.guidPrefix == local_prefix;
        const bool valid = Remote(data);
        {
            std::lock_guard<std::mutex> l(discovery_mutex);
            if (removed)
                discovered.erase(guid);
            else if (!local && valid)
                discovered.insert(guid);
        }
        Log("DISCOVERY", "kind=" + kind + " change=" + (removed ? "REMOVED" : "DISCOVERED_OR_UPDATED") +
                             " guid=" + guid + " local=" + std::to_string(local) +
                             " topic=" + data.topic_name.c_str() + " type=" + data.type_name.c_str() +
                             " valid=" + std::to_string(valid));
    }
    bool Initialize()
    {
        if (!Good() || opened)
            return Fail("open_precondition");
        auto *f = dds::DomainParticipantFactory::get_instance();
        eprosima::fastdds::LibrarySettings s;
        s.intraprocess_delivery = eprosima::fastdds::INTRAPROCESS_OFF;
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
        if (test_factory_preconfigured)
        {
            eprosima::fastdds::LibrarySettings actual;
            if (f->get_library_settings(actual) != dds::RETCODE_OK ||
                actual.intraprocess_delivery != eprosima::fastdds::INTRAPROCESS_OFF)
                return Fail("test_library_not_preconfigured");
        }
        else
#endif
            if (f->set_library_settings(s) != dds::RETCODE_OK)
            return Fail("library_intraprocess");
        Log("SCOPE", "scope=" + std::string(reference ? "HOST_REFERENCE_UDP" : "BYTES_NATIVE") +
                         " identity_grouping=FIXTURE_NOT_OS_PROOF "
                         "table_sha256=662c7e0ea79087117362ba4dcc3c538448b427fc3659d6f586a80c069c297a15");
        return true;
    }
    void ParticipantQos(dds::DomainParticipantQos &q)
    {
        q.name("keyed-" + opts.run + "-" + (opts.board_a ? "A" : "B"));
        ApplyDualParticipantPolicy(q);
        ApplyDualIdentityFixture(q, opts.run, opts.board_a, 1);
        q.transport().use_builtin_transports = false;
        q.transport().user_transports.clear();
        auto &w = q.wire_protocol();
        w.builtin.discovery_config.discoveryProtocol = rtps::DiscoveryProtocol::SIMPLE;
        w.builtin.discovery_config.leaseDuration_announcementperiod = {1, 0};
        w.builtin.discovery_config.initial_announcements.count = 10;
        w.builtin.discovery_config.initial_announcements.period = {0, 300000000};
        w.builtin.metatrafficMulticastLocatorList.clear();
        w.builtin.metatrafficUnicastLocatorList.clear();
        w.default_multicast_locator_list.clear();
        w.default_unicast_locator_list.clear();
        w.default_external_unicast_locators.clear();
        w.builtin.initialPeersList.clear();
        w.builtin.mutation_tries = 0;
    }
    bool CreateParticipant(const dds::DomainParticipantQos &q)
    {
        auto *f = dds::DomainParticipantFactory::get_instance();
        participant = f->create_participant(73, q, dl.get(), ParticipantListenerMask());
        if (!participant)
            return Fail("participant_create");
        const auto &a = participant->get_qos();
        eprosima::fastdds::LibrarySettings lib;
        const bool valid = DualBytes(participant->guid()) ==
                               DualIdentity(opts.run, opts.board_a, 1, EndpointKind::Participant) &&
                           !a.transport().use_builtin_transports &&
                           a.transport().user_transports == q.transport().user_transports &&
                           DualParticipantPolicyRequested(a) &&
                           f->get_library_settings(lib) == dds::RETCODE_OK &&
                           lib.intraprocess_delivery == eprosima::fastdds::INTRAPROCESS_OFF;
        unsigned native = 0, udp = 0;
        for (auto &d : a.transport().user_transports)
        {
            native += !!std::dynamic_pointer_cast<ExperimentalDescriptor>(d);
            udp += !!std::dynamic_pointer_cast<rtps::UDPv4TransportDescriptor>(d);
        }
        const auto loc = [](const rtps::LocatorList &xs)
        {
            std::string r;
            for (auto &x : xs)
            {
                if (!r.empty())
                    r += ',';
                r += std::to_string(x.kind) + ":" + Hex(x.address, 16) + ":" + std::to_string(x.port);
            }
            return r.empty() ? "none" : r;
        };
        Log("PARTICIPANT_QOS",
            "guid=" + Guid(participant->guid()) + " domain=" + std::to_string(participant->get_domain_id()) +
                " use_builtin=" + std::to_string(a.transport().use_builtin_transports) +
                " native_descriptor_count=" + std::to_string(native) + " udp_descriptor_count=" +
                std::to_string(udp) + " intraprocess=" + std::to_string(lib.intraprocess_delivery) +
                " default_locators=" + loc(a.wire_protocol().default_unicast_locator_list) +
                " metatraffic_locators=" + loc(a.wire_protocol().builtin.metatrafficUnicastLocatorList) +
                " initial_peer_locators=" + loc(a.wire_protocol().builtin.initialPeersList) +
                " multicast_count=" +
                std::to_string(a.wire_protocol().default_multicast_locator_list.size() +
                               a.wire_protocol().builtin.metatrafficMulticastLocatorList.size()) +
                " valid=" + std::to_string(valid));
        if (!valid || native != (reference ? 0u : 1u) || udp != (reference ? 1u : 0u))
            return Fail("participant_qos");
        if (control)
        {
            const auto s = control->Snapshot();
            Log("NATIVE_IDENTITY", "endpoint=" + std::string(opts.board_a ? "A1" : "B1") +
                                       " participant_guid=" + Guid(participant->guid()) +
                                       " runtime=" + std::to_string(s.runtime_id) +
                                       " native_instance=" + std::to_string(s.instance_id) +
                                       " application=" + std::to_string(s.application_incarnation) +
                                       " receiver=" + std::to_string(s.receiver_incarnation) +
                                       " token=" + std::to_string(s.logical_token));
        }
        if (type.register_type(participant) != dds::RETCODE_OK)
            return Fail("register_type");
        publisher = participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
        subscriber = participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        topic = participant->create_topic(Topic(), type.get_type_name(), dds::TOPIC_QOS_DEFAULT);
        if (!publisher || !subscriber || !topic)
            return Fail("entities_create");
        if (opts.board_a)
        {
            if (!CreateWriter(1))
                return false;
            if (opts.case_id == "K2" && !CreateWriter(2))
                return false;
        }
        else if (opts.case_id != "H1" && !CreateReader())
            return false;
        opened = true;
        return Good();
    }
    bool CreateWriter(unsigned id)
    {
        auto &w = writers[id];
        w.listener = std::make_unique<WriterListener>(*this, id);
        dds::DataWriterQos q;
        Qos(q, true, id);
        q.publish_mode().kind = dds::SYNCHRONOUS_PUBLISH_MODE;
        q.writer_data_lifecycle().autodispose_unregistered_instances = opts.case_id != "K4_FALSE";
        w.ptr = publisher->create_datawriter(topic, q, w.listener.get());
        if (!w.ptr)
            return Fail("writer_create");
        const auto &a = w.ptr->get_qos();
        const bool valid = QosValid(a, true, id) && Guid(w.ptr->guid()) == Expected(true, id) &&
                           a.publish_mode().kind == dds::SYNCHRONOUS_PUBLISH_MODE &&
                           a.writer_data_lifecycle().autodispose_unregistered_instances ==
                               q.writer_data_lifecycle().autodispose_unregistered_instances;
        Log("ENTITY_CREATED", "phase=" + phase + " endpoint=W" + std::to_string(id) + " kind=writer guid=" +
                                  Guid(w.ptr->guid()) + " entity_kind=2 valid=" + std::to_string(valid));
        QosLog(a, "W" + std::to_string(id), valid);
        Log("WRITER_LIFECYCLE_QOS",
            "endpoint=W" + std::to_string(id) + " autodispose=" +
                std::to_string(a.writer_data_lifecycle().autodispose_unregistered_instances) +
                " publish_mode=" + std::to_string(a.publish_mode().kind));
        return valid || Fail("writer_qos_or_guid");
    }
    bool CreateReader()
    {
        if (reader)
            return Fail("reader_recreate");
        dds::DataReaderQos q;
        Qos(q, false, 11);
        reader = subscriber->create_datareader(topic, q, rl.get());
        if (!reader)
            return Fail("reader_create");
        const bool valid =
            QosValid(reader->get_qos(), false, 11) && Guid(reader->guid()) == Expected(false, 11);
        Log("ENTITY_CREATED", "phase=" + phase + " endpoint=R1 kind=reader guid=" + Guid(reader->guid()) +
                                  " entity_kind=7 valid=" + std::to_string(valid));
        QosLog(reader->get_qos(), "R1", valid);
        return valid || Fail("reader_qos_or_guid");
    }
    bool Graph(bool late, bool emit)
    {
        if (opts.board_a)
        {
            for (auto &[id, w] : writers)
            {
                std::vector<dds::InstanceHandle_t> hs;
                if (w.ptr->get_matched_subscriptions(hs) != dds::RETCODE_OK)
                    return Fail("matched_query");
                std::set<std::string> have, want;
                if (opts.case_id != "H1" || late)
                    want.insert(Expected(false, 11));
                for (auto &h : hs)
                {
                    dds::SubscriptionBuiltinTopicData d;
                    if (w.ptr->get_matched_subscription_data(d, h) != dds::RETCODE_OK)
                        return false;
                    if (!Remote(d))
                        return Fail("remote_qos");
                    have.insert(Guid(d.guid));
                    if (emit)
                        Log("REMOTE_QOS", "endpoint=W" + std::to_string(id) + " peer_guid=" + Guid(d.guid) +
                                              " reliability=" + std::to_string(d.reliability.kind) +
                                              " durability=" + std::to_string(d.durability.kind) +
                                              " valid=1");
                }
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
                if (emit && graph_mismatch)
                    have.clear();
#endif
                if (emit)
                    Log("MATCH_SNAPSHOT", "phase=" + phase + " endpoint=W" + std::to_string(id) +
                                              " guid=" + Expected(true, id) + " peers=" + Join(have) +
                                              " expected=" + Join(want) +
                                              " valid=" + std::to_string(have == want));
                if (have != want)
                    return false;
            }
        }
        else if (reader)
        {
            std::vector<dds::InstanceHandle_t> hs;
            if (reader->get_matched_publications(hs) != dds::RETCODE_OK)
                return Fail("matched_query");
            std::set<std::string> have, want{Expected(true, 1)};
            if (opts.case_id == "K2")
                want.insert(Expected(true, 2));
            for (auto &h : hs)
            {
                dds::PublicationBuiltinTopicData d;
                if (reader->get_matched_publication_data(d, h) != dds::RETCODE_OK)
                    return false;
                if (!Remote(d))
                    return Fail("remote_qos");
                have.insert(Guid(d.guid));
                if (emit)
                    Log("REMOTE_QOS", "endpoint=R1 peer_guid=" + Guid(d.guid) +
                                          " reliability=" + std::to_string(d.reliability.kind) +
                                          " durability=" + std::to_string(d.durability.kind) + " valid=1");
            }
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
            if (emit && graph_mismatch)
                have.clear();
#endif
            if (emit)
                Log("MATCH_SNAPSHOT", "phase=" + phase + " endpoint=R1 guid=" + Expected(false, 11) +
                                          " peers=" + Join(have) + " expected=" + Join(want) +
                                          " valid=" + std::to_string(have == want));
            if (have != want)
                return false;
        }
        return Good();
    }
    bool EmitCheckedSnapshot(bool late, const std::string &predicate)
    {
        return Graph(late, true) || Fail(predicate);
    }
    DSoftBusKeyedSample Sample(unsigned writer, unsigned key, unsigned ordinal) const
    {
        DSoftBusKeyedSample x;
        x.key(key);
        x.run(run_value);
        x.writer(writer);
        x.ordinal(ordinal);
        for (unsigned i = 0; i < 32; ++i)
            x.payload()[i] = static_cast<std::uint8_t>(key * 3 + writer * 17 + ordinal * 7 +
                                                       ((run_value >> ((i % 8) * 8)) & 255) + i);
        return x;
    }
    bool Register(unsigned id, unsigned key, bool defined = true)
    {
        auto x = Sample(id, key, 0);
        auto &w = writers.at(id);
        const auto h = w.ptr->register_instance(&x);
        const bool got = h != dds::HANDLE_NIL;
        dds::InstanceHandle_t computed;
        const bool computed_ok = type->compute_key(&x, computed);
        Log("REGISTER_RETURN",
            "phase=" + phase + " endpoint=W" + std::to_string(id) + " key=" + std::to_string(key) +
                " instance_handle=" + Handle(h) + " computed_handle=" + Handle(computed) +
                " defined=" + std::to_string(got) + " expected_defined=" + std::to_string(defined));
        if (got != defined || !computed_ok || (got && h != computed))
            return Fail("register_instance_result");
        if (got)
        {
            for (auto &[k, v] : w.handles)
                if (k != key && v == h)
                    return Fail("register_handle_alias");
            w.handles[key] = h;
        }
        return Good();
    }
    bool Write(unsigned id, unsigned key, unsigned ordinal)
    {
        auto &w = writers.at(id);
        if (!w.handles.count(key) || !w.written.emplace(key, ordinal).second)
            return Fail("unregistered_or_application_resend");
        auto sample = Sample(id, key, ordinal);
        rtps::WriteParams p;
        const auto rc = w.ptr->write(&sample, p);
        const auto &i = p.sample_identity();
        const auto &sn = i.sequence_number();
        Log("WRITE_RETURN",
            "phase=" + phase + " endpoint=W" + std::to_string(id) + " writer_guid=" + Guid(i.writer_guid()) +
                " sn_hi=" + std::to_string(sn.high) + " sn_lo=" + std::to_string(sn.low) +
                " mode=DEDUCED_REGISTERED_KEY registered_handle=" + Handle(w.handles.at(key)) +
                " key=" + std::to_string(key) + " writer_id=" + std::to_string(id) +
                " ordinal=" + std::to_string(ordinal) + " data_run=" + opts.run +
                " payload_hex=" + Hex(sample.payload().data(), 32) +
                " payload_hash=" + Hash(sample.payload()) + " rc=" + std::to_string(int(rc)));
        return (rc == dds::RETCODE_OK && Guid(i.writer_guid()) == Expected(true, id) && sn.high >= 0 &&
                (sn.high || sn.low)) ||
               Fail("write_return_or_identity");
    }
    bool Ack()
    {
        if (!opts.board_a)
            return true;
        for (auto &[id, w] : writers)
        {
            std::vector<dds::InstanceHandle_t> h;
            if (w.ptr->get_matched_subscriptions(h) != dds::RETCODE_OK)
                return Fail("ack_match_query");
            if (h.empty())
            {
                Log("DDS_WAIT", "phase=" + phase + " endpoint=W" + std::to_string(id) +
                                    " state=NotCalled reason=NO_MATCHED_RELIABLE_READER");
                continue;
            }
            const auto rc = w.ptr->wait_for_acknowledgments({5, 0});
            Log("DDS_WAIT", "phase=" + phase + " endpoint=W" + std::to_string(id) +
                                " state=Called matched_reliable=" + std::to_string(h.size()) +
                                " rc=" + std::to_string(int(rc)));
            if (rc != dds::RETCODE_OK)
                return Fail("dds_wait");
        }
        return true;
    }
    std::string InfoFields(const dds::SampleInfo &i) const
    {
        return " instance_handle=" + Handle(i.instance_handle) +
               " sample_state=" + std::to_string(i.sample_state) +
               " view_state=" + std::to_string(i.view_state) +
               " instance_state=" + std::to_string(i.instance_state) +
               " disposed_generation=" + std::to_string(i.disposed_generation_count) +
               " no_writers_generation=" + std::to_string(i.no_writers_generation_count);
    }
    struct Batch
    {
        dds::ReturnCode_t rc = dds::RETCODE_ERROR;
        std::set<SampleKey> values;
    };
    dds::ReturnCode_t ReturnLoan(dds::LoanableSequence<DSoftBusKeyedSample> &data,
                                 dds::SampleInfoSeq &infos) noexcept
    {
        const auto rc = reader->return_loan(data, infos);
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
        ++real_loan_returns;
        last_loan_rc = int(rc);
#endif
        if (rc == dds::RETCODE_OK)
            --loans;
        return rc;
    }
    Batch Access(bool take, dds::SampleStateMask mask = dds::ANY_SAMPLE_STATE)
    {
        Batch out;
        const auto op = ++operation;
        dds::LoanableSequence<DSoftBusKeyedSample> data;
        dds::SampleInfoSeq infos;
        out.rc = take ? reader->take(data, infos, dds::LENGTH_UNLIMITED, mask, dds::ANY_VIEW_STATE,
                                     dds::ANY_INSTANCE_STATE)
                      : reader->read(data, infos, dds::LENGTH_UNLIMITED, mask, dds::ANY_VIEW_STATE,
                                     dds::ANY_INSTANCE_STATE);
        if (out.rc == dds::RETCODE_OK)
            ++loans;
        // The public API lends the buffers only until return_loan. Even a host
        // allocation exception while copying evidence cannot strand that loan.
        struct LoanGuard
        {
            Impl &owner;
            dds::LoanableSequence<DSoftBusKeyedSample> &data;
            dds::SampleInfoSeq &infos;
            bool armed;
            ~LoanGuard()
            {
                if (armed)
                    owner.ReturnLoan(data, infos);
            }
        } guard{*this, data, infos, out.rc == dds::RETCODE_OK};
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
        if (out.rc == dds::RETCODE_OK)
            InjectEvidence(EvidenceFault::Format);
#endif
        const auto base = "phase=" + phase +
                          " operation=" + (take ? std::string("take") : std::string("read")) +
                          " operation_id=" + std::to_string(op) + " sample_mask=" + std::to_string(mask) +
                          " view_mask=" + std::to_string(dds::ANY_VIEW_STATE) +
                          " instance_mask=" + std::to_string(dds::ANY_INSTANCE_STATE);
        if (out.rc == dds::RETCODE_NO_DATA)
        {
            Log("API_RETURN", base + " rc=" + std::to_string(int(out.rc)) + " count=0");
            return out;
        }
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
        if (out.rc == dds::RETCODE_OK)
            InjectEvidence(EvidenceFault::Log);
#endif
        Log("API_RETURN",
            base + " rc=" + std::to_string(int(out.rc)) + " count=" + std::to_string(infos.length()));
        if (out.rc != dds::RETCODE_OK)
        {
            Fail("read_take_return");
            return out;
        }
        for (dds::LoanableCollection::size_type n = 0; n < infos.length(); ++n)
        {
            const auto &i = infos[n];
            const auto handle = Handle(i.instance_handle);
            const auto guid = Guid(i.sample_identity.writer_guid());
            const auto sn = i.sample_identity.sequence_number();
            if (!i.valid_data)
            {
                const bool identity = sn.high >= 0 && (sn.high || sn.low) && guid != std::string(32, '0');
                const auto key = ledger->Key(handle);
                Log("INSTANCE_STATE", base + " endpoint=R1 reader_guid=" + Guid(reader->guid()) +
                                          " writer_guid=" + (identity ? guid : "UNKNOWN") +
                                          " sn_hi=" + (identity ? std::to_string(sn.high) : "UNKNOWN") +
                                          " sn_lo=" + (identity ? std::to_string(sn.low) : "UNKNOWN") +
                                          " identity_known=" + std::to_string(identity) +
                                          " known_key=" + (key ? std::to_string(key) : "UNKNOWN") +
                                          " mapping_source=" + (key ? "PRIOR_VALID_DATA" : "UNKNOWN") +
                                          " valid_data=0 payload_accessed=0" + InfoFields(i));
                if (!key || !ExpectedLifecycleState(opts.case_id, phase, key, i.instance_state) ||
                    (identity && guid != Expected(true, 1)) || !ledger->State(handle, i.instance_state))
                    Fail("state_handle_or_kind");
                else
                {
                    state[key] = i.instance_state;
                    generations[key] = {i.disposed_generation_count, i.no_writers_generation_count};
                }
                continue;
            }
#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
            InjectEvidence(EvidenceFault::Payload);
#endif
            const auto &x = data[n];
            const auto expected = Sample(x.writer(), x.key(), x.ordinal());
            const bool valid = x.run() == run_value && x.payload() == expected.payload() &&
                               guid == Expected(true, x.writer()) &&
                               ((x.key() == 17 || x.key() == 34) || (opts.case_id == "R3" && x.key() == 51));
            Log("DATA", base + " endpoint=R1 reader_guid=" + Guid(reader->guid()) + " writer_guid=" + guid +
                            " sn_hi=" + std::to_string(sn.high) + " sn_lo=" + std::to_string(sn.low) +
                            " key=" + std::to_string(x.key()) + " writer_id=" + std::to_string(x.writer()) +
                            " ordinal=" + std::to_string(x.ordinal()) + " data_run=" + RunHex(x.run()) +
                            " payload_hex=" + Hex(x.payload().data(), 32) +
                            " payload_hash=" + Hash(x.payload()) + " payload_valid=" + std::to_string(valid) +
                            " valid_data=1" + InfoFields(i));
            if (!ledger->Data({guid, handle, sn.high, sn.low, x.key(), x.ordinal(), valid}, take))
                Fail(ledger->Failure());
            out.values.emplace(guid, x.key(), x.ordinal());
            state[x.key()] = i.instance_state;
            generations[x.key()] = {i.disposed_generation_count, i.no_writers_generation_count};
        }
        const auto rc = ReturnLoan(data, infos);
        guard.armed = false;
        if (rc != dds::RETCODE_OK)
            Fail("return_loan");
        Log("RETURN_LOAN", "phase=" + phase + " operation_id=" + std::to_string(op) + " rc=" +
                               std::to_string(int(rc)) + " loans_outstanding=" + std::to_string(loans));
        return out;
    }
    std::set<SampleKey> ExpectedSet(unsigned first, unsigned last,
                                    std::vector<unsigned> keys = {17, 34}) const
    {
        std::set<SampleKey> s;
        for (auto key : keys)
            for (unsigned i = first; i <= last; ++i)
                s.emplace(Expected(true, 1), key, i);
        return s;
    }
    bool ReceiveUntil(const std::set<SampleKey> &expected, bool take)
    {
        if (opts.board_a)
            return true;
        return Wait(
            [&]
            {
                Access(take, take ? dds::ANY_SAMPLE_STATE : dds::NOT_READ_SAMPLE_STATE);
                if (!Good())
                    return false;
                return ledger->Seen() == expected;
            },
            "reader_expected_set");
    }
    bool ReadRound(unsigned ordinal)
    {
        if (opts.board_a)
            return true;
        const auto wanted = ExpectedSet(ordinal, ordinal);
        return Wait(
            [&]
            {
                const auto b = Access(false);
                const bool seen =
                    std::includes(b.values.begin(), b.values.end(), wanted.begin(), wanted.end());
                if (seen)
                    Log("SNAPSHOT", "phase=" + phase + " round=" + std::to_string(ordinal) +
                                        " count=" + std::to_string(b.values.size()) +
                                        " target_pair_present=1 operation_id=" + std::to_string(operation));
                return seen && Good();
            },
            "round_pair_not_observed");
    }
    bool ExactAccess(bool take, dds::SampleStateMask mask, const std::set<SampleKey> &expected,
                     dds::ReturnCode_t rc = dds::RETCODE_OK)
    {
        if (opts.board_a)
            return true;
        const auto b = Access(take, mask);
        const bool valid = b.rc == rc && b.values == expected;
        Log("SNAPSHOT", "phase=" + phase + " operation_id=" + std::to_string(operation) + " exact_count=" +
                            std::to_string(expected.size()) + " valid=" + std::to_string(valid));
        return valid || Fail("exact_read_take_set");
    }
    bool Pair(unsigned ordinal, std::vector<unsigned> keys = {17, 34})
    {
        if (!opts.board_a)
            return true;
        for (auto k : keys)
            if (!Write(1, k, ordinal))
                return false;
        return Ack();
    }
    bool NegativeCreate()
    {
        if (opts.board_a)
        {
            dds::DataWriterQos q;
            Qos(q, true, 99);
            q.history().depth = 3;
            auto *bad = publisher->create_datawriter(topic, q);
            Log("INVALID_CREATE", "phase=" + phase + " kind=writer result=" +
                                      (bad ? std::string("NONNULL") : std::string("NULL")) +
                                      " expected=NULL depth=3 max_samples_per_instance=2");
            if (bad)
            {
                publisher->delete_datawriter(bad);
                return Fail("inconsistent_create_accepted");
            }
        }
        else
        {
            dds::DataReaderQos q;
            Qos(q, false, 99);
            q.history().depth = 3;
            auto *bad = subscriber->create_datareader(topic, q);
            Log("INVALID_CREATE", "phase=" + phase + " kind=reader result=" +
                                      (bad ? std::string("NONNULL") : std::string("NULL")) +
                                      " expected=NULL depth=3 max_samples_per_instance=2");
            if (bad)
            {
                subscriber->delete_datareader(bad);
                return Fail("inconsistent_create_accepted");
            }
        }
        return true;
    }
    bool NegativeSet()
    {
        dds::ReturnCode_t rc;
        bool unchanged = false;
        if (opts.board_a)
        {
            auto *q = writers.at(1).ptr;
            auto before = q->get_qos();
            auto bad = before;
            bad.history().depth = 3;
            rc = q->set_qos(bad);
            unchanged = q->get_qos() == before;
        }
        else
        {
            auto before = reader->get_qos();
            auto bad = before;
            bad.history().depth = 3;
            rc = reader->set_qos(bad);
            unchanged = reader->get_qos() == before;
        }
        Log("INVALID_SET_QOS", "phase=" + phase +
                                   " endpoint=" + (opts.board_a ? std::string("W1") : std::string("R1")) +
                                   " rc=" + std::to_string(int(rc)) +
                                   " expected_rc=" + std::to_string(int(dds::RETCODE_INCONSISTENT_POLICY)) +
                                   " unchanged=" + std::to_string(unchanged));
        return (rc == dds::RETCODE_INCONSISTENT_POLICY && unchanged) || Fail("inconsistent_set_not_rejected");
    }
    bool StateChange()
    {
        if (opts.board_a)
        {
            auto &w = writers.at(1);
            auto x = Sample(1, 17, 0);
            const auto rc = opts.case_id == "K3" ? w.ptr->dispose(&x, w.handles.at(17))
                                                 : w.ptr->unregister_instance(&x, w.handles.at(17));
            Log("STATE_OPERATION",
                "phase=" + phase + " endpoint=W1 operation=" +
                    (opts.case_id == "K3" ? std::string("dispose") : std::string("unregister")) +
                    " key=17 instance_handle=" + Handle(w.handles.at(17)) + " rc=" + std::to_string(int(rc)));
            return (rc == dds::RETCODE_OK && Ack()) || Fail("state_operation");
        }
        const unsigned desired = opts.case_id == "K4_FALSE" ? dds::NOT_ALIVE_NO_WRITERS_INSTANCE_STATE
                                                            : dds::NOT_ALIVE_DISPOSED_INSTANCE_STATE;
        return Wait(
            [&]
            {
                Access(false, dds::NOT_READ_SAMPLE_STATE);
                return state[17] == desired && state[34] == dds::ALIVE_INSTANCE_STATE && Good();
            },
            "state_transition_not_observed");
    }
    bool Run()
    {
        if (!opened || closed || !Good())
            return Fail("run_precondition");
        if (!Wait([&] { return Graph(false, false); }, "initial_match_timeout"))
            return false;
        if (!EmitCheckedSnapshot(false, "initial_match_snapshot"))
            return false;
        if (opts.board_a)
        {
            for (auto &[id, w] : writers)
            {
                if (!Register(id, 17))
                    return false;
                if (opts.case_id != "K2" && !Register(id, 34))
                    return false;
                if (opts.case_id == "R3" && !Register(id, 51))
                    return false;
            }
        }
        if (!Done() || !Release("PRE"))
            return false;
        if (opts.case_id == "R1")
        {
            if (!NegativeCreate())
                return false;
        }
        else if (opts.case_id == "K2")
        {
            if (opts.board_a)
            {
                for (unsigned id : {1u, 2u})
                    for (unsigned n = 1; n <= 3; ++n)
                        if (!Write(id, 17, n))
                            return false;
                if (!Ack())
                    return false;
            }
            else
            {
                auto wanted = ExpectedSet(1, 3, {17});
                for (unsigned n = 1; n <= 3; ++n)
                    wanted.emplace(Expected(true, 2), 17, n);
                if (!ReceiveUntil(wanted, true))
                    return false;
            }
        }
        else if (opts.case_id == "K1" || opts.case_id == "H1" || opts.case_id == "H3")
        {
            for (unsigned n = 1; n <= 4; ++n)
                if (!Pair(n))
                    return false;
            if (opts.case_id != "H1" && !ReceiveUntil(ExpectedSet(1, 4), opts.case_id == "K1"))
                return false;
        }
        else
        {
            const auto keys =
                opts.case_id == "R3" ? std::vector<unsigned>{17, 34, 51} : std::vector<unsigned>{17, 34};
            if (!Pair(1, keys))
                return false;
            if (opts.case_id == "H2")
            {
                if (!ReadRound(1))
                    return false;
            }
            else if (!ReceiveUntil(ExpectedSet(1, 1, keys), !Lifecycle()))
                return false;
        }
        if (!Done() || !Release("CHANGE"))
            return false;
        if (Lifecycle())
        {
            if (!StateChange())
                return false;
        }
        else if (opts.case_id == "H1")
        {
            if (!opts.board_a && !CreateReader())
                return false;
            if (!Wait([&] { return Graph(true, false); }, "late_match_timeout"))
                return false;
            if (!EmitCheckedSnapshot(true, "late_match_snapshot"))
                return false;
            if (!ReceiveUntil(ExpectedSet(3, 4), true))
                return false;
        }
        else if (opts.case_id == "H2")
        {
            if (!Pair(2) || !ReadRound(2))
                return false;
        }
        else if (opts.case_id == "H3")
        {
            if (!ExactAccess(false, dds::ANY_SAMPLE_STATE, ExpectedSet(1, 4)) ||
                !ExactAccess(false, dds::READ_SAMPLE_STATE, ExpectedSet(1, 4)) ||
                !ExactAccess(false, dds::NOT_READ_SAMPLE_STATE, {}, dds::RETCODE_NO_DATA))
                return false;
        }
        else if (opts.case_id == "R1")
        {
            if (!NegativeSet())
                return false;
        }
        else if (opts.case_id == "R2" && opts.board_a)
        {
            if (!Register(1, 51, false))
                return false;
        }
        if (!Done() || !Release("POST"))
            return false;
        if (Lifecycle())
        {
            if (opts.board_a)
            {
                if (!Write(1, 34, 2) || !Ack())
                    return false;
            }
            else
            {
                auto expected = ExpectedSet(1, 1);
                expected.emplace(Expected(true, 1), 34, 2);
                if (!ReceiveUntil(expected, false))
                    return false;
                const auto desired = opts.case_id == "K4_FALSE" ? dds::NOT_ALIVE_NO_WRITERS_INSTANCE_STATE
                                                                : dds::NOT_ALIVE_DISPOSED_INSTANCE_STATE;
                if (state[17] != desired || state[34] != dds::ALIVE_INSTANCE_STATE)
                    return Fail("nonalive_A_or_live_B_changed");
                Log("LIFECYCLE_BOUNDARY", "phase=POST A_state=" + std::to_string(state[17]) +
                                              " B_state=" + std::to_string(state[34]) + " B2_observed=1");
            }
        }
        else if (opts.case_id == "H1")
        {
            if (!Pair(5) || !ReceiveUntil(ExpectedSet(3, 5), true))
                return false;
        }
        else if (opts.case_id == "H2")
        {
            if (!Pair(3) || !ReadRound(3))
                return false;
        }
        else if (opts.case_id == "H3")
        {
            if (!ExactAccess(true, dds::ANY_SAMPLE_STATE, ExpectedSet(1, 4)) ||
                !ExactAccess(false, dds::ANY_SAMPLE_STATE, {}, dds::RETCODE_NO_DATA) ||
                !ExactAccess(true, dds::ANY_SAMPLE_STATE, {}, dds::RETCODE_NO_DATA))
                return false;
        }
        else if (opts.case_id == "R1")
        {
            if (!Pair(1) || !ReceiveUntil(ExpectedSet(1, 1), true))
                return false;
        }
        else if (opts.case_id == "R2" || opts.case_id == "R3")
        {
            const auto keys =
                opts.case_id == "R3" ? std::vector<unsigned>{17, 34, 51} : std::vector<unsigned>{17, 34};
            if (!Pair(2, keys) || !ReceiveUntil(ExpectedSet(1, 2, keys), true))
                return false;
        }
        if (!Done() || !Release("OBSERVE"))
            return false;
        if (Lifecycle())
        {
            if (opts.board_a)
            {
                if (opts.case_id != "K3" && !Register(1, 17))
                    return false;
                if (!Write(1, 17, 2) || !Ack())
                    return false;
            }
            else
            {
                if (!ReceiveUntil(ExpectedSet(1, 2), false))
                    return false;
                const auto g = generations[17];
                if (state[17] != dds::ALIVE_INSTANCE_STATE || state[34] != dds::ALIVE_INSTANCE_STATE ||
                    (opts.case_id == "K4_FALSE" ? g.second != 1 : g.first != 1))
                    return Fail("retained_generation_transition");
                Log("LIFECYCLE_BOUNDARY", "phase=OBSERVE A_state=" + std::to_string(state[17]) +
                                              " B_state=" + std::to_string(state[34]) +
                                              " disposed_generation=" + std::to_string(g.first) +
                                              " no_writers_generation=" + std::to_string(g.second));
            }
        }
        else if (opts.case_id == "H2")
        {
            if (!Pair(4) || !ReadRound(4) || !ExactAccess(false, dds::ANY_SAMPLE_STATE, ExpectedSet(3, 4)) ||
                !ExactAccess(true, dds::ANY_SAMPLE_STATE, ExpectedSet(3, 4)) ||
                !ExactAccess(true, dds::ANY_SAMPLE_STATE, {}, dds::RETCODE_NO_DATA))
                return false;
        }
        const auto start = Clock::now();
        while (Clock::now() < start + std::chrono::seconds(2))
        {
            if (!Good() || !Abort() || !Graph(true, false))
                return Fail("observation_changed");
            if (!opts.board_a)
            {
                Access(false, dds::NOT_READ_SAMPLE_STATE);
                if (!Good())
                    return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Log("OBSERVATION",
            "duration_ms=" +
                std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count()) +
                " complete=1");
        if (!EmitCheckedSnapshot(true, "final_match_snapshot"))
            return false;
        if (!opts.board_a)
            Log("LEDGER", "phase=FINAL endpoint=R1 reader_guid=" + Guid(reader->guid()) +
                              " unique=" + std::to_string(ledger->Unique()) +
                              " failure=" + (ledger->Failure().empty() ? "none" : ledger->Failure()));
        if (!Done() || !Release("CLEANUP") || !Close())
            return false;
        return Done();
    }
    bool Close()
    {
        if (closed)
            return cleanup_failure.empty();
        cleaning = true;
        bool ok = true;
        if (loans)
        {
            CleanupFail("loan_not_returned");
            ok = false;
        }
        if (reader)
        {
            const auto rc = subscriber->delete_datareader(reader);
            Log("ENTITY_DELETE_RETURN", "endpoint=R1 rc=" + std::to_string(int(rc)));
            if (rc == dds::RETCODE_OK)
                reader = nullptr;
            else
            {
                CleanupFail("reader_delete");
                ok = false;
            }
        }
        for (auto &[id, w] : writers)
            if (w.ptr)
            {
                const auto rc = publisher->delete_datawriter(w.ptr);
                Log("ENTITY_DELETE_RETURN",
                    "endpoint=W" + std::to_string(id) + " rc=" + std::to_string(int(rc)));
                if (rc == dds::RETCODE_OK)
                    w.ptr = nullptr;
                else
                {
                    CleanupFail("writer_delete");
                    ok = false;
                }
            }
        if (participant)
        {
            const auto contained = participant->delete_contained_entities();
            const auto rc =
                contained == dds::RETCODE_OK
                    ? dds::DomainParticipantFactory::get_instance()->delete_participant(participant)
                    : dds::RETCODE_PRECONDITION_NOT_MET;
            Log("PARTICIPANT_DELETE_RETURN",
                "contained_rc=" + std::to_string(int(contained)) + " rc=" + std::to_string(int(rc)));
            if (rc == dds::RETCODE_OK)
                participant = nullptr;
            else
            {
                CleanupFail("participant_delete");
                ok = false;
            }
        }
        closed = ok;
        return ok;
    }
};
KeyedFixture::KeyedFixture(Options o) : impl_(std::make_unique<Impl>(std::move(o))) {}
KeyedFixture::~KeyedFixture() { impl_->Close(); }
bool KeyedFixture::OpenReference(std::uint16_t port)
{
    impl_->reference = true;
    if (port < 1024 || port > 65000)
        return impl_->Fail("reference_port");
    if (!impl_->Initialize())
        return false;
    dds::DomainParticipantQos q;
    impl_->ParticipantQos(q);
    auto udp = std::make_shared<rtps::UDPv4TransportDescriptor>();
    udp->interfaceWhiteList.push_back("127.0.0.1");
    q.transport().user_transports.push_back(udp);
    const auto loc = [&](bool a, unsigned extra)
    {
        rtps::Locator_t l;
        l.kind = LOCATOR_KIND_UDPv4;
        l.port = port + (a ? 0 : 2) + extra;
        rtps::IPLocator::setIPv4(l, 127, 0, 0, 1);
        return l;
    };
    q.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(loc(impl_->opts.board_a, 0));
    q.wire_protocol().default_unicast_locator_list.push_back(loc(impl_->opts.board_a, 10));
    q.wire_protocol().builtin.initialPeersList.push_back(loc(!impl_->opts.board_a, 0));
    return impl_->CreateParticipant(q);
}
bool KeyedFixture::OpenNative(const std::vector<Config> &configs, SocketApi api,
                              std::shared_ptr<ProcessRuntime> runtime)
{
    if (configs.size() != 1 || !runtime || !configs[0].additional_peers.empty() ||
        impl_->opts.case_id == "R1")
        return impl_->Fail("native_configuration_or_host_only_case");
    if (!impl_->Initialize())
        return false;
    dds::DomainParticipantQos q;
    impl_->ParticipantQos(q);
    const auto &c = configs[0];
    impl_->control = std::make_shared<Control>();
    q.transport().user_transports.push_back(
        std::make_shared<ExperimentalDescriptor>(c, api, impl_->control, runtime));
    q.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(c.local_locator);
    q.wire_protocol().default_unicast_locator_list.push_back(c.local_locator);
    q.wire_protocol().builtin.initialPeersList.push_back(c.peer_locator);
    return impl_->CreateParticipant(q);
}
bool KeyedFixture::Run() { return impl_->Run(); }
bool KeyedFixture::Close() { return impl_->Close(); }
std::string KeyedFixture::FirstFailure() const
{
    std::lock_guard<std::mutex> l(impl_->failure_mutex);
    return impl_->failure;
}
std::string KeyedFixture::CleanupFailure() const
{
    std::lock_guard<std::mutex> l(impl_->failure_mutex);
    return impl_->cleanup_failure;
}
std::vector<std::shared_ptr<Control>> KeyedFixture::Controls() const
{
    return impl_->control ? std::vector<std::shared_ptr<Control>>{impl_->control}
                          : std::vector<std::shared_ptr<Control>>{};
}

#ifdef FASTDDS_DSOFTBUS_KEYED_HOST_CONTRACT_TESTS
bool KeyedFixture::ContractTest(const std::string &test, std::uint16_t port)
{
    const bool graph = test.rfind("graph-", 0) == 0;
    const bool state_only = test == "state-no-payload";
    const std::string selected = test == "graph-late" ? "H1" : state_only ? "K3" : "K1";
    const std::string run = RunHex(0x7564000000000000ull + std::uint64_t(Pid()) * 65536 + port);
    KeyedFixture a({run, selected, true, "."}), b({run, selected, false, "."});
    if (!a.OpenReference(port))
        return false;
    b.impl_->test_factory_preconfigured = true;
    if (!b.OpenReference(port))
        return false;
    if (test == "graph-late")
    {
        if (!a.impl_->Graph(false, false))
            return false;
        b.impl_->phase = "CHANGE";
        if (!b.impl_->CreateReader())
            return false;
    }
    if (!a.impl_->Wait([&] { return a.impl_->Graph(true, false) && b.impl_->Graph(true, false); },
                       "contract_match"))
        return false;
    bool pass = false;
    if (graph)
    {
        const std::string phase = test == "graph-initial" ? "INITIAL"
                                  : test == "graph-late"  ? "CHANGE"
                                                          : "OBSERVE";
        const std::string expected = test == "graph-initial" ? "initial_match_snapshot"
                                     : test == "graph-late"  ? "late_match_snapshot"
                                                             : "final_match_snapshot";
        a.impl_->phase = phase;
        const bool prior = a.impl_->Graph(true, false);
        a.impl_->graph_mismatch = true;
        const bool admitted = a.impl_->EmitCheckedSnapshot(test != "graph-initial", expected);
        pass = prior && !admitted && a.FirstFailure() == expected;
        std::printf(
            "KEYED_GRAPH_CONTRACT prior_poll=%d emitted_snapshot_return=%d first_failure=%s expected=%s\n",
            prior, admitted, a.FirstFailure().c_str(), expected.c_str());
    }
    else
    {
        if (!a.impl_->Register(1, 17) || !a.impl_->Write(1, 17, 1) || !a.impl_->Ack() ||
            !b.impl_->reader->wait_for_unread_message({5, 0}))
            return false;
        if (state_only)
        {
            b.impl_->Access(false, dds::NOT_READ_SAMPLE_STATE);
            a.impl_->phase = b.impl_->phase = "CHANGE";
            if (!a.impl_->StateChange() || !b.impl_->reader->wait_for_unread_message({5, 0}))
                return false;
        }
        const auto before = b.impl_->real_loan_returns;
        b.impl_->evidence_fault = test.find("format") != std::string::npos ? Impl::EvidenceFault::Format
                                  : test.find("log") != std::string::npos  ? Impl::EvidenceFault::Log
                                                                           : Impl::EvidenceFault::Payload;
        bool caught = false;
        try
        {
            b.impl_->Access(test.find("take") != std::string::npos, dds::NOT_READ_SAMPLE_STATE);
        }
        catch (const Impl::EvidenceFailure &)
        {
            caught = true;
        }
        const auto returned = b.impl_->real_loan_returns - before;
        pass = (caught != state_only) && returned == 1 && b.impl_->last_loan_rc == int(dds::RETCODE_OK) &&
               b.impl_->loans == 0;
        if (state_only)
            pass = pass && b.impl_->evidence_fault == Impl::EvidenceFault::Payload &&
                   b.impl_->state[17] == dds::NOT_ALIVE_DISPOSED_INSTANCE_STATE;
        std::printf("KEYED_LOAN_CONTRACT caught=%d state_only=%d actual_return_calls=%u actual_last_rc=%d "
                    "outstanding=%u\n",
                    caught, state_only, returned, b.impl_->last_loan_rc, b.impl_->loans);
    }
    const bool clean_b = b.Close(), clean_a = a.Close();
    return pass && clean_b && clean_a;
}
#endif
} // namespace fastdds_dsoftbus::experiment::keyed
