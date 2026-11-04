// CVE-2026-63340 Host verification probe.
//
// Task: CANDIDATE_SECURITY_MITIGATION_FROM_3BE5492.
//
// One process plays exactly one role so that the authenticated handshake and
// admission control are exercised between independent processes rather than
// through an intra-process shortcut. Every role uses the real Fast DDS 2.14.7
// participant, the real builtin PKI-DH / Access-Permissions / AES-GCM-GMAC
// plugins and the governance and permissions documents produced by
// generate_isolated_pki.py.
//
// The probe is deliberately restricted to the loopback interface: builtin
// transports are replaced by an explicit UDPv4 descriptor whitelisted to
// 127.0.0.1, multicast metatraffic is cleared and discovery runs through an
// explicit initial peer. No packet leaves the host.
//
// This program never crafts or injects raw RTPS bytes. The unprotected peer it
// can be paired with is an ordinary Fast DDS participant with no security
// properties, which is what an unauthenticated deployment actually looks like.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/core/status/StatusMask.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/dds/topic/qos/TopicQos.hpp>
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastdds/rtps/common/InstanceHandle.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastdds/dds/log/Log.hpp>
#include <fastrtps/utils/IPLocator.h>

#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>

namespace {

using namespace eprosima::fastdds::dds;

// Upper bound for one serialized probe sample, including the 4-byte
// encapsulation header. Chosen so a single sample is several times larger than
// the 65500-byte default UDP datagram limit and therefore must be fragmented by
// the real writer and reassembled by the real reader.
constexpr uint32_t kMaxPayloadBytes = 512u * 1024u;
constexpr uint32_t kMaxSerializedBytes = kMaxPayloadBytes + 64u;

struct ProbeSample
{
    uint32_t index = 0;
    std::vector<uint8_t> payload;
};

// Diagnostics for the probe's own serializer. A failure here is a harness
// defect, not a finding about the library under test, so it is surfaced
// explicitly instead of being silently reported as a bare error code.
std::mutex g_serialize_error_mutex;
std::string g_serialize_error;
uint32_t g_last_payload_max_size = 0;

void note_serialize_error(
        const std::string& text)
{
    std::lock_guard<std::mutex> lock(g_serialize_error_mutex);
    if (g_serialize_error.empty())
    {
        g_serialize_error = text;
    }
}

// Minimal hand-written TopicDataType. It uses Fast CDR directly, exactly like a
// generated typesupport would, and is registered with the real participant.
class ProbeSampleDataType : public TopicDataType
{
public:

    ProbeSampleDataType()
    {
        setName("Cve63340ProbeSample");
        m_typeSize = kMaxSerializedBytes;
        m_isGetKeyDefined = false;
    }

    bool serialize(
            void* data,
            eprosima::fastrtps::rtps::SerializedPayload_t* payload) override
    {
        return serialize(data, payload, DataRepresentationId_t::XCDR_DATA_REPRESENTATION);
    }

    bool serialize(
            void* data,
            eprosima::fastrtps::rtps::SerializedPayload_t* payload,
            DataRepresentationId_t) override
    {
        ProbeSample* sample = static_cast<ProbeSample*>(data);
        g_last_payload_max_size = payload->max_size;
        eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->max_size);
        eprosima::fastcdr::Cdr ser(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                eprosima::fastcdr::CdrVersion::XCDRv1);
        payload->encapsulation =
                ser.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
        try
        {
            ser.serialize_encapsulation();
            ser << sample->index;
            ser << sample->payload;
        }
        catch (const std::exception& e)
        {
            note_serialize_error(std::string("serialize exception: ") + e.what()
                    + " (payload max_size=" + std::to_string(payload->max_size)
                    + ", bytes=" + std::to_string(sample->payload.size()) + ")");
            return false;
        }
        catch (...)
        {
            note_serialize_error("serialize unknown exception (payload max_size="
                    + std::to_string(payload->max_size) + ")");
            return false;
        }
        payload->length = static_cast<uint32_t>(ser.get_serialized_data_length());
        return true;
    }

