// Independent public-DDS jobs share one participant, never one writer identity.
// The selected type is registered once on this owner thread. No D3/D4 core.
#include "../cpp_backend/participant_transport.hpp"
#include "../web_gateway/c4_transport.hpp"
#include "../web_gateway/c4_shutdown.hpp"
#define main d5_unused_peer_main
#include "../opcua_complete/native_peer.cpp"
#undef main
struct Job {
    fs::path directory;std::ofstream log;fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;
    unsigned index=0,event=0;int matched=-1;bool ready=false,written=false,closed=false;
    std::map<uint32_t,fd::InstanceHandle_t> handles;
};
int main(int argc,char** argv){
    std::unique_ptr<c4::Transport> transport;fd::DomainParticipant* participant=nullptr;
    fd::Publisher* pub=nullptr;fd::Subscriber* sub=nullptr;std::vector<std::unique_ptr<Job>> jobs;int result=0;
    try{
        require(argc==8,"usage: d4_peer TRANSPORT MODE DOMAIN TOPIC REP ROOT COUNT");
        std::string mode=argv[2],name=argv[4];auto domain=integer(argv[3]),rep=integer(argv[5]),count=integer(argv[7]);
        require((mode=="rpc"||mode=="publish")&&domain==219&&(rep==0||rep==2)&&count>=1&&count<=5,"D5 peer options");
        transport=std::make_unique<c4::Transport>(argv[1]);transport->start();
        fd::DomainParticipantQos pq;transport->configure(domain,pq);
        participant=fd::DomainParticipantFactory::get_instance()->create_participant(domain,pq);require(participant,"D5 peer participant");
        fd::TypeSupport type(mode=="rpc"?static_cast<fd::TopicDataType*>(new Standards::RpcMessagePubSubType):static_cast<fd::TopicDataType*>(new Standards::BoundedRecordPubSubType));
        require(type.register_type(participant)==GOOD,"D5 peer type");
        auto* tx=participant->create_topic(mode=="rpc"?name+"_Request":name,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);
        auto* rx=mode=="rpc"?participant->create_topic(name+"_Reply",type.get_type_name(),fd::TOPIC_QOS_DEFAULT):nullptr;
        pub=participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);if(mode=="rpc")sub=participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);
        require(tx&&pub&&(mode!="rpc"||(rx&&sub)),"D5 peer entities");
        fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.reliability().max_blocking_time={0,100000000};wq.history().kind=fd::KEEP_ALL_HISTORY_QOS;wq.data_sharing().off();
        wq.resource_limits().max_instances=5;wq.resource_limits().max_samples_per_instance=128;wq.resource_limits().max_samples=640;wq.resource_limits().allocated_samples=16;wq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
        fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.data_sharing().off();rq.resource_limits().max_samples=256;rq.resource_limits().max_instances=1;rq.resource_limits().max_samples_per_instance=256;rq.resource_limits().allocated_samples=16;
#if defined(D3_FASTDDS_214)
        rq.type_consistency().representation.m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#else
        rq.representation().m_value={static_cast<fd::DataRepresentationId_t>(rep)};
