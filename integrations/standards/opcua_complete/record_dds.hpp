#pragma once
#include B3_HEADER
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(D3_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
inline const auto D4_DDS_OK=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
inline const auto D4_DDS_EMPTY=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
inline const auto D4_DDS_OK=eprosima::fastdds::dds::RETCODE_OK;
inline const auto D4_DDS_EMPTY=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include "common.hpp"
namespace d4 {
namespace fd=eprosima::fastdds::dds;
using Record=Standards::BoundedRecord;
class RecordDds{
    fd::TypeSupport type_{new Standards::BoundedRecordPubSubType};
    fd::DomainParticipant* participant_=nullptr;
public:
    fd::DataReader* reader=nullptr;
    RecordDds()=default;
    void open(const Config& c){
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        participant_=fd::DomainParticipantFactory::get_instance()->create_participant(c.domain,pq);check(participant_,"D4 DDS participant");check(type_.register_type(participant_)==D4_DDS_OK,"D4 DDS type");
        auto* topic=participant_->create_topic(c.topic,type_.get_type_name(),fd::TOPIC_QOS_DEFAULT);auto* sub=participant_->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);check(topic&&sub,"D4 DDS topic/subscriber");
        fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.data_sharing().off();
        rq.resource_limits().max_instances=5;rq.resource_limits().max_samples_per_instance=64;rq.resource_limits().max_samples=320;rq.resource_limits().allocated_samples=16;
#if defined(D3_FASTDDS_214)
        rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(c.rep)};
#else
        rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(c.rep)};
#endif
        reader=sub->create_datareader(topic,rq);check(reader,"D4 DDS reader");
    }
    ~RecordDds(){close();}
    RecordDds(const RecordDds&)=delete;
    RecordDds& operator=(const RecordDds&)=delete;
    bool close()noexcept{try{if(!participant_)return true;if(participant_->delete_contained_entities()!=D4_DDS_OK)return false;if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant_)!=D4_DDS_OK)return false;participant_=nullptr;reader=nullptr;return true;}catch(...){return false;}}
    bool owns()const{return participant_!=nullptr;}
};
}