    bool deserialize(
            eprosima::fastrtps::rtps::SerializedPayload_t* payload,
            void* data) override
    {
        ProbeSample* sample = static_cast<ProbeSample*>(data);
        eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->length);
        eprosima::fastcdr::Cdr deser(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                eprosima::fastcdr::CdrVersion::XCDRv1);
        try
        {
            deser.read_encapsulation();
            deser >> sample->index;
            deser >> sample->payload;
        }
        catch (const std::exception&)
        {
            return false;
        }
        return true;
    }

    std::function<uint32_t()> getSerializedSizeProvider(
            void* data) override
    {
        ProbeSample* sample = static_cast<ProbeSample*>(data);
        const uint32_t size = 4u + 4u + 4u + static_cast<uint32_t>(sample->payload.size());
        return [size]() -> uint32_t
               {
                   return size;
               };
    }

    std::function<uint32_t()> getSerializedSizeProvider(
            void* data,
            DataRepresentationId_t) override
    {
        return getSerializedSizeProvider(data);
    }

    bool getKey(
            void*,
            eprosima::fastrtps::rtps::InstanceHandle_t*,
            bool) override
    {
        return false;
    }

    void* createData() override
    {
        return new ProbeSample();
    }

    void deleteData(
            void* data) override
    {
        delete static_cast<ProbeSample*>(data);
    }
};

std::string json_escape(
        const std::string& value)
{
    std::string out;
    for (char ch : value)
    {
        switch (ch)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += ch; break;
        }
    }
    return out;
}

struct Observation
{
    std::atomic<bool> matched{false};
    std::atomic<int> matched_current{0};
    std::atomic<uint64_t> matched_total{0};
    std::atomic<uint64_t> samples_written{0};
    std::atomic<uint64_t> samples_received{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> deserialize_failures{0};
    std::atomic<int> first_index{-1};
    std::atomic<int> last_index{-1};
    std::atomic<bool> contiguous{true};
    // Participant-level observations. These are the layer attribution used to
    // say where an unauthenticated or unauthorized peer stopped.
    std::atomic<uint64_t> discovered_participants{0};
    std::atomic<uint64_t> removed_participants{0};
    std::atomic<uint64_t> authorized_participants{0};
    std::atomic<uint64_t> unauthorized_participants{0};
    // Serializes the take loop; the listener callback and the polling fallback
    // can run on different threads.
    std::mutex drain_mutex;
    std::mutex error_mutex;
    std::string last_error;
};

void record_error(
        Observation& obs,
        const std::string& text)
{
    std::lock_guard<std::mutex> lock(obs.error_mutex);
    if (obs.last_error.empty())
    {
        obs.last_error = text;
    }
}

class WriterListener : public DataWriterListener
{
public:

    explicit WriterListener(
            Observation& obs)
        : obs_(obs)
    {
    }

    void on_publication_matched(
            DataWriter*,
            const PublicationMatchedStatus& info) override
    {
        if (info.current_count_change == 1)
        {
            obs_.matched = true;
        }
        obs_.matched_current = info.current_count;
        obs_.matched_total = info.total_count;
        std::cout << "[probe] publication matched current=" << info.current_count
                  << " total=" << info.total_count << std::endl;
    }

    void on_offered_deadline_missed(
            DataWriter*,
            const OfferedDeadlineMissedStatus&) override
    {
    }

private:

    Observation& obs_;
};

// Single drain implementation shared by the listener callback and the polling
// fallback, so a listener that never fires cannot be mistaken for "no data".
// Both callers can run on different threads, so the bookkeeping under the lock
// is the only place that reads or advances the index state.
void drain_reader(
        DataReader* reader,
        Observation& obs)
{
    std::lock_guard<std::mutex> lock(obs.drain_mutex);
    ProbeSample sample;
    SampleInfo info;
    while (reader->take_next_sample(&sample, &info) == ReturnCode_t::RETCODE_OK)
    {
        if (!info.valid_data)
        {
            continue;
        }
        const int index = static_cast<int>(sample.index);
        const int previous = obs.last_index.load();
        if (previous >= 0 && index != previous + 1)
        {
            obs.contiguous = false;
        }
        if (previous < 0)
        {
            obs.first_index = index;
        }
        obs.last_index = index;
        obs.samples_received.fetch_add(1);
        obs.bytes_received.fetch_add(sample.payload.size());
    }
}

class ReaderListener : public DataReaderListener
{
public:

