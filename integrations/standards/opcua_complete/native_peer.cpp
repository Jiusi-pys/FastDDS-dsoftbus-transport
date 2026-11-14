// Independent fixture: generated data types and public DDS only; no D3/D4 core.
#include D3_HEADER
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
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>
namespace fd=eprosima::fastdds::dds;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static uint32_t integer(const std::string& s){require(!s.empty()&&s.size()<=10&&s.find_first_not_of("0123456789")==std::string::npos,"peer integer");auto n=std::stoull(s);require(n<=UINT32_MAX,"peer integer range");return static_cast<uint32_t>(n);}
static std::vector<uint8_t> bytes(const std::string& s,size_t max){require(s.size()%2==0&&s.size()<=2*max,"peer hex length");std::vector<uint8_t> v;for(size_t i=0;i<s.size();i+=2){auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("peer hex digit");};v.push_back(static_cast<uint8_t>(digit(s[i])*16+digit(s[i+1])));}return v;}
static std::string hex(const std::vector<uint8_t>& data){const char* digits="0123456789abcdef";std::string s;for(auto b:data){s+=digits[b>>4];s+=digits[b&15];}return s;}
static std::string token(const rt::SampleIdentity& id){std::vector<uint8_t> v;for(unsigned i=0;i<16;++i)v.push_back(i<12?id.writer_guid().guidPrefix.value[i]:id.writer_guid().entityId.value[i-12]);return hex(v)+":"+std::to_string((uint64_t(uint32_t(id.sequence_number().high))<<32)|id.sequence_number().low);}
int main(int argc,char** argv){
    fd::DomainParticipant* owner=nullptr;fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;bool written=false;int code=0;
    try{
        require(argc==6,"usage: d4_native_peer rpc|publish DOMAIN NAME REP DIR");std::string mode=argv[1],name=argv[3];unsigned domain=integer(argv[2]),rep=integer(argv[4]);fs::path directory=fs::absolute(argv[5]);
        require((mode=="rpc"||mode=="publish")&&domain<=225&&(rep==0||rep==2),"peer configuration");auto end=Clock::now()+std::chrono::seconds(90);
        auto pause=[&]{require(Clock::now()<end,"peer supervision deadline");std::this_thread::sleep_for(std::chrono::milliseconds(2));};
        fd::DomainParticipantQos pq;pq.transport().use_builtin_transports=false;pq.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());owner=fd::DomainParticipantFactory::get_instance()->create_participant(domain,pq);require(owner,"peer participant");
        fd::TypeSupport type(mode=="rpc"?static_cast<fd::TopicDataType*>(new Standards::RpcMessagePubSubType):static_cast<fd::TopicDataType*>(new Standards::BoundedRecordPubSubType));require(type.register_type(owner)==GOOD,"peer type");
        auto* tx=owner->create_topic(mode=="rpc"?name+"_Request":name,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);auto* pub=owner->create_publisher(fd::PUBLISHER_QOS_DEFAULT);require(tx&&pub,"peer topic/pub");
        fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.reliability().max_blocking_time={0,100000000};wq.history().kind=fd::KEEP_ALL_HISTORY_QOS;wq.data_sharing().off();
        wq.resource_limits().max_instances=5;wq.resource_limits().max_samples_per_instance=128;wq.resource_limits().max_samples=640;wq.resource_limits().allocated_samples=16;wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
        writer=pub->create_datawriter(tx,wq);require(writer,"peer writer");
        if(mode=="rpc"){
            auto* rx=owner->create_topic(name+"_Reply",type.get_type_name(),fd::TOPIC_QOS_DEFAULT);auto* sub=owner->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);require(rx&&sub,"peer reply topic/sub");
            fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.data_sharing().off();rq.resource_limits().max_samples=256;rq.resource_limits().max_instances=1;rq.resource_limits().max_samples_per_instance=256;rq.resource_limits().allocated_samples=16;
#if defined(D3_FASTDDS_214)
            rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
            rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
            reader=sub->create_datareader(rx,rq);require(reader,"peer reader");
        }
        for(;;){require(!fs::exists(directory/"stop"),"peer stopped before discovery");fd::PublicationMatchedStatus txs;require(writer->get_publication_matched_status(txs)==GOOD,"peer match");bool matched=txs.current_count>=1;
            if(reader){fd::SubscriptionMatchedStatus rxs;require(reader->get_subscription_matched_status(rxs)==GOOD,"peer reader match");matched=matched&&rxs.current_count>=1;}if(matched)break;pause();}
        std::cout<<"D4_PEER_READY"<<std::endl;unsigned index=0;std::map<uint32_t,fd::InstanceHandle_t> handles;int last_matches=-1;unsigned match_event=0;
        while(!fs::exists(directory/"stop")){
            fd::PublicationMatchedStatus matches;require(writer->get_publication_matched_status(matches)==GOOD,"peer current matches");
            if(matches.current_count!=last_matches){last_matches=matches.current_count;std::cout<<"D4_PEER_MATCH event="<<match_event++<<" count="<<last_matches<<std::endl;}
            auto file=directory/("request-"+std::to_string(index)+".txt");if(!fs::exists(file)){pause();continue;}
            if(matches.current_count<1){pause();continue;}
            require(fs::file_size(file)<=9000,"peer control size");std::ifstream input(file);std::vector<std::string> lines;std::string line;while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);}require(input.eof(),"peer control read");
            uint32_t status=0;std::vector<uint8_t> response;std::string id="none";
            if(mode=="rpc"){
                require(lines.size()==3,"peer RPC command");Standards::RpcMessage message;message.operation(integer(lines[0]));message.label(lines[1]);message.payload(bytes(lines[2],4096));require(message.label().size()<=64,"peer label");rt::WriteParams p;
#if defined(D3_FASTDDS_214)
                require(writer->write(&message,p),"peer RPC write");
#else
                require(writer->write(&message,p)==GOOD,"peer RPC write");
#endif
                written=true;auto identity=p.sample_identity();require(identity!=rt::SampleIdentity::unknown(),"peer assigned identity");id=token(identity);std::cout<<"D4_PEER_SENT index="<<index<<" token="<<id<<" op="<<message.operation()<<" payload="<<lines[2]<<std::endl;
                auto deadline=Clock::now()+std::chrono::seconds(8);
                for(;;){require(Clock::now()<deadline,"peer RPC response deadline");Standards::RpcMessage reply;fd::SampleInfo info;auto result=reader->take_next_sample(&reply,&info);require(result==GOOD||result==EMPTY,"peer take");
                    if(result==GOOD&&info.valid_data&&info.related_sample_identity==identity){require(reply.operation()==message.operation()&&reply.label()==message.label()&&reply.payload().size()<=4096,"peer reply metadata");status=reply.status();response=reply.payload();break;}pause();}
            }else{
                require(lines.size()==7,"peer record command");Standards::BoundedRecord record;record.id(integer(lines[1]));record.sequence_number(integer(lines[2]));auto label=bytes(lines[5],64);record.label(std::string(label.begin(),label.end()));record.payload(bytes(lines[6],256));require(record.label().find('\0')==std::string::npos,"peer record NUL");
                auto it=handles.find(record.id());if(it==handles.end()){require(handles.size()<4,"peer key capacity");auto handle=writer->register_instance(&record);require(handle.isDefined(),"peer register");it=handles.emplace(record.id(),handle).first;}
                auto sec=integer(lines[3]),nsec=integer(lines[4]);require(sec<=INT32_MAX&&nsec<1000000000,"peer timestamp bounds");
#if defined(D3_FASTDDS_214)
                eprosima::fastrtps::Time_t stamp{static_cast<int32_t>(sec),nsec};
#else
                fd::Time_t stamp{static_cast<int32_t>(sec),nsec};
#endif
                if(lines[0]=="write")require(writer->write_w_timestamp(&record,it->second,stamp)==GOOD,"peer timestamped write");
                else if(lines[0]=="dispose")require(writer->dispose(&record,it->second)==GOOD,"peer dispose");else throw std::runtime_error("peer record operation");
                // The independent UA oracle waits for actual delivery after each write.
                // Check acknowledgments for all published samples at the shutdown boundary.
                written=true;
                std::cout<<"D4_PEER_PUBLISHED index="<<index<<" key="<<record.id()<<" sequence="<<record.sequence_number()<<" source_seconds="<<sec<<" source_nanos="<<nsec<<" label="<<lines[5]<<" payload="<<lines[6]<<std::endl;
            }
            auto temp=directory/("response-"+std::to_string(index)+".tmp");std::ofstream out(temp);out<<status<<'\n'<<hex(response)<<'\n'<<id<<'\n';out.close();require(bool(out),"peer response file");fs::rename(temp,directory/("response-"+std::to_string(index)+".txt"));++index;pause();
        }
    }catch(const std::exception& error){std::cerr<<"D4_PEER_FAIL "<<error.what()<<std::endl;code=3;}
    if(owner){if(written&&writer&&writer->wait_for_acknowledgments({5,0})!=GOOD)code=3;
        if(owner->delete_contained_entities()!=GOOD)code=3;else if(fd::DomainParticipantFactory::get_instance()->delete_participant(owner)!=GOOD)code=3;else owner=nullptr;}
    std::cout<<"D4_PEER_CLOSED exit="<<code<<" owners="<<(owner?1:0)<<std::endl;return code;
}
