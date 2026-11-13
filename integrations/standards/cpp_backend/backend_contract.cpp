#include B3_HEADER
#include <algorithm>
#include <fastdds/dds/core/policy/QosPolicies.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
namespace rt = eprosima::fastrtps::rtps;
const auto OK = eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
const auto NO_DATA = eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/dds/xtypes/type_representation/ITypeObjectRegistry.hpp>
namespace rt = eprosima::fastdds::rtps;
const auto OK = eprosima::fastdds::dds::RETCODE_OK;
const auto NO_DATA = eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <chrono>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace fd = eprosima::fastdds::dds;
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
std::string hex(const unsigned char* data, size_t size) {
    std::ostringstream out;
    for (size_t i = 0; i < size; ++i) out << std::hex << std::setfill('0') << std::setw(2) << unsigned(data[i]);
    return out.str();
}
template<class Support> void contract(typename Support::type input, const char* name, int index) {
    Support support;
    for (auto rep : {fd::XCDR_DATA_REPRESENTATION, fd::XCDR2_DATA_REPRESENTATION}) {
        rt::SerializedPayload_t payload(65536);
        typename Support::type output;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        check(support.serialize(&input, &payload, rep), "serialize");
        check(support.getSerializedSizeProvider(&input, rep)() == payload.length, "serialized size");
        check(support.deserialize(&payload, &output), "deserialize");
#else
        check(support.serialize(&input, payload, rep), "serialize");
        check(support.calculate_serialized_size(&input, rep) == payload.length, "serialized size");
        check(support.deserialize(payload, &output), "deserialize");
#endif
        check(input == output, "roundtrip value");
        std::cout << "B3_WIRE " << name << ' ' << index << ' ' << rep << ' ' << hex(payload.data, payload.length) << '\n';
        for (bool force : {false, true}) {
            rt::InstanceHandle_t handle;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
            bool has_key = support.getKey(&input, &handle, force);
#else
            bool has_key = support.compute_key(&input, handle, force);
            rt::InstanceHandle_t from_wire;
            check(support.compute_key(payload, from_wire, force) == has_key, "wire key status");
            if (has_key) check(std::memcmp(handle.value, from_wire.value, 16) == 0, "wire key value");
#endif
            std::cout << "B3_KEY " << name << ' ' << index << ' ' << force << ' ' << has_key;
            if (has_key) std::cout << ' ' << hex(handle.value, 16);
            std::cout << '\n';
        }
    }
#if !defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    support.register_type_object_representation();
    fd::xtypes::TypeIdentifierPair identifiers;
    check(fd::DomainParticipantFactory::get_instance()->type_object_registry().get_type_identifiers(name, identifiers) == fd::RETCODE_OK, "type identifiers");
    for (const auto* id : {&identifiers.type_identifier1(), &identifiers.type_identifier2()}) {
        std::cout << "B3_TYPE " << name << ' ' << unsigned(id->_d());
        if (id->_d() == fd::xtypes::EK_COMPLETE || id->_d() == fd::xtypes::EK_MINIMAL)
            std::cout << ' ' << hex(id->equivalence_hash().data(), id->equivalence_hash().size());
        std::cout << '\n';
    }
#endif
}

