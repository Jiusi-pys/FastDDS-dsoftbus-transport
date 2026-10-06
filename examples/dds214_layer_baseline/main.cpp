// One direct DDS pair over the installed 2.14 factory integration. No ROS/RMW,
// private headers, native Socket call or application-owned replacement pump.
#include "LayerSample.hpp"
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif
extern "C" void FastddsDsoftbusFailureSnapshot(std::uint64_t) noexcept;
extern "C" void FastddsDsoftbusTeardownCheckpoint(const char*) noexcept;
namespace fd=eprosima::fastdds::dds;
using DdsReturn=eprosima::fastrtps::types::ReturnCode_t;
using namespace std::chrono_literals;
std::string guid(const eprosima::fastrtps::rtps::GUID_t& value) {
    std::ostringstream out;out<<std::hex<<std::setfill('0');
    for(auto v:value.guidPrefix.value)out<<std::setw(2)<<unsigned(v);
    for(auto v:value.entityId.value)out<<std::setw(2)<<unsigned(v);
    return out.str();
}
std::uint64_t sn(const eprosima::fastrtps::rtps::SequenceNumber_t& value) {
    return (std::uint64_t(std::uint32_t(value.high))<<32)|value.low;
}
LayerSample sample(std::uint64_t run,unsigned side,unsigned ordinal) {
    LayerSample result;result.run=run;result.side=side;result.ordinal=ordinal;
    for(unsigned i=0;i<result.payload.size();++i)result.payload[i]=std::uint8_t((run>>(8*(i%8)))^side^ordinal^i);
    return result;
}
bool stage_release(const std::string& run,const std::string& side,long pid,const char* stage) {
    const char* prefix=std::getenv("FASTDDS_DSOFTBUS_DDS214_CONTROL_PREFIX");
    if(!prefix)return false;
    std::cout<<"DDS214_STAGE run="<<run<<" side="<<side<<" pid="<<pid<<" stage="<<stage<<std::endl;
    const std::string expected="DDS214_RELEASE run="+run+" side="+side+" stage="+stage;
    const auto end=std::chrono::steady_clock::now()+90s;
    while(std::chrono::steady_clock::now()<end) {
        std::ifstream input(std::string(prefix)+"."+stage);std::string line;
        if(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();return line==expected;}
        std::this_thread::sleep_for(2ms);
    }
    return false;
}
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    const std::string side=argv[1],run=argv[2],mode=argv[3];
    if((side!="A"&&side!="B")||!std::regex_match(run,std::regex("[0-9a-f]{16}"))||
       (mode!="normal"&&mode!="delete-refusal")||!std::getenv("FASTDDS_DSOFTBUS_REQUIRED_CONFIG"))return 2;
#if defined(__OHOS__)
    // Refusal injection is Host-only; a normal data grant is not fault authority.
    if(mode!="normal")return 2;
#endif
#if defined(_WIN32)
    const long pid=_getpid();
#else
    const long pid=getpid();
