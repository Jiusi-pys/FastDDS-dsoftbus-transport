#include "PortableFixture.hpp"
#include "CaseTable.hpp"
#include "PerReaderValidator.hpp"
#include "../DualParticipantPolicy.hpp"
#include "../DualValidation.hpp"
#include "../ListenerPolicy.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/publisher/DataWriterListener.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/DataReaderListener.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
namespace mdds::experiment::matrix {
namespace dds=eprosima::fastdds::dds;
namespace rtps=eprosima::fastdds::rtps;
using Clock=std::chrono::steady_clock;
namespace {
std::string Hex(const std::uint8_t* p,std::size_t n){
    static constexpr char digit[]="0123456789abcdef";std::string s;s.reserve(n*2);
    for(std::size_t i=0;i<n;++i){s+=digit[p[i]>>4];s+=digit[p[i]&15];}return s;
}
std::string Guid(const rtps::GUID_t& g){const auto b=DualBytes(g);return Hex(b.data(),b.size());}
std::string Handle(const dds::InstanceHandle_t& h){return Hex(h.value,16);}
std::string Hash(const std::string& s){
    std::uint64_t h=14695981039346656037ull;for(unsigned char c:s){h^=c;h*=1099511628211ull;}
    std::ostringstream os;os<<std::hex<<std::setfill('0')<<std::setw(16)<<h;return os.str();
}
std::string Join(const std::set<std::string>& s){std::string out;for(const auto& x:s){if(!out.empty())out+=',';out+=x;}return out.empty()?"none":out;}
int Pid(){
#ifdef _WIN32
    return _getpid();
#else
    return getpid();
#endif
}
bool SafeRun(const std::string& s){return s.size()==16 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
template<class Q> void ConfigureQos(Q& q,const EndpointSpec& e){
    q.reliability().kind=e.reliable?dds::RELIABLE_RELIABILITY_QOS:dds::BEST_EFFORT_RELIABILITY_QOS;
    q.reliability().max_blocking_time={1,0};
    q.durability().kind=e.transient_local?dds::TRANSIENT_LOCAL_DURABILITY_QOS:dds::VOLATILE_DURABILITY_QOS;
    q.history().kind=dds::KEEP_LAST_HISTORY_QOS;q.history().depth=32;
    q.resource_limits().max_samples=64;q.resource_limits().max_instances=1;
    q.resource_limits().max_samples_per_instance=64;q.resource_limits().allocated_samples=64;
    q.data_sharing().off();q.ownership().kind=dds::SHARED_OWNERSHIP_QOS;
    q.deadline().period=dds::c_TimeInfinite;q.lifespan().duration=dds::c_TimeInfinite;
    q.liveliness().kind=dds::AUTOMATIC_LIVELINESS_QOS;q.liveliness().lease_duration=dds::c_TimeInfinite;
    q.endpoint().entity_id=e.entity_id;
}
bool Resources(const dds::ResourceLimitsQosPolicy& r){return r.max_samples==64&&r.max_instances==1&&r.max_samples_per_instance==64&&r.allocated_samples==64;}
template<class Q> bool LocalQos(const Q& q,const EndpointSpec& e){
    return q.reliability().kind==(e.reliable?dds::RELIABLE_RELIABILITY_QOS:dds::BEST_EFFORT_RELIABILITY_QOS)&&
        q.reliability().max_blocking_time==dds::Duration_t(1,0)&&
        q.durability().kind==(e.transient_local?dds::TRANSIENT_LOCAL_DURABILITY_QOS:dds::VOLATILE_DURABILITY_QOS)&&
        q.history().kind==dds::KEEP_LAST_HISTORY_QOS&&q.history().depth==32&&Resources(q.resource_limits())&&
        q.data_sharing().kind()==dds::OFF&&q.ownership().kind==dds::SHARED_OWNERSHIP_QOS&&
        q.deadline().period.is_infinite()&&q.lifespan().duration.is_infinite()&&
        q.liveliness().kind==dds::AUTOMATIC_LIVELINESS_QOS&&q.liveliness().lease_duration.is_infinite()&&
        q.endpoint().entity_id==e.entity_id;
}
}
struct MatrixFixture::Impl {
    struct Endpoint;
    struct WriterListener final:dds::DataWriterListener {
        Impl& owner;Endpoint& endpoint;
        WriterListener(Impl& o,Endpoint& e):owner(o),endpoint(e){}
        void on_publication_matched(dds::DataWriter*,const dds::PublicationMatchedStatus&) override;
        void on_offered_incompatible_qos(dds::DataWriter*,const dds::OfferedIncompatibleQosStatus&) override;
    };
    struct ReaderListener final:dds::DataReaderListener {
        Impl& owner;Endpoint& endpoint;
        ReaderListener(Impl& o,Endpoint& e):owner(o),endpoint(e){}
        void on_subscription_matched(dds::DataReader*,const dds::SubscriptionMatchedStatus&) override;
        void on_requested_incompatible_qos(dds::DataReader*,const dds::RequestedIncompatibleQosStatus&) override;
        void on_data_available(dds::DataReader*) override;
    };
    struct DiscoveryListener final:dds::DomainParticipantListener {
        Impl& owner;unsigned participant;
        DiscoveryListener(Impl& o,unsigned p):owner(o),participant(p){}
        void on_participant_discovery(dds::DomainParticipant*,rtps::ParticipantDiscoveryStatus,const dds::ParticipantBuiltinTopicData&,bool&) override;
        void on_data_writer_discovery(dds::DomainParticipant*,rtps::WriterDiscoveryStatus,const dds::PublicationBuiltinTopicData&,bool&) override;
        void on_data_reader_discovery(dds::DomainParticipant*,rtps::ReaderDiscoveryStatus,const dds::SubscriptionBuiltinTopicData&,bool&) override;
    };
    struct Endpoint {
        EndpointSpec spec;std::string guid;
        dds::DataWriter* writer=nullptr;dds::DataReader* reader=nullptr;
        std::unique_ptr<WriterListener> wl;std::unique_ptr<ReaderListener> rl;
        std::unique_ptr<PerReaderValidator> ledger;
        std::atomic<bool> delete_complete{false};std::atomic<unsigned> reliability_incompatible{0};
        std::set<unsigned> written;std::set<std::pair<std::int32_t,std::uint32_t>> written_sequences;
        bool created=false;
    };
    struct Participant {
        unsigned id=0,domain=0;dds::DomainParticipant* value=nullptr;
        dds::Publisher* publisher=nullptr;dds::Subscriber* subscriber=nullptr;
        std::map<std::string,dds::Topic*> topics;
        std::unique_ptr<DiscoveryListener> listener;
        std::shared_ptr<Control> control;
    };
    struct DiscoveryRecord {unsigned participant;std::string kind,change,guid;bool valid;};
    MatrixOptions options;const CaseSpec* spec=nullptr;
    std::map<std::string,std::unique_ptr<Endpoint>> endpoints;
    std::map<unsigned,std::unique_ptr<Participant>> participants;
    dds::TypeSupport type{new HelloWorldPubSubType()};
    mutable std::mutex failure_mutex,log_mutex,discovery_mutex;
    std::string first_failure,cleanup_failure;
    std::map<unsigned,std::set<std::string>> discovered;
    std::vector<DiscoveryRecord> discovery_history;
    std::uint64_t order=0;
    bool opened=false,closed=false,reference=false;
    std::atomic<bool> cleaning{false},observation_complete{false};
    unsigned next_release=0;
    const std::vector<std::string> phases{"PRE","CHANGE","POST","OBSERVE","CLEANUP"};

