#pragma once
// Fast DDS 2.14 / 3.x compatibility layer for the cross-board device suite (test code only).
// The DSoftBus Transport public target defines FASTDDS_DSOFTBUS_FASTDDS_214 for the 2.14 line;
// without it the 3.x API is used. Only public Fast DDS DDS-layer API is touched here.
#include <fastdds/dds/core/condition/WaitSet.hpp>
#include <fastdds/dds/core/LoanableSequence.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/ReadCondition.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/topic/ContentFilteredTopic.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TopicDataType.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/flowcontrol/FlowControllerDescriptor.hpp>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastrtps/config.h>
#include <fastdds/rtps/common/WriteParams.h>
#include <fastdds/rtps/attributes/ServerAttributes.h>
#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicDataFactory.h>
#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastrtps/types/TypeObjectFactory.h>
#define DSB_FASTDDS_LINE FASTRTPS_VERSION_STR
#else
#include <fastdds/config.hpp>
#include <fastdds/dds/core/Time_t.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilderFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicTypeBuilder.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicDataFactory.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicData.hpp>
#include <fastdds/dds/xtypes/dynamic_types/DynamicPubSubType.hpp>
#include <fastdds/dds/xtypes/dynamic_types/TypeDescriptor.hpp>
#include <fastdds/dds/xtypes/dynamic_types/MemberDescriptor.hpp>
#include <fastdds/dds/xtypes/type_representation/ITypeObjectRegistry.hpp>
#define DSB_FASTDDS_LINE FASTDDS_VERSION_STR
#endif