    explicit ReaderListener(
            Observation& obs)
        : obs_(obs)
    {
    }

    void on_subscription_matched(
            DataReader*,
            const SubscriptionMatchedStatus& info) override
    {
        if (info.current_count_change == 1)
        {
            obs_.matched = true;
        }
        obs_.matched_current = info.current_count;
        obs_.matched_total = info.total_count;
        std::cout << "[probe] subscription matched current=" << info.current_count
                  << " total=" << info.total_count << std::endl;
    }

    void on_data_available(
            DataReader* reader) override
    {
        drain_reader(reader, obs_);
    }

    void on_sample_lost(
            DataReader*,
            const SampleLostStatus& status) override
    {
        record_error(obs_, "sample lost total=" + std::to_string(status.total_count));
    }

    void on_requested_deadline_missed(
            DataReader*,
            const RequestedDeadlineMissedStatus&) override
    {
    }

private:

    Observation& obs_;
};

// Participant-level listener. on_participant_authentication is raised by the
// real SecurityManager after authentication and access-control evaluation, so
// AUTHORIZED/UNAUTHORIZED here is plugin evidence rather than a test flag.
class ParticipantListener : public DomainParticipantListener
{
public:

    explicit ParticipantListener(
            Observation& obs)
        : obs_(obs)
    {
    }

    void on_participant_discovery(
            DomainParticipant*,
            eprosima::fastrtps::rtps::ParticipantDiscoveryInfo&& info) override
    {
        using Status = eprosima::fastrtps::rtps::ParticipantDiscoveryInfo::DISCOVERY_STATUS;
        switch (info.status)
        {
            case Status::DISCOVERED_PARTICIPANT:
                obs_.discovered_participants.fetch_add(1);
                std::cout << "[probe] remote participant discovered" << std::endl;
                break;
            case Status::REMOVED_PARTICIPANT:
                obs_.removed_participants.fetch_add(1);
                break;
            default:
                break;
        }
    }

    void onParticipantAuthentication(
            DomainParticipant*,
            eprosima::fastrtps::rtps::ParticipantAuthenticationInfo&& info) override
    {
        using Status = eprosima::fastrtps::rtps::ParticipantAuthenticationInfo::AUTHENTICATION_STATUS;
        if (info.status == Status::AUTHORIZED_PARTICIPANT)
        {
            obs_.authorized_participants.fetch_add(1);
            std::cout << "[probe] remote participant AUTHORIZED by plugins" << std::endl;
        }
        else
        {
            obs_.unauthorized_participants.fetch_add(1);
            std::cout << "[probe] remote participant UNAUTHORIZED by plugins" << std::endl;
        }
    }

private:

