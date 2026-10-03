#pragma once
// Version-bound user sample only; no DDS/RTPS or Transport protocol adaptation.
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <array>
#include <cstdint>

struct LayerSample {
    std::uint64_t run = 0;
    std::uint32_t side = 0, ordinal = 0;
    std::array<std::uint8_t, 32> payload{};
};
class LayerSampleType final : public eprosima::fastdds::dds::TopicDataType {
public:
    LayerSampleType() {
        setName("FastDDSDSoftBusLayerSample214"); m_typeSize=64; m_isGetKeyDefined=false;
        auto_fill_type_object(false); auto_fill_type_information(false);
    }
    bool serialize(void* data, eprosima::fastrtps::rtps::SerializedPayload_t* payload) override {
        try {
            auto& value=*static_cast<LayerSample*>(data);
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data),payload->max_size);
            eprosima::fastcdr::Cdr cdr(buffer,eprosima::fastcdr::Cdr::LITTLE_ENDIANNESS,eprosima::fastcdr::XCDRv1);
            cdr.serialize_encapsulation(); cdr<<value.run<<value.side<<value.ordinal;
            cdr.serialize_array(value.payload.data(),value.payload.size());
            payload->encapsulation=CDR_LE; payload->length=static_cast<std::uint32_t>(cdr.get_serialized_data_length());return true;
        }catch(...){return false;}
    }
    bool deserialize(eprosima::fastrtps::rtps::SerializedPayload_t* payload,void* data) override {
        try {
            auto& value=*static_cast<LayerSample*>(data);
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data),payload->length);
            eprosima::fastcdr::Cdr cdr(buffer,eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,eprosima::fastcdr::XCDRv1);
            cdr.read_encapsulation(); cdr>>value.run>>value.side>>value.ordinal;
            cdr.deserialize_array(value.payload.data(),value.payload.size());return true;
        }catch(...){return false;}
    }
    std::function<std::uint32_t()> getSerializedSizeProvider(void*) override {return []{return 64u;};}
    void* createData() override {return new LayerSample;}
    void deleteData(void* data) override {delete static_cast<LayerSample*>(data);}
    bool getKey(void*,eprosima::fastrtps::rtps::InstanceHandle_t*,bool=false) override {return false;}
};