namespace dsb {
namespace fd = eprosima::fastdds::dds;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
namespace rtps = eprosima::fastrtps::rtps;
using RC = eprosima::fastrtps::types::ReturnCode_t;
using Duration = eprosima::fastrtps::Duration_t;
inline const RC& rc_ok() { static const RC v = RC::RETCODE_OK; return v; }
inline const RC& rc_no_data() { static const RC v = RC::RETCODE_NO_DATA; return v; }
#else
namespace rtps = eprosima::fastdds::rtps;
using RC = eprosima::fastdds::dds::ReturnCode_t;
using Duration = eprosima::fastdds::dds::Duration_t;
inline RC rc_ok() { return eprosima::fastdds::dds::RETCODE_OK; }
inline RC rc_no_data() { return eprosima::fastdds::dds::RETCODE_NO_DATA; }
#endif
inline bool ok(const RC& r) { return r == rc_ok(); }
// DataWriter::write returns bool in 2.14 and ReturnCode_t in 3.x.
inline bool write_ok(bool b) { return b; }
#if !defined(FASTDDS_DSOFTBUS_FASTDDS_214)
inline bool write_ok(RC r) { return r == rc_ok(); }
#endif
inline bool write(fd::DataWriter* w, void* data) { return write_ok(w->write(data)); }
inline bool write(fd::DataWriter* w, void* data, rtps::WriteParams& p) { return write_ok(w->write(data, p)); }
// Renamed QoS members between the lines.
template<class Q> auto& reader_disable_positive_acks(Q& q)
{
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    return q.reliable_reader_qos().disable_positive_ACKs;
#else
    return q.reliable_reader_qos().disable_positive_acks;
#endif
}
template<class Q> auto& heartbeat_period(Q& q)
{
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    return q.reliable_writer_qos().times.heartbeatPeriod;
#else
    return q.reliable_writer_qos().times.heartbeat_period;
#endif
}
inline Duration seconds(int s) { return Duration(s, 0u); }
inline Duration millis(int ms) { return Duration(ms / 1000, static_cast<std::uint32_t>(ms % 1000) * 1000000u); }

// ---- Hand-written TopicDataType: one implementation for both API generations. ----
class SimpleType : public fd::TopicDataType {
public:
    SimpleType(const char* name, std::uint32_t max_bytes, bool keyed, bool plain = false)
        : max_(max_bytes), keyed_(keyed), plain_(plain)
    {
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        setName(name); m_typeSize = max_bytes; m_isGetKeyDefined = keyed;
        auto_fill_type_object(false); auto_fill_type_information(false);
#else
        set_name(name); max_serialized_type_size = max_bytes; is_compute_key_provided = keyed;
#endif
    }
protected:
    // Serialize the sample after a 4-byte CDR_LE encapsulation header; return payload bytes or 0.
    virtual std::uint32_t encode(const void* data, std::uint8_t* out, std::uint32_t capacity) = 0;
    virtual bool decode(const std::uint8_t* in, std::uint32_t length, void* data) = 0;
    virtual std::uint32_t encoded_size(const void* data) = 0;
    virtual void* make() = 0;
    virtual void destroy(void* data) = 0;
    virtual void key_of(const void*, std::uint8_t out[16]) { std::memset(out, 0, 16); }
    bool ser(const void* data, rtps::SerializedPayload_t& p)
    {
        if (p.max_size < 4) return false;
        p.data[0] = 0; p.data[1] = 1; p.data[2] = 0; p.data[3] = 0;
        const auto n = encode(data, p.data + 4, p.max_size - 4);
        if (!n) return false;
        p.encapsulation = CDR_LE; p.length = 4 + n; return true;
    }
    bool de(const rtps::SerializedPayload_t& p, void* data)
    { return p.length >= 4 && p.data[1] == 1 && decode(p.data + 4, p.length - 4, data); }
    bool key_from_payload(const rtps::SerializedPayload_t& p, rtps::InstanceHandle_t& h)
    {
        void* tmp = make(); const bool good = de(p, tmp);
        if (good) key_of(tmp, h.value);
        destroy(tmp); return good;
    }
public:
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    bool serialize(void* data, rtps::SerializedPayload_t* p) override { return ser(data, *p); }
    bool deserialize(rtps::SerializedPayload_t* p, void* data) override { return de(*p, data); }
    std::function<std::uint32_t()> getSerializedSizeProvider(void* data) override
    { return [this, data] { return 4 + encoded_size(data); }; }
    void* createData() override { return make(); }
    void deleteData(void* data) override { destroy(data); }
    bool getKey(void* data, rtps::InstanceHandle_t* h, bool = false) override
    { if (!keyed_ || !data || !h) return false; key_of(data, h->value); return true; }
    bool is_bounded() const override { return plain_; }
    bool is_plain() const override { return plain_; }
    bool is_plain(fd::DataRepresentationId_t) const override { return plain_; }
#else
    bool serialize(const void* const data, rtps::SerializedPayload_t& p, fd::DataRepresentationId_t) override
    { return ser(data, p); }
    bool deserialize(rtps::SerializedPayload_t& p, void* data) override { return de(p, data); }
    std::uint32_t calculate_serialized_size(const void* const data, fd::DataRepresentationId_t) override
    { return 4 + encoded_size(data); }
    void* create_data() override { return make(); }
    void delete_data(void* data) override { destroy(data); }
    bool compute_key(rtps::SerializedPayload_t& p, rtps::InstanceHandle_t& h, bool = false) override
    { return keyed_ && key_from_payload(p, h); }
    bool compute_key(const void* const data, rtps::InstanceHandle_t& h, bool = false) override
    { if (!keyed_ || !data) return false; key_of(data, h.value); return true; }
    bool is_bounded() const override { return plain_; }
    bool is_plain(fd::DataRepresentationId_t) const override { return plain_; }
#endif
private:
    std::uint32_t max_;
    bool keyed_, plain_;
};

// Little-endian CDR-aligned byte writer/reader relative to the encapsulation end.
struct Out {
    std::uint8_t* p; std::uint32_t cap, n = 0; bool fail = false;
    void align(std::uint32_t a) { while (n % a) put8(0); }
    void put8(std::uint8_t v) { if (n >= cap) { fail = true; return; } p[n++] = v; }
    void u32(std::uint32_t v) { align(4); for (int i = 0; i < 4; ++i) put8(static_cast<std::uint8_t>(v >> (8 * i))); }
    void u64(std::uint64_t v) { align(8); for (int i = 0; i < 8; ++i) put8(static_cast<std::uint8_t>(v >> (8 * i))); }
    void bytes(const std::uint8_t* b, std::uint32_t len)
    { if (n + len > cap) { fail = true; return; } std::memcpy(p + n, b, len); n += len; }
    std::uint32_t done() const { return fail ? 0 : n; }
};
struct In {
    const std::uint8_t* p; std::uint32_t len, n = 0; bool fail = false;
    void align(std::uint32_t a) { while (n % a) ++n; }
    std::uint32_t u32() { align(4); if (n + 4 > len) { fail = true; return 0; } std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t(p[n + i]) << (8 * i); n += 4; return v; }
    std::uint64_t u64() { align(8); if (n + 8 > len) { fail = true; return 0; } std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t(p[n + i]) << (8 * i); n += 8; return v; }
    bool bytes(std::uint8_t* b, std::uint32_t count)
    { if (n + count > len) { fail = true; return false; } std::memcpy(b, p + n, count); n += count; return true; }
};

// ---- DynamicType {side:uint32, n:uint32[, text:string<64>]} for DDS-SQL filters and TypeLookup. ----
struct DynRecord {
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    using Type = eprosima::fastrtps::types::DynamicType_ptr;
    using Data = eprosima::fastrtps::types::DynamicData*;
    Type type;
    static Type build(const std::string& name, bool text)
    {
        namespace ft = eprosima::fastrtps::types;
        auto* f = ft::DynamicTypeBuilderFactory::get_instance(); auto* b = f->create_struct_builder();
        if (!b) return Type();
        b->add_member(0, "side", f->create_uint32_type()); b->add_member(1, "n", f->create_uint32_type());
        if (text) b->add_member(2, "text", f->create_string_type(64));
        b->set_name(name); return b->build();
    }
    explicit DynRecord(Type t) : type(std::move(t)) {}
    DynRecord(const std::string& name, bool text) : type(build(name, text)) {}
    fd::TypeSupport support(bool type_information) const
    {
        auto* pst = new eprosima::fastrtps::types::DynamicPubSubType(type);
        pst->auto_fill_type_information(type_information); pst->auto_fill_type_object(false);
        return fd::TypeSupport(pst);
    }
    // Propagate the full TypeObject and no TypeInformation.
    //
    // EDP::hasTypeObject prefers the TypeInformation branch whenever both ends
    // have it assigned, and that branch makes TypeIdentifier::consistent resolve
    // BOTH identifiers through the local TypeObjectFactory, which has no entry
    // for the remote type and so refuses the match. Suppressing TypeInformation
    // makes EDP::checkTypeObject fall through to comparing the two TypeObjects
    // carried in the discovery data directly, which needs no factory lookup.
    // Required for the XTypes assignability cases; not used by dsb_suite, whose
    // TypeLookup probe depends on the TypeInformation path above.
    fd::TypeSupport support_with_type_object() const
    {
        auto* pst = new eprosima::fastrtps::types::DynamicPubSubType(type);
        pst->auto_fill_type_information(false); pst->auto_fill_type_object(true);
        return fd::TypeSupport(pst);
    }
    Data create() const { return eprosima::fastrtps::types::DynamicDataFactory::get_instance()->create_data(type); }
    static void release(Data d) { if (d) eprosima::fastrtps::types::DynamicDataFactory::get_instance()->delete_data(d); }
    static void* ptr(Data& d) { return d; }
    static bool set(Data d, std::uint32_t side, std::uint32_t n)
    { return ok(d->set_uint32_value(side, 0)) && ok(d->set_uint32_value(n, 1)); }
    static bool set_text(Data d, const std::string& t) { return ok(d->set_string_value(t, 2)); }
    static bool get(Data d, std::uint32_t& side, std::uint32_t& n)
    { return ok(d->get_uint32_value(side, 0)) && ok(d->get_uint32_value(n, 1)); }
    static bool get_text(Data d, std::string& t) { return ok(d->get_string_value(t, 2)); }
#else
    using Type = eprosima::fastdds::dds::traits<eprosima::fastdds::dds::DynamicType>::ref_type;
    using Data = eprosima::fastdds::dds::traits<eprosima::fastdds::dds::DynamicData>::ref_type;
    Type type;
    static Type build(const std::string& name, bool text)
    {
        using namespace eprosima::fastdds::dds;
        auto f = DynamicTypeBuilderFactory::get_instance();
        TypeDescriptor::_ref_type td {traits<TypeDescriptor>::make_shared()};
        td->kind(TK_STRUCTURE); td->name(name);
        auto b = f->create_type(td); if (!b) return Type();
        auto member = [&](MemberId id, const char* n, traits<DynamicType>::ref_type t) {
            MemberDescriptor::_ref_type md {traits<MemberDescriptor>::make_shared()};
            md->id(id); md->name(n); md->type(t); return b->add_member(md); };
        if (!ok(member(0, "side", f->get_primitive_type(TK_UINT32))) || !ok(member(1, "n", f->get_primitive_type(TK_UINT32))))
            return Type();
        if (text && !ok(member(2, "text", f->create_string_type(64)->build()))) return Type();
        return b->build();
    }
    explicit DynRecord(Type t) : type(std::move(t)) {}
    DynRecord(const std::string& name, bool text) : type(build(name, text)) {}
    // XTypes 1.3: DynamicPubSubType always registers its TypeObject/TypeInformation.
    fd::TypeSupport support(bool) const { return fd::TypeSupport(new eprosima::fastdds::dds::DynamicPubSubType(type)); }
    // 3.6.2 has no per-type propagation switches, so this is the same registration.
    // EDP::valid_matching compares TypeIdentifiers for equality and never reads
    // TypeConsistencyEnforcementQosPolicy, so assignability cannot match here.
    fd::TypeSupport support_with_type_object() const { return support(true); }
    Data create() const { return eprosima::fastdds::dds::DynamicDataFactory::get_instance()->create_data(type); }
    static void release(Data& d) { if (d) eprosima::fastdds::dds::DynamicDataFactory::get_instance()->delete_data(d); d.reset(); }
    static void* ptr(Data& d) { return &d; }
    static bool set(Data& d, std::uint32_t side, std::uint32_t n)
    { return ok(d->set_uint32_value(0, side)) && ok(d->set_uint32_value(1, n)); }
    static bool set_text(Data& d, const std::string& t) { return ok(d->set_string_value(2, t)); }
    static bool get(Data& d, std::uint32_t& side, std::uint32_t& n)
    { return ok(d->get_uint32_value(side, 0)) && ok(d->get_uint32_value(n, 1)); }
    static bool get_text(Data& d, std::string& t) { return ok(d->get_string_value(t, 2)); }
#endif
};
} // namespace dsb