    Observation& obs_;
};

struct Options
{
    std::string role;
    std::string topic = "Cve63340IsolatedTest";
    std::string pki_dir;
    std::string governance;
    std::string permissions;
    std::string identity = "a";
    std::string result_path;
    std::string note;
    int domain = 0;
    uint32_t payload_bytes = 4096;
    uint32_t samples = 5;
    uint32_t deadline_ms = 8000;
    uint32_t match_timeout_ms = 12000;
    uint16_t metatraffic_port = 0;
    uint16_t peer_port = 0;
    uint16_t data_port = 0;
    bool verbose = false;
};

bool parse_args(
        int argc,
        char** argv,
        Options& opts)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        auto next = [&](std::string& out) -> bool
                   {
                       if (i + 1 >= argc)
                       {
                           std::cerr << "missing value for " << key << std::endl;
                           return false;
                       }
                       out = argv[++i];
                       return true;
                   };
        auto next_u32 = [&](uint32_t& out) -> bool
                        {
                            std::string raw;
                            if (!next(raw))
                            {
                                return false;
                            }
                            out = static_cast<uint32_t>(std::stoul(raw));
                            return true;
                        };
        if (key == "--role")
        {
            if (!next(opts.role)) return false;
        }
        else if (key == "--topic")
        {
            if (!next(opts.topic)) return false;
        }
        else if (key == "--pki")
        {
            if (!next(opts.pki_dir)) return false;
        }
        else if (key == "--governance")
        {
            if (!next(opts.governance)) return false;
        }
        else if (key == "--permissions")
        {
            if (!next(opts.permissions)) return false;
        }
        else if (key == "--identity")
        {
            if (!next(opts.identity)) return false;
        }
        else if (key == "--result")
        {
            if (!next(opts.result_path)) return false;
        }
        else if (key == "--note")
        {
            if (!next(opts.note)) return false;
        }
        else if (key == "--domain")
        {
            std::string raw;
            if (!next(raw)) return false;
            opts.domain = std::stoi(raw);
        }
        else if (key == "--payload-bytes")
        {
            if (!next_u32(opts.payload_bytes)) return false;
        }
        else if (key == "--samples")
        {
            if (!next_u32(opts.samples)) return false;
        }
        else if (key == "--deadline-ms")
        {
            if (!next_u32(opts.deadline_ms)) return false;
        }
        else if (key == "--match-timeout-ms")
        {
            if (!next_u32(opts.match_timeout_ms)) return false;
        }
        else if (key == "--metatraffic-port")
        {
            uint32_t value = 0;
            if (!next_u32(value)) return false;
            opts.metatraffic_port = static_cast<uint16_t>(value);
        }
        else if (key == "--peer-port")
        {
            uint32_t value = 0;
            if (!next_u32(value)) return false;
            opts.peer_port = static_cast<uint16_t>(value);
        }
        else if (key == "--data-port")
        {
            uint32_t value = 0;
            if (!next_u32(value)) return false;
            opts.data_port = static_cast<uint16_t>(value);
        }
        else if (key == "--verbose")
        {
            opts.verbose = true;
        }
        else
        {
            std::cerr << "unknown argument: " << key << std::endl;
            return false;
        }
    }
    return !opts.role.empty();
}

// Security property names exactly as the builtin plugins read them.
void apply_security_properties(
        DomainParticipantQos& pqos,
        const Options& opts,
        std::vector<std::string>& recorded)
{
    auto add = [&](const std::string& name, const std::string& value)
              {
                  pqos.properties().properties().emplace_back(name, value);
                  recorded.push_back(name + "=" + value);
              };
    const std::string pki = opts.pki_dir;
    const std::string governance = opts.governance.empty()
            ? pki + "/governance_sign.smime" : opts.governance;
    const std::string permissions = opts.permissions.empty()
            ? pki + "/permissions.smime" : opts.permissions;
    const std::string identity_base = pki + "/isolated_" + opts.identity;

    add("dds.sec.auth.plugin", "builtin.PKI-DH");
    add("dds.sec.auth.builtin.PKI-DH.identity_ca", "file://" + pki + "/ca_cert.pem");
    add("dds.sec.auth.builtin.PKI-DH.identity_certificate", "file://" + identity_base + "_cert.pem");
    add("dds.sec.auth.builtin.PKI-DH.private_key", "file://" + identity_base + "_key.pem");
    add("dds.sec.access.plugin", "builtin.Access-Permissions");
    add("dds.sec.access.builtin.Access-Permissions.permissions_ca", "file://" + pki + "/ca_cert.pem");
    add("dds.sec.access.builtin.Access-Permissions.governance", "file://" + governance);
    add("dds.sec.access.builtin.Access-Permissions.permissions", "file://" + permissions);
    add("dds.sec.crypto.plugin", "builtin.AES-GCM-GMAC");
}

// Record the property values the participant is actually created with, after
// any role-specific mutation. The receipt must not report the intended values.
void capture_effective_properties(
        const DomainParticipantQos& pqos,
        std::vector<std::string>& recorded)
{
    recorded.clear();
    for (const auto& property : pqos.properties().properties())
    {
        recorded.push_back(property.name() + "=" + property.value());
    }
}

bool role_is_publisher(
        const std::string& role)
{
    return role == "secure_pub" || role == "plain_pub" || role == "misconfig_pub"
           || role == "missing_cert_pub";
}

bool role_is_secure(
        const std::string& role)
{
    return role == "secure_pub" || role == "secure_sub" || role == "wrongperm_sub"
           || role == "misconfig_pub" || role == "missing_cert_pub";
}

