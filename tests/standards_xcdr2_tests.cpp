// Public API contracts for the opt-in legacy DynamicData framing patch.
// Run separately with the process gate unset, 0, 1, and non-exact values.
#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicDataFactory.h>
#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace eprosima::fastrtps::types;
using eprosima::fastrtps::rtps::SerializedPayload_t;
using eprosima::fastdds::dds::DataRepresentationId_t;
static unsigned checks = 0;
static void check(bool value, const char* label)
{
    if (!value) throw std::runtime_error(label);
    ++checks;
}
static DynamicType_ptr type(bool text = false, const char* extent = "appendable",
        bool unsupported = false, unsigned first_id = 0)
{
    auto* factory = DynamicTypeBuilderFactory::get_instance();
    auto* builder = factory->create_struct_builder();
    builder->set_name("FramingRecord");
    builder->apply_annotation("extensibility", "value", extent);
    builder->add_member(0, "side", unsupported ? factory->create_uint64_type() : factory->create_uint32_type());
    builder->add_member(1, "n", factory->create_uint32_type());
    if (first_id) check(builder->apply_annotation_to_member(0, first_id == 1 ? "key" : "optional", "value", "true") ==
            ReturnCode_t::RETCODE_OK, "unsupported annotation applied");
    if (text) builder->add_member(first_id + 2, "text", factory->create_string_type(64));
    auto result = builder->build();
    factory->delete_builder(builder);
    return result;
}
struct Sample
{
    DynamicData* data;
    explicit Sample(DynamicType_ptr t) : data(DynamicDataFactory::get_instance()->create_data(t)) {}
    ~Sample() { DynamicDataFactory::get_instance()->delete_data(data); }
};
static bool decode(DynamicPubSubType& support, DynamicData* data, const std::vector<unsigned char>& bytes)
{
    SerializedPayload_t payload(static_cast<uint32_t>(bytes.size()));
    std::memcpy(payload.data, bytes.data(), bytes.size());
    payload.length = static_cast<uint32_t>(bytes.size());
    return support.deserialize(&payload, data);
}
int main(int argc, char** argv)
{
    try
    {
        const bool on = argc == 2 && std::string(argv[1]) == "on";
        auto t = type(); DynamicPubSubType support(t); Sample in(t), out(t);
        check(in.data->set_uint32_value(7, 0) == ReturnCode_t::RETCODE_OK, "set side");
        check(in.data->set_uint32_value(42, 1) == ReturnCode_t::RETCODE_OK, "set n");
        const std::vector<unsigned char> xcdr1{0,1,0,0,7,0,0,0,42,0,0,0};
        const std::vector<unsigned char> xcdr2{0,9,0,0,8,0,0,0,7,0,0,0,42,0,0,0};
        const std::vector<unsigned char> legacy{0,9,0,0,7,0,0,0,42,0,0,0};
        for (auto rep : {DataRepresentationId_t::XCDR_DATA_REPRESENTATION,
                DataRepresentationId_t::XCDR2_DATA_REPRESENTATION})
        {
            const auto& expected = rep == DataRepresentationId_t::XCDR_DATA_REPRESENTATION ? xcdr1 : on ? xcdr2 : legacy;
            const auto size = support.getSerializedSizeProvider(in.data)(); // Writer calls without representation.
            SerializedPayload_t payload(size);
            check(support.serialize(in.data, &payload, rep), "serialize");
            check(payload.length == expected.size() && !std::memcmp(payload.data, expected.data(), expected.size()), "wire bytes");
            check(size >= payload.length && support.m_typeSize >= payload.length, "size bounds");
            check(decode(support, out.data, expected), "roundtrip decode");
            check(out.data->get_uint32_value(0) == 7 && out.data->get_uint32_value(1) == 42, "roundtrip values");
        }
        if (on)
        {
            check(!decode(support, out.data, legacy), "missing DHEADER rejected");
            check(decode(support, out.data, {0,8,0,0,0,0,0,8,0,0,0,7,0,0,0,42}), "big endian frame");
            check(out.data->get_uint32_value(0) == 7 && out.data->get_uint32_value(1) == 42, "big endian values");
            for (size_t n = 0; n < xcdr2.size(); ++n)
                check(!decode(support, out.data, std::vector<unsigned char>(xcdr2.begin(), xcdr2.begin()+n)), "truncation rejected");
            for (unsigned length : {0u, 4u, 7u, 9u, 255u})
            {
                auto bad = xcdr2; bad[4] = static_cast<unsigned char>(length);
                check(!decode(support, out.data, bad), "invalid DHEADER rejected");
            }
            auto trailing = xcdr2; trailing[4] = 12; trailing.insert(trailing.end(), {99,0,0,0});
            check(decode(support, out.data, trailing), "unknown trailing member skipped");
            SerializedPayload_t small(12); small.length = 12;
            check(!support.serialize(in.data, &small, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION), "small buffer fails");
            check(small.length == 0, "failed serialization clears length");
            for (auto unsupported : {type(false, "FINAL"), type(false, "MUTABLE"), type(false, "appendable", true),
                    type(false, "appendable", false, 1), type(false, "appendable", false, 2)})
            {
                DynamicPubSubType rejected(unsupported); Sample sample(unsupported); SerializedPayload_t bytes(128);
                check(!rejected.serialize(sample.data, &bytes, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION), "unsupported encode fails");
                check(!decode(rejected, sample.data, xcdr2), "unsupported decode fails");
                check(rejected.getSerializedSizeProvider(sample.data, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION)() == 0, "unsupported size fails");
            }
            auto st = type(true); DynamicPubSubType strings(st); Sample sin(st), sout(st);
            sin.data->set_uint32_value(7,0); sin.data->set_uint32_value(42,1);
            for (size_t length : {size_t(0), size_t(1), size_t(3), size_t(4), size_t(63), size_t(64)})
            {
                const std::string text(length, 'x'); sin.data->set_string_value(text,2);
                const auto size = strings.getSerializedSizeProvider(sin.data)(); SerializedPayload_t payload(size);
                check(strings.serialize(sin.data, &payload, DataRepresentationId_t::XCDR2_DATA_REPRESENTATION), "string serialize");
                check(payload.length == 21 + length && payload.length <= strings.m_typeSize, "string size");
                std::vector<unsigned char> bytes(payload.data,payload.data+payload.length);
                check(decode(strings, sout.data, bytes) && sout.data->get_string_value(2) == text, "string roundtrip");
                auto bad = bytes; bad[16] = 0; bad[17] = bad[18] = bad[19] = 0;
                check(!decode(strings,sout.data,bad), "zero string length rejected");
                bad=bytes; bad[16]=66; check(!decode(strings,sout.data,bad), "overbound string rejected");
                bad=bytes; bad.back()='x'; check(!decode(strings,sout.data,bad), "missing null rejected");
            }
            sin.data->set_string_value(std::string("a\0b",3),2);
            SerializedPayload_t bytes(128);
            check(!strings.serialize(sin.data,&bytes,DataRepresentationId_t::XCDR2_DATA_REPRESENTATION), "embedded null rejected");
        }
        // The first construction latches the gate. Changing it must not change wire behavior.
#ifdef _WIN32
        _putenv_s("FASTDDS_XCDR2_DYNAMIC_FRAMING", on ? "0" : "1");
#else
        setenv("FASTDDS_XCDR2_DYNAMIC_FRAMING", on ? "0" : "1", 1);
#endif
        SerializedPayload_t final(32);
        check(support.serialize(in.data,&final,DataRepresentationId_t::XCDR2_DATA_REPRESENTATION), "latched gate serialize");
        const auto& expected = on ? xcdr2 : legacy;
        check(final.length == expected.size() && !std::memcmp(final.data,expected.data(),expected.size()), "gate latched once");
        std::cout << "PASS checks=" << checks << " gate=" << (on ? "on" : "off") << '\n';
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
