#include "run_protocol.hpp"
#include "../../examples/dds214_layer_baseline/LayerSample.hpp"
#include "frag_sample.hpp"
#include "keyed_sample.hpp"
#include "large_sample.hpp"
#include "plain_sample.hpp"
#include <fastdds/dds/core/LoanableSequence.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/topic/ContentFilteredTopic.hpp>
#include <fastdds/dds/core/condition/WaitSet.hpp>
#include <fastdds/dds/subscriber/ReadCondition.hpp>
#include <fastdds/rtps/flowcontrol/FlowControllerDescriptor.hpp>
#include <fastdds/rtps/attributes/ServerAttributes.h>
#include <sstream>
#include <fastrtps/types/DynamicTypeBuilderFactory.h>
#include <fastrtps/types/DynamicTypeBuilder.h>
#include <fastrtps/types/DynamicDataFactory.h>
#include <fastrtps/types/DynamicData.h>
#include <fastrtps/types/DynamicPubSubType.h>
#include <fastrtps/types/TypeObjectFactory.h>
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
#include <sys/wait.h>
#include <csignal>
#include <cstring>
#include <optional>
extern char** environ;

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
// Feature matrix: observes peer user/topic/group data during discovery (test code only).
struct DiscoverySpy final:fd::DomainParticipantListener {
    NS_RunHandle& h;std::string want_user,want_topic,want_group;std::atomic<bool> user{false},topic{false},group{false};
    explicit DiscoverySpy(NS_RunHandle& x):h(x){}
    template<class V> static bool eq(const V& v,const std::string& s){if(v.size()!=s.size())return false;for(std::size_t i=0;i<s.size();++i)if(v[i]!=static_cast<unsigned char>(s[i]))return false;return true;}
    void on_participant_discovery(fd::DomainParticipant*,eprosima::fastrtps::rtps::ParticipantDiscoveryInfo&& info) override{
        if(eq(info.info.m_userData.data_vec(),want_user))user=true;h.progress.notify();}
    void on_publisher_discovery(fd::DomainParticipant*,eprosima::fastrtps::rtps::WriterDiscoveryInfo&& info) override{
        if(eq(info.info.m_qos.m_topicData.data_vec(),want_topic))topic=true;if(eq(info.info.m_qos.m_groupData.data_vec(),want_group))group=true;h.progress.notify();}
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
// ---- Peer process restart helper (feature matrix only). The runner re-executes its own
// image (/proc/self/exe) with MDDS_RESTART_HELPER=<request file>; this constructor then runs
// one standalone peer incarnation on its own Runtime and exits before the runner's main.
// It is a separate OS process: its own native sockets, Runtime and Participant. ----
struct RestartRequest{NS_Request q;std::uint64_t run;std::uint32_t side,peer,incarnation,domain;char status[200],go[200],topic_tx[160],topic_rx[160];};
static void restart_status(const char* path,const std::string& text){const std::string tmp=std::string(path)+".tmp";
    if(FILE* f=std::fopen(tmp.c_str(),"w")){std::fputs(text.c_str(),f);std::fclose(f);std::rename(tmp.c_str(),path);}}
static void restart_fail(const RestartRequest& r,const char* where){restart_status(r.status,std::string("fail ")+where);_exit(3);}
static int peer_restart_helper(const RestartRequest& r){
    auto chk=[&](bool v,const char* where){if(!v)restart_fail(r,where);};
    NS_Request q=r.q;q.domain=r.domain;ds::DSoftBusTransportDescriptor config(65536);
    try{config=configuration(q);}catch(...){restart_fail(r,"config");}
    auto rt=std::make_unique<ds::experimental::Runtime>(backend(config,q),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
    // The listener appears with the first Participant; the dial waits until the live peer's
    // listener exists too (the parent creates the go file).
    // Participant creation blocks in this gate; reaching it means this listener exists.
    chk(rt->SetPreConnectGate([&r]{restart_status(r.status,"listening");const auto end=Clock::now()+std::chrono::seconds(60);
        while(access(r.go,F_OK)!=0){if(Clock::now()>=end)return false;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return true;}),"gate");
    auto desc=rt->MakeStockDescriptor(config);chk(bool(desc),"descriptor");
    std::thread pump([&]{try{for(;;){auto g=rt->WorkGeneration();
        for(;;){auto x=rt->PumpSend();if(x.status!=ds::NativeStatus::None)break;}
        ds::phase0::BackendStatus rx;do{rx=rt->PumpReceive();}while(rx==ds::phase0::BackendStatus::None);
        auto w=rt->WaitForWork(g);if(w!=ds::WorkWaitResult::Work&&w!=ds::WorkWaitResult::Deadline)return;}}catch(...){}});
    auto* f=fd::DomainParticipantFactory::get_instance();
    fd::DomainParticipantQos qos;qos.name(("restart_"+std::to_string(r.side)+"_"+std::to_string(r.incarnation)).c_str());
    qos.transport().use_builtin_transports=false;qos.transport().user_transports={desc};qos.transport().max_msg_size_no_frag=config.maxMessageSize;
    auto& b=qos.wire_protocol().builtin;b.metatrafficUnicastLocatorList.clear();b.metatrafficUnicastLocatorList.push_back(config.default_user_unicast.at(0));
    b.metatrafficMulticastLocatorList.clear();b.initialPeersList.clear();b.initialPeersList.push_back(config.directory.routes.at(0).key.remote_locator);
    qos.wire_protocol().default_unicast_locator_list.clear();qos.wire_protocol().default_unicast_locator_list.push_back(config.default_user_unicast.at(0));
    b.discovery_config.leaseDuration=eprosima::fastrtps::Duration_t(3,0);b.discovery_config.leaseDuration_announcementperiod=eprosima::fastrtps::Duration_t(1,0);
    auto* p=f->create_participant(q.domain,qos);chk(p!=nullptr,"participant");
    fd::TypeSupport type{new LayerSampleType};chk(type.register_type(p)==RC::RETCODE_OK,"type");
    auto* pub=p->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub=p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);chk(pub&&sub,"pubsub");
    auto* tx=p->create_topic(r.topic_tx,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);auto* rxt=p->create_topic(r.topic_rx,type.get_type_name(),fd::TOPIC_QOS_DEFAULT);chk(tx&&rxt,"topics");
    fd::DataWriterQos wq;wq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;wq.durability().kind=fd::VOLATILE_DURABILITY_QOS;wq.history().kind=fd::KEEP_ALL_HISTORY_QOS;wq.data_sharing().off();
    fd::DataReaderQos rq;rq.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;rq.durability().kind=fd::VOLATILE_DURABILITY_QOS;rq.history().kind=fd::KEEP_ALL_HISTORY_QOS;rq.data_sharing().off();
    auto* w=pub->create_datawriter(tx,wq);auto* rd=sub->create_datareader(rxt,rq);chk(w&&rd,"endpoints");
    auto until=[](auto pred,int ms){const auto end=Clock::now()+std::chrono::milliseconds(ms);while(Clock::now()<end){if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return pred();};
    chk(until([&]{fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus c;w->get_publication_matched_status(a);rd->get_subscription_matched_status(c);return a.current_count>=1&&c.current_count>=1;},45000),"match");
    restart_status(r.status,"matched");
    for(std::uint32_t n=1;n<=3;++n){LayerSample v;v.run=r.run;v.side=r.side;v.ordinal=n;v.payload=stage1_run::payload(v.run,v.side,n);chk(w->write(&v),"write");}
    std::uint32_t next=1;
    chk(until([&]{for(;;){LayerSample x;fd::SampleInfo i;if(rd->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
        if(i.valid_data&&x.run==r.run&&x.side==r.peer&&x.ordinal==next&&x.payload==stage1_run::payload(x.run,x.side,x.ordinal))++next;}return next==4;},20000),"receive");
    chk(w->wait_for_acknowledgments(eprosima::fastrtps::Duration_t(5,0))==RC::RETCODE_OK,"ack");
    char prefix[64];{std::ostringstream o;o<<p->guid().guidPrefix;std::snprintf(prefix,sizeof(prefix),"%s",o.str().c_str());}
    restart_status(r.status,std::string("exchanged received=3 pid=")+std::to_string(getpid())+" prefix="+prefix);
    if(r.incarnation==1)for(;;)std::this_thread::sleep_for(std::chrono::seconds(1)); // killed by the parent: a crash, no teardown
    std::this_thread::sleep_for(std::chrono::seconds(1));
    chk(p->delete_contained_entities()==RC::RETCODE_OK&&f->delete_participant(p)==RC::RETCODE_OK,"teardown");
    desc.reset();rt->RequestPumpStop();pump.join();const bool closed=rt->Close();
    restart_status(r.status,std::string("done received=3 closed=")+std::to_string(closed)+" pid="+std::to_string(getpid())+" prefix="+prefix);
    return closed?0:4;
}
__attribute__((constructor)) static void peer_restart_helper_entry(){
    const char* path=std::getenv("MDDS_RESTART_HELPER");if(!path)return;
    RestartRequest r{};FILE* f=std::fopen(path,"rb");if(!f)_exit(2);const bool ok=std::fread(&r,sizeof(r),1,f)==1;std::fclose(f);if(!ok)_exit(2);
    _exit(peer_restart_helper(r));
}
static void guid(uint8_t* p,const eprosima::fastrtps::rtps::GUID_t& g){std::memcpy(p,g.guidPrefix.value,12);std::memcpy(p+12,g.entityId.value,4);}
static int64_t sequence(const eprosima::fastrtps::rtps::SequenceNumber_t& s){need(s.high>=0&&(s.high||s.low));return (int64_t(s.high)<<32)|s.low;}
static void run_session(Session& s,NS_RunResult& out){
    auto& h=s.h;const auto& q=h.request.deployment;const auto& local=h.request.local;
    auto config=configuration(q);
    if([]{const char* d=std::getenv("TMPDIR");if(!d)return false;return access((std::string(d)+"/feature_matrix").c_str(),F_OK)==0;}()){
        config.max_participants=4;config.receive_records=256;config.receive_bytes=2u<<20;
        config.send_limits.peer_records=256;config.send_limits.peer_bytes=2u<<20;config.send_limits.global_records=256;config.send_limits.global_bytes=2u<<20;
    }
    s.runtime=std::make_unique<ds::experimental::Runtime>(backend(config,q),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
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
    if([]{const char* d=std::getenv("TMPDIR");if(!d)return false;return access((std::string(d)+"/feature_matrix").c_str(),F_OK)==0;}())
    {   // Fast DDS feature matrix (opt-in: TMPDIR/feature_matrix). Symmetric across the two boards;
        // one FEATURE_MATRIX line per case; any failure fails the run. Test code only.
        namespace ft=eprosima::fastrtps::types;
        const unsigned me=local.side,peer=1-local.side;bool all=true;std::uint32_t stage=0;
        auto tname=[&](const std::string& what,unsigned side){return "native_run_"+std::to_string(local.run)+"_"+std::to_string(side)+"_f_"+what;};
        auto waitp=[&](auto pred,int ms){const auto end=Clock::now()+std::chrono::milliseconds(ms);
            for(;;){const auto seen=h.progress.snapshot();need(h.live()&&!s.pump_failed,NS_CANCELLED);if(pred())return true;if(Clock::now()>=end)return false;
                (void)h.progress.wait(seen,std::min(end,Clock::now()+std::chrono::milliseconds(50)));}};
        auto report=[&](const char* test,bool pass,const std::string& detail){all=all&&pass;
            std::printf("FEATURE_MATRIX side=%u test=%s pass=%d %s\n",me,test,int(pass),detail.c_str());std::fflush(stdout);};
        auto topic2=[&](fd::DomainParticipant* p,const std::string& what,const std::string& type,fd::Topic*& tx,fd::Topic*& rx,const fd::TopicQos& tq){
            tx=p->create_topic(tname(what,me),type,tq);rx=p->create_topic(tname(what,peer),type,tq);need(tx&&rx);};
        auto matched=[&](fd::DataWriter* w,fd::DataReader* r,int wn,int rn,int ms){return waitp([&]{fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus b;
            need(w->get_publication_matched_status(a)==RC::RETCODE_OK&&r->get_subscription_matched_status(b)==RC::RETCODE_OK);return a.current_count==wn&&b.current_count==rn;},ms);};
        auto layer=[&](std::uint64_t run,std::uint32_t n){LayerSample v;v.run=run;v.side=me;v.ordinal=n;v.payload=stage1_run::payload(run,me,n);return v;};
        auto dur=[](int sec){return eprosima::fastrtps::Duration_t(sec,0u);};
        fd::Topic *sync_tx=nullptr,*sync_rx=nullptr;topic2(s.participant,"sync",s.type.get_type_name(),sync_tx,sync_rx,fd::TOPIC_QOS_DEFAULT);
        fd::DataWriterQos swq=wq;swq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;swq.history().kind=fd::KEEP_ALL_HISTORY_QOS;
        fd::DataReaderQos srq=rq;srq.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;srq.history().kind=fd::KEEP_ALL_HISTORY_QOS;
        auto* sw=pub->create_datawriter(sync_tx,swq,&s.signals);auto* sr=sub->create_datareader(sync_rx,srq,&s.signals);need(sw&&sr);
        std::uint32_t peer_stage=0;
        auto barrier=[&]{const auto n=++stage;auto v=layer(local.run+20,n);need(sw->write(&v));
            need(waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(sr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+20&&x.side==peer&&x.ordinal>peer_stage)peer_stage=x.ordinal;}return peer_stage>=n;},20000),NS_CANCELLED);};
        auto keep_all=[](auto q){q.history().kind=fd::KEEP_ALL_HISTORY_QOS;return q;};
        // Extra participants are deleted before the session's own teardown, also on failure.
        DiscoverySpy spy(h);spy.want_user="fdds-dsb-user-"+std::to_string(peer);spy.want_topic="fdds-dsb-topic-"+std::to_string(peer);spy.want_group="fdds-dsb-group-"+std::to_string(peer);
        struct Extra{fd::DomainParticipantFactory* f;std::vector<fd::DomainParticipant*> ps;
            ~Extra(){for(auto it=ps.rbegin();it!=ps.rend();++it){if(!*it)continue;try{if((*it)->delete_contained_entities()==RC::RETCODE_OK)(void)f->delete_participant(*it);}catch(...){}}}} extra{s.factory,{}};
        auto octets=[](const std::string& t){return std::vector<eprosima::fastrtps::rtps::octet>(t.begin(),t.end());};
        barrier();
        {   // 1. Second Participant in the same process and domain, with user/topic/group data,
            //    a flow controller and SQLite persistence configured on it.
            fd::DomainParticipantQos q2=qos;q2.name((std::string(qos.name().c_str())+"_p2").c_str());
            auto d2=s.runtime->MakeStockDescriptor(config);need(bool(d2));s.retained_descriptors.push_back(d2);q2.transport().user_transports={d2};
            q2.user_data().data_vec(octets("fdds-dsb-user-"+std::to_string(me)));
            auto fc=std::make_shared<eprosima::fastdds::rtps::FlowControllerDescriptor>();fc->name="fdds_dsb_fc";fc->max_bytes_per_period=65536;fc->period_ms=100;q2.flow_controllers().push_back(fc);
            const std::string db=std::string(std::getenv("TMPDIR"))+"/persist_"+std::to_string(me)+".db";(void)unlink(db.c_str());
            q2.properties().properties().emplace_back("dds.persistence.plugin","builtin.SQLITE3");q2.properties().properties().emplace_back("dds.persistence.sqlite3.filename",db);
            auto* p2=s.factory->create_participant(q.domain,q2,&spy,fd::StatusMask::none());extra.ps.push_back(p2);need(p2,NS_STATE);
            need(s.type.register_type(p2)==RC::RETCODE_OK);
            fd::TopicQos tq=fd::TOPIC_QOS_DEFAULT;tq.topic_data().data_vec(octets("fdds-dsb-topic-"+std::to_string(me)));
            fd::PublisherQos pq=fd::PUBLISHER_QOS_DEFAULT;pq.group_data().data_vec(octets("fdds-dsb-group-"+std::to_string(me)));
            auto* pub2=p2->create_publisher(pq);auto* sub2=p2->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub2&&sub2);
            fd::Topic *tx,*rx;tx=p2->create_topic(tname("p2",me),s.type.get_type_name(),tq);rx=p2->create_topic(tname("p2",peer),s.type.get_type_name(),fd::TOPIC_QOS_DEFAULT);need(tx&&rx);
            auto* w2=pub2->create_datawriter(tx,keep_all(wq),&s.signals);auto* r2=sub2->create_datareader(rx,keep_all(rq),&s.signals);need(w2&&r2);
            const bool m=matched(w2,r2,1,1,15000);barrier();
            for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+21,n);need(w2->write(&v));}
            std::uint32_t next=1;const bool got=m&&waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(r2->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+21&&x.side==peer&&x.ordinal==next&&x.payload==stage1_run::payload(x.run,peer,x.ordinal))++next;}return next==4;},8000);
            report("second_participant_same_domain",got,"matched="+std::to_string(m)+" received="+std::to_string(next-1)+" main_participant_live="+std::to_string(s.participant!=nullptr));
            const bool disc=waitp([&]{return spy.user.load()&&spy.topic.load()&&spy.group.load();},5000);
            report("discovery_user_topic_group_data",disc,"user_data="+std::to_string(spy.user.load())+" topic_data="+std::to_string(spy.topic.load())+" group_data="+std::to_string(spy.group.load()));
            barrier();
            {   // 2. ASYNCHRONOUS publish mode through a 64 KiB / 100 ms flow controller: 6 x 150000-byte samples.
                fd::TypeSupport ft2{new stage1_frag::Type};need(ft2.register_type(p2)==RC::RETCODE_OK);
                fd::Topic *atx,*arx;topic2(p2,"async",ft2.get_type_name(),atx,arx,fd::TOPIC_QOS_DEFAULT);
                fd::DataWriterQos aw=keep_all(wq);aw.publish_mode().kind=fd::ASYNCHRONOUS_PUBLISH_MODE;aw.publish_mode().flow_controller_name="fdds_dsb_fc";
                auto* fw=pub2->create_datawriter(atx,aw,&s.signals);auto* fr=sub2->create_datareader(arx,keep_all(rq),&s.signals);need(fw&&fr);
                const bool am=matched(fw,fr,1,1,15000);barrier();
                const auto t0=Clock::now();for(std::uint32_t n=1;n<=6;++n){auto v=stage1_frag::make(local.run+40+n,me);need(fw->write(&v));}
                const bool acked=fw->wait_for_acknowledgments(dur(25))==RC::RETCODE_OK;
                const auto ack_ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t0).count();
                std::uint32_t nextf=1;const bool fok=am&&waitp([&]{for(;;){stage1_frag::Sample v;fd::SampleInfo i;if(fr->take_next_sample(&v,&i)!=RC::RETCODE_OK)break;
                    if(i.valid_data&&stage1_frag::valid(v,local.run+40+nextf,peer))++nextf;}return nextf==7;},25000);
                // Baseline on the same link: the same 6 samples through an async writer without the
                // flow controller. The limit is proven by the ratio, not by an assumed link speed.
                fd::Topic *btx,*brx;topic2(p2,"async_free",ft2.get_type_name(),btx,brx,fd::TOPIC_QOS_DEFAULT);
                fd::DataWriterQos bw=keep_all(wq);bw.publish_mode().kind=fd::ASYNCHRONOUS_PUBLISH_MODE;
                auto* bfw=pub2->create_datawriter(btx,bw,&s.signals);auto* bfr=sub2->create_datareader(brx,keep_all(rq),&s.signals);need(bfw&&bfr);
                const bool bm=matched(bfw,bfr,1,1,15000);barrier();
                const auto t1=Clock::now();for(std::uint32_t n=1;n<=6;++n){auto v=stage1_frag::make(local.run+60+n,me);need(bfw->write(&v));}
                const bool backed=bfw->wait_for_acknowledgments(dur(25))==RC::RETCODE_OK;
                const auto free_ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t1).count();
                std::uint32_t nextb=1;(void)waitp([&]{for(;;){stage1_frag::Sample v;fd::SampleInfo i;if(bfr->take_next_sample(&v,&i)!=RC::RETCODE_OK)break;
                    if(i.valid_data&&stage1_frag::valid(v,local.run+60+nextb,peer))++nextb;}return nextb==7;},25000);
                // 6 x 150000 bytes at 65536 bytes / 100 ms needs at least ~1.3 s; unthrottled must be at least twice as fast.
                const bool limited=ack_ms>=1200&&bm&&backed&&nextb==7&&free_ms*2<=ack_ms;
                report("async_publish_flow_controller",fok&&acked&&limited,"received_verified="+std::to_string(nextf-1)+" acked="+std::to_string(acked)+" write_to_ack_ms="+std::to_string(ack_ms)+
                    " unthrottled_ms="+std::to_string(free_ms)+" unthrottled_received="+std::to_string(nextb-1)+" limit=65536B/100ms");
            }
            barrier();
            {   // 3. TRANSIENT durability with the SQLite persistence plugin: history survives writer recreation.
                fd::Topic *ptx,*prx;topic2(p2,"persist",s.type.get_type_name(),ptx,prx,fd::TOPIC_QOS_DEFAULT);
                fd::DataWriterQos pw=keep_all(wq);pw.durability().kind=fd::TRANSIENT_DURABILITY_QOS;
                pw.properties().properties().emplace_back("dds.persistence.guid","fd.d5.b0.77.72.69.74.65.72.00.00.0"+std::to_string(me+1)+"|0.0.1.3");
                auto* w1=pub2->create_datawriter(ptx,pw,&s.signals);need(w1);
                for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+22,n);need(w1->write(&v));}
                need(pub2->delete_datawriter(w1)==RC::RETCODE_OK);
                auto* w2b=pub2->create_datawriter(ptx,pw,&s.signals);need(w2b);
                for(std::uint32_t n=4;n<=5;++n){auto v=layer(local.run+22,n);need(w2b->write(&v));}
                barrier();
                fd::DataReaderQos pr=keep_all(rq);pr.durability().kind=fd::TRANSIENT_LOCAL_DURABILITY_QOS;
                auto* rr=sub2->create_datareader(prx,pr,&s.signals);need(rr);
                std::uint32_t nextp=1;bool order=true;
                const bool pok=waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(rr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;
                    if(x.run!=local.run+22||x.side!=peer||x.ordinal!=nextp)order=false;else ++nextp;}return nextp==6||!order;},10000)&&order&&nextp==6;
                report("transient_durability_sqlite_persistence",pok,"received="+std::to_string(nextp-1)+" in_order="+std::to_string(order)+" writer_recreated=1");
                (void)w2b->wait_for_acknowledgments(dur(3));
            }
            barrier();
            {   // 1b. Four Participants on one Runtime (descriptor max_participants=4): the main one,
                //     the second one above, and two more, each with its own topic and 3 samples.
                std::array<fd::DomainParticipant*,2> px{};std::array<fd::DataWriter*,2> wx{};std::array<fd::DataReader*,2> rx_{};
                for(unsigned k=0;k<2;++k){fd::DomainParticipantQos qx=qos;qx.name((std::string(qos.name().c_str())+"_p"+std::to_string(3+k)).c_str());
                    auto dx=s.runtime->MakeStockDescriptor(config);need(bool(dx));s.retained_descriptors.push_back(dx);qx.transport().user_transports={dx};
                    px[k]=s.factory->create_participant(q.domain,qx);extra.ps.push_back(px[k]);need(px[k],NS_STATE);need(s.type.register_type(px[k])==RC::RETCODE_OK);
                    auto* pubx=px[k]->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* subx=px[k]->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pubx&&subx);
                    fd::Topic *tx,*rx;topic2(px[k],"p"+std::to_string(3+k),s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
                    wx[k]=pubx->create_datawriter(tx,keep_all(wq),&s.signals);rx_[k]=subx->create_datareader(rx,keep_all(rq),&s.signals);need(wx[k]&&rx_[k]);}
                const bool m=matched(wx[0],rx_[0],1,1,15000)&&matched(wx[1],rx_[1],1,1,15000);barrier();
                for(unsigned k=0;k<2;++k)for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+35+k,n);need(wx[k]->write(&v));}
                std::array<std::uint32_t,2> nx{1,1};
                const bool got=m&&waitp([&]{for(unsigned k=0;k<2;++k){for(;;){LayerSample x;fd::SampleInfo i;if(rx_[k]->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                    if(i.valid_data&&x.run==local.run+35+k&&x.side==peer&&x.ordinal==nx[k])++nx[k];}}return nx[0]==4&&nx[1]==4;},8000);
                const bool acked=got&&wx[0]->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK&&wx[1]->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;
                report("four_participants_one_runtime",got&&acked,"participants=4 matched="+std::to_string(m)+" received="+std::to_string(nx[0]-1)+","+std::to_string(nx[1]-1)+" acked="+std::to_string(acked));
                barrier();
                for(unsigned k=2;k-->0;){need(px[k]->delete_contained_entities()==RC::RETCODE_OK&&s.factory->delete_participant(px[k])==RC::RETCODE_OK);
                    for(auto& e:extra.ps)if(e==px[k])e=nullptr;}
                barrier();
            }
            // Release the second Participant before the cases that use the main one.
            need(p2->delete_contained_entities()==RC::RETCODE_OK&&s.factory->delete_participant(p2)==RC::RETCODE_OK);for(auto& e:extra.ps)if(e==p2)e=nullptr;
        }
        barrier();
        {   // 4. ContentFilteredTopic (DDS-SQL filter with parameters) on a DynamicType that carries a TypeObject.
            auto* f=ft::DynamicTypeBuilderFactory::get_instance();auto* sb=f->create_struct_builder();need(sb);
            need(sb->add_member(0,"side",f->create_uint32_type())==RC::RETCODE_OK&&sb->add_member(1,"n",f->create_uint32_type())==RC::RETCODE_OK);
            need(sb->set_name("FastDDSDSoftBusCft214")==RC::RETCODE_OK);ft::DynamicType_ptr dt=sb->build();need(bool(dt));
            fd::TypeSupport ct(new ft::DynamicPubSubType(dt));need(ct.register_type(s.participant)==RC::RETCODE_OK);
            fd::Topic *tx,*rx;topic2(s.participant,"cft",ct.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            auto* cw=pub->create_datawriter(tx,keep_all(wq),&s.signals);need(cw);barrier();
            // Fast DDS 2.14.5's DDS-SQL filter factory pools expressions without a lock, and remote
            // reader filters are created/updated/deleted on the receive thread. Local filter changes
            // are therefore staggered between the boards so the two never overlap in one process.
            auto staged=[&](auto fn){for(unsigned turn=0;turn<2;++turn){if(turn==me)fn();std::this_thread::sleep_for(std::chrono::milliseconds(1000));barrier();}};
            fd::ContentFilteredTopic* cft=nullptr;fd::DataReader* cr=nullptr;
            staged([&]{cft=s.participant->create_contentfilteredtopic(tname("cft_view",peer),rx,"n > %0 AND n < %1",{"3","8"});need(cft,NS_STATE);
                cr=sub->create_datareader(cft,keep_all(rq),&s.signals);need(cr);});
            struct DD{ft::DynamicData* d;~DD(){if(d)ft::DynamicDataFactory::get_instance()->delete_data(d);}};
            DD wd{ft::DynamicDataFactory::get_instance()->create_data(dt)},rd{ft::DynamicDataFactory::get_instance()->create_data(dt)};need(wd.d&&rd.d);
            const auto side_id=wd.d->get_member_id_by_name("side"),n_id=wd.d->get_member_id_by_name("n");
            auto send=[&](std::uint32_t a,std::uint32_t b){for(std::uint32_t n=a;n<=b;++n){need(wd.d->set_uint32_value(me,side_id)==RC::RETCODE_OK&&wd.d->set_uint32_value(n,n_id)==RC::RETCODE_OK);need(cw->write(wd.d));}};
            std::vector<std::uint32_t> got;bool foreign=false;
            auto drain=[&]{for(;;){fd::SampleInfo i;if(cr->take_next_sample(rd.d,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;std::uint32_t sd=0,n=0;
                need(rd.d->get_uint32_value(sd,side_id)==RC::RETCODE_OK&&rd.d->get_uint32_value(n,n_id)==RC::RETCODE_OK);if(sd!=peer)foreign=true;else got.push_back(n);}};
            const bool m=matched(cw,cr,1,1,10000);barrier();send(1,10);
            (void)waitp([&]{drain();return got.size()>=4;},5000);(void)cw->wait_for_acknowledgments(dur(3));barrier();drain();
            const auto first=got;got.clear();
            staged([&]{need(cft->set_expression_parameters({"10","13"})==RC::RETCODE_OK);});
            send(11,15);(void)waitp([&]{drain();return got.size()>=2;},5000);(void)cw->wait_for_acknowledgments(dur(3));barrier();drain();
            const bool ok=m&&!foreign&&first==std::vector<std::uint32_t>{4,5,6,7}&&got==std::vector<std::uint32_t>{11,12};
            auto list=[](const std::vector<std::uint32_t>& v){std::string r;for(auto x:v)r+=(r.empty()?"":",")+std::to_string(x);return r.empty()?std::string("-"):r;};
            report("content_filtered_topic_sql",ok,"filter='n > %0 AND n < %1' params=3,8 got="+list(first)+" params=10,13 got="+list(got)+" type_object=dynamic");
            staged([&]{need(sub->delete_datareader(cr)==RC::RETCODE_OK&&s.participant->delete_contentfilteredtopic(cft)==RC::RETCODE_OK);});
        }
        barrier();
        {   // 5. BEST_EFFORT + KEEP_LAST: 50 samples at 20 ms; in order, no duplicates.
            fd::Topic *tx,*rx;topic2(s.participant,"besteffort",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataWriterQos w=wq;w.reliability().kind=fd::BEST_EFFORT_RELIABILITY_QOS;w.history().depth=64;
            fd::DataReaderQos r=rq;r.reliability().kind=fd::BEST_EFFORT_RELIABILITY_QOS;r.history().depth=64;
            auto* bw=pub->create_datawriter(tx,w,&s.signals);auto* br=sub->create_datareader(rx,r,&s.signals);need(bw&&br);
            const bool m=matched(bw,br,1,1,10000);barrier();
            std::uint32_t last=0,received=0;bool order=true;
            auto drain=[&]{for(;;){LayerSample x;fd::SampleInfo i;if(br->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+23||x.side!=peer)continue;
                if(x.ordinal<=last)order=false;last=x.ordinal;++received;}};
            for(std::uint32_t n=1;n<=50;++n){auto v=layer(local.run+23,n);need(bw->write(&v));drain();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
            (void)waitp([&]{drain();return last==50;},3000);
            report("best_effort_keep_last",m&&order&&received>=45,"received="+std::to_string(received)+"/50 in_order_no_dup="+std::to_string(order));
        }
        barrier();
        {   // 6. Reader KEEP_LAST depth 1: of 10 acknowledged samples only the newest is kept.
            fd::Topic *tx,*rx;topic2(s.participant,"depth1",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataWriterQos w=wq;w.history().depth=16;fd::DataReaderQos r=rq;r.history().depth=1;
            auto* dw=pub->create_datawriter(tx,w,&s.signals);auto* dr=sub->create_datareader(rx,r,&s.signals);need(dw&&dr);
            const bool m=matched(dw,dr,1,1,10000);barrier();
            for(std::uint32_t n=1;n<=10;++n){auto v=layer(local.run+24,n);need(dw->write(&v));}
            const bool acked=dw->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;barrier();
            unsigned count=0;std::uint32_t lastn=0;for(;;){LayerSample x;fd::SampleInfo i;if(dr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(i.valid_data&&x.run==local.run+24&&x.side==peer){++count;lastn=x.ordinal;}}
            report("keep_last_depth1_reader",m&&acked&&count==1&&lastn==10,"kept="+std::to_string(count)+" newest="+std::to_string(lastn)+" acked="+std::to_string(acked));
        }
        barrier();
        {   // 7. Reader resource limits (max_samples 5) with KEEP_ALL RELIABLE: back-pressure, then all 20 in order.
            fd::Topic *tx,*rx;topic2(s.participant,"limits",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataReaderQos r=keep_all(rq);r.resource_limits().max_samples=5;r.resource_limits().max_instances=1;r.resource_limits().max_samples_per_instance=5;r.resource_limits().allocated_samples=5;
            fd::DataWriterQos lwq=keep_all(wq);lwq.reliable_writer_qos().times.heartbeatPeriod=eprosima::fastrtps::Duration_t(0,100000000);
            auto* lw=pub->create_datawriter(tx,lwq,&s.signals);auto* lr=sub->create_datareader(rx,r,&s.signals);need(lw&&lr);
            const bool m=matched(lw,lr,1,1,10000);barrier();
            for(std::uint32_t n=1;n<=20;++n){auto v=layer(local.run+25,n);need(lw->write(&v));}
            std::this_thread::sleep_for(std::chrono::milliseconds(800));const auto peak=lr->get_unread_count();
            std::uint32_t next=1;bool order=true;
            const bool got=waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(lr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;
                if(x.run!=local.run+25||x.side!=peer||x.ordinal!=next)order=false;else ++next;}return next==21||!order;},10000);
            const bool acked=lw->wait_for_acknowledgments(dur(15))==RC::RETCODE_OK;
            report("reader_resource_limits_backpressure",m&&got&&order&&peak==5&&acked,"buffered_before_take="+std::to_string(peak)+" max_samples=5 received="+std::to_string(next-1)+" in_order="+std::to_string(order)+" acked="+std::to_string(acked));
        }
        barrier();
        {   // 8. Request/reply correlation through related_sample_identity.
            fd::Topic *qtx,*qrx,*ptx,*prx;topic2(s.participant,"request",s.type.get_type_name(),qtx,qrx,fd::TOPIC_QOS_DEFAULT);topic2(s.participant,"reply",s.type.get_type_name(),ptx,prx,fd::TOPIC_QOS_DEFAULT);
            auto* qw=pub->create_datawriter(qtx,keep_all(wq),&s.signals);auto* qr=sub->create_datareader(qrx,keep_all(rq),&s.signals);
            auto* pw=pub->create_datawriter(ptx,keep_all(wq),&s.signals);auto* pr=sub->create_datareader(prx,keep_all(rq),&s.signals);need(qw&&qr&&pw&&pr);
            const bool m=matched(qw,qr,1,1,10000)&&matched(pw,pr,1,1,10000);barrier();
            std::array<eprosima::fastrtps::rtps::SampleIdentity,3> ids{};
            for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+26,n);eprosima::fastrtps::rtps::WriteParams p;need(qw->write(&v,p));ids[n-1]=p.sample_identity();}
            unsigned served=0,correlated=0,wrong=0;
            const bool ok=m&&waitp([&]{
                for(;;){LayerSample x;fd::SampleInfo i;if(qr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+26||x.side!=peer)continue;
                    auto v=layer(local.run+27,x.ordinal+100);eprosima::fastrtps::rtps::WriteParams p;p.related_sample_identity(i.sample_identity);need(pw->write(&v,p));++served;}
                for(;;){LayerSample x;fd::SampleInfo i;if(pr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data||x.run!=local.run+27||x.side!=peer)continue;
                    if(x.ordinal>=101&&x.ordinal<=103&&i.related_sample_identity==ids[x.ordinal-101])++correlated;else ++wrong;}
                return served==3&&correlated==3;},10000);
            (void)pw->wait_for_acknowledgments(dur(3));
            report("request_reply_related_sample_identity",ok&&wrong==0,"served="+std::to_string(served)+" correlated_replies="+std::to_string(correlated)+" mismatched="+std::to_string(wrong));
        }
        barrier();
        {   // 9. WaitSet with a StatusCondition (DATA_AVAILABLE) and a ReadCondition (NOT_READ); read() then sample states.
            fd::Topic *tx,*rx;topic2(s.participant,"waitset",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            auto* ww=pub->create_datawriter(tx,wq,nullptr);auto* wr=sub->create_datareader(rx,rq,nullptr);need(ww&&wr);
            fd::StatusCondition& sc=wr->get_statuscondition();need(sc.set_enabled_statuses(fd::StatusMask::data_available())==RC::RETCODE_OK);
            fd::ReadCondition* rc=wr->create_readcondition(fd::NOT_READ_SAMPLE_STATE,fd::ANY_VIEW_STATE,fd::ANY_INSTANCE_STATE);need(rc);
            fd::WaitSet ws;need(ws.attach_condition(sc)==RC::RETCODE_OK&&ws.attach_condition(*rc)==RC::RETCODE_OK);
            const bool m=matched(ww,wr,1,1,10000);barrier();
            {auto v=layer(local.run+28,1);need(ww->write(&v));}
            fd::ConditionSeq active;const bool woke=ws.wait(active,dur(5))==RC::RETCODE_OK&&!active.empty();
            bool rc_seen=false;for(auto* c:active)if(c==rc)rc_seen=true;
            if(!rc_seen)rc_seen=waitp([&]{return rc->get_trigger_value();},2000);
            LayerSample x;fd::SampleInfo i;const bool read=wr->read_next_sample(&x,&i)==RC::RETCODE_OK&&i.valid_data&&x.run==local.run+28&&x.side==peer;
            const bool states=read&&i.sample_state==fd::NOT_READ_SAMPLE_STATE&&i.view_state==fd::NEW_VIEW_STATE&&i.instance_state==fd::ALIVE_INSTANCE_STATE;
            const bool cleared=!rc->get_trigger_value()&&wr->get_unread_count()==0;
            need(ws.detach_condition(*rc)==RC::RETCODE_OK&&ws.detach_condition(sc)==RC::RETCODE_OK&&wr->delete_readcondition(rc)==RC::RETCODE_OK);
            (void)ww->wait_for_acknowledgments(dur(3));
            report("waitset_status_and_read_condition",m&&woke&&rc_seen&&states&&cleared,"woke="+std::to_string(woke)+" read_condition="+std::to_string(rc_seen)+" states_not_read_new_alive="+std::to_string(states)+" cleared_after_read="+std::to_string(cleared));
        }
        barrier();
        {   // 10. RELIABLE with positive ACKs disabled.
            fd::Topic *tx,*rx;topic2(s.participant,"noacks",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataWriterQos w=keep_all(wq);w.reliable_writer_qos().disable_positive_acks.enabled=true;w.reliable_writer_qos().disable_positive_acks.duration=dur(1);
            fd::DataReaderQos r=keep_all(rq);r.reliable_reader_qos().disable_positive_ACKs.enabled=true;
            auto* nw=pub->create_datawriter(tx,w,&s.signals);auto* nr=sub->create_datareader(rx,r,&s.signals);need(nw&&nr);
            const bool m=matched(nw,nr,1,1,10000);barrier();
            for(std::uint32_t n=1;n<=20;++n){auto v=layer(local.run+29,n);need(nw->write(&v));} // unpaced burst
            std::uint32_t next=1;bool order=true;
            const bool got=waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(nr->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;
                if(x.run!=local.run+29||x.side!=peer||x.ordinal!=next)order=false;else ++next;}return next==21||!order;},8000);
            report("reliable_disable_positive_acks",m&&got&&order,"received="+std::to_string(next-1)+" in_order="+std::to_string(order)+" unpaced=1");
            barrier();
        }
        barrier();
        {   // 11. 1 MiB sample (~17 transport messages of <= 64 KiB) per direction, byte-verified and acknowledged.
            fd::TypeSupport lt{new stage1_large::Type};need(lt.register_type(s.participant)==RC::RETCODE_OK);
            fd::Topic *tx,*rx;topic2(s.participant,"large",lt.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataWriterQos w=wq;w.history().depth=1;fd::DataReaderQos r=rq;r.history().depth=1;
            auto* lw=pub->create_datawriter(tx,w,&s.signals);auto* lr=sub->create_datareader(rx,r,&s.signals);need(lw&&lr);
            const bool m=matched(lw,lr,1,1,10000);barrier();
            const auto t0=Clock::now();{auto v=stage1_large::make(local.run+30,me);need(lw->write(&v));}
            bool got=false;const bool rx_ok=m&&waitp([&]{for(;;){stage1_large::Sample v;fd::SampleInfo i;if(lr->take_next_sample(&v,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&stage1_large::valid(v,local.run+30,peer))got=true;}return got;},25000);
            const bool acked=lw->wait_for_acknowledgments(dur(25))==RC::RETCODE_OK;
            const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-t0).count();
            report("large_sample_1MiB",rx_ok&&acked,"bytes="+std::to_string(stage1_large::kBytes)+" verified="+std::to_string(got)+" acked="+std::to_string(acked)+" ms="+std::to_string(ms));
        }
        barrier();
        {   // 12. Discovery and delivery across 20 topics at once.
            constexpr unsigned K=20;std::array<fd::DataWriter*,K> mw{};std::array<fd::DataReader*,K> mr{};
            for(unsigned k=0;k<K;++k){fd::Topic *tx,*rx;topic2(s.participant,"many"+std::to_string(k),s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
                mw[k]=pub->create_datawriter(tx,wq,&s.signals);mr[k]=sub->create_datareader(rx,rq,&s.signals);need(mw[k]&&mr[k]);}
            const bool m=waitp([&]{for(unsigned k=0;k<K;++k){fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus b;
                need(mw[k]->get_publication_matched_status(a)==RC::RETCODE_OK&&mr[k]->get_subscription_matched_status(b)==RC::RETCODE_OK);if(a.current_count!=1||b.current_count!=1)return false;}return true;},20000);
            barrier();
            for(unsigned k=0;k<K;++k){auto v=layer(local.run+31,k+1);need(mw[k]->write(&v));}
            std::array<bool,K> got{};unsigned total=0;
            (void)waitp([&]{for(unsigned k=0;k<K;++k){for(;;){LayerSample x;fd::SampleInfo i;if(mr[k]->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+31&&x.side==peer&&x.ordinal==k+1&&!got[k]){got[k]=true;++total;}}}return total==K;},10000);
            report("twenty_topics_discovery_and_delivery",m&&total==K,"matched_all="+std::to_string(m)+" delivered="+std::to_string(total)+"/20");
        }
        barrier();
        {   // 12b. DataWriter loans: loan_sample() on a plain type, fill in place, write the loan.
            //      The reader takes with a LoanableSequence and returns the loan.
            fd::TypeSupport pt{new stage1_plain::Type};need(pt.register_type(s.participant)==RC::RETCODE_OK);
            fd::Topic *tx,*rx;topic2(s.participant,"loans",pt.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            // Data sharing off: a plain type would select it automatically (AUTO), and this case
            // tests loans over the network transport.
            fd::DataWriterQos lwq=keep_all(wq);lwq.data_sharing().off();fd::DataReaderQos lrq=keep_all(rq);lrq.data_sharing().off();
            auto* lw=pub->create_datawriter(tx,lwq,&s.signals);auto* lr=sub->create_datareader(rx,lrq,&s.signals);need(lw&&lr);
            const bool m=matched(lw,lr,1,1,10000);barrier();
            bool loaned=true;
            for(std::uint32_t n=1;n<=5;++n){void* raw=nullptr;if(lw->loan_sample(raw)!=RC::RETCODE_OK||!raw){loaned=false;break;}
                stage1_plain::fill(*static_cast<stage1_plain::Sample*>(raw),local.run+37,me,n);need(lw->write(raw));}
            std::uint32_t next=1;bool valid=true,returned=true;
            const bool got=m&&loaned&&waitp([&]{fd::LoanableSequence<stage1_plain::Sample> data;fd::SampleInfoSeq infos;
                if(lr->take(data,infos,4)==RC::RETCODE_OK){for(fd::LoanableCollection::size_type k=0;k<infos.length();++k){if(!infos[k].valid_data)continue;
                    if(stage1_plain::valid(data[k],local.run+37,peer,next))++next;else valid=false;}
                    returned=lr->return_loan(data,infos)==RC::RETCODE_OK&&returned;}
                return next==6||!valid;},8000);
            const bool acked=got&&lw->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;
            report("datawriter_loans_plain_type",got&&valid&&returned&&acked,"data_sharing=OFF loaned_writes="+std::to_string(loaned?5:0)+" received_verified="+std::to_string(next-1)+" reader_loans_returned="+std::to_string(returned)+" acked="+std::to_string(acked));
        }
        barrier();
        {   // 12c. Static endpoint discovery (STATIC EDP from XML): SPDP finds the peer Participant,
            //      endpoints match from the XML description only, then 3 samples each way.
            const auto pname=[&](unsigned side){return std::string(qos.name().c_str()).substr(0,std::string(qos.name().c_str()).size()-1)+std::to_string(side)+"_sedp";};
            const std::string type=s.type.get_type_name();
            const auto ep=[&](const char* kind,unsigned id,const std::string& topic){return std::string("<")+kind+"><userId>"+std::to_string(id)+"</userId><entityID>"+std::to_string(id)+
                "</entityID><topicName>"+topic+"</topicName><topicDataType>"+type+"</topicDataType><topicKind>NO_KEY</topicKind>"
                "<reliabilityQos>RELIABLE_RELIABILITY_QOS</reliabilityQos><durabilityQos>VOLATILE_DURABILITY_QOS</durabilityQos></"+kind+">";};
            const std::string xml="data://<?xml version=\"1.0\" encoding=\"utf-8\"?><staticdiscovery><participant><name>"+pname(peer)+"</name>"+
                ep("writer",110+peer,tname("sedp",peer))+ep("reader",120+peer,tname("sedp",me))+"</participant></staticdiscovery>";
            fd::DomainParticipantQos q5=qos;q5.name(pname(me).c_str());
            auto d5=s.runtime->MakeStockDescriptor(config);need(bool(d5));s.retained_descriptors.push_back(d5);q5.transport().user_transports={d5};
            auto& dc5=q5.wire_protocol().builtin.discovery_config;dc5.use_SIMPLE_EndpointDiscoveryProtocol=false;dc5.use_STATIC_EndpointDiscoveryProtocol=true;dc5.static_edp_xml_config(xml.c_str());
            auto* p5=s.factory->create_participant(q.domain,q5);extra.ps.push_back(p5);need(p5,NS_STATE);need(s.type.register_type(p5)==RC::RETCODE_OK);
            auto* pub5=p5->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub5=p5->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub5&&sub5);
            fd::Topic *tx,*rx;topic2(p5,"sedp",type,tx,rx,fd::TOPIC_QOS_DEFAULT);
            fd::DataWriterQos w5=keep_all(wq);w5.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;w5.durability().kind=fd::VOLATILE_DURABILITY_QOS;w5.endpoint().user_defined_id=static_cast<int16_t>(110+me);w5.endpoint().entity_id=static_cast<int16_t>(110+me);
            fd::DataReaderQos r5=keep_all(rq);r5.reliability().kind=fd::RELIABLE_RELIABILITY_QOS;r5.durability().kind=fd::VOLATILE_DURABILITY_QOS;r5.endpoint().user_defined_id=static_cast<int16_t>(120+me);r5.endpoint().entity_id=static_cast<int16_t>(120+me);
            auto* sw5=pub5->create_datawriter(tx,w5,&s.signals);auto* sr5=sub5->create_datareader(rx,r5,&s.signals);need(sw5&&sr5);
            const bool m=matched(sw5,sr5,1,1,20000);barrier();
            for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+38,n);need(sw5->write(&v));}
            std::uint32_t next=1;const bool got=m&&waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(sr5->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+38&&x.side==peer&&x.ordinal==next)++next;}return next==4;},8000);
            const bool acked=got&&sw5->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;
            report("static_endpoint_discovery_xml",got&&acked,"edp=STATIC matched="+std::to_string(m)+" received="+std::to_string(next-1)+" acked="+std::to_string(acked));
            barrier();
            need(p5->delete_contained_entities()==RC::RETCODE_OK&&s.factory->delete_participant(p5)==RC::RETCODE_OK);for(auto& e:extra.ps)if(e==p5)e=nullptr;
        }
        barrier();
        {   // 12d. TypeLookup service: each side publishes a DynamicType the peer never registers.
            //      The peer learns it from discovery (TypeInformation), fetches it through the
            //      TypeLookup service (register_remote_type), builds a reader from it and reads.
            struct TLSpy final:fd::DomainParticipantListener{NS_RunHandle& h;std::string want;std::mutex m;std::optional<eprosima::fastrtps::types::TypeInformation> info;std::string type_name;
                ft::DynamicType_ptr remote;std::string remote_name;explicit TLSpy(NS_RunHandle& x):h(x){}
                void on_type_information_received(fd::DomainParticipant*,const eprosima::fastrtps::string_255 topic,const eprosima::fastrtps::string_255 type,const eprosima::fastrtps::types::TypeInformation& ti) override{
                    {std::lock_guard<std::mutex> l(m);if(topic.to_string()!=want||info)return;info=ti;type_name=type.to_string();}h.progress.notify();}} tl(h);
            tl.want=tname("tl",peer);
            fd::DomainParticipantQos q7=qos;q7.name((std::string(qos.name().c_str())+"_tl").c_str());
            auto d7=s.runtime->MakeStockDescriptor(config);need(bool(d7));s.retained_descriptors.push_back(d7);q7.transport().user_transports={d7};
            q7.wire_protocol().builtin.typelookup_config.use_client=true;q7.wire_protocol().builtin.typelookup_config.use_server=true;
            auto* p7=s.factory->create_participant(q.domain,q7,&tl,fd::StatusMask::none());extra.ps.push_back(p7);need(p7,NS_STATE);
            auto* f=ft::DynamicTypeBuilderFactory::get_instance();auto* sb=f->create_struct_builder();need(sb);
            need(sb->add_member(0,"side",f->create_uint32_type())==RC::RETCODE_OK&&sb->add_member(1,"n",f->create_uint32_type())==RC::RETCODE_OK&&sb->add_member(2,"text",f->create_string_type(64))==RC::RETCODE_OK);
            need(sb->set_name(("FastDDSDSoftBusTypeLookup214_"+std::to_string(me)).c_str())==RC::RETCODE_OK);ft::DynamicType_ptr dt=sb->build();need(bool(dt));
            fd::TypeSupport ts(new ft::DynamicPubSubType(dt));ts->auto_fill_type_information(true);ts->auto_fill_type_object(false);need(ts.register_type(p7)==RC::RETCODE_OK);
            auto* pub7=p7->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub7=p7->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub7&&sub7);
            auto* t7=p7->create_topic(tname("tl",me),ts.get_type_name(),fd::TOPIC_QOS_DEFAULT);need(t7);
            auto* w7=pub7->create_datawriter(t7,keep_all(wq),&s.signals);need(w7);
            const bool info=waitp([&]{std::lock_guard<std::mutex> l(tl.m);return bool(tl.info);},20000);
            // register_remote_type takes the callback by lvalue reference; it is called from a
            // TypeLookup reply thread, so it only stores the type for this thread.
            std::function<void(const std::string&,const ft::DynamicType_ptr)> on_remote=[&tl](const std::string& name,const ft::DynamicType_ptr type){
                {std::lock_guard<std::mutex> l2(tl.m);tl.remote=type;tl.remote_name=name;}tl.h.progress.notify();};
            // Fast DDS 2.14: RETCODE_OK means the type was already resolvable here and was registered
            // without the callback; RETCODE_NO_DATA means TypeLookup requests are pending and the
            // callback brings the type. The publisher sends no TypeObject in discovery
            // (auto_fill_type_object(false)), so on this board a resolvable TypeObject can only have
            // come from the peer through the TypeLookup service.
            std::string path="none";bool requested=false;
            if(info){eprosima::fastrtps::types::TypeInformation ti;std::string tn;{std::lock_guard<std::mutex> l(tl.m);ti=*tl.info;tn=tl.type_name;}
                const bool object_before=ft::TypeObjectFactory::get_instance()->get_type_object(&ti.complete().typeid_with_size().type_id())!=nullptr;
                const auto rc=p7->register_remote_type(ti,tn,on_remote);
                if(rc==RC::RETCODE_OK){fd::TypeSupport found=p7->find_type(tn);auto* dps=found?dynamic_cast<ft::DynamicPubSubType*>(found.get()):nullptr;
                    if(dps){std::lock_guard<std::mutex> l(tl.m);tl.remote=dps->GetDynamicType();tl.remote_name=tn;}
                    path=object_before?"typelookup_before_register":"resolved_at_register";requested=dps!=nullptr;}
                else if(rc==RC::RETCODE_NO_DATA){path="typelookup_reply_callback";requested=true;}}
            const bool resolved=requested&&waitp([&]{std::lock_guard<std::mutex> l(tl.m);return bool(tl.remote);},20000);
            fd::DataReader* r7=nullptr;ft::DynamicType_ptr rt7;std::string rname;
            if(resolved){{std::lock_guard<std::mutex> l(tl.m);rt7=tl.remote;rname=tl.remote_name;}
                fd::TypeSupport rts(new ft::DynamicPubSubType(rt7));rts->auto_fill_type_information(false);rts->auto_fill_type_object(false);
                if(!p7->find_type(rname))need(rts.register_type(p7,rname)==RC::RETCODE_OK);auto* rt7t=p7->create_topic(tname("tl",peer),rname,fd::TOPIC_QOS_DEFAULT);need(rt7t);
                r7=sub7->create_datareader(rt7t,keep_all(rq),&s.signals);need(r7);}
            const bool m=resolved&&waitp([&]{fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus b;need(w7->get_publication_matched_status(a)==RC::RETCODE_OK&&r7->get_subscription_matched_status(b)==RC::RETCODE_OK);
                return a.current_count==1&&b.current_count==1;},20000);
            barrier();
            struct DD{ft::DynamicData* d;~DD(){if(d)ft::DynamicDataFactory::get_instance()->delete_data(d);}};
            DD wd{ft::DynamicDataFactory::get_instance()->create_data(dt)};need(wd.d);
            for(std::uint32_t n=1;n<=3;++n){need(wd.d->set_uint32_value(me,0)==RC::RETCODE_OK&&wd.d->set_uint32_value(n,1)==RC::RETCODE_OK&&wd.d->set_string_value("tl-"+std::to_string(me)+"-"+std::to_string(n),2)==RC::RETCODE_OK);need(w7->write(wd.d));}
            std::uint32_t next=1;bool texts=true;
            if(m){DD rd{ft::DynamicDataFactory::get_instance()->create_data(rt7)};need(rd.d);
                (void)waitp([&]{for(;;){fd::SampleInfo i;if(r7->take_next_sample(rd.d,&i)!=RC::RETCODE_OK)break;if(!i.valid_data)continue;std::uint32_t sd=0,n=0;std::string t;
                    need(rd.d->get_uint32_value(sd,0)==RC::RETCODE_OK&&rd.d->get_uint32_value(n,1)==RC::RETCODE_OK&&rd.d->get_string_value(t,2)==RC::RETCODE_OK);
                    if(sd==peer&&n==next){if(t!="tl-"+std::to_string(peer)+"-"+std::to_string(n))texts=false;++next;}}return next==4;},8000);}
            const bool acked=w7->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;
            report("type_lookup_remote_type",info&&resolved&&m&&next==4&&texts&&acked,"type_information="+std::to_string(info)+" remote_type_resolved="+std::to_string(resolved)+" path="+path+" type_object_in_discovery=0"+
                " remote_type="+rname+" matched="+std::to_string(m)+" received="+std::to_string(next-1)+" fields_ok="+std::to_string(texts)+" acked="+std::to_string(acked));
            barrier();
            need(p7->delete_contained_entities()==RC::RETCODE_OK&&s.factory->delete_participant(p7)==RC::RETCODE_OK);for(auto& e:extra.ps)if(e==p7)e=nullptr;
        }
        barrier();
        {   // 13. A second DDS domain (domain+1) concurrently with the main domain in the same process.
            //     DSoftBus service names encode the domain, so each domain uses its own Runtime.
            NS_Request q1=q;q1.domain=q.domain+1;auto config1=configuration(q1);
            struct Second{fd::DomainParticipantFactory* f=nullptr;std::unique_ptr<ds::experimental::Runtime> rt;std::thread pump;std::atomic<bool> failed{false},go{false},gated{false};
                fd::DomainParticipant* p=nullptr;std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> desc;bool closed=false,torn=false;
                bool teardown(){if(torn)return closed;torn=true;bool ok=true;
                    if(p){try{if(p->delete_contained_entities()!=RC::RETCODE_OK||f->delete_participant(p)!=RC::RETCODE_OK)ok=false;else p=nullptr;}catch(...){ok=false;}}
                    go=true;if(rt)rt->RequestPumpStop();if(pump.joinable())pump.join();
                    if(ok&&rt){desc.reset();try{ok=rt->Close();}catch(...){ok=false;}}closed=ok;return ok;}
                // On a failed teardown the Runtime is leaked, never destroyed while still in use.
                ~Second(){if(!teardown()&&rt)(void)rt.release();}} d1;d1.f=s.factory;
            d1.rt=std::make_unique<ds::experimental::Runtime>(backend(config1,q1),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
            // Dial only after the peer's second runtime exists (barrier below).
            need(d1.rt->SetPreConnectGate([&d1,&h]{d1.gated=true;const auto end=Clock::now()+std::chrono::seconds(30);
                while(!d1.go.load()){if(Clock::now()>=end||!h.live())return false;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return true;}));
            d1.desc=d1.rt->MakeStockDescriptor(config1);need(bool(d1.desc));
            d1.pump=std::thread([&d1]{try{for(;;){auto g=d1.rt->WorkGeneration();
                for(;;){auto r=d1.rt->PumpSend();if(r.outcome==ds::NativeSendOutcome::ReturnedInvalidCode||r.outcome==ds::NativeSendOutcome::ReturnedByException||(r.actual_return_code&&*r.actual_return_code!=0))d1.failed=true;
                    if(r.status!=ds::NativeStatus::None)break;}
                ds::phase0::BackendStatus rx;do{rx=d1.rt->PumpReceive();}while(rx==ds::phase0::BackendStatus::None);
                if(rx!=ds::phase0::BackendStatus::NoWork&&rx!=ds::phase0::BackendStatus::InvalidState)d1.failed=true;
                auto w=d1.rt->WaitForWork(g);if(w!=ds::WorkWaitResult::Work&&w!=ds::WorkWaitResult::Deadline){if(w!=ds::WorkWaitResult::StopRequested&&w!=ds::WorkWaitResult::Closed)d1.failed=true;return;}}}catch(...){d1.failed=true;}});
            fd::DomainParticipantQos q3=qos;q3.name((std::string(qos.name().c_str())+"_d1").c_str());
            q3.transport().user_transports={d1.desc};q3.transport().max_msg_size_no_frag=config1.maxMessageSize;
            q3.wire_protocol().builtin.metatrafficUnicastLocatorList.clear();q3.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(config1.default_user_unicast.at(0));
            q3.wire_protocol().builtin.initialPeersList.clear();q3.wire_protocol().builtin.initialPeersList.push_back(config1.directory.routes.at(0).key.remote_locator);
            q3.wire_protocol().default_unicast_locator_list.clear();q3.wire_protocol().default_unicast_locator_list.push_back(config1.default_user_unicast.at(0));
            // Both second runtimes have published their listener (gate entered) before either dials.
            const bool gated=waitp([&]{return d1.gated.load();},10000);barrier();d1.go=true;
            d1.p=s.factory->create_participant(q1.domain,q3);
            bool ok=false;std::string detail="listener_gate="+std::to_string(gated)+" participant_created="+std::to_string(d1.p!=nullptr)+" domain="+std::to_string(q1.domain);
            barrier();
            if(d1.p){need(s.type.register_type(d1.p)==RC::RETCODE_OK);auto* pub3=d1.p->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub3=d1.p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub3&&sub3);
                fd::Topic *tx,*rx;topic2(d1.p,"domain1",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
                auto* w3=pub3->create_datawriter(tx,keep_all(wq),&s.signals);auto* r3=sub3->create_datareader(rx,keep_all(rq),&s.signals);need(w3&&r3);
                const bool m=matched(w3,r3,1,1,20000);barrier();
                for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+32,n);need(w3->write(&v));}
                std::uint32_t next=1;const bool got=m&&waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(r3->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                    if(i.valid_data&&x.run==local.run+32&&x.side==peer&&x.ordinal==next)++next;}return next==4;},8000);
                const bool acked=w3->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;barrier();
                const bool clean=d1.teardown();
                ok=got&&acked&&clean&&!d1.failed.load();detail+=" matched="+std::to_string(m)+" received="+std::to_string(next-1)+" acked="+std::to_string(acked)+" second_runtime_closed="+std::to_string(clean)+" pump_failed="+std::to_string(d1.failed.load());
            }else{barrier();barrier();}
            report("second_domain_concurrent",ok,detail);
        }
        barrier();
        {   // 13b. Peer process restart: board B runs the peer in a separate OS process (helper),
            //      kills it (SIGKILL, no teardown) and starts a fresh process; board A's process
            //      and its Runtime stay live and must reconnect to the new peer process.
            //      Domain+2, so the names never collide with this process's other Runtimes.
            NS_Request q2=q;q2.domain=q.domain+2;const std::uint64_t base=local.run+50;
            const std::string tmpdir=std::getenv("TMPDIR");
            std::string detail;bool ok=false;
            if(me==0){
                auto config2=configuration(q2);
                struct Host{std::unique_ptr<ds::experimental::Runtime> rt;std::thread pump;std::atomic<bool> failed{false},go{false},entered{false};fd::DomainParticipantFactory* f=nullptr;fd::DomainParticipant* p=nullptr;
                    std::shared_ptr<eprosima::fastdds::rtps::TransportDescriptorInterface> desc;bool torn=false,closed=false;
                    bool teardown(){if(torn)return closed;torn=true;bool good=true;if(p){if(p->delete_contained_entities()!=RC::RETCODE_OK||f->delete_participant(p)!=RC::RETCODE_OK)good=false;else p=nullptr;}
                        go=true;if(rt)rt->RequestPumpStop();if(pump.joinable())pump.join();if(good&&rt){desc.reset();try{good=rt->Close();}catch(...){good=false;}}closed=good;if(!good)(void)rt.release();return good;}
                    ~Host(){(void)teardown();}} hs;hs.f=s.factory;
                hs.rt=std::make_unique<ds::experimental::Runtime>(backend(config2,q2),ds::experimental::RuntimeLifetime::PersistentNativeAnchor);
                // Participant creation blocks in this gate (its listener exists by then).
                need(hs.rt->SetPreConnectGate([&hs,&h]{hs.entered=true;const auto end=Clock::now()+std::chrono::seconds(40);
                    while(!hs.go.load()){if(Clock::now()>=end||!h.live())return false;std::this_thread::sleep_for(std::chrono::milliseconds(20));}return true;}));
                hs.desc=hs.rt->MakeStockDescriptor(config2);need(bool(hs.desc));
                hs.pump=std::thread([&hs]{try{for(;;){auto g=hs.rt->WorkGeneration();
                    for(;;){auto x=hs.rt->PumpSend();if(x.outcome==ds::NativeSendOutcome::ReturnedByException)hs.failed=true;if(x.status!=ds::NativeStatus::None)break;}
                    ds::phase0::BackendStatus rx;do{rx=hs.rt->PumpReceive();}while(rx==ds::phase0::BackendStatus::None);
                    auto w=hs.rt->WaitForWork(g);if(w!=ds::WorkWaitResult::Work&&w!=ds::WorkWaitResult::Deadline){if(w!=ds::WorkWaitResult::StopRequested&&w!=ds::WorkWaitResult::Closed)hs.failed=true;return;}}}catch(...){hs.failed=true;}});
                fd::DomainParticipantQos q8=qos;q8.name((std::string(qos.name().c_str())+"_restart").c_str());
                q8.transport().user_transports={hs.desc};q8.transport().max_msg_size_no_frag=config2.maxMessageSize;
                auto& b8=q8.wire_protocol().builtin;b8.metatrafficUnicastLocatorList.clear();b8.metatrafficUnicastLocatorList.push_back(config2.default_user_unicast.at(0));
                b8.initialPeersList.clear();b8.initialPeersList.push_back(config2.directory.routes.at(0).key.remote_locator);
                q8.wire_protocol().default_unicast_locator_list.clear();q8.wire_protocol().default_unicast_locator_list.push_back(config2.default_user_unicast.at(0));
                b8.discovery_config.leaseDuration=eprosima::fastrtps::Duration_t(3,0);b8.discovery_config.leaseDuration_announcementperiod=eprosima::fastrtps::Duration_t(1,0);
                // Created on its own thread: it blocks in the gate until both listeners exist.
                std::thread creator([&]{hs.p=s.factory->create_participant(q2.domain,q8);});
                struct Join{std::thread& t;~Join(){if(t.joinable())t.join();}} join_creator{creator};
                (void)waitp([&]{return hs.entered.load()||!creator.joinable();},20000);
                barrier();             // A's listener exists; its dial waits on the gate
                barrier();             // B's incarnation 1 listens
                hs.go=true;creator.join();
                need(hs.p,NS_STATE);need(s.type.register_type(hs.p)==RC::RETCODE_OK);
                auto* pub8=hs.p->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub8=hs.p->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub8&&sub8);
                fd::Topic *tx,*rx;topic2(hs.p,"restart",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
                auto* w8=pub8->create_datawriter(tx,keep_all(wq),&s.signals);auto* r8=sub8->create_datareader(rx,keep_all(rq),&s.signals);need(w8&&r8);
                auto counts=[&](int want){fd::PublicationMatchedStatus a;fd::SubscriptionMatchedStatus c;need(w8->get_publication_matched_status(a)==RC::RETCODE_OK&&r8->get_subscription_matched_status(c)==RC::RETCODE_OK);return a.current_count==want&&c.current_count==want;};
                auto exchange=[&](unsigned inc){if(!waitp([&]{return counts(1);},45000))return 0u;
                    for(std::uint32_t n=1;n<=3;++n){auto v=layer(base+inc,n);need(w8->write(&v));}
                    std::uint32_t next=1;(void)waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(r8->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                        if(i.valid_data&&x.run==base+inc&&x.side==peer&&x.ordinal==next&&x.payload==stage1_run::payload(x.run,peer,x.ordinal))++next;}return next==4;},20000);
                    (void)w8->wait_for_acknowledgments(dur(5));return next-1;};
                const unsigned got1=exchange(1);
                barrier();             // incarnation 1 exchanged; B kills it
                barrier();
                const bool gone=waitp([&]{return counts(0);},20000);   // lease 3 s
                barrier();             // B starts incarnation 2
                const unsigned got2=exchange(2);
                barrier();             // incarnation 2 exchanged
                barrier();             // incarnation 2 ended
                const bool pump_ok=!hs.failed.load();const bool closed=hs.teardown();
                ok=got1==3&&gone&&got2==3&&pump_ok&&closed;
                detail="role=LIVE_PROCESS inc1_received="+std::to_string(got1)+" unmatched_after_kill="+std::to_string(gone)+" inc2_received="+std::to_string(got2)+
                    " runtime_pump_ok="+std::to_string(pump_ok)+" runtime_closed="+std::to_string(closed);
            }else{
                auto spawn=[&](unsigned inc,const std::string& status)->pid_t{
                    RestartRequest r{};r.q=q;r.run=base+inc;r.side=me;r.peer=peer;r.incarnation=inc;r.domain=q2.domain;
                    std::snprintf(r.status,sizeof(r.status),"%s",status.c_str());std::snprintf(r.go,sizeof(r.go),"%s",(tmpdir+"/restart_go_"+std::to_string(inc)).c_str());
                    std::snprintf(r.topic_tx,sizeof(r.topic_tx),"%s",tname("restart",me).c_str());std::snprintf(r.topic_rx,sizeof(r.topic_rx),"%s",tname("restart",peer).c_str());
                    const std::string req=tmpdir+"/restart_req_"+std::to_string(inc);(void)unlink(status.c_str());(void)unlink(r.go);
                    {FILE* f=std::fopen(req.c_str(),"wb");need(f!=nullptr);need(std::fwrite(&r,sizeof(r),1,f)==1);std::fclose(f);}
                    // Everything the child needs is built before fork: only execve runs in the child.
                    std::vector<std::string> env_s;for(char** e=environ;*e;++e)if(std::strncmp(*e,"MDDS_RESTART_HELPER=",20))env_s.emplace_back(*e);
                    env_s.push_back("MDDS_RESTART_HELPER="+req);std::vector<char*> envp;for(auto& e:env_s)envp.push_back(e.data());envp.push_back(nullptr);
                    char arg0[]="mdds-restart-helper";char* argv_[]={arg0,nullptr};
                    const pid_t pid=fork();if(pid==0){execve("/proc/self/exe",argv_,envp.data());_exit(127);}
                    return pid;};
                auto read_status=[](const std::string& path){std::string t;if(FILE* f=std::fopen(path.c_str(),"r")){char b[256]{};if(std::fgets(b,sizeof(b),f))t=b;std::fclose(f);}return t;};
                auto wait_status=[&](const std::string& path,const char* prefix,int ms){std::string t;(void)waitp([&]{t=read_status(path);return t.rfind(prefix,0)==0||t.rfind("fail",0)==0;},ms);return t;};
                const std::string st1=tmpdir+"/restart_1.status",st2=tmpdir+"/restart_2.status";
                barrier();
                const pid_t pid1=spawn(1,st1);
                const std::string r0=wait_status(st1,"listening",30000);
                barrier();             // both listeners exist: release both dials
                {FILE* g=std::fopen((tmpdir+"/restart_go_1").c_str(),"w");need(g!=nullptr);std::fclose(g);}
                const std::string r1=wait_status(st1,"exchanged",60000);
                barrier();
                int ws1=0;const bool killed=pid1>0&&kill(pid1,SIGKILL)==0&&waitpid(pid1,&ws1,0)==pid1&&WIFSIGNALED(ws1)&&WTERMSIG(ws1)==SIGKILL;
                barrier();
                barrier();
                const pid_t pid2=spawn(2,st2);  // spawn() clears the go file first
                {FILE* g=std::fopen((tmpdir+"/restart_go_2").c_str(),"w");need(g!=nullptr);std::fclose(g);}  // A listens already
                const std::string r2a=wait_status(st2,"exchanged",60000);
                barrier();
                const std::string r2=wait_status(st2,"done",30000);int ws2=0;
                const bool exited=pid2>0&&waitpid(pid2,&ws2,0)==pid2&&WIFEXITED(ws2)&&WEXITSTATUS(ws2)==0;
                barrier();
                auto field=[](const std::string& t,const std::string& k){const auto i=t.find(k+"=");if(i==std::string::npos)return std::string();const auto j=t.find_first_of(" \n",i);return t.substr(i+k.size()+1,j==std::string::npos?std::string::npos:j-i-k.size()-1);};
                const bool new_process=field(r1,"pid")!=field(r2,"pid")&&!field(r2,"pid").empty();
                const bool new_participant=field(r1,"prefix")!=field(r2,"prefix")&&!field(r2,"prefix").empty();
                ok=r1.rfind("exchanged",0)==0&&killed&&r2a.rfind("exchanged",0)==0&&r2.rfind("done",0)==0&&field(r2,"closed")=="1"&&exited&&new_process&&new_participant;
                std::string c1=r1,c2=r2;for(auto* c:{&c1,&c2})for(auto& ch:*c)if(ch=='\n'||ch==' ')ch='_';
                detail="role=RESTARTED_PROCESS inc1="+c1+" killed_SIGKILL="+std::to_string(killed)+" inc2="+c2+" exit0="+std::to_string(exited)+
                    " new_pid="+std::to_string(new_process)+" new_guid_prefix="+std::to_string(new_participant);
            }
            report("peer_process_restart",ok,detail);
        }
        barrier();
        {   // 14. Discovery Server: side A runs a SERVER Participant, side B a CLIENT that only knows the
            //     server's GUID prefix and DSoftBus locator; then DDS data in both directions.
            namespace rt=eprosima::fastrtps::rtps;static constexpr char server_prefix[]="44.53.01.5f.46.44.44.53.44.53.42.31";
            fd::DomainParticipantQos q4=qos;q4.name((std::string(qos.name().c_str())+"_ds").c_str());
            auto config4=config;config4.bind_declared_domain_at_open=true;
            auto d4=s.runtime->MakeStockDescriptor(config4);need(bool(d4));s.retained_descriptors.push_back(d4);q4.transport().user_transports={d4};
            // No initial peers: the client reaches the server only through m_DiscoveryServers; the view
            // binds its declared domain when its first input opens (bind_declared_domain_at_open).
            q4.wire_protocol().builtin.initialPeersList.clear();
            auto& dc=q4.wire_protocol().builtin.discovery_config;
            if(me==0){dc.discoveryProtocol=rt::DiscoveryProtocol_t::SERVER;std::istringstream in(server_prefix);in>>q4.wire_protocol().prefix;need(!in.fail()&&q4.wire_protocol().prefix!=rt::GuidPrefix_t::unknown());}
            else{dc.discoveryProtocol=rt::DiscoveryProtocol_t::CLIENT;rt::RemoteServerAttributes srv;need(srv.ReadguidPrefix(server_prefix));
                srv.metatrafficUnicastLocatorList.push_back(config.directory.routes.at(0).key.remote_locator);dc.m_DiscoveryServers.push_back(srv);}
            barrier(); // the server exists before the client starts pinging it
            if(me==0){auto* p4=s.factory->create_participant(q.domain,q4);extra.ps.push_back(p4);need(p4,NS_STATE);}
            barrier();
            if(me==1){auto* p4=s.factory->create_participant(q.domain,q4);extra.ps.push_back(p4);need(p4,NS_STATE);}
            auto* p4=extra.ps.back();need(s.type.register_type(p4)==RC::RETCODE_OK);
            const auto eff=p4->get_qos().wire_protocol().builtin.discovery_config.discoveryProtocol;
            const bool role_ok=me==0?(eff==rt::DiscoveryProtocol_t::SERVER&&p4->guid().guidPrefix==q4.wire_protocol().prefix):eff==rt::DiscoveryProtocol_t::CLIENT;
            auto* pub4=p4->create_publisher(fd::PUBLISHER_QOS_DEFAULT);auto* sub4=p4->create_subscriber(fd::SUBSCRIBER_QOS_DEFAULT);need(pub4&&sub4);
            fd::Topic *tx,*rx;topic2(p4,"ds",s.type.get_type_name(),tx,rx,fd::TOPIC_QOS_DEFAULT);
            auto* w4=pub4->create_datawriter(tx,keep_all(wq),&s.signals);auto* r4=sub4->create_datareader(rx,keep_all(rq),&s.signals);need(w4&&r4);
            const bool m=matched(w4,r4,1,1,20000);barrier();
            for(std::uint32_t n=1;n<=3;++n){auto v=layer(local.run+34,n);need(w4->write(&v));}
            std::uint32_t next=1;const bool got=m&&waitp([&]{for(;;){LayerSample x;fd::SampleInfo i;if(r4->take_next_sample(&x,&i)!=RC::RETCODE_OK)break;
                if(i.valid_data&&x.run==local.run+34&&x.side==peer&&x.ordinal==next)++next;}return next==4;},8000);
            const bool acked=w4->wait_for_acknowledgments(dur(5))==RC::RETCODE_OK;
            report("discovery_server",got&&acked&&role_ok,std::string("role=")+(me==0?"SERVER":"CLIENT")+" initial_peers=0 effective_role="+std::to_string(role_ok)+" matched="+std::to_string(m)+" received="+std::to_string(next-1)+" acked="+std::to_string(acked));
            barrier();
            // Delete the client before the server so the client never outlives its server's announcements.
            if(me==1){need(p4->delete_contained_entities()==RC::RETCODE_OK&&s.factory->delete_participant(p4)==RC::RETCODE_OK);extra.ps.back()=nullptr;}
            barrier();
        }
        barrier();
        // Features outside this transport's scope or not available in the board's Fast DDS 2.14.5 build.
        for(const char* t:{"time_based_filter","destination_order","presentation","latency_budget","transport_priority","durability_service"})
            std::printf("FEATURE_MATRIX side=%u test=%s pass=NOT_APPLICABLE reason=not_implemented_in_fastdds_2_14\n",me,t);
        std::printf("FEATURE_MATRIX side=%u test=dds_security pass=NOT_APPLICABLE reason=board_fastdds_built_with_SECURITY_OFF\n",me);
        std::printf("FEATURE_MATRIX side=%u test=statistics_module pass=NOT_APPLICABLE reason=board_fastdds_built_without_FASTDDS_STATISTICS\n",me);
        for(const char* t:{"data_sharing","shared_memory_transport","zero_copy_loans","intraprocess_delivery"})
            std::printf("FEATURE_MATRIX side=%u test=%s pass=NOT_APPLICABLE reason=same_host_only_not_a_network_transport_path\n",me,t);
        std::printf("FEATURE_MATRIX side=%u summary all_pass=%d\n",me,int(all));std::fflush(stdout);
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
    // From DATA_DONE on, the peer may legally tear down: its closed sockets make our
    // remaining heartbeats fail to send, which is not a data failure.
    s.tolerate_send_failures=true;NS_RunEvent done{};done.kind=NS_DATA_DONE;h.emit(done);out.phase=3;h.wait(NS_DELETE_RELEASE);
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
