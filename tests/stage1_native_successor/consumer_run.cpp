#include "run_protocol.hpp"
#include "../../examples/dds214_layer_baseline/LayerSample.hpp"
#include "frag_sample.hpp"
#include "keyed_sample.hpp"
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/dds/core/status/DeadlineMissedStatus.hpp>
#include <fastdds/dds/core/status/LivelinessChangedStatus.hpp>
#include <algorithm>
#include <vector>
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
        // Marker content "2" selects the repeated-outage variant: 60 s window, two drops.
        const unsigned outages=[]{const char* d=std::getenv("TMPDIR");std::FILE* f=d?std::fopen((std::string(d)+"/route_window").c_str(),"r"):nullptr;
            int c=f?std::fgetc(f):EOF;if(f)std::fclose(f);return c=='2'?2u:1u;}();
        const auto t0=Clock::now();const auto window=std::chrono::seconds(outages==2?60:45);const int64_t window_ms=outages==2?60000:45000;
        uint32_t written=0,received=0,max_peer=0,write_fail=0,long_gaps=0;int64_t max_gap_ms=0,gap_end_ms=-1,last_rx_ms=-1,last_gap_end_ms=-1;bool aborted=false;
        auto ms=[&]{return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t0).count());};
        try{
            auto next=t0;
            while(Clock::now()-t0<window){
                need(h.live()&&!s.pump_failed,NS_CANCELLED);
                if(Clock::now()>=next){LayerSample v;v.run=local.run+2;v.side=local.side;v.ordinal=written+1;v.payload=stage1_run::payload(v.run,v.side,1);
                    if(ow->write(&v))++written;else ++write_fail;next+=std::chrono::milliseconds(200);}
                for(;;){LayerSample v;fd::SampleInfo info;const auto rc=orr->take_next_sample(&v,&info);if(rc==RC::RETCODE_NO_DATA)break;need(rc==RC::RETCODE_OK);
                    if(!info.valid_data)continue;need(v.run==local.run+2&&v.side==1-local.side);
                    const auto now=ms();if(last_rx_ms>=0&&now-last_rx_ms>=8000){++long_gaps;last_gap_end_ms=now;}if(last_rx_ms>=0&&now-last_rx_ms>max_gap_ms){max_gap_ms=now-last_rx_ms;gap_end_ms=now;}
                    last_rx_ms=now;++received;if(v.ordinal>max_peer)max_peer=v.ordinal;}
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }catch(...){aborted=true;}
        const bool recovered=!aborted&&gap_end_ms>=0&&last_rx_ms>gap_end_ms&&last_rx_ms>=window_ms-3000&&long_gaps>=outages&&last_rx_ms>last_gap_end_ms;
        std::printf("ROUTE_OUTAGE side=%u written=%u write_fail=%u received=%u max_peer=%u max_gap_ms=%lld gap_end_ms=%lld last_rx_ms=%lld aborted=%d pump_failed=%d recovered=%d send_failures=%u outages=%u long_gaps=%u\n",
            static_cast<unsigned>(local.side),written,write_fail,received,max_peer,static_cast<long long>(max_gap_ms),static_cast<long long>(gap_end_ms),static_cast<long long>(last_rx_ms),int(aborted),int(s.pump_failed.load()),int(recovered),s.send_failures.load(),outages,long_gaps);std::fflush(stdout);
        {const auto o=s.runtime->ObserveSocket();const auto& snd=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Send)];
         const auto& sk=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Socket)];const auto& bd=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Bind)];const auto& sh=o.operations[static_cast<size_t>(ds::phase0::PublicSocketOperation::Shutdown)];
         auto opt=[](const std::optional<std::int32_t>& v){return v?static_cast<long long>(*v):-99999LL;};
         std::printf("ROUTE_SOCKET side=%u stopped=%d closed=%d out_sock=%lld out_stopped=%d acc_sock=%lld acc_stopped=%d listen_stopped=%d shutdown_reason=%lld first_cb_err=%lld api_err=%d socket_calls=%llu bind_calls=%llu bind_last=%lld send_calls=%llu send_returns=%llu send_last=%lld shutdown_calls=%llu cb=%llu rejected_cb=%llu\n",
            static_cast<unsigned>(local.side),int(o.stopped),int(o.closed),opt(o.outgoing_socket),int(o.outgoing_stopped),opt(o.accepted_socket),int(o.accepted_stopped),int(o.listener_stopped),opt(o.shutdown_reason),opt(o.first_callback_error),int(bool(o.first_api_error)),
            (unsigned long long)sk.calls,(unsigned long long)bd.calls,bd.last_call?opt(bd.last_call->provider_return):-99999LL,(unsigned long long)snd.calls,(unsigned long long)snd.returns,snd.last_call?opt(snd.last_call->provider_return):-99999LL,(unsigned long long)sh.calls,(unsigned long long)o.callback_entries,(unsigned long long)o.rejected_callbacks);std::fflush(stdout);}
        need(recovered,NS_CANCELLED);
    }
    if([]{const char* d=std::getenv("TMPDIR");if(!d)return false;return access((std::string(d)+"/qos_matrix").c_str(),F_OK)==0;}())
    {   // QoS / keyed-topic matrix (opt-in: TMPDIR/qos_matrix). Symmetric: each side writes its
        // own topics and reads the peer's. One QOS_MATRIX line per case; any failure fails the run.
        using IH=eprosima::fastrtps::rtps::InstanceHandle_t;
        const unsigned me=local.side,peer=1-local.side;bool all=true;
        auto tname=[&](const char* what,unsigned side){return "native_run_"+std::to_string(local.run)+"_"+std::to_string(side)+"_q_"+what;};
        auto waitp=[&](auto pred,int ms){const auto end=Clock::now()+std::chrono::milliseconds(ms);
            for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);if(pred())return true;if(Clock::now()>=end)return false;
                (void)h.progress.wait(seen,std::min(end,Clock::now()+std::chrono::milliseconds(50)));}};
        auto report=[&](const char* test,bool pass,const std::string& detail){all=all&&pass;
            std::printf("QOS_MATRIX side=%u test=%s pass=%d %s\n",me,test,int(pass),detail.c_str());std::fflush(stdout);};
        auto topic2=[&](const char* what,const std::string& type,fd::Topic*& tx,fd::Topic*& rx){
            tx=s.participant->create_topic(tname(what,me),type,fd::TOPIC_QOS_DEFAULT);rx=s.participant->create_topic(tname(what,peer),type,fd::TOPIC_QOS_DEFAULT);need(tx&&rx);};
        auto matched=[&](fd::DataWriter* w,fd::DataReader* r,int wn,int rn){return waitp([&]{fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus b;
            need(w->get_publication_matched_status(a)==RC::RETCODE_OK&&r->get_subscription_matched_status(b)==RC::RETCODE_OK);return a.current_count==wn&&b.current_count==rn;},5000);};
        auto layer=[&](std::uint64_t run,std::uint32_t n){LayerSample v;v.run=run;v.side=me;v.ordinal=n;v.payload=stage1_run::payload(run,me,n);return v;};
        // Cross-side barrier on a TRANSIENT_LOCAL KEEP_ALL topic.
        fd::Topic *sync_tx=nullptr,*sync_rx=nullptr;topic2("sync",s.type.get_type_name(),sync_tx,sync_rx);
        fd::DataWriterQos swq=wq;swq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;swq.history().kind=fd::KEEP_ALL_HISTORY_QOS;
        fd::DataReaderQos srq=rq;srq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;srq.history().kind=fd::KEEP_ALL_HISTORY_QOS;
        auto* sw=pub->create_datawriter(sync_tx,swq,&s.signals);auto* sr=sub->create_datareader(sync_rx,srq,&s.signals);need(sw&&sr);
        std::uint32_t peer_stage=0;
        auto barrier=[&](std::uint32_t n){auto v=layer(local.run+7,n);need(sw->write(&v));
            need(waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(sr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+7&&x.side==peer&&x.ordinal>peer_stage)peer_stage=x.ordinal;}return peer_stage>=n;},15000),NS_CANCELLED);};
        barrier(1);
        fd::TypeSupport kt{new stage1_keyed::Type};need(kt.register_type(s.participant)==RC::RETCODE_OK);
        {   // 1. Keyed instances, dispose and unregister (no autodispose).
            fd::Topic *tx,*rx;topic2("keyed",kt.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.history().depth=4;w.writer_data_lifecycle().autodispose_unregistered_instances=false;fd::DataReaderQos r=rq;r.history().depth=4;
            auto* kw=pub->create_datawriter(tx,w,&s.signals);auto* kr=sub->create_datareader(rx,r,&s.signals);need(kw&&kr);
            const bool m=matched(kw,kr,1,1);barrier(2);
            std::array<IH,3> hs{};
            for(std::uint32_t k=1;k<=3;++k){stage1_keyed::Sample v;v.key=k;v.run=local.run;v.side=me;hs[k-1]=kw->register_instance(&v);
                for(std::uint32_t n=1;n<=2;++n){v.seq=n;v.tag=k*10+n;need(kw->write(&v,hs[k-1])==RC::RETCODE_OK);}}
            {stage1_keyed::Sample v;v.run=local.run;v.side=me;v.key=1;need(kw->dispose(&v,hs[0])==RC::RETCODE_OK);v.key=2;need(kw->unregister_instance(&v,hs[1])==RC::RETCODE_OK);}
            std::array<unsigned,3> got{};bool disposed=false,nowriters=false;std::vector<IH> handles;
            bool ok=m&&waitp([&]{for(;;){stage1_keyed::Sample x;fd::SampleInfo i;if(kr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data){if(x.run==local.run&&x.side==peer&&x.key>=1&&x.key<=3&&x.tag==x.key*10+x.seq){++got[x.key-1];
                    if(std::find(handles.begin(),handles.end(),i.instance_handle)==handles.end())handles.push_back(i.instance_handle);}}
                else{if(i.instance_state==fd::NOT_ALIVE_DISPOSED_INSTANCE_STATE)disposed=true;if(i.instance_state==fd::NOT_ALIVE_NO_WRITERS_INSTANCE_STATE)nowriters=true;}}
                return got[0]==2&&got[1]==2&&got[2]==2&&disposed&&nowriters;},5000);
            ok=ok&&handles.size()==3;
            report("keyed_instances_dispose_unregister",ok,"samples="+std::to_string(got[0])+","+std::to_string(got[1])+","+std::to_string(got[2])+
                " instances="+std::to_string(handles.size())+" disposed="+std::to_string(disposed)+" no_writers="+std::to_string(nowriters));
            (void)kw->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(2,0));
        }
        barrier(3);
        {   // 2. TRANSIENT_LOCAL late joiner: reader created 1.5 s after the writer's samples.
            fd::Topic *tx,*rx;topic2("late",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;w.history().depth=5;
            auto* lw=pub->create_datawriter(tx,w,&s.signals);need(lw);
            for(std::uint32_t n=1;n<=5;++n){auto v=layer(local.run+8,n);need(lw->write(&v));}
            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            fd::DataReaderQos r=rq;r.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;r.history().depth=5;
            auto* lr=sub->create_datareader(rx,r,&s.signals);need(lr);
            std::uint32_t next=1;bool order=true;
            const bool ok=waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(lr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;
                if(x.run!=local.run+8||x.side!=peer||x.ordinal!=next||x.payload!=stage1_run::payload(x.run,peer,x.ordinal))order=false;else ++next;}
                return next==6||!order;},6000)&&order&&next==6;
            report("transient_local_late_joiner",ok,"received="+std::to_string(next-1)+" in_order="+std::to_string(order));
            (void)lw->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(3,0));
        }
        barrier(4);
        {   // 3. KEEP_ALL reliable burst of 300 samples, exact order.
            fd::Topic *tx,*rx;topic2("burst",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.history().kind=fd::KEEP_ALL_HISTORY_QOS;fd::DataReaderQos r=rq;r.history().kind=fd::KEEP_ALL_HISTORY_QOS;
            auto* bw=pub->create_datawriter(tx,w,&s.signals);auto* br=sub->create_datareader(rx,r,&s.signals);need(bw&&br);
            const bool m=matched(bw,br,1,1);barrier(5);
            for(std::uint32_t n=1;n<=300;++n){auto v=layer(local.run+9,n);need(bw->write(&v));}
            std::uint32_t next=1;bool order=true;
            const bool ok=m&&waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(br->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;
                if(x.run!=local.run+9||x.side!=peer||x.ordinal!=next)order=false;else ++next;}return next==301||!order;},15000)&&order&&next==301;
            const bool acked=bw->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(5,0))==RC::RETCODE_OK;
            report("keep_all_reliable_burst_300",ok&&acked,"received="+std::to_string(next-1)+" in_order="+std::to_string(order)+" acked="+std::to_string(acked));
        }
        barrier(6);
        {   // 4. DEADLINE 300 ms: no miss while writing every 100 ms, misses after writes stop.
            fd::Topic *tx,*rx;topic2("deadline",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.deadline().period=eprosima::fastrtps::Duration_t(0,300000000);fd::DataReaderQos r=rq;r.deadline().period=w.deadline().period;
            auto* dw=pub->create_datawriter(tx,w,&s.signals);auto* dr=sub->create_datareader(rx,r,&s.signals);need(dw&&dr);
            const bool m=matched(dw,dr,1,1);barrier(7);
            for(std::uint32_t n=1;n<=5;++n){auto v=layer(local.run+10,n);need(dw->write(&v));std::this_thread::sleep_for(std::chrono::milliseconds(100));}
            fd::RequestedDeadlineMissedStatus during{};fd::OfferedDeadlineMissedStatus offered_during{};
            need(dr->get_requested_deadline_missed_status(during)==RC::RETCODE_OK&&dw->get_offered_deadline_missed_status(offered_during)==RC::RETCODE_OK);
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            fd::RequestedDeadlineMissedStatus after{};fd::OfferedDeadlineMissedStatus offered_after{};
            need(dr->get_requested_deadline_missed_status(after)==RC::RETCODE_OK&&dw->get_offered_deadline_missed_status(offered_after)==RC::RETCODE_OK);
            unsigned received=0;for(;;){LayerSample x;fd::SampleInfo i;if(dr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(i.valid_data&&x.run==local.run+10&&x.side==peer)++received;}
            const bool ok=m&&received==5&&during.total_count==0&&offered_during.total_count==0&&after.total_count>=1&&offered_after.total_count>=1;
            report("deadline_300ms",ok,"received="+std::to_string(received)+" requested_missed="+std::to_string(during.total_count)+"->"+std::to_string(after.total_count)+
                " offered_missed="+std::to_string(offered_during.total_count)+"->"+std::to_string(offered_after.total_count));
        }
        barrier(8);
        {   // 5. AUTOMATIC liveliness (lease 1 s): peer writer alive, then not alive after it is deleted.
            fd::Topic *tx,*rx;topic2("liveliness",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.liveliness().kind=fd::AUTOMATIC_LIVELINESS_QOS;w.liveliness().lease_duration=eprosima::fastrtps::Duration_t(1,0);
            w.liveliness().announcement_period=eprosima::fastrtps::Duration_t(0,500000000);
            fd::DataReaderQos r=rq;r.liveliness().kind=fd::AUTOMATIC_LIVELINESS_QOS;r.liveliness().lease_duration=eprosima::fastrtps::Duration_t(1,0);
            auto* lw=pub->create_datawriter(tx,w,&s.signals);auto* lr=sub->create_datareader(rx,r,&s.signals);need(lw&&lr);
            auto alive=[&]{fd::LivelinessChangedStatus st;need(lr->get_liveliness_changed_status(st)==RC::RETCODE_OK);return st.alive_count;};
            const bool m=matched(lw,lr,1,1);
            const bool up=m&&waitp([&]{return alive()==1;},4000);
            barrier(9);
            need(pub->delete_datawriter(lw)==RC::RETCODE_OK);
            const bool down=waitp([&]{return alive()==0;},5000);
            report("liveliness_automatic_1s",up&&down,"alive_seen="+std::to_string(up)+" not_alive_after_delete="+std::to_string(down));
        }
        barrier(10);
        {   // 6. EXCLUSIVE ownership: only the strength-20 writer's samples are delivered.
            fd::Topic *tx,*rx;topic2("ownership",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos low=wq;low.ownership().kind=fd::EXCLUSIVE_OWNERSHIP_QOS;low.ownership_strength().value=10;
            fd::DataWriterQos high=low;high.ownership_strength().value=20;
            fd::DataReaderQos r=rq;r.ownership().kind=fd::EXCLUSIVE_OWNERSHIP_QOS;r.history().depth=32;
            auto* w1=pub->create_datawriter(tx,low,&s.signals);auto* w2=pub->create_datawriter(tx,high,&s.signals);auto* orr=sub->create_datareader(rx,r,&s.signals);need(w1&&w2&&orr);
            const bool m=matched(w1,orr,1,2)&&matched(w2,orr,1,2);barrier(11);
            // Ownership is assigned on arrival: establish the strength-20 owner first.
            {auto b=layer(local.run+11,3000);need(w2->write(&b));}
            const bool owned=waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(orr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+11&&x.side==peer&&x.ordinal==3000)return true;}return false;},3000);
            barrier(19);
            for(std::uint32_t n=1;n<=10;++n){auto a=layer(local.run+11,1000+n);auto b=layer(local.run+11,2000+n);need(w1->write(&a)&&w2->write(&b));
                std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            unsigned hi=0,lo=0;
            (void)waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(orr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+11||x.side!=peer)continue;
                if(x.ordinal>=2000)++hi;else ++lo;}return hi>=10;},5000);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            for(;;){LayerSample x;fd::SampleInfo i;if(orr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+11||x.side!=peer)continue;if(x.ordinal>=2000)++hi;else ++lo;}
            report("ownership_exclusive_strength",m&&owned&&hi==10&&lo==0,"owner_established="+std::to_string(owned)+" strength20="+std::to_string(hi)+" strength10="+std::to_string(lo));
        }
        barrier(12);
        {   // 7. LIFESPAN 2 s: unread samples expire; fresh samples are still delivered.
            fd::Topic *tx,*rx;topic2("lifespan",s.type.get_type_name(),tx,rx);
            fd::DataWriterQos w=wq;w.lifespan().duration=eprosima::fastrtps::Duration_t(2,0);w.history().depth=10;
            fd::DataReaderQos r=rq;r.lifespan().duration=eprosima::fastrtps::Duration_t(2,0);r.history().depth=10;
            auto* fw=pub->create_datawriter(tx,w,&s.signals);auto* fr=sub->create_datareader(rx,r,&s.signals);need(fw&&fr);
            const bool m=matched(fw,fr,1,1);barrier(13);
            for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+12,n);need(fw->write(&v));}
            std::this_thread::sleep_for(std::chrono::milliseconds(3500));
            unsigned stale=0,fresh=0;
            auto drain=[&]{for(;;){LayerSample x;fd::SampleInfo i;if(fr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+12||x.side!=peer)continue;
                if(x.ordinal<=3)++stale;else ++fresh;}};
            drain();barrier(14);
            for(std::uint32_t n=4;n<=5;++n){auto v=layer(local.run+12,n);need(fw->write(&v));}
            (void)waitp([&]{drain();return fresh>=2;},1500);
            report("lifespan_2s",m&&stale==0&&fresh==2,"expired_batch_delivered="+std::to_string(stale)+" fresh_delivered="+std::to_string(fresh));
        }
        barrier(15);
        {   // 8. PARTITIONS: a reader in partition "pb" never matches a writer in "pa"; one in "pa" does.
            fd::Topic *tx,*rx;topic2("partition",s.type.get_type_name(),tx,rx);
            fd::PublisherQos pq=fd::PUBLISHER_QOS_DEFAULT;pq.partition().push_back("pa");
            fd::SubscriberQos wrong_q=fd::SUBSCRIBER_QOS_DEFAULT;wrong_q.partition().push_back("pb");
            fd::SubscriberQos right_q=fd::SUBSCRIBER_QOS_DEFAULT;right_q.partition().push_back("pa");
            auto* ppub=s.participant->create_publisher(pq);auto* wsub=s.participant->create_subscriber(wrong_q);auto* rsub=s.participant->create_subscriber(right_q);need(ppub&&wsub&&rsub);
            auto* pw=ppub->create_datawriter(tx,wq,&s.signals);auto* wrong=wsub->create_datareader(rx,rq,&s.signals);auto* right=rsub->create_datareader(rx,rq,&s.signals);need(pw&&wrong&&right);
            // Both directions: our in-partition reader saw the peer writer AND our writer saw the peer's in-partition reader.
            const bool m=waitp([&]{fd::SubscriptionMatchedStatus b;fd::PublicationMatchedStatus a;need(right->get_subscription_matched_status(b)==RC::RETCODE_OK&&
                pw->get_publication_matched_status(a)==RC::RETCODE_OK);return b.current_count==1&&a.current_count==1;},10000);
            barrier(16);
            auto v=layer(local.run+13,1);need(pw->write(&v));
            unsigned got_right=0,got_wrong=0;
            (void)waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(right->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(i.valid_data&&x.run==local.run+13&&x.side==peer)++got_right;}return got_right>=1;},3000);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            for(;;){LayerSample x;fd::SampleInfo i;if(wrong->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(i.valid_data)++got_wrong;}
            fd::SubscriptionMatchedStatus ws;need(wrong->get_subscription_matched_status(ws)==RC::RETCODE_OK);
            report("partitions",m&&got_right==1&&got_wrong==0&&ws.total_count==0,"matched="+std::to_string(m)+" same_partition_delivered="+std::to_string(got_right)+
                " other_partition_matches="+std::to_string(ws.total_count)+" other_partition_delivered="+std::to_string(got_wrong));
        }
        barrier(17);
        std::printf("QOS_MATRIX side=%u test=content_filtered_topic pass=SKIPPED reason=test_types_have_no_type_object\n",me);
        std::printf("QOS_MATRIX side=%u summary all_pass=%d\n",me,int(all));std::fflush(stdout);
        need(all,NS_STATE);
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
