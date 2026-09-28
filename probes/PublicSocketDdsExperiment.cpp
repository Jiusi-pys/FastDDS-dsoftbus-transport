#include "PublicSocketDdsExperiment.hpp"
#include "SharedSendControl.hpp"
#include "PublicSocketDdsTrace.hpp"
#include "PublicSocketDecisionTrace.hpp"
#include "PublicSocketRuntimeTrace.hpp"
#include "PublicSocketRtpsObservation.hpp"
#include "../experiments/fastdds_socket/LargeSampleContract.hpp"
#include "ExperimentLargeSamplePubSubTypes.hpp"
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
thread_local std::function<void()> after_frame_selection;
thread_local bool shared_delete_failure_once=false;
#endif
thread_local HostProviderLoop* inspecting=nullptr;
thread_local std::uint64_t current_ingress=0;
void Require(bool b,const char* p){if(!b)throw std::runtime_error(p);}
std::string Hex(const std::uint8_t* p,std::size_t n){const char* d="0123456789abcdef";std::string r;r.reserve(n*2);for(std::size_t i=0;i<n;++i){r+=d[p[i]>>4];r+=d[p[i]&15];}return r;}
std::string Guid(const rtps::GUID_t& g){return Hex(g.guidPrefix.value,12)+Hex(g.entityId.value,4);}
std::uint64_t Sequence(const rtps::SequenceNumber_t& s){return(std::uint64_t(std::uint32_t(s.high))<<32)|s.low;}
Locator Local(unsigned side){EndpointId id{};id[0]=10;id[14]=128;id[15]=std::uint8_t(side+1);Locator l;Require(LocatorCodec::Encode(id,l),"LOCATOR_ENCODING");return l;}
std::string Name(const Config& c,unsigned side,const char* role){return c.service_prefix+(c.service_prefix=="host.public"?"":c.run)+(side?".B.":".A.")+role;}
DSoftBusTransportDescriptor Configuration(const Config& c){DSoftBusTransportDescriptor d(8192);auto l=Local(c.side),p=Local(1-c.side);d.directory.locals={{l,Name(c,c.side,"namespace"),Name(c,c.side,"listener")}};d.directory.routes={{{l,p},{c.peer_network,Name(c,1-c.side,"listener"),Name(c,c.side,"outgoing"),{Name(c,1-c.side,"outgoing")},RecordDataType::Bytes},1}};d.default_metatraffic_unicast={l};d.default_user_unicast={l};return d;}
PublicSocketConfiguration BackendConfiguration(const Config& c){PublicSocketConfiguration b;b.identity={Name(c,c.side,"outgoing"),{910+c.side*4},{1010+c.side*4}};b.listener_identity={Name(c,c.side,"listener"),{911+c.side*4},{1011+c.side*4}};b.package_name="com.kaihong.mdds";b.peer_name=Name(c,1-c.side,"listener");b.peer_network_id=c.peer_network;b.bind_qos={{QOS_TYPE_MIN_BW,80},{QOS_TYPE_MAX_WAIT_TIMEOUT,2000}};b.listen_qos=b.bind_qos;b.max_message_size=8192;return b;}
#ifdef MDDS_PUBLIC_SOCKET_BOUNDED_REGRESSION
constexpr std::size_t frame_limit=16384;
#else
constexpr std::size_t frame_limit=4096;
#endif
struct Frame {const char* stage=nullptr;const char* kind=nullptr;std::int32_t socket=-1;std::array<std::uint8_t,16> writer{},reader{};std::uint64_t ingress=0,packet=0,sn=0,hash=0,correlation=0,app=0,receiver=0,owner=0,admitted=0,entered=0;std::size_t length=0,offset=0;std::uint32_t first=0,count=0,fragment_size=0,sample_size=0,bits=0;std::array<std::uint32_t,8> bitmap{};bool writer_known=true,reader_known=false;unsigned cycle=0;};
struct Frames {std::mutex gate;std::array<Frame,frame_limit> rows{};std::size_t count=0,overflow=0;std::uint64_t packets=0,omitted=0;};
Frames& Store(){alignas(Frames) static unsigned char memory[sizeof(Frames)];static Frames* p=new(memory) Frames;return *p;}
std::uint64_t FrameBytes(const char* stage,std::int32_t socket,const std::uint8_t* p,std::size_t n,const ReceiveDiagnostic* diagnostic=nullptr) noexcept {
    if(!p||n<20||n>8192)return 0;
    const auto observation_cycle=runtime_probe::ObservationCycle();
    if(observation_cycle>1){auto& s=Store();std::lock_guard<std::mutex> l(s.gate);++s.omitted;return ++s.packets;}
#ifdef MDDS_DDS_EXPERIMENT_TESTING
    if(after_frame_selection)after_frame_selection();
#endif
    const auto seen=mdds::experiment::ObserveRtps(p,n);if(!seen.valid)return 0;
    Frame base;base.cycle=observation_cycle;base.stage=stage;base.socket=socket;base.length=n;base.hash=14695981039346656037ull;for(std::size_t i=0;i<n;++i){base.hash^=p[i];base.hash*=1099511628211ull;}
    if(diagnostic){auto native=diagnostic->native.Observe();base.correlation=diagnostic->correlation;base.app=diagnostic->identity.application_incarnation;base.receiver=diagnostic->identity.receiver_incarnation;base.owner=native.incarnation;}
    else if(inspecting){auto view=inspecting->Observe();base.owner=view.adapter.binding.owner_incarnation;base.admitted=view.shared_runtime?view.runtime_admitted_outstanding:view.send.admitted_outstanding;base.entered=view.native.send.entered;}
    auto& store=Store();std::lock_guard<std::mutex> l(store.gate);base.packet=++store.packets;base.ingress=std::strcmp(stage,"SDK_INGRESS")==0?base.packet:current_ingress;
    auto save=[&](const Frame& f){if(store.count==store.rows.size())++store.overflow;else store.rows[store.count++]=f;};
    for(const auto& x:seen.data){auto f=base;f.writer=x.writer_guid;const auto e=std::array<std::uint8_t,4>{f.writer[12],f.writer[13],f.writer[14],f.writer[15]};f.kind=e==std::array<std::uint8_t,4>{0,1,0,0xc2}?"SPDP":(e==std::array<std::uint8_t,4>{0,0,3,0xc2}||e==std::array<std::uint8_t,4>{0,0,4,0xc2})?"SEDP":"DATA";f.sn=(std::uint64_t(std::uint32_t(x.sequence_hi))<<32)|x.sequence_lo;f.offset=x.submessage_offset;save(f);}
    for(const auto& x:seen.fragments){auto f=base;f.kind="DATA_FRAG";f.writer=x.writer_guid;f.sn=(std::uint64_t(std::uint32_t(x.sequence_hi))<<32)|x.sequence_lo;f.offset=x.submessage_offset;f.first=x.fragment_start;f.count=x.fragment_count;f.fragment_size=x.fragment_size;f.sample_size=x.sample_size;save(f);}
    for(const auto& x:seen.controls){auto f=base;f.kind=x.kind==6?"ACKNACK":x.kind==7?"HEARTBEAT":x.kind==0x12?"NACK_FRAG":"HEARTBEAT_FRAG";f.writer=x.writer_guid;f.reader=x.reader_guid;f.reader_known=x.reader_prefix_known;f.writer_known=x.writer_prefix_known;f.sn=(std::uint64_t(std::uint32_t(x.sequence_hi))<<32)|x.sequence_lo;f.offset=x.submessage_offset;f.first=x.fragment_base;f.bits=x.num_bits;f.bitmap=x.bitmap;save(f);}
    return base.packet;
}
struct DropState {std::mutex gate;bool armed=false,hit=false;int socket=-1;std::string writer;std::chrono::steady_clock::time_point hit_time{};};
DropState& Drop(){alignas(DropState) static unsigned char memory[sizeof(DropState)];static DropState* value=new(memory) DropState;return *value;}
void ArmDrop(int socket,const std::string& writer){auto& d=Drop();std::lock_guard<std::mutex> l(d.gate);Require(!d.armed&&socket>=0&&writer.size()==32,"DROP_ARM");d.socket=socket;d.writer=writer;d.armed=true;std::printf("DDS_DROP_ARM socket=%d writer=%s sn=1 fragment=3\n",socket,writer.c_str());}
std::chrono::steady_clock::time_point DropTime(){auto& d=Drop();std::lock_guard<std::mutex> l(d.gate);return d.hit_time;}
bool DropHit(){auto& d=Drop();std::lock_guard<std::mutex> l(d.gate);return d.hit;}
struct Observer final:ProcessReceiveObserver {unsigned side;explicit Observer(unsigned s):side(s){}
    void OnDecision(const ReceiveDecision& d) noexcept override {RecordDecision(d,current_ingress,side);}
    void OnReceive(ReceiveStage stage,const ReceiveDiagnostic& d,const std::uint8_t* p,std::size_t n)noexcept override{
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
    bool Regression()const{return config.scenario!="SMALL";}
    bool Large()const{return config.scenario=="F256"||config.scenario=="F1M"||config.scenario=="REL_LOSS"||config.scenario=="BE_LOSS";}
    bool Late()const{return config.scenario=="VOL_LATE"||config.scenario=="TL_LATE"||config.scenario=="KEYED_LAST";}
    bool BestEffort()const{return config.scenario=="BE_LOSS";}
    std::size_t Payload()const{return config.scenario=="F1M"?1048576:262144;}
    dds::Topic* topic=nullptr;dds::Subscriber* sub=nullptr;
    dds::TypeSupport type;dds::DomainParticipant* participant=nullptr;dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
    std::atomic<bool> stop{false},tail{false},pump_fault{false};std::thread tx,rx;bool deleted=false;
    Endpoint(const Config& c,const Control& b):config(c),barrier(b),backend(std::make_shared<PublicSocketBackend>(BackendConfiguration(c))),loop(std::make_shared<HostProviderLoop>(backend,std::make_shared<Observer>(c.side))),descriptor(std::make_shared<Descriptor>(c,loop)),type((c.scenario=="F256"||c.scenario=="F1M"||c.scenario=="REL_LOSS"||c.scenario=="BE_LOSS")?static_cast<dds::TopicDataType*>(new ExperimentLargeSamplePubSubType):static_cast<dds::TopicDataType*>(new MddsKeyedSamplePubSubType)){}
    void Failure(const char* p){if(result.first_failure.empty())result.first_failure=p;}
    void Stage(const char* p){Require(barrier&&barrier(p),p);}
    void Wait(const std::function<bool()>& predicate,std::chrono::seconds budget,const char* why){const auto end=std::chrono::steady_clock::now()+budget;while(!predicate()){Require(!pump_fault,"PUMP_FAULT");Require(std::chrono::steady_clock::now()<end,why);std::this_thread::sleep_for(2ms);}Require(!pump_fault,"PUMP_FAULT");}
    void Pumps(){
        tx=std::thread([this]{inspecting=loop.get();while(!stop){auto r=loop->PumpSend();const auto v=loop->Observe();const bool idle=r.outcome==NativeSendOutcome::NotEntered&&r.status==NativeStatus::NotFound;const bool good=r.outcome==NativeSendOutcome::ReturnedSdkCode&&r.actual_return_code==0;const bool terminal=ExpectedTerminal(r,v,tail);
            if(!idle&&!good){std::printf("DDS_TX_RESULT side=%u status=%u outcome=%u code_present=%d code=%d terminal_class=%d sealed=%d outgoing_stopped=%d\n",config.side,unsigned(r.status),unsigned(r.outcome),bool(r.actual_return_code),r.actual_return_code.value_or(-999),terminal,v.sealed,v.adapter.outgoing_stopped);if(!terminal)pump_fault=true;}std::this_thread::sleep_for(1ms);}inspecting=nullptr;});
        rx=std::thread([this]{while(!stop){auto r=loop->PumpReceive();auto v=loop->Observe();const bool good=r==BackendStatus::None||r==BackendStatus::NoWork;const bool terminal=tail&&(v.sealed||!v.input_open||v.adapter.accepted_phase==AcceptedPhase::Stopping||v.adapter.accepted_phase==AcceptedPhase::Closed)&&(r==BackendStatus::InvalidState||r==BackendStatus::ChannelClosed||r==BackendStatus::ProviderStopped||r==BackendStatus::BindingRejected);
            if(!good&&!terminal){std::printf("DDS_RX_ERROR side=%u status=%u\n",config.side,unsigned(r));pump_fault=true;}if(!Regression()||r==BackendStatus::NoWork)std::this_thread::sleep_for(1ms);}});
    }
    std::set<std::string> Matches(bool readers){std::vector<dds::InstanceHandle_t> h;Require((readers?writer->get_matched_subscriptions(h):reader->get_matched_publications(h))==dds::RETCODE_OK,"MATCH_QUERY");std::set<std::string> out;for(auto& x:h)out.insert(Hex(x.value,16));return out;}
    void Create(){Require(backend->Kind()==BackendKind::DSoftBusProvider,"REAL_KIND");dds::DomainParticipantQos q;q.name("NewPublicSocket"+config.run+(config.side?"B":"A"));q.transport().use_builtin_transports=false;q.transport().user_transports={descriptor};q.properties().properties().emplace_back("fastdds.ignore_local_endpoints","true");auto& w=q.wire_protocol();w.builtin.metatrafficMulticastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.clear();w.builtin.metatrafficUnicastLocatorList.push_back(Local(config.side));w.default_unicast_locator_list.clear();w.default_unicast_locator_list.push_back(Local(config.side));w.default_multicast_locator_list.clear();w.builtin.initialPeersList.clear();w.builtin.initialPeersList.push_back(Local(1-config.side));w.builtin.discovery_config.leaseDuration_announcementperiod={1,0};w.builtin.discovery_config.initial_announcements.count=5;w.builtin.discovery_config.initial_announcements.period={0,200000000};
        participant=dds::DomainParticipantFactory::get_instance()->create_participant(151,q);std::printf("DDS_CREATE side=%u non_null=%d\n",config.side,participant!=nullptr);std::fflush(stdout);Require(participant,"PARTICIPANT_NULL");result.factories=descriptor->factories;Require(result.factories==1&&loop->Observe().input_open,"FACTORY_INPUT_NOT_READY");result.participant=Guid(participant->guid());auto effective=participant->get_qos();Require(!effective.transport().use_builtin_transports&&effective.transport().user_transports.size()==1&&effective.transport().user_transports[0]==descriptor,"EFFECTIVE_TRANSPORT");bool no_local=false;for(auto& p:effective.properties().properties())if(p.name()=="fastdds.ignore_local_endpoints"&&p.value()=="true")no_local=true;Require(no_local,"IGNORE_LOCAL_EFFECTIVE");const auto one=[](const auto& list,const Locator& l){return list.size()==1&&*list.begin()==l;};const auto& wire=effective.wire_protocol();Require(one(wire.builtin.metatrafficUnicastLocatorList,Local(config.side))&&one(wire.default_unicast_locator_list,Local(config.side))&&one(wire.builtin.initialPeersList,Local(1-config.side))&&wire.builtin.metatrafficMulticastLocatorList.empty()&&wire.default_multicast_locator_list.empty(),"EFFECTIVE_LOCATORS");
        Pumps();Wait([&]{auto a=loop->Observe().adapter;return a.phase==AdapterPhase::Published&&a.accepted_phase==AcceptedPhase::Active;},15s,"ACCEPTED_READY");auto v=loop->Observe();std::printf("DDS_NATIVE_READY side=%u listener=%d outgoing=%d accepted=%d record=%llu owner=%llu app=%llu receiver=%llu\n",config.side,v.adapter.listener.socket,v.adapter.binding.socket,v.adapter.accepted.socket,(unsigned long long)v.adapter.accepted.record_incarnation,(unsigned long long)v.adapter.accepted.owner_incarnation,(unsigned long long)v.adapter.accepted.receiver_identity.application_incarnation,(unsigned long long)v.adapter.accepted.receiver_identity.receiver_incarnation);Stage("NATIVE_READY");
        Require(type.register_type(participant)==dds::RETCODE_OK,"REGISTER_TYPE");topic=participant->create_topic("NewPublicSocketBounded",type.get_type_name(),dds::TOPIC_QOS_DEFAULT);auto pub=participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);sub=participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);Require(topic&&pub&&sub,"ENTITIES");auto wq=dds::DATAWRITER_QOS_DEFAULT;auto rq=dds::DATAREADER_QOS_DEFAULT;EndpointQos(wq);EndpointQos(rq);if(Regression()){RegressionQos(wq,false);RegressionQos(rq,true);}wq.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;writer=pub->create_datawriter(topic,wq);if(!(Regression()&&Late()&&config.side==1))reader=sub->create_datareader(topic,rq);Require(writer&&(reader||(Regression()&&Late()&&config.side==1)),"ENDPOINTS");Require((Regression()?RegressionEffective(writer->get_qos(),false):(Effective(writer->get_qos())&&Effective(reader->get_qos())))&&writer->get_qos().publish_mode().kind==dds::SYNCHRONOUS_PUBLISH_MODE,"EFFECTIVE_QOS");if(Regression()&&reader)Require(RegressionEffective(reader->get_qos(),true),"READER_EFFECTIVE_QOS");result.writer=Guid(writer->guid());if(reader)result.reader=Guid(reader->guid());
        if(!Regression()){
        std::printf("DDS_IDENTITIES side=%u participant=%s writer=%s reader=%s\n",config.side,result.participant.c_str(),result.writer.c_str(),result.reader.c_str());std::printf("DDS_CONFIG side=%u domain=151 builtin=0 intraprocess=0 data_sharing=0 ignore_local=1 reliability=RELIABLE durability=VOLATILE history=16 max_samples=32 max_instances=1 max_per_instance=32 max_record=8192 payload=32 local=%s peer=%s meta_user_same=1\n",config.side,Hex(Local(config.side).address,16).c_str(),Hex(Local(1-config.side).address,16).c_str());std::fflush(stdout);
        }
    }
    template<class Q>void RegressionQos(Q& q,bool receive){
        q.reliability().kind=BestEffort()?dds::BEST_EFFORT_RELIABILITY_QOS:dds::RELIABLE_RELIABILITY_QOS;
        q.durability().kind=config.scenario=="TL_LATE"||config.scenario=="KEYED_LAST"?dds::TRANSIENT_LOCAL_DURABILITY_QOS:dds::VOLATILE_DURABILITY_QOS;
        q.history().kind=receive?dds::KEEP_ALL_HISTORY_QOS:dds::KEEP_LAST_HISTORY_QOS;
        q.history().depth=!receive&&config.scenario=="KEYED_LAST"?2:16;
        q.resource_limits().max_samples=64;q.resource_limits().max_instances=4;q.resource_limits().max_samples_per_instance=16;q.resource_limits().allocated_samples=16;
    }
    template<class Q>bool RegressionEffective(const Q& q,bool receive){Q expected=q;RegressionQos(expected,receive);return q.reliability().kind==expected.reliability().kind&&q.durability().kind==expected.durability().kind&&q.history().kind==expected.history().kind&&q.history().depth==expected.history().depth&&q.resource_limits().max_samples==64&&q.resource_limits().max_instances==4&&q.resource_limits().max_samples_per_instance==16&&q.data_sharing().kind()==dds::OFF;}
    std::set<std::pair<unsigned,unsigned>> received;std::set<std::uint64_t> sample_sequences;
    void WriteSample(unsigned ordinal,unsigned key=1){
        rtps::WriteParams identity;dds::ReturnCode_t rc;std::uint32_t serialized=0;std::uint64_t hash=0;
        if(Large()){ExperimentLargeSample sample;Require(mdds::experiment::FillLargeSample(config.run,ordinal,Payload(),sample),"LARGE_FILL");serialized=type->calculate_serialized_size(&sample,dds::XCDR_DATA_REPRESENTATION);hash=mdds::experiment::LargeSampleHash(sample);rc=writer->write(&sample,identity);}
        else{MddsKeyedSample sample;sample.key(key);sample.run(std::stoull(config.run,nullptr,16));sample.writer(1);sample.ordinal(ordinal);for(unsigned k=0;k<32;++k)sample.payload()[k]=std::uint8_t(ordinal+k+key);hash=14695981039346656037ull;for(auto byte:sample.payload()){hash^=byte;hash*=1099511628211ull;}serialized=type->calculate_serialized_size(&sample,dds::XCDR_DATA_REPRESENTATION);rc=writer->write(&sample,identity);}
        auto sn=Sequence(identity.sample_identity().sequence_number());std::printf("REG_WRITE run=%s case=%s side=%u ordinal=%u key=%u writer=%s sn=%llu rc=%d payload=%zu serialized=%u hash=%llu\n",config.run.c_str(),config.scenario.c_str(),config.side,ordinal,key,Guid(identity.sample_identity().writer_guid()).c_str(),(unsigned long long)sn,int(rc),Large()?Payload():std::size_t(32),serialized,(unsigned long long)hash);std::fflush(stdout);
        Require(rc==dds::RETCODE_OK&&sn>0&&Guid(identity.sample_identity().writer_guid())==result.writer,"REG_WRITE");++result.writes;
    }
    bool TakeOne(){
        dds::SampleInfo info;unsigned ordinal=0,key=1;bool valid=false;std::uint64_t hash=0;dds::ReturnCode_t rc;
        if(Large()){ExperimentLargeSample sample;rc=reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_OK&&info.valid_data){ordinal=sample.index();valid=mdds::experiment::LargeSampleWriteReady(config.run,Payload(),sample);hash=mdds::experiment::LargeSampleHash(sample);}}
        else{MddsKeyedSample sample;rc=reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_OK&&info.valid_data){ordinal=sample.ordinal();key=sample.key();valid=sample.run()==std::stoull(config.run,nullptr,16)&&sample.writer()==1;for(unsigned k=0;k<32;++k)valid=valid&&sample.payload()[k]==std::uint8_t(ordinal+k+key);hash=14695981039346656037ull;for(auto byte:sample.payload()){hash^=byte;hash*=1099511628211ull;}}}
        if(rc==dds::RETCODE_NO_DATA)return false;Require(rc==dds::RETCODE_OK,"REG_TAKE_RC");if(!info.valid_data)return true;
        const auto sn=Sequence(info.sample_identity.sequence_number());valid=valid&&config.side==1&&Guid(info.sample_identity.writer_guid())==result.remote_writer&&sn>0&&received.insert({key,ordinal}).second&&sample_sequences.insert(sn).second;
        std::printf("REG_TAKE run=%s case=%s side=%u reader=%s writer=%s sn=%llu key=%u ordinal=%u instance=%s valid=%d hash=%llu\n",config.run.c_str(),config.scenario.c_str(),config.side,result.reader.c_str(),Guid(info.sample_identity.writer_guid()).c_str(),(unsigned long long)sn,key,ordinal,Hex(info.instance_handle.value,16).c_str(),valid,(unsigned long long)hash);std::fflush(stdout);Require(valid,"REG_LEDGER_IDENTITY");++result.takes;return true;
    }
    void Collect(const std::set<std::pair<unsigned,unsigned>>& expected){Wait([&]{while(TakeOne()){}for(auto x:received)Require(expected.count(x)!=0,"REG_UNEXPECTED_SAMPLE");return received==expected;},15s,"REG_READER_MISSING");}
    void RegressionExtras(){const auto count=result.takes;if(reader)while(TakeOne()){}Require(result.takes==count,"REG_EXTRA_SAMPLE");}
    void Admission(const char* stage){const auto a=loop->Observe().adapter;const auto& b=a.accepted;std::printf("REG_ADMISSION side=%u stage=%s admitted=%zu refused_phase=%zu refused_native=%zu refused_receive=%zu refused_no_route=%zu adapter_rejected=%zu sdk_callback_rejected=%llu\n",config.side,stage,b.admitted,b.refused_phase,b.refused_native,b.refused_receive,b.refused_no_route,a.ingress_rejected,(unsigned long long)backend->Observe().rejected_callbacks);}
    void RegressionData(){
        if(Late()){
            if(config.side==0){for(unsigned key=1;key<=(config.scenario=="KEYED_LAST"?2u:1u);++key)for(unsigned n=1;n<=(config.scenario=="KEYED_LAST"?4u:3u);++n)WriteSample(n,key);}
            Stage("PREWRITE_DONE");
            if(config.side==1){auto q=dds::DATAREADER_QOS_DEFAULT;EndpointQos(q);RegressionQos(q,true);reader=sub->create_datareader(topic,q);Require(reader&&RegressionEffective(reader->get_qos(),true),"LATE_READER_CREATE");result.reader=Guid(reader->guid());std::printf("REG_READER_CREATED run=%s reader=%s after_prewrite=1\n",config.run.c_str(),result.reader.c_str());}
        }
        std::printf("DDS_IDENTITIES side=%u participant=%s writer=%s reader=%s\n",config.side,result.participant.c_str(),result.writer.c_str(),result.reader.c_str());
        Wait([&]{auto rs=Matches(true),ws=Matches(false);if(rs.size()!=1||ws.size()!=1)return false;result.remote_reader=*rs.begin();result.remote_writer=*ws.begin();return result.remote_reader.substr(0,24)!=result.participant.substr(0,24)&&result.remote_writer.substr(0,24)!=result.participant.substr(0,24);},20s,"DDS_REMOTE_MATCH");
        std::printf("DDS_MATCH side=%u writer=%s reader=%s remote_writer=%s remote_reader=%s\n",config.side,result.writer.c_str(),result.reader.c_str(),result.remote_writer.c_str(),result.remote_reader.c_str());
        if(config.side==1&&(config.scenario=="REL_LOSS"||BestEffort()))ArmDrop(loop->Observe().adapter.accepted.socket,result.remote_writer);
        std::printf("REG_CONFIG case=%s side=%u reliability=%s durability=%s writer_depth=%d reader_history=KEEP_ALL max_samples=64 max_instances=4 max_per_instance=16 max_record=8192 payload=%zu\n",config.scenario.c_str(),config.side,BestEffort()?"BEST_EFFORT":"RELIABLE",config.scenario=="TL_LATE"||config.scenario=="KEYED_LAST"?"TRANSIENT_LOCAL":"VOLATILE",config.scenario=="KEYED_LAST"?2:16,Large()?Payload():std::size_t(32));std::fflush(stdout);Admission("MATCH");Stage("MATCH");
        std::set<std::pair<unsigned,unsigned>> expected;
        if(Large())for(unsigned n=1;n<=3;++n){
            if(config.side==0)WriteSample(n);
            else if(BestEffort()&&n==1){Wait([&]{return DropHit();},15s,"DROP_NOT_EXERCISED");const auto start=std::chrono::steady_clock::now();const auto end=start+3s;do{while(TakeOne()){}Require(received.empty(),"BE_TARGET_DELIVERED");std::this_thread::sleep_for(2ms);}while(std::chrono::steady_clock::now()<end);Require(DropHit(),"DROP_NOT_EXERCISED");result.absence_after_drop_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-DropTime()).count();std::printf("REG_ABSENCE run=%s ordinal=1 window_ms=3000 completed=1 started_after_drop=1 after_drop_us=%llu\n",config.run.c_str(),(unsigned long long)result.absence_after_drop_us);}
            else if(config.side==1){expected.insert({1,n});Collect(expected);}
            const auto stage="SAMPLE_"+std::to_string(n);Stage(stage.c_str());
        }
        else{
            if(config.scenario=="TL_LATE")for(unsigned n=1;n<=3;++n)expected.insert({1,n});
            if(config.scenario=="KEYED_LAST")for(unsigned key=1;key<=2;++key)for(unsigned n=3;n<=4;++n)expected.insert({key,n});
            if(config.side==1&&!expected.empty())Collect(expected);
            Stage("HISTORY_DONE");
            if(config.side==0){if(config.scenario=="KEYED_LAST"){WriteSample(5,1);WriteSample(5,2);}else for(unsigned n=4;n<=6;++n)WriteSample(n);}
            else{if(config.scenario=="KEYED_LAST"){expected.insert({1,5});expected.insert({2,5});}else for(unsigned n=4;n<=6;++n)expected.insert({1,n});Collect(expected);}
            Stage("NEW_DONE");
        }
        if(config.side==1&&(config.scenario=="REL_LOSS"||BestEffort()))Require(DropHit(),"DROP_NOT_EXERCISED");
        if(!BestEffort()&&config.side==0){auto rc=writer->wait_for_acknowledgments({5,0});std::printf("REG_WAIT side=%u rc=%d meaning=DDS_API_NOT_PACKET_COUNT\n",config.side,int(rc));Require(rc==dds::RETCODE_OK,"REG_WAIT");}else std::printf("REG_WAIT side=%u status=NotCalled\n",config.side);
        const auto quiet=std::chrono::steady_clock::now()+100ms;do{RegressionExtras();std::this_thread::sleep_for(1ms);}while(std::chrono::steady_clock::now()<quiet);
        Require(Matches(true)==std::set<std::string>{result.remote_reader}&&Matches(false)==std::set<std::string>{result.remote_writer},"CURRENT_MATCH_LOST");Admission("DATA_DONE");tail=true;Stage("DATA_DONE");
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
#include "PublicSocketSharedDds.inc"
}
#ifdef MDDS_DDS_EXPERIMENT_TESTING
void SetAfterWaitHook(std::function<void(dds::DataWriter*)> h){after_wait_hook=std::move(h);}
void SetAfterFrameSelectionForTest(std::function<void()> h){after_frame_selection=std::move(h);}
unsigned FrameCycleForTest(std::size_t index){auto& s=Store();std::lock_guard<std::mutex> l(s.gate);return s.rows.at(index).cycle;}
void SetSharedDeleteFailureForTest(bool enabled){shared_delete_failure_once=enabled;}
bool DropIsolationObserved() noexcept {
    auto& s=Store();std::lock_guard<std::mutex> l(s.gate);std::uint64_t occurrence=0;bool observed=false;
    for(std::size_t i=0;i<s.count;++i)if(std::strcmp(s.rows[i].stage,"EXPERIMENT_DROP")==0){if(occurrence&&occurrence!=s.rows[i].ingress)return false;occurrence=s.rows[i].ingress;}
    if(!occurrence||s.overflow)return false;
    for(std::size_t i=0;i<s.count;++i){const auto& f=s.rows[i];if(f.ingress!=occurrence)continue;if(std::strcmp(f.stage,"SDK_INGRESS")==0)observed=true;if(std::strcmp(f.stage,"RX_CAPTURED")==0||std::strcmp(f.stage,"RX_OWNED")==0)return false;}
    return observed;
}
#endif
bool ExpectedTerminal(const NativeSendRunResult& r,const HostLoopObservation& v,bool tail) noexcept {return tail&&(v.sealed||v.adapter.outgoing_stopped)&&r.outcome==NativeSendOutcome::NotEntered&&(r.status==NativeStatus::ServiceClosing||r.status==NativeStatus::Revoked||r.status==NativeStatus::NotReady);}
bool Valid(const Config& c) noexcept {
#ifndef MDDS_PUBLIC_SOCKET_BOUNDED_REGRESSION
if(c.scenario!="SMALL")return false;
#endif
bool shared=false;
#ifdef MDDS_PUBLIC_SOCKET_SHARED_REATTACH
shared=c.scenario=="SHARED_W1"||c.scenario=="SHARED_W2";
#endif
if(c.cycles<1||c.cycles>100||(!shared&&c.cycles!=1))return false;
if(!shared&&c.scenario!="SMALL"&&c.scenario!="F256"&&c.scenario!="F1M"&&c.scenario!="REL_LOSS"&&c.scenario!="BE_LOSS"&&c.scenario!="VOL_LATE"&&c.scenario!="TL_LATE"&&c.scenario!="KEYED_LAST")return false;if(c.side>1||c.delete_first>1||c.run.size()!=16||c.peer_network.empty()||c.peer_network.size()>64)return false;for(char v:c.run)if(!((v>='0'&&v<='9')||(v>='a'&&v<='f')))return false;return c.service_prefix=="com.kaihong.mdds.p"||c.service_prefix=="host.public";}
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
Result Run(const Config& c,const Control& b) noexcept {Result failed;if(!Valid(c)){failed.first_failure="CONFIG";failed.drained=true;return failed;}
#ifdef MDDS_PUBLIC_SOCKET_SHARED_REATTACH
if(c.scenario=="SHARED_W1"||c.scenario=="SHARED_W2")return RunShared(c,b);
#endif
control=&b;setup_control_failed=false;try{Endpoint endpoint(c,b);try{endpoint.Create();if(c.scenario=="SMALL")endpoint.Data();else endpoint.RegressionData();endpoint.Stage("DELETE");if(c.scenario=="SMALL")endpoint.RejectExtras("PRE_DELETE_EXTRA");else endpoint.RegressionExtras();Require(endpoint.Cleanup(),"DDS_CLEANUP");endpoint.Stage("DDS_DELETED");Require(!endpoint.pump_fault,"PUMP_FAULT");Require(FrameOverflow()==0,"FRAME_OVERFLOW");endpoint.result.success=true;}catch(const std::exception& e){endpoint.Failure(setup_control_failed?"CONTROL_LISTENER_READY":e.what());if(!endpoint.deleted)endpoint.Cleanup();}catch(...){endpoint.Failure("UNKNOWN_EXCEPTION");if(!endpoint.deleted)endpoint.Cleanup();}control=nullptr;std::printf("DDS_RESULT side=%u success=%d writes=%u takes=%u drained=%d first_failure=%s\n",c.side,endpoint.result.success,endpoint.result.writes,endpoint.result.takes,endpoint.result.drained,endpoint.result.first_failure.c_str());std::fflush(stdout);return endpoint.result;}catch(...){control=nullptr;failed.first_failure="CONSTRUCTION_EXCEPTION";return failed;}}
IngressObservationScope::IngressObservationScope(std::uint64_t id) noexcept:previous_(current_ingress){current_ingress=id;}
IngressObservationScope::~IngressObservationScope(){current_ingress=previous_;}
std::uint64_t SocketFrame(const char* stage,std::int32_t id,const std::uint8_t* p,std::size_t n) noexcept {return FrameBytes(stage,id,p,n);}
std::size_t FrameLimit() noexcept{return frame_limit;}
bool DropRegressionRecord(std::int32_t socket,const std::uint8_t* p,std::size_t n) noexcept {
#ifdef MDDS_PUBLIC_SOCKET_BOUNDED_REGRESSION
    try{if(!p||n>8192)return false;const auto seen=mdds::experiment::ObserveRtps(p,n);if(!seen.valid||!seen.drop_shape_candidate)return false;auto& d=Drop();bool hit=false;{std::lock_guard<std::mutex> l(d.gate);if(!d.armed||d.hit||d.socket!=socket)return false;for(const auto& f:seen.fragments)if(Hex(f.writer_guid.data(),16)==d.writer&&f.sequence_hi==0&&f.sequence_lo==1&&f.fragment_start<=3&&f.fragment_end>=3&&f.payload_bounds_known){d.hit_time=std::chrono::steady_clock::now();d.hit=true;hit=true;break;}}if(hit)FrameBytes("EXPERIMENT_DROP",socket,p,n);return hit;}catch(...){return false;}
#else
    (void)socket;(void)p;(void)n;return false;
#endif
}
std::size_t FrameOverflow() noexcept {auto& s=Store();std::lock_guard<std::mutex> l(s.gate);return s.overflow;}
std::size_t FrameCount() noexcept {auto& s=Store();std::lock_guard<std::mutex> l(s.gate);return s.count;}
void DumpFrames() noexcept {DumpDecisions();try{std::vector<Frame> rows;std::size_t overflow;std::uint64_t omitted,packets;auto& s=Store();{std::lock_guard<std::mutex> l(s.gate);rows.assign(s.rows.begin(),s.rows.begin()+s.count);overflow=s.overflow;omitted=s.omitted;packets=s.packets;}unsigned ordinal=0;for(auto& e:rows)std::printf("DDS_FRAME event=%u cycle=%u packet=%llu ingress=%llu stage=%s kind=%s socket=%d length=%zu hash=%llu writer=%s sn=%llu correlation=%llu app=%llu receiver=%llu owner=%llu admitted=%llu entered=%llu offset=%zu first=%u count=%u fragment_size=%u sample_size=%u writer_known=%d reader=%s reader_known=%d bits=%u bitmap=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x\n",++ordinal,e.cycle,(unsigned long long)e.packet,(unsigned long long)e.ingress,e.stage,e.kind,e.socket,e.length,(unsigned long long)e.hash,Hex(e.writer.data(),16).c_str(),(unsigned long long)e.sn,(unsigned long long)e.correlation,(unsigned long long)e.app,(unsigned long long)e.receiver,(unsigned long long)e.owner,(unsigned long long)e.admitted,(unsigned long long)e.entered,e.offset,e.first,e.count,e.fragment_size,e.sample_size,e.writer_known,Hex(e.reader.data(),16).c_str(),e.reader_known,e.bits,e.bitmap[0],e.bitmap[1],e.bitmap[2],e.bitmap[3],e.bitmap[4],e.bitmap[5],e.bitmap[6],e.bitmap[7]);std::printf("DDS_FRAME_SUMMARY count=%zu limit=%zu overflow=%zu storage_bytes=%zu observations=%llu omitted=%llu\n",rows.size(),frame_limit,overflow,sizeof(Frames),(unsigned long long)packets,(unsigned long long)omitted);}catch(...){auto& s=Store();std::lock_guard<std::mutex> l(s.gate);++s.overflow;}}
}
namespace mdds::dsoftbus::phase0 {bool RuntimeProbeBeforeConnect() noexcept {try{const bool ok=dds_experiment::control&&(*dds_experiment::control)("LISTENER_READY");if(!ok)dds_experiment::setup_control_failed=true;return ok;}catch(...){dds_experiment::setup_control_failed=true;return false;}}}