#ifdef B3_CASES
using Sample = StandardsB3::Record;
using SampleSupport = StandardsB3::RecordPubSubType;
Sample sample(int index) {
    Sample value;
    value.id(17 + index); value.ready(index != 0); value.letter('Z'); value.delta(-32768);
    value.count(65535); value.total(StandardsB3::MIN_TOTAL); value.serial(StandardsB3::MAX_SERIAL);
    value.ratio(-0.0f); value.scale(1.25); value.mode(StandardsB3::State::ACTIVE);
    value.label(index == 0 ? "" : std::string(32, 'x'));
    if (index) value.samples({-2147483647, 0, 17, 2147483647});
    value.coordinates({{{{1, -2, 3}}, {{4, 5, -6}}}});
    StandardsB3::Point point; point.x(-1.5); point.y(0.25f); value.origin(point);
    if (index) value.points({point, point, point});
    return value;
}
void run_contracts() {
    for (int i = 0; i < 2; ++i) contract<SampleSupport>(sample(i), "StandardsB3::Record", i);
    StandardsB3::Point point; point.x(-1.5); point.y(0.25f);
    contract<StandardsB3::PointPubSubType>(point, "StandardsB3::Point", 0);
    StandardsB3::Appended appended; appended.label(std::string(24, 'k')); appended.magnitude(-2.5);
    appended.payload({0, 1, 127, 128, 255, 3, 2, 1});
    contract<StandardsB3::AppendedPubSubType>(appended, "StandardsB3::Appended", 0);
    StandardsB3::Tiny tiny; tiny.one(0x0102030405060708ULL); tiny.two(0x1112131415161718ULL); tiny.value(255);
    contract<StandardsB3::TinyPubSubType>(tiny, "StandardsB3::Tiny", 0);
    StandardsB3::AlignedKey aligned; aligned.first(0x01020304); aligned.second(0x1112131415161718ULL); aligned.third(0x22);
    contract<StandardsB3::AlignedKeyPubSubType>(aligned, "StandardsB3::AlignedKey", 0);
    StandardsB3::Collections collections;
    for (auto& row : collections.groups()) for (auto& group : row) group = {point, point};
    for (auto& row : collections.primitive_groups()) for (auto& group : row) group = {1, -2};
    collections.layers({{1, -2, 3}, {4, 5, 6}});
    contract<StandardsB3::CollectionsPubSubType>(collections, "StandardsB3::Collections", 0);
}
#else
using Sample = Standards::BoundedRecord;
using SampleSupport = Standards::BoundedRecordPubSubType;
Sample sample(int index) {
    Sample value; value.id(17 + index); value.sequence_number(42 + index);
    value.label(index == 0 ? "" : std::string(64, 'z'));
    if (index) value.payload(std::vector<uint8_t>(256, 255));
    return value;
}
void run_contracts() {
    for (int i = 0; i < 2; ++i) contract<SampleSupport>(sample(i), "Standards::BoundedRecord", i);
}
#endif

#ifdef B3_OWN_BACKEND
void concurrent_contract() {
    SampleSupport support;
    std::atomic<bool> valid{true};
    std::array<std::thread, 4> threads;
    for (auto& thread : threads) thread = std::thread([&] {
        for (int i = 0; i < 64; ++i) {
            auto input = sample(i % 2);
            Sample output;
            rt::SerializedPayload_t payload(65536);
            rt::InstanceHandle_t key;
            auto rep = i % 2 ? fd::XCDR_DATA_REPRESENTATION : fd::XCDR2_DATA_REPRESENTATION;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
            if (!support.serialize(&input, &payload, rep) || !support.deserialize(&payload, &output) ||
                    !support.getKey(&input, &key, true) || !(input == output)) valid = false;
#else
            if (!support.serialize(&input, payload, rep) || !support.deserialize(payload, &output) ||
                    !support.compute_key(&input, key, true) || !(input == output)) valid = false;
#endif
        }
    });
    for (auto& thread : threads) thread.join();
    check(valid, "concurrent serialization/key calls");
    std::cout << "B3_CONCURRENT PASS\n";
}

void invalid_contract() {
    SampleSupport support;
    auto input = sample(1);
    for (auto rep : {fd::XCDR_DATA_REPRESENTATION, fd::XCDR2_DATA_REPRESENTATION}) {
        auto invalid = input;
        invalid.label(std::string(1024, 'x'));
        rt::SerializedPayload_t payload(65536), small(4);
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        check(!support.serialize(&invalid, &payload, rep) && payload.length == 0, "reject bound");
        check(support.getSerializedSizeProvider(&invalid, rep)() == 0, "reject invalid size");
        check(!support.serialize(&input, &small, rep) && small.length == 0, "reject small buffer");
        check(support.serialize(&input, &payload, rep), "valid input before truncation");
#else
        check(!support.serialize(&invalid, payload, rep) && payload.length == 0, "reject bound");
        check(support.calculate_serialized_size(&invalid, rep) == 0, "reject invalid size");
        check(!support.serialize(&input, small, rep) && small.length == 0, "reject small buffer");
        check(support.serialize(&input, payload, rep), "valid input before truncation");
#endif
        payload.length -= 1;
        auto output = sample(0), before = output;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        check(!support.deserialize(&payload, &output), "reject truncated payload");
#else
        check(!support.deserialize(payload, &output), "reject truncated payload");
#endif
        check(output == before, "failed decode must not mutate destination");
        rt::InstanceHandle_t handle;
        std::memset(handle.value, 0x55, 16);
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        check(!support.getKey(&invalid, &handle, false), "reject invalid key input");
#else
        check(!support.compute_key(&invalid, handle, false), "reject invalid key input");
#endif
        for (int i = 0; i < 16; ++i) check(handle.value[i] == 0x55, "failed key must not mutate handle");
    }
    std::cout << "B3_INVALID PASS\n";
}
#endif

