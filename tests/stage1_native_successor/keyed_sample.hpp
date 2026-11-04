#pragma once
// Test-only keyed sample for the device QoS/keyed matrix. The 4-byte key is
// serialized big-endian into the instance handle (key size <= 16 bytes).
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <cstdint>
#include <cstring>

namespace stage1_keyed {
struct Sample {
    std::uint32_t key = 0, seq = 0;
    std::uint64_t run = 0;
    std::uint32_t side = 0, tag = 0;
};
class Type final : public eprosima::fastdds::dds::TopicDataType {
public:
    Type()
    {
        setName("FastDDSDSoftBusKeyed214"); m_typeSize = 4 + 4 + 4 + 8 + 4 + 4 + 8; m_isGetKeyDefined = true;
        auto_fill_type_object(false); auto_fill_type_information(false);
    }
    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override
    {
        try {
            auto& v = *static_cast<Sample*>(data);
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->max_size);
            eprosima::fastcdr::Cdr cdr(buffer, eprosima::fastcdr::Cdr::LITTLE_ENDIANNESS, eprosima::fastcdr::XCDRv1);
            cdr.serialize_encapsulation(); cdr << v.key << v.seq << v.run << v.side << v.tag;
            payload->encapsulation = CDR_LE; payload->length = static_cast<std::uint32_t>(cdr.get_serialized_data_length()); return true;
        } catch (...) { return false; }
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload, void* data) override
    {
        try {
            auto& v = *static_cast<Sample*>(data);
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->length);
            eprosima::fastcdr::Cdr cdr(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::XCDRv1);
            cdr.read_encapsulation(); cdr >> v.key >> v.seq >> v.run >> v.side >> v.tag; return true;
        } catch (...) { return false; }
    }
    std::function<std::uint32_t()> getSerializedSizeProvider(void*) override { return [] { return 4u + 4 + 4 + 8 + 4 + 4 + 8; }; }
    void* createData() override { return new Sample; }
    void deleteData(void* data) override { delete static_cast<Sample*>(data); }
    bool getKey(void* data, eprosima::fastrtps::rtps::InstanceHandle_t* handle, bool = false) override
    {
        if (!data || !handle) return false;
        const auto key = static_cast<Sample*>(data)->key;
        // Keys are nonzero by contract, so the handle is always "defined".
        for (unsigned i = 0; i < 16; ++i)
            handle->value[i] = i < 4 ? static_cast<std::uint8_t>(key >> (8 * (3 - i))) : std::uint8_t(0);
        return true;
    }
};
} // namespace stage1_keyed
