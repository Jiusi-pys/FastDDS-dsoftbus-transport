#include "run_protocol.hpp"
#include "../../examples/dds214_layer_baseline/LayerSample.hpp"
#include "frag_sample.hpp"
#include <cstdio>
#include <cstdlib>
#include <fastdds_dsoftbus/dsoftbus/ExperimentalRuntime.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.h>
#include <fastrtps/xmlparser/XMLProfileManager.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/syscall.h>

namespace ds=fastdds_dsoftbus::dsoftbus;
namespace fd=eprosima::fastdds::dds;
using RC=eprosima::fastrtps::types::ReturnCode_t;
using stage1_run::Clock;
static_assert(sizeof(NS_ConsumerApi)==32,"Preserve V1 ABI");
static_assert(sizeof(NS_Envelope)==320&&sizeof(NS_RunEvent)==424&&sizeof(NS_RunControl)==56&&sizeof(NS_RunApi)==48&&sizeof(NS_RunResult)==48,"ARM64 C++ POD layout matches C TU");
static constexpr char sdk_abi[]=NS_STR(_LIBCPP_ABI_NAMESPACE);
static_assert(sizeof(sdk_abi)==5&&sdk_abi[2]=='n'&&sdk_abi[3]=='1',"Actual SDK __n1");
static std::mutex single_run;
static bool retained_process=false; // guarded by single_run, terminal for this process
static uint64_t ticks(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());}
static Clock::time_point end_at(uint64_t ns){return Clock::time_point(std::chrono::nanoseconds(ns));}
static void need(bool v,int code=NS_STATE){if(!v)throw code;}
static bool header(const NS_Header& h,size_t n){return h.version==NS_RUN_VERSION&&h.size==n&&h.scope==NS_SCOPE&&!h.reserved;}
struct Session;
struct NS_RunHandle {
    NS_RunRequest request;NS_RunControl control;
    stage1_run::Progress progress;
    std::mutex callback_gate,watch_gate;std::condition_variable watch_changed;
    std::atomic<bool> cancelled{false},running{false};bool used=false,callbacks_enabled=false,watch_stop=false;
    Session* retained=nullptr;
    explicit NS_RunHandle(const NS_RunRequest& q,const NS_RunControl& c):request(q),control(c){}
    bool live()const{return !cancelled.load()&&ticks()<request.deployment.deadline_ns;}
    void abort(){
        cancelled=true;progress.cancel();
        std::lock_guard<std::mutex> l(callback_gate);
        if(callbacks_enabled){try{control.abort(control.context);}catch(...){} }
    }
    void disarm(){std::lock_guard<std::mutex> l(callback_gate);callbacks_enabled=false;control={};}
    void emit(NS_RunEvent e){need(live(),NS_CANCELLED);e.header={NS_RUN_VERSION,sizeof(e),NS_SCOPE,0};e.envelope=request.local;need(control.emit(control.context,&e)==NS_OK);need(live(),NS_CANCELLED);}
    void wait(uint32_t phase){need(live(),NS_CANCELLED);need(control.wait(control.context,phase,request.deployment.deadline_ns)==NS_OK,NS_CANCELLED);need(live(),NS_CANCELLED);}
};
struct Signals final:fd::DataWriterListener,fd::DataReaderListener {
    NS_RunHandle& h;explicit Signals(NS_RunHandle& x):h(x){}
    void on_publication_matched(fd::DataWriter*,const fd::PublicationMatchedStatus&) override{h.progress.notify();}
    void on_subscription_matched(fd::DataReader*,const fd::SubscriptionMatchedStatus&) override{h.progress.notify();}
    void on_data_available(fd::DataReader*) override{h.progress.notify();}
};
struct Session {
    NS_RunHandle& h;Signals signals;fd::TypeSupport type{new LayerSampleType};
    std::unique_ptr<ds::experimental::Runtime> runtime;
    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> descriptor;
    std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> retained_descriptors;
    fd::DomainParticipantFactory* factory=nullptr;fd::DomainParticipant* participant=nullptr;
    fd::DataWriter* writer=nullptr;fd::DataReader* reader=nullptr;
    std::thread pump,watch;std::atomic<bool> pump_failed{false},tolerate_send_failures{false};std::atomic<std::uint32_t> send_failures{0};bool finish_attempted=false,closed=false;
    explicit Session(NS_RunHandle& x):h(x),signals(x){}
    void start(){
        watch=std::thread([this]{std::unique_lock<std::mutex> l(h.watch_gate);if(!h.watch_changed.wait_until(l,end_at(h.request.deployment.deadline_ns),[&]{return h.watch_stop;})){l.unlock();h.abort();runtime->RequestPumpStop();}});
        pump=std::thread([this]{try{for(;;){
            auto g=runtime->WorkGeneration();
            for(;;){auto r=runtime->PumpSend();if(r.outcome==ds::NativeSendOutcome::ReturnedInvalidCode||r.outcome==ds::NativeSendOutcome::ReturnedByException||(r.actual_return_code&&*r.actual_return_code!=0)){
                // A route-outage window expects native send failures while the route is down.
                if(tolerate_send_failures.load()&&r.outcome!=ds::NativeSendOutcome::ReturnedByException)++send_failures;else{pump_failed=true;h.abort();}}
                if(r.status!=ds::NativeStatus::None)break;}
            ds::phase0::BackendStatus rx;do{rx=runtime->PumpReceive();}while(rx==ds::phase0::BackendStatus::None);
            if(rx!=ds::phase0::BackendStatus::NoWork&&rx!=ds::phase0::BackendStatus::InvalidState){pump_failed=true;h.abort();}
            auto w=runtime->WaitForWork(g);if(w!=ds::WorkWaitResult::Work&&w!=ds::WorkWaitResult::Deadline){if(w!=ds::WorkWaitResult::StopRequested&&w!=ds::WorkWaitResult::Closed){pump_failed=true;h.abort();}return;}
        }}catch(...){pump_failed=true;h.abort();}});
    }
    bool finish()noexcept{
        if(finish_attempted)return closed;finish_attempted=true;bool ok=true;
        // Keep the pump active while DDS performs public deletion. No callback
        // quiet or interruptibility is inferred; failures pin the entire owner.
        try{if(participant){if(participant->delete_contained_entities()!=RC::RETCODE_OK)ok=false;
            else if(factory->delete_participant(participant)!=RC::RETCODE_OK)ok=false;
            else participant=nullptr;}}catch(...){ok=false;}
        if(runtime)runtime->RequestPumpStop();
        try{if(pump.joinable())pump.join();}catch(...){ok=false;}
        {std::lock_guard<std::mutex> l(h.watch_gate);h.watch_stop=true;h.watch_changed.notify_all();}
        try{if(watch.joinable())watch.join();}catch(...){ok=false;}
        if(ok){descriptor.reset();retained_descriptors.clear();try{if(runtime&&!runtime->Close())ok=false;}catch(...){ok=false;}}
        closed=ok;return ok;
    }
};
static ds::DSoftBusTransportDescriptor configuration(const NS_Request& q){
    ds::V1Deployment d;d.identity={q.name_space,q.domain,q.udid};d.slot=q.slot;d.callback_namespace="native.successor.run";
    d.native_name_scope={ds::v1::NativeNameContract::PackageScopedV1,q.package_name,q.prefix};
    ds::V1PeerDeployment p;p.identity={q.name_space,q.domain,q.peer_udid};p.slot=q.peer_slot;p.network_id=q.peer_network;
    p.local_initiator_name=q.initiator;p.expected_remote_initiator_names={q.peer_initiator};p.native_name_scope=d.native_name_scope;d.peers={p};
    ds::DSoftBusTransportDescriptor config(65536);need(config.ConfigureV1(d).code==ds::DirectoryError::None,NS_CONTEXT);return config;
}
static ds::phase0::PublicSocketConfiguration backend(const ds::DSoftBusTransportDescriptor& config,const NS_Request& q){
    const auto& r=config.directory.routes.at(0).fields;ds::phase0::PublicSocketConfiguration b;
    b.package_name=q.package_name;b.identity={r.local_initiator_name,{1},{1}};b.listener_identity={config.directory.locals.at(0).local_listen_name,{2},{2}};
    b.peer_name=r.remote_listen_name;b.peer_network_id=r.remote_network_id;b.expected_peer_initiator_name=r.expected_remote_initiator_names.at(0);b.max_message_size=65536;b.listen_qos={{0,80},{1,2000}};b.bind_qos=b.listen_qos;return b;
}
static void guid(uint8_t* p,const eprosima::fastrtps::rtps::GUID_t& g){std::memcpy(p,g.guidPrefix.value,12);std::memcpy(p+12,g.entityId.value,4);}
static int64_t sequence(const eprosima::fastrtps::rtps::SequenceNumber_t& s){need(s.high>=0&&(s.high||s.low));return (int64_t(s.high)<<32)|s.low;}
static void run_session(Session& s,NS_RunResult& out){
    auto& h=s.h;const auto& q=h.request.deployment;const auto& local=h.request.local;
    auto config=configuration(q);s.runtime=std::make_unique<ds::experimental::Runtime>(backend(config,q),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
    need(s.runtime->SetPreConnectGate([&s,&h]{try{
        const auto o=s.runtime->ObserveSocket();const auto& listen=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Listen)];
        need(!o.fake_sdk&&!o.stopped&&!o.closed&&!o.listener_stopped&&o.listener_configured&&o.listener_socket&&*o.listener_socket>=0&&!o.first_api_error&&!o.first_callback_error&&!o.first_exception&&listen.calls==1&&listen.returns==1&&listen.last_call&&listen.last_call->called&&listen.last_call->returned&&listen.last_call->provider_return==0);
        NS_RunEvent e{};e.kind=NS_ROOT_LISTENER;e.socket=*o.listener_socket;e.listen_rc=0;e.flags=NS_REAL_ROOT_LISTEN_PUBLISHED;h.emit(e);h.wait(NS_CONNECT_RELEASE);return true;
    }catch(...){h.abort();return false;}}));
    s.descriptor=s.runtime->MakeStockDescriptor(config);need(bool(s.descriptor));
    s.factory=fd::DomainParticipantFactory::get_instance();
    static constexpr char xml[]="<dds xmlns=\"http://www.eprosima.com/XMLSchemas/fastRTPS_Profiles\"><library_settings><intraprocess_delivery>OFF</intraprocess_delivery></library_settings></dds>";
    need(s.factory->load_XML_profiles_string(xml,sizeof(xml)-1)==RC::RETCODE_OK);
    need(eprosima::fastrtps::xmlparser::XMLProfileManager::library_settings().intraprocess_delivery==eprosima::fastrtps::INTRAPROCESS_OFF);
    fd::DomainParticipantQos qos;qos.name(("native_run_"+std::to_string(local.run)+"_"+std::to_string(local.side)).c_str());
    qos.transport().use_builtin_transports=false;qos.transport().user_transports={s.descriptor};qos.transport().max_msg_size_no_frag=config.maxMessageSize;
    qos.wire_protocol().builtin.metatrafficUnicastLocatorList.clear();qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(config.default_user_unicast.at(0));
    qos.wire_protocol().builtin.metatrafficMulticastLocatorList.clear();qos.wire_protocol().builtin.initialPeersList.clear();qos.wire_protocol().builtin.initialPeersList.push_back(config.directory.routes.at(0).key.remote_locator);
    qos.wire_protocol().default_unicast_locator_list.clear();qos.wire_protocol().default_unicast_locator_list.push_back(config.default_user_unicast.at(0));qos.wire_protocol().default_multicast_locator_list.clear();
    s.start();need(h.live(),NS_CANCELLED);s.participant=s.factory->create_participant(q.domain,qos);need(s.participant);out.phase=1;
    const auto& effective=s.participant->get_qos();need(!effective.transport().use_builtin_transports&&effective.transport().user_transports.size()==1&&effective.transport().user_transports.front()==s.descriptor);
    need(effective.wire_protocol().default_multicast_locator_list.empty()&&effective.wire_protocol().builtin.metatrafficMulticastLocatorList.empty());
    need(s.type.register_type(s.participant)==RC::RETCODE_OK);
    auto topic=[&](unsigned side){return s.participant->create_topic("native_run_"+std::to_string(local.run)+"_"+std::to_string(side),s.type.get_type_name(),fd::TOPIC_QOS_DEFAULT);};
    auto* tx=topic(local.side);auto* rx=topic(1-local.side);need(tx&&rx);auto* pub=s.participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub=s.participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub&&sub);
    fd::DataWriterQos wq;fd::DataReaderQos rq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;
    wq.durability().kind=fd::VOLATILE_DURABILITY_QOS;rq.durability().kind=fd::VOLATILE_DURABILITY_QOS;wq.history().depth=8;rq.history().depth=8;wq.data_sharing().off();rq.data_sharing().off();
    s.writer=pub->create_datawriter(tx,wq,&s.signals);s.reader=sub->create_datareader(rx,rq,&s.signals);need(s.writer&&s.reader);
    for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);fd::PublicationMatchedStatus w;fd::SubscriptionMatchedStatus r;
        need(s.writer->get_publication_matched_status(w)==RC::RETCODE_OK&&s.reader->get_subscription_matched_status(r)==RC::RETCODE_OK);
        need(w.current_count<=1&&r.current_count<=1);if(w.current_count==1&&r.current_count==1)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);}
    NS_RunEvent matched{};matched.kind=NS_MATCHED;matched.writer_matches=1;matched.reader_matches=1;guid(matched.writer_guid,s.writer->guid());h.emit(matched);h.wait(NS_SEND_RELEASE);out.phase=2;
    for(unsigned n=1;n<=3;++n){need(h.live()&&!s.pump_failed,NS_CANCELLED);LayerSample v;v.run=local.run;v.side=local.side;v.ordinal=n;v.payload=stage1_run::payload(v.run,v.side,n);
        eprosima::fastrtps::rtps::WriteParams p;need(s.writer->write(&v,p));NS_RunEvent e{};e.kind=NS_WRITE;e.ordinal=n;e.sequence=sequence(p.sample_identity().sequence_number());guid(e.writer_guid,p.sample_identity().writer_guid());std::memcpy(e.payload,v.payload.data(),32);need(!std::memcmp(e.writer_guid,matched.writer_guid,16));h.emit(e);++out.writes;}
    std::array<bool,3> taken{};std::array<int64_t,3> sequences{};uint8_t remote_guid[16]{};
    for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);
        for(;;){LayerSample v;fd::SampleInfo info;const auto rc=s.reader->take_next_sample(&v,&info);if(rc==RC::RETCODE_NO_DATA)break;need(rc==RC::RETCODE_OK&&info.valid_data);
            need(out.takes<3&&v.run==local.run&&v.side==1-local.side&&v.ordinal>=1&&v.ordinal<=3&&!taken[v.ordinal-1]&&v.payload==stage1_run::payload(v.run,v.side,v.ordinal));
            NS_RunEvent e{};e.kind=NS_TAKE;e.ordinal=v.ordinal;e.sequence=sequence(info.sample_identity.sequence_number());guid(e.writer_guid,info.sample_identity.writer_guid());
            if(out.takes)need(!std::memcmp(remote_guid,e.writer_guid,16));else std::memcpy(remote_guid,e.writer_guid,16);
            for(unsigned i=0;i<out.takes;++i)need(sequences[i]!=e.sequence);sequences[out.takes]=e.sequence;taken[v.ordinal-1]=true;std::memcpy(e.payload,v.payload.data(),32);h.emit(e);++out.takes;
        }
        if(out.takes==3)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);
    }
    {   // Fragmented sample: one 150000-byte sample per direction on a separate topic.
        // Not reported through the 32-byte event protocol; any failure fails the run.
        fd::TypeSupport frag_type{new stage1_frag::Type};need(frag_type.register_type(s.participant)==RC::RETCODE_OK);
        auto ftopic=[&](unsigned side){return s.participant->create_topic("native_run_"+std::to_string(local.run)+"_"+std::to_string(side)+"_frag",frag_type.get_type_name(),fd::TOPIC_QOS_DEFAULT);};
        auto* ftx=ftopic(local.side);auto* frx=ftopic(1-local.side);need(ftx&&frx);
        fd::DataWriterQos fwq=wq;fd::DataReaderQos frq=rq;fwq.history().depth=1;frq.history().depth=1;
        auto* fw=pub->create_datawriter(ftx,fwq,&s.signals);auto* fr=sub->create_datareader(frx,frq,&s.signals);need(fw&&fr);
        for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);fd::PublicationMatchedStatus w;fd::SubscriptionMatchedStatus r;
            need(fw->get_publication_matched_status(w)==RC::RETCODE_OK&&fr->get_subscription_matched_status(r)==RC::RETCODE_OK);
            if(w.current_count==1&&r.current_count==1)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);}
        auto mine=stage1_frag::make(local.run,local.side);need(fw->write(&mine));
        bool got=false;
        for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);
            for(;;){stage1_frag::Sample v;fd::SampleInfo info;const auto rc=fr->take_next_sample(&v,&info);if(rc==RC::RETCODE_NO_DATA)break;need(rc==RC::RETCODE_OK&&info.valid_data&&!got);
                need(stage1_frag::valid(v,local.run,1-local.side));got=true;}
            if(got)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);}
        const auto fleft=q.deadline_ns-ticks();need(h.live(),NS_CANCELLED);need(fleft<=uint64_t(INT32_MAX)*1000000000u,NS_CONTEXT);
        need(fw->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(static_cast<int32_t>(fleft/1000000000u),static_cast<uint32_t>(fleft%1000000000u)))==RC::RETCODE_OK);
        std::printf("FRAG_SAMPLE side=%u bytes=%u max_message=%u written=1 taken=1 verified=1 acked=1\n",static_cast<unsigned>(local.side),stage1_frag::kBytes,static_cast<unsigned>(config.maxMessageSize));std::fflush(stdout);
    }
    {   // Same-name Participant hot rebuild on the live native anchor (PersistentNativeAnchor).
        // Delete the whole Participant, create a new one with the same name from a new
        // descriptor, then exchange one 150000-byte sample per direction on a
        // TRANSIENT_LOCAL topic so a peer that rebuilds later still receives it.
        std::vector<std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface>> retained{s.descriptor};
        const auto old_prefix=s.participant->guid().guidPrefix;
        need(s.participant->delete_contained_entities()==RC::RETCODE_OK);need(s.factory->delete_participant(s.participant)==RC::RETCODE_OK);
        s.participant=nullptr;s.writer=nullptr;s.reader=nullptr;need(h.live()&&!s.pump_failed,NS_CANCELLED);
        s.descriptor=s.runtime->MakeStockDescriptor(config);need(bool(s.descriptor));retained.push_back(s.descriptor);
        qos.transport().user_transports={s.descriptor};
        s.participant=s.factory->create_participant(q.domain,qos);need(s.participant);need(!(s.participant->guid().guidPrefix==old_prefix));
        need(std::string(s.participant->get_qos().name().c_str())==std::string(qos.name().c_str()));
        need(s.type.register_type(s.participant)==RC::RETCODE_OK);
        fd::TypeSupport rb_type{new stage1_frag::Type};need(rb_type.register_type(s.participant)==RC::RETCODE_OK);
        pub=s.participant->create_publisher(fd::PUBLISHER_QOS_DEFAULT);sub=s.participant->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub&&sub);
        auto rtopic=[&](unsigned side){return s.participant->create_topic("native_run_"+std::to_string(local.run)+"_"+std::to_string(side)+"_rebuild",rb_type.get_type_name(),fd::TOPIC_QOS_DEFAULT);};
        auto* rtx=rtopic(local.side);auto* rrx=rtopic(1-local.side);need(rtx&&rrx);
        fd::DataWriterQos bwq=wq;fd::DataReaderQos brq=rq;bwq.history().depth=1;brq.history().depth=1;
        bwq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;brq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;
        auto* bw=pub->create_datawriter(rtx,bwq,&s.signals);auto* br=sub->create_datareader(rrx,brq,&s.signals);need(bw&&br);
        auto mine=stage1_frag::make(local.run+1,local.side);need(bw->write(&mine));
        bool got=false;
        for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);
            for(;;){stage1_frag::Sample v;fd::SampleInfo info;const auto rc=br->take_next_sample(&v,&info);if(rc==RC::RETCODE_NO_DATA)break;need(rc==RC::RETCODE_OK&&info.valid_data&&!got);
                need(stage1_frag::valid(v,local.run+1,1-local.side));got=true;}
            if(got)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);}
        const auto bleft=q.deadline_ns-ticks();need(h.live(),NS_CANCELLED);need(bleft<=uint64_t(INT32_MAX)*1000000000u,NS_CONTEXT);
        need(bw->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(static_cast<int32_t>(bleft/1000000000u),static_cast<uint32_t>(bleft%1000000000u)))==RC::RETCODE_OK);
        // Recreate the standard endpoints for the existing ACK/teardown protocol.
        tx=topic(local.side);rx=topic(1-local.side);need(tx&&rx);
        s.writer=pub->create_datawriter(tx,wq,&s.signals);s.reader=sub->create_datareader(rx,rq,&s.signals);need(s.writer&&s.reader);
        std::printf("HOT_REBUILD side=%u same_name=1 new_guid_prefix=1 descriptors=%u sample_bytes=%u written=1 taken=1 verified=1 acked=1\n",static_cast<unsigned>(local.side),static_cast<unsigned>(retained.size()),stage1_frag::kBytes);std::fflush(stdout);
        s.retained_descriptors=std::move(retained);
    }
    if([]{const char* d=std::getenv("TMPDIR");if(!d)return false;return access((std::string(d)+"/route_window").c_str(),F_OK)==0;}())
    {   // Route outage window (opt-in: TMPDIR/route_window exists): 45 s of 200 ms counter samples per direction on the live
        // Participant. The host drops all IP traffic between the boards for part of it.
        // Pass requires samples to arrive again after the longest gap (recovery).
        auto rtopic=[&](unsigned side){return s.participant->create_topic("native_run_"+std::to_string(local.run)+"_"+std::to_string(side)+"_route",s.type.get_type_name(),fd::TOPIC_QOS_DEFAULT);};
        auto* otx=rtopic(local.side);auto* orx=rtopic(1-local.side);need(otx&&orx);
        fd::DataWriterQos owq=wq;fd::DataReaderQos orq=rq;owq.history().depth=16;orq.history().depth=16;
        auto* ow=pub->create_datawriter(otx,owq,&s.signals);auto* orr=sub->create_datareader(orx,orq,&s.signals);need(ow&&orr);
        for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);fd::PublicationMatchedStatus w;fd::SubscriptionMatchedStatus r;
            need(ow->get_publication_matched_status(w)==RC::RETCODE_OK&&orr->get_subscription_matched_status(r)==RC::RETCODE_OK);
            if(w.current_count>=1&&r.current_count>=1)break;need(h.progress.wait(seen,end_at(q.deadline_ns)),NS_CANCELLED);}
        s.tolerate_send_failures=true;
        const auto t0=Clock::now();const auto window=std::chrono::seconds(45);
        uint32_t written=0,received=0,max_peer=0,write_fail=0;int64_t max_gap_ms=0,gap_end_ms=-1,last_rx_ms=-1;bool aborted=false;
        auto ms=[&]{return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t0).count());};
        try{
            auto next=t0;
            while(Clock::now()-t0<window){
                need(h.live()&&!s.pump_failed,NS_CANCELLED);
                if(Clock::now()>=next){LayerSample v;v.run=local.run+2;v.side=local.side;v.ordinal=written+1;v.payload=stage1_run::payload(v.run,v.side,1);
                    if(ow->write(&v))++written;else ++write_fail;next+=std::chrono::milliseconds(200);}
                for(;;){LayerSample v;fd::SampleInfo info;const auto rc=orr->take_next_sample(&v,&info);if(rc==RC::RETCODE_NO_DATA)break;need(rc==RC::RETCODE_OK);
                    if(!info.valid_data)continue;need(v.run==local.run+2&&v.side==1-local.side);
                    const auto now=ms();if(last_rx_ms>=0&&now-last_rx_ms>max_gap_ms){max_gap_ms=now-last_rx_ms;gap_end_ms=now;}
                    last_rx_ms=now;++received;if(v.ordinal>max_peer)max_peer=v.ordinal;}
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }catch(...){aborted=true;}
        const bool recovered=!aborted&&gap_end_ms>=0&&last_rx_ms>gap_end_ms&&last_rx_ms>=45000-3000;
        std::printf("ROUTE_OUTAGE side=%u written=%u write_fail=%u received=%u max_peer=%u max_gap_ms=%lld gap_end_ms=%lld last_rx_ms=%lld aborted=%d pump_failed=%d recovered=%d send_failures=%u\n",
            static_cast<unsigned>(local.side),written,write_fail,received,max_peer,static_cast<long long>(max_gap_ms),static_cast<long long>(gap_end_ms),static_cast<long long>(last_rx_ms),int(aborted),int(s.pump_failed.load()),int(recovered),s.send_failures.load());std::fflush(stdout);
        {const auto o=s.runtime->ObserveSocket();const auto& snd=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Send)];
         const auto& sk=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Socket)];const auto& bd=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Bind)];const auto& sh=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Shutdown)];
         auto opt=[](const std::optional<std::int32_t>& v){return v?static_cast<long long>(*v):-99999LL;};
         std::printf("ROUTE_SOCKET side=%u stopped=%d closed=%d out_sock=%lld out_stopped=%d acc_sock=%lld acc_stopped=%d listen_stopped=%d shutdown_reason=%lld first_cb_err=%lld api_err=%d socket_calls=%llu bind_calls=%llu bind_last=%lld send_calls=%llu send_returns=%llu send_last=%lld shutdown_calls=%llu cb=%llu rejected_cb=%llu\n",
            static_cast<unsigned>(local.side),int(o.stopped),int(o.closed),opt(o.outgoing_socket),int(o.outgoing_stopped),opt(o.accepted_socket),int(o.accepted_stopped),int(o.listener_stopped),opt(o.shutdown_reason),opt(o.first_callback_error),int(bool(o.first_api_error)),
            (unsigned long long)sk.calls,(unsigned long long)bd.calls,bd.last_call?opt(bd.last_call->provider_return):-99999LL,(unsigned long long)snd.calls,(unsigned long long)snd.returns,snd.last_call?opt(snd.last_call->provider_return):-99999LL,(unsigned long long)sh.calls,(unsigned long long)o.callback_entries,(unsigned long long)o.rejected_callbacks);std::fflush(stdout);}
        need(recovered,NS_CANCELLED);
    }
    const auto left=q.deadline_ns-ticks();need(h.live(),NS_CANCELLED);need(left<=uint64_t(INT32_MAX)*1000000000u,NS_CONTEXT);
    need(s.writer->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(static_cast<int32_t>(left/1000000000u),static_cast<uint32_t>(left%1000000000u)))==RC::RETCODE_OK);
    NS_RunEvent ack{};ack.kind=NS_ACK;h.emit(ack);h.wait(NS_DATA_CHECK_RELEASE);
    // Reject an already queued unexpected extra sample rather than silently
    // treating arbitrary take errors as NO_DATA. This is not a late-data barrier.
    LayerSample extra;fd::SampleInfo info;need(s.reader->take_next_sample(&extra,&info)==RC::RETCODE_NO_DATA);
    // Absence of our DATA_DONE still prevents compliant peer entity deletion.
    // Once DELETE_RELEASE is granted, peer teardown is legal and this strict
    // sample assertion would race normal DDS lifecycle notifications.
    NS_RunEvent done{};done.kind=NS_DATA_DONE;h.emit(done);out.phase=3;h.wait(NS_DELETE_RELEASE);
    need(s.finish());NS_RunEvent deleted{};deleted.kind=NS_TEARDOWN;h.emit(deleted);out.phase=4;
}
static bool request_valid(const NS_RunRequest& q,const NS_RunControl& c){
    const auto& d=q.deployment;
    if(!header(q.header,sizeof(q))||!header(c.header,sizeof(c))||ns_header(&d.header,sizeof(d))||q.sample_count!=3||q.reserved||d.cookie!=NS_COOKIE||d.ownership!=NS_BORROWED||d.external_approval!=NS_EXTERNAL_APPROVAL||!c.context||!c.allow||!c.emit||!c.wait||!c.abort)return false;
    if(!stage1_run::valid(q.local)||!stage1_run::valid(q.peer)||q.local.side==q.peer.side||q.local.run!=q.peer.run||q.local.domain!=q.peer.domain||q.local.domain!=d.domain||std::strcmp(q.local.name_space,q.peer.name_space))return false;
    if(!stage1_run::bounded(d.name_space,sizeof d.name_space)||std::strcmp(d.name_space,q.local.name_space))return false;
    return stage1_run::bounded(d.udid,sizeof d.udid)&&stage1_run::bounded(d.peer_udid,sizeof d.peer_udid)&&stage1_run::bounded(d.peer_network,sizeof d.peer_network)&&stage1_run::bounded(d.package_name,sizeof d.package_name)&&!std::strcmp(d.package_name,NS_PACKAGE)&&stage1_run::bounded(d.prefix,sizeof d.prefix)&&stage1_run::bounded(d.initiator,sizeof d.initiator)&&stage1_run::bounded(d.peer_initiator,sizeof d.peer_initiator)&&d.deadline_ns>ticks()&&d.deadline_ns-ticks()<=UINT64_C(120000000000);
}
static int32_t create_run(const NS_RunRequest* q,const NS_RunControl* c,NS_RunHandle** out){try{if(!q||!c||!out)return NS_BOUND;*out=nullptr;if(!request_valid(*q,*c))return NS_CONTEXT;*out=new NS_RunHandle(*q,*c);return NS_OK;}catch(...){return NS_STATE;}}
static int32_t execute_run(NS_RunHandle* h,NS_RunResult* out){
    if(!h||!out)return NS_BOUND;if(!header(out->header,sizeof(*out)))return NS_ABI;
    std::unique_lock<std::mutex> exclusive(single_run,std::try_to_lock);if(!exclusive.owns_lock())return NS_STATE;
    if(retained_process)return NS_UNLOAD;
    if(h->used)return NS_STATE;h->used=true;h->running=true;
    NS_RunResult r{{NS_RUN_VERSION,sizeof(r),NS_SCOPE,0},NS_STATE,0,0,0,0,0,1,0};Session* s=nullptr;
    {std::lock_guard<std::mutex> l(h->callback_gate);h->callbacks_enabled=true;}
    try{
        need(h->request.local.pid==static_cast<uint32_t>(getpid())&&getpid()==syscall(SYS_gettid),NS_CONTEXT);
        need(h->live(),NS_CANCELLED);need(h->control.allow(h->control.context,h->request.deployment.deadline_ns)==NS_OK,NS_AUTH);need(h->live(),NS_CANCELLED);
        s=new Session(*h);run_session(*s,r);r.status=NS_OK;
    }catch(int code){r.status=code;h->abort();}catch(...){r.status=NS_STATE;h->abort();}
    if(s){if(!s->finish()){r.status=NS_UNLOAD;r.owners_retained=1;h->retained=s;retained_process=true;}else delete s;}
    h->disarm();h->running=false;*out=r;return r.status;
}
static int32_t cancel_run(NS_RunHandle* h){if(!h)return NS_BOUND;h->abort();return NS_OK;}
static int32_t destroy_run(NS_RunHandle* h){if(!h)return NS_BOUND;if(h->running)return NS_STATE;if(h->retained)return NS_UNLOAD;delete h;return NS_OK;}
extern "C" NS_EXPORT int32_t ns_consumer_get_run_api(uint32_t version,uint32_t capacity,NS_RunApi* out){
    if(version!=NS_RUN_VERSION)return NS_ABI;if(capacity!=sizeof(NS_RunApi))return NS_SIZE;if(!out)return NS_BOUND;
    if(!ns_run_c_layout_check())return NS_ABI;
    *out={{NS_RUN_VERSION,sizeof(NS_RunApi),NS_SCOPE,0},create_run,execute_run,cancel_run,destroy_run};return NS_OK;
}
