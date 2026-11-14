#pragma once
#include D3_HEADER
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(D3_FASTDDS_214)
#include <fastdds/rtps/common/WriteParams.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#else
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#endif
#include <chrono>
#include <stdexcept>
#include <thread>

namespace d3 {
namespace fd=eprosima::fastdds::dds;
#if defined(D3_FASTDDS_214)
namespace rt=eprosima::fastrtps::rtps;
inline const auto OK=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
inline const auto NO_DATA=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
namespace rt=eprosima::fastdds::rtps;
inline const auto OK=fd::RETCODE_OK;
inline const auto NO_DATA=fd::RETCODE_NO_DATA;
#endif
using Message=Standards::RpcMessage;
using Identity=rt::SampleIdentity;
using Clock=std::chrono::steady_clock;
inline void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Config {
    unsigned domain=219,representation=0,capacity=16,minimum_peers=1;
    std::string service="d3_service";
};
inline bool valid_message(const Message& m){
    return m.label().size()<=64&&m.payload().size()<=4096&&m.label().find('\0')==std::string::npos;
}
inline bool valid_identity(const Identity& id){
    return id.writer_guid()!=rt::GUID_t::unknown()&&id.sequence_number().high>=0&&
           (id.sequence_number().high!=0||id.sequence_number().low!=0);
}
inline std::string identity_text(const Identity& id){
    static const char hex[]="0123456789abcdef";
    std::string out;
    for(unsigned i=0;i<16;++i){
        auto byte=i<12?id.writer_guid().guidPrefix.value[i]:id.writer_guid().entityId.value[i-12];
        out+=hex[byte>>4];out+=hex[byte&15];
    }
    return out+":"+std::to_string((uint64_t(uint32_t(id.sequence_number().high))<<32)|id.sequence_number().low);
}

namespace detail {
class Endpoint {
    std::thread::id owner_=std::this_thread::get_id();
    bool attempted_close_=false,failed_=false,written_=false;
    fd::DomainParticipant* participant_=nullptr;
    fd::TypeSupport type_{new Standards::RpcMessagePubSubType};
public:
    fd::DataWriter* writer=nullptr;
    fd::DataReader* reader=nullptr;
    Config config;
    Endpoint(const Config& c,bool client):config(c){
        require(c.domain<=225&&(c.representation==0||c.representation==2)&&c.capacity>=1&&c.capacity<=16&&
                c.minimum_peers>=1&&c.minimum_peers<=4,"D3 configuration bounds");
        require(!c.service.empty()&&c.service.size()<=100&&c.service.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,"D3 service name");
        try{
            fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;
            pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
            participant_=fd::DomainParticipantFactory::get_instance()->create_participant(c.domain,pq);
            require(participant_,"D3 participant");require(type_.register_type(participant_)==OK,"D3 type registration");
            auto* request=participant_->create_topic(c.service+"_Request",type_.get_type_name(),fd::TOPIC_QOS_DEFAULT);
            auto* reply=participant_->create_topic(c.service+"_Reply",type_.get_type_name(),fd::TOPIC_QOS_DEFAULT);
            require(request&&reply,"D3 topics");
            auto* pub=participant_->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
            auto* sub=participant_->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);require(pub&&sub,"D3 pub/sub");
            fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
            wq.reliability().max_blocking_time={0,100000000};wq.history().kind=fd::KEEP_ALL_HISTORY_QOS;wq.data_sharing().off();
            wq.resource_limits().max_instances=1;wq.resource_limits().max_samples=256;
            wq.resource_limits().max_samples_per_instance=256;wq.resource_limits().allocated_samples=16;
            wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(c.representation)};
            fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
            rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.data_sharing().off();
            rq.resource_limits().max_instances=1;rq.resource_limits().max_samples=256;
            rq.resource_limits().max_samples_per_instance=256;rq.resource_limits().allocated_samples=16;
#if defined(D3_FASTDDS_214)
            rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(c.representation)};
#else
            rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(c.representation)};
#endif
            writer=pub->create_datawriter(client?request:reply,wq);
            reader=sub->create_datareader(client?reply:request,rq);require(writer&&reader,"D3 endpoints");
        }catch(...){close();throw;}
    }
    Endpoint(const Endpoint&)=delete;
    Endpoint& operator=(const Endpoint&)=delete;
    ~Endpoint(){if(!attempted_close_)close();}
    void thread()const{require(owner_==std::this_thread::get_id(),"D3 endpoint is thread confined");}
    void active()const{thread();require(participant_&&!attempted_close_&&!failed_,"D3 endpoint closed/failed");}
    bool ready(){
        active();fd::PublicationMatchedStatus tx;fd::SubscriptionMatchedStatus rx;
        require(writer->get_publication_matched_status(tx)==OK&&reader->get_subscription_matched_status(rx)==OK,"D3 matching status");
        return tx.current_count>=int(config.minimum_peers)&&rx.current_count>=int(config.minimum_peers);
    }
    Identity send(Message& data,const Identity& related=Identity::unknown()){
        active();require(valid_message(data),"D3 message bounds");rt::WriteParams parameters;
        parameters.related_sample_identity(related);
#if defined(D3_FASTDDS_214)
        bool sent=writer->write(&data,parameters);
#else
        bool sent=writer->write(&data,parameters)==OK;
#endif
        if(!sent){failed_=true;throw std::runtime_error("D3 native write failed");}
        written_=true;
        if(!valid_identity(parameters.sample_identity())){failed_=true;throw std::runtime_error("D3 missing assigned identity");}
        return parameters.sample_identity();
    }
    bool receive(Message& data,fd::SampleInfo& info){
        active();auto code=reader->take_next_sample(&data,&info);
        if(code==NO_DATA)return false;
        if(code!=OK){failed_=true;throw std::runtime_error("D3 native take failed");}
        return true;
    }
    bool close()noexcept{
        if(owner_!=std::this_thread::get_id())return false;
        if(attempted_close_)return !participant_&&!failed_;
        attempted_close_=true;
        if(!participant_)return !failed_;
        try{
            if(written_&&writer->wait_for_acknowledgments({5,0})!=OK)failed_=true;
            if(participant_->delete_contained_entities()!=OK){failed_=true;return false;}
            if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant_)!=OK){failed_=true;return false;}
            participant_=nullptr;writer=nullptr;reader=nullptr;
        }catch(...){failed_=true;}
        return !participant_&&!failed_;
    }
    bool owns_participant()const noexcept{return participant_!=nullptr;}
};
}
}
