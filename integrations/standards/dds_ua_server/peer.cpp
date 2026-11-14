// Independent public DDS fixture. No gateway, B4, UA model or UA headers.
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
const auto OK=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
const auto EMPTY=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
const auto OK=eprosima::fastdds::dds::RETCODE_OK;
const auto EMPTY=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
namespace fd=eprosima::fastdds::dds;
static void check(bool b,const char* t){if(!b)throw std::runtime_error(t);}
static std::string decode(std::string text){if(text=="-")return {};check(text.size()%2==0,"hex length");std::string out;for(size_t i=0;i<text.size();i+=2){auto part=text.substr(i,2);check(part.find_first_not_of("0123456789abcdef")==std::string::npos,"hex character");out.push_back(static_cast<char>(std::stoul(part,nullptr,16)));}return out;}
static std::string hex(const std::string& bytes){std::ostringstream out;for(unsigned char c:bytes)out<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c);return bytes.empty()?"-":out.str();}
int main(int argc,char** argv){
    fd::DomainParticipant* participant=nullptr;int result=0;bool written=false;
    try{
        check(argc==5,"usage: d2_peer DOMAIN TOPIC REP CONTROL_DIR");unsigned domain=std::stoul(argv[1]),rep=std::stoul(argv[3]);check(domain<=225&&(rep==0||rep==2),"DDS options");std::filesystem::path dir=argv[4];
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        participant=fd::DomainParticipantFactory::get_instance()->create_participant(domain,pq);check(participant,"participant");fd::TypeSupport type(new Standards::BoundedRecordPubSubType());check(type.register_type(participant)==OK,"registertype");
        auto* topic=participant->create_topic(argv[2],type.get_type_name(),fd::TOPIC_QOS_DEFAULT);check(topic,"topic");auto* pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);check(pub&&sub,"pubsub");
        fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.history().depth=16;wq.data_sharing().off();wq.writer_data_lifecycle().autodispose_unregistered_instances=false;wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
        fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.history().depth=16;rq.data_sharing().off();
        for(auto* limits:{&wq.resource_limits(),&rq.resource_limits()}){limits->max_instances=65;limits->max_samples_per_instance=16;limits->max_samples=1040;limits->allocated_samples=16;}
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
        rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
        rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        auto* writer=pub->create_datawriter(topic,wq);auto* reader=sub->create_datareader(topic,rq);check(writer&&reader,"writerreader");
        std::map<uint32_t,fd::InstanceHandle_t> handles,observed;uint64_t serial=0;auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);std::cout<<"D2_PEER_READY"<<std::endl;
        while(!std::filesystem::exists(dir/"stop")){
            check(std::chrono::steady_clock::now()<deadline,"peer deadline");
            fd::PublicationMatchedStatus match;check(writer->get_publication_matched_status(match)==OK,"match");
            fd::SubscriptionMatchedStatus receiving;check(reader->get_subscription_matched_status(receiving)==OK,"reader match");
            std::ifstream file(dir/("command-"+std::to_string(serial+1)));uint64_t next;std::string op,label,payload;uint32_t id,seq;
            // Each endpoint has a local match; require the gateway in both directions before commands.
            if(match.current_count>1&&receiving.current_count>1&&(file>>next>>op>>id>>seq>>label>>payload)&&next==serial+1){
                Standards::BoundedRecord r;r.id(id);r.sequence_number(seq);r.label(decode(label));auto raw=decode(payload);r.payload(std::vector<uint8_t>(raw.begin(),raw.end()));check(r.label().size()<=64&&r.payload().size()<=256,"record bounds");
                if(op=="observe"){
                    // Supply an expected key identity, never a valid sample or gateway cache value.
                    fd::InstanceHandle_t expected;
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
                    check(type->getKey(&r,&expected),"expected key");
#else
                    check(type->compute_key(&r,expected),"expected key");
#endif
                    observed[id]=expected;check(observed.size()<=65,"expected key bound");
                }else if(op=="write"){
                    if(!handles.count(id)){auto h=writer->register_instance(&r);check(h.isDefined(),"register key");handles.emplace(id,h);}
                    check(writer->write(&r,handles.at(id))==OK,"write");
                    written=true;
                }else{check(handles.count(id),"lifecycle requires owned key");auto code=op=="dispose"?writer->dispose(&r,handles.at(id)):writer->unregister_instance(&r,handles.at(id));check(code==OK,"lifecycle");if(op=="unregister")handles.erase(id);else check(op=="dispose","unknown operation");}
                serial=next;std::cout<<"D2_PEER_SENT serial="<<serial<<" op="<<op<<" key="<<id<<" writer_matches="<<match.current_count<<" reader_matches="<<receiving.current_count<<std::endl;
            }
            for(unsigned budget=0;budget<16;++budget){Standards::BoundedRecord r;fd::SampleInfo info;auto code=reader->take_next_sample(&r,&info);if(code==EMPTY)break;check(code==OK,"take");
                if(info.valid_data){observed[r.id()]=info.instance_handle;check(observed.size()<=65,"observed key bound");}
                else{auto found=std::find_if(observed.begin(),observed.end(),[&](const auto& item){return item.second==info.instance_handle;});check(found!=observed.end(),"lifecycle without observed key");r.id(found->first);}
                std::cout<<"D2_PEER_DATA key="<<r.id()<<" valid="<<info.valid_data<<" state="<<static_cast<unsigned>(info.instance_state);if(info.valid_data)std::cout<<" sequence="<<r.sequence_number()<<" label="<<hex(r.label())<<" payload="<<hex(std::string(r.payload().begin(),r.payload().end()));std::cout<<std::endl;}
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
        if(written)check(writer->wait_for_acknowledgments({5,0})==OK,"ACK");
    }catch(const std::exception& e){std::cerr<<"D2_PEER_FAIL "<<e.what()<<std::endl;result=3;}
    if(participant){if(participant->delete_contained_entities()!=OK)result=3;else if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant)!=OK)result=3;}
    std::cout<<"D2_PEER_CLOSED exit="<<result<<std::endl;return result;
}
