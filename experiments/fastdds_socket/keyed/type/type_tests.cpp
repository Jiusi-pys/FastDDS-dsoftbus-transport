#include "DSoftBusKeyedSamplePubSubTypes.hpp"
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/dds/xtypes/type_representation/ITypeObjectRegistry.hpp>
#include <fastdds/dds/xtypes/type_representation/TypeObject.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
namespace dds = eprosima::fastdds::dds;
namespace rtps = eprosima::fastdds::rtps;
namespace xtypes = eprosima::fastdds::dds::xtypes;

static void Require(bool condition, const char* predicate)
{
    if (!condition) throw std::runtime_error(predicate);
}

static DSoftBusKeyedSample Sample(uint32_t key)
{
    DSoftBusKeyedSample result;
    result.key(key);
    result.run(UINT64_C(0x0102030405060708));
    result.writer(7);
    result.ordinal(9);
    for (size_t i = 0; i < result.payload().size(); ++i)
        result.payload()[i] = static_cast<uint8_t>(i * 7 + 3);
    return result;
}

static rtps::InstanceHandle_t Handle(DSoftBusKeyedSamplePubSubType& type,
        const DSoftBusKeyedSample& sample, bool md5 = false)
{
    rtps::InstanceHandle_t handle;
    Require(type.compute_key(&sample, handle, md5), "compute_key(object)");
    return handle;
}

static void KeyContract()
{
    static_assert(std::is_same_v<decltype(DSoftBusKeyedSample{}.key()), uint32_t&>);
    DSoftBusKeyedSamplePubSubType type;
    Require(type.is_compute_key_provided, "is_compute_key_provided");
    Require(type.is_bounded(), "bounded fixed type");
    Require(type.max_serialized_type_size < 128, "small bounded serialized maximum");
    const auto a = Handle(type, Sample(0x12345678));
    const auto b = Handle(type, Sample(0x12345679));
    Require(a != b, "different keys have different handles");
    const std::array<uint8_t, 16> expected{{0x12, 0x34, 0x56, 0x78}};
    for (size_t i = 0; i < expected.size(); ++i)
        Require(a.value[i] == expected[i], "uint32 key is big endian with zero padding");
    Require(Handle(type, Sample(0)) != Handle(type, Sample(UINT32_MAX)), "key uint32 boundaries distinct");
}

static void NonKeyContract()
{
    DSoftBusKeyedSamplePubSubType type;
    const auto original = Sample(17);
    for (bool md5 : {false, true})
    {
        const auto expected = Handle(type, original, md5);
        auto changed = original;
        changed.run(UINT64_MAX);
        Require(Handle(type, changed, md5) == expected, "run is not a key");
        changed = original;
        changed.writer(UINT32_MAX);
        Require(Handle(type, changed, md5) == expected, "Writer label is not a key");
        changed = original;
        changed.ordinal(UINT32_MAX);
        Require(Handle(type, changed, md5) == expected, "ordinal is not a key");
        changed = original;
        changed.payload().fill(0xff);
        Require(Handle(type, changed, md5) == expected, "payload is not a key");
    }
}

static void Md5Contract()
{
    DSoftBusKeyedSamplePubSubType type;
    // Independent known-answer MD5 of the four bytes 12 34 56 78.
    const std::array<uint8_t, 16> expected{{0x89,0x1a,0x26,0xe0,0x58,0x1a,0x7f,0x2c,
        0x9a,0x57,0x4c,0xef,0xf1,0x54,0x9e,0xe1}};
    const auto actual = Handle(type, Sample(0x12345678), true);
    for (size_t i = 0; i < expected.size(); ++i)
        Require(actual.value[i] == expected[i], "forced MD5 known key digest");
    Require(actual != Handle(type, Sample(0x12345678)), "forced hash differs from raw handle");
}

static void RoundtripContract()
{
    DSoftBusKeyedSamplePubSubType type;
    for (auto representation : {dds::XCDR_DATA_REPRESENTATION, dds::XCDR2_DATA_REPRESENTATION})
    {
        for (uint32_t key : {0u, 17u, UINT32_MAX})
        {
            auto input = Sample(key);
            input.run(UINT64_MAX);
            rtps::SerializedPayload_t payload(type.max_serialized_type_size);
            Require(type.serialize(&input, payload, representation), "serialize complete sample");
            Require(payload.length <= type.max_serialized_type_size, "serialized upper bound");
            Require(payload.length == type.calculate_serialized_size(&input, representation), "actual size equals calculated size");
            DSoftBusKeyedSample output;
            Require(type.deserialize(payload, &output), "deserialize complete sample");
            Require(input == output, "roundtrip preserves key and every non-key field");
            for (bool md5 : {false, true})
            {
                rtps::InstanceHandle_t serialized_handle;
                Require(type.compute_key(payload, serialized_handle, md5), "compute_key(serialized)");
                Require(serialized_handle == Handle(type, input, md5), "serialized and object keys agree");
            }
            const auto complete_length = payload.length;
            payload.length = complete_length - 1;
            Require(!type.deserialize(payload, &output), "truncated payload rejected");
            rtps::InstanceHandle_t invalid_handle;
            Require(!type.compute_key(payload, invalid_handle), "truncated serialized key computation rejected");
            std::cout << "TYPE_BOUND representation=" << representation << " key=" << key
                      << " serialized=" << complete_length << " maximum=" << type.max_serialized_type_size << '\n';
        }
    }
}