void configure_transport(
        DomainParticipantQos& pqos,
        const Options& opts)
{
    auto udp = std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
    udp->interfaceWhiteList.push_back("127.0.0.1");
    pqos.transport().user_transports.push_back(udp);
    pqos.transport().use_builtin_transports = false;

    // No multicast: discovery is driven purely by the explicit loopback peer.
    pqos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();
    if (opts.metatraffic_port != 0)
    {
        eprosima::fastrtps::rtps::Locator_t unicast;
        eprosima::fastrtps::rtps::IPLocator::createLocator(
            LOCATOR_KIND_UDPv4, "127.0.0.1", opts.metatraffic_port, unicast);
        pqos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(unicast);
    }
    // Builtin transports are replaced above, so the domain-derived default
    // locators are not computed. User data needs an explicit loopback locator
    // or the participant advertises nowhere to send to.
    if (opts.data_port != 0)
    {
        eprosima::fastrtps::rtps::Locator_t data_locator;
        eprosima::fastrtps::rtps::IPLocator::createLocator(
            LOCATOR_KIND_UDPv4, "127.0.0.1", opts.data_port, data_locator);
        pqos.wire_protocol().default_unicast_locator_list.push_back(data_locator);
    }
    if (opts.peer_port != 0)
    {
        eprosima::fastrtps::rtps::Locator_t peer_locator;
        eprosima::fastrtps::rtps::IPLocator::createLocator(
            LOCATOR_KIND_UDPv4, "127.0.0.1", opts.peer_port, peer_locator);
        pqos.wire_protocol().builtin.initialPeersList.push_back(peer_locator);
    }
}

void write_result(
        const Options& opts,
        const std::string& verdict,
        bool participant_created,
        const std::vector<std::string>& security_properties,
        size_t discovered_participants,
        const Observation& obs)
{
    std::ostringstream body;
    body << "{\n";
    body << "  \"role\": \"" << json_escape(opts.role) << "\",\n";
    body << "  \"note\": \"" << json_escape(opts.note) << "\",\n";
    body << "  \"verdict\": \"" << verdict << "\",\n";
    body << "  \"participant_created\": " << (participant_created ? "true" : "false") << ",\n";
    body << "  \"security_enabled_role\": " << (role_is_secure(opts.role) ? "true" : "false") << ",\n";
    body << "  \"identity\": \"" << json_escape(opts.identity) << "\",\n";
    // The CLI argument, not necessarily the document the participant used: the
    // misconfigured roles rewrite it. The effective value is in
    // security_properties below, which is the authoritative field.
    body << "  \"governance_argument\": \"" << json_escape(opts.governance) << "\",\n";
    body << "  \"topic\": \"" << json_escape(opts.topic) << "\",\n";
    body << "  \"payload_bytes\": " << opts.payload_bytes << ",\n";
    body << "  \"discovered_participants\": " << discovered_participants << ",\n";
    body << "  \"removed_participants\": " << obs.removed_participants.load() << ",\n";
    body << "  \"authorized_participants\": " << obs.authorized_participants.load() << ",\n";
    body << "  \"unauthorized_participants\": " << obs.unauthorized_participants.load() << ",\n";
    body << "  \"matched\": " << (obs.matched.load() ? "true" : "false") << ",\n";
    body << "  \"matched_current\": " << obs.matched_current.load() << ",\n";
    body << "  \"matched_total\": " << obs.matched_total.load() << ",\n";
    body << "  \"samples_written\": " << obs.samples_written.load() << ",\n";
    body << "  \"samples_received\": " << obs.samples_received.load() << ",\n";
    body << "  \"bytes_received\": " << obs.bytes_received.load() << ",\n";
    body << "  \"deserialize_failures\": " << obs.deserialize_failures.load() << ",\n";
    body << "  \"first_index\": " << obs.first_index.load() << ",\n";
    body << "  \"last_index\": " << obs.last_index.load() << ",\n";
    body << "  \"contiguous\": " << (obs.contiguous.load() ? "true" : "false") << ",\n";
    body << "  \"last_error\": \"" << json_escape(obs.last_error) << "\",\n";
    {
        std::lock_guard<std::mutex> lock(g_serialize_error_mutex);
        body << "  \"probe_serialize_error\": \"" << json_escape(g_serialize_error) << "\",\n";
    }
    body << "  \"probe_serialize_payload_max_size\": " << g_last_payload_max_size << ",\n";
    body << "  \"security_properties\": [";
    for (size_t i = 0; i < security_properties.size(); ++i)
    {
        body << (i ? ", " : "") << "\"" << json_escape(security_properties[i]) << "\"";
    }
    body << "]\n}\n";

    std::cout << body.str();
    if (!opts.result_path.empty())
    {
        std::ofstream out(opts.result_path, std::ios::binary | std::ios::trunc);
        out << body.str();
    }
}

} // namespace

