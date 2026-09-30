#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include "DSoftBusKeyedSamplePubSubTypes.hpp"
#include "HostParticipantEvidence.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <functional>
#include <sstream>

namespace host_dds {
using namespace fastdds_dsoftbus::dsoftbus;
using namespace fastdds_dsoftbus::dsoftbus::phase0;
using namespace fastdds_dsoftbus::dsoftbus::host_test;
namespace dds=eprosima::fastdds::dds;
namespace rtps=eprosima::fastdds::rtps;
using namespace std::chrono_literals;
namespace {
void Require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::string Guid(const rtps::GUID_t& g){return Hex(g.guidPrefix.value,12)+Hex(g.entityId.value,4);}
std::string Handle(const dds::InstanceHandle_t& h){return Hex(h.value,16);}
std::uint64_t Sequence(const rtps::SequenceNumber_t& n){return(std::uint64_t(std::uint32_t(n.high))<<32)|n.low;}
Locator Local(int side){EndpointId id{};id[0]=10;id[14]=128;id[15]=static_cast<std::uint8_t>(side+1);Locator l;Require(LocatorCodec::Encode(id,l),"locator encode");return l;}
DSoftBusTransportDescriptor Configuration(int side){
    DSoftBusTransportDescriptor d(8192);const auto l=Local(side),p=Local(1-side);
    d.directory.locals={{l,"host.dds","host.dds."+std::to_string(side)}};
    d.directory.routes={{{l,p},{"host.model.network","host.model.remote","host.model.dial",{"host.model.peer"},RecordDataType::Bytes},1}};
    d.default_metatraffic_unicast={l};d.default_user_unicast={l};return d;
}
struct Backend final:NativeBackend {
    std::mutex gate;ProviderCallbacks callbacks;int side;std::atomic<int> creates{0},closes{0},sends{0},forwarded{0},rejected{0},binds{0};
    std::weak_ptr<Backend> peer;std::weak_ptr<HostProviderLoop> loop;std::shared_ptr<Trace> trace;
    bool forward=true,fail_bind=false;std::atomic<bool> stopped{false},throw_once{false};
    explicit Backend(int s,std::shared_ptr<Trace> t=std::make_shared<Trace>()):side(s),trace(std::move(t)){}
    BackendKind Kind()const noexcept override{return BackendKind::HostMock;}
    BackendIdentity Identity()const override{return {"host.real.dds."+std::to_string(side),{std::uint64_t(151+side)},{std::uint64_t(251+side)}};}
    BackendSocketResult Create(ProviderCallbacks c)override{std::lock_guard<std::mutex> l(gate);callbacks=c;++creates;trace->Add({"PROVIDER_CREATE","","","declared_model_identity",side});return {BackendStatus::None,42+side};}
    BackendCallResult Listen(std::int32_t)override{return BackendStatus::None;}
    BackendCallResult Bind(std::int32_t)override{++binds;trace->Add({"PROVIDER_BIND","","",fail_bind?"MODEL_REJECTED":"MODEL_OK",side});return fail_bind?BackendStatus::BindingRejected:BackendStatus::None;}
    BackendSendResult Send(std::int32_t id,const std::uint8_t* bytes,std::size_t n,std::chrono::steady_clock::time_point)override{
        Require(id==42+side&&!stopped,"send live own model socket");++sends;
        auto owner=loop.lock();Require(bool(owner),"live model loop");const auto view=owner->Observe();Require(view.send.admitted_outstanding==1,"original NativeSendExecution ticket pinned");
        if(throw_once.exchange(false)){trace->Add({"EXPECTED_FAULT_INJECTED","","","Host_send_exception",side});throw std::runtime_error("expected Host send exception");}
        trace->Frame("MODEL_SEND",side,bytes,n);
        trace->Add({"EXECUTION_TICKET","","","PumpSend,ticket_outstanding=1,ticket_id=UNOBSERVED,owner="+std::to_string(view.adapter.binding.owner_incarnation)+",record="+std::to_string(view.adapter.binding.record_incarnation)+",model_socket="+std::to_string(id),side});
        if(forward)if(auto remote=peer.lock()){
            ProviderCallbacks saved;{std::lock_guard<std::mutex> l(remote->gate);saved=remote->callbacks;}
            // Synchronous borrow: adapter owns its copy before this callback returns.
            trace->Frame("MODEL_FORWARD",side,bytes,n);
            const auto status=saved.OnBytes(42+remote->side,bytes,static_cast<std::uint32_t>(n));
            if(status==BackendStatus::None)++forwarded;else ++rejected;
            trace->Add({"MODEL_CALLBACK_RESULT","","",std::to_string(static_cast<int>(status)),remote->side});
        }
        return {BackendStatus::None,0};
    }
    BackendCallResult Shutdown(std::int32_t id)override{Require(id==42+side,"shutdown owned model socket");stopped=true;++closes;trace->Add({"PROVIDER_SHUTDOWN","","","no_SDK_drain_claim",side});return BackendStatus::None;}
};
// Test-program-only descriptor; no normal descriptor or production default changes.
struct HostDescriptor final:rtps::TransportDescriptorInterface {
    DSoftBusTransportDescriptor config;std::shared_ptr<HostProviderLoop> loop;mutable std::atomic<int> factories{0};
    HostDescriptor(int side,std::shared_ptr<HostProviderLoop> l):TransportDescriptorInterface(8192,1),config(Configuration(side)),loop(std::move(l)){}
    rtps::TransportInterface* create_transport()const override{
        ++factories;
        return HostFacadeTestAccess::Create(config,loop).release();
    }
    std::uint32_t min_send_buffer_size()const override{return config.min_send_buffer_size();}
};
void Isolate(){
    for(const auto* name:{"FASTDDS_DEFAULT_PROFILES_FILE","FASTRTPS_DEFAULT_PROFILES_FILE","FASTDDS_ENVIRONMENT_FILE","ROS_DISCOVERY_SERVER","ROS_SUPER_CLIENT","FASTDDS_BUILTIN_TRANSPORTS","FASTDDS_STATISTICS"})Require(_putenv_s(name,"")==0,"clear process-only DDS environment");
    Require(_putenv_s("SKIP_DEFAULT_XML","1")==0,"skip default XML");
    eprosima::fastdds::LibrarySettings s;s.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
    auto* f=dds::DomainParticipantFactory::get_instance();Require(f->set_library_settings(s)==dds::RETCODE_OK,"intraprocess set");
    Require(f->get_library_settings(s)==dds::RETCODE_OK&&s.intraprocess_delivery==eprosima::fastdds::INTRAPROCESS_OFF,"intraprocess effective");
}
dds::DomainParticipantQos Qos(int side,std::shared_ptr<rtps::TransportDescriptorInterface> desc){
    dds::DomainParticipantQos q;q.name("HostNative"+std::to_string(side));q.transport().use_builtin_transports=false;q.transport().user_transports={std::move(desc)};
    q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");
    auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(Local(side));w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(Local(side));w.default_multicast_locator_list.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(Local(1-side));
    w.builtin.discovery_config.leaseDuration_announcementperiod={1,0};w.builtin.discovery_config.initial_announcements.count=5;w.builtin.discovery_config.initial_announcements.period={0,200000000};return q;
}
void Factory(){
    Isolate();auto backend=std::make_shared<Backend>(0);auto loop=std::make_shared<HostProviderLoop>(backend);auto desc=std::make_shared<HostDescriptor>(0,loop);
    auto* factory=dds::DomainParticipantFactory::get_instance();auto* p=factory->create_participant(151,Qos(0,desc));
    if(p){Require(factory->delete_participant(p)==dds::RETCODE_OK,"DDS delete");}
    Require(p!=nullptr&&desc->factories==1&&backend->creates==1,"real DDS factory must select injected actual facade");
    Require(loop->Observe().closed&&backend->closes==1,"DDS owns shutdown");
}
struct RxObserver final:ProcessReceiveObserver {
    int side;std::shared_ptr<Trace> trace;RxObserver(int s,std::shared_ptr<Trace> t):side(s),trace(std::move(t)){}
    void OnReceive(ReceiveStage s,const ReceiveDiagnostic& d,const std::uint8_t* p,std::size_t n)noexcept override{
        const char* name=s==ReceiveStage::Captured?"RX_CAPTURED":s==ReceiveStage::OwnedCopy?"RX_OWNED":s==ReceiveStage::Running?"RX_RUNNING":s==ReceiveStage::Settled?"RX_SETTLED":"RX_OTHER";
        trace->Frame(name,side,p,n,d.correlation,d.identity.application_incarnation,d.identity.receiver_incarnation);
    }
};
template<class Q>void EndpointQos(Q& q){
    q.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;q.reliability().max_blocking_time={1,0};
    q.durability().kind=dds::VOLATILE_DURABILITY_QOS;q.history().kind=dds::KEEP_LAST_HISTORY_QOS;q.history().depth=16;
    q.resource_limits().max_samples=32;q.resource_limits().max_instances=1;q.resource_limits().max_samples_per_instance=32;q.resource_limits().allocated_samples=16;q.data_sharing().off();
}
template<class Q>bool EffectiveEndpoint(const Q& q){return q.reliability().kind==dds::RELIABLE_RELIABILITY_QOS&&q.durability().kind==dds::VOLATILE_DURABILITY_QOS&&q.history().kind==dds::KEEP_LAST_HISTORY_QOS&&q.history().depth==16&&q.resource_limits().max_samples==32&&q.resource_limits().max_instances==1&&q.resource_limits().max_samples_per_instance==32&&q.data_sharing().kind()==dds::OFF;}
bool ClosedLedger(const HostLoopObservation& v,int provider_closes,bool pump_kept_alive){return pump_kept_alive&&v.closed&&!v.input_open&&v.active_pumps==0&&v.tx.records==0&&v.tx.bytes==0&&v.receive.records==0&&v.receive.bytes==0&&v.receive.copying==0&&v.receive.active_applications==0&&v.receive.retained_applications==0&&v.close.status==BackendStatus::None&&v.close.adapter_drained&&provider_closes==1&&v.close_calls>0&&v.shutdown_calls>0&&v.first_shutdown_order>0&&v.first_shutdown_order<v.first_close_order;}
bool PumpHealthy(const NativeSendRunResult& tx,BackendStatus rx,bool closing){
    bool tx_ok=false;
    if(tx.outcome==NativeSendOutcome::ReturnedSdkCode)tx_ok=tx.status==NativeStatus::None&&tx.actual_return_code&&*tx.actual_return_code==0;
    else if(tx.outcome==NativeSendOutcome::NotEntered)tx_ok=tx.status==NativeStatus::NotFound||(closing&&(tx.status==NativeStatus::ServiceClosing||tx.status==NativeStatus::Revoked||tx.status==NativeStatus::WrongState||tx.status==NativeStatus::AlreadySettled||tx.status==NativeStatus::OwnerUnavailable));
    const bool rx_ok=rx==BackendStatus::None||rx==BackendStatus::NoWork||(closing&&(rx==BackendStatus::InvalidState||rx==BackendStatus::ChannelClosed||rx==BackendStatus::ProviderStopped||rx==BackendStatus::BindingRejected));
    return tx_ok&&rx_ok;
}
struct Endpoint {
    std::shared_ptr<Backend> backend;std::shared_ptr<HostProviderLoop> loop;std::shared_ptr<HostDescriptor> descriptor;
    dds::DomainParticipant* participant=nullptr;dds::Publisher* publisher=nullptr;dds::Subscriber* subscriber=nullptr;
    dds::Topic* topic=nullptr;dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::thread pump;std::atomic<bool> stop{false},deleting{false};std::atomic<unsigned> pump_errors{0};std::string participant_id,writer_id,reader_id;
};
struct Fixture {
    std::shared_ptr<Trace> trace=std::make_shared<Trace>();Endpoint e[2];
    dds::TypeSupport type{new DSoftBusKeyedSamplePubSubType};
    const std::uint64_t run=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    bool cleanup_failed=false;
    explicit Fixture(bool forwarding=true){
        for(int i=0;i<2;++i){e[i].backend=std::make_shared<Backend>(i,trace);e[i].backend->forward=forwarding;e[i].loop=std::make_shared<HostProviderLoop>(e[i].backend,std::make_shared<RxObserver>(i,trace));e[i].backend->loop=e[i].loop;e[i].descriptor=std::make_shared<HostDescriptor>(i,e[i].loop);}
        e[0].backend->peer=e[1].backend;e[1].backend->peer=e[0].backend;
    }
    ~Fixture(){
        for(int i:{1,0})if(e[i].participant)Delete(i,false);
        for(auto& x:e)x.stop=true;for(auto& x:e)if(x.pump.joinable())x.pump.join();
        trace->Dump(run);
    }
    void Create(int side,bool expect=true){
        auto& x=e[side];auto q=Qos(side,x.descriptor);
        trace->Add({"DDS_CREATE_BEGIN","","","",side});
        x.participant=dds::DomainParticipantFactory::get_instance()->create_participant(151,q);
        trace->Add({"DDS_CREATE_RESULT","","",x.participant?"non_null":"null",side});
        if(!expect)return; // Record the real API outcome; do not prescribe it.
        Require(x.participant!=nullptr,"Participant creation");x.participant_id=Guid(x.participant->guid());
        const auto actual=x.participant->get_qos();
        Require(!actual.transport().use_builtin_transports&&actual.transport().user_transports.size()==1&&actual.transport().user_transports[0]==x.descriptor,"effective transport singleton");
        const auto& wire=actual.wire_protocol();const auto one=[](const auto& locators,const Locator& l){return locators.size()==1&&*locators.begin()==l;};
        Require(one(wire.builtin.metatrafficUnicastLocatorList,Local(side))&&one(wire.default_unicast_locator_list,Local(side))&&one(wire.builtin.initialPeersList,Local(1-side))&&wire.builtin.metatrafficMulticastLocatorList.empty()&&wire.default_multicast_locator_list.empty(),"effective exact locator mapping");
        bool ignore_local=false;for(const auto& prop:actual.properties().properties())if(prop.name()=="fastdds.ignore_local_endpoints"&&prop.value()=="true")ignore_local=true;
        Require(ignore_local,"effective self-match exclusion");
        Require(x.descriptor->factories==1&&x.backend->creates==1&&x.loop->Observe().input_open,"actual factory and ReceiverResource Open");
        trace->Add({"FACTORY_SELECTION","","","actual_DSoftBusTransport,HostFacadeTestAccess,factory_calls=1,facade_instance="+std::to_string(side),side});
        trace->Add({"DDS_PARTICIPANT","","",x.participant_id,side});
        trace->Add({"EFFECTIVE_CONFIG","","","domain=151,one_descriptor,builtins=off,intraprocess=off,meta_user_same_local,private_kind="+std::to_string(Local(side).kind)+",local="+Hex(Local(side).address,16)+",peer="+Hex(Local(1-side).address,16)+",max_message=8192",side});
        x.pump=std::thread([&,side]{auto& y=e[side];try{while(!y.stop){const auto tx=y.loop->PumpSend();const auto rx=y.loop->PumpReceive();if(!PumpHealthy(tx,rx,y.deleting)&&y.pump_errors.fetch_add(1)==0)trace->Add({"PUMP_ERROR","","","tx_status="+std::to_string(static_cast<int>(tx.status))+",outcome="+std::to_string(static_cast<int>(tx.outcome))+",rx="+std::to_string(static_cast<int>(rx)),side});std::this_thread::sleep_for(1ms);}}catch(...){trace->Add({"PUMP_ERROR","","","exception",side});}});
        Require(type.register_type(x.participant)==dds::RETCODE_OK,"register bounded type");
        x.topic=x.participant->create_topic("HostProviderBounded",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);Require(x.topic,"topic");
        x.publisher=x.participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);x.subscriber=x.participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);Require(x.publisher&&x.subscriber,"publisher/subscriber");
        auto wq=dds::DATAWRITER_QOS_DEFAULT;EndpointQos(wq);wq.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;
        x.writer=x.publisher->create_datawriter(x.topic,wq);Require(x.writer,"writer");x.writer_id=Guid(x.writer->guid());
        Require(EffectiveEndpoint(x.writer->get_qos())&&x.writer->get_qos().publish_mode().kind==dds::SYNCHRONOUS_PUBLISH_MODE,"effective Writer QoS");
        NewReader(side);trace->Add({"DDS_WRITER","","",x.writer_id,side});
    }
    void NewReader(int side){auto& x=e[side];auto rq=dds::DATAREADER_QOS_DEFAULT;EndpointQos(rq);x.reader=x.subscriber->create_datareader(x.topic,rq);Require(x.reader,"reader");x.reader_id=Guid(x.reader->guid());Require(EffectiveEndpoint(x.reader->get_qos()),"effective Reader QoS");trace->Add({"DDS_READER","","",x.reader_id,side});}
    std::set<std::string> Readers(int side){std::vector<dds::InstanceHandle_t> h;Require(e[side].writer->get_matched_subscriptions(h)==dds::RETCODE_OK,"current matched readers API");std::set<std::string> v;for(auto& x:h)v.insert(Handle(x));return v;}
    std::set<std::string> Writers(int side){std::vector<dds::InstanceHandle_t> h;Require(e[side].reader->get_matched_publications(h)==dds::RETCODE_OK,"current matched writers API");std::set<std::string> v;for(auto& x:h)v.insert(Handle(x));return v;}
    void Wait(const std::function<bool()>& predicate,const char* label){auto end=std::chrono::steady_clock::now()+8s;while(!predicate()){Require(trace->Good(),"trace/pump fault");Require(std::chrono::steady_clock::now()<end,label);std::this_thread::sleep_for(10ms);}}
    void Match(){Wait([&]{return CurrentMatch(Readers(0),e[1].reader_id)&&CurrentMatch(Readers(1),e[0].reader_id)&&CurrentMatch(Writers(0),e[1].writer_id)&&CurrentMatch(Writers(1),e[0].writer_id);},"current remote match");for(int i=0;i<2;++i)trace->Add({"CURRENT_REMOTE_MATCH","",e[1-i].writer_id,e[1-i].reader_id,i});}
    void Write(int side,unsigned first,unsigned last){for(unsigned n=first;n<=last;++n){DSoftBusKeyedSample sample;sample.key(1);sample.run(run);sample.writer(side+1);sample.ordinal(n);for(unsigned k=0;k<32;++k)sample.payload()[k]=static_cast<std::uint8_t>(n+k+side);rtps::WriteParams params;Require(e[side].writer->write(&sample,params)==dds::RETCODE_OK,"DDS write");trace->Add({"DDS_WRITE","DATA",e[side].writer_id,std::to_string(n),side,Sequence(params.sample_identity().sequence_number())});}}
    std::vector<SampleReceipt> Take(int side,unsigned first,unsigned last){
        std::vector<SampleReceipt> rows;rows.reserve(16);auto& x=e[side];
        Wait([&]{DSoftBusKeyedSample sample;dds::SampleInfo info;for(;;){const auto rc=x.reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_NO_DATA)break;Require(rc==dds::RETCODE_OK,"DDS take");if(!info.valid_data)continue;Require(rows.size()<16,"bounded Reader ledger");bool valid=sample.key()==1&&sample.writer()==unsigned(2-side);for(unsigned k=0;k<32;++k)valid=valid&&sample.payload()[k]==static_cast<std::uint8_t>(sample.ordinal()+k+1-side);SampleReceipt row{x.reader_id,Guid(info.sample_identity.writer_guid()),sample.run(),Sequence(info.sample_identity.sequence_number()),sample.ordinal(),valid,true};rows.push_back(row);trace->Add({"DDS_TAKE","DATA",row.writer,std::to_string(row.ordinal)+":"+row.reader,side,row.sn});}return rows.size()>=last-first+1;},"Reader sample delivery");
        Require(LedgerValid(rows,x.reader_id,e[1-side].writer_id,run,first,last),"exact independent Reader ledger");
        for(const auto& row:rows)for(const auto& stage:{"MODEL_SEND","MODEL_FORWARD","RX_OWNED","RX_RUNNING"})Require(trace->Has(stage,stage[0]=='M'?1-side:side,"DATA",row.writer,row.sn),"same-run model/data/take chain");return rows;
    }
    void Ack(int side){Require(e[side].writer->wait_for_acknowledgments({3,0})==dds::RETCODE_OK,"DDS wait_for_acknowledgments");trace->Add({"DDS_WAIT_OK","","","API_not_packet_count",side});}
    void PostAck(){
        // Frozen bounded observation, not permanent absence or a transport SLA.
        const auto end=std::chrono::steady_clock::now()+100ms;
        do{Require(trace->Good(),"pump failure before post-ACK completion");for(int side=0;side<2;++side){
            DSoftBusKeyedSample sample;dds::SampleInfo info;
            for(;;){const auto rc=e[side].reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_NO_DATA)break;Require(rc==dds::RETCODE_OK,"post-ACK take");
                if(info.valid_data){trace->Add({"POST_ACK_REJECT","DATA",Guid(info.sample_identity.writer_guid()),std::to_string(sample.ordinal())+":"+e[side].reader_id,side,Sequence(info.sample_identity.sequence_number())});throw std::runtime_error("unexpected valid sample after ACK");}
            }
        }std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<end);
        trace->Add({"POST_ACK_EMPTY","","","window_ms=100,both_current_Readers",0});
    }
    void Traffic(unsigned first,unsigned last){Write(0,first,last);Write(1,first,last);Take(1,first,last);Take(0,first,last);Ack(0);Ack(1);PostAck();}
    void Recreate(){
        auto& b=e[1];const auto old=b.reader_id;const auto p=b.participant_id,w=b.writer_id;const auto old_view=b.loop->Observe();
        Require(b.subscriber->delete_datareader(b.reader)==dds::RETCODE_OK,"delete actual Reader");b.reader=nullptr;trace->Add({"DDS_READER_DELETE","","",old,1});
        Wait([&]{return Readers(0).empty();},"old reader match removed");NewReader(1);Require(b.reader_id!=old,"fresh Reader GUID");Match();
        Require(Guid(b.participant->guid())==p&&Guid(b.writer->guid())==w&&b.loop->Observe().open_calls==old_view.open_calls&&b.loop->Observe().close_calls==old_view.close_calls,"Reader recreate is not Participant/input recreate");Write(0,6,10);Take(1,6,10);Ack(0);PostAck();
        trace->Add({"READER_RECREATE_VALIDATED","","","old="+old+",new="+b.reader_id+",participant_writer_unchanged,input_close_delta=0",1});
    }
    bool Delete(int side,bool verify){
        auto& x=e[side];x.deleting=true;bool ok=true;const bool pump_kept_alive=x.pump.joinable()&&!x.stop;auto rc=[&](auto value){if(value!=dds::RETCODE_OK)ok=false;};trace->Add({"DDS_DELETE_BEGIN","","",x.participant_id,side});
        if(x.reader){rc(x.subscriber->delete_datareader(x.reader));x.reader=nullptr;}if(x.writer){rc(x.publisher->delete_datawriter(x.writer));x.writer=nullptr;}
        if(x.subscriber){rc(x.participant->delete_subscriber(x.subscriber));x.subscriber=nullptr;}if(x.publisher){rc(x.participant->delete_publisher(x.publisher));x.publisher=nullptr;}if(x.topic){rc(x.participant->delete_topic(x.topic));x.topic=nullptr;}
        rc(dds::DomainParticipantFactory::get_instance()->delete_participant(x.participant));x.participant=nullptr;
        const auto v=x.loop->Observe();trace->Add({"DDS_DELETE_COMPLETE","","","open="+std::to_string(v.open_calls)+",close="+std::to_string(v.close_calls)+",shutdown="+std::to_string(v.shutdown_calls)+",first_shutdown="+std::to_string(v.first_shutdown_order)+",first_close="+std::to_string(v.first_close_order),side});
        const bool settled=ClosedLedger(v,x.backend->closes,pump_kept_alive);
        trace->Add({"CLEANUP_RESULT","","","api_ok="+std::to_string(ok)+",settled="+std::to_string(settled)+",tx_records="+std::to_string(v.tx.records)+",rx_records="+std::to_string(v.receive.records)+",rx_bytes="+std::to_string(v.receive.bytes)+",app="+std::to_string(v.receive.active_applications)+",retained="+std::to_string(v.receive.retained_applications)+",pumps_kept_alive="+std::to_string(pump_kept_alive),side});
        cleanup_failed=cleanup_failed||!ok||!settled;if(verify)Require(ok&&settled,"DDS delete actual input/runtime settlement");return ok&&settled;
    }
    void End(bool b_first){Delete(b_first?1:0,true);Delete(b_first?0:1,true);for(auto& x:e)x.stop=true;for(auto& x:e)if(x.pump.joinable())x.pump.join();Require(!cleanup_failed&&trace->Good(),"normal cleanup/pump join");trace->Add({"PUMPS_JOINED","","","both",0});}
    void Discovery(){for(int i=0;i<2;++i){Require(trace->Has("MODEL_SEND",i,"SPDP")&&trace->Has("RX_RUNNING",1-i,"SPDP"),"both SPDP paths");Require(trace->Has("MODEL_SEND",i,"SEDP")&&trace->Has("RX_RUNNING",1-i,"SEDP"),"both SEDP paths");Require(trace->Has("MODEL_SEND",i,"HEARTBEAT")||trace->Has("MODEL_SEND",i,"ACKNACK"),"actual reliable RTPS control");}}
};
void Normal(bool b_first,bool recreate){Isolate();Fixture f;f.Create(0);f.Create(1);f.Match();f.Traffic(1,5);if(recreate)f.Recreate();f.Discovery();f.End(b_first);}
void Negative(){Isolate();Fixture f(false);f.Create(0);f.Create(1);const auto end=std::chrono::steady_clock::now()+2s;while(std::chrono::steady_clock::now()<end){Require(f.Readers(0).empty()&&f.Readers(1).empty()&&f.Writers(0).empty()&&f.Writers(1).empty(),"disabled link must not bypass-match");for(auto& e:f.e){DSoftBusKeyedSample s;dds::SampleInfo info;Require(e.reader->take_next_sample(&s,&info)==dds::RETCODE_NO_DATA,"disabled link no delivery");}std::this_thread::sleep_for(10ms);}Require(f.e[0].backend->sends>0&&f.e[1].backend->sends>0&&f.e[0].backend->forwarded==0&&f.e[1].backend->forwarded==0,"negative actually exercised sending");f.End(true);}
void Default(){Isolate();auto* f=dds::DomainParticipantFactory::get_instance();auto d=std::make_shared<DSoftBusTransportDescriptor>(Configuration(0));auto*p=f->create_participant(151,Qos(0,d));std::cout<<"DEFAULT_PARTICIPANT_RESULT="<<(p?"non_null":"null")<<'\n';if(p)f->delete_participant(p);Require(!p,"default unavailable via real Participant");}
void Failure(){Isolate();Fixture f;f.Create(0);const auto old=f.e[0].loop->Observe();f.e[1].backend->fail_bind=true;f.Create(1,false);auto v=f.e[1].loop->Observe();Require(f.e[1].backend->binds==1&&f.e[1].backend->closes==1&&v.receive.records==0&&v.receive.active_applications==0&&!v.input_open,"failed provider prepare rollback");auto a=f.e[0].loop->Observe();Require(a.input_open&&!a.closed&&a.adapter.binding.receiver_identity==old.adapter.binding.receiver_identity&&f.e[0].backend->closes==0,"failed instance cannot clean live A");if(f.e[1].participant){Require(dds::DomainParticipantFactory::get_instance()->delete_participant(f.e[1].participant)==dds::RETCODE_OK,"delete API-returned failed-input Participant");f.e[1].participant=nullptr;}Require(f.e[1].loop->Observe().closed,"failed-instance final shutdown");f.Delete(0,true);}
void PumpFault(){Isolate();Fixture f;f.e[0].backend->throw_once=true;f.Create(0);auto end=std::chrono::steady_clock::now()+2s;while(f.e[0].pump_errors==0&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);Require(f.e[0].pump_errors>0&&!f.trace->Good(),"converted provider fault must reject scenario");auto first=f.trace->FirstPumpFailure();f.Delete(0,false);Require(f.trace->FirstPumpFailure()==first&&!first.empty(),"cleanup must retain first pump failure");}
void LateExtra(){Isolate();Fixture f;f.Create(0);f.Create(1);f.Match();f.Traffic(1,5);f.Write(0,1,1);f.Ack(0);f.Wait([&]{return f.e[1].reader->get_unread_count()>0;},"late extra actually pending");bool rejected=false;try{f.PostAck();}catch(const std::runtime_error& e){rejected=std::string(e.what())=="unexpected valid sample after ACK";}Require(rejected,"late extra must reject complete phase");f.End(true);}
void Oracle(){
    std::vector<std::uint8_t> packet(52,0);std::memcpy(packet.data(),"RTPS",4);packet[8]=1;
    packet[20]=0x01;packet[21]=1;packet[24]=0x09;packet[25]=3;
    packet[28]=0x15;packet[29]=1;packet[30]=20;packet[40]=0x00;packet[41]=0x01;packet[42]=0x00;packet[43]=0xc2;packet[48]=1;
    const auto parsed=Parse(packet.data(),packet.size());Require(parsed.size()==1&&parsed[0].kind=="SPDP"&&parsed[0].sn==1,"PAD0 and invalidated INFO_TS0 do not end trace parsing");
    packet[30]=21;Require(Parse(packet.data(),packet.size()).empty(),"truncated trace rejected");
    Require(!CurrentMatch({"local"},"remote")&&!CurrentMatch({"old-reader"},"new-reader"),"reject local/historical match");
    std::vector<SampleReceipt> good{{"r","w",1,1,1,true,true}};Require(LedgerValid(good,"r","w",1,1,1),"ledger control");
    for(int i=0;i<6;++i){auto bad=good;if(i==0)bad[0].reader="old";if(i==1)bad[0].writer="wrong";if(i==2)bad[0].taken=false;if(i==3)bad.push_back(bad[0]);if(i==4)bad.clear();if(i==5)bad[0].run=0;Require(!LedgerValid(bad,"r","w",1,1,1),"ledger counterexample");}
    HostLoopObservation done;done.closed=true;done.close.status=BackendStatus::None;done.close.adapter_drained=true;done.close_calls=done.shutdown_calls=1;done.first_shutdown_order=1;done.first_close_order=2;Require(ClosedLedger(done,1,true),"close oracle control");
    Require(!ClosedLedger(done,1,false),"premature pump stop rejected");
    for(int i=0;i<5;++i){auto bad=done;if(i==0)bad.input_open=true;if(i==1)bad.receive.records=1;if(i==2)bad.receive.retained_applications=1;if(i==3)bad.tx.records=1;if(i==4)bad.close_calls=0;Require(!ClosedLedger(bad,1,true),"unsettled receiver/lease/ticket/close counterexample");}
    done.close.status=BackendStatus::ProviderException;Require(!ClosedLedger(done,1,true),"cleanup provider error cannot be success");
}
}
}
int main(int argc,char**argv){try{using namespace host_dds;Require(argc==2,"case");std::string c=argv[1];if(c=="factory")Factory();else if(c=="default")Default();else if(c=="normal-b-first")Normal(true,true);else if(c=="normal-a-first")Normal(false,false);else if(c=="disabled-link")Negative();else if(c=="failure")Failure();else if(c=="oracle")Oracle();else if(c=="pump-fault")PumpFault();else if(c=="late-extra")LateExtra();else throw std::runtime_error("unknown case");std::cout<<"PASS "<<c<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FIRST_FAILURE predicate="<<e.what()<<'\n';return 1;}}