static void TypeObjectContract()
{
    DSoftBusKeyedSamplePubSubType type;
    type.register_type_object_representation();
    xtypes::CompleteTypeObject object;
    auto& registry = dds::DomainParticipantFactory::get_instance()->type_object_registry();
    Require(registry.get_complete_type_object(type.type_identifiers(), object) == dds::RETCODE_OK,
            "complete generated TypeObject registered");
    const auto& structure = object.struct_type();
    Require((structure.struct_flags() & xtypes::IS_FINAL) != 0, "final extensibility registered");
    Require(structure.member_seq().size() == 5, "complete TypeObject has five members");
    size_t keys = 0;
    for (const auto& member : structure.member_seq())
    {
        const auto name = member.detail().name().to_string();
        const bool is_key = (member.common().member_flags() & xtypes::IS_KEY) != 0;
        Require(is_key == (name == "key"), "sole registered key is key field");
        if (is_key) ++keys;
        std::cout << "TYPE_MEMBER name=" << name << " key=" << is_key << '\n';
    }
    Require(keys == 1, "exactly one CompleteTypeObject key flag");
}

static void WithKeyContract()
{
    auto* factory = dds::DomainParticipantFactory::get_instance();
    auto qos = dds::PARTICIPANT_QOS_DEFAULT;
    qos.transport().use_builtin_transports = false;
    auto udp = std::make_shared<rtps::UDPv4TransportDescriptor>();
    udp->interfaceWhiteList.push_back("127.0.0.1");
    qos.transport().user_transports.push_back(udp);
    auto* participant = factory->create_participant(199, qos);
    Require(participant != nullptr, "reference-only Participant created");
    bool okay = false;
    try
    {
        dds::TypeSupport type(new DSoftBusKeyedSamplePubSubType);
        Require(type.register_type(participant) == dds::RETCODE_OK, "real Participant type registration");
        auto* topic = participant->create_topic("mdds-keyed-type-contract", type.get_type_name(), dds::TOPIC_QOS_DEFAULT);
        auto* publisher = participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
        auto* subscriber = participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        Require(topic && publisher && subscriber, "real endpoint parents created");
        auto writer_qos = dds::DATAWRITER_QOS_DEFAULT;
        writer_qos.data_sharing().off();
        auto reader_qos = dds::DATAREADER_QOS_DEFAULT;
        reader_qos.data_sharing().off();
        auto* writer = publisher->create_datawriter(topic, writer_qos);
        auto* reader = subscriber->create_datareader(topic, reader_qos);
        Require(writer && reader, "real keyed Writer and Reader created");
        // RTPS EntityKind discriminators: user Writer with key 0x02, Reader with key 0x07.
        Require(writer->guid().entityId.value[3] == 0x02, "actual Writer is WITH_KEY");
        Require(reader->guid().entityId.value[3] == 0x07, "actual Reader is WITH_KEY");
        auto sample = Sample(17);
        const auto registered = writer->register_instance(&sample);
        Require(registered != rtps::c_InstanceHandle_Unknown, "public Writer register_instance accepts key");
        Require(registered == Handle(*static_cast<DSoftBusKeyedSamplePubSubType*>(type.get()), sample),
                "Writer registered instance uses generated key");
        okay = true;
    }
    catch (...)
    {
        participant->delete_contained_entities();
        factory->delete_participant(participant);
        throw;
    }
    Require(participant->delete_contained_entities() == dds::RETCODE_OK, "endpoint cleanup");
    Require(factory->delete_participant(participant) == dds::RETCODE_OK, "Participant cleanup");
    Require(okay, "WITH_KEY contract completed");
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "one contract selector required");
        const std::string name = argv[1];
        if (name == "key") KeyContract();
        else if (name == "non_key") NonKeyContract();
        else if (name == "md5") Md5Contract();
        else if (name == "roundtrip") RoundtripContract();
        else if (name == "type_object") TypeObjectContract();
        else if (name == "with_key") WithKeyContract();
        else throw std::runtime_error("unknown contract selector");
        std::cout << "KEYED_TYPE_CONTRACT selector=" << name << " status=PASS scope=HOST_REFERENCE_ONLY\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "KEYED_TYPE_CONTRACT status=FAIL predicate=" << error.what() << '\n';
        return 1;
    }
}
