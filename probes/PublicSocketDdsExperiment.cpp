#include "PublicSocketDdsExperiment.hpp"
#include "PublicSocketDdsTrace.hpp"
#include <mdds/dsoftbus/RuntimeProbeAccess.hpp>
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
#include "MddsKeyedSamplePubSubTypes.hpp"
#include <socket.h> // Private opt-in assembly only; use the authorized QosType declarations.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
namespace mdds::dsoftbus::dds_experiment {
namespace dds=eprosima::fastdds::dds;namespace rtps=eprosima::fastdds::rtps;
using namespace phase0;using namespace host_test;using namespace std::chrono_literals;
namespace {
thread_local const Control* control=nullptr;
thread_local bool setup_control_failed=false;
#ifdef MDDS_DDS_EXPERIMENT_TESTING
thread_local std::function<void(dds::DataWriter*)> after_wait_hook;
#endif
thread_local HostProviderLoop* inspecting=nullptr;
void Require(bool b,const char* p){if(!b)throw std::runtime_error(p);}
std::string Hex(const std::uint8_t* p,std::size_t n){const char* d="0123456789abcdef";std::string r;r.reserve(n*2);for(std::size_t i=0;i<n;++i){r+=d[p[i]>>4];r+=d[p[i]&15];}return r;}
std::string Guid(const rtps::GUID_t& g){return Hex(g.guidPrefix.value,12)+Hex(g.entityId.value,4);}
std::uint64_t Sequence(const rtps::SequenceNumber_t& s){return(std::uint64_t(std::uint32_t(s.high))<<32)|s.low;}
Locator Local(unsigned side){EndpointId id{};id[0]=10;id[14]=128;id[15]=std::uint8_t(side+1);Locator l;Require(LocatorCodec::Encode(id,l),"LOCATOR_ENCODING");return l;}
std::string Name(const Config& c,unsigned side,const char* role){return c.service_prefix+(c.service_prefix=="host.public"?"":c.run)+(side?".B.":".A.")+role;}
DSoftBusTransportDescriptor Configuration(const Config& c){DSoftBusTransportDescriptor d(8192);auto l=Local(c.side),p=Local(1-c.side);d.directory.locals={{l,Name(c,c.side,"namespace"),Name(c,c.side,"listener")}};d.directory.routes={{{l,p},{c.peer_network,Name(c,1-c.side,"listener"),Name(c,c.side,"outgoing"),{Name(c,1-c.side,"outgoing")},RecordDataType::Bytes},1}};d.default_metatraffic_unicast={l};d.default_user_unicast={l};return d;}
PublicSocketConfiguration BackendConfiguration(const Config& c){PublicSocketConfiguration b;b.identity={Name(c,c.side,"outgoing"),{910+c.side*4},{1010+c.side*4}};b.listener_identity={Name(c,c.side,"listener"),{911+c.side*4},{1011+c.side*4}};b.package_name="com.kaihong.mdds";b.peer_name=Name(c,1-c.side,"listener");b.peer_network_id=c.peer_network;b.bind_qos={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};b.listen_qos=b.bind_qos;b.max_message_size=8192;return b;}
struct Frame {const char* stage=nullptr;const char* kind=nullptr;std::int32_t socket=-1;std::array<std::uint8_t,16> writer{};std::uint64_t packet=0,sn=0,hash=0,correlation=0,app=0,receiver=0,owner=0,admitted=0,entered=0;std::size_t length=0;};
struct Frames {std::mutex gate;std::array<Frame,4096> rows{};std::size_t count=0,overflow=0;std::uint64_t packets=0;};
Frames& Store(){alignas(Frames) static unsigned char memory[sizeof(Frames)];static Frames* p=new(memory) Frames;return *p;}
void FrameBytes(const char* stage,std::int32_t socket,const std::uint8_t* p,std::size_t n,const ReceiveDiagnostic* diagnostic=nullptr) noexcept {
    if(!p||n<20||n>8192||std::memcmp(p,"RTPS",4))return;
    Frame f;f.stage=stage;f.socket=socket;f.length=n;f.hash=14695981039346656037ull;for(std::size_t i=0;i<n;++i){f.hash^=p[i];f.hash*=1099511628211ull;}
    if(diagnostic){auto native=diagnostic->native.Observe();f.correlation=diagnostic->correlation;f.app=diagnostic->identity.application_incarnation;f.receiver=diagnostic->identity.receiver_incarnation;f.owner=native.incarnation;}
    else if(inspecting){auto view=inspecting->Observe();f.owner=view.adapter.binding.owner_incarnation;f.admitted=view.send.admitted_outstanding;f.entered=view.native.send.entered;}
    auto& store=Store();{std::lock_guard<std::mutex> l(store.gate);f.packet=++store.packets;}
    std::copy(p+8,p+20,f.writer.begin());
    for(std::size_t pos=20;pos+4<=n;){const auto id=p[pos],flags=p[pos+1];bool le=(flags&1)!=0;std::size_t len=le?(p[pos+2]|std::size_t(p[pos+3])<<8):(std::size_t(p[pos+2])<<8|p[pos+3]);auto body=pos+4;bool empty=id==1||(id==9&&(flags&2));auto end=len?body+len:(empty?body:n);if(end>n)return;
        auto u32=[&](std::size_t at){std::uint32_t x=0;for(unsigned j=0;j<4;++j)x|=std::uint32_t(p[at+j])<<(8*(le?j:3-j));return x;};f.kind=nullptr;f.sn=0;
        if(id==12&&end-body>=20)std::copy(p+body+8,p+body+20,f.writer.begin());
        if(id==21&&end-body>=20){std::copy(p+body+8,p+body+12,f.writer.begin()+12);const auto entity=std::array<std::uint8_t,4>{p[body+8],p[body+9],p[body+10],p[body+11]};f.kind=entity==std::array<std::uint8_t,4>{0,1,0,0xc2}?"SPDP":(entity==std::array<std::uint8_t,4>{0,0,3,0xc2}||entity==std::array<std::uint8_t,4>{0,0,4,0xc2})?"SEDP":"DATA";f.sn=(std::uint64_t(u32(body+12))<<32)|u32(body+16);}
        else if(id==6||id==7){f.kind=id==6?"ACKNACK":"HEARTBEAT";std::fill(f.writer.begin()+12,f.writer.end(),0);}
        if(f.kind){std::lock_guard<std::mutex> l(store.gate);if(store.count==store.rows.size())++store.overflow;else store.rows[store.count++]=f;}pos=end;
    }
}
struct Observer final:ProcessReceiveObserver {void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& d,const std::uint8_t* p,std::size_t n)noexcept override{
    const char* name=stage==ReceiveStage::Captured?"RX_CAPTURED":stage==ReceiveStage::OwnedCopy?"RX_OWNED":stage==ReceiveStage::Running?"RX_RUNNING":nullptr;
    if(name){const auto native=d.native.Observe();FrameBytes(name,native.owned_id.value_or(-1),p,n,&d);}
}};
struct Descriptor final:rtps::TransportDescriptorInterface {
    DSoftBusTransportDescriptor config;std::shared_ptr<HostProviderLoop> loop;mutable std::atomic<unsigned> factories{0};
    Descriptor(const Config& c,std::shared_ptr<HostProviderLoop> l):TransportDescriptorInterface(8192,1),config(Configuration(c)),loop(std::move(l)){}
    rtps::TransportInterface* create_transport()const override{++factories;auto p=HostFacadeTestAccess::Create(config,loop);std::printf("DDS_FACTORY implementation=DSoftBusTransport variant=isolated_experiment mode=%u facade=%p\n",unsigned(p->mode()),static_cast<void*>(p.get()));std::fflush(stdout);return p.release();}
    std::uint32_t min_send_buffer_size()const override{return config.min_send_buffer_size();}
};
template<class Q>void EndpointQos(Q& q){q.reliability().kind=dds::RELIABLE_RELIABILITY_QOS;q.reliability().max_blocking_time={1,0};q.durability().kind=dds::VOLATILE_DURABILITY_QOS;q.history().kind=dds::KEEP_LAST_HISTORY_QOS;q.history().depth=16;q.resource_limits().max_samples=32;q.resource_limits().max_instances=1;q.resource_limits().max_samples_per_instance=32;q.resource_limits().allocated_samples=16;q.data_sharing().off();}
template<class Q>bool Effective(const Q& q){return q.reliability().kind==dds::RELIABLE_RELIABILITY_QOS&&q.durability().kind==dds::VOLATILE_DURABILITY_QOS&&q.history().kind==dds::KEEP_LAST_HISTORY_QOS&&q.history().depth==16&&q.resource_limits().max_samples==32&&q.resource_limits().max_instances==1&&q.resource_limits().max_samples_per_instance==32&&q.data_sharing().kind()==dds::OFF;}
struct Endpoint {
    Config config;const Control& barrier;Result result;std::shared_ptr<PublicSocketBackend> backend;std::shared_ptr<HostProviderLoop> loop;std::shared_ptr<Descriptor> descriptor;
    dds::TypeSupport type{new MddsKeyedSamplePubSubType};dds::DomainParticipant* participant=nullptr;dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::atomic<bool> stop{false},tail{false},pump_fault{false};std::thread tx,rx;bool deleted=false;
    Endpoint(const Config& c,const Control& b):config(c),barrier(b),backend(std::make_shared<PublicSocketBackend>(BackendConfiguration(c))),loop(std::make_shared<HostProviderLoop>(backend,std::make_shared<Observer>())),descriptor(std::make_shared<Descriptor>(c,loop)){}
    void Failure(const char* p){if(result.first_failure.empty())result.first_failure=p;}
    void Stage(const char* p){Require(barrier&&barrier(p),p);}
    void Wait(const std::function<bool()>& predicate,std::chrono::seconds budget,const char* why){const auto end=std::chrono::steady_clock::now()+budget;while(!predicate()){Require(!pump_fault,"PUMP_FAULT");Require(std::chrono::steady_clock::now()<end,why);std::this_thread::sleep_for(2ms);}Require(!pump_fault,"PUMP_FAULT");}
    void Pumps(){
        tx=std::thread([this]{inspecting=loop.get();while(!stop){auto r=loop->PumpSend();const auto v=loop->Observe();const bool idle=r.outcome==NativeSendOutcome::NotEntered&&r.status==NativeStatus::NotFound;const bool good=r.outcome==NativeSendOutcome::ReturnedSdkCode&&r.actual_return_code==0;const bool terminal=ExpectedTerminal(r,v,tail);
            if(!idle&&!good){std::printf("DDS_TX_RESULT side=%u status=%u outcome=%u code_present=%d code=%d terminal_class=%d sealed=%d outgoing_stopped=%d\n",config.side,unsigned(r.status),unsigned(r.outcome),bool(r.actual_return_code),r.actual_return_code.value_or(-999),terminal,v.sealed,v.adapter.outgoing_stopped);if(!terminal)pump_fault=true;}std::this_thread::sleep_for(1ms);}inspecting=nullptr;});
        rx=std::thread([this]{while(!stop){auto r=loop->PumpReceive();auto v=loop->Observe();const bool good=r==BackendStatus::None||r==BackendStatus::NoWork;const bool terminal=tail&&(v.sealed||!v.input_open||v.adapter.accepted_phase==AcceptedPhase::Stopping||v.adapter.accepted_phase==AcceptedPhase::Closed)&&(r==BackendStatus::InvalidState||r==BackendStatus::ChannelClosed||r==BackendStatus::ProviderStopped||r==BackendStatus::BindingRejected);
            if(!good&&!terminal){std::printf("DDS_RX_ERROR side=%u status=%u\n",config.side,unsigned(r));pump_fault=true;}std::this_thread::sleep_for(1ms);}});
    }
    std::set<std::string> Matches(bool readers){std::vector<dds::InstanceHandle_t> h;Require((readers?writer->get_matched_subscriptions(h):reader->get_matched_publications(h))==dds::RETCODE_OK,"MATCH_QUERY");std::set<std::string> out;for(auto& x:h)out.insert(Hex(x.value,16));return out;}
    void Create(){Require(backend->Kind()==BackendKind::DSoftBusProvider,"REAL_KIND");dds::DomainParticipantQos q;q.name("NewPublicSocket"+config.run+(config.side?"B":"A"));q.transport().use_builtin_transports=false;q.transport().user_transports={descriptor};q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(Local(config.side));w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(Local(config.side));w.default_multicast_locator_list.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(Local(1-config.side));w.builtin.discovery_config.leaseDuration_announcementperiod={1,0};w.builtin.discovery_config.initial_announcements.count=5;w.builtin.discovery_config.initial_announcements.period={0,200000000};
        participant=dds::DomainParticipantFactory::get_instance()->create_participant(151,q);std::printf("DDS_CREATE side=%u non_null=%d\n",config.side,participant!=nullptr);std::fflush(stdout);Require(participant,"PARTICIPANT_NULL");result.factories=descriptor->factories;Require(result.factories==1&&loop->Observe().input_open,"FACTORY_INPUT_NOT_READY");result.participant=Guid(participant->guid());auto effective=participant->get_qos();Require(!effective.transport().use_builtin_transports&&effective.transport().user_transports.size()==1&&effective.transport().user_transports[0]==descriptor,"EFFECTIVE_TRANSPORT");bool no_local=false;for(auto& p:effective.properties().properties())if(p.name()=="fastdds.ignore_local_endpoints"&&p.value()=="true")no_local=true;Require(no_local,"IGNORE_LOCAL_EFFECTIVE");const auto one=[](const auto& list,const Locator& l){return list.size()==1&&*list.begin()==l;};const auto& wire=effective.wire_protocol();Require(one(wire.builtin.metatrafficUnicastLocatorList,Local(config.side))&&one(wire.default_unicast_locator_list,Local(config.side))&&one(wire.builtin.initialPeersList,Local(1-config.side))&&wire.builtin.metatrafficMulticastLocatorList.empty()&&wire.default_multicast_locator_list.empty(),"EFFECTIVE_LOCATORS");
        Pumps();Wait([&]{auto a=loop->Observe().adapter;return a.phase==AdapterPhase::Published&&a.accepted_phase==AcceptedPhase::Active;},15s,"ACCEPTED_READY");auto v=loop->Observe();std::printf("DDS_NATIVE_READY side=%u listener=%d outgoing=%d accepted=%d record=%llu owner=%llu app=%llu receiver=%llu\n",config.side,v.adapter.listener.socket,v.adapter.binding.socket,v.adapter.accepted.socket,(unsigned long long)v.adapter.accepted.record_incarnation,(unsigned long long)v.adapter.accepted.owner_incarnation,(unsigned long long)v.adapter.accepted.receiver_identity.application_incarnation,(unsigned long long)v.adapter.accepted.receiver_identity.receiver_incarnation);Stage("NATIVE_READY");
        Require(type.register_type(participant)==dds::RETCODE_OK,"REGISTER_TYPE");auto topic=participant->create_topic("NewPublicSocketBounded",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);auto pub=participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);auto sub=participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);Require(topic&&pub&&sub,"ENTITIES");auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;EndpointQos(wq);EndpointQos(rq);wq.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;writer=pub->create_datawriter(topic,wq);reader=sub->create_datareader(topic,rq);Require(writer&&reader,"ENDPOINTS");Require(Effective(writer->get_qos())&&Effective(reader->get_qos())&&writer->get_qos().publish_mode().kind==dds::SYNCHRONOUS_PUBLISH_MODE,"EFFECTIVE_QOS");result.writer=Guid(writer->guid());result.reader=Guid(reader->guid());
        std::printf("DDS_IDENTITIES side=%u participant=%s writer=%s reader=%s\n",config.side,result.participant.c_str(),result.writer.c_str(),result.reader.c_str());std::printf("DDS_CONFIG side=%u domain=151 builtin=0 intraprocess=0 data_sharing=0 ignore_local=1 reliability=RELIABLE durability=VOLATILE history=16 max_samples=32 max_instances=1 max_per_instance=32 max_record=8192 payload=32 local=%s peer=%s meta_user_same=1\n",config.side,Hex(Local(config.side).address,16).c_str(),Hex(Local(1-config.side).address,16).c_str());std::fflush(stdout);
    }
    void Data(){Wait([&]{auto readers=Matches(true),writers=Matches(false);if(readers.size()!=1||writers.size()!=1)return false;result.remote_reader=*readers.begin();result.remote_writer=*writers.begin();return result.remote_reader.substr(0,24)!=result.participant.substr(0,24)&&result.remote_writer.substr(0,24)!=result.participant.substr(0,24);},20s,"DDS_REMOTE_MATCH");std::printf("DDS_MATCH side=%u writer=%s reader=%s remote_writer=%s remote_reader=%s\n",config.side,result.writer.c_str(),result.reader.c_str(),result.remote_writer.c_str(),result.remote_reader.c_str());std::fflush(stdout);Stage("MATCH");const auto run=std::stoull(config.run,nullptr,16);
        for(unsigned n=1;n<=5;++n){MddsKeyedSample s;s.key(1);s.run(run);s.writer(config.side+1);s.ordinal(n);for(unsigned k=0;k<32;++k)s.payload()[k]=std::uint8_t(n+k+config.side);rtps::WriteParams p;auto rc=writer->write(&s,p);std::printf("DDS_WRITE side=%u ordinal=%u writer=%s sn=%llu rc=%d serialized=%u\n",config.side,n,Guid(p.sample_identity().writer_guid()).c_str(),(unsigned long long)Sequence(p.sample_identity().sequence_number()),int(rc),type->calculate_serialized_size(&s,dds::XCDR_DATA_REPRESENTATION));Require(rc==dds::RETCODE_OK&&Sequence(p.sample_identity().sequence_number())>0&&Guid(p.sample_identity().writer_guid())==result.writer,"DDS_WRITE");++result.writes;}
        std::set<unsigned> ordinals;std::set<std::uint64_t> seq;Wait([&]{for(;;){MddsKeyedSample s;dds::SampleInfo info;auto rc=reader->take_next_sample(&s,&info);if(rc==dds::RETCODE_NO_DATA)break;Require(rc==dds::RETCODE_OK,"TAKE_RC");if(!info.valid_data)continue;bool valid=s.run()==run&&s.key()==1&&s.writer()==2-config.side&&s.ordinal()>=1&&s.ordinal()<=5&&Guid(info.sample_identity.writer_guid())==result.remote_writer;for(unsigned k=0;k<32;++k)valid=valid&&s.payload()[k]==std::uint8_t(s.ordinal()+k+1-config.side);auto sn=Sequence(info.sample_identity.sequence_number());valid=valid&&sn>0&&ordinals.insert(s.ordinal()).second&&seq.insert(sn).second;std::printf("DDS_TAKE side=%u run=%s ordinal=%u reader=%s writer=%s sn=%llu valid=%d\n",config.side,config.run.c_str(),s.ordinal(),result.reader.c_str(),Guid(info.sample_identity.writer_guid()).c_str(),(unsigned long long)sn,valid);Require(valid,"READER_LEDGER");++result.takes;}return result.takes==5;},10s,"READER_MISSING");auto ack=writer->wait_for_acknowledgments({5,0});std::printf("DDS_WAIT side=%u rc=%d meaning=DDS_API_NOT_PACKET_COUNT\n",config.side,int(ack));Require(ack==dds::RETCODE_OK,"DDS_WAIT");
#ifdef MDDS_DDS_EXPERIMENT_TESTING
        if(after_wait_hook)after_wait_hook(writer);
#endif
        const auto quiet_end=std::chrono::steady_clock::now()+100ms;do{RejectExtras("AFTER_ACK_EXTRA");std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<quiet_end);RejectExtras("AFTER_ACK_EXTRA");std::printf("DDS_POST_ACK side=%u observation_ms=100 valid_extra=0\n",config.side);
        Require(Matches(true)==std::set<std::string>{result.remote_reader}&&Matches(false)==std::set<std::string>{result.remote_writer},"CURRENT_MATCH_LOST");tail=true;Stage("DATA_DONE");
    }
    void RejectExtras(const char* why){
        for(unsigned budget=0;budget<32;++budget){MddsKeyedSample s;dds::SampleInfo info;auto rc=reader->take_next_sample(&s,&info);if(rc==dds::RETCODE_NO_DATA)return;Require(rc==dds::RETCODE_OK,"EXTRA_TAKE_RC");if(info.valid_data){std::printf("DDS_EXTRA side=%u reader=%s writer=%s sn=%llu ordinal=%u\n",config.side,result.reader.c_str(),Guid(info.sample_identity.writer_guid()).c_str(),(unsigned long long)Sequence(info.sample_identity.sequence_number()),s.ordinal());throw std::runtime_error(why);}}
        throw std::runtime_error("EXCESS_LIFECYCLE_NOTICES");
    }
    bool Cleanup() noexcept {
        tail=true;bool ok=true;if(participant){std::printf("DDS_DELETE_BEGIN side=%u participant=%s\n",config.side,result.participant.c_str());std::fflush(stdout);auto contained=participant->delete_contained_entities();auto removed=dds::DomainParticipantFactory::get_instance()->delete_participant(participant);ok=contained==dds::RETCODE_OK&&removed==dds::RETCODE_OK;participant=nullptr;writer=nullptr;reader=nullptr;std::printf("DDS_DELETE_RETURN side=%u contained_rc=%d participant_rc=%d\n",config.side,int(contained),int(removed));}
        auto v=loop->Observe();const bool fallback=!v.closed;if(fallback){std::printf("DDS_EXCEPTION_FINALIZER side=%u explicit_loop_shutdown=1\n",config.side);loop->Shutdown();v=loop->Observe();}
        stop=true;if(tx.joinable())tx.join();if(rx.joinable())rx.join();const bool settled=v.closed&&!v.input_open&&v.active_pumps==0&&v.close.adapter_drained&&v.close.cleanup_status==BackendStatus::None&&v.receive.records==0&&v.receive.active_applications==0&&v.receive.retained_applications==0&&v.tx.records==0&&v.native.callbacks==0&&v.native.receives==0;
        result.drained=ok&&settled;deleted=true;std::printf("DDS_LOCAL_SETTLE side=%u settled=%d fallback=%d input_close=%llu shutdown=%llu first_close=%llu first_shutdown=%llu receive_records=%zu send_records=%zu native_terminal_metadata=%zu sdk_drain=UNPROVEN\n",config.side,result.drained,fallback,(unsigned long long)v.close_calls,(unsigned long long)v.shutdown_calls,(unsigned long long)v.first_close_order,(unsigned long long)v.first_shutdown_order,v.receive.records,v.tx.records,v.native.owners);std::fflush(stdout);return result.drained&&v.close.status==BackendStatus::None&&!fallback&&v.close_calls>0&&v.shutdown_calls>0;
    }
    ~Endpoint(){if(!deleted)Cleanup();}
};
}
#ifdef MDDS_DDS_EXPERIMENT_TESTING
void SetAfterWaitHook(std::function<void(dds::DataWriter*)> h){after_wait_hook=std::move(h);}
#endif
bool ExpectedTerminal(const NativeSendRunResult& r,const HostLoopObservation& v,bool tail) noexcept {return tail&&(v.sealed||v.adapter.outgoing_stopped)&&r.outcome==NativeSendOutcome::NotEntered&&(r.status==NativeStatus::ServiceClosing||r.status==NativeStatus::Revoked||r.status==NativeStatus::NotReady);}
bool Valid(const Config& c) noexcept {if(c.side>1||c.delete_first>1||c.run.size()!=16||c.peer_network.empty()||c.peer_network.size()>64)return false;for(char v:c.run)if(!((v>='0'&&v<='9')||(v>='a'&&v<='f')))return false;return c.service_prefix=="com.kaihong.mdds.p"||c.service_prefix=="host.public";}
bool IsolateProcess() noexcept {try{for(const char* p:{"FASTDDS_DEFAULT_PROFILES_FILE","FASTRTPS_DEFAULT_PROFILES_FILE","FASTDDS_ENVIRONMENT_FILE","ROS_DISCOVERY_SERVER","ROS_SUPER_CLIENT","FASTDDS_BUILTIN_TRANSPORTS","FASTDDS_STATISTICS"}){
#ifdef _WIN32
    if(_putenv_s(p,""))return false;
#else
    if(unsetenv(p))return false;
#endif
    }
#ifdef _WIN32
    if(_putenv_s("SKIP_DEFAULT_XML","1"))return false;
#else
    if(setenv("SKIP_DEFAULT_XML","1",1))return false;
#endif
    eprosima::fastdds::LibrarySettings settings;settings.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;auto f=dds::DomainParticipantFactory::get_instance();return f->set_library_settings(settings)==dds::RETCODE_OK&&f->get_library_settings(settings)==dds::RETCODE_OK&&settings.intraprocess_delivery==eprosima::fastdds::INTRAPROCESS_OFF;
}catch(...){return false;}}
Result Run(const Config& c,const Control& b) noexcept {Result failed;if(!Valid(c)){failed.first_failure="CONFIG";failed.drained=true;return failed;}control=&b;setup_control_failed=false;try{Endpoint endpoint(c,b);try{endpoint.Create();endpoint.Data();endpoint.Stage("DELETE");endpoint.RejectExtras("PRE_DELETE_EXTRA");Require(endpoint.Cleanup(),"DDS_CLEANUP");endpoint.Stage("DDS_DELETED");Require(!endpoint.pump_fault,"PUMP_FAULT");Require(FrameOverflow()==0,"FRAME_OVERFLOW");endpoint.result.success=true;}catch(const std::exception& e){endpoint.Failure(setup_control_failed?"CONTROL_LISTENER_READY":e.what());if(!endpoint.deleted)endpoint.Cleanup();}catch(...){endpoint.Failure("UNKNOWN_EXCEPTION");if(!endpoint.deleted)endpoint.Cleanup();}control=nullptr;std::printf("DDS_RESULT side=%u success=%d writes=%u takes=%u drained=%d first_failure=%s\n",c.side,endpoint.result.success,endpoint.result.writes,endpoint.result.takes,endpoint.result.drained,endpoint.result.first_failure.c_str());std::fflush(stdout);return endpoint.result;}catch(...){control=nullptr;failed.first_failure="CONSTRUCTION_EXCEPTION";return failed;}}
void SocketFrame(const char* stage,std::int32_t id,const std::uint8_t* p,std::size_t n) noexcept {FrameBytes(stage,id,p,n);}
std::size_t FrameOverflow() noexcept {auto& s=Store();std::lock_guard<std::mutex> l(s.gate);return s.overflow;}
std::size_t FrameCount() noexcept {auto& s=Store();std::lock_guard<std::mutex> l(s.gate);return s.count;}
void DumpFrames() noexcept {try{std::vector<Frame> rows;std::size_t overflow;auto& s=Store();{std::lock_guard<std::mutex> l(s.gate);rows.assign(s.rows.begin(),s.rows.begin()+s.count);overflow=s.overflow;}unsigned ordinal=0;for(auto& e:rows)std::printf("DDS_FRAME event=%u packet=%llu stage=%s kind=%s socket=%d length=%zu hash=%llu writer=%s sn=%llu correlation=%llu app=%llu receiver=%llu owner=%llu admitted=%llu entered=%llu\n",++ordinal,(unsigned long long)e.packet,e.stage,e.kind,e.socket,e.length,(unsigned long long)e.hash,Hex(e.writer.data(),16).c_str(),(unsigned long long)e.sn,(unsigned long long)e.correlation,(unsigned long long)e.app,(unsigned long long)e.receiver,(unsigned long long)e.owner,(unsigned long long)e.admitted,(unsigned long long)e.entered);std::printf("DDS_FRAME_SUMMARY count=%zu limit=4096 overflow=%zu storage_bytes=%zu\n",rows.size(),overflow,sizeof(Frames));}catch(...){auto& s=Store();std::lock_guard<std::mutex> l(s.gate);++s.overflow;}}
}
namespace mdds::dsoftbus::phase0 {bool RuntimeProbeBeforeConnect() noexcept {try{const bool ok=dds_experiment::control&&(*dds_experiment::control)("LISTENER_READY");if(!ok)dds_experiment::setup_control_failed=true;return ok;}catch(...){dds_experiment::setup_control_failed=true;return false;}}}