int dds_exchange(bool writer_role, unsigned domain, const std::string& topic_name, int rep) {
    auto* factory = fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipantQos pq;
    pq.transport().use_builtin_transports = false;
    pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
    auto* participant = factory->create_participant(domain, pq);
    check(participant != nullptr, "participant");
    fd::TypeSupport support(new SampleSupport());
    check(support.register_type(participant) == OK, "register type");
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    auto* topic = participant->create_topic(topic_name, support.get_type_name(), fd::TOPIC_QOS_DEFAULT);
#else
    auto* topic = participant->create_topic(topic_name, support.get_type_name(), fd::TOPIC_QOS_DEFAULT);
#endif
    check(topic != nullptr, "topic");
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool passed = false;
    if (writer_role) {
        auto* publisher = participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
        check(publisher != nullptr, "publisher");
        fd::DataWriterQos qos;
        qos.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        qos.history().kind = fd::KEEP_LAST_HISTORY_QOS; qos.history().depth = 16;
        qos.representation().m_value = {static_cast<fd::DataRepresentationId_t>(rep)};
        auto* writer = publisher->create_datawriter(topic, qos);
        check(writer != nullptr, "writer");
        while (std::chrono::steady_clock::now() < deadline) {
            fd::PublicationMatchedStatus status;
            check(writer->get_publication_matched_status(status) == OK, "writer match status");
            if (status.current_count > 0) {
                for (int i = 0; i < 10; ++i) {
                    auto value = sample(i);
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
                    check(writer->write(&value), "write");
#else
                    check(writer->write(&value) == OK, "write");
#endif
                }
                check(writer->wait_for_acknowledgments({5, 0}) == OK, "acknowledgments");
                passed = true; break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    } else {
        auto* subscriber = participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        check(subscriber != nullptr, "subscriber");
        fd::DataReaderQos qos;
        qos.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        qos.history().kind = fd::KEEP_LAST_HISTORY_QOS; qos.history().depth = 16;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        qos.type_consistency().representation.m_value = {static_cast<fd::DataRepresentationId_t>(rep)};
#else
        qos.representation().m_value = {static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        auto* reader = subscriber->create_datareader(topic, qos);
        check(reader != nullptr, "reader");
        std::array<bool, 10> seen{};
        while (std::chrono::steady_clock::now() < deadline) {
            Sample value; fd::SampleInfo info;
            auto code = reader->take_next_sample(&value, &info);
            if (code == OK && info.valid_data) {
                int index = static_cast<int>(value.id()) - 17;
                check(index >= 0 && index < 10 && value == sample(index), "DDS sample value"); seen[index] = true;
                if (std::all_of(seen.begin(), seen.end(), [](bool v) { return v; })) { passed = true; break; }
            } else check(code == OK || code == NO_DATA, "take");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    check(participant->delete_contained_entities() == OK, "delete entities");
    check(factory->delete_participant(participant) == OK, "delete participant");
    check(passed, "DDS exchange timed out");
    std::cout << "B3_DDS PASS role=" << (writer_role ? "writer" : "reader") << " rep=" << rep << '\n';
    return 0;
}
int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string(argv[1]) == "--dds")
            return dds_exchange(std::string(argv[2]) == "writer", static_cast<unsigned>(std::stoul(argv[3])), argv[4], std::stoi(argv[5]));
        check(argc == 1, "usage: [--dds writer|reader DOMAIN TOPIC REP]");
        run_contracts();
#ifdef B3_OWN_BACKEND
        invalid_contract();
        concurrent_contract();
#endif
        std::cout << "B3_CONTRACT PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "B3_FAIL " << error.what() << '\n'; return 1; }
}
