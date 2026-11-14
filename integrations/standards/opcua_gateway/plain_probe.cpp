// Independent public-DDS reader: deliberately has no gateway/B4/open62541 dependency.
#include B3_HEADER
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
const auto OK=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
const auto NO_DATA=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
const auto OK=eprosima::fastdds::dds::RETCODE_OK;
const auto NO_DATA=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
namespace fd=eprosima::fastdds::dds;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static std::string hex(const std::string& bytes){std::ostringstream out;for(unsigned char c:bytes)out<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(c);return out.str();}
int main(int argc,char** argv) {
    fd::DomainParticipant* participant=nullptr;int exit=0;unsigned received=0;
    auto* factory=fd::DomainParticipantFactory::get_instance();
    try {
        check(argc==6,"usage: d1_plain_probe DOMAIN TOPIC REP STOPFILE RUNTIME_MS");
        unsigned domain=std::stoul(argv[1]),rep=std::stoul(argv[3]),runtime=std::stoul(argv[5]);
        check(domain<=225 && (rep==0||rep==2) && runtime<=120000,"probe option bounds");
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;
        pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        participant=factory->create_participant(domain,pq);check(participant,"participant");
        fd::TypeSupport type(new Standards::BoundedRecordPubSubType());check(type.register_type(participant)==OK,"register type");
        auto* topic=participant->create_topic(argv[2],type.get_type_name(),fd::TOPIC_QOS_DEFAULT);check(topic,"topic");
        auto* subscriber=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);check(subscriber,"subscriber");
        fd::DataReaderQos qos;qos.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;qos.history().depth=16;qos.data_sharing().off();
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        qos.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
        qos.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        auto* reader=subscriber->create_datareader(topic,qos);check(reader,"reader");
        std::cout<<"D1_PROBE_READY rep="<<rep<<std::endl;
        auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(runtime);
        while(!std::filesystem::exists(argv[4])) {
            check(std::chrono::steady_clock::now()<deadline,"probe deadline");
            Standards::BoundedRecord value;fd::SampleInfo info;auto code=reader->take_next_sample(&value,&info);
            check(code==OK||code==NO_DATA,"take status");
            if(code==OK&&info.valid_data){++received;std::cout<<"D1_SAMPLE key="<<value.id()<<" value="<<value.sequence_number()<<" label="<<hex(value.label())<<" payload_size="<<value.payload().size()<<std::endl;}
            else std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    } catch(const std::exception& e){std::cerr<<"D1_PROBE_FAIL "<<e.what()<<std::endl;exit=3;}
    if(participant){if(participant->delete_contained_entities()!=OK)exit=3;else if(factory->delete_participant(participant)!=OK)exit=3;}
    std::cout<<"D1_PROBE_CLOSED exit="<<exit<<" received="<<received<<std::endl;return exit;
}