    explicit Impl(MatrixOptions o):options(std::move(o)){
        for(const auto& c:Cases())if(c.id==options.case_id)spec=&c;
        if(!spec||!SafeRun(options.run)||options.control_directory.empty()){Fail("invalid_options");return;}
        for(const auto& e:spec->endpoints)if(e.board_a==options.board_a){
            auto p=std::make_unique<Endpoint>();p->spec=e;p->guid=ExpectedGuid(e);
            if(e.writer)p->wl=std::make_unique<WriterListener>(*this,*p);
            else {p->rl=std::make_unique<ReaderListener>(*this,*p);p->ledger=std::make_unique<PerReaderValidator>(p->guid,ExpectedReader(e.id,"FINAL"));}
            endpoints.emplace(e.id,std::move(p));
            if(!participants.count(e.participant)){
                auto part=std::make_unique<Participant>();part->id=e.participant;part->domain=e.domain;
                part->listener=std::make_unique<DiscoveryListener>(*this,e.participant);
                participants.emplace(e.participant,std::move(part));
            }
        }
    }
    void Log(const std::string& event,const std::string& fields=""){
        std::lock_guard<std::mutex> lock(log_mutex);
        std::printf("MATRIX event=%s run=%s case=%s side=%s pid=%d order=%llu %s\n",event.c_str(),options.run.c_str(),options.case_id.c_str(),options.board_a?"A":"B",Pid(),static_cast<unsigned long long>(++order),fields.c_str());std::fflush(stdout);
    }
    bool Fail(const std::string& reason){
        bool first=false;{std::lock_guard<std::mutex> lock(failure_mutex);if(first_failure.empty()){first_failure=reason;first=true;}}
        if(first)Log("FIRST_FAILURE","stage=FIXTURE predicate="+reason+" reason="+reason);return false;
    }
    bool CleanupFail(const std::string& reason){
        bool first=false;{std::lock_guard<std::mutex> lock(failure_mutex);if(cleanup_failure.empty()){cleanup_failure=reason;first=true;}}
        if(first)Log("CLEANUP_FAILURE","predicate="+reason+" reason="+reason);return false;
    }
    bool Good()const{std::lock_guard<std::mutex> lock(failure_mutex);return first_failure.empty();}
    const EndpointSpec* Find(const std::string& id)const{for(const auto& e:spec->endpoints)if(e.id==id)return &e;return nullptr;}
    const EndpointSpec* FindGuid(const std::string& guid)const{for(const auto& e:spec->endpoints)if(ExpectedGuid(e)==guid)return &e;return nullptr;}
    std::string ExpectedGuid(const EndpointSpec& e)const{
        auto b=DualIdentity(options.run,e.board_a,e.participant,EndpointKind::Participant);
        b[12]=static_cast<std::uint8_t>(e.entity_id>>16);b[13]=static_cast<std::uint8_t>(e.entity_id>>8);
        b[14]=static_cast<std::uint8_t>(e.entity_id);b[15]=e.writer?3:4;return Hex(b.data(),b.size());
    }
    std::string Topic(const EndpointSpec& e)const{return "mdds-matrix-"+options.run+"-"+e.topic;}
    std::string Payload(const EndpointSpec& e,unsigned ordinal)const{return "matrix|"+options.run+"|"+options.case_id+"|"+e.id+"|"+std::to_string(ordinal);}
    ExpectedSamples ExpectedReader(const std::string& id,const std::string& phase)const{
        ExpectedSamples out;const auto reader=Find(id);const auto found=spec->final_samples.find(id);
        if(found==spec->final_samples.end())return out;
        if(phase=="PRE"&&reader->late)return out;
        for(const auto& writer:found->second){
            std::set<unsigned> ord;
            for(auto x:writer.second){
                if(phase=="FINAL")ord.insert(x);
                else {const auto pre=spec->pre.find(writer.first);if(pre!=spec->pre.end()&&std::find(pre->second.begin(),pre->second.end(),x)!=pre->second.end())ord.insert(x);}
            }
            if(!ord.empty())out.emplace(ExpectedGuid(*Find(writer.first)),std::move(ord));
        }return out;
    }
    template<class T> bool RemoteQos(const T& q,const EndpointSpec& e){
        // Pinned 3.6.2 QosPoliciesSerializer<ReliabilityQosPolicy>::should_be_sent
        // omits the complete reliability parameter for a BEST_EFFORT Reader.
        // Its remote proxy therefore exposes the 100 ms decoder default; the
        // actual Reader get_qos is independently required to contain 1000 ms.
        const auto blocking=(e.board_a!=options.board_a&&!e.writer&&!e.reliable)?dds::Duration_t(0,100000000):dds::Duration_t(1,0);
        return std::string(q.topic_name.c_str())==Topic(e)&&std::string(q.type_name.c_str())=="HelloWorld"&&
            q.reliability.kind==(e.reliable?dds::RELIABLE_RELIABILITY_QOS:dds::BEST_EFFORT_RELIABILITY_QOS)&&
            q.reliability.max_blocking_time==blocking&&
            q.durability.kind==(e.transient_local?dds::TRANSIENT_LOCAL_DURABILITY_QOS:dds::VOLATILE_DURABILITY_QOS)&&
            q.ownership.kind==dds::SHARED_OWNERSHIP_QOS&&q.deadline.period.is_infinite()&&
            q.lifespan.duration.is_infinite()&&q.liveliness.kind==dds::AUTOMATIC_LIVELINESS_QOS&&q.liveliness.lease_duration.is_infinite()&&
            q.partition.names().empty()&&q.data_sharing.kind()==dds::OFF&&
            (!q.history.has_value()||(q.history->kind==dds::KEEP_LAST_HISTORY_QOS&&q.history->depth==32))&&
            (!q.resource_limits.has_value()||Resources(*q.resource_limits));
    }
    template<class T> std::string RemoteFacts(const T& q){
        return " topic="+std::string(q.topic_name.c_str())+" type="+q.type_name.c_str()+
            " reliability="+std::to_string(q.reliability.kind)+" durability="+std::to_string(q.durability.kind)+
            " max_blocking_seconds="+std::to_string(q.reliability.max_blocking_time.seconds)+" max_blocking_nanosec="+std::to_string(q.reliability.max_blocking_time.nanosec)+
            " ownership="+std::to_string(q.ownership.kind)+" deadline_infinite="+std::to_string(q.deadline.period.is_infinite())+
            " lifespan_infinite="+std::to_string(q.lifespan.duration.is_infinite())+" liveliness="+std::to_string(q.liveliness.kind)+
            " lease_infinite="+std::to_string(q.liveliness.lease_duration.is_infinite())+" partitions="+std::to_string(q.partition.names().size())+
            " data_sharing="+std::to_string(q.data_sharing.kind())+" history_present="+std::to_string(q.history.has_value())+
            " history_kind="+(q.history.has_value()?std::to_string(q.history->kind):"NA")+" history_depth="+(q.history.has_value()?std::to_string(q.history->depth):"NA")+
            " resources_present="+std::to_string(q.resource_limits.has_value())+
            " max_samples="+(q.resource_limits.has_value()?std::to_string(q.resource_limits->max_samples):"NA")+
            " max_instances="+(q.resource_limits.has_value()?std::to_string(q.resource_limits->max_instances):"NA")+
            " max_samples_per_instance="+(q.resource_limits.has_value()?std::to_string(q.resource_limits->max_samples_per_instance):"NA")+
            " allocated_samples="+(q.resource_limits.has_value()?std::to_string(q.resource_limits->allocated_samples):"NA");
    }
    template<class T> void Discovered(unsigned p,const char* kind,const char* change,const T& data){
        const auto guid=Guid(data.guid);const auto e=FindGuid(guid);const bool removed=std::string(change)=="REMOVED";
        // Fast DDS also reports this Participant's own endpoint creation through
        // discovery listeners. Retain those raw events without treating them as
        // remote readiness or as a local-match policy failure.
        const bool local=e&&e->board_a==options.board_a&&e->participant==p;
        const bool valid=e&&(local||e->board_a!=options.board_a)&&e->domain==participants.at(p)->domain&&RemoteQos(data,*e);
        {std::lock_guard<std::mutex> lock(discovery_mutex);discovery_history.push_back({p,kind,change,guid,valid});
            if(removed)discovered[p].erase(guid);else if(valid&&!local)discovered[p].insert(guid);}
        Log("DISCOVERY","participant="+std::to_string(p)+" kind="+kind+" change="+change+" guid="+guid+" topic="+data.topic_name.c_str()+" type="+data.type_name.c_str()+" local="+std::to_string(local)+" valid="+std::to_string(valid));
        if(!removed){
            Log("REMOTE_QOS","source=discovery endpoint="+(e?e->id:std::string("unknown"))+" guid="+guid+RemoteFacts(data)+" max_blocking_source="+(e&&!local&&!e->writer&&!e->reliable?"OMITTED_BE_READER_DECODER_DEFAULT":"ANNOUNCED_OR_LOCAL")+" valid="+std::to_string(valid));
            if(!valid&&!cleaning)Fail("remote_qos_or_identity_"+guid);
        }
    }
    bool Initialize(){
        if(!Good()||opened)return Fail("open_precondition");
        auto* factory=dds::DomainParticipantFactory::get_instance();
        eprosima::fastdds::LibrarySettings settings;settings.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
        if(factory->set_library_settings(settings)!=dds::RETCODE_OK)return Fail("intraprocess_set");
        Log("SCOPE","scope="+std::string(reference?"HOST_REFERENCE_UDP":"BYTES_NATIVE")+" identity_grouping=FIXTURE_NOT_OS_PROOF table_sha256=3f51d3a4c5031e0b016d328497b1bae47b4d06082862b7229557fd3c6f4a8259");
        return true;
    }
    void BaseParticipantQos(dds::DomainParticipantQos& qos,unsigned id){
        qos.name("matrix-"+options.run+"-"+(options.board_a?"A":"B")+std::to_string(id));
        ApplyDualParticipantPolicy(qos);ApplyDualIdentityFixture(qos,options.run,options.board_a,id);
        qos.transport().use_builtin_transports=false;qos.transport().user_transports.clear();
        auto& wire=qos.wire_protocol();wire.builtin.discovery_config.discoveryProtocol=rtps::DiscoveryProtocol::SIMPLE;
        wire.builtin.discovery_config.leaseDuration_announcementperiod={1,0};
        wire.builtin.discovery_config.initial_announcements.count=10;wire.builtin.discovery_config.initial_announcements.period={0,300000000};
        wire.builtin.metatrafficMulticastLocatorList.clear();wire.builtin.metatrafficUnicastLocatorList.clear();
        wire.default_multicast_locator_list.clear();wire.default_unicast_locator_list.clear();
        wire.default_external_unicast_locators.clear();wire.builtin.initialPeersList.clear();wire.builtin.mutation_tries=0;
    }
    bool CreateParticipant(Participant& p,const dds::DomainParticipantQos& qos){
        Log("ENTITY_CREATE_BEGIN","endpoint="+std::string(options.board_a?"A":"B")+std::to_string(p.id)+" kind=participant phase=INITIAL domain="+std::to_string(p.domain));
        auto* factory=dds::DomainParticipantFactory::get_instance();
        p.value=factory->create_participant(p.domain,qos,p.listener.get(),ParticipantListenerMask());
        if(!p.value)return Fail("create_participant_"+std::to_string(p.id));
        const auto& actual=p.value->get_qos();eprosima::fastdds::LibrarySettings lib;
        const bool valid=p.value->get_domain_id()==p.domain&&DualBytes(p.value->guid())==DualIdentity(options.run,options.board_a,p.id,EndpointKind::Participant)&&
            DualParticipantPolicyRequested(actual)&&!actual.transport().use_builtin_transports&&actual.transport().user_transports==qos.transport().user_transports&&
            actual.wire_protocol().builtin.metatrafficUnicastLocatorList==qos.wire_protocol().builtin.metatrafficUnicastLocatorList&&
            actual.wire_protocol().default_unicast_locator_list==qos.wire_protocol().default_unicast_locator_list&&
            actual.wire_protocol().builtin.initialPeersList==qos.wire_protocol().builtin.initialPeersList&&
            actual.wire_protocol().builtin.metatrafficMulticastLocatorList.empty()&&actual.wire_protocol().default_multicast_locator_list.empty()&&
            factory->get_library_settings(lib)==dds::RETCODE_OK&&lib.intraprocess_delivery==eprosima::fastdds::INTRAPROCESS_OFF;
        Log("ENTITY_CREATED","endpoint="+std::string(options.board_a?"A":"B")+std::to_string(p.id)+" kind=participant phase=INITIAL guid="+Guid(p.value->guid())+" domain="+std::to_string(p.domain)+" valid="+std::to_string(valid));
        if(p.control){const auto s=p.control->Snapshot();Log("NATIVE_IDENTITY","endpoint="+std::string(options.board_a?"A":"B")+std::to_string(p.id)+" participant="+std::to_string(p.id)+" participant_guid="+Guid(p.value->guid())+" runtime="+std::to_string(s.runtime_id)+" native_instance="+std::to_string(s.instance_id)+" application="+std::to_string(s.application_incarnation)+" receiver="+std::to_string(s.receiver_incarnation)+" token="+std::to_string(s.logical_token)+" local_port="+std::to_string(qos.wire_protocol().default_unicast_locator_list.begin()->port));}
        if(!valid)return Fail("participant_readback");
        if(type.register_type(p.value)!=dds::RETCODE_OK)return Fail("register_type");
        p.publisher=p.value->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
        p.subscriber=p.value->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        if(!p.publisher||!p.subscriber||!p.publisher->get_qos().partition().names().empty()||!p.subscriber->get_qos().partition().names().empty())return Fail("publisher_subscriber_partition");
        unsigned native_descriptors=0,udp_descriptors=0;
        for(const auto& descriptor:actual.transport().user_transports){
            if(std::dynamic_pointer_cast<ExperimentalDescriptor>(descriptor))++native_descriptors;
            if(std::dynamic_pointer_cast<rtps::UDPv4TransportDescriptor>(descriptor))++udp_descriptors;
        }
        const auto locators=[](const rtps::LocatorList& list){std::string out;for(const auto& locator:list){if(!out.empty())out+=',';out+=std::to_string(locator.kind)+":"+Hex(locator.address,16)+":"+std::to_string(locator.port);}return out.empty()?"none":out;};
        Log("PARTICIPANT_QOS","participant="+std::to_string(p.id)+" domain="+std::to_string(p.value->get_domain_id())+
            " use_builtin="+std::to_string(actual.transport().use_builtin_transports)+" transport_count="+std::to_string(actual.transport().user_transports.size())+
            " native_descriptor_count="+std::to_string(native_descriptors)+" udp_descriptor_count="+std::to_string(udp_descriptors)+
            " intraprocess="+std::to_string(lib.intraprocess_delivery)+" local_policy="+std::to_string(DualParticipantPolicyRequested(actual))+
            " metatraffic_multicast_count="+std::to_string(actual.wire_protocol().builtin.metatrafficMulticastLocatorList.size())+
            " default_multicast_count="+std::to_string(actual.wire_protocol().default_multicast_locator_list.size())+
            " metatraffic_unicast_count="+std::to_string(actual.wire_protocol().builtin.metatrafficUnicastLocatorList.size())+
            " default_unicast_count="+std::to_string(actual.wire_protocol().default_unicast_locator_list.size())+
            " initial_peer_count="+std::to_string(actual.wire_protocol().builtin.initialPeersList.size())+
            " default_locators="+locators(actual.wire_protocol().default_unicast_locator_list)+
            " metatraffic_locators="+locators(actual.wire_protocol().builtin.metatrafficUnicastLocatorList)+
            " initial_peer_locators="+locators(actual.wire_protocol().builtin.initialPeersList)+
            " publisher_partition_count="+std::to_string(p.publisher->get_qos().partition().names().size())+
            " subscriber_partition_count="+std::to_string(p.subscriber->get_qos().partition().names().size()));
        for(const auto& item:endpoints)if(item.second->spec.participant==p.id){
            const auto topic=Topic(item.second->spec);if(!p.topics.count(topic))p.topics[topic]=p.value->create_topic(topic,type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
            if(!p.topics[topic])return Fail("create_topic");
        }return true;
    }
    bool CreateEndpoint(Endpoint& e,const std::string& phase){
        auto& p=*participants.at(e.spec.participant);
        if(e.created)return Fail("endpoint_recreation_"+e.spec.id);
        Log("ENTITY_CREATE_BEGIN","endpoint="+e.spec.id+" kind="+(e.spec.writer?"writer":"reader")+" phase="+phase+" expected_guid="+e.guid);
        bool valid=false;std::string actual;
        if(e.spec.writer){
            dds::DataWriterQos q;ConfigureQos(q,e.spec);q.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;
            e.writer=p.publisher->create_datawriter(p.topics.at(Topic(e.spec)),q,e.wl.get(),dds::StatusMask::all());
            if(e.writer){actual=Guid(e.writer->guid());valid=LocalQos(e.writer->get_qos(),e.spec)&&e.writer->get_qos().publish_mode().kind==dds::SYNCHRONOUS_PUBLISH_MODE;}
        }else{
            dds::DataReaderQos q;ConfigureQos(q,e.spec);
            e.reader=p.subscriber->create_datareader(p.topics.at(Topic(e.spec)),q,e.rl.get(),dds::StatusMask::all());
            if(e.reader){actual=Guid(e.reader->guid());valid=LocalQos(e.reader->get_qos(),e.spec);}
        }
        valid=valid&&actual==e.guid;e.created=!actual.empty();
        Log("ENTITY_CREATED","endpoint="+e.spec.id+" kind="+(e.spec.writer?"writer":"reader")+" phase="+phase+" guid="+(actual.empty()?"none":actual)+" topic="+Topic(e.spec)+" type=HelloWorld domain="+std::to_string(e.spec.domain)+" valid="+std::to_string(valid));
        Log("LOCAL_QOS","endpoint="+e.spec.id+" reliability="+(e.spec.reliable?"RELIABLE":"BEST_EFFORT")+" durability="+(e.spec.transient_local?"TRANSIENT_LOCAL":"VOLATILE")+" history=KEEP_LAST depth=32 max_samples=64 max_instances=1 max_samples_per_instance=64 allocated_samples=64 max_blocking_ms=1000 data_sharing=OFF ownership=SHARED deadline=INFINITE lifespan=INFINITE liveliness=AUTOMATIC lease=INFINITE publish_mode="+(e.spec.writer?"SYNCHRONOUS":"NA")+" readback="+(valid?"PASS":"FAIL"));
        return valid||Fail("endpoint_readback_"+e.spec.id);
    }
    bool FinishOpen(){for(auto& item:endpoints)if(!item.second->spec.late&&!CreateEndpoint(*item.second,"INITIAL"))return false;opened=true;return Good();}
    std::set<std::string> ExpectedPeers(const EndpointSpec& e,bool final)const{
        std::set<std::string> out;for(const auto& edge:final?spec->final_edges:spec->initial_edges){
            if(edge[0]==e.id)out.insert(ExpectedGuid(*Find(edge[1])));if(edge[1]==e.id)out.insert(ExpectedGuid(*Find(edge[0])));
        }return out;
    }
    bool CurrentGraph(bool final,const std::string& phase,bool emit){
        bool same=true;
        for(auto& item:endpoints){auto& e=*item.second;if(!e.created||e.delete_complete)continue;
            std::vector<dds::InstanceHandle_t> handles;
            const auto rc=e.spec.writer?e.writer->get_matched_subscriptions(handles):e.reader->get_matched_publications(handles);
            if(rc!=dds::RETCODE_OK)return Fail("matched_query_"+e.spec.id);
            std::set<std::string> current;
            for(const auto& handle:handles){
                std::string guid,facts;bool valid=false;
                if(e.spec.writer){dds::SubscriptionBuiltinTopicData data;if(e.writer->get_matched_subscription_data(data,handle)!=dds::RETCODE_OK){same=false;continue;}
                    guid=Guid(data.guid);const auto remote=FindGuid(guid);valid=remote&&RemoteQos(data,*remote);facts=RemoteFacts(data);
                }else{dds::PublicationBuiltinTopicData data;if(e.reader->get_matched_publication_data(data,handle)!=dds::RETCODE_OK){same=false;continue;}
                    guid=Guid(data.guid);const auto remote=FindGuid(guid);valid=remote&&RemoteQos(data,*remote)&&(!data.publish_mode.has_value()||data.publish_mode->kind==dds::SYNCHRONOUS_PUBLISH_MODE);facts=RemoteFacts(data)+" publish_mode_present="+std::to_string(data.publish_mode.has_value())+" publish_mode="+(data.publish_mode.has_value()?std::to_string(data.publish_mode->kind):"NA");
                }
                current.insert(guid);if(!valid)return Fail("matched_remote_qos_"+e.spec.id);
                if(emit)Log("REMOTE_QOS","source=get_matched endpoint="+e.spec.id+" peer_guid="+guid+facts+" valid=1");
            }
            const auto wanted=ExpectedPeers(e.spec,final);if(current!=wanted)same=false;
            for(const auto& edge:spec->forbidden_edges){const auto other=edge[0]==e.spec.id?edge[1]:edge[1]==e.spec.id?edge[0]:std::string();
                if(!other.empty()&&current.count(ExpectedGuid(*Find(other))))return Fail("forbidden_match_"+e.spec.id);}
            if(emit)Log("MATCH_SNAPSHOT","phase="+phase+" endpoint="+e.spec.id+" guid="+e.guid+" peers="+Join(current)+" expected="+Join(wanted)+" source=get_matched valid="+std::to_string(current==wanted));
        }
        // Raw discovery is an independent configuration check; it never substitutes
        // for current matched sets above, including newly created late Readers.
        {std::lock_guard<std::mutex> lock(discovery_mutex);
            for(const auto& p:participants)for(const auto& e:spec->endpoints){
                if(e.board_a==options.board_a||e.domain!=p.second->domain||(!final&&e.late))continue;
                if(final&&std::find(spec->deletions.begin(),spec->deletions.end(),e.id)!=spec->deletions.end())continue;
                if(!discovered[p.first].count(ExpectedGuid(e)))same=false;
            }
        }
        return same&&Good();
    }
    bool Incompatibility(bool emit){
        if(options.case_id!="Q_BE_REL")return true;
        auto& e=*endpoints.at(options.board_a?"A1.W1":"B1.R1");
        dds::IncompatibleQosStatus status;
        const auto rc=e.spec.writer?e.writer->get_offered_incompatible_qos_status(status):e.reader->get_requested_incompatible_qos_status(status);
        if(rc!=dds::RETCODE_OK)return Fail("incompatible_status_query");
        unsigned count=0;for(const auto& policy:status.policies)if(policy.policy_id==dds::RELIABILITY_QOS_POLICY_ID)count=policy.count;
        const bool valid=count>=1&&status.last_policy_id==dds::RELIABILITY_QOS_POLICY_ID;
        if(emit)Log("INCOMPATIBLE_STATUS","source=get_status endpoint="+e.spec.id+" total="+std::to_string(status.total_count)+" last_policy="+std::to_string(status.last_policy_id)+" reliability_policy_count="+std::to_string(count)+" valid="+std::to_string(valid));
        return valid;
    }
    bool LedgerStage(const std::string& phase,bool emit){
        bool all=true;for(auto& item:endpoints){auto& e=*item.second;if(e.spec.writer)continue;
            const auto wanted=ExpectedReader(e.spec.id,phase);const bool valid=e.ledger->Complete(wanted);all=all&&valid;
            if(emit){const auto c=e.ledger->Counters();Log("LEDGER","phase="+phase+" endpoint="+e.spec.id+" reader_guid="+e.guid+" valid_unique="+std::to_string(c.valid_unique)+" missing="+std::to_string(e.ledger->Missing(wanted))+" duplicate="+std::to_string(c.duplicate)+" wrong_writer="+std::to_string(c.wrong_writer)+" wrong_reader="+std::to_string(c.wrong_reader)+" unexpected_ordinal="+std::to_string(c.unexpected_ordinal)+" payload_mismatch="+std::to_string(c.payload_mismatch)+" invalid_sample="+std::to_string(c.invalid_sample)+" callback_after_delete="+std::to_string(c.after_delete)+" complete="+std::to_string(valid));}
            const auto failure=e.ledger->Failure();if(!failure.empty())return Fail(failure+"_"+e.spec.id);
        }return all&&Good();
    }
    template<class Predicate> bool Wait(Predicate predicate,const std::string& reason){
        const auto deadline=Clock::now()+std::chrono::seconds(20);
        while(Good()&&Clock::now()<deadline){if(predicate())return true;if(!CheckAbort())return false;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        return Fail(reason);
    }
    bool CheckAbort(){
        std::ifstream file(options.control_directory+"/ABORT.release",std::ios::binary);if(!file)return Good();
        const std::string text((std::istreambuf_iterator<char>(file)),{});
        if(text!="MATRIX_ABORT "+options.run+" "+options.case_id+"\n")return Fail("malformed_abort");
        return Fail("controller_abort");
    }
    bool Release(const std::string& phase){
        if(next_release>=phases.size()||phases[next_release]!=phase)return Fail("release_internal_order");
        return Wait([&]{
            // Rename each consumed release to an immutable .consumed receipt.
            // Reappearance of the release path is a duplicate even when its
            // bytes are identical; no timestamp or cross-device clock is used.
            for(unsigned i=0;i<phases.size();++i){
                std::ifstream file(options.control_directory+"/"+phases[i]+".release",std::ios::binary);if(!file)continue;
                if(i<next_release){Fail("release_replaced_"+phases[i]);return false;}
                const std::string value((std::istreambuf_iterator<char>(file)),{});
                if(value!="MATRIX_RELEASE "+options.run+" "+options.case_id+" "+phases[i]+"\n"){Fail("release_content_"+phases[i]);return false;}
                if(i>next_release){Fail("release_out_of_order_"+phases[i]);return false;}
            }
            const auto path=options.control_directory+"/"+phase;
            std::ifstream file(path+".release",std::ios::binary);if(!file)return false;file.close();
            std::ifstream consumed(path+".consumed",std::ios::binary);if(consumed){Fail("release_already_consumed_"+phase);return false;}
            if(std::rename((path+".release").c_str(),(path+".consumed").c_str())!=0){Fail("release_consume_"+phase);return false;}
            ++next_release;Log("CONTROL_CONSUMED","phase="+phase);return true;
        },"release_timeout_"+phase);
    }
    bool Writes(const WriteSet& writes,const std::string& phase){
        for(const auto& plan:writes){const auto it=endpoints.find(plan.first);if(it==endpoints.end())continue;auto& e=*it->second;
            for(auto ordinal:plan.second){
                if(!e.writer||!e.written.insert(ordinal).second)return Fail("application_resend_"+e.spec.id);
                HelloWorld sample;sample.index(ordinal);const auto payload=Payload(e.spec,ordinal);sample.message(payload);
                if(payload.size()>=128)return Fail("payload_bound");
                rtps::WriteParams params; // Fresh: never request DDS to reuse an earlier identity.
                const auto rc=e.writer->write(&sample,params);const auto& id=params.sample_identity();const auto& sn=id.sequence_number();
                const auto actual=Guid(id.writer_guid());
                Log("WRITE_RETURN","phase="+phase+" endpoint="+e.spec.id+" writer_guid="+actual+" sn_hi="+std::to_string(sn.high)+" sn_lo="+std::to_string(sn.low)+" ordinal="+std::to_string(ordinal)+" payload_hash="+Hash(payload)+" payload_len="+std::to_string(payload.size())+" payload_valid=1 rc="+std::to_string(int(rc)));
                if(rc!=dds::RETCODE_OK||actual!=e.guid||sn.high<0||(sn.high==0&&sn.low==0)||!e.written_sequences.emplace(sn.high,sn.low).second)return Fail("write_or_identity_"+e.spec.id);
            }
            std::vector<dds::InstanceHandle_t> matches;if(e.writer->get_matched_subscriptions(matches)!=dds::RETCODE_OK)return Fail("ack_matches_query");
            bool reliable_reader=false;for(const auto& h:matches){dds::SubscriptionBuiltinTopicData data;if(e.writer->get_matched_subscription_data(data,h)!=dds::RETCODE_OK)return Fail("ack_remote_query");if(data.reliability.kind==dds::RELIABLE_RELIABILITY_QOS)reliable_reader=true;}
            if(e.spec.reliable&&reliable_reader&&!plan.second.empty()){
                const auto rc=e.writer->wait_for_acknowledgments({5,0});Log("DDS_WAIT","phase="+phase+" endpoint="+e.spec.id+" state=Called rc="+std::to_string(int(rc))+" matched_reliable=1");if(rc!=dds::RETCODE_OK)return Fail("dds_wait_"+e.spec.id);
            }else Log("DDS_WAIT","phase="+phase+" endpoint="+e.spec.id+" state=NotCalled reason="+(!e.spec.reliable?std::string("BEST_EFFORT_WRITER"):!reliable_reader?std::string("NO_MATCHED_RELIABLE_READER"):std::string("NO_WRITES")));
        }return Good();
    }
    bool DeleteReader(Endpoint& e){
        if(!e.reader)return CleanupFail("reader_delete_precondition_"+e.spec.id);
        Log("READER_DELETE_BEGIN","endpoint="+e.spec.id+" reader_guid="+e.guid);
        const auto rc=participants.at(e.spec.participant)->subscriber->delete_datareader(e.reader);
        if(rc==dds::RETCODE_OK){e.reader=nullptr;e.delete_complete=true;e.ledger->Freeze();}
        Log("READER_DELETE_RETURN","endpoint="+e.spec.id+" reader_guid="+e.guid+" rc="+std::to_string(int(rc)));
        return rc==dds::RETCODE_OK;
    }
    void Done(const std::string& phase){Log("PHASE_DONE","phase="+phase+(phase=="CLEANUP"?" cleanup_scope=DDS":""));}
    bool Run(){
        if(!opened||closed||!Good())return Fail("run_precondition");
        if(!Wait([&]{return CurrentGraph(false,"INITIAL",false)&&Incompatibility(false);},"initial_graph_timeout"))return false;
        CurrentGraph(false,"INITIAL",true);Incompatibility(true);Done("INITIAL");
        if(!Release("PRE")||!Writes(spec->pre,"PRE")||!Wait([&]{return LedgerStage("PRE",false);},"pre_ledger_timeout"))return false;
        LedgerStage("PRE",true);Done("PRE");
        if(!Release("CHANGE"))return false;
        for(const auto& id:spec->deletions){auto it=endpoints.find(id);if(it!=endpoints.end()&&!DeleteReader(*it->second))return Fail("reader_delete_"+id);}
        for(auto& item:endpoints)if(item.second->spec.late&&!CreateEndpoint(*item.second,"CHANGE"))return false;
        if(!Wait([&]{return CurrentGraph(true,"CHANGE",false)&&LedgerStage("CHANGE",false);},"change_graph_or_history_timeout"))return false;
        CurrentGraph(true,"CHANGE",true);LedgerStage("CHANGE",true);Done("CHANGE");
        if(!Release("POST")||!Writes(spec->post,"POST")||!Wait([&]{return LedgerStage("FINAL",false);},"final_ledger_timeout"))return false;
        LedgerStage("FINAL",true);if(!CurrentGraph(true,"FINAL",true))return Fail("final_graph_changed");Done("POST");
        if(!Release("OBSERVE"))return false;
        const auto begin=Clock::now();const auto until=begin+std::chrono::milliseconds(2000);
        while(Clock::now()<until){
            if(!Good()||!CurrentGraph(true,"FINAL",false)||!LedgerStage("FINAL",false)||!Incompatibility(false)||!CheckAbort())return Fail("observation_changed");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Log("OBSERVATION","duration_ms="+std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-begin).count())+" complete=1");
        CurrentGraph(true,"FINAL",true);LedgerStage("FINAL",true);Incompatibility(true);observation_complete=true;Done("OBSERVE");
        if(!Release("CLEANUP")||!Close())return false;Done("CLEANUP");return Good();
    }
    bool Close(){
        if(closed){std::lock_guard<std::mutex> lock(failure_mutex);return cleanup_failure.empty();}
        cleaning=true;bool ok=true;
        for(auto& item:endpoints){auto& e=*item.second;
            if(e.reader&&!DeleteReader(e)){ok=false;CleanupFail("delete_reader_"+e.spec.id);}
            if(e.writer){const auto rc=participants.at(e.spec.participant)->publisher->delete_datawriter(e.writer);
                Log("WRITER_DELETE_RETURN","endpoint="+e.spec.id+" writer_guid="+e.guid+" rc="+std::to_string(int(rc)));
                if(rc==dds::RETCODE_OK){e.writer=nullptr;e.delete_complete=true;}else{ok=false;CleanupFail("delete_writer_"+e.spec.id);}}
        }
        for(auto& item:participants){auto& p=*item.second;if(!p.value)continue;
            const auto contained=p.value->delete_contained_entities();const auto removed=contained==dds::RETCODE_OK?dds::DomainParticipantFactory::get_instance()->delete_participant(p.value):dds::RETCODE_PRECONDITION_NOT_MET;
            Log("PARTICIPANT_DELETE_RETURN","participant="+std::to_string(p.id)+" contained_rc="+std::to_string(int(contained))+" rc="+std::to_string(int(removed)));
            if(removed==dds::RETCODE_OK)p.value=nullptr;else{ok=false;CleanupFail("delete_participant_"+std::to_string(p.id));}
        }
        closed=ok;return ok;
    }
};

void MatrixFixture::Impl::WriterListener::on_publication_matched(dds::DataWriter*,const dds::PublicationMatchedStatus& s){
    if(endpoint.delete_complete){owner.Fail("writer_callback_after_delete_"+endpoint.spec.id);return;}
    owner.Log("MATCH_EVENT","endpoint="+endpoint.spec.id+" guid="+endpoint.guid+" peer_guid="+Handle(s.last_subscription_handle)+" current="+std::to_string(s.current_count)+" change="+std::to_string(s.current_count_change)+" total="+std::to_string(s.total_count));
}
void MatrixFixture::Impl::ReaderListener::on_subscription_matched(dds::DataReader*,const dds::SubscriptionMatchedStatus& s){
    if(endpoint.delete_complete){endpoint.ledger->Observe(endpoint.guid,"",0,0,0,false,false);owner.Fail("reader_callback_after_delete_"+endpoint.spec.id);return;}
    owner.Log("MATCH_EVENT","endpoint="+endpoint.spec.id+" guid="+endpoint.guid+" peer_guid="+Handle(s.last_publication_handle)+" current="+std::to_string(s.current_count)+" change="+std::to_string(s.current_count_change)+" total="+std::to_string(s.total_count));
}
void MatrixFixture::Impl::WriterListener::on_offered_incompatible_qos(dds::DataWriter*,const dds::OfferedIncompatibleQosStatus& s){
    unsigned count=0;for(const auto& p:s.policies)if(p.policy_id==dds::RELIABILITY_QOS_POLICY_ID)count=p.count;
    endpoint.reliability_incompatible=count;owner.Log("INCOMPATIBLE_STATUS","source=callback endpoint="+endpoint.spec.id+" total="+std::to_string(s.total_count)+" last_policy="+std::to_string(s.last_policy_id)+" reliability_policy_count="+std::to_string(count));
}
void MatrixFixture::Impl::ReaderListener::on_requested_incompatible_qos(dds::DataReader*,const dds::RequestedIncompatibleQosStatus& s){
    if(endpoint.delete_complete){endpoint.ledger->Observe(endpoint.guid,"",0,0,0,false,false);owner.Fail("reader_callback_after_delete_"+endpoint.spec.id);return;}
    unsigned count=0;for(const auto& p:s.policies)if(p.policy_id==dds::RELIABILITY_QOS_POLICY_ID)count=p.count;
    endpoint.reliability_incompatible=count;owner.Log("INCOMPATIBLE_STATUS","source=callback endpoint="+endpoint.spec.id+" total="+std::to_string(s.total_count)+" last_policy="+std::to_string(s.last_policy_id)+" reliability_policy_count="+std::to_string(count));
}
void MatrixFixture::Impl::ReaderListener::on_data_available(dds::DataReader* reader){
    if(endpoint.delete_complete){endpoint.ledger->Observe(endpoint.guid,"",0,0,0,false,false);owner.Fail("reader_callback_after_delete_"+endpoint.spec.id);return;}
    HelloWorld sample;dds::SampleInfo info;
    for(;;){
        const auto rc=reader->take_next_sample(&sample,&info);if(rc==dds::RETCODE_NO_DATA)break;if(rc!=dds::RETCODE_OK){owner.Fail("take_sample_"+endpoint.spec.id);break;}
        // DDS reports instance state transitions separately from business data.
        // Peer teardown is legal only after the complete observation barrier;
        // retain that event without inventing a Writer GUID/SN for it.
        if(!info.valid_data&&(owner.cleaning||owner.observation_complete)&&
                (info.instance_state==dds::NOT_ALIVE_NO_WRITERS_INSTANCE_STATE||info.instance_state==dds::NOT_ALIVE_DISPOSED_INSTANCE_STATE)){
            owner.Log("DDS_INSTANCE_STATE","endpoint="+endpoint.spec.id+" reader_guid="+Guid(reader->guid())+" instance_state="+std::to_string(info.instance_state)+" valid_data=0 phase=CLEANUP business_sample=0");continue;
        }
        const auto reader_guid=Guid(reader->guid());const auto writer_guid=Guid(info.sample_identity.writer_guid());const auto sn=info.sample_identity.sequence_number();
        const auto writer=owner.FindGuid(writer_guid);const auto payload=sample.message();
        const bool payload_valid=writer&&writer->writer&&payload==owner.Payload(*writer,sample.index())&&payload.size()<128;
        const auto evidence="endpoint="+endpoint.spec.id+" reader_guid="+reader_guid+" writer_guid="+writer_guid+" sn_hi="+std::to_string(sn.high)+" sn_lo="+std::to_string(sn.low)+" ordinal="+std::to_string(sample.index())+" payload_hash="+Hash(payload)+" payload_len="+std::to_string(payload.size())+" payload_valid="+std::to_string(payload_valid)+" valid_data="+std::to_string(info.valid_data);
        const bool valid=endpoint.ledger->PublishObservation(reader_guid,writer_guid,sn.high,sn.low,sample.index(),payload_valid,info.valid_data,
            [&]{owner.Log("SAMPLE",evidence);});
        if(!valid)owner.Fail(endpoint.ledger->Failure()+"_"+endpoint.spec.id);
    }
}
void MatrixFixture::Impl::DiscoveryListener::on_participant_discovery(dds::DomainParticipant*,rtps::ParticipantDiscoveryStatus change,const dds::ParticipantBuiltinTopicData& data,bool& ignored){
    ignored=false;owner.Log("PARTICIPANT_DISCOVERY","participant="+std::to_string(participant)+" reason="+std::to_string(static_cast<unsigned>(change))+" guid="+Guid(data.guid));
}
void MatrixFixture::Impl::DiscoveryListener::on_data_writer_discovery(dds::DomainParticipant*,rtps::WriterDiscoveryStatus change,const dds::PublicationBuiltinTopicData& data,bool& ignored){
    ignored=false;owner.Discovered(participant,"writer",change==rtps::WriterDiscoveryStatus::REMOVED_WRITER?"REMOVED":change==rtps::WriterDiscoveryStatus::DISCOVERED_WRITER?"DISCOVERED":"UPDATED",data);
}
void MatrixFixture::Impl::DiscoveryListener::on_data_reader_discovery(dds::DomainParticipant*,rtps::ReaderDiscoveryStatus change,const dds::SubscriptionBuiltinTopicData& data,bool& ignored){
    ignored=false;owner.Discovered(participant,"reader",change==rtps::ReaderDiscoveryStatus::REMOVED_READER?"REMOVED":change==rtps::ReaderDiscoveryStatus::DISCOVERED_READER?"DISCOVERED":"UPDATED",data);
}
MatrixFixture::MatrixFixture(MatrixOptions o):impl_(std::make_unique<Impl>(std::move(o))){}
MatrixFixture::~MatrixFixture(){impl_->Close();}
bool MatrixFixture::OpenNative(const std::vector<Config>& configs,SocketApi api,std::shared_ptr<ProcessRuntime> runtime){
    if(!impl_->Initialize())return false;
    if(configs.size()!=impl_->participants.size()||!runtime)return impl_->Fail("native_configuration_count_or_runtime");
    unsigned index=0;for(auto& item:impl_->participants){auto& p=*item.second;const auto& c=configs.at(index++);
        dds::DomainParticipantQos qos;impl_->BaseParticipantQos(qos,p.id);p.control=std::make_shared<Control>();
        qos.transport().user_transports.push_back(std::make_shared<ExperimentalDescriptor>(c,api,p.control,runtime));
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(c.local_locator);
        qos.wire_protocol().default_unicast_locator_list.push_back(c.local_locator);
        qos.wire_protocol().builtin.initialPeersList.push_back(c.peer_locator);
        for(const auto& peer:c.additional_peers)qos.wire_protocol().builtin.initialPeersList.push_back(peer.locator);
        if(!impl_->CreateParticipant(p,qos))return false;
    }return impl_->FinishOpen();
}
bool MatrixFixture::OpenReference(std::uint16_t base_port){
    impl_->reference=true;if(base_port<1024||base_port>65000)return impl_->Fail("reference_port");
    if(!impl_->Initialize())return false;
    for(auto& item:impl_->participants){auto& p=*item.second;dds::DomainParticipantQos qos;impl_->BaseParticipantQos(qos,p.id);
        auto udp=std::make_shared<rtps::UDPv4TransportDescriptor>();udp->interfaceWhiteList.push_back("127.0.0.1");qos.transport().user_transports.push_back(udp);
        const auto locator=[&](bool board_a,unsigned participant,unsigned extra){rtps::Locator_t l;l.kind=LOCATOR_KIND_UDPv4;l.port=base_port+(board_a?0:2)+participant-1+extra;eprosima::fastdds::rtps::IPLocator::setIPv4(l,127,0,0,1);return l;};
        qos.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(locator(impl_->options.board_a,p.id,0));
        qos.wire_protocol().default_unicast_locator_list.push_back(locator(impl_->options.board_a,p.id,10));
        std::set<unsigned> remote;for(const auto& e:impl_->spec->endpoints)if(e.board_a!=impl_->options.board_a&&e.domain==p.domain)remote.insert(e.participant);
        for(auto id:remote)qos.wire_protocol().builtin.initialPeersList.push_back(locator(!impl_->options.board_a,id,0));
        if(!impl_->CreateParticipant(p,qos))return false;
    }return impl_->FinishOpen();
}
bool MatrixFixture::Run(){return impl_->Run();}
bool MatrixFixture::Close(){return impl_->Close();}
std::string MatrixFixture::FirstFailure()const{std::lock_guard<std::mutex> lock(impl_->failure_mutex);return impl_->first_failure;}
std::string MatrixFixture::CleanupFailure()const{std::lock_guard<std::mutex> lock(impl_->failure_mutex);return impl_->cleanup_failure;}
std::vector<std::shared_ptr<Control>> MatrixFixture::Controls()const{std::vector<std::shared_ptr<Control>> out;for(const auto& item:impl_->participants)if(item.second->control)out.push_back(item.second->control);return out;}
}
