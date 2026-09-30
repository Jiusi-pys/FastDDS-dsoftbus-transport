// Out-of-repository consumer of the INSTALLED MDDS experimental component.
//
// This program uses only installed public headers, the installed generated type,
// the public experimental::Runtime, and public Fast DDS APIs. It includes no
// MDDS probe, test, artifact or private header, and it never uses
// add_subdirectory or a source-tree object.
//
// The identity component in ./identity/ is a verbatim copy of the reviewed,
// device-proven token entry. It carries the platform identity setup that the
// install documentation says is a separate deployment concern, and is kept
// apart from every MDDS and Fast DDS call below.
#include <mdds/dsoftbus/ExperimentalRuntime.hpp>
#include <mdds/examples/MddsKeyedSamplePubSubTypes.hpp>

#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/qos/DomainParticipantQos.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/qos/DataWriterQos.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/qos/DataReaderQos.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/core/status/PublicationMatchedStatus.hpp>
#include <fastdds/dds/core/status/SubscriptionMatchedStatus.hpp>

#include "identity/token_first_entry.hpp"
#include "token_bridge.h"
#include "ServiceNames.hpp"

#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

namespace fd = eprosima::fastdds::dds;
using namespace mdds::dsoftbus;

namespace {

constexpr std::uint32_t kDomain = 177;
constexpr std::uint32_t kMaxRecordBytes = 8192;
constexpr int kPerDirection = 5;
constexpr int kPayloadBytes = 32;
constexpr int kHistoryDepth = 16;
constexpr int kMaxSamples = 32;

std::string g_run;
char g_side = 'A';
std::string g_control;

void Say(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

[[noreturn]] void FailCleanup(const char* why) {
    // A failed DDS deletion or local Close must terminate the failed process
    // rather than unwind a runtime that may still be owned by the SDK. This is
    // failure protection, NOT normal cleanup, SDK cancellation or drain.
    Say("INST_FAILCLEANUP run=%s side=%c reason=%s normal_cleanup=0", g_run.c_str(), g_side, why);
    std::fflush(stdout);
    std::abort();
}

std::string Guid(const eprosima::fastdds::rtps::GUID_t& guid) {
    static const char* hex = "0123456789abcdef";
    const auto* prefix = reinterpret_cast<const unsigned char*>(&guid.guidPrefix);
    const auto* entity = reinterpret_cast<const unsigned char*>(&guid.entityId);
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 12; ++i) { out.push_back(hex[prefix[i] >> 4]); out.push_back(hex[prefix[i] & 0xF]); }
    for (int i = 0; i < 4; ++i) { out.push_back(hex[entity[i] >> 4]); out.push_back(hex[entity[i] & 0xF]); }
    return out;
}

// Deterministic, verifiable payload shared by both boards: it binds the run, the
// writing direction and the ordinal into every byte position.
std::uint8_t Payload(unsigned writer_side, unsigned ordinal, std::uint64_t run, int i) {
    const std::uint64_t mixed = run ^ (static_cast<std::uint64_t>(writer_side + 1) << 40)
            ^ (static_cast<std::uint64_t>(ordinal) << 16) ^ static_cast<std::uint64_t>(i);
    return static_cast<std::uint8_t>(mixed & 0xFFu);
}

// The runner owns the release. This is the reviewed controlled-start mechanism:
// a side publishes a stage line and blocks until the runner, which waits for both
// boards, writes the matching control file.
bool Stage(const char* name) {
    Say("INST_STAGE run=%s side=%c pid=%d stage=%s", g_run.c_str(), g_side, static_cast<int>(getpid()), name);
    if (std::strcmp(name, "DDS_DELETED") == 0) return true;
    const std::string path = g_control + "." + name;
    const std::string expected = g_run + " " + name;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline) {
        std::ifstream file(path);
        std::string line;
        if (file && std::getline(file, line) && line == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Say("INST_STAGE_TIMEOUT run=%s side=%c stage=%s", g_run.c_str(), g_side, name);
    return false;
}

std::string Name(const std::string& prefix, unsigned side, const char* role) {
    return prefix + (side ? ".B." : ".A.") + role;
}

Locator Logical(unsigned side) {
    EndpointId id{};
    id[0] = 10;
    id[15] = static_cast<std::uint8_t>(side + 1);
    Locator locator;
    if (!LocatorCodec::Encode(id, locator)) throw std::runtime_error("locator codec rejected the logical id");
    return locator;
}

class Entry final : public mdds_probe::TokenEntryOperations {
public:
    Entry(std::uint64_t token, std::string run) : token_(token), run_(std::move(run)) {}
    mdds_probe::TaskCheckpoint ObserveTasks() override {
        mdds_probe::TaskCheckpoint out;
        out.process_id = static_cast<std::uint64_t>(getpid());
        // The entry runs on the main thread, where TID equals PID.
        out.current_tid = out.process_id;
        DIR* directory = opendir("/proc/self/task");
        out.opened = directory != nullptr;
        if (directory) {
            bool valid = true;
            for (;;) {
                errno = 0;
                auto* item = readdir(directory);
                if (!item) { out.complete = valid && errno == 0; break; }
                if (item->d_name[0] == '.') continue;
                std::uint64_t tid = 0;
                const char* end = item->d_name + std::strlen(item->d_name);
                const auto rc = std::from_chars(item->d_name, end, tid);
                if (rc.ec != std::errc{} || rc.ptr != end || !tid) valid = false;
                if (!out.count) out.sole_tid = tid;
                if (out.count < 2) ++out.count;
            }
            if (closedir(directory)) out.complete = false;
        }
        return out;
    }
    std::uint64_t GetSelf() override { return MddsTokenBridgeGetSelfToken(); }
    int SetSelf(std::uint64_t token) override { return MddsTokenBridgeSetSelfToken(token); }
    mdds_probe::OwnedTokenObservation VerifyOwned(std::uint64_t token, const char* phase) override {
        MddsTokenBridgeResult result{};
        const int rc = MddsTokenBridgeVerify(static_cast<std::uint32_t>(token), &result);
        Say("INST_IDENTITY phase=%s bridge=%d info_rc=%d permission_rc=%d granted=%d", phase, rc,
                result.infoRc, result.permissionRc, result.granted);
        mdds_probe::OwnedTokenObservation out;
        if (rc || result.called != 15) return out;
        out.lookup = result.lookupToken;
        out.type = result.type;
        out.native_type = result.nativeType;
        out.info_rc = result.infoRc;
        out.process_name = result.processName;
        out.apl = result.apl;
        out.normal_apl = result.normalApl;
        out.query_rc = result.permissionRc;
        out.permission_granted = result.permissionGranted;
        if (result.permissionCount == 1) out.states.push_back(result.permissionState);
        return out;
    }
    void Diagnostic(const char* phase, std::uint64_t expected, std::uint64_t actual, int rc) override {
        Say("INST_IDENTITY phase=%s matches=%d rc=%d", phase, expected == actual, rc);
    }
    // The identity component never drives the experiment; the caller does.
    bool EnterProvider() override { return true; }
    bool CloseProvider() override { return true; }
private:
    std::uint64_t token_;
    std::string run_;
};

void ReportNative(const char* stage, const phase0::PublicSocketObservation& o) {
    auto socket = [](const std::optional<std::int32_t>& v) {
        return v ? std::to_string(*v) : std::string("none");
    };
    Say("INST_NATIVE run=%s side=%c stage=%s configured=%d listener_configured=%d outgoing=%s listener=%s "
        "accepted=%s socket_calls=%llu listen_calls=%llu bind_calls=%llu send_calls=%llu shutdown_calls=%llu "
        "callback_entries=%llu rejected_callbacks=%llu negotiations_denied=%llu exceptions=%llu "
        "first_api_error=%s first_callback_error=%s stopped=%d closed=%d",
            g_run.c_str(), g_side, stage, o.configured ? 1 : 0, o.listener_configured ? 1 : 0,
            socket(o.outgoing_socket).c_str(), socket(o.listener_socket).c_str(), socket(o.accepted_socket).c_str(),
            static_cast<unsigned long long>(o.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Socket)].calls),
            static_cast<unsigned long long>(o.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Listen)].calls),
            static_cast<unsigned long long>(o.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Bind)].calls),
            static_cast<unsigned long long>(o.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Send)].calls),
            static_cast<unsigned long long>(o.operations[static_cast<std::size_t>(phase0::PublicSocketOperation::Shutdown)].calls),
            static_cast<unsigned long long>(o.callback_entries), static_cast<unsigned long long>(o.rejected_callbacks),
            static_cast<unsigned long long>(o.negotiations_denied), static_cast<unsigned long long>(o.exceptions),
            o.first_api_error ? std::to_string(o.first_api_error->code).c_str() : "none",
            o.first_callback_error ? std::to_string(*o.first_callback_error).c_str() : "none",
            o.stopped ? 1 : 0, o.closed ? 1 : 0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 8) {
        std::fprintf(stderr, "usage: mdds_installed_consumer <side:0|1> <peer-network-id> <authorized-package> "
                             "<shared-service-prefix> <run-hex16> <token-decimal> <control-prefix>\n");
        return 2;
    }
    const unsigned side = std::string(argv[1]) == "1" ? 1u : 0u;
    if (std::string(argv[1]) != "0" && std::string(argv[1]) != "1") return 2;
    const std::string peer_network_id = argv[2];
    const std::string package = argv[3];
    const std::string name_stem = argv[4];
    g_run = argv[5];
    g_side = side ? 'B' : 'A';
    g_control = argv[7];
    const std::string prefix = installed_example::ServicePrefix(package,name_stem,g_run);
    std::uint64_t token = 0;
    if (!mdds_probe::ParseTokenArgument(argv[6], token)) return 2;
    if (peer_network_id.size() != 64) return 2;

    Say("INST_MAIN run=%s side=%c pid=%d mode=installed_runtime consumer=external mdds_headers=INSTALLED",
            g_run.c_str(), g_side, static_cast<int>(getpid()));

    // Platform identity, deliberately separate from every MDDS call below. The
    // permission itself was written and will be removed by the reviewed external
    // native permission probe, never by this process.
    Entry operations(token, g_run);
    mdds_probe::TokenFirstEntry identity(operations);
    const bool admitted = identity.Begin({mdds_probe::TokenEntryMode::Record, true, true, token});
    if (!admitted) {
        Say("INST_EXIT run=%s side=%c result=1 reason=IDENTITY_ENTRY failed=1", g_run.c_str(), g_side);
        return 3;
    }

    // Keep the selected identity throughout actual Runtime creation, use and
    // destruction. Restore only after this scope has completed normally.
    const auto run_dds = [&]() -> int {
    // Fast DDS must not deliver this Participant's own samples to its own reader.
    eprosima::fastdds::LibrarySettings library;
    library.intraprocess_delivery = eprosima::fastdds::INTRAPROCESS_OFF;
    auto* factory = fd::DomainParticipantFactory::get_instance();
    if (factory->set_library_settings(library) != fd::RETCODE_OK) return 4;
    eprosima::fastdds::LibrarySettings effective;
    if (factory->get_library_settings(effective) != fd::RETCODE_OK ||
            effective.intraprocess_delivery != eprosima::fastdds::INTRAPROCESS_OFF) return 4;

    const std::uint64_t run_value = std::strtoull(g_run.c_str(), nullptr, 16);
    phase0::PublicSocketConfiguration backend;
    backend.identity = {Name(prefix, side, "outgoing"), {910 + side * 4}, {1010 + side * 4}};
    backend.listener_identity = {Name(prefix, side, "listener"), {911 + side * 4}, {1011 + side * 4}};
    backend.package_name = package;
    backend.peer_network_id = peer_network_id;
    backend.peer_name = Name(prefix, 1 - side, "listener");
    backend.max_message_size = kMaxRecordBytes;
    backend.bind_qos = {{0, 80}, {1, 2000}};
    backend.listen_qos = backend.bind_qos;

    DSoftBusTransportDescriptor config(kMaxRecordBytes);
    config.directory.locals = {{Logical(side), prefix + ".ns", backend.listener_identity.service_name}};
    config.directory.routes = {{{Logical(side), Logical(1 - side)},
            {peer_network_id, backend.peer_name, backend.identity.service_name,
             {Name(prefix, 1 - side, "outgoing")}, RecordDataType::Bytes}, 1}};
    config.default_metatraffic_unicast = {Logical(side)};
    config.default_user_unicast = {Logical(side)};

    experimental::Runtime runtime(backend);
    // One Runtime, one descriptor issuance, one factory call, one Participant.
    if (!runtime.SetPreConnectGate([&] { return Stage("LISTENER_READY"); })) return 5;
    auto descriptor = runtime.MakeDescriptor(config);
    if (!descriptor) return 6;

    std::atomic<bool> stop{false}, pump_failed{false};
    std::thread pump([&] {
        while (!stop.load()) {
            const auto send = runtime.PumpSend();
            if (send.outcome == NativeSendOutcome::ReturnedInvalidCode ||
                    send.outcome == NativeSendOutcome::ReturnedByException ||
                    (send.actual_return_code && *send.actual_return_code != 0)) pump_failed = true;
            const auto receive = runtime.PumpReceive();
            if (receive != phase0::BackendStatus::None && receive != phase0::BackendStatus::NoWork &&
                    receive != phase0::BackendStatus::InvalidState) pump_failed = true;
            std::this_thread::yield();
        }
    });

    fd::DomainParticipant* participant = nullptr;
    int result = 1;
    try {
        fd::DomainParticipantQos qos;
        qos.transport().use_builtin_transports = false;
        qos.transport().user_transports.push_back(descriptor);
        qos.wire_protocol().builtin.initialPeersList.push_back(Logical(1 - side));
        participant = factory->create_participant(kDomain, qos);
        if (!participant) throw std::runtime_error("participant creation failed");
        const auto native = runtime.ObserveSocket();
        ReportNative("POST_CREATE", native);
        if (!native.listener_configured || !native.listener_socket) throw std::runtime_error("listener not configured");
        if (!native.outgoing_socket) throw std::runtime_error("outgoing socket missing");

        fd::TypeSupport type(new MddsKeyedSamplePubSubType());
        if (type.register_type(participant) != fd::RETCODE_OK) throw std::runtime_error("type registration failed");
        auto* topic = participant->create_topic("mdds_installed_runtime_sample", type.get_type_name(), fd::TOPIC_QOS_DEFAULT);
        auto* publisher = participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
        auto* subscriber = participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        if (!topic || !publisher || !subscriber) throw std::runtime_error("entity creation failed");

        fd::DataWriterQos writer_qos;
        writer_qos.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        writer_qos.durability().kind = fd::VOLATILE_DURABILITY_QOS;
        writer_qos.history().kind = fd::KEEP_LAST_HISTORY_QOS;
        writer_qos.history().depth = kHistoryDepth;
        writer_qos.resource_limits().max_samples = kMaxSamples;
        writer_qos.resource_limits().max_instances = 1;
        writer_qos.resource_limits().max_samples_per_instance = kMaxSamples;
        writer_qos.data_sharing().off();
        fd::DataReaderQos reader_qos;
        reader_qos.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        reader_qos.durability().kind = fd::VOLATILE_DURABILITY_QOS;
        reader_qos.history().kind = fd::KEEP_LAST_HISTORY_QOS;
        reader_qos.history().depth = kHistoryDepth;
        reader_qos.resource_limits().max_samples = kMaxSamples;
        reader_qos.resource_limits().max_instances = 1;
        reader_qos.resource_limits().max_samples_per_instance = kMaxSamples;
        reader_qos.data_sharing().off();

        auto* writer = publisher->create_datawriter(topic, writer_qos);
        auto* reader = subscriber->create_datareader(topic, reader_qos);
        if (!writer || !reader) throw std::runtime_error("endpoint creation failed");
        Say("INST_CONFIG run=%s side=%c domain=%u builtin=0 intraprocess=0 sharing=0 reliability=RELIABLE "
            "durability=VOLATILE history=%d samples=%d instances=1 per_instance=%d max_record=%u payload=%d",
                g_run.c_str(), g_side, kDomain, kHistoryDepth, kMaxSamples, kMaxSamples,
                kMaxRecordBytes, kPayloadBytes);
        Say("INST_ID run=%s side=%c participant=%s writer=%s reader=%s",
                g_run.c_str(), g_side, Guid(participant->guid()).c_str(), Guid(writer->guid()).c_str(),
                Guid(reader->guid()).c_str());

        // Only a real DDS remote match may precede the formal samples.
        fd::PublicationMatchedStatus published;
        fd::SubscriptionMatchedStatus subscribed;
        const auto match_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (;;) {
            writer->get_publication_matched_status(published);
            reader->get_subscription_matched_status(subscribed);
            if ((published.current_count > 0 && subscribed.current_count > 0) || pump_failed) break;
            if (std::chrono::steady_clock::now() >= match_deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (published.current_count <= 0 || subscribed.current_count <= 0 || pump_failed)
            throw std::runtime_error("remote DDS match timed out");
        Say("INST_MATCH run=%s side=%c writer=%s reader=%s pub_matched=%d sub_matched=%d",
                g_run.c_str(), g_side, Guid(writer->guid()).c_str(), Guid(reader->guid()).c_str(),
                published.current_count, subscribed.current_count);
        if (!Stage("MATCH")) throw std::runtime_error("MATCH stage not released");

        MddsKeyedSample sample;
        for (int ordinal = 1; ordinal <= kPerDirection; ++ordinal) {
            sample.key(side + 1);
            sample.run(run_value);
            sample.writer(side + 1);
            sample.ordinal(static_cast<std::uint32_t>(ordinal));
            for (int i = 0; i < kPayloadBytes; ++i) sample.payload()[i] = Payload(side, ordinal, run_value, i);
            const auto rc = writer->write(&sample);
            if (rc != fd::RETCODE_OK) throw std::runtime_error("write failed");
            Say("INST_WRITE run=%s side=%c ordinal=%d writer=%s rc=0", g_run.c_str(), g_side, ordinal,
                    Guid(writer->guid()).c_str());
        }

        int taken = 0;
        const auto take_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (taken < kPerDirection && std::chrono::steady_clock::now() < take_deadline) {
            MddsKeyedSample received;
            fd::SampleInfo info;
            if (reader->take_next_sample(&received, &info) != fd::RETCODE_OK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            if (!info.valid_data) continue;
            const unsigned peer = 1 - side;
            if (received.key() != peer + 1 || received.writer() != peer + 1 || received.run() != run_value) continue;
            const unsigned ordinal = received.ordinal();
            if (ordinal < 1 || ordinal > static_cast<unsigned>(kPerDirection)) continue;
            bool payload_ok = true;
            for (int i = 0; i < kPayloadBytes; ++i)
                if (received.payload()[i] != Payload(peer, ordinal, run_value, i)) payload_ok = false;
            const auto remote = info.sample_identity.writer_guid();
            Say("INST_TAKE run=%s side=%c ordinal=%u reader=%s remote_writer=%s sn=%lld run_field=%llu "
                "writer_field=%u payload_ok=%d",
                    g_run.c_str(), g_side, ordinal, Guid(reader->guid()).c_str(), Guid(remote).c_str(),
                    static_cast<long long>(info.sample_identity.sequence_number().to64long()),
                    static_cast<unsigned long long>(received.run()), received.writer(), payload_ok ? 1 : 0);
            if (!payload_ok) throw std::runtime_error("remote payload verification failed");
            ++taken;
        }
        Say("INST_WAIT run=%s side=%c taken=%d expected=%d", g_run.c_str(), g_side, taken, kPerDirection);
        if (taken != kPerDirection) throw std::runtime_error("missing remote deliveries");
        if (!Stage("DATA_DONE")) throw std::runtime_error("DATA_DONE stage not released");
        result = 0;
    } catch (const std::exception& e) {
        Say("INST_ERROR run=%s side=%c reason=%s", g_run.c_str(), g_side, e.what());
    }

    // Normal shutdown: stop new business writes, delete from this external control
    // thread while the pumps can still progress, join the pumps, then Close.
    if (participant) {
        if (participant->delete_contained_entities() != fd::RETCODE_OK) {
            stop = true; pump.join();
            FailCleanup("DDS entity deletion failed");
        }
        if (factory->delete_participant(participant) != fd::RETCODE_OK) {
            stop = true; pump.join();
            FailCleanup("DDS Participant deletion failed");
        }
        Say("INST_DELETE run=%s side=%c entities_rc=0 participant_rc=0", g_run.c_str(), g_side);
    }
    stop = true;
    pump.join();
    if (!runtime.Close()) FailCleanup("local runtime did not close");
    const auto observed = runtime.ObserveSocket();
    ReportNative("CLOSED", observed);
    if (pump_failed) result = 1;
    if (observed.first_api_error || observed.first_callback_error || observed.exceptions) result = 1;
    return result;
    };
    const int result=run_dds();
    const bool restored = identity.Restore();
    Stage("DDS_DELETED");
    Say("INST_EXIT run=%s side=%c result=%d restored=%d sdk_drain=UNPROVEN", g_run.c_str(), g_side, result,
            restored ? 1 : 0);
    return result == 0 && restored ? 0 : 7;
}
