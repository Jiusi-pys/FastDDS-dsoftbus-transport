#include "b4.h"
#include "runtime_owner.hpp"
#include "../cpp_backend/participant_transport.hpp"
#include B3_HEADER
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
namespace rt = eprosima::fastrtps::rtps;
static const auto ok = eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
static const auto no_data = eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
static const auto timed_out = eprosima::fastrtps::types::ReturnCode_t::RETCODE_TIMEOUT;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
namespace rt = eprosima::fastdds::rtps;
static const auto ok = eprosima::fastdds::dds::RETCODE_OK;
static const auto no_data = eprosima::fastdds::dds::RETCODE_NO_DATA;
static const auto timed_out = eprosima::fastdds::dds::RETCODE_TIMEOUT;
#endif
#include <chrono>
#include <thread>
#include <memory>
#include <cstring>
#ifdef B4_DSOFTBUS
#include "../../ros2/autoload/DeviceDeployment.hpp"
#include <fstream>
#include <cstdio>
namespace dd = fastdds_dsoftbus::device_deployment;
namespace ds = fastdds_dsoftbus::dsoftbus;
#endif
namespace fd = eprosima::fastdds::dds;
struct b4_sample { Standards::BoundedRecord value; };
struct b4_endpoint {
    fd::TypeSupport type{new Standards::BoundedRecordPubSubType};
    fd::DomainParticipant *participant=nullptr;
    fd::DataWriter *writer=nullptr;
    fd::DataReader *reader=nullptr;
    bool closing=false;
#ifdef B4_DSOFTBUS
    dd::PumpedRuntime runtime;
    std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> descriptors;
#endif
    bool healthy() const {
        if(closing) return false;
#ifdef B4_DSOFTBUS
        return !runtime.failed.load();
#else
        return true;
#endif
    }
};
template<class F> static b4_result boundary(F f) noexcept { try { return f(); } catch (...) { return B4_ERROR; } }
static bool valid(const b4_options *o) { return o && o->topic && *o->topic &&
    (o->writer==0 || o->writer==1) && (o->representation==0 || o->representation==2) && o->domain<=232; }
extern "C" b4_result b4_sample_create(b4_sample **out) { return boundary([&] {
    if (!out || *out) return B4_BAD_PARAMETER;
    *out=new b4_sample; return B4_OK; }); }
extern "C" b4_result b4_sample_destroy(b4_sample *s) { return boundary([&] { delete s; return B4_OK; }); }
extern "C" b4_result b4_sample_set(b4_sample *s,const b4_record *v) { return boundary([&] {
    if (!s || !v) return B4_BAD_PARAMETER;
    if (v->label_size>64 || v->payload_size>256) return B4_BOUNDS;
    if (std::memchr(v->label,0,v->label_size)) return B4_BAD_PARAMETER;
    Standards::BoundedRecord next;
    next.id(v->id); next.sequence_number(v->sequence_number);
    next.label(std::string(v->label,v->label_size));
    next.payload(std::vector<uint8_t>(v->payload,v->payload+v->payload_size));
    s->value=std::move(next); return B4_OK; }); }
extern "C" b4_result b4_sample_get(const b4_sample *s,b4_record *out) { return boundary([&] {
    if (!s || !out) return B4_BAD_PARAMETER;
    b4_record next{}; next.id=s->value.id(); next.sequence_number=s->value.sequence_number();
    next.label_size=s->value.label().size(); next.payload_size=s->value.payload().size();
    if(next.label_size>64 || next.payload_size>256) return B4_BOUNDS;
    if(next.label_size) std::memcpy(next.label,s->value.label().data(),next.label_size);
    if(next.payload_size) std::memcpy(next.payload,s->value.payload().data(),next.payload_size);
    *out=next; return B4_OK; }); }
