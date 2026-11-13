// Runtime adapter emitted by the B3 backend. Uses public Fast DDS/Fast CDR APIs.
#pragma once
#ifndef STANDARDS_B3_SUPPORT_V1_HPP
#define STANDARDS_B3_SUPPORT_V1_HPP
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <fastcdr/Cdr.h>
#include <fastcdr/CdrSizeCalculator.hpp>
#include <fastcdr/exceptions/BadParamException.h>
#if B3_FASTDDS_214
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastrtps/utils/md5.h>
#else
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <fastdds/utils/md5.hpp>
#endif

namespace b3 {
namespace cdr = eprosima::fastcdr;
namespace dds = eprosima::fastdds::dds;
#if B3_FASTDDS_214
namespace rtps = eprosima::fastrtps::rtps;
using Md5 = ::MD5;
#else
namespace rtps = eprosima::fastdds::rtps;
using Md5 = eprosima::fastdds::MD5;
#endif
template<class T> struct Traits;
inline void require(bool value) {
    if (!value) throw cdr::exception::BadParamException("B3 value violates its IDL type");
}
inline bool representation(dds::DataRepresentationId_t value) {
    return value == dds::XCDR_DATA_REPRESENTATION || value == dds::XCDR2_DATA_REPRESENTATION;
}
inline cdr::CdrVersion version(dds::DataRepresentationId_t value) {
    return value == dds::XCDR_DATA_REPRESENTATION ? cdr::CdrVersion::XCDRv1 : cdr::CdrVersion::XCDRv2;
}
inline cdr::EncodingAlgorithmFlag encoding(cdr::CdrVersion value, bool appendable) {
    return value == cdr::CdrVersion::XCDRv1 ? cdr::EncodingAlgorithmFlag::PLAIN_CDR :
           appendable ? cdr::EncodingAlgorithmFlag::DELIMIT_CDR2 : cdr::EncodingAlgorithmFlag::PLAIN_CDR2;
}
template<class T> class B3Support : public dds::TopicDataType {
    using Meta = Traits<T>;
    bool write_payload(const T* data, rtps::SerializedPayload_t& payload, dds::DataRepresentationId_t rep) {
        payload.length = 0;
        if (!data || !payload.data || !representation(rep) || !Meta::valid(*data)) return false;
        try {
            cdr::FastBuffer buffer(reinterpret_cast<char*>(payload.data), payload.max_size);
            cdr::Cdr stream(buffer, cdr::Cdr::DEFAULT_ENDIAN, version(rep));
            stream.set_encoding_flag(encoding(version(rep), Meta::appendable));
            stream.serialize_encapsulation();
            Meta::write(stream, *data);
            stream.set_dds_cdr_options({0, 0});
            payload.length = static_cast<uint32_t>(stream.get_serialized_data_length());
            payload.encapsulation = stream.endianness() == cdr::Cdr::BIG_ENDIANNESS ? 0 : 1;
            return true;
        } catch (const cdr::exception::Exception&) { return false; }
        catch (const std::exception&) { return false; }
    }
    bool read_payload(rtps::SerializedPayload_t& payload, T* data) {
        if (!data || !payload.data || payload.length < 4 || payload.length > payload.max_size) return false;
        try {
            cdr::FastBuffer buffer(reinterpret_cast<char*>(payload.data), payload.length);
            cdr::Cdr stream(buffer);
            stream.read_encapsulation();
            T temporary;
            Meta::read(stream, temporary);
            if (!Meta::valid(temporary)) return false;
            *data = std::move(temporary);
            return true;
        } catch (const cdr::exception::Exception&) { return false; }
        catch (const std::exception&) { return false; }
    }
    static uint32_t size_payload(const T* data, dds::DataRepresentationId_t rep) {
        if (!data || !representation(rep) || !Meta::valid(*data)) return 0;
        try {
            cdr::CdrSizeCalculator calculator(version(rep));
            size_t alignment = 0;
            return static_cast<uint32_t>(Meta::size(calculator, *data, alignment) + 4);
        } catch (const cdr::exception::Exception&) { return 0; }
        catch (const std::exception&) { return 0; }
    }
    bool key_payload(const T* data, rtps::InstanceHandle_t& handle, bool force_md5) {
        if (!Meta::has_key || !data || !Meta::valid(*data)) return false;
        try {
            std::vector<unsigned char> bytes((std::max)(size_t(16), Meta::key_max), 0);
            cdr::FastBuffer buffer(reinterpret_cast<char*>(bytes.data()), bytes.size());
            cdr::Cdr stream(buffer, cdr::Cdr::BIG_ENDIANNESS,
                    B3_FASTDDS_214 ? cdr::CdrVersion::XCDRv1 : cdr::CdrVersion::XCDRv2);
            Meta::key(stream, *data);
            if (force_md5 || Meta::key_max > 16) {
                Md5 digest;
                digest.init();
                digest.update(bytes.data(), static_cast<unsigned int>(stream.get_serialized_data_length()));
                digest.finalize();
                std::memcpy(handle.value, digest.digest, 16);
            } else std::memcpy(handle.value, bytes.data(), 16);
            return true;
        } catch (const cdr::exception::Exception&) { return false; }
        catch (const std::exception&) { return false; }
    }
public:
    using type = T;
    B3Support() {
#if B3_FASTDDS_214
        setName(Meta::name()); m_typeSize = Meta::max_size; m_isGetKeyDefined = Meta::has_key;
#else
        set_name(Meta::name()); max_serialized_type_size = Meta::max_size; is_compute_key_provided = Meta::has_key;
#endif
    }
#if B3_FASTDDS_214
    bool serialize(void* data, rtps::SerializedPayload_t* payload) override {
        return payload && write_payload(static_cast<T*>(data), *payload, dds::XCDR_DATA_REPRESENTATION);
    }
    bool serialize(void* data, rtps::SerializedPayload_t* payload, dds::DataRepresentationId_t rep) override {
        return payload && write_payload(static_cast<T*>(data), *payload, rep);
    }
    bool deserialize(rtps::SerializedPayload_t* payload, void* data) override {
        return payload && read_payload(*payload, static_cast<T*>(data));
    }
    std::function<uint32_t()> getSerializedSizeProvider(void* data) override {
        return getSerializedSizeProvider(data, dds::XCDR_DATA_REPRESENTATION);
    }
    std::function<uint32_t()> getSerializedSizeProvider(void* data, dds::DataRepresentationId_t rep) override {
        return [data, rep] { return size_payload(static_cast<T*>(data), rep); };
    }
    bool getKey(void* data, rtps::InstanceHandle_t* handle, bool force_md5 = false) override {
        return handle && key_payload(static_cast<T*>(data), *handle, force_md5);
    }
    void* createData() override { return new T(); }
    void deleteData(void* data) override { delete static_cast<T*>(data); }
#else
    bool serialize(const void* const data, rtps::SerializedPayload_t& payload, dds::DataRepresentationId_t rep) override {
        return write_payload(static_cast<const T*>(data), payload, rep);
    }
    bool deserialize(rtps::SerializedPayload_t& payload, void* data) override { return read_payload(payload, static_cast<T*>(data)); }
    uint32_t calculate_serialized_size(const void* const data, dds::DataRepresentationId_t rep) override {
        return size_payload(static_cast<const T*>(data), rep);
    }
    bool compute_key(const void* const data, rtps::InstanceHandle_t& handle, bool force_md5 = false) override {
        return key_payload(static_cast<const T*>(data), handle, force_md5);
    }
    bool compute_key(rtps::SerializedPayload_t& payload, rtps::InstanceHandle_t& handle, bool force_md5 = false) override {
        T data;
        return read_payload(payload, &data) && key_payload(&data, handle, force_md5);
    }
    void* create_data() override { return new T(); }
    void delete_data(void* data) override { delete static_cast<T*>(data); }
#endif
    bool is_bounded() const override { return true; }
};
} // namespace b3
#endif // STANDARDS_B3_SUPPORT_V1_HPP
