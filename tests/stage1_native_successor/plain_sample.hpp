#pragma once
// Test-only plain (fixed-size, bounded) sample for DataWriter::loan_sample in the feature
// matrix. Fast DDS lends writer samples only for plain types. No DDS/RTPS or Transport
// protocol adaptation: over DSoftBus the loaned sample is serialized like any other.
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <cstdint>
#include <cstring>

namespace stage1_plain {
constexpr std::uint32_t kBytes = 4000;
struct Sample {
    std::uint64_t run;
    std::uint32_t side;
    std::uint32_t ordinal;
    std::uint8_t bytes[kBytes];
};
constexpr std::uint32_t kEncapsulation = 4;
inline std::uint8_t pattern(std::uint64_t run, std::uint32_t side, std::uint32_t ordinal, std::uint32_t i)
{
    return static_cast<std::uint8_t>((i * 7u) ^ (ordinal * 13u) ^ (side * 0x5Bu) ^ static_cast<std::uint32_t>(run));
}
inline void fill(Sample& s, std::uint64_t run, std::uint32_t side, std::uint32_t ordinal)
{
    s.run = run; s.side = side; s.ordinal = ordinal;
    for (std::uint32_t i = 0; i < kBytes; ++i) s.bytes[i] = pattern(run, side, ordinal, i);
}
inline bool valid(const Sample& s, std::uint64_t run, std::uint32_t side, std::uint32_t ordinal)
{
    if (s.run != run || s.side != side || s.ordinal != ordinal) return false;
    for (std::uint32_t i = 0; i < kBytes; ++i) if (s.bytes[i] != pattern(run, side, ordinal, i)) return false;
    return true;
}
class Type final : public eprosima::fastdds::dds::TopicDataType {
public:
    Type()
    {
        setName("FastDDSDSoftBusPlainSample214"); m_typeSize = kEncapsulation + sizeof(Sample); m_isGetKeyDefined = false;
        auto_fill_type_object(false); auto_fill_type_information(false);
    }
    // The in-memory layout is the wire layout (little-endian boards), after a CDR_LE header.
    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override
    {
        if (payload->max_size < kEncapsulation + sizeof(Sample)) return false;
        payload->data[0] = 0; payload->data[1] = 1; payload->data[2] = 0; payload->data[3] = 0;
        std::memcpy(payload->data + kEncapsulation, data, sizeof(Sample));
        payload->encapsulation = CDR_LE; payload->length = kEncapsulation + sizeof(Sample); return true;
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override
    {
        if (payload->length != kEncapsulation + sizeof(Sample) || payload->data[1] != 1) return false;
        std::memcpy(data, payload->data + kEncapsulation, sizeof(Sample)); return true;
    }
    std::function<std::uint32_t()> getSerializedSizeProvider(void*) override
    { return [] { return static_cast<std::uint32_t>(kEncapsulation + sizeof(Sample)); }; }
    void* createData() override { return new Sample(); }
    void deleteData(void* data) override { delete static_cast<Sample*>(data); }
    bool getKey(void*, eprosima::fastrtps::rtps::InstanceHandle_t*, bool = false) override { return false; }
    bool is_bounded() const override { return true; }
    bool is_plain() const override { return true; }
    // Fast DDS 2.14 loan_sample() checks the overload for the writer's data representation.
    bool is_plain(eprosima::fastdds::dds::DataRepresentationId_t) const override { return true; }
};
} // namespace stage1_plain