static b4_result cleanup(b4_endpoint *e) {
    if (!e) return B4_OK;
    e->closing=true;
    if(e->participant) {
        if(e->participant->delete_contained_entities()!=ok) return B4_ERROR;
        if(fd::DomainParticipantFactory::get_instance()->delete_participant(e->participant)!=ok) return B4_ERROR;
        e->participant=nullptr; e->writer=nullptr; e->reader=nullptr;
    }
#ifdef B4_DSOFTBUS
    if(!b4_detail::close_runtime(e->runtime,e->descriptors)) return B4_ERROR;
#endif
    return B4_OK;
}
static b4_result open_impl(const b4_options *o,const b4_dsoftbus_options *d,b4_endpoint **out) {
    if(!valid(o) || !out || *out) return B4_BAD_PARAMETER;
    std::unique_ptr<b4_endpoint> e(new b4_endpoint);
    try {
        fd::DomainParticipantQos pq;
        pq.transport().use_builtin_transports=false;
        if(d) {
#ifdef B4_DSOFTBUS
            if(!d->token || !d->run || !d->local_udid || !d->peer_udid || !d->peer_network || !d->gate_file || d->side>1)
                return B4_BAD_PARAMETER;
            dd::SetSelfToken(d->token);
            auto deploy=dd::MakeDeploy("com.kaihong.mdds","b4.c",d->side,d->run,o->domain,d->local_udid,d->peer_udid,d->peer_network);
            auto c=dd::Configuration(deploy,true);
            e->runtime.rt=std::make_unique<ds::experimental::Runtime>(dd::Backend("com.kaihong.mdds",c),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
            std::string gate=d->gate_file;
            if(!e->runtime.rt->SetPreConnectGate([gate] {
                std::puts("B4_LISTENING"); std::fflush(stdout);
                auto until=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while(std::chrono::steady_clock::now()<until) {
                    if(std::ifstream(gate).good()) return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                } return false;
            })) throw std::runtime_error("preconnect gate");
            auto desc=e->runtime.rt->MakeStockDescriptor(c);
            if(!desc) throw std::runtime_error("stock descriptor");
            e->descriptors.push_back(desc); pq.transport().user_transports={desc};
            pq.transport().max_msg_size_no_frag=c.maxMessageSize;
            auto &w=pq.wire_protocol();
            w.builtin.metatrafficUnicastLocatorList.push_back(c.default_user_unicast.at(0));
            w.builtin.metatrafficMulticastLocatorList.clear();
            w.builtin.initialPeersList.push_back(c.directory.routes.at(0).key.remote_locator);
            w.default_unicast_locator_list.push_back(c.default_user_unicast.at(0));
            w.default_multicast_locator_list.clear();
            e->runtime.start();
#else
            return B4_UNSUPPORTED;
#endif
        } else standards_transport::configure(o->domain,pq);
        auto factory=fd::DomainParticipantFactory::get_instance();
        e->participant=factory->create_participant(o->domain,pq);
        if(!e->participant || e->type.register_type(e->participant)!=ok) throw std::runtime_error("participant/type");
        auto topic=e->participant->create_topic(o->topic,e->type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        if(!topic) throw std::runtime_error("topic");
        if(o->writer) {
            auto pub=e->participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
            fd::DataWriterQos q; q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
            q.history().depth=16; q.data_sharing().off();
            q.representation().m_value={static_cast<fd::DataRepresentationId_t>(o->representation)};
            if(pub) e->writer=pub->create_datawriter(topic,q);
            if(!e->writer) throw std::runtime_error("writer");
        } else {
            auto sub=e->participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
            fd::DataReaderQos q; q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
            q.history().depth=16; q.data_sharing().off();
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
            q.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(o->representation)};
#else
            q.representation().m_value={static_cast<fd::DataRepresentationId_t>(o->representation)};
#endif
            if(sub) e->reader=sub->create_datareader(topic,q);
            if(!e->reader) throw std::runtime_error("reader");
        }
    } catch (...) {
        // Keep a failed cleanup owner reachable for caller retry; never destroy a
        // live Runtime or a Participant retained by Fast DDS.
        bool cleaned=false;
        try { cleaned=cleanup(e.get())==B4_OK; } catch (...) {}
        if(!cleaned) { *out=e.release(); }
        throw;
    }
    *out=e.release(); return B4_OK;
}
extern "C" b4_result b4_open(const b4_options *o,b4_endpoint **out) { return boundary([&] { return open_impl(o,nullptr,out); }); }
extern "C" b4_result b4_open_dsoftbus(const b4_options *o,const b4_dsoftbus_options *d,b4_endpoint **out) {
    return boundary([&] { if(!d) return B4_BAD_PARAMETER; return open_impl(o,d,out); }); }
extern "C" b4_result b4_close(b4_endpoint *e) { return boundary([&] {
    auto result=cleanup(e); if(result==B4_OK) delete e; return result; }); }
extern "C" b4_result b4_transport_stats(b4_endpoint *e,b4_transport_counters *out) { return boundary([&] {
    if(!e || !out) return B4_BAD_PARAMETER;
#ifdef B4_DSOFTBUS
    if(e->runtime.rt) {
        if(!e->healthy()) return B4_ERROR;
        b4_transport_counters next{e->runtime.sdk_sends.load(),e->runtime.received_records.load()};
        *out=next; return B4_OK;
    }
#endif
    return B4_UNSUPPORTED; }); }
extern "C" b4_result b4_wait_match(b4_endpoint *e,uint32_t ms) { return boundary([&] {
    if(!e || ms>60000) return B4_BAD_PARAMETER;
    auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    do {
        if(!e->healthy()) return B4_ERROR;
        if(e->writer) { fd::PublicationMatchedStatus s; if(e->writer->get_publication_matched_status(s)!=ok) return B4_ERROR; if(s.current_count>0) return B4_OK; }
        else if(e->reader) { fd::SubscriptionMatchedStatus s; if(e->reader->get_subscription_matched_status(s)!=ok) return B4_ERROR; if(s.current_count>0) return B4_OK; }
        else return B4_BAD_PARAMETER;
        if(std::chrono::steady_clock::now()>=until) return B4_TIMEOUT;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } while(true); }); }
extern "C" b4_result b4_write(b4_endpoint *e,const b4_sample *s) { return boundary([&] {
    if(!e || !s || !e->writer) return B4_BAD_PARAMETER;
    if(!e->healthy()) return B4_ERROR;
    auto copy=s->value;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
    return e->writer->write(&copy)?B4_OK:B4_ERROR;
#else
    return e->writer->write(&copy)==ok?B4_OK:B4_ERROR;
#endif
    }); }
extern "C" b4_result b4_take(b4_endpoint *e,b4_sample *s) { return boundary([&] {
    if(!e || !s || !e->reader) return B4_BAD_PARAMETER;
    if(!e->healthy()) return B4_ERROR;
    Standards::BoundedRecord next; fd::SampleInfo info;
    auto code=e->reader->take_next_sample(&next,&info);
    if(code==no_data || (code==ok && !info.valid_data)) return B4_NO_DATA;
    if(code!=ok) return B4_ERROR;
    s->value=std::move(next); return B4_OK; }); }
extern "C" b4_result b4_wait_ack(b4_endpoint *e,uint32_t ms) { return boundary([&] {
    if(!e || !e->writer || ms>60000) return B4_BAD_PARAMETER;
    if(!e->healthy()) return B4_ERROR;
    auto code=e->writer->wait_for_acknowledgments({static_cast<int32_t>(ms/1000),static_cast<uint32_t>((ms%1000)*1000000)});
    return code==ok?B4_OK:code==timed_out?B4_TIMEOUT:B4_ERROR; }); }
