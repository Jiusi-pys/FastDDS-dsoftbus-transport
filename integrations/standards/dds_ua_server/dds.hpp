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
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
inline const auto DDS_OK=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
inline const auto DDS_NO_DATA=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
inline const auto DDS_OK=eprosima::fastdds::dds::RETCODE_OK;
inline const auto DDS_NO_DATA=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <stdexcept>
namespace d2 {
namespace fd=eprosima::fastdds::dds;
using Record=Standards::BoundedRecord;
inline void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
class Dds {
public:
    fd::DomainParticipant* participant=nullptr; fd::DataReader* reader=nullptr;fd::DataWriter* writer=nullptr;
    fd::TypeSupport type{new Standards::BoundedRecordPubSubType};
    void open(unsigned domain,const std::string& name,unsigned rep) {
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;
        pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        participant=fd::DomainParticipantFactory::get_instance()->create_participant(domain,pq);require(participant,"DDS participant");
        require(type.register_type(participant)==DDS_OK,"DDS type");
        auto* topic=participant->create_topic(name,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);require(topic,"DDS topic");
        auto* sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);require(sub,"DDS subscriber");
        fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.history().depth=16;rq.data_sharing().off();
        // One extra instance admits an observable overflow sample beyond the maximum UA cap.
        rq.resource_limits().max_instances=65;rq.resource_limits().max_samples_per_instance=16;
        rq.resource_limits().max_samples=1040;rq.resource_limits().allocated_samples=16;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
        rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        reader=sub->create_datareader(topic,rq);require(reader,"DDS reader");
        auto* pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);require(pub,"DDS publisher");
        fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.history().depth=16;wq.data_sharing().off();
        wq.resource_limits().max_instances=65;wq.resource_limits().max_samples_per_instance=16;
        wq.resource_limits().max_samples=1040;wq.resource_limits().allocated_samples=16;
        wq.writer_data_lifecycle().autodispose_unregistered_instances=false;
        wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
        writer=pub->create_datawriter(topic,wq);require(writer,"DDS writer");
    }
    bool write(Record& record,const fd::InstanceHandle_t& handle) {
        return writer->write(&record,handle)==DDS_OK;
    }
    bool close() {
        if(!participant)return true;
        if(participant->delete_contained_entities()!=DDS_OK)return false;
        if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant)!=DDS_OK)return false;
        participant=nullptr;reader=nullptr;writer=nullptr;return true;
    }
};
}