int main(
        int argc,
        char** argv)
{
    Options opts;
    if (!parse_args(argc, argv, opts))
    {
        std::cerr << "usage: secure_discovery_probe --role <role> --pki <dir> "
                     "[--governance file] [--permissions file] [--identity a|b|c] "
                     "[--domain n] [--topic name] [--payload-bytes n] [--samples n] "
                     "[--deadline-ms n] [--match-timeout-ms n] "
                     "[--metatraffic-port n] [--peer-port n] [--result file] [--verbose]\n";
        return 2;
    }

    if (opts.verbose)
    {
        Log::SetVerbosity(Log::Info);
    }
    else
    {
        Log::SetVerbosity(Log::Warning);
    }
    Log::SetCategoryFilter(std::regex(".*"));
    Log::ReportFilenames(false);
    Log::ReportFunctions(false);

    Observation obs;
    std::vector<std::string> security_properties;

    DomainParticipantQos pqos = PARTICIPANT_QOS_DEFAULT;
    configure_transport(pqos, opts);

    if (role_is_secure(opts.role))
    {
        apply_security_properties(pqos, opts, security_properties);
    }
    if (opts.role == "misconfig_pub")
    {
        // Present the plugin but point governance at a document that does not
        // exist. A deployment that silently falls back to non-secure operation
        // would still create a participant; fail-closed must not.
        pqos.properties().properties().clear();
        security_properties.clear();
        apply_security_properties(pqos, opts, security_properties);
        for (auto& property : pqos.properties().properties())
        {
            if (property.name() == "dds.sec.access.builtin.Access-Permissions.governance")
            {
                property.value() = "file://" + opts.pki_dir + "/does_not_exist.smime";
            }
        }
    }
    if (opts.role == "missing_cert_pub")
    {
        pqos.properties().properties().clear();
        security_properties.clear();
        apply_security_properties(pqos, opts, security_properties);
        for (auto& property : pqos.properties().properties())
        {
            if (property.name() == "dds.sec.auth.builtin.PKI-DH.identity_certificate")
            {
                property.value() = "file://" + opts.pki_dir + "/absent_identity_cert.pem";
            }
        }
    }

    ParticipantListener participant_listener(obs);
    // Record what is actually configured, not what the role intended: the
    // misconfigured roles deliberately rewrite two of these paths below.
    capture_effective_properties(pqos, security_properties);
    DomainParticipant* participant =
            DomainParticipantFactory::get_instance()->create_participant(
        opts.domain, pqos, &participant_listener, StatusMask::all());

    // One exit path, so every early return releases the participant instead of
    // relying on process exit to reclaim it.
    auto finish = [&](const std::string& verdict, bool created, size_t discovered) -> int
    {
        write_result(opts, verdict, created, security_properties, discovered, obs);
        if (participant != nullptr)
        {
            DomainParticipantFactory::get_instance()->delete_participant(participant);
        }
        Log::Reset();
        return 0;
    };

    if (participant == nullptr)
    {
        return finish("PARTICIPANT_CREATE_FAILED", false, 0);
    }

    TypeSupport type(new ProbeSampleDataType());
    if (participant->register_type(type) != ReturnCode_t::RETCODE_OK)
    {
        record_error(obs, "register_type failed");
        return finish("REGISTER_TYPE_FAILED", true, 0);
    }

    Topic* topic = participant->create_topic(opts.topic, type.get_type_name(), TOPIC_QOS_DEFAULT);
    if (topic == nullptr)
    {
        record_error(obs, "create_topic failed");
        return finish("CREATE_TOPIC_FAILED", true, 0);
    }

    WriterListener writer_listener(obs);
    ReaderListener reader_listener(obs);
    DataWriter* writer = nullptr;
    DataReader* reader = nullptr;

    if (role_is_publisher(opts.role))
    {
        Publisher* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
        if (publisher == nullptr)
        {
            record_error(obs, "create_publisher failed");
            return finish("CREATE_PUBLISHER_FAILED", true, 0);
        }
        DataWriterQos wqos = DATAWRITER_QOS_DEFAULT;
        wqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        wqos.history().kind = KEEP_LAST_HISTORY_QOS;
        wqos.history().depth = 20;
        wqos.resource_limits().max_samples = 50;
        wqos.resource_limits().max_samples_per_instance = 20;
        writer = publisher->create_datawriter(topic, wqos, &writer_listener);
        if (writer == nullptr)
        {
            record_error(obs, "create_datawriter failed");
            return finish("CREATE_WRITER_FAILED", true, 0);
        }
    }
    else
    {
        Subscriber* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
        if (subscriber == nullptr)
        {
            record_error(obs, "create_subscriber failed");
            return finish("CREATE_SUBSCRIBER_FAILED", true, 0);
        }
        DataReaderQos rqos = DATAREADER_QOS_DEFAULT;
        rqos.reliability().kind = RELIABLE_RELIABILITY_QOS;
        rqos.history().kind = KEEP_LAST_HISTORY_QOS;
        rqos.history().depth = 20;
        rqos.resource_limits().max_samples = 50;
        rqos.resource_limits().max_samples_per_instance = 20;
        reader = subscriber->create_datareader(topic, rqos, &reader_listener);
        if (reader == nullptr)
        {
            record_error(obs, "create_datareader failed");
            return finish("CREATE_READER_FAILED", true, 0);
        }
    }

    const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(opts.deadline_ms);

    if (writer != nullptr)
    {
        // Bounded wait for the authenticated match. Giving up early is an
        // observation, not a pass.
        const auto match_deadline = std::chrono::steady_clock::now()
                + std::chrono::milliseconds(opts.match_timeout_ms);
        while (!obs.matched.load() && std::chrono::steady_clock::now() < match_deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (obs.matched.load())
        {
            ProbeSample sample;
            sample.payload.assign(opts.payload_bytes, 0x5A);
            for (uint32_t i = 0; i < opts.samples; ++i)
            {
                sample.index = i + 1;
                // Use the handle overload: the single-argument DataWriter::write
                // in this release returns bool, so its value must not be read as
                // a ReturnCode_t.
                const ReturnCode_t written = writer->write(&sample, HANDLE_NIL);
                if (written == ReturnCode_t::RETCODE_OK)
                {
                    obs.samples_written.fetch_add(1);
                }
                else
                {
                    record_error(obs, "write returned code " + std::to_string(written())
                            + " at index " + std::to_string(i + 1));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    }
    else
    {
        // Poll as well as listen. The listener is the primary path; polling
        // makes an unfired listener visible instead of looking like silence.
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (reader != nullptr)
            {
                drain_reader(reader, obs);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        if (reader != nullptr)
        {
            drain_reader(reader, obs);
        }
    }

    const size_t discovered_count = obs.discovered_participants.load();

    std::string verdict;
    if (writer != nullptr)
    {
        if (!obs.matched.load())
        {
            verdict = "NO_MATCH";
        }
        else if (obs.samples_written.load() == opts.samples)
        {
            verdict = "MATCHED_AND_WRITTEN";
        }
        else
        {
            verdict = "MATCHED_PARTIAL_WRITE";
        }
    }
    else
    {
        if (!obs.matched.load())
        {
            verdict = "NO_MATCH";
        }
        else if (obs.samples_received.load() == opts.samples && obs.contiguous.load())
        {
            verdict = "MATCHED_AND_DELIVERED";
        }
        else if (obs.samples_received.load() > 0)
        {
            verdict = "MATCHED_PARTIAL_DELIVERY";
        }
        else
        {
            verdict = "MATCHED_NO_DELIVERY";
        }
    }

    return finish(verdict, true, discovered_count);
}
