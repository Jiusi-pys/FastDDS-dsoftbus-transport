// Independent native DDS peer: no HTTP, XML, Gateway or c1_core dependency.
#include B3_HEADER
#include "peer_run.hpp"
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
const auto NO_DATA=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
const auto OK=eprosima::fastdds::dds::RETCODE_OK;
const auto NO_DATA=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <iostream>
#include <thread>
#include <chrono>
#include <array>
#include <stdexcept>
namespace fd=eprosima::fastdds::dds;
static void check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
static Standards::BoundedRecord sample(unsigned n) {
    Standards::BoundedRecord s;s.id(17+n);s.sequence_number(100+n);s.label("C1&<"+std::to_string(n));
    std::vector<uint8_t> bytes(n%2?256:0);for(size_t i=0;i<bytes.size();++i)bytes[i]=static_cast<uint8_t>(i+n);s.payload(bytes);return s;
}
int run_peer(int argc,char** argv,const PeerTransport& transport,std::ostream& out,std::ostream& err) {
    fd::DomainParticipant* p=nullptr;auto* factory=fd::DomainParticipantFactory::get_instance();
    try {
        check(argc==6,"usage: writer|reader|reader-ready DOMAIN TOPIC REPRESENTATION REGISTERED_TYPE");
        auto role=std::string(argv[1]);check(role=="writer"||role=="reader"||role=="reader-ready","invalid role");int rep=std::stoi(argv[4]);check(rep==0||rep==2,"invalid representation");
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;
        if(transport.configure)transport.configure(static_cast<unsigned>(std::stoul(argv[2])),pq);
        else pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
        p=factory->create_participant(static_cast<unsigned>(std::stoul(argv[2])),pq);check(p,"participant");
        fd::TypeSupport type(new Standards::BoundedRecordPubSubType);check(type.register_type(p,argv[5])==OK,"register type");
        auto* topic=p->create_topic(argv[3],argv[5],fd::TOPIC_QOS_DEFAULT);check(topic,"topic");
        auto until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        if(role=="writer") {
            auto* pub=p->create_publisher(fd::PUBLISHER_QOS_DEFAULT);check(pub,"publisher");fd::DataWriterQos q;
            q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;q.history().depth=16;q.data_sharing().off();q.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
            auto* w=pub->create_datawriter(topic,q);check(w,"writer");bool matched=false;
            while(std::chrono::steady_clock::now()<until){check(!transport.healthy||transport.healthy(),"transport unavailable");fd::PublicationMatchedStatus s;check(w->get_publication_matched_status(s)==OK,"match status");if(s.current_count>0){matched=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            check(matched,"discovery timeout");out<<"C1_PEER_MATCHED\n"<<std::flush;
            for(unsigned i=0;i<10;++i){auto value=sample(i);
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
                check(w->write(&value),"write");
#else
                check(w->write(&value)==OK,"write");
#endif
            }
            check(w->wait_for_acknowledgments({5,0})==OK,"ACK");
        } else {
            auto* sub=p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);check(sub,"subscriber");fd::DataReaderQos q;
            q.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;q.history().depth=16;q.data_sharing().off();
#if defined(FASTDDS_DSOFTBUS_FASTDDS_214)
            q.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
            q.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
            auto* r=sub->create_datareader(topic,q);check(r,"reader");bool matched=false;
            while(std::chrono::steady_clock::now()<until){check(!transport.healthy||transport.healthy(),"transport unavailable");fd::SubscriptionMatchedStatus s;check(r->get_subscription_matched_status(s)==OK,"match status");if(s.current_count>0){matched=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
            check(matched,"discovery timeout");out<<"C1_PEER_MATCHED\n"<<std::flush;
            bool ready=role!="reader-ready";
            if(!ready)until=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            std::array<bool,10> seen{};bool complete=false;
            while(std::chrono::steady_clock::now()<until) {
                check(!transport.healthy||transport.healthy(),"transport unavailable");
                Standards::BoundedRecord value;fd::SampleInfo info;auto code=r->take_next_sample(&value,&info);
                if(code==OK&&info.valid_data){
                    if(role=="reader-ready"&&value.id()==17&&value.sequence_number()==UINT32_MAX&&value.label()=="C2_READY"&&value.payload().empty()) {
                        if(!ready){ready=true;until=std::chrono::steady_clock::now()+std::chrono::seconds(15);out<<"C1_PEER_READY\n"<<std::flush;}
                        continue;
                    }
                    check(ready,"data arrived before readiness barrier");check(value.id()>=17&&value.id()<27,"id");unsigned n=value.id()-17;check(value==sample(n),"exact sample");seen[n]=true;
                }
                else check(code==OK||code==NO_DATA,"take");
                complete=std::all_of(seen.begin(),seen.end(),[](bool b){return b;});if(complete)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if(!complete){err<<"C1_PEER_SEEN";for(size_t i=0;i<seen.size();++i)err<<" "<<i<<"="<<seen[i];err<<"\n";}
            check(complete,"data timeout");
        }
        check(p->delete_contained_entities()==OK,"delete entities");check(factory->delete_participant(p)==OK,"delete participant");p=nullptr;
        out<<"C1_PEER PASS role="<<role<<"\n";return 0;
    } catch(const std::exception& e) {
        err<<"C1_PEER FAIL "<<e.what()<<"\n";
        if(p){if(p->delete_contained_entities()!=OK||factory->delete_participant(p)!=OK)err<<"C1_PEER CLEANUP FAIL\n";}
        return 1;
    }
}
#ifndef C4_PEER_LIBRARY
int main(int argc,char** argv){return run_peer(argc,argv,{},std::cout,std::cerr);}
#endif
