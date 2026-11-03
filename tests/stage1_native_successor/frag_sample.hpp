#pragma once
// Test-only large sample that forces RTPS DATA_FRAG over the DSoftBus transport.
// No DDS/RTPS or Transport protocol adaptation.
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <cstdint>
#include <vector>

namespace stage1_frag {
// 150000 bytes > 65536-byte transport maximum, so Fast DDS must fragment it.
constexpr std::uint32_t kBytes = 150000;
constexpr std::uint32_t kHeader = 4 + 8 + 4 + 4; // encapsulation + run + side + length
inline std::uint8_t pattern(std::uint64_t run, std::uint32_t side, std::uint32_t i)
{
    return static_cast<std::uint8_t>((i * 31u) ^ (i >> 8) ^ (side * 0x5Bu) ^ static_cast<std::uint32_t>(run));
}
struct Sample {
    std::uint64_t run = 0;
    std::uint32_t side = 0;
    std::vector<std::uint8_t> bytes;
};
inline Sample make(std::uint64_t run, std::uint32_t side)
{
    Sample s; s.run = run; s.side = side; s.bytes.resize(kBytes);
    for (std::uint32_t i = 0; i < kBytes; ++i) s.bytes[i] = pattern(run, side, i);
    return s;
}
inline bool valid(const Sample& s, std::uint64_t run, std::uint32_t side)
{
    if (s.run != run || s.side != side || s.bytes.size() != kBytes) return false;
    for (std::uint32_t i = 0; i < kBytes; ++i) if (s.bytes[i] != pattern(run, side, i)) return false;
    return true;
}
class Type final : public eprosima::fastdds::dds::TopicDataType {
public:
    Type()
    {
        setName("FastDDSDSoftBusFragSample214"); m_typeSize = kHeader + kBytes + 8; m_isGetKeyDefined = false;
        auto_fill_type_object(false); auto_fill_type_information(false);
    }
    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override
    {
        try {
            auto& v = *static_cast<Sample*>(data);
            if (v.bytes.size() != kBytes) return false;
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->max_size);
            eprosima::fastcdr::Cdr cdr(buffer, eprosima::fastcdr::Cdr::LITTLE_ENDIANNESS, eprosima::fastcdr::XCDRv1);
            cdr.serialize_encapsulation(); cdr << v.run << v.side << static_cast<std::uint32_t>(v.bytes.size());
            cdr.serialize_array(v.bytes.data(), v.bytes.size());
            payload->encapsulation = CDR_LE; payload->length = static_cast<std::uint32_t>(cdr.get_serialized_data_length()); return true;
        } catch (...) { return false; }
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override
    {
        try {
            auto& v = *static_cast<Sample*>(data);
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->length);
            eprosima::fastcdr::Cdr cdr(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::XCDRv1);
            std::uint32_t n = 0; cdr.read_encapsulation(); cdr >> v.run >> v.side >> n;
            if (n != kBytes) return false;
            v.bytes.resize(n); cdr.deserialize_array(v.bytes.data(), n); return true;
        } catch (...) { return false; }
    }
    std::function<std::uint32_t()> getSerializedSizeProvider(void*) override { return [] { return kHeader + kBytes; }; }
    void* createData() override { return new Sample; }
    void deleteData(void* data) override { delete static_cast<Sample*>(data); }
    bool getKey(void*, eprosima::fastrtps::rtps::InstanceHandle_t*, bool = false) override { return false; }
};
} // namespace stage1_frag
