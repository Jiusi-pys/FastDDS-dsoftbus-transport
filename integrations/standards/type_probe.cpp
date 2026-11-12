#include "compat.hpp"
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#endif
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

// Separate processes exercise the public DDS entry points and UDP transport.
// This executable reports conformance failures, including failures of stock DDS.
namespace {
using namespace dsb;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

DynRecord::Type make_type(const std::string& scenario, bool writer)
{
    const bool evolution = scenario == "appendable_added_member" || scenario == "mutable_added_member" ||
            scenario == "final_added_member" || scenario == "final_vs_appendable" ||
            scenario == "mutable_vs_appendable" || scenario == "disallow_coercion" ||
            scenario == "prevent_type_widening";
    const bool text = evolution && writer;
    const bool conflict = scenario == "incompatible_member" && writer;
    const char* extent = (scenario == "final_added_member" || (scenario == "final_vs_appendable" && !writer)) ?
            "final" : (scenario == "mutable_added_member" || (scenario == "mutable_vs_appendable" && !writer)) ?
            "mutable" : "appendable";
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    namespace ft = eprosima::fastrtps::types;
    auto* factory = ft::DynamicTypeBuilderFactory::get_instance();
    auto* builder = factory->create_struct_builder();
    require(builder != nullptr, "create type builder");
    require(ok(builder->set_name("DdsStandardsRecord")), "type name");
    require(ok(builder->apply_annotation("extensibility", "value", extent)), "extensibility");
    require(ok(builder->add_member(0, "side", factory->create_uint32_type())), "side member");
    require(ok(builder->add_member(1, "n", conflict ? factory->create_string_type(64) : factory->create_uint32_type())), "n member");
    if (text) require(ok(builder->add_member(2, "text", factory->create_string_type(64))), "text member");
    auto type = builder->build();
    factory->delete_builder(builder);
    return type;
#else
    using namespace eprosima::fastdds::dds;
    auto factory = DynamicTypeBuilderFactory::get_instance();
    TypeDescriptor::_ref_type descriptor {traits<TypeDescriptor>::make_shared()};
    descriptor->kind(TK_STRUCTURE);
    descriptor->name("DdsStandardsRecord");
    descriptor->extensibility_kind(std::string(extent) == "final" ? ExtensibilityKind::FINAL :
            std::string(extent) == "mutable" ? ExtensibilityKind::MUTABLE : ExtensibilityKind::APPENDABLE);
    auto builder = factory->create_type(descriptor);
    require(bool(builder), "create type builder");
    auto add = [&](MemberId id, const char* name, traits<DynamicType>::ref_type type) {
        MemberDescriptor::_ref_type member {traits<MemberDescriptor>::make_shared()};
        member->id(id); member->name(name); member->type(type);
        require(ok(builder->add_member(member)), "add member");
    };
    add(0, "side", factory->get_primitive_type(TK_UINT32));
    add(1, "n", conflict ? factory->create_string_type(64)->build() : factory->get_primitive_type(TK_UINT32));
    if (text) add(2, "text", factory->create_string_type(64)->build());
    return builder->build();
#endif
}

struct Participant {
    fd::DomainParticipant* value = nullptr;
    bool close()
    {
        if (!value) return true;
        const bool entities_deleted = ok(value->delete_contained_entities());
        const bool participant_deleted = ok(fd::DomainParticipantFactory::get_instance()->delete_participant(value));
        if (participant_deleted) value = nullptr;
        return entities_deleted && participant_deleted;
    }
    ~Participant()
    {
        if (!close()) std::cerr << "DDS_STANDARDS_ERROR participant cleanup failed" << std::endl;
    }
};

int run(const std::string& scenario, bool writer, unsigned domain, const std::string& topic, unsigned duration)
{
    auto* factory = fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipantQos qos;
    qos.transport().use_builtin_transports = false;
    qos.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
    Participant participant {factory->create_participant(domain, qos)};
    require(participant.value != nullptr, "create participant");
    DynRecord record(make_type(scenario, writer));
    require(bool(record.type), "build dynamic type");
    auto support = record.support_with_type_object();
    require(ok(support.register_type(participant.value)), "register type");
    auto* t = participant.value->create_topic(topic, "DdsStandardsRecord", fd::TOPIC_QOS_DEFAULT);
    require(t != nullptr, "create topic");
    DynRecord control("DdsStandardsControl", false);
    auto control_support = control.support(true);
    require(ok(control_support.register_type(participant.value)), "register control type");
    auto* control_topic = participant.value->create_topic(topic + "_control", "DdsStandardsControl", fd::TOPIC_QOS_DEFAULT);
    require(control_topic != nullptr, "create control topic");
    const auto representation = scenario == "xml" ? fd::XML_DATA_REPRESENTATION :
            (scenario == "identical_xcdr1" ? fd::XCDR_DATA_REPRESENTATION : fd::XCDR2_DATA_REPRESENTATION);
    fd::DataWriter* w = nullptr;
    fd::DataReader* r = nullptr;
    fd::DataWriter* control_writer = nullptr;
    fd::DataReader* control_reader = nullptr;
    if (writer) {
        auto* pub = participant.value->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
        require(pub != nullptr, "create publisher");
        fd::DataWriterQos q;
        q.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        q.representation().m_value = {representation};
        w = pub->create_datawriter(t, q);
        require(w != nullptr, "create writer");
        q.representation().m_value = {fd::XCDR_DATA_REPRESENTATION};
        control_writer = pub->create_datawriter(control_topic, q);
        require(control_writer != nullptr, "create control writer");
    } else {
        auto* sub = participant.value->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        require(sub != nullptr, "create subscriber");
        fd::DataReaderQos q;
        q.reliability().kind = fd::RELIABLE_RELIABILITY_QOS;
        // The recorded baselines force type validation. With
        // DDS_STANDARDS_FORCE_TYPE_VALIDATION=0 the reader does not touch
        // m_force_type_validation, leaving whatever the linked Fast DDS
        // release defaults to: the configuration the 2026-11-10 mixed-version
        // investigation named as untested, where 2.14.5
        // EDP::checkTypeValidation step 2 can accept a peer that supplies no
        // legacy TypeIdentifier/TypeObject. Absent or any other value keeps
        // the forced validation, so existing evidence stands.
        const char* const force_env = std::getenv("DDS_STANDARDS_FORCE_TYPE_VALIDATION");
        const bool force_validation = !(force_env && std::string(force_env) == "0");
        std::cout << "DDS_STANDARDS_NOTE force_type_validation="
                  << (force_validation ? 1 : 0) << std::endl;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        q.type_consistency().representation.m_value = {representation};
        q.type_consistency().type_consistency.m_kind = fd::ALLOW_TYPE_COERCION;
        if (scenario == "disallow_coercion")
            q.type_consistency().type_consistency.m_kind = fd::DISALLOW_TYPE_COERCION;
        if (scenario == "prevent_type_widening")
            q.type_consistency().type_consistency.m_prevent_type_widening = true;
        if (force_validation) {
            q.type_consistency().type_consistency.m_force_type_validation = true;
        }
#else
        q.representation().m_value = {representation};
        q.type_consistency().m_kind = fd::ALLOW_TYPE_COERCION;
        if (scenario == "disallow_coercion") q.type_consistency().m_kind = fd::DISALLOW_TYPE_COERCION;
        if (scenario == "prevent_type_widening") q.type_consistency().m_prevent_type_widening = true;
        if (force_validation) {
            q.type_consistency().m_force_type_validation = true;
        }
#endif
        r = sub->create_datareader(t, q);
        require(r != nullptr, "create reader");
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        q.type_consistency().representation.m_value = {fd::XCDR_DATA_REPRESENTATION};
#else
        q.representation().m_value = {fd::XCDR_DATA_REPRESENTATION};
#endif
        control_reader = sub->create_datareader(control_topic, q);
        require(control_reader != nullptr, "create control reader");
    }
    auto data = record.create();
    require(bool(data), "create dynamic data");
    auto control_data = control.create();
    require(bool(control_data), "create control data");
    // Cases that must NOT match. `incompatible_member` is a genuine XTypes
    // rejection. `xml` is a documented non-conformance: Fast DDS implements no
    // XML data representation in either version line, and
    // EDP::checkDataRepresentationQos leaves the representation incompatible
    // while logging "DataRepresentationQosPolicy XML_DATA_REPRESENTATION isn't
    // supported." (2.14.5 and 3.6.2 alike). This product does not claim the
    // XTypes XML data representation conformance point, so the probe asserts the
    // rejection that both versions actually perform rather than a delivery that
    // neither can perform. A future XML implementation turns this back into a
    // delivery case; it is not a pass granted to a broken implementation.
    const bool negative = scenario == "incompatible_member" || scenario == "xml" ||
            scenario == "final_added_member" || scenario == "final_vs_appendable" ||
            scenario == "mutable_vs_appendable" || scenario == "disallow_coercion" ||
            scenario == "prevent_type_widening";
    const char* const expectation = negative ? "rejection" : "delivery";
    const char* const basis = scenario == "xml" ? "documented_non_conformance" : "xtypes";
    bool matched = false, delivered = false;
    bool peer_alive = false;
    unsigned received = 0;
    const auto until = Clock::now() + std::chrono::seconds(duration);
    while (Clock::now() < until) {
        if (writer) {
            fd::PublicationMatchedStatus control_status;
            require(ok(control_writer->get_publication_matched_status(control_status)), "control writer status");
            if (control_status.current_count > 0) {
                require(DynRecord::set(control_data, 7, 42), "set control sample");
                require(write_ok(control_writer->write(DynRecord::ptr(control_data))), "write control sample");
                peer_alive = ok(control_writer->wait_for_acknowledgments(seconds(1))) || peer_alive;
            }
            fd::PublicationMatchedStatus status;
            require(ok(w->get_publication_matched_status(status)), "writer match status");
            matched = matched || status.total_count > 0;
            if (status.current_count > 0 && !negative) {
                require(DynRecord::set(data, 7, 42), "set sample");
                if (scenario == "appendable_added_member" || scenario == "mutable_added_member")
                    require(DynRecord::set_text(data, "added"), "set extension");
                require(write_ok(w->write(DynRecord::ptr(data))), "write sample");
                delivered = ok(w->wait_for_acknowledgments(seconds(1)));
            }
        } else {
            fd::SampleInfo control_info;
            while (ok(control_reader->take_next_sample(DynRecord::ptr(control_data), &control_info))) {
                if (!control_info.valid_data) continue;
                std::uint32_t side = 0, n = 0;
                require(DynRecord::get(control_data, side, n) && side == 7 && n == 42, "control sample mismatch");
                peer_alive = true;
            }
            fd::SubscriptionMatchedStatus status;
            require(ok(r->get_subscription_matched_status(status)), "reader match status");
            matched = matched || status.total_count > 0;
            fd::SampleInfo info;
            while (ok(r->take_next_sample(DynRecord::ptr(data), &info))) {
                if (!info.valid_data) continue;
                std::uint32_t side = 0, n = 0;
                require(DynRecord::get(data, side, n), "read fields");
                require(side == 7 && n == 42, "sample value mismatch");
                ++received;
                delivered = true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    DynRecord::release(data);
    DynRecord::release(control_data);
    const bool cleanup_ok = participant.close();
    const bool passed = cleanup_ok && peer_alive && (negative ? !matched && !delivered : matched && delivered);
    std::cout << "DDS_STANDARDS_RESULT {\"case\":\"" << scenario
              << "\",\"role\":\"" << (writer ? "writer" : "reader")
              << "\",\"fastdds\":\"" << DSB_FASTDDS_LINE
              << "\",\"transport\":\"udp4\",\"expectation\":\"" << expectation
              << "\",\"basis\":\"" << basis
              << "\",\"matched\":" << (matched ? "true" : "false")
              << ",\"received\":" << received << ",\"cleanup_ok\":" << (cleanup_ok ? "true" : "false")
              << ",\"peer_alive\":" << (peer_alive ? "true" : "false")
              << ",\"passed\":" << (passed ? "true" : "false") << "}" << std::endl;
    if (!peer_alive) return 3;
    return passed ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    try {
        require(argc == 6, "usage: dds_type_probe CASE writer|reader DOMAIN TOPIC SECONDS");
        const std::string scenario = argv[1], role = argv[2];
        require(scenario == "identical_xcdr1" || scenario == "identical_xcdr2" ||
                scenario == "appendable_added_member" || scenario == "mutable_added_member" ||
                scenario == "incompatible_member" || scenario == "xml" ||
                scenario == "final_added_member" || scenario == "final_vs_appendable" ||
                scenario == "mutable_vs_appendable" || scenario == "disallow_coercion" ||
                scenario == "prevent_type_widening", "unknown case");
        require(role == "writer" || role == "reader", "unknown role");
        const auto domain = std::stoul(argv[3]), duration = std::stoul(argv[5]);
        require(domain < 233 && duration >= 1 && duration <= 60, "domain or duration out of range");
        return run(scenario, role == "writer", static_cast<unsigned>(domain), argv[4], static_cast<unsigned>(duration));
    } catch (const std::exception& error) {
        std::cerr << "DDS_STANDARDS_ERROR " << error.what() << std::endl;
        return 2;
    }
}