#endif
        for(unsigned slot=0;slot<count;++slot){
            auto job=std::make_unique<Job>();job->directory=fs::path(argv[6])/std::to_string(slot);job->log.open(job->directory/"process.log");require(bool(job->log),"D5 peer job log");
            job->writer=pub->create_datawriter(tx,wq);if(mode=="rpc")job->reader=sub->create_datareader(rx,rq);
            require(job->writer&&(mode!="rpc"||job->reader),"D5 peer endpoints");jobs.push_back(std::move(job));
        }
        auto deadline=Clock::now()+std::chrono::seconds(90);unsigned closed=0;
        while(closed<count){
            require(Clock::now()<deadline,"D5 peer supervision deadline");
            for(auto& owned:jobs){auto& job=*owned;if(job.closed)continue;
                if(fs::exists(job.directory/"stop")){
                    if(job.written)require(job.writer->wait_for_acknowledgments({5,0})==GOOD,"D5 peer ACK");
                    if(job.reader){require(sub->delete_datareader(job.reader)==GOOD,"D5 peer reader close");job.reader=nullptr;}
                    require(pub->delete_datawriter(job.writer)==GOOD,"D5 peer writer close");job.writer=nullptr;job.closed=true;++closed;
                    job.log<<"D4_PEER_CLOSED exit=0 owners=0"<<std::endl;continue;
                }
                fd::PublicationMatchedStatus txs;require(job.writer->get_publication_matched_status(txs)==GOOD,"D5 writer match");bool matched=txs.current_count>=1;
                if(job.reader){fd::SubscriptionMatchedStatus rxs;require(job.reader->get_subscription_matched_status(rxs)==GOOD,"D5 reader match");matched=matched&&rxs.current_count>=1;}
                if(matched&&!job.ready){job.ready=true;job.log<<"D4_PEER_READY"<<std::endl;}
                if(txs.current_count!=job.matched){job.matched=txs.current_count;job.log<<"D4_PEER_MATCH event="<<job.event++<<" count="<<job.matched<<std::endl;}
                auto file=job.directory/("request-"+std::to_string(job.index)+".txt");if(!matched||!fs::exists(file))continue;
                require(fs::file_size(file)<=9000,"D5 peer command bound");std::ifstream input(file);std::vector<std::string> lines;std::string line;
                while(std::getline(input,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);}require(input.eof(),"D5 peer command read");
                uint32_t status=0;std::vector<uint8_t> response;std::string id="none";
                if(mode=="rpc"){
                    require(lines.size()==3,"D5 RPC command");Standards::RpcMessage message;message.operation(integer(lines[0]));message.label(lines[1]);message.payload(bytes(lines[2],4096));require(message.label().size()<=64,"D5 label");rt::WriteParams parameters;
#if defined(D3_FASTDDS_214)
                    require(job.writer->write(&message,parameters),"D5 RPC write");
#else
                    require(job.writer->write(&message,parameters)==GOOD,"D5 RPC write");
#endif
                    job.written=true;auto identity=parameters.sample_identity();require(identity!=rt::SampleIdentity::unknown(),"D5 assigned identity");id=token(identity);
                    job.log<<"D4_PEER_SENT index="<<job.index<<" token="<<id<<" op="<<message.operation()<<" payload="<<lines[2]<<std::endl;
                    auto until=Clock::now()+std::chrono::seconds(8);
                    for(;;){require(Clock::now()<until,"D5 RPC reply deadline");Standards::RpcMessage reply;fd::SampleInfo info;auto code=job.reader->take_next_sample(&reply,&info);require(code==GOOD||code==EMPTY,"D5 RPC take");
                        if(code==GOOD&&info.valid_data&&info.related_sample_identity==identity){require(reply.operation()==message.operation()&&reply.label()==message.label()&&reply.payload().size()<=4096,"D5 RPC metadata");status=reply.status();response=reply.payload();break;}
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));}
                }else{
                    require(lines.size()==7,"D5 record command");Standards::BoundedRecord record;record.id(integer(lines[1]));record.sequence_number(integer(lines[2]));auto label=bytes(lines[5],64);record.label(std::string(label.begin(),label.end()));record.payload(bytes(lines[6],256));require(record.label().find('\0')==std::string::npos,"D5 record NUL");
                    auto found=job.handles.find(record.id());if(found==job.handles.end()){require(job.handles.size()<4,"D5 peer key capacity");auto handle=job.writer->register_instance(&record);require(handle.isDefined(),"D5 key registration");found=job.handles.emplace(record.id(),handle).first;}
                    auto sec=integer(lines[3]),nsec=integer(lines[4]);require(sec<=INT32_MAX&&nsec<1000000000,"D5 timestamp");
#if defined(D3_FASTDDS_214)
                    eprosima::fastrtps::Time_t stamp{static_cast<int32_t>(sec),nsec};
#else
                    fd::Time_t stamp{static_cast<int32_t>(sec),nsec};
#endif
                    if(lines[0]=="write")require(job.writer->write_w_timestamp(&record,found->second,stamp)==GOOD,"D5 record write");
                    else if(lines[0]=="dispose")require(job.writer->dispose(&record,found->second)==GOOD,"D5 record dispose");else throw std::runtime_error("D5 record operation");
                    job.written=true;job.log<<"D4_PEER_PUBLISHED index="<<job.index<<" key="<<record.id()<<" sequence="<<record.sequence_number()<<" source_seconds="<<sec<<" source_nanos="<<nsec<<" label="<<lines[5]<<" payload="<<lines[6]<<std::endl;
                }
                auto path=job.directory/("response-"+std::to_string(job.index));std::ofstream output(path.string()+".tmp");output<<status<<'\n'<<hex(response)<<'\n'<<id<<'\n';output.close();require(bool(output),"D5 peer response");fs::rename(path.string()+".tmp",path.string()+".txt");++job.index;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }catch(const std::exception& e){std::cerr<<"D5_PEER_FAIL "<<e.what()<<std::endl;result=1;}
    catch(...){std::cerr<<"D5_PEER_FAIL unknown"<<std::endl;result=1;}
    for(auto& job:jobs)if(!job->closed)job->log<<"D4_PEER_CLOSED exit=1 owners=1"<<std::endl;
    if(participant){if(participant->delete_contained_entities()!=GOOD)result=1;else if(fd::DomainParticipantFactory::get_instance()->delete_participant(participant)!=GOOD)result=1;else participant=nullptr;}
    if(transport){auto shutdown=c4::finish_transport([&]{return transport->close();},[&]{return transport->healthy();});if(!shutdown.success())result=1;}
    if(!result)std::cout<<"D5_SHUTDOWN PASS application=0 jobs="<<jobs.size()<<" participants=0"<<std::endl;
    return result;
}