#endif
    const unsigned own=side=="A"?0:1;const auto run_number=std::stoull(run,nullptr,16);
    std::cout<<"DDS214_MAIN scenario=DDS214_LAYER_BASELINE run="<<run<<" side="<<side<<" pid="<<pid<<" mode="<<mode<<std::endl;
    auto factory=fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipant* participant=nullptr;int result=4;const char* stage="CREATE";
    fd::TypeSupport type(new LayerSampleType());
    try {
        fd::DomainParticipantQos qos;qos.name("dds214_"+run+"_"+side);
        participant=factory->create_participant(201,qos);
        if(!participant)throw std::runtime_error("PARTICIPANT_CREATE");
        FastddsDsoftbusTeardownCheckpoint("DIRECT_DDS_OWNER_LIVE");
        const std::string writer_topic="dds214_"+run+"_from_"+side;
        const std::string reader_topic="dds214_"+run+"_from_"+(side=="A"?"B":"A");
        if(type.register_type(participant)!=DdsReturn::RETCODE_OK)throw std::runtime_error("TYPE_REGISTER");
        auto* outgoing_topic=participant->create_topic(writer_topic,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* incoming_topic=participant->create_topic(reader_topic,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);
        auto* sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        if(!outgoing_topic||!incoming_topic||!pub||!sub)throw std::runtime_error("ENTITY_CREATE");
        fd::DataWriterQos wq;fd::DataReaderQos rq;
        wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
        wq.durability().kind=fd::VOLATILE_DURABILITY_QOS;rq.durability().kind=fd::VOLATILE_DURABILITY_QOS;
        wq.history().depth=16;rq.history().depth=16;
        wq.resource_limits().max_samples=32;rq.resource_limits().max_samples=32;
        wq.resource_limits().allocated_samples=16;rq.resource_limits().allocated_samples=16;
        wq.data_sharing().off();rq.data_sharing().off();
        auto* writer=pub->create_datawriter(outgoing_topic,wq);auto* reader=sub->create_datareader(incoming_topic,rq);
        if(!writer||!reader)throw std::runtime_error("ENDPOINT_CREATE");
        std::cout<<"DDS214_ID run="<<run<<" side="<<side<<" participant="<<guid(participant->guid())<<" writer="<<guid(writer->guid())<<" reader="<<guid(reader->guid())<<" writer_topic="<<outgoing_topic->get_name()<<" reader_topic="<<incoming_topic->get_name()<<std::endl;
        stage="MATCH";const auto match_end=std::chrono::steady_clock::now()+30s;
        fd::PublicationMatchedStatus wm;fd::SubscriptionMatchedStatus rm;
        do {writer->get_publication_matched_status(wm);reader->get_subscription_matched_status(rm);
            if(wm.current_count==1&&rm.current_count==1)break;std::this_thread::sleep_for(1ms);
        }while(std::chrono::steady_clock::now()<match_end);
        if(wm.current_count!=1||rm.current_count!=1)throw std::runtime_error("PEER_MATCH_MISSING");
        std::cout<<"DDS214_MATCH run="<<run<<" side="<<side<<" writers="<<wm.current_count<<" readers="<<rm.current_count<<std::endl;
        if(!stage_release(run,side,pid,"MATCH"))throw std::runtime_error("MATCH_RELEASE_MISSING");
        if(mode=="delete-refusal") {
            stage="DELETE_REFUSAL";
            // The internal has_active_entities declaration is not a 2.14 DLL
            // export. Observe owned live endpoints through exported public APIs.
            std::vector<fd::DataWriter*> live_writers;std::vector<fd::DataReader*> live_readers;
            if(!pub->get_datawriters(live_writers)||sub->get_datareaders(live_readers)!=DdsReturn::RETCODE_OK)
                throw std::runtime_error("LIVE_ENDPOINT_QUERY_FAILED");
            const bool active=live_writers.size()==1&&live_readers.size()==1&&live_writers[0]==writer&&live_readers[0]==reader;
            const auto participant_id=guid(participant->guid()),writer_id=guid(writer->guid()),reader_id=guid(reader->guid());
            const auto prior=participant->delete_topic(outgoing_topic);
            std::cout<<"DDS214_PRIOR_DELETE_REFUSAL run="<<run<<" side="<<side<<" api=delete_topic rc="<<prior()<<" responsibility=LIVE_DATAWRITER_TOPIC_REFERENCE"<<std::endl;
            if(prior!=DdsReturn::RETCODE_PRECONDITION_NOT_MET)throw std::runtime_error("TOPIC_REFUSAL_CONTRACT");
            const auto refused=factory->delete_participant(participant);
            std::cout<<"DDS214_DELETE_REFUSAL run="<<run<<" side="<<side<<" active="<<int(active)<<" active_source=PUBLIC_OWNED_ENDPOINT_LISTS writers="<<live_writers.size()<<" readers="<<live_readers.size()<<" rc="<<refused()<<" participant="<<participant_id<<" writer="<<writer_id<<" reader="<<reader_id<<" identity_source=BEFORE_CALL"<<std::endl;
            // Only the specified PRECONDITION result proves this handle survives.
            if(!active||refused!=DdsReturn::RETCODE_PRECONDITION_NOT_MET){participant=nullptr;throw std::runtime_error("DELETE_REFUSAL_CONTRACT");}
        }
        stage="DATA";const auto data_end=std::chrono::steady_clock::now()+30s;
        for(unsigned ordinal=1;ordinal<=5;++ordinal) {
            auto value=sample(run_number,own,ordinal);eprosima::fastrtps::rtps::WriteParams params;
            const bool written=writer->write(&value,params);
            std::cout<<"DDS214_WRITE run="<<run<<" side="<<side<<" ordinal="<<ordinal<<" ok="<<int(written)<<" writer="<<guid(writer->guid())<<" sn="<<sn(params.sample_identity().sequence_number())<<std::endl;
            if(!written)throw std::runtime_error("WRITE_FAILED");
        }
        std::set<unsigned> taken;
        while(taken.size()<5&&std::chrono::steady_clock::now()<data_end) {
            LayerSample value;fd::SampleInfo info;
            if(reader->take_next_sample(&value,&info)!=DdsReturn::RETCODE_OK){std::this_thread::sleep_for(1ms);continue;}
            if(!info.valid_data)continue;
            const auto expected=sample(run_number,1-own,value.ordinal);
            if(value.run!=run_number||value.side!=1-own||value.ordinal<1||value.ordinal>5||value.payload!=expected.payload||!taken.insert(value.ordinal).second)throw std::runtime_error("SAMPLE_IDENTITY");
            std::cout<<"DDS214_TAKE run="<<run<<" side="<<side<<" ordinal="<<value.ordinal<<" writer="<<guid(info.sample_identity.writer_guid())<<" reader="<<guid(reader->guid())<<" sn="<<sn(info.sample_identity.sequence_number())<<" payload_ok=1"<<std::endl;
        }
        if(taken.size()!=5)throw std::runtime_error("TAKE_MISSING");
        // Keep both Readers alive until the public DDS wait has returned.
        const auto acknowledged=writer->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(10,0));
        std::cout<<"DDS214_WAIT run="<<run<<" side="<<side<<" rc="<<acknowledged()<<" scope=DDS_CONDITION_NOT_NETWORK_ACK_COUNT"<<std::endl;
        if(acknowledged!=DdsReturn::RETCODE_OK)throw std::runtime_error("DDS_WAIT_FAILED");
        std::cout<<"DDS214_DATA_DONE run="<<run<<" side="<<side<<" writes=5 takes="<<taken.size()<<std::endl;
        if(!stage_release(run,side,pid,"DATA_DONE"))throw std::runtime_error("DATA_DONE_RELEASE_MISSING");
        result=0;
    }catch(const std::exception& error){std::cout<<"DDS214_FAILURE run="<<run<<" side="<<side<<" stage="<<stage<<" reason="<<error.what()<<std::endl;FastddsDsoftbusFailureSnapshot(1);}
    if(participant) {
        FastddsDsoftbusTeardownCheckpoint("BEFORE_DIRECT_DDS_ENTITY_DELETE");
        const auto entities=participant->delete_contained_entities();
        std::cout<<"DDS214_DELETE_ENTITIES run="<<run<<" side="<<side<<" rc="<<entities()<<std::endl;
        if(entities!=DdsReturn::RETCODE_OK)result=4;
        else {
            std::cout<<"DDS214_DELETE_PARTICIPANT_ENTER run="<<run<<" side="<<side<<std::endl;
            const auto deleted=factory->delete_participant(participant);
            std::cout<<"DDS214_DELETE_PARTICIPANT_RETURN run="<<run<<" side="<<side<<" rc="<<deleted()<<std::endl;
            if(deleted!=DdsReturn::RETCODE_OK)result=4;
        }
    }
    FastddsDsoftbusTeardownCheckpoint("AFTER_DIRECT_DDS_CLEANUP");
    std::cout<<"DDS214_EXIT run="<<run<<" side="<<side<<" result="<<result<<std::endl;return result;
}
