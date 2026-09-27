#include "PublicSocketDdsTestSupport.hpp"
namespace shared_dds {
using namespace public_dds;
namespace dds=eprosima::fastdds::dds;
namespace rtps=eprosima::fastdds::rtps;
// Complete owned/running records for this finite Host window; no duplicate
// per-stage owner prose rows. The native owner predicate is checked directly.
struct SharedObserver final:ProcessReceiveObserver {
    int side;std::shared_ptr<Trace> trace;
    SharedObserver(int s,std::shared_ptr<Trace> t):side(s),trace(std::move(t)){}
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& d,const std::uint8_t* p,std::size_t n)noexcept override {
        if(stage!=ReceiveStage::OwnedCopy&&stage!=ReceiveStage::Running)return;
        const auto owner=d.native.Observe();
        if(owner.role!=NativeOwnerRole::Accepted||!owner.owned_id)trace->Add({"TRACE_ERROR","","","wrong native owner",side});
        trace->Frame(stage==ReceiveStage::OwnedCopy?"RX_OWNED":"RX_RUNNING",side,p,n,d.correlation,d.identity.application_incarnation,d.identity.receiver_incarnation);
    }
};
struct App {
    std::shared_ptr<HostProviderLoop> view;
    std::shared_ptr<Descriptor> descriptor;
    dds::DomainParticipant* participant=nullptr;
    dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::string p,w,r;ReceiverIdentity receiver;
};
struct Node {
    std::shared_ptr<PublicSocketBackend> backend;
    std::shared_ptr<HostProviderLoop> runtime;
    App apps[2];std::thread pump;std::atomic<bool> stop{false};
};
struct Fixture {
    Node nodes[2];std::shared_ptr<Trace> trace=std::make_shared<Trace>();
    dds::TypeSupport type{new MddsKeyedSamplePubSubType};
    std::uint64_t run=std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    std::atomic<bool> finishing{false};
    Fixture(bool forward=true){
        public_socket_fake::Configure(trace,forward);
        for(int side=0;side<2;++side){auto& n=nodes[side];n.backend=std::make_shared<PublicSocketBackend>(PublicConfig(side));n.runtime=HostProviderLoop::CreateShared(n.backend,std::make_shared<SharedObserver>(side,trace));}
        public_socket_fake::SetSendObserver([&](int side,int id){const auto v=nodes[side].runtime->Observe();Require(id==101+100*side&&v.native.send.entered==1,"original shared outgoing ticket");trace->Add({"SHARED_SEND_TICKET","","","owner="+std::to_string(v.adapter.binding.owner_incarnation),side});});
    }
    ~Fixture(){finishing=true;for(int group:{0,1})for(int side:{1,0})try{Delete(side,group);}catch(...){trace->Add({"TRACE_ERROR","","","cleanup",side});}for(auto& n:nodes)n.stop=true;for(auto& n:nodes)if(n.pump.joinable())n.pump.join();public_socket_fake::SetSendObserver({});trace->Dump(run);}
    void Wait(const std::function<bool()>& f,const char* why){auto end=std::chrono::steady_clock::now()+8s;while(!f()){Require(trace->Good(),"trace or pump error");Require(std::chrono::steady_clock::now()<end,why);std::this_thread::sleep_for(5ms);}}
    void Create(int side,int group,bool first){
        auto& n=nodes[side];auto& a=n.apps[group];Require(!a.participant,"no overwritten DDS object");
        a.view=first?n.runtime:n.runtime->Join();Require(bool(a.view),"live runtime join");
        a.descriptor=std::make_shared<Descriptor>(side,a.view);auto qos=Qos(side,a.descriptor);qos.name(group?"Q":"P");
        a.participant=dds::DomainParticipantFactory::get_instance()->create_participant(152,qos);Require(a.participant,"real Participant creation");
        Require(a.descriptor->factories==1&&a.view->Observe().input_open,"factory creates independent receiver");
        const auto actual=a.participant->get_qos();Require(!actual.transport().use_builtin_transports&&actual.transport().user_transports.size()==1&&actual.transport().user_transports[0]==a.descriptor,"only selected facade");
        Require(type.register_type(a.participant)==dds::RETCODE_OK,"register bounded type");
        auto* topic=a.participant->create_topic(group?"SharedTopicQ":"SharedTopicP",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* pub=a.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto* sub=a.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);Require(topic&&pub&&sub,"entities");
        auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;EndpointQos(wq);EndpointQos(rq);wq.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;
        a.writer=pub->create_datawriter(topic,wq);a.reader=sub->create_datareader(topic,rq);Require(a.writer&&a.reader,"real endpoints");Require(EffectiveEndpoint(a.writer->get_qos())&&EffectiveEndpoint(a.reader->get_qos()),"effective QoS");
        a.p=Guid(a.participant->guid());a.w=Guid(a.writer->guid());a.r=Guid(a.reader->guid());a.receiver=a.view->Observe().receiver_identity;
        trace->Add({"SHARED_CREATE","",a.w,std::string(group?"Q":"P")+",participant="+a.p+",reader="+a.r,side,0,0,0,a.receiver.application_incarnation,a.receiver.receiver_incarnation});
    }
    void CreatePair(int group,bool first){std::exception_ptr errors[2];std::thread threads[2];for(int i=0;i<2;++i)threads[i]=std::thread([&,i]{try{Create(i,group,first);}catch(...){errors[i]=std::current_exception();}});for(auto& t:threads)t.join();for(auto& e:errors)if(e)std::rethrow_exception(e);
        if(first)for(int side=0;side<2;++side)nodes[side].pump=std::thread([&,side]{while(!nodes[side].stop){auto tx=nodes[side].runtime->PumpSend();public_socket_fake::PumpOne();auto rx=nodes[side].runtime->PumpReceive();if(!finishing&&((tx.outcome!=NativeSendOutcome::NotEntered&&(tx.outcome!=NativeSendOutcome::ReturnedSdkCode||tx.actual_return_code!=0))||(rx!=BackendStatus::None&&rx!=BackendStatus::NoWork)))trace->Add({"PUMP_ERROR","","","shared",side});std::this_thread::sleep_for(1ms);}});
    }
    std::set<std::string> Matches(int side,int group,bool readers){auto& a=nodes[side].apps[group];std::vector<dds::InstanceHandle_t> h;Require((readers?a.writer->get_matched_subscriptions(h):a.reader->get_matched_publications(h))==dds::RETCODE_OK,"match API");std::set<std::string> result;for(auto& v:h)result.insert(Handle(v));return result;}
    void Match(int group){Wait([&]{for(int i=0;i<2;++i)if(!CurrentMatch(Matches(i,group,true),nodes[1-i].apps[group].r)||!CurrentMatch(Matches(i,group,false),nodes[1-i].apps[group].w))return false;return true;},"current remote identities");}
    void Traffic(int group,unsigned first,unsigned last,const char* phase){
        for(int i=0;i<2;++i)for(unsigned ordinal=first;ordinal<=last;++ordinal){auto& a=nodes[i].apps[group];MddsKeyedSample sample;sample.key(1);sample.run(run);sample.writer(unsigned(1+i+2*group));sample.ordinal(ordinal);for(unsigned k=0;k<32;++k)sample.payload()[k]=std::uint8_t(k+ordinal+i+10*group);rtps::WriteParams params;Require(a.writer->write(&sample,params)==dds::RETCODE_OK,"one new write");trace->Add({"SHARED_WRITE","DATA",a.w,std::string(phase)+":"+std::to_string(ordinal),i,Sequence(params.sample_identity().sequence_number())});}
        for(int i=0;i<2;++i){auto& a=nodes[i].apps[group];std::vector<SampleReceipt> rows;Wait([&]{MddsKeyedSample sample;dds::SampleInfo info;for(;;){auto rc=a.reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_NO_DATA)break;Require(rc==dds::RETCODE_OK,"take");if(!info.valid_data)continue;bool valid=sample.key()==1&&sample.writer()==unsigned(2-i+2*group);for(unsigned k=0;k<32;++k)valid=valid&&sample.payload()[k]==std::uint8_t(k+sample.ordinal()+1-i+10*group);Require(rows.size()<16,"bounded ledger");rows.push_back({a.r,Guid(info.sample_identity.writer_guid()),sample.run(),Sequence(info.sample_identity.sequence_number()),sample.ordinal(),valid,true});}return rows.size()>=last-first+1;},"actual independent Reader ledger");Require(LedgerValid(rows,a.r,nodes[1-i].apps[group].w,run,first,last),"exact current Writer/SN/ordinal/payload");for(const auto& row:rows){Require(trace->Has("FAKE_SEND",1-i,"DATA",row.writer,row.sn)&&trace->Has("FAKE_INGRESS",i,"DATA",row.writer,row.sn)&&trace->Has("RX_OWNED",i,"DATA",row.writer,row.sn),"public shim and Binding path");trace->Add({"SHARED_TAKE","DATA",row.writer,std::string(phase)+":"+std::to_string(row.ordinal)+",reader="+row.reader,i,row.sn,0,0,a.receiver.application_incarnation,a.receiver.receiver_incarnation});}}
        for(auto& n:nodes)Require(n.apps[group].writer->wait_for_acknowledgments({3,0})==dds::RETCODE_OK,"DDS wait API");
    }
    void Delete(int side,int group){auto& a=nodes[side].apps[group];if(!a.participant)return;Require(a.participant->delete_contained_entities()==dds::RETCODE_OK,"public entity deletion");Require(dds::DomainParticipantFactory::get_instance()->delete_participant(a.participant)==dds::RETCODE_OK,"public Participant deletion");a.participant=nullptr;a.writer=nullptr;a.reader=nullptr;Require(!a.view->Observe().input_open,"actual facade input close");trace->Add({"SHARED_DELETE","",a.w,group?"Q":"P",side});}
    void End(){finishing=true;for(int group:{0,1})for(int side:{1,0})Delete(side,group);Wait([&]{return public_socket_fake::Observe().queued==0;},"copied Fake link drains");for(auto& n:nodes){auto v=n.runtime->Observe();Require(v.closed&&v.close.adapter_drained&&v.receive.records==0&&v.receive.active_applications==0&&v.receive.retained_applications==0&&v.native.receives==0&&v.native.callbacks==0,"last view final drain");}Require(public_socket_fake::Observe().live==0&&trace->Good(),"all owners settled");}
};
void Run(bool creator,bool negative){Isolate();Fixture f(!negative);const int first=creator?0:1;f.CreatePair(first,true);f.CreatePair(1-first,false);
    if(negative){auto end=std::chrono::steady_clock::now()+1500ms;while(std::chrono::steady_clock::now()<end){for(int g=0;g<2;++g)for(int i=0;i<2;++i)Require(f.Matches(i,g,true).empty()&&f.Matches(i,g,false).empty(),"disabled link no DDS bypass");std::this_thread::sleep_for(5ms);}auto api=public_socket_fake::Observe();Require(api.sends[0]&&api.sends[1]&&!api.forwards[0]&&!api.forwards[1],"negative exercised");f.End();return;}
    f.Match(0);f.Match(1);f.Traffic(0,1,3,"initial-P");f.Traffic(1,1,3,"initial-Q");
    std::string old[2],q[2];ReceiverIdentity old_rx[2];for(int i=0;i<2;++i){old[i]=f.nodes[i].apps[0].w;old_rx[i]=f.nodes[i].apps[0].receiver;q[i]=f.nodes[i].apps[1].p+f.nodes[i].apps[1].w+f.nodes[i].apps[1].r;}
    const auto before=public_socket_fake::Observe();for(int i=0;i<2;++i)f.Delete(i,0);
    auto gap=public_socket_fake::Observe();Require(gap.shutdowns[0]==before.shutdowns[0]&&gap.shutdowns[1]==before.shutdowns[1],"P deletion does not close shared native owners");f.Traffic(1,4,6,"gap-Q");
    f.CreatePair(0,false);f.Match(0);f.Match(1);for(int i=0;i<2;++i){auto& n=f.nodes[i];Require(n.apps[0].w!=old[i]&&!(n.apps[0].receiver==old_rx[i]),"new actual Writer and receiver incarnation");Require(q[i]==n.apps[1].p+n.apps[1].w+n.apps[1].r,"Q entities not rebuilt");}
    f.Traffic(0,1,3,"new-P");f.Traffic(1,7,9,"after-Q");auto after=public_socket_fake::Observe();for(int i=0;i<2;++i)Require(after.sockets[i]==2&&after.listens[i]==1&&after.binds[i]==1&&after.shutdowns[i]==before.shutdowns[i],"continuous native instances without hidden rebuild");f.End();
}
}
int main(int argc,char**argv){try{public_dds::Require(argc==2,"case");std::string c=argv[1];public_dds::Require(c=="creator"||c=="joiner"||c=="disabled-link","known case");shared_dds::Run(c!="joiner",c=="disabled-link");std::cout<<"PASS SHARED_REAL_DDS_FAKE "<<c<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FIRST_FAILURE "<<e.what()<<'\n';return 1;}}
