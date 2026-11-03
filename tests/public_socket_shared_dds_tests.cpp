#include "PublicSocketDdsTestSupport.hpp"
#include "../probes/SharedSendControl.hpp"
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
        // Declared sampling window: the stored trace is bounded at 8192 events, so beyond
        // the declared detailed cycles only the lifecycle ledger is kept. The owner
        // predicate above still runs every cycle; only the frame storage is sampled, and
        // the formal Reader ledger and every stage receipt stay complete for the whole run.
        // Stage-local observation, explicitly not a packet-spanning SDK generation.
        trace->DetailFrame(trace->Capture(),stage==ReceiveStage::OwnedCopy?"RX_OWNED":"RX_RUNNING",side,p,n,d.correlation,d.identity.application_incarnation,d.identity.receiver_incarnation);
    }
};
struct App {
    std::shared_ptr<HostProviderLoop> view;
    std::shared_ptr<Descriptor> descriptor;
    dds::DomainParticipant* participant=nullptr;
    dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::string p,w,r;ReceiverIdentity receiver;unsigned life=0;
};
struct Node {
    std::shared_ptr<PublicSocketBackend> backend;
    std::shared_ptr<HostProviderLoop> runtime;
    shared_probe::SendControl send_control;std::uint64_t cancelled_sends=0;
    App apps[2];std::thread pump;std::atomic<bool> stop{false};
};
struct Fixture {
    Node nodes[2];std::shared_ptr<Trace> trace=std::make_shared<Trace>();
    dds::TypeSupport type{new DSoftBusKeyedSamplePubSubType};
    std::uint64_t run=std::uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    std::atomic<bool> finishing{false};
    // Declared, not adaptive: cycles 1..detailed_cycles keep the full per-sample trace;
    // later cycles keep every lifecycle and ledger receipt but no per-sample frames.
    unsigned cycles=1,detailed_cycles=1;
    unsigned p_created[2]{},p_deleted[2]{},q_created[2]{},q_deleted[2]{};
    unsigned writes[2]{},takes[2]{};
    unsigned cycle=1;
    Fixture(bool forward=true){
        DSoftBusKeyedSamplePubSubType().register_type_object_representation();
        public_socket_fake::Configure(trace,forward);
        public_socket_fake::SetListenerNames({Name(0,"listener"),Name(1,"listener")});
        for(int side=0;side<2;++side){auto& n=nodes[side];n.backend=std::make_shared<PublicSocketBackend>(PublicConfig(side));n.runtime=HostProviderLoop::CreateShared(n.backend,std::make_shared<SharedObserver>(side,trace));}
        public_socket_fake::SetSendObserver([&](int side,int id){const auto v=nodes[side].runtime->Observe();Require(id==101+100*side&&v.native.send.entered==1,"original shared outgoing ticket");trace->Detail({"SHARED_SEND_TICKET","","","owner="+std::to_string(v.adapter.binding.owner_incarnation),side},trace->Capture());});
    }
    ~Fixture(){finishing=true;for(int group:{0,1})for(int side:{1,0})try{Delete(side,group);}catch(...){trace->Add({"TRACE_ERROR","","","cleanup",side});}for(auto& n:nodes)n.stop=true;for(auto& n:nodes)if(n.pump.joinable())n.pump.join();for(int side=0;side<2;++side){auto& n=nodes[side];if(!n.send_control.Good())trace->Add({"PUMP_ERROR","","","retirement control",side});std::printf("SHARED_SEND_SUMMARY run=%016llx side=%d cycle=%u cancelled=%llu control_ok=%d\n",(unsigned long long)run,side,cycle,(unsigned long long)n.cancelled_sends,n.send_control.Good());}public_socket_fake::SetSendObserver({});trace->Dump(run);}
    // Every receipt carries run, side, cycle and phase, so a repeated stage name cannot be
    // mistaken for the previous cycle's completion.
    void Emit(const char* tag,const char* phase,int side,const std::string& body,int group,
            std::uint64_t sn=0,std::uint64_t app=0,std::uint64_t receiver=0)const{
        std::printf("%s run=%016llx side=%d cycle=%u phase=%s life=%u group=%s sn=%llu app=%llu receiver=%llu %s\n",
                tag,static_cast<unsigned long long>(run),side,cycle,phase,nodes[side].apps[group].life,group?"Q":"P",
                static_cast<unsigned long long>(sn),static_cast<unsigned long long>(app),
                static_cast<unsigned long long>(receiver),body.c_str());
        std::fflush(stdout);
    }
    void Native(int side,const char* stage)const{
        const auto s=nodes[side].backend->Observe();const auto v=nodes[side].runtime->Observe();
        const AttachmentLimits limits;Require(v.receive.records<=limits.records&&v.receive.bytes<=limits.bytes,"bounded runtime records/bytes");
        const std::string stage_name=stage;const unsigned expected_views=stage_name=="FINAL"?0:stage_name=="GAP"?1:2;
        Require(v.live_views==expected_views&&v.receive.active_applications==expected_views,"stage membership count");
        Require(v.view_controls<=3&&v.receive.retained_applications<=2,"bounded controls and retired applications");
        std::printf("SHARED_NATIVE run=%016llx side=%d cycle=%u phase=%u stage=%s listener=%d outgoing=%d accepted=%d socket_calls=%llu listen_calls=%llu bind_calls=%llu shutdown_calls=%llu send_calls=%llu views=%zu records=%zu active_apps=%zu records_limit=64 bytes_limit=524288 retained_apps=%zu view_controls=%zu bytes=%zu runtime=%p provider=%p listener_entry=%zu outgoing_entry=%zu listener_owner=%llu outgoing_owner=%llu accepted_owner=%llu\n",
                static_cast<unsigned long long>(run),side,cycle,cycle,stage,
                s.listener_socket.value_or(-1),s.outgoing_socket.value_or(-1),s.accepted_socket.value_or(-1),
                static_cast<unsigned long long>(s.operations[0].calls),
                static_cast<unsigned long long>(s.operations[1].calls),
                static_cast<unsigned long long>(s.operations[2].calls),
                static_cast<unsigned long long>(s.operations[4].calls),static_cast<unsigned long long>(s.operations[3].calls),
                v.live_views,v.receive.records,v.receive.active_applications,v.receive.retained_applications,v.view_controls,v.receive.bytes,static_cast<void*>(nodes[side].runtime.get()),static_cast<void*>(nodes[side].backend.get()),s.listener_entry_index,s.entry_index,(unsigned long long)v.adapter.listener.owner_incarnation,(unsigned long long)v.adapter.binding.owner_incarnation,(unsigned long long)v.adapter.accepted.owner_incarnation);
        std::fflush(stdout);
    }
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
        a.life=++(group?q_created[side]:p_created[side]);
        Emit("SHARED_ID",a.life==1?"INITIAL":"REATTACH",side,"participant="+a.p+" writer="+a.w+" reader="+a.r+" token="+std::to_string(a.receiver.logical_token)+" name="+(group?"Q":"P"),group,0,a.receiver.application_incarnation,a.receiver.receiver_incarnation);
    }
    void CreatePair(int group,bool first){std::exception_ptr errors[2];std::thread threads[2];for(int i=0;i<2;++i)threads[i]=std::thread([&,i]{try{Create(i,group,first);}catch(...){errors[i]=std::current_exception();}});for(auto& t:threads)t.join();for(auto& e:errors)if(e)std::rethrow_exception(e);
        if(first)for(int side=0;side<2;++side)nodes[side].pump=std::thread([&,side]{auto& n=nodes[side];while(!n.stop){
            const auto sequence=n.send_control.Begin();HostSendObservation selected;auto tx=n.runtime->PumpSendObserved(selected);auto v=n.runtime->Observe();
            shared_probe::Retirement retirement;const bool cancelled=n.send_control.Find(sequence,selected.receiver,retirement)&&shared_probe::RetiredBeforeEntry(tx,selected,v,retirement);
            if(cancelled){++n.cancelled_sends;std::printf("SHARED_SEND_CANCELLED run=%016llx side=%d cycle=%u phase=%s group=%c life=%u app=%llu receiver=%llu token=%llu status=%u outcome=%u code_present=%d selected=1 view_closing=1 admission_closed=1 registration_ok=1 shared_live=1 count=%llu\n",(unsigned long long)run,side,retirement.cycle,retirement.final?"FINAL":"GAP",retirement.group?'Q':'P',retirement.life,(unsigned long long)selected.receiver.application_incarnation,(unsigned long long)selected.receiver.receiver_incarnation,(unsigned long long)selected.receiver.logical_token,unsigned(tx.status),unsigned(tx.outcome),bool(tx.actual_return_code),(unsigned long long)n.cancelled_sends);}
            const bool good=tx.outcome==NativeSendOutcome::ReturnedSdkCode&&tx.actual_return_code==0;
            const bool idle=tx.outcome==NativeSendOutcome::NotEntered&&tx.status==NativeStatus::NotFound;
            const bool terminal=finishing&&(v.sealed||v.adapter.outgoing_stopped)&&tx.outcome==NativeSendOutcome::NotEntered&&(tx.status==NativeStatus::ServiceClosing||tx.status==NativeStatus::Revoked||tx.status==NativeStatus::NotReady);
            const bool finished=n.send_control.Finish(sequence);
            if((!good&&!idle&&!terminal&&!cancelled)||!finished)trace->Add({"PUMP_ERROR","","","shared TX",side});
            public_socket_fake::PumpOne();auto rx=n.runtime->PumpReceive();if(!finishing&&rx!=BackendStatus::None&&rx!=BackendStatus::NoWork)trace->Add({"PUMP_ERROR","","","shared RX",side});std::this_thread::sleep_for(1ms);}});
    }
    std::set<std::string> Matches(int side,int group,bool readers){auto& a=nodes[side].apps[group];std::vector<dds::InstanceHandle_t> h;Require((readers?a.writer->get_matched_subscriptions(h):a.reader->get_matched_publications(h))==dds::RETCODE_OK,"match API");std::set<std::string> result;for(auto& v:h)result.insert(Handle(v));return result;}
    void Match(int group,const char* phase){Wait([&]{for(int i=0;i<2;++i)if(!CurrentMatch(Matches(i,group,true),nodes[1-i].apps[group].r)||!CurrentMatch(Matches(i,group,false),nodes[1-i].apps[group].w))return false;return true;},"current remote identities");for(int side=0;side<2;++side){auto& a=nodes[side].apps[group];auto& peer=nodes[1-side].apps[group];Emit("SHARED_MATCH",phase,side,"writer="+a.w+" reader="+a.r+" remote_writer="+peer.w+" remote_reader="+peer.r,group,0,a.receiver.application_incarnation,a.receiver.receiver_incarnation);}}
    void Traffic(int group,unsigned first,unsigned last,const char* phase,bool link=true){
        for(int i=0;i<2;++i)for(unsigned ordinal=first;ordinal<=last;++ordinal){auto& a=nodes[i].apps[group];DSoftBusKeyedSample sample;sample.key(1);sample.run(run);sample.writer(unsigned(1+i+2*group));sample.ordinal(ordinal);for(unsigned k=0;k<32;++k)sample.payload()[k]=std::uint8_t(k+ordinal+i+10*group);rtps::WriteParams params;Require(a.writer->write(&sample,params)==dds::RETCODE_OK,"one new write");++writes[i];Emit("SHARED_WRITE",phase,i,"writer="+a.w+" ordinal="+std::to_string(ordinal)+" rc=0 serialized=60",group,Sequence(params.sample_identity().sequence_number()),a.receiver.application_incarnation,a.receiver.receiver_incarnation);}
        for(int i=0;i<2;++i){auto& a=nodes[i].apps[group];std::vector<SampleReceipt> rows;Wait([&]{DSoftBusKeyedSample sample;dds::SampleInfo info;for(;;){auto rc=a.reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_NO_DATA)break;Require(rc==dds::RETCODE_OK,"take");if(!info.valid_data)continue;bool valid=sample.key()==1&&sample.writer()==unsigned(2-i+2*group);for(unsigned k=0;k<32;++k)valid=valid&&sample.payload()[k]==std::uint8_t(k+sample.ordinal()+1-i+10*group);Require(rows.size()<16,"bounded ledger");rows.push_back({a.r,Guid(info.sample_identity.writer_guid()),sample.run(),Sequence(info.sample_identity.sequence_number()),sample.ordinal(),valid,true});}return rows.size()>=last-first+1;},"actual independent Reader ledger");Require(LedgerValid(rows,a.r,nodes[1-i].apps[group].w,run,first,last),"exact current Writer/SN/ordinal/payload");for(const auto& row:rows){if(link)Require(trace->Has("FAKE_SEND",1-i,"DATA",row.writer,row.sn)&&trace->Has("FAKE_INGRESS",i,"DATA",row.writer,row.sn)&&trace->Has("RX_OWNED",i,"DATA",row.writer,row.sn),"public shim and Binding path");++takes[i];Emit("SHARED_TAKE",phase,i,"writer="+row.writer+" reader="+row.reader+" ordinal="+std::to_string(row.ordinal)+" valid="+std::to_string(row.payload),group,row.sn,a.receiver.application_incarnation,a.receiver.receiver_incarnation);}}
        for(auto& n:nodes)Require(n.apps[group].writer->wait_for_acknowledgments({3,0})==dds::RETCODE_OK,"DDS wait API");
    }
    void Delete(int side,int group){auto& a=nodes[side].apps[group];if(!a.participant)return;Require(nodes[side].send_control.Register({a.receiver,cycle,a.life,unsigned(group),bool(finishing)}),"explicit retirement registration");shared_probe::RetirementCompletion completion{nodes[side].send_control,a.receiver};Require(a.participant->delete_contained_entities()==dds::RETCODE_OK,"public entity deletion");Require(dds::DomainParticipantFactory::get_instance()->delete_participant(a.participant)==dds::RETCODE_OK,"public Participant deletion");a.participant=nullptr;a.writer=nullptr;a.reader=nullptr;const auto retired=a.view->Observe();Require(!retired.input_open&&retired.view_closing&&retired.send.admission_closed&&retired.send.drained&&retired.send.admitted_outstanding==0&&retired.send.submission_inflight==0&&retired.tx.records==0&&retired.tx.bytes==0,"actual view receiver and egress drain");++(group?q_deleted[side]:p_deleted[side]);Emit("SHARED_DELETE",finishing?"FINAL":"GAP",side,"deleted=1 writer="+a.w+" participant="+a.p+" input_open=0 egress_closed="+std::to_string(retired.send.admission_closed)+" egress_drained="+std::to_string(retired.send.drained)+" tx_records="+std::to_string(retired.tx.records)+" tx_bytes="+std::to_string(retired.tx.bytes),group,0,a.receiver.application_incarnation,a.receiver.receiver_incarnation);a.descriptor.reset();a.view.reset();}
    void End(){finishing=true;for(int group:{0,1})for(int side:{1,0})Delete(side,group);Wait([&]{return public_socket_fake::Observe().queued==0;},"copied Fake link drains");for(auto& n:nodes){auto v=n.runtime->Observe();Require(v.closed&&v.close.adapter_drained&&v.receive.records==0&&v.receive.active_applications==0&&v.receive.retained_applications==0&&v.native.receives==0&&v.native.callbacks==0,"last view final drain");}Require(public_socket_fake::Observe().live==0&&trace->Good(),"all owners settled");}
};
void Run(bool creator,bool negative,unsigned cycles=1){Isolate();Fixture f(!negative);f.cycles=cycles;
    // Declared sampling window, fixed before the run: the stored per-sample trace is
    // bounded at 8192 events, so only the first cycles keep it. The printed lifecycle and
    // formal Reader ledger stay complete for every cycle.
    f.detailed_cycles=1;f.trace->Sampling(f.detailed_cycles);f.trace->Cycle(1);const int first=creator?0:1;f.CreatePair(first,true);f.CreatePair(1-first,false);
    if(negative){auto end=std::chrono::steady_clock::now()+1500ms;while(std::chrono::steady_clock::now()<end){for(int g=0;g<2;++g)for(int i=0;i<2;++i)Require(f.Matches(i,g,true).empty()&&f.Matches(i,g,false).empty(),"disabled link no DDS bypass");std::this_thread::sleep_for(5ms);}auto api=public_socket_fake::Observe();Require(api.sends[0]&&api.sends[1]&&!api.forwards[0]&&!api.forwards[1],"negative exercised");f.End();return;}
    std::string old[2],q[2];ReceiverIdentity old_rx[2];for(int i=0;i<2;++i){old[i]=f.nodes[i].apps[0].w;old_rx[i]=f.nodes[i].apps[0].receiver;q[i]=f.nodes[i].apps[1].p+f.nodes[i].apps[1].w+f.nodes[i].apps[1].r;}
    const auto native_before=public_socket_fake::Observe();
    std::printf("SHARED_SAMPLING run=%016llx cycles=%u detailed_cycles=%u detail_scope=per_sample_frames lifecycle_scope=all_cycles ledger_scope=all_cycles observer_scope=stage_local fake_scope=send_latched send_policy=selected_retirement_v1\n",
            static_cast<unsigned long long>(f.run),f.cycles,f.detailed_cycles);std::fflush(stdout);
    unsigned q_next=1;
    for(unsigned c=1;c<=f.cycles;++c){
        f.cycle=c;f.trace->Cycle(c);const bool link=c<=f.detailed_cycles;
        f.Match(0,"INITIAL");f.Match(1,"INITIAL");f.Native(0,"INITIAL");f.Native(1,"INITIAL");
        f.Traffic(0,1,3,"INITIAL",link);f.Traffic(1,q_next,q_next+2,"INITIAL",link);q_next+=3;
        const auto before=public_socket_fake::Observe();
        for(int i=0;i<2;++i)f.Delete(i,0);
        const auto gap=public_socket_fake::Observe();
        Require(gap.shutdowns[0]==before.shutdowns[0]&&gap.shutdowns[1]==before.shutdowns[1],"P deletion does not close shared native owners");
        f.Native(0,"GAP");f.Native(1,"GAP");
        // The gap is written only after both boards' deletion receipts exist, and the next
        // cycle's P is exactly the generation created at the end of this one.
        f.Traffic(1,q_next,q_next+2,"GAP",link);q_next+=3;
        f.CreatePair(0,false);
        f.Match(0,"REATTACH");f.Match(1,"REATTACH");
        for(int i=0;i<2;++i){auto& n=f.nodes[i];
            Require(n.apps[0].w!=old[i]&&!(n.apps[0].receiver==old_rx[i]),"new actual Writer and receiver incarnation");
            Require(q[i]==n.apps[1].p+n.apps[1].w+n.apps[1].r,"Q entities not rebuilt");
            old[i]=n.apps[0].w;old_rx[i]=n.apps[0].receiver;}
        f.Traffic(0,1,3,"REATTACH",link);
        f.Traffic(1,q_next,q_next+2,"REATTACH",link);q_next+=3;
        f.Native(0,"REATTACHED");f.Native(1,"REATTACHED");
    }
    auto after=public_socket_fake::Observe();
    for(int i=0;i<2;++i)Require(after.sockets[i]==2&&after.listens[i]==1&&after.binds[i]==1&&after.shutdowns[i]==native_before.shutdowns[i],"continuous native instances without hidden rebuild");
    f.End();
    // Counted per endpoint, and only complete after End() has performed the final
    // deletion: P created and deleted N+1 times, Q created and deleted once.
    for(int side=0;side<2;++side){
        Require(f.p_created[side]==cycles+1&&f.p_deleted[side]==cycles+1,"per-side P rebuild and final deletion");
        Require(f.q_created[side]==1&&f.q_deleted[side]==1,"per-side Q continuous life");
        Require(f.writes[side]==15*cycles&&f.takes[side]==15*cycles,"actual per-side full ledger");
        f.Native(side,"FINAL");
        const auto wire=public_socket_fake::Observe();
        Require(wire.queued==0&&wire.bytes==0&&wire.record_peak<=64&&wire.byte_peak<=512*1024&&wire.invalid==0,"final bounded Fake responsibilities");
        std::printf("SHARED_LINK run=%016llx side=%d cycle=%u phase=FINAL sends=%u forwards=%u queued=%zu bytes=%zu record_peak=%zu byte_peak=%zu invalid=%u shutdowns=%u notifications=%u\n",static_cast<unsigned long long>(f.run),side,cycles,wire.sends[side],wire.forwards[side],wire.queued,wire.bytes,wire.record_peak,wire.byte_peak,wire.invalid,wire.shutdowns[side],wire.notified[side]);
        std::printf("SHARED_RESULT run=%016llx side=%d cycles=%u p_created=%u p_deleted=%u q_created=%u q_deleted=%u writes=%u takes=%u success=1\n",static_cast<unsigned long long>(f.run),side,cycles,f.p_created[side],f.p_deleted[side],f.q_created[side],f.q_deleted[side],f.writes[side],f.takes[side]);
    }

}
}
int main(int argc,char**argv){try{public_dds::Require(argc==2,"case");std::string c=argv[1];
    if(c.rfind("cycle-",0)==0){const unsigned n=unsigned(std::stoul(c.substr(6)));public_dds::Require(n==1||n==10||n==100,"known cycle count");shared_dds::Run(true,false,n);std::cout<<"PASS SHARED_REAL_DDS_FAKE "<<c<<'\n';return 0;}
    public_dds::Require(c=="creator"||c=="joiner"||c=="disabled-link","known case");shared_dds::Run(c!="joiner",c=="disabled-link");std::cout<<"PASS SHARED_REAL_DDS_FAKE "<<c<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FIRST_FAILURE "<<e.what()<<'\n';return 1;}}
