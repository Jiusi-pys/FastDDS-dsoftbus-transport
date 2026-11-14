// Independent SDK fixture: no D3 headers, implementation, tables or library linkage.
#include D3_HEADER
#include "../cpp_backend/participant_transport.hpp"
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
namespace rt=eprosima::fastrtps::rtps;
inline const auto GOOD=eprosima::fastrtps::types::ReturnCode_t::RETCODE_OK;
inline const auto EMPTY=eprosima::fastrtps::types::ReturnCode_t::RETCODE_NO_DATA;
#else
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
namespace rt=eprosima::fastdds::rtps;
inline const auto GOOD=eprosima::fastdds::dds::RETCODE_OK;
inline const auto EMPTY=eprosima::fastdds::dds::RETCODE_NO_DATA;
#endif
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>
namespace fd=eprosima::fastdds::dds;
using Message=Standards::RpcMessage;
using Identity=rt::SampleIdentity;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static std::string hex(const std::string& bytes){std::string out;const char* h="0123456789abcdef";for(unsigned char c:bytes){out+=h[c>>4];out+=h[c&15];}return out.empty()?"-":out;}
static std::string token(const Identity& id){
    std::string bytes;for(unsigned n=0;n<16;++n)bytes.push_back(char(n<12?id.writer_guid().guidPrefix.value[n]:id.writer_guid().entityId.value[n-12]));
    return hex(bytes)+":"+std::to_string(uint64_t(uint32_t(id.sequence_number().high))*4294967296ULL+id.sequence_number().low);
}
static void show(const char* event,const Identity& id,const Message& m){
    std::cout<<event<<" token="<<token(id)<<" operation="<<m.operation()<<" status="<<m.status()<<" label="<<hex(m.label())<<" payload="<<hex(std::string(m.payload().begin(),m.payload().end()))<<std::endl;
}
static Message response(Message m){m.status(m.operation()==UINT32_MAX?41:0);std::reverse(m.payload().begin(),m.payload().end());return m;}
static bool same(const Message& a,const Message& b){return a.operation()==b.operation()&&a.status()==b.status()&&a.label()==b.label()&&a.payload()==b.payload();}
static Message input(unsigned n,const std::string& tag){
    Message m;m.operation(n==2?UINT32_MAX:n==0?0:42);m.label(tag);
    if(n==1)m.label(std::string(1,char(128))+std::string(63,'x'));
    if(n==2){m.payload().resize(4096);for(unsigned j=0;j<4096;++j)m.payload()[j]=static_cast<uint8_t>(j);}
    else if(n)m.payload({0,255,127,static_cast<uint8_t>(n)});
    return m;
}
int main(int argc,char** argv){
    fd::DomainParticipant* participant=nullptr;fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;
    bool written=false;int result=0;
    try{
        check(argc==9,"usage: d3_peer ROLE DOMAIN SERVICE REP MODE DIR PEERS COUNT");
        std::string role=argv[1],service=argv[3],mode=argv[5];unsigned domain=std::stoul(argv[2]),rep=std::stoul(argv[4]),peers=std::stoul(argv[7]),count=std::stoul(argv[8]);
        check((role=="client"||role=="server")&&domain<=225&&(rep==0||rep==2)&&peers>=1&&peers<=4&&count>=1&&count<=16,"peer bounds");
        fs::path dir=fs::absolute(argv[6]);auto end=Clock::now()+std::chrono::seconds(60);
        fd::DomainParticipantQos pq;standards_transport::configure(domain,pq);
        participant=fd::DomainParticipantFactory::get_instance()->create_participant(domain,pq);check(participant,"peer participant");
        fd::TypeSupport type(new Standards::RpcMessagePubSubType);check(type.register_type(participant)==GOOD,"peer type");
        auto* request=participant->create_topic(service+"_Request",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* reply=participant->create_topic(service+"_Reply",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);check(request&&reply,"peer topics");
        auto* publisher=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* subscriber=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);check(publisher&&subscriber,"peer pubsub");
        fd::DataWriterQos wq;wq.history().kind=fd::KEEP_ALL_HISTORY_QOS;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.reliability().max_blocking_time={0,100000000};wq.data_sharing().off();
        wq.resource_limits().max_instances=1;wq.resource_limits().max_samples=256;wq.resource_limits().max_samples_per_instance=256;wq.resource_limits().allocated_samples=16;
        wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
        fd::DataReaderQos rq;rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.data_sharing().off();
        rq.resource_limits().max_instances=1;rq.resource_limits().max_samples=256;rq.resource_limits().max_samples_per_instance=256;rq.resource_limits().allocated_samples=16;
#if defined(D3_FASTDDS_214)
        rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
        rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        writer=publisher->create_datawriter(role=="client"?request:reply,wq);reader=subscriber->create_datareader(role=="client"?reply:request,rq);check(writer&&reader,"peer endpoints");
        auto pause=[&]{check(Clock::now()<end,"peer deadline");std::this_thread::sleep_for(std::chrono::milliseconds(2));};
        for(;;){fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus b;check(writer->get_publication_matched_status(a)==GOOD&&reader->get_subscription_matched_status(b)==GOOD,"peer match status");if(a.current_count>=int(peers)&&b.current_count>=int(peers))break;pause();}
        std::cout<<"D3_READY role="<<role<<std::endl;while(!fs::exists(dir/"go"))pause();
        auto send=[&](Message value,const Identity& related){
            rt::WriteParams p;p.related_sample_identity(related);
#if defined(D3_FASTDDS_214)
            check(writer->write(&value,p),"peer native write");
#else
            check(writer->write(&value,p)==GOOD,"peer native write");
#endif
            written=true;check(p.sample_identity()!=Identity::unknown(),"peer assigned identity");return p.sample_identity();
        };
        auto take=[&](Message& m,fd::SampleInfo& info){auto code=reader->take_next_sample(&m,&info);if(code==EMPTY)return false;check(code==GOOD&&info.valid_data,"peer native take");return true;};
        if(role=="server"){
            struct Request{Identity id;Message value;};std::vector<Request> held;unsigned received=0,sent=0;bool injected=false;
            while(received<count||!held.empty()){
                Message value;fd::SampleInfo info;
                if(take(value,info)){check(info.sample_identity!=Identity::unknown()&&info.related_sample_identity==Identity::unknown(),"peer request metadata");held.push_back({info.sample_identity,value});++received;show("D3_PEER_REQUEST",info.sample_identity,value);}
                unsigned batch=mode=="reverse"?count:mode=="capacity"?2:1;
                if(held.size()>=batch||(received==count&&!held.empty())){
                    if(mode=="drop"){held.clear();continue;}
                    if(mode=="wrong"&&sent==0&&!injected){
                        auto id=held[0].id;auto evil=response(held[0].value);evil.status(777);
                        send(evil,Identity::unknown());auto wrong=id;wrong.writer_guid().guidPrefix.value[0]^=128;send(evil,wrong);
                        wrong=id;wrong.sequence_number().low+=1000;send(evil,wrong);injected=true;std::cout<<"D3_PEER_BAD_SENT count=3"<<std::endl;
                    }
                    if((mode=="wrong"||mode=="hold"||(mode=="capacity"&&sent==0))&&!fs::exists(dir/"continue")){pause();continue;}
                    while(!held.empty()){
                        auto item=held.back();held.pop_back();auto answer=response(item.value);send(answer,item.id);++sent;show("D3_PEER_REPLY",item.id,answer);
                        if(mode=="duplicate"){
                            while(!fs::exists(dir/"continue"))pause();answer.status(777);answer.payload({255});send(answer,item.id);std::cout<<"D3_PEER_DUPLICATE_SENT"<<std::endl;
                        }
                    }
                }
                pause();
            }
        }else{
            std::map<Identity,Message> expected;for(unsigned n=0;n<count;++n){auto m=input(n,dir.filename().string());auto id=send(m,Identity::unknown());expected.emplace(id,response(m));show("D3_PEER_SUBMITTED",id,m);}
            if(mode=="drop"){
                auto until=Clock::now()+std::chrono::milliseconds(300);
                while(Clock::now()<until){Message m;fd::SampleInfo i;check(!take(m,i),"peer drop received unexpected reply");pause();}
                std::cout<<"D3_PEER_TIMEOUT count="<<count<<std::endl;
            }else{
                unsigned complete=0;
                while(complete<count){Message m;fd::SampleInfo i;if(take(m,i)){
                    // The shared reply Topic also carries replies for other request writers.
                    if(i.related_sample_identity.writer_guid()!=expected.begin()->first.writer_guid()){pause();continue;}
                    auto it=expected.find(i.related_sample_identity);check(it!=expected.end()&&same(m,it->second),"peer exact correlated reply");show("D3_PEER_RESULT",i.related_sample_identity,m);expected.erase(it);++complete;
                }pause();}
                check(expected.empty(),"peer reply completeness");
            }
        }
        std::cout<<"D3_DONE role="<<role<<std::endl;while(!fs::exists(dir/"stop"))pause();
        if(written){auto code=writer->wait_for_acknowledgments({5,0});std::cout<<"D3_PEER_ACK ok="<<(code==GOOD)<<std::endl;check(code==GOOD,"peer ACK");}
    }catch(const std::exception& error){std::cerr<<"D3_PEER_FAIL "<<error.what()<<std::endl;result=3;}
    if(participant){if(participant->delete_contained_entities()!=GOOD)result=3;else if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant)!=GOOD)result=3;else participant=nullptr;}
    std::cout<<"D3_PEER_CLOSED exit="<<result<<" owners="<<(participant?1:0)<<std::endl;return result;
}
