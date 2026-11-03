#if !defined(FASTDDS_DSOFTBUS_EXPERIMENT_NATIVE_ACKNOWLEDGED)
#error This program requires the dedicated unqualified native experiment build.
#endif
#include "ExperimentalTransport.hpp"
#include "MatrixNativeConfig.hpp"
#include "matrix/PortableFixture.hpp"
#include "KeyedNativeConfig.hpp"
#include "keyed/PortableFixture.hpp"
#include "PerformanceNativeConfig.hpp"
#include "PerformanceObservation.hpp"
#include "perf/PortableFixture.hpp"
#include "ScenarioChecks.hpp"
#include "DualScenarioChecks.hpp"
#include "DualValidation.hpp"
#include "DualExecution.hpp"
#include "HotScenario.hpp"
#include "HotClose.hpp"
#include "DualParticipantPolicy.hpp"
#include "LossScenarioChecks.hpp"
#include "ListenerPolicy.hpp"
#include "LargeSampleContract.hpp"
#include "large_sample/ExperimentLargeSamplePubSubTypes.hpp"
#include "TokenBridge.h"
#include "token_first_entry.hpp"
#include "HelloWorldPubSubTypes.hpp"
#include <softbus_bus_center.h>
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <array>
#include <atomic>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/syscall.h>

namespace {
namespace dds = eprosima::fastdds::dds;
namespace rtps = eprosima::fastdds::rtps;
namespace v1 = fastdds_dsoftbus::dsoftbus::v1;
namespace ex = fastdds_dsoftbus::experiment;
using Clock = std::chrono::steady_clock;
constexpr char package_name[] = "com.kaihong.mdds";
constexpr unsigned domain = 73;
decltype(&::Socket) native_socket = nullptr;
int LoggedSocket(SocketInfo info) {
    std::printf("stage=NATIVE_SOCKET_CONTEXT pid=%d tid=%ld token=%llu service=%s package=%s\n",getpid(),syscall(SYS_gettid),
        static_cast<unsigned long long>(MddsTokenBridgeGetSelfToken()),info.name,info.pkgName);
    return native_socket(info); // Assigned and checked before any worker is started.
}
struct Arguments {
    bool publisher = false, dual = false; unsigned hot_cycles=0; std::string run, peer; std::uint64_t token = 0; std::size_t sample_bytes = 0;
    std::string matrix_case,keyed_case,control_directory;
    ex::PerformanceRequest performance;
    ex::LossMode loss_mode = ex::LossMode::Disabled;
};
constexpr bool loss_build_enabled =
#if defined(FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP) && FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
    true;
#else
    false;
#endif
bool Hex(const char* value, std::size_t length) {
    if (!value || std::strlen(value) != length) return false;
    for (std::size_t i=0;i<length;++i) if (!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f'))) return false;
    return true;
}
bool Parse(int argc, char** argv, Arguments& out) {
    if ((argc != 11 && argc != 12 && argc != 13 && argc != 15 && argc != 17) || std::strcmp(argv[1],"--role") ||
        (std::strcmp(argv[2],"publisher") && std::strcmp(argv[2],"subscriber")) ||
        std::strcmp(argv[3],"--run-id") || !Hex(argv[4],16) ||
        std::strcmp(argv[5],"--peer-network-id") || !Hex(argv[6],64) ||
std::strcmp(argv[7],"--expected-token") || !fastdds_dsoftbus_probe::ParseTokenArgument(argv[8],out.token) ||
        std::strcmp(argv[9],"--preflight-confirmed") || std::strcmp(argv[10],"--allow-unqualified-experiment")) return false;
    out.publisher=std::strcmp(argv[2],"publisher")==0; out.run=argv[4]; out.peer=argv[6];
    if(argc==17) {
        const char* suffix[]={argv[11],argv[12],argv[13],argv[14],argv[15],argv[16]};
        return ex::ParsePerformanceRequest(6,suffix,out.run,out.publisher,out.performance)&&
fastdds_dsoftbus_probe::AdmitTokenEntry({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,out.token});
    }
    if(argc==15 && !std::strcmp(argv[11],"--keyed-case")) {
        if(std::strcmp(argv[13],"--control-dir") || !ex::ValidKeyedRequest(argv[12],out.run,out.publisher,argv[14])) return false;
        out.keyed_case=argv[12];out.control_directory=argv[14];
return fastdds_dsoftbus_probe::AdmitTokenEntry({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,out.token});
    }
    if(argc==15 && !std::strcmp(argv[11],"--matrix-case")) {
        if(std::strcmp(argv[13],"--control-dir") || !ex::ValidMatrixRequest(argv[12],out.run,out.publisher,argv[14])) return false;
        out.matrix_case=argv[12];out.control_directory=argv[14];
return fastdds_dsoftbus_probe::AdmitTokenEntry({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,out.token});
    }
    if(argc==12) {
        if(std::strcmp(argv[11],"--dual-participant")) return false;
        out.dual=true;
    }
    if(argc==13 && !std::strcmp(argv[11],"--hot-cycles")) {
        const auto* end=argv[12]+std::strlen(argv[12]);const auto value=std::from_chars(argv[12],end,out.hot_cycles);
        if(value.ec!=std::errc{} || value.ptr!=end || !ex::ValidHotCycles(out.hot_cycles)) return false;
        out.dual=true;
    }
    if(argc>=13 && !out.hot_cycles) {
        if(std::strcmp(argv[11],"--sample-bytes")) return false;
        const auto* end=argv[12]+std::strlen(argv[12]);
        const auto size=std::from_chars(argv[12],end,out.sample_bytes);
        if(size.ec!=std::errc{} || size.ptr!=end || !out.sample_bytes || out.sample_bytes>ex::large_sample_bound ||
                !ex::ValidSampleBytes(static_cast<std::uint32_t>(out.sample_bytes))) return false;
    }
    if(argc==15) {
        if(std::strcmp(argv[13],"--controlled-loss")) return false;
        if(!std::strcmp(argv[14],"reliable")) out.loss_mode=ex::LossMode::Reliable;
        else if(!std::strcmp(argv[14],"best-effort")) out.loss_mode=ex::LossMode::BestEffort;
        else return false;
    }
    if(!ex::ValidDualOptions(out.dual,out.sample_bytes,out.loss_mode!=ex::LossMode::Disabled)) return false;
    if(!ex::ValidControlledLoss(out.loss_mode,static_cast<std::uint32_t>(out.sample_bytes),loss_build_enabled)) return false;
return fastdds_dsoftbus_probe::AdmitTokenEntry({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,out.token});
}
std::string Guid(const rtps::GUID_t& guid) { std::ostringstream out; out<<guid; return out.str(); }
ex::EndpointIdentity Identity(const rtps::GUID_t& guid) {
    ex::EndpointIdentity result;
    std::memcpy(result.data(),guid.guidPrefix.value,12);std::memcpy(result.data()+12,guid.entityId.value,4);
    return result;
}
std::string Payload(const std::string& run, unsigned index) { return "dsoftbus/"+run+"/"+std::to_string(index); }
std::string DualPayload(const std::string& run,unsigned instance,bool board_a,unsigned index) {
    return "dsoftbus/"+run+"/"+std::to_string(instance)+"/"+(board_a?"A":"B")+"/"+std::to_string(index);
}
std::string DualReadyPayload(const std::string& run,bool board_a,unsigned boundary) {
    return "dsoftbus/"+run+"/2/"+(board_a?"A":"B")+"/ready/"+(boundary==0?"pre-delete":"final");
}
using ex::ProgramIdentity;using ex::HotPayload;using ex::HotReadyPayload;
// Opaque steady_clock epoch within this process. Transport process_steady_us
// uses its own relative origin; the app field is intentionally named differently.
long long MonotonicUs() {return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();}
int Count(const char* directory) {
    DIR* handle=opendir(directory); if(!handle) return -1;
    int count=0; while(auto* entry=readdir(handle)) if(entry->d_name[0]!='.') ++count;
    if(closedir(handle)) return -1; return count;
}
class Listener final : public dds::DomainParticipantListener {
public:
    explicit Listener(std::string run,std::size_t sample_bytes,ex::LossMode loss_mode,bool publisher,unsigned instance=0,unsigned generation=0,unsigned hot_cycles=0)
        : hot_data(instance==2?2*hot_cycles+1:1),hot_ready(hot_cycles),run_(std::move(run)),sample_bytes_(sample_bytes),loss_mode_(loss_mode),publisher_(publisher),instance_(instance),generation_(generation),hot_cycles_(hot_cycles) {}
    std::atomic<unsigned> participants{0}, writers{0}, readers{0}, mask{0}, invalid{0};
    std::atomic<bool> matched_once{false};
    std::atomic<int> publication_matches{0},subscription_matches{0};
    std::atomic<unsigned> phase{0};
    ex::LossArmProgress loss_arm;
    ex::DualDiscovery discovery;
    ex::HotDataLedger hot_data;ex::HotReadyLedger hot_ready;
    std::array<ex::DualReadyState,2> ready;
    std::atomic<unsigned> ready_received{0};
    void SetControl(std::shared_ptr<ex::Control> control) { control_=std::move(control); }
    void on_participant_discovery(dds::DomainParticipant*, rtps::ParticipantDiscoveryStatus reason,
        const dds::ParticipantBuiltinTopicData& info, bool& ignored) override {
        ignored=false;
        if(hot_cycles_) {const auto s=control_->Snapshot();std::printf("stage=HOT_DISCOVERY_CONTEXT kind=PARTICIPANT run=%s instance=%u generation=%u reason=%u guid=%s application=%llu receiver=%llu token=%llu\n",run_.c_str(),instance_,generation_,unsigned(reason),Guid(info.guid).c_str(),(unsigned long long)s.application_incarnation,(unsigned long long)s.receiver_incarnation,(unsigned long long)s.logical_token);}
        if(reason==rtps::ParticipantDiscoveryStatus::DISCOVERED_PARTICIPANT) ++participants;
        if(instance_ && (reason==rtps::ParticipantDiscoveryStatus::REMOVED_PARTICIPANT || reason==rtps::ParticipantDiscoveryStatus::DROPPED_PARTICIPANT))
            discovery.RemoveParticipant(Identity(info.guid));
        std::printf("stage=DDS_PARTICIPANT_DISCOVERY run=%s instance=%u reason=%u guid=%s\n",run_.c_str(),instance_,unsigned(reason),Guid(info.guid).c_str());
    }
    void on_data_writer_discovery(dds::DomainParticipant*, rtps::WriterDiscoveryStatus reason,
        const dds::PublicationBuiltinTopicData& info, bool& ignored) override {
        ignored=false;
        if(hot_cycles_) {const auto s=control_->Snapshot();std::printf("stage=HOT_DISCOVERY_CONTEXT kind=WRITER run=%s instance=%u generation=%u reason=%u guid=%s application=%llu receiver=%llu token=%llu\n",run_.c_str(),instance_,generation_,unsigned(reason),Guid(info.guid).c_str(),(unsigned long long)s.application_incarnation,(unsigned long long)s.receiver_incarnation,(unsigned long long)s.logical_token);}
        if(reason==rtps::WriterDiscoveryStatus::DISCOVERED_WRITER) ++writers;
        if(instance_) discovery.Writer(reason,info);
        std::printf("stage=DDS_WRITER_DISCOVERY run=%s instance=%u reason=%u guid=%s topic=%s\n",run_.c_str(),instance_,unsigned(reason),Guid(info.guid).c_str(),info.topic_name.c_str());
#if defined(FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP) && FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
        if(!publisher_ && loss_mode_!=ex::LossMode::Disabled &&
                reason==rtps::WriterDiscoveryStatus::DISCOVERED_WRITER &&
                std::string(info.topic_name.c_str())=="mdds-"+run_ && std::strcmp(info.type_name.c_str(),"ExperimentLargeSample")==0 &&
                info.reliability.kind==(loss_mode_==ex::LossMode::BestEffort?dds::BEST_EFFORT_RELIABILITY_QOS:dds::RELIABLE_RELIABILITY_QOS) &&
                loss_arm.TryClaim()) {
            ex::RxDropRule rule;
            std::memcpy(rule.writer_guid.data(),info.guid.guidPrefix.value,12);
            std::memcpy(rule.writer_guid.data()+12,info.guid.entityId.value,4);
            rule.sequence_hi=0; rule.sequence_lo=1; rule.fragment_start=3; rule.fragment_count=1;
            rule.sample_size=262156; // Measured 3.6.2 ExperimentLargeSample CDR size, not DDS payload length.
            const bool armed=control_ && control_->ArmRxDrop(rule);
            std::printf("stage=CONTROLLED_LOSS_ARM_FROM_WRITER_DISCOVERY run=%s armed=%d writer_guid=%s sequence_high=0 sequence_low=1 fragment_start=3 fragment_count=1 sample_size=262156 user_reader_exists=0\n",
                run_.c_str(),armed,Guid(info.guid).c_str());
            loss_arm.Publish(armed);
        }
#endif
    }
    void on_data_reader_discovery(dds::DomainParticipant*, rtps::ReaderDiscoveryStatus reason,
        const dds::SubscriptionBuiltinTopicData& info, bool& ignored) override {
        ignored=false;
        if(hot_cycles_) {const auto s=control_->Snapshot();std::printf("stage=HOT_DISCOVERY_CONTEXT kind=READER run=%s instance=%u generation=%u reason=%u guid=%s application=%llu receiver=%llu token=%llu\n",run_.c_str(),instance_,generation_,unsigned(reason),Guid(info.guid).c_str(),(unsigned long long)s.application_incarnation,(unsigned long long)s.receiver_incarnation,(unsigned long long)s.logical_token);}
        if(reason==rtps::ReaderDiscoveryStatus::DISCOVERED_READER) ++readers;
        if(instance_) discovery.Reader(reason,info);
        std::printf("stage=DDS_READER_DISCOVERY run=%s instance=%u reason=%u guid=%s topic=%s\n",run_.c_str(),instance_,unsigned(reason),Guid(info.guid).c_str(),info.topic_name.c_str());
    }
    void on_publication_matched(dds::DataWriter*, const dds::PublicationMatchedStatus& info) override {
        publication_matches=info.current_count;
        if(info.current_count>0) matched_once=true;
        std::printf("stage=DDS_PUBLICATION_MATCH run=%s instance=%u current=%d total=%d\n",run_.c_str(),instance_,info.current_count,info.total_count);
    }
    void on_subscription_matched(dds::DataReader*, const dds::SubscriptionMatchedStatus& info) override {
        subscription_matches=info.current_count;
        if(info.current_count>0) matched_once=true;
        std::printf("stage=DDS_SUBSCRIPTION_MATCH run=%s instance=%u current=%d total=%d\n",run_.c_str(),instance_,info.current_count,info.total_count);
    }
    void on_data_available(dds::DataReader* reader) override {
        if(sample_bytes_) {
            ExperimentLargeSample sample; dds::SampleInfo info;
            while(reader->take_next_sample(&sample,&info)==dds::RETCODE_OK) {
                if(!info.valid_data) continue;
                const auto index=sample.index();
                const bool exact=ex::LargeSampleWriteReady(run_,sample_bytes_,sample);
                bool duplicate=false;
                if(exact) duplicate=(mask.fetch_or(1u<<(index-1)) & (1u<<(index-1)))!=0;
                if(!exact || duplicate) ++invalid;
                const auto& identity=info.sample_identity;
                const auto& sequence=identity.sequence_number();
                std::printf("stage=DDS_READER_SAMPLE run=%s index=%u exact=%d duplicate=%d mask=%03x payload_bytes=%zu payload_hash=%016llx writer_guid=%s sequence_high=%d sequence_low=%u type=ExperimentLargeSample\n",
                    run_.c_str(),index,exact,duplicate,mask.load(),sample.data().size(),
                    static_cast<unsigned long long>(ex::LargeSampleHash(sample)),Guid(identity.writer_guid()).c_str(),sequence.high,sequence.low);
            }
            return;
        }
        HelloWorld sample; dds::SampleInfo info;
        while(reader->take_next_sample(&sample,&info)==dds::RETCODE_OK) {
            if(!info.valid_data) continue;
            const auto index=sample.index();
            if(hot_cycles_) {
                const auto source=Identity(info.sample_identity.writer_guid());
                const bool source_exact=source==ProgramIdentity(run_,!publisher_,instance_,generation_,true,ex::EndpointKind::Writer);
                bool exact=false;
                if(index==0) {
                    unsigned cycle=hot_cycles_+1,phase=3;
                    if(instance_==2 && source_exact) for(unsigned c=0;c<=hot_cycles_;++c) for(unsigned p=0;p<3;++p)
                        if(sample.message()==HotReadyPayload(run_,!publisher_,c,p)){cycle=c;phase=p;}
                    exact=cycle<=hot_cycles_ && phase<3 && hot_ready.Receive(cycle,phase);
                    std::printf("stage=HOT_READY_RECEIVE run=%s instance=%u generation=%u cycle=%u phase=%u exact=%d source_exact=%d writer_guid=%s sequence_high=%d sequence_low=%u\n",run_.c_str(),instance_,generation_,cycle,phase,exact,source_exact,Guid(info.sample_identity.writer_guid()).c_str(),info.sample_identity.sequence_number().high,info.sample_identity.sequence_number().low);
                } else {
                    exact=hot_data.Accept(index,source_exact && sample.message()==HotPayload(run_,instance_,generation_,!publisher_,index));
                    const auto state=control_->Snapshot();
                    std::printf("stage=HOT_READER_SAMPLE run=%s instance=%u generation=%u index=%u exact=%d source_exact=%d writer_guid=%s sequence_high=%d sequence_low=%u runtime=%llu application=%llu receiver=%llu token=%llu\n",run_.c_str(),instance_,generation_,index,exact,source_exact,Guid(info.sample_identity.writer_guid()).c_str(),info.sample_identity.sequence_number().high,info.sample_identity.sequence_number().low,(unsigned long long)state.runtime_id,(unsigned long long)state.application_incarnation,(unsigned long long)state.receiver_incarnation,(unsigned long long)state.logical_token);
                }
                if(!exact) ++invalid;
                continue;
            }
            const bool source_exact=!instance_ || Identity(info.sample_identity.writer_guid())==
                ex::DualIdentity(run_,!publisher_,instance_,ex::EndpointKind::Writer);
            if(instance_ && index==0) {
                unsigned boundary=2;
                if(instance_==2 && source_exact) for(unsigned i=0;i<2;++i)
                    if(sample.message()==DualReadyPayload(run_,!publisher_,i)) boundary=i;
                bool duplicate=false;
                if(boundary<2) {
                    duplicate=(ready_received.fetch_or(1u<<boundary)&(1u<<boundary))!=0;
                    if(!duplicate) ready[boundary].Peer();
                }
                if(boundary>=2 || duplicate) ++invalid;
                std::printf("stage=DUAL_READY_RECEIVE run=%s instance=%u boundary=%u source_exact=%d duplicate=%d writer_guid=%s sequence_high=%d sequence_low=%u user_mask_unchanged=%03x\n",
                    run_.c_str(),instance_,boundary,source_exact,duplicate,Guid(info.sample_identity.writer_guid()).c_str(),
                    info.sample_identity.sequence_number().high,info.sample_identity.sequence_number().low,mask.load());
                continue;
            }
            const bool exact=index>=1 && index<=(instance_==1?6u:10u) &&
                sample.message()==(instance_?DualPayload(run_,instance_,!publisher_,index):Payload(run_,index)) && source_exact;
            bool duplicate=false;
            if(exact) duplicate=(mask.fetch_or(1u<<(index-1)) & (1u<<(index-1)))!=0;
            if(!exact || duplicate) ++invalid;
            const auto& identity=info.sample_identity;
            const auto& sequence=identity.sequence_number();
            std::printf("stage=DDS_READER_SAMPLE run=%s instance=%u phase=%u index=%u exact=%d duplicate=%d mask=%03x payload_bytes=%zu source_exact=%d writer_guid=%s sequence_high=%d sequence_low=%u\n",
                run_.c_str(),instance_,phase.load(),index,exact,duplicate,mask.load(),sample.message().size(),source_exact,
                Guid(identity.writer_guid()).c_str(),sequence.high,sequence.low);
        }
    }
private:
    const std::string run_; const std::size_t sample_bytes_; const ex::LossMode loss_mode_; const bool publisher_; const unsigned instance_,generation_,hot_cycles_;
    std::shared_ptr<ex::Control> control_; // Installed before Participant/native listener creation, never changed afterward.
};

class ParticipantRun final {
public:
    explicit ParticipantRun(Arguments args,unsigned instance=0,unsigned generation=0) : args_(std::move(args)),
        listener_(args_.run,args_.sample_bytes,args_.loss_mode,args_.publisher,instance,generation,args_.hot_cycles),
        start_(Clock::now()),instance_(instance),generation_(generation) { loss_evidence.mode=args_.loss_mode; }
    ex::ScenarioEvidence evidence;
    ex::LossEvidence loss_evidence;
    ex::LossVerdict DataVerdict() const { return ex::LossDataVerdict(evidence,loss_evidence); }
    bool DataComplete() const { return args_.dual?dual_data_pass_:DataVerdict()==ex::LossVerdict::Pass; }
    const char* failure_stage="PARTICIPANT_SETUP";
    bool Open(ex::Config config,const ex::SocketApi& api,std::shared_ptr<ex::ProcessRuntime> runtime={}) {
        shared_runtime_=static_cast<bool>(runtime);
        tasks_before_=Count("/proc/self/task");fds_before_=Count("/proc/self/fd");
        auto* factory=dds::DomainParticipantFactory::get_instance();
        eprosima::fastdds::LibrarySettings effective_settings;
        control_=std::make_shared<ex::Control>();
        listener_.SetControl(control_);
        descriptor_=std::make_shared<ex::ExperimentalDescriptor>(config,api,control_,std::move(runtime));
        dds::DomainParticipantQos qos;
        display_name_="mdds-exp-"+args_.run+(args_.publisher?"-publisher":"-subscriber")+"-"+std::to_string(instance_);
        qos.name(display_name_);
        qos.transport().use_builtin_transports=false;
        qos.transport().user_transports.clear(); qos.transport().user_transports.push_back(descriptor_);
        auto& wire=qos.wire_protocol();
        wire.builtin.discovery_config.discoveryProtocol=rtps::DiscoveryProtocol::SIMPLE;
        wire.builtin.discovery_config.leaseDuration_announcementperiod={1,0};
        wire.builtin.discovery_config.initial_announcements.count=10;
        wire.builtin.discovery_config.initial_announcements.period={0,300000000};
        wire.builtin.metatrafficMulticastLocatorList.clear();
        wire.builtin.metatrafficUnicastLocatorList.clear(); wire.builtin.metatrafficUnicastLocatorList.push_back(config.local_locator);
        wire.default_multicast_locator_list.clear(); wire.default_unicast_locator_list.clear();
        wire.default_unicast_locator_list.push_back(config.local_locator);
        wire.default_external_unicast_locators.clear();
        wire.builtin.initialPeersList.clear(); wire.builtin.initialPeersList.push_back(config.peer_locator);
        wire.builtin.mutation_tries=0;
        if(args_.dual) {ex::ApplyDualParticipantPolicy(qos);qos.wire_protocol().prefix=ex::DualGuid(ExpectedIdentity(args_.publisher,ex::EndpointKind::Participant)).guidPrefix;}
        std::printf("stage=DDS_CREATE_PARTICIPANT_BEGIN role=%s instance=%u domain=%u transports=1 builtin=0 kind=%d qualification=NOT_QUALIFIED\n",
            Role(),instance_,domain,config.local_locator.kind);
        failure_stage="PARTICIPANT_DESCRIPTOR_INPUT";
        participant_=factory->create_participant(domain,qos,&listener_,ex::ParticipantListenerMask());
        if(!participant_) return false;
        const auto& actual=participant_->get_qos();
        const bool library_still_isolated=factory->get_library_settings(effective_settings)==dds::RETCODE_OK &&
            effective_settings.intraprocess_delivery==eprosima::fastdds::INTRAPROCESS_OFF;
        isolated_=!actual.transport().use_builtin_transports && actual.transport().user_transports.size()==1 &&
            actual.transport().user_transports.front().get()==descriptor_.get() &&
            actual.wire_protocol().builtin.metatrafficMulticastLocatorList.empty() &&
            actual.wire_protocol().default_multicast_locator_list.empty() &&
            actual.wire_protocol().builtin.metatrafficUnicastLocatorList.size()==1 &&
            actual.wire_protocol().default_unicast_locator_list.size()==1 &&
            actual.wire_protocol().builtin.initialPeersList.size()==1 && library_still_isolated &&
            !participant_->get_status_mask().is_active(dds::StatusMask::data_on_readers()) &&
            *actual.wire_protocol().builtin.metatrafficUnicastLocatorList.begin()==config.local_locator &&
            *actual.wire_protocol().default_unicast_locator_list.begin()==config.local_locator &&
            *actual.wire_protocol().builtin.initialPeersList.begin()==config.peer_locator &&
            (!args_.dual || (ex::DualParticipantPolicyRequested(actual) &&
                Identity(participant_->guid())==ExpectedIdentity(args_.publisher,ex::EndpointKind::Participant)));
        std::printf("stage=DDS_PARTICIPANT_CREATED instance=%u guid=%s isolation_verified=%d intraprocess=OFF mutation_tries=0 parent_data_on_readers=MASKED\n",instance_,Guid(participant_->guid()).c_str(),isolated_);
        if(args_.dual) std::printf("stage=DUAL_MATCH_POLICY instance=%u ignore_local_endpoints=true participant_filter=FILTER_SAME_PROCESS qos_readback=%d guid_prefix=DUAL_EXPLICIT_FIXTURE process_grouping=FIXTURE_NOT_OS_PROOF runtime_match_evidence=REQUIRED\n",
            instance_,ex::DualParticipantPolicyRequested(actual));
        if(args_.dual) std::printf("stage=DUAL_EXPECTED_IDENTITIES run=%s instance=%u side=%s origin=DUAL_EXPLICIT_FIXTURE participant=%s writer=%s reader=%s remote_participant=%s remote_writer=%s remote_reader=%s\n",
            args_.run.c_str(),instance_,args_.publisher?"A":"B",Guid(participant_->guid()).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(args_.publisher,ex::EndpointKind::Writer))).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(args_.publisher,ex::EndpointKind::Reader))).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(!args_.publisher,ex::EndpointKind::Participant))).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(!args_.publisher,ex::EndpointKind::Writer))).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader))).c_str());
        if(!isolated_) return false;
        if(args_.sample_bytes) type_=std::make_unique<dds::TypeSupport>(new ExperimentLargeSamplePubSubType());
        else type_=std::make_unique<dds::TypeSupport>(new HelloWorldPubSubType());
        std::printf("stage=DDS_SAMPLE_CONFIGURATION type=%s payload_bytes=%zu sample_count=%u native_message_limit=%u sample_size_includes_CDR=1\n",
            type_->get_type_name().c_str(),args_.sample_bytes,ex::SampleCount(args_.sample_bytes),config.max_message_size);
        failure_stage="TYPE_TOPIC_ENDPOINT";
        if(type_->register_type(participant_)!=dds::RETCODE_OK) return false;
        topic_=participant_->create_topic("mdds-"+args_.run,type_->get_type_name(),dds::TOPIC_QOS_DEFAULT);
        if(!topic_) return false;
        if(args_.publisher || args_.dual) {
            publisher_=participant_->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
            if(!publisher_) return false;
            dds::DataWriterQos writer_qos;
            writer_qos.reliability().kind=RequestedReliability();
            writer_qos.history().depth=16; writer_qos.data_sharing().off();
            if(args_.dual) writer_qos.endpoint().entity_id=1;
            if(args_.sample_bytes) SetLargeSampleLimits(writer_qos.history(),writer_qos.resource_limits());
            writer_=publisher_->create_datawriter(topic_,writer_qos,&listener_);
            if(!writer_ || writer_->get_qos().data_sharing().kind()!=dds::OFF ||
                    writer_->get_qos().reliability().kind!=RequestedReliability()) return false;
            if(args_.sample_bytes && !CheckLargeSampleLimits(writer_->get_qos().history(),writer_->get_qos().resource_limits())) return false;
            if(args_.dual && Identity(writer_->guid())!=ExpectedIdentity(args_.publisher,ex::EndpointKind::Writer)) return false;
            std::printf("stage=DDS_USER_WRITER instance=%u guid=%s data_sharing=OFF reliability=%s qos_readback=VERIFIED\n",instance_,Guid(writer_->guid()).c_str(),ReliabilityName());
        }
        if(!args_.publisher || args_.dual) {
            if(ControlledLoss()) {
                failure_stage="CONTROLLED_LOSS_ARM_BEFORE_READER_CREATE";
                while(!listener_.loss_arm.Completed() && Elapsed()<20000 && !control_->Snapshot().fatal)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if(!listener_.loss_arm.Armed()) { CaptureEvidence(); PrintData(); return false; }
                // No user Reader exists until its writer's rule has been armed.
                // Its later SEDP advertisement is necessary for the publisher's
                // publication match, which is required before the first write.
                std::printf("stage=CONTROLLED_LOSS_READER_CREATE_AFTER_ARM run=%s armed=1\n",args_.run.c_str());
            }
            auto* subscriber=participant_->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
            if(!subscriber) return false;
            dds::DataReaderQos reader_qos;
            reader_qos.reliability().kind=RequestedReliability();
            reader_qos.history().depth=16; reader_qos.data_sharing().off();
            if(args_.dual) reader_qos.endpoint().entity_id=2;
            if(args_.sample_bytes) SetLargeSampleLimits(reader_qos.history(),reader_qos.resource_limits());
            reader_=subscriber->create_datareader(topic_,reader_qos,&listener_);
            if(!reader_ || reader_->get_qos().data_sharing().kind()!=dds::OFF ||
                    reader_->get_qos().reliability().kind!=RequestedReliability()) return false;
            if(args_.sample_bytes && !CheckLargeSampleLimits(reader_->get_qos().history(),reader_->get_qos().resource_limits())) return false;
            if(args_.dual && Identity(reader_->guid())!=ExpectedIdentity(args_.publisher,ex::EndpointKind::Reader)) return false;
            std::printf("stage=DDS_USER_READER instance=%u guid=%s data_sharing=OFF reliability=%s qos_readback=VERIFIED\n",instance_,Guid(reader_->guid()).c_str(),ReliabilityName());
        }

        initial_participant_=participant_->guid();
        if(writer_) initial_writer_=writer_->guid();
        if(reader_) initial_reader_=reader_->guid();
        initial_state_=control_->Snapshot();
        return true;
    }
    bool RunSingle() {
        failure_stage="SPDP_SEDP_MATCH";
        while(!listener_.matched_once && Elapsed()<20000 && !control_->Snapshot().fatal) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if(!listener_.matched_once) { CaptureEvidence(); PrintData(); return false; }
        if(ControlledLoss()) {
            loss_evidence.matching_qos_verified=VerifyMatchingQos();
            if(!loss_evidence.matching_qos_verified) {
                failure_stage="CONTROLLED_LOSS_MATCHED_QOS"; CaptureEvidence(); PrintData(); return false;
            }
        }
        failure_stage="DDS_SAMPLE_PATH";
        if(args_.publisher) {
            for(unsigned i=1;i<=ex::SampleCount(args_.sample_bytes);++i) {
                dds::ReturnCode_t rc=dds::RETCODE_ERROR;
                if(args_.sample_bytes) {
                    ExperimentLargeSample sample;
                    if(!ex::FillLargeSample(args_.run,i,args_.sample_bytes,sample) ||
                            !ex::LargeSampleWriteReady(args_.run,args_.sample_bytes,sample)) {
                        failure_stage="LARGE_SAMPLE_ADMISSION";
                        std::printf("stage=DDS_WRITE_ADMISSION_REJECT index=%u sdk_or_dds_write=NotCalled\n",i);
                        break;
                    }
                    std::printf("stage=DDS_WRITE_BEGIN run=%s index=%u payload_bytes=%zu payload_hash=%016llx type=ExperimentLargeSample\n",
                        args_.run.c_str(),i,sample.data().size(),static_cast<unsigned long long>(ex::LargeSampleHash(sample)));
                    rc=writer_->write(&sample);
                } else {
                    HelloWorld sample; sample.index(i); sample.message(Payload(args_.run,i));
                    rc=writer_->write(&sample);
                }
                if(rc==dds::RETCODE_OK) ++writes_;
                std::printf("stage=DDS_WRITE run=%s index=%u rc=%d\n",args_.run.c_str(),i,int(rc));
                if(rc!=dds::RETCODE_OK) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if(args_.loss_mode==ex::LossMode::BestEffort) {
                std::printf("stage=DDS_ACKNOWLEDGED run=%s sdk_or_dds_call=NotCalled reliability=BEST_EFFORT\n",args_.run.c_str());
                ObserveWindow(4000,"PUBLISHER_AFTER_WRITES");
            } else {
                if(ControlledLoss()) loss_evidence.matching_qos_verified=loss_evidence.matching_qos_verified && VerifyMatchingQos();
                const auto rc=writer_->wait_for_acknowledgments({5,0}); acknowledged_=rc==dds::RETCODE_OK;
                loss_evidence.ack_called=true;
                if(ControlledLoss()) loss_evidence.matching_qos_verified=loss_evidence.matching_qos_verified && VerifyMatchingQos();
                std::printf("stage=DDS_ACKNOWLEDGED run=%s rc=%d matching_qos_verified=%d\n",args_.run.c_str(),int(rc),loss_evidence.matching_qos_verified);
            }
        } else {
            const unsigned expected_mask=args_.loss_mode==ex::LossMode::BestEffort?6:ex::SampleMask(args_.sample_bytes);
            while((listener_.mask.load() & expected_mask)!=expected_mask && !listener_.invalid && Elapsed()<25000 && !control_->Snapshot().fatal)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(args_.loss_mode==ex::LossMode::BestEffort) ObserveWindow(3000,"SUBSCRIBER_AFTER_LATER_SAMPLE_WAIT");
            // Reliable publisher stops after DDS ACK; BEST_EFFORT stops after
            // its predefined observation window, without calling the ACK API.
            if((listener_.mask.load() & expected_mask)==expected_mask) {
                while(!control_->Snapshot().peer_shutdown_events && Elapsed()<30000)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                peer_close_seen_=control_->Snapshot().peer_shutdown_events>0;
                std::printf("stage=SUBSCRIBER_PEER_STOP_OBSERVED observed=%d\n",peer_close_seen_);
            }
        }
        CaptureEvidence();
        const bool complete=DataComplete() && (args_.publisher || peer_close_seen_);
        if(complete) failure_stage="NONE";
        else if(DataComplete()) failure_stage="PEER_NORMAL_STOP_OBSERVATION";
        PrintData();
        return complete;
    }
    bool Close() {
        const auto started=Elapsed();
        std::printf("stage=DDS_STOP_BEGIN run=%s instance=%u data_path=%d\n",args_.run.c_str(),instance_,DataComplete());
        if(close_state_.terminal)std::printf("stage=DDS_STOP_ALREADY_TERMINAL instance=%u original_result=%d entity_calls=NotCalled\n",instance_,close_state_.result);
        // Each bidirectional Reader remains alive while its peer's Writer
        // finishes ACK and announces deletion. This is normal public DDS
        // entity teardown, with the native channel retained until afterward.
        dds::ReturnCode_t contained_code=dds::RETCODE_ERROR;
        evidence.cleanup_observed=ex::ExecuteParticipantClose(close_state_,
            [&]{return participant_!=nullptr;},
            [&]{return args_.dual && acknowledged_ && writer_!=nullptr;},
            [&]{return DrainDualWriters();},
            [&]{contained_code=participant_->delete_contained_entities();return contained_code==dds::RETCODE_OK;},
            [&]{const auto removed=dds::DomainParticipantFactory::get_instance()->delete_participant(participant_);
                std::printf("stage=DDS_DELETE_RETURN instance=%u contained_rc=%d participant_rc=%d\n",instance_,int(contained_code),int(removed));
                return removed==dds::RETCODE_OK;},
            [&]{writer_=nullptr;reader_=nullptr;publisher_=nullptr;topic_=nullptr;},
            [&]{participant_=nullptr;},
            [&]{
                if(!control_)return false;
                control_->PrintSummary();const auto s=control_->Snapshot();
                const bool observed=shared_runtime_?s.application_detached && s.app_clean_observed:
                    s.closed && s.clean_observed && s.tx_worker_joined && s.rx_worker_joined &&
                    !s.sdk_calls_active && !s.receiver_leases && !s.tx_records && !s.rx_records && !s.tx_bytes && !s.rx_bytes &&
                    !s.live_tx_blocks && !s.live_rx_blocks && !s.live_tx_payload_bytes && !s.live_rx_payload_bytes;
                std::printf("stage=APPLICATION_DETACH_RESULT instance=%u generation=%u detached=%d app_clean=%d native_stop_required=%d runtime=%llu application=%llu receiver=%llu token=%llu\n",instance_,generation_,s.application_detached,s.app_clean_observed,!shared_runtime_,(unsigned long long)s.runtime_id,(unsigned long long)s.application_incarnation,(unsigned long long)s.receiver_incarnation,(unsigned long long)s.logical_token);
                // Include owned records settled during normal stop, including
                // a late complete controlled-loss BEST_EFFORT target.
                if(ControlledLoss())CaptureEvidence();return observed;
            });
        std::printf("stage=OBSERVABLE_STOP_END instance=%u clean=%d elapsed_ms=%lld tasks_before_provider=%d tasks_after=%d fds_before_provider=%d fds_after=%d sdk_internal_settlement=UNOBSERVED retained_callback_tombstone=PROCESS_LIFETIME\n",
            instance_,evidence.cleanup_observed,Elapsed()-started,tasks_before_,Count("/proc/self/task"),fds_before_,Count("/proc/self/fd"));
        return evidence.cleanup_observed;
    }
    bool StableIdentity() const {
        if(!participant_ || !writer_ || !reader_) return false;
        const auto state=control_->Snapshot();
        return participant_->guid()==initial_participant_ && writer_->guid()==initial_writer_ && reader_->guid()==initial_reader_ &&
            initial_state_.instance_id && state.instance_id==initial_state_.instance_id &&
            initial_state_.receiver_lease_id && state.receiver_lease_id==initial_state_.receiver_lease_id &&
            state.server_callback_slot==initial_state_.server_callback_slot &&
            state.runtime_id==initial_state_.runtime_id && state.application_incarnation==initial_state_.application_incarnation &&
            state.receiver_incarnation==initial_state_.receiver_incarnation && state.logical_token==initial_state_.logical_token;
    }
    bool WaitBothMatched() {
        failure_stage="DUAL_SPDP_SEDP_MATCH";
        while(!BothMatched() && Elapsed()<20000 && !control_->Snapshot().fatal && !listener_.invalid)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return BothMatched() && VerifyDualMatching();
    }
    bool BothMatched() const { return listener_.publication_matches==1 && listener_.subscription_matches==1; }
    bool WriteDual(unsigned index,unsigned phase) {
        listener_.phase=phase;
        if(!StableIdentity() || !BothMatched() || index<1 || index>(instance_==1?6u:10u) || index!=writes_+1) return false;
        HelloWorld sample;sample.index(index);sample.message(DualPayload(args_.run,instance_,args_.publisher,index));
        if(instance_==2 && index>=5 && !p1_delete_complete_us_) return false;
        std::printf("stage=DUAL_DDS_WRITE_BEGIN run=%s instance=%u board=%s phase=%u index=%u writer_guid=%s participant_guid=%s app_steady_us=%lld p1_delete_complete_us=%lld\n",
            args_.run.c_str(),instance_,args_.publisher?"A":"B",phase,index,Guid(writer_->guid()).c_str(),Guid(participant_->guid()).c_str(),MonotonicUs(),p1_delete_complete_us_);
        const auto rc=writer_->write(&sample);if(rc==dds::RETCODE_OK) ++writes_;
        std::printf("stage=DUAL_DDS_WRITE run=%s instance=%u board=%s phase=%u index=%u rc=%d writer_guid=%s local_ms=%lld participant_unchanged=%d\n",
            args_.run.c_str(),instance_,args_.publisher?"A":"B",phase,index,int(rc),Guid(writer_->guid()).c_str(),Elapsed(),StableIdentity());
        return rc==dds::RETCODE_OK;
    }
    bool WaitMask(unsigned mask) {
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        while((listener_.mask.load()&mask)!=mask && !listener_.invalid && !control_->Snapshot().fatal && Clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return (listener_.mask.load()&mask)==mask && !listener_.invalid && !control_->Snapshot().fatal;
    }
    bool AcknowledgeDual() {
        if(!VerifyDualWriterReader()) return false;
        const auto rc=writer_->wait_for_acknowledgments({5,0});acknowledged_=rc==dds::RETCODE_OK;
        std::printf("stage=DUAL_DDS_ACK instance=%u rc=%d matching_reader=1 writer_guid=%s\n",instance_,int(rc),Guid(writer_->guid()).c_str());
        return acknowledged_ && VerifyDualWriterReader();
    }
    unsigned Mask() const { return listener_.mask; }
    unsigned Writes() const { return writes_; }
    unsigned Invalid() const { return listener_.invalid; }
    bool IdentitySnapshot(const char* boundary) const {
        const auto s=control_->Snapshot();const bool stable=StableIdentity();
        std::printf("stage=DUAL_IDENTITY_SNAPSHOT run=%s instance=%u boundary=%s stable=%d participant_guid=%s writer_guid=%s reader_guid=%s expected_remote_writer=%s expected_remote_reader=%s state_instance=%llu receiver_lease=%llu server_callback_slot=%d client_callback_slot=%d native_owners=%llu app_steady_us=%lld\n",
            args_.run.c_str(),instance_,boundary,stable,Guid(participant_?participant_->guid():rtps::GUID_t{}).c_str(),
            Guid(writer_?writer_->guid():rtps::GUID_t{}).c_str(),Guid(reader_?reader_->guid():rtps::GUID_t{}).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(!args_.publisher,ex::EndpointKind::Writer))).c_str(),
            Guid(ex::DualGuid(ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader))).c_str(),
            static_cast<unsigned long long>(s.instance_id),static_cast<unsigned long long>(s.receiver_lease_id),s.server_callback_slot,s.client_callback_slot,
            static_cast<unsigned long long>(s.native_owners),MonotonicUs());
        return stable;
    }
    void P1Deleted(long long boundary) {p1_delete_complete_us_=boundary;}
    void SealValidation(unsigned boundary) {listener_.ready.at(boundary).Validate();}
    bool ExchangeReady(unsigned boundary) {
        auto& ready=listener_.ready.at(boundary);
        if(instance_!=2 || !ready.CanSend() || !StableIdentity()) return false;
        HelloWorld sample;sample.index(0);sample.message(DualReadyPayload(args_.run,args_.publisher,boundary));
        const auto rc=writer_->write(&sample);
        std::printf("stage=DUAL_READY_WRITE run=%s instance=2 boundary=%u rc=%d writer_guid=%s user_writes_unchanged=%u app_steady_us=%lld\n",
            args_.run.c_str(),boundary,int(rc),Guid(writer_->guid()).c_str(),writes_,MonotonicUs());
        if(rc!=dds::RETCODE_OK || !ready.MarkSent()) return false;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        while(!ready.Complete() && !listener_.invalid && !control_->Snapshot().fatal && Clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // The current-identity checks were sealed before our READY write.
        // A peer may now start its legal teardown; do not recheck its endpoints.
        return ready.Complete() && !listener_.invalid && !control_->Snapshot().fatal;
    }
    ex::HotIdentitySnapshot HotIdentity(const char* boundary) const {
        const auto v=control_->Snapshot();
        auto out=ex::MakeHotIdentity(display_name_,v,Identity(participant_?participant_->guid():initial_participant_),Identity(writer_?writer_->guid():initial_writer_),Identity(reader_?reader_->guid():initial_reader_));
        const auto d=listener_.discovery.Snapshot();
        std::printf("stage=HOT_IDENTITY run=%s side=%s pid=%d instance=%u generation=%u boundary=%s display_name=%s runtime=%llu application=%llu receiver=%llu token=%llu native_endpoint=%llu receiver_lease=%llu server_entry=%d client_entry=%d participant_guid=%s writer_guid=%s reader_guid=%s current_endpoints=%zu raw_discovery_events=%llu stable=%d app_detached=%d app_clean=%d app_steady_us=%lld\n",
            args_.run.c_str(),args_.publisher?"A":"B",getpid(),instance_,generation_,boundary,display_name_.c_str(),(unsigned long long)out.runtime,(unsigned long long)out.application,(unsigned long long)out.receiver,(unsigned long long)out.token,(unsigned long long)out.native_endpoint,(unsigned long long)out.receiver_lease,out.server_entry,out.client_entry,
            Guid(ex::DualGuid(out.participant)).c_str(),Guid(ex::DualGuid(out.writer)).c_str(),Guid(ex::DualGuid(out.reader)).c_str(),d.active.size(),(unsigned long long)d.raw_events,participant_?StableIdentity():false,v.application_detached,v.app_clean_observed,MonotonicUs());
        return out;
    }
    bool HotFresh() const {
        const auto d=listener_.discovery.Snapshot();
        const bool current=listener_.discovery.Validate(0,0,0,ExpectedIdentity(!args_.publisher,ex::EndpointKind::Writer),ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader),"mdds-"+args_.run,"HelloWorld");
        const bool matched=VerifyDualMatching();
        const bool pass=ex::HotFreshDiscovery(d.raw_events,current,matched);
        std::printf("stage=HOT_CURRENT_DISCOVERY run=%s instance=%u generation=%u expected_current=%d matched=%d raw_events=%llu current_endpoints=%zu pass=%d\n",args_.run.c_str(),instance_,generation_,current,matched,(unsigned long long)d.raw_events,d.active.size(),pass);
        return pass;
    }
    bool HotWrite(unsigned index,unsigned cycle,const char* phase) {
        if(!args_.hot_cycles || !StableIdentity() || !BothMatched() || index!=writes_+1 ||
            index>(instance_==1?1:2*args_.hot_cycles+1)) return false;
        const auto ids=control_->Snapshot();
        std::printf("stage=HOT_DDS_WRITE_BEGIN run=%s side=%s pid=%d instance=%u generation=%u cycle=%u phase=%s index=%u runtime=%llu application=%llu receiver=%llu token=%llu writer_guid=%s app_steady_us=%lld\n",args_.run.c_str(),args_.publisher?"A":"B",getpid(),instance_,generation_,cycle,phase,index,(unsigned long long)ids.runtime_id,(unsigned long long)ids.application_incarnation,(unsigned long long)ids.receiver_incarnation,(unsigned long long)ids.logical_token,Guid(writer_->guid()).c_str(),MonotonicUs());
        HelloWorld sample;sample.index(index);sample.message(HotPayload(args_.run,instance_,generation_,args_.publisher,index));
        const auto rc=writer_->write(&sample);if(rc==dds::RETCODE_OK)++writes_;
        std::printf("stage=HOT_DDS_WRITE_RETURN run=%s instance=%u generation=%u cycle=%u index=%u rc=%d\n",args_.run.c_str(),instance_,generation_,cycle,index,int(rc));
        return rc==dds::RETCODE_OK;
    }
    bool HotWait(unsigned expected) {
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        while(listener_.hot_data.Count()<expected && !listener_.invalid && !control_->Snapshot().fatal && Clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return listener_.hot_data.Complete(expected) && !listener_.invalid && !control_->Snapshot().fatal;
    }
    bool HotPath(unsigned expected) {
        const auto s=control_->Snapshot();const bool pass=isolated_&&StableIdentity()&&HotFresh()&&listener_.hot_data.Complete(expected)&&writes_==expected&&!listener_.invalid&&acknowledged_&&
            s.tx_spdp&&s.rx_spdp&&s.tx_sedp_publications&&s.rx_sedp_publications&&s.tx_sedp_subscriptions&&s.rx_sedp_subscriptions&&s.tx_user&&s.rx_user&&s.tx_send_returns&&s.receiver_returns;
        std::printf("stage=HOT_DATA_PATH run=%s instance=%u generation=%u expected=%u writes=%u received=%u invalid=%u acked=%d pass=%d tx_spdp=%llu rx_spdp=%llu tx_user=%llu rx_user=%llu receiver_returns=%llu\n",args_.run.c_str(),instance_,generation_,expected,writes_,listener_.hot_data.Count(),listener_.invalid.load(),acknowledged_,pass,(unsigned long long)s.tx_spdp,(unsigned long long)s.rx_spdp,(unsigned long long)s.tx_user,(unsigned long long)s.rx_user,(unsigned long long)s.receiver_returns);
        dual_data_pass_=pass;return pass;
    }
    bool HotExchangeReady(unsigned cycle,unsigned phase) {
        if(instance_!=2 || !StableIdentity() || !listener_.hot_ready.Seal(cycle,phase))return false;
        HelloWorld sample;sample.index(0);sample.message(HotReadyPayload(args_.run,args_.publisher,cycle,phase));
        const auto rc=writer_->write(&sample);
        std::printf("stage=HOT_READY_WRITE run=%s cycle=%u phase=%u rc=%d writer_guid=%s application=%llu user_writes_unchanged=%u\n",args_.run.c_str(),cycle,phase,int(rc),Guid(writer_->guid()).c_str(),(unsigned long long)control_->Snapshot().application_incarnation,writes_);
        if(rc!=dds::RETCODE_OK || !listener_.hot_ready.Sent(cycle,phase))return false;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        while(!listener_.hot_ready.Complete(cycle,phase)&&!listener_.invalid&&!control_->Snapshot().fatal&&Clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // Both peers sealed their current predicates before their ready write.
        // Do not require the retiring peer's endpoints to remain matched now.
        return listener_.hot_ready.Complete(cycle,phase)&&!listener_.invalid&&!control_->Snapshot().fatal;
    }
    void PrintFinalNativeSummary() const {std::printf("stage=FINAL_NATIVE_OBSERVATION instance=%u generation=%u app_closed=%d\n",instance_,generation_,!participant_);if(control_)control_->PrintSummary();}
    bool ApplicationDetached() const {const auto s=control_->Snapshot();return !participant_&&s.application_detached&&s.app_clean_observed;}
    bool DualPath(unsigned expected) {
        CaptureEvidence(); const auto s=control_->Snapshot();
        dual_path_failure_=ex::DualStepResult::Pass();
        const auto require=[&](bool ok,const char* predicate,std::string wanted,std::string observed){
            if(!ok && dual_path_failure_.ok) dual_path_failure_=ex::DualStepResult::Fail(predicate,std::move(wanted),std::move(observed),instance_);
            return ok;
        };
        require(isolated_,"exclusive_experimental_transport","true",isolated_?"true":"false");
        const bool discovered=listener_.discovery.Validate(listener_.participants,listener_.writers,listener_.readers,
            ExpectedIdentity(!args_.publisher,ex::EndpointKind::Writer),
            ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader),"mdds-"+args_.run,"HelloWorld");
        require(discovered,"current_expected_remote_endpoints","true",discovered?"true":"false");
        const bool matched=VerifyDualMatching();
        require(matched,"current_expected_matched_endpoints","writer=1,reader=1,exact_guid=true",
            "writer="+std::to_string(listener_.subscription_matches.load())+",reader="+std::to_string(listener_.publication_matches.load())+",exact_guid="+(matched?"true":"false"));
        require(Mask()==((1u<<expected)-1),"sample_mask",std::to_string((1u<<expected)-1),std::to_string(Mask()));
        require(Invalid()==0,"invalid_or_duplicate_samples","0",std::to_string(Invalid()));
        require(writes_==expected,"accepted_user_writes",std::to_string(expected),std::to_string(writes_));
        require(acknowledged_,"required_ack","true",acknowledged_?"true":"false");
        const std::pair<const char*,std::uint64_t> path[]={{"tx_spdp",s.tx_spdp},{"rx_spdp",s.rx_spdp},
            {"tx_sedp_publications",s.tx_sedp_publications},{"rx_sedp_publications",s.rx_sedp_publications},
            {"tx_sedp_subscriptions",s.tx_sedp_subscriptions},{"rx_sedp_subscriptions",s.rx_sedp_subscriptions},
            {"tx_user",s.tx_user},{"rx_user",s.rx_user},{"sdk_send_returns",s.tx_send_returns},{"receiver_returns",s.receiver_returns}};
        for(const auto& value:path) require(value.second!=0,value.first,"nonzero",std::to_string(value.second));
        const bool stable=StableIdentity();require(stable,"identity_continuity","true",stable?"true":"false");
        const bool pass=dual_path_failure_.ok;
        std::printf("stage=DUAL_INSTANCE_DATA instance=%u pass=%d writes=%u sample_mask=%03x invalid=%u participant_discoveries=%u writer_discoveries=%u reader_discoveries=%u tx_user=%llu rx_user=%llu unchanged=%d\n",
            instance_,pass,writes_,Mask(),Invalid(),listener_.participants.load(),listener_.writers.load(),listener_.readers.load(),
            static_cast<unsigned long long>(s.tx_user),static_cast<unsigned long long>(s.rx_user),StableIdentity());
        const auto identities=listener_.discovery.Snapshot();
        std::printf("stage=DUAL_CURRENT_DISCOVERY instance=%u expected_pair_active=%d raw_events=%llu current_unique_endpoints=%zu raw_counts_retained=1\n",
            instance_,discovered,static_cast<unsigned long long>(identities.raw_events),identities.active.size());
        dual_data_pass_=pass && expected==(instance_==1?6u:10u);
        return pass;
    }
    ex::DualStepResult DualPathFailure() const {return dual_path_failure_;}
    void Phase(unsigned phase) { listener_.phase=phase; }
private:
    ex::EndpointIdentity ExpectedIdentity(bool board,ex::EndpointKind kind) const {
        return ProgramIdentity(args_.run,board,instance_,generation_,args_.hot_cycles!=0,kind);
    }
    bool VerifyDualWriterReader() const {
        if(!writer_ || listener_.publication_matches!=1) return false;
        std::vector<dds::InstanceHandle_t> handles;
        if(writer_->get_matched_subscriptions(handles)!=dds::RETCODE_OK || handles.size()!=1) return false;
        dds::SubscriptionBuiltinTopicData data;
        const auto rc=writer_->get_matched_subscription_data(data,handles.front());
        const auto local=writer_->guid();
        const bool same_process=local.guidPrefix.is_on_same_process_as(data.guid.guidPrefix);
        const bool valid=ex::DualRemoteEndpoint(rc==dds::RETCODE_OK && data.reliability.kind==dds::RELIABLE_RELIABILITY_QOS &&
            std::string(data.topic_name.c_str())=="mdds-"+args_.run && std::strcmp(data.type_name.c_str(),"HelloWorld")==0,
            Identity(local),Identity(data.guid),same_process) &&
            Identity(data.guid)==ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader);
        std::printf("stage=DUAL_ACK_READER instance=%u verified=%d remote_reader=%s count=1\n",instance_,valid,Guid(data.guid).c_str());
        return valid;
    }
    bool DrainDualWriters() {
        const auto writer_guid=Guid(writer_->guid());
        const auto rc=publisher_->delete_datawriter(writer_);
        if(rc==dds::RETCODE_OK) writer_=nullptr;
        std::printf("stage=DUAL_WRITER_DELETE_RETURN instance=%u rc=%d acknowledged=%d writer_guid=%s local_reader_retained=1\n",
            instance_,int(rc),acknowledged_,writer_guid.c_str());
        if(rc!=dds::RETCODE_OK) return false;
        const auto deadline=Clock::now()+std::chrono::seconds(8);
        while(listener_.subscription_matches!=0 && Clock::now()<deadline && !control_->Snapshot().fatal)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const bool drained=listener_.subscription_matches==0 && !control_->Snapshot().fatal;
        std::printf("stage=DUAL_PEER_WRITER_DELETE_OBSERVED instance=%u observed=%d subscription_matches=%d local_reader_retained=1\n",
            instance_,drained,listener_.subscription_matches.load());
        return drained;
    }
    bool VerifyDualMatching() const {
        if(!BothMatched()) return false;
        std::vector<dds::InstanceHandle_t> subscriptions,publications;
        const auto a=writer_->get_matched_subscriptions(subscriptions),b=reader_->get_matched_publications(publications);
        if(a!=dds::RETCODE_OK || b!=dds::RETCODE_OK || subscriptions.size()!=1 || publications.size()!=1) return false;
        dds::SubscriptionBuiltinTopicData subscription;dds::PublicationBuiltinTopicData publication;
        const auto c=writer_->get_matched_subscription_data(subscription,subscriptions.front());
        const auto d=reader_->get_matched_publication_data(publication,publications.front());
        const auto local=writer_->guid();
        const bool writer_same_process=local.guidPrefix.is_on_same_process_as(publication.guid.guidPrefix);
        const bool reader_same_process=local.guidPrefix.is_on_same_process_as(subscription.guid.guidPrefix);
        const bool valid=ex::DualRemotePair(c==dds::RETCODE_OK && d==dds::RETCODE_OK &&
            subscription.reliability.kind==dds::RELIABLE_RELIABILITY_QOS && publication.reliability.kind==dds::RELIABLE_RELIABILITY_QOS &&
            std::string(subscription.topic_name.c_str())=="mdds-"+args_.run && std::string(publication.topic_name.c_str())=="mdds-"+args_.run &&
            std::strcmp(subscription.type_name.c_str(),"HelloWorld")==0 && std::strcmp(publication.type_name.c_str(),"HelloWorld")==0,
            Identity(local),Identity(publication.guid),Identity(subscription.guid),writer_same_process,reader_same_process) &&
            Identity(publication.guid)==ExpectedIdentity(!args_.publisher,ex::EndpointKind::Writer) &&
            Identity(subscription.guid)==ExpectedIdentity(!args_.publisher,ex::EndpointKind::Reader);
        std::printf("stage=DUAL_MATCHED_IDENTITIES instance=%u verified=%d local_writer=%s local_reader=%s remote_writer=%s remote_reader=%s count_writer=1 count_reader=1 same_topic=1 writer_same_process=%d reader_same_process=%d\n",
            instance_,valid,Guid(writer_->guid()).c_str(),Guid(reader_->guid()).c_str(),Guid(publication.guid).c_str(),Guid(subscription.guid).c_str(),writer_same_process,reader_same_process);
        return valid;
    }
    bool ControlledLoss() const { return args_.loss_mode!=ex::LossMode::Disabled; }
    dds::ReliabilityQosPolicyKind RequestedReliability() const {
        return args_.loss_mode==ex::LossMode::BestEffort?dds::BEST_EFFORT_RELIABILITY_QOS:dds::RELIABLE_RELIABILITY_QOS;
    }
    const char* ReliabilityName() const { return args_.loss_mode==ex::LossMode::BestEffort?"BEST_EFFORT":"RELIABLE"; }
    bool VerifyMatchingQos() const {
        std::vector<dds::InstanceHandle_t> handles;
        const auto handles_rc=args_.publisher?writer_->get_matched_subscriptions(handles):reader_->get_matched_publications(handles);
        if(handles_rc!=dds::RETCODE_OK || handles.size()!=1) {
            std::printf("stage=DDS_MATCHED_QOS role=%s handles_rc=%d count=%zu verified=0\n",Role(),int(handles_rc),handles.size());
            return false;
        }
        rtps::GUID_t remote; dds::ReturnCode_t rc=dds::RETCODE_ERROR; bool valid=false;
        if(args_.publisher) {
            dds::SubscriptionBuiltinTopicData data;
            rc=writer_->get_matched_subscription_data(data,handles.front()); remote=data.guid;
            valid=rc==dds::RETCODE_OK && data.reliability.kind==RequestedReliability() &&
                std::string(data.topic_name.c_str())=="mdds-"+args_.run && std::strcmp(data.type_name.c_str(),"ExperimentLargeSample")==0;
        } else {
            dds::PublicationBuiltinTopicData data;
            rc=reader_->get_matched_publication_data(data,handles.front()); remote=data.guid;
            valid=rc==dds::RETCODE_OK && data.reliability.kind==RequestedReliability() &&
                std::string(data.topic_name.c_str())=="mdds-"+args_.run && std::strcmp(data.type_name.c_str(),"ExperimentLargeSample")==0;
        }
        std::printf("stage=DDS_MATCHED_QOS role=%s remote_guid=%s requested=%s rc=%d verified=%d count=%zu\n",
            Role(),Guid(remote).c_str(),ReliabilityName(),int(rc),valid,handles.size());
        return valid;
    }
    void ObserveWindow(unsigned milliseconds,const char* phase) {
        const auto shutdown_before=control_->Snapshot().peer_shutdown_events;
        const auto begin=Clock::now();
        const auto deadline=begin+std::chrono::milliseconds(milliseconds);
        std::printf("stage=CONTROLLED_LOSS_OBSERVATION_BEGIN phase=%s duration_ms=%u clock=LOCAL_STEADY shutdown_before=%llu\n",
            phase,milliseconds,static_cast<unsigned long long>(shutdown_before));
        std::this_thread::sleep_until(deadline);
        const auto end=Clock::now();
        const auto shutdown_after=control_->Snapshot().peer_shutdown_events;
        loss_evidence.observation_window_complete=ex::ControlledObservationComplete(end>=deadline,shutdown_before,shutdown_after);
        std::printf("stage=CONTROLLED_LOSS_OBSERVATION_END phase=%s elapsed_ms=%lld sample_mask=%03x shutdown_before=%llu shutdown_after=%llu complete=%d peer_window_evidence=%s native_alive_proof=UNOBSERVED\n",phase,
            static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(end-begin).count()),listener_.mask.load(),
            static_cast<unsigned long long>(shutdown_before),static_cast<unsigned long long>(shutdown_after),
            loss_evidence.observation_window_complete,
            shutdown_before==0 && shutdown_after==0?"NO_SHUTDOWN_CALLBACK_OBSERVED":"SHUTDOWN_CALLBACK_OBSERVED");
    }
    static void SetLargeSampleLimits(dds::HistoryQosPolicy& history,dds::ResourceLimitsQosPolicy& limits) {
        history.kind=dds::KEEP_LAST_HISTORY_QOS; history.depth=4;
        limits.max_samples=4; limits.max_instances=1; limits.max_samples_per_instance=4;
        limits.allocated_samples=4; limits.extra_samples=1;
    }
    bool CheckLargeSampleLimits(const dds::HistoryQosPolicy& history,const dds::ResourceLimitsQosPolicy& limits) const {
        const bool valid=history.kind==dds::KEEP_LAST_HISTORY_QOS && history.depth==4 && limits.max_samples==4 &&
            limits.max_instances==1 && limits.max_samples_per_instance==4 && limits.allocated_samples==4 && limits.extra_samples==1;
        std::printf("stage=DDS_LARGE_RESOURCE_LIMITS role=%s valid=%d history_depth=%d max_samples=%d max_instances=%d max_samples_per_instance=%d allocated_samples=%d extra_samples=%d\n",
            Role(),valid,history.depth,limits.max_samples,limits.max_instances,limits.max_samples_per_instance,limits.allocated_samples,limits.extra_samples);
        return valid;
    }
    long long Elapsed() const { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start_).count(); }
    const char* Role() const { return args_.publisher?"publisher":"subscriber"; }
    void CaptureEvidence() {
        const auto s=control_->Snapshot();
        evidence.publisher=args_.publisher; evidence.exclusive_transport=isolated_;
        evidence.participant_discovered=listener_.participants>0;
        evidence.endpoint_discovered=args_.publisher?listener_.readers>0:listener_.writers>0;
        evidence.matched_once=listener_.matched_once;
        evidence.spdp_tx=s.tx_spdp; evidence.spdp_rx=s.rx_spdp;
        evidence.sedp_required_tx=args_.publisher?s.tx_sedp_publications:s.tx_sedp_subscriptions;
        evidence.sedp_required_rx=args_.publisher?s.rx_sedp_subscriptions:s.rx_sedp_publications;
        evidence.user_required=args_.publisher?s.tx_user:s.rx_user;
        evidence.receiver_returns=s.receiver_returns; evidence.sdk_send_returns=s.tx_send_returns;
        evidence.writes=writes_; evidence.acknowledged=acknowledged_;
        evidence.sample_mask=listener_.mask; evidence.invalid_samples=listener_.invalid;
        evidence.sample_bytes=args_.sample_bytes;
        evidence.fragment_observations=args_.publisher?s.tx_data_frag:s.rx_data_frag;
#if defined(FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP) && FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
        if(ControlledLoss() && !args_.publisher) {
            const auto drop=control_->DropSnapshot();
            loss_evidence.armed=drop.armed; loss_evidence.dropped_messages=drop.dropped_messages;
            loss_evidence.recovered_range_deliveries=drop.recovered_range_deliveries;
            std::printf("stage=CONTROLLED_LOSS_EVIDENCE run=%s armed=%d dropped_messages=%llu dropped_invocation=%llu dropped_len=%zu dropped_hash=%016llx recovered_range_deliveries=%llu control_causality=REQUIRES_TRACE_CORRELATION\n",
                args_.run.c_str(),drop.armed,static_cast<unsigned long long>(drop.dropped_messages),
                static_cast<unsigned long long>(drop.dropped_invocation),drop.dropped_len,
                static_cast<unsigned long long>(drop.dropped_hash),static_cast<unsigned long long>(drop.recovered_range_deliveries));
        }
#endif
    }
    void PrintData() const {
        std::printf("stage=DATA_PATH_RESULT role=%s instance=%u run=%s result=%s failure_stage=%s spdp_tx=%llu spdp_rx=%llu sedp_required_tx=%llu sedp_required_rx=%llu user_submessages=%llu writes=%u sample_mask=%03x invalid=%u acked=%d payload_bytes=%zu data_frag_submessages=%llu\n",
            Role(),instance_,args_.run.c_str(),DataResultName(),failure_stage,
            static_cast<unsigned long long>(evidence.spdp_tx),static_cast<unsigned long long>(evidence.spdp_rx),
            static_cast<unsigned long long>(evidence.sedp_required_tx),static_cast<unsigned long long>(evidence.sedp_required_rx),
            static_cast<unsigned long long>(evidence.user_required),evidence.writes,evidence.sample_mask,evidence.invalid_samples,evidence.acknowledged,
            args_.sample_bytes,static_cast<unsigned long long>(evidence.fragment_observations));
    }
public:
    const char* DataResultName() const {
        return DataVerdict()==ex::LossVerdict::Pass?"PASS":DataVerdict()==ex::LossVerdict::NotExercised?"NOT_EXERCISED":"FAIL";
    }

private:
    Arguments args_; Listener listener_; const Clock::time_point start_;const unsigned instance_,generation_;
    bool shared_runtime_=false;std::string display_name_;
    std::shared_ptr<ex::Control> control_;
    std::shared_ptr<ex::ExperimentalDescriptor> descriptor_;
    dds::DomainParticipant* participant_=nullptr; dds::Topic* topic_=nullptr;dds::Publisher* publisher_=nullptr;
    dds::DataWriter* writer_=nullptr; dds::DataReader* reader_=nullptr;
    std::unique_ptr<dds::TypeSupport> type_;
    rtps::GUID_t initial_participant_,initial_writer_,initial_reader_;
    ex::Stats initial_state_;
    bool isolated_=false,acknowledged_=false,peer_close_seen_=false,dual_data_pass_=false;
    unsigned writes_=0; int tasks_before_=-1,fds_before_=-1;
    long long p1_delete_complete_us_=0;
    ex::DualStepResult dual_path_failure_;
    ex::ParticipantCloseState close_state_;
};

class Experiment final : public fastdds_dsoftbus_probe::TokenEntryOperations {
public:
    explicit Experiment(Arguments args):args_(std::move(args)),dual_execution_(args_.run,args_.publisher?"A":"B"),hot_execution_(args_.run,args_.publisher?"A":"B") { loss_evidence.mode=args_.loss_mode; }
    ex::ScenarioEvidence evidence;ex::LossEvidence loss_evidence;
    const char* failure_stage="TOKEN_ENTRY";
    ex::LossVerdict DataVerdict() const {
        if(!args_.performance.mode.empty()) return performance_data_?ex::LossVerdict::Pass:ex::LossVerdict::Fail;
        if(!args_.keyed_case.empty()) return keyed_data_?ex::LossVerdict::Pass:ex::LossVerdict::Fail;
        if(!args_.matrix_case.empty()) return matrix_data_?ex::LossVerdict::Pass:ex::LossVerdict::Fail;
        return args_.hot_cycles?(hot_data_?ex::LossVerdict::Pass:ex::LossVerdict::Fail):args_.dual?(dual_data_?ex::LossVerdict::Pass:ex::LossVerdict::Fail):ex::LossDataVerdict(evidence,loss_evidence);
    }
    const char* DataResultName() const { return DataVerdict()==ex::LossVerdict::Pass?"PASS":DataVerdict()==ex::LossVerdict::NotExercised?"NOT_EXERCISED":"FAIL"; }
    bool Complete() const {
        if(!args_.performance.mode.empty()) return performance_data_ && evidence.cleanup_observed && evidence.token_restored;
        if(!args_.keyed_case.empty()) return keyed_data_ && evidence.cleanup_observed && evidence.token_restored;
        if(!args_.matrix_case.empty()) return matrix_data_ && evidence.cleanup_observed && evidence.token_restored;
        return args_.hot_cycles?hot_data_ && evidence.cleanup_observed && evidence.token_restored:
            args_.dual?dual_data_ && ex::DualScenarioComplete(progress_) && evidence.cleanup_observed && evidence.token_restored:
            ex::LossScenarioComplete(evidence,loss_evidence);
    }
fastdds_dsoftbus_probe::TaskCheckpoint ObserveTasks() override {
fastdds_dsoftbus_probe::TaskCheckpoint out; out.process_id=getpid(); out.current_tid=syscall(SYS_gettid);
        DIR* dir=opendir("/proc/self/task"); out.opened=dir!=nullptr;
        if(dir) {
            bool valid=true;
            for(;;) {
                errno=0; auto* item=readdir(dir); if(!item) { out.complete=valid && !errno; break; }
                if(item->d_name[0]=='.') continue;
                std::uint64_t tid=0; const auto* end=item->d_name+std::strlen(item->d_name);
                const auto value=std::from_chars(item->d_name,end,tid);
                if(value.ec!=std::errc{} || value.ptr!=end || !tid) valid=false;
                if(!out.count) out.sole_tid=tid;
                if(out.count<2) ++out.count;
            }
            if(closedir(dir)) out.complete=false;
        }
std::printf("stage=INITIAL_TASKS pid=%d single_main=%d\n",getpid(),fastdds_dsoftbus_probe::IsSingleMainTask(out));
        return out;
    }
    std::uint64_t GetSelf() override { return MddsTokenBridgeGetSelfToken(); }
    int SetSelf(std::uint64_t token) override { return MddsTokenBridgeSetSelfToken(token); }
fastdds_dsoftbus_probe::OwnedTokenObservation VerifyOwned(std::uint64_t token,const char* phase) override {
        MddsTokenBridgeResult result{};
        const int bridge_status=MddsTokenBridgeVerify(static_cast<std::uint32_t>(token),&result);
        std::printf("stage=TOKEN_VERIFY phase=%s bridge_status=%d called=%u info_rc=%d query_rc=%d count=%u granted=%d\n",
            phase,bridge_status,result.called,result.infoRc,result.permissionRc,result.permissionCount,result.granted);
fastdds_dsoftbus_probe::OwnedTokenObservation out;
        if(bridge_status || result.called!=15 || strnlen(result.processName,sizeof(result.processName))==sizeof(result.processName)) return out;
        out.lookup=result.lookupToken; out.type=result.type; out.native_type=result.nativeType;
        out.info_rc=result.infoRc; out.process_name=result.processName; out.apl=result.apl; out.normal_apl=result.normalApl;
        out.query_rc=result.permissionRc; out.permission_granted=result.permissionGranted;
        if(result.permissionCount==1) out.states.push_back(result.permissionState);
        return out;
    }
    void Diagnostic(const char* stage,std::uint64_t expected,std::uint64_t actual,int rc) override {
        std::printf("stage=%s expected=%llu actual=%llu rc=%d\n",stage,
            static_cast<unsigned long long>(expected),static_cast<unsigned long long>(actual),rc);
    }
    bool EnterProvider() override {
        failure_stage="PUBLIC_DEVICE_DIRECTORY";
        ex::Config config;
        if(!ConfigureNative(config,api_)) return false;
        failure_stage="DDS_CONFIGURATION";
        if(!EnvironmentIsolated()) return false;
        auto* factory=dds::DomainParticipantFactory::get_instance();
        eprosima::fastdds::LibrarySettings settings;settings.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
        if(factory->set_library_settings(settings)!=dds::RETCODE_OK) return false;
        eprosima::fastdds::LibrarySettings effective;
        if(factory->get_library_settings(effective)!=dds::RETCODE_OK || effective.intraprocess_delivery!=eprosima::fastdds::INTRAPROCESS_OFF) return false;
        if(!args_.performance.mode.empty()) {
            failure_stage="PERFORMANCE_NATIVE_CONFIGURATION";
            ex::Config endpoint;
            if(!ex::BuildPerformanceNativeConfig(args_.performance,args_.run,args_.publisher,config,endpoint)||
                    !ex::EnablePerformanceObservation())return false;
            std::vector<ex::Config> endpoints{endpoint};
            runtime_=ex::ProcessRuntime::Acquire(endpoints,api_);
            if(!runtime_){failure_stage="PERFORMANCE_RUNTIME_ACQUIRE";return false;}
            ex::perf::Options options;
            options.run=args_.run;options.board_a=args_.publisher;options.control_directory=args_.performance.directory;
            options.mode=args_.performance.mode=="TRACE"?ex::perf::Mode::Trace:ex::perf::Mode::Measure;
            options.repeat=args_.performance.repeat;
            options.quiet_observer=[](bool quiet){if(auto* observer=ex::CurrentPerformanceObservation())observer->SetQuiet(quiet);};
            options.observation_snapshot=[this](const char* point,unsigned stage){PerformanceSnapshot(point,stage);};
            performance_=std::make_unique<ex::perf::PerformanceFixture>(std::move(options));
            failure_stage="PERFORMANCE_APPLICATION";
            if(!performance_->OpenNative(endpoints,api_,runtime_))return false;
            performance_data_=performance_->Run();
            if(performance_data_)failure_stage="NONE";
            return performance_data_;
        }
        if(!args_.keyed_case.empty()) {
            failure_stage="KEYED_NATIVE_CONFIGURATION";
            ex::Config endpoint;
            if(!ex::BuildKeyedNativeConfig(args_.keyed_case,args_.run,args_.publisher,config,endpoint)) return false;
            std::vector<ex::Config> endpoints{endpoint};
            std::printf("stage=KEYED_NATIVE_ADDRESS run=%s case=%s instance=%s local_port=%u primary_peer_port=%u additional_peers=0 key_routing=NONE\n",args_.run.c_str(),args_.keyed_case.c_str(),endpoint.instance_name.c_str(),endpoint.local_locator.port,endpoint.peer_locator.port);
            runtime_=ex::ProcessRuntime::Acquire(endpoints,api_);
            if(!runtime_) {failure_stage="KEYED_PROCESS_RUNTIME_ACQUIRE";return false;}
            keyed_=std::make_unique<ex::keyed::KeyedFixture>(ex::keyed::Options{args_.run,args_.keyed_case,args_.publisher,args_.control_directory});
            failure_stage="KEYED_APPLICATION";
            if(!keyed_->OpenNative(endpoints,api_,runtime_)) return false;
            keyed_data_=keyed_->Run();
            if(keyed_data_) failure_stage="NONE";
            return keyed_data_;
        }
        if(!args_.matrix_case.empty()) {
            failure_stage="MATRIX_NATIVE_CONFIGURATION";
            std::vector<ex::Config> endpoints;
            if(!ex::BuildMatrixNativeConfigs(args_.matrix_case,args_.run,args_.publisher,config,matrix_keys_,endpoints)) return false;
            for(const auto& c:endpoints) std::printf("stage=MATRIX_NATIVE_ADDRESS run=%s case=%s instance=%s local_port=%u primary_peer_port=%u additional_peers=%zu local_service=%s\n",args_.run.c_str(),args_.matrix_case.c_str(),c.instance_name.c_str(),c.local_locator.port,c.peer_locator.port,c.additional_peers.size(),c.local_service.c_str());
            runtime_=ex::ProcessRuntime::Acquire(endpoints,api_);
            if(!runtime_) {failure_stage="MATRIX_PROCESS_RUNTIME_ACQUIRE";return false;}
            matrix_=std::make_unique<ex::matrix::MatrixFixture>(ex::matrix::MatrixOptions{args_.run,args_.matrix_case,args_.publisher,args_.control_directory});
            failure_stage="MATRIX_APPLICATION";
            if(!matrix_->OpenNative(endpoints,api_,runtime_)) return false;
            matrix_data_=matrix_->Run();
            if(matrix_data_) failure_stage="NONE";
            return matrix_data_;
        }
#if defined(FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP) && FASTDDS_DSOFTBUS_EXPERIMENT_RX_DROP
        // Freeze the exact capability before keyed runtime acquisition. The
        // application descriptor must not mutate that native configuration.
        config.enable_rx_drop=args_.loss_mode!=ex::LossMode::Disabled && !args_.publisher;
#endif
        std::vector<ex::Config> endpoints{config};
        if(args_.dual) {ex::Config q=config;if(!ConfigureInstance(q,2))return false;endpoints.push_back(q);}
        runtime_=ex::ProcessRuntime::Acquire(endpoints,api_);
        if(!runtime_) {failure_stage="PROCESS_RUNTIME_ACQUIRE";return false;}
        if(args_.hot_cycles) return RunHot(config);
        first_=std::make_unique<ParticipantRun>(args_,args_.dual?1u:0u);
        if(!first_->Open(config,api_,runtime_)) { failure_stage=first_->failure_stage;return false; }
        if(!args_.dual) {
            const bool result=first_->RunSingle();CopySingleEvidence();return result;
        }
        return RunDual(config);
    }
    bool CloseProvider() override {
        if(!args_.performance.mode.empty()) {
            const bool application_clean=!performance_||performance_->Close();
            const bool runtime_clean=!runtime_||runtime_->Finalize();
            if(performance_)for(const auto& control:performance_->Controls())control->PrintSummary();
            ex::PrintProcessSummary("PERFORMANCE_FINAL");const auto p=ex::GetProcessStats();
            evidence.cleanup_observed=application_clean&&runtime_clean&&!p.active_states&&!p.reserved_workers&&!p.native_owners&&
                !p.tx_records&&!p.rx_records&&!p.tx_bytes&&!p.rx_bytes&&!p.live_tx_blocks&&!p.live_rx_blocks&&
                !p.live_tx_payload_bytes&&!p.live_rx_payload_bytes;
            PerformanceSnapshot("AFTER_FINALIZE",0);FlushPerformanceSnapshots();
            if(auto* observation=ex::CurrentPerformanceObservation()) {
                const auto measured=observation->Snapshot();
                if(performance_observation_overflow_||measured.counter_overflow||measured.error_details_overflow) {
                    performance_data_=false;
                    if(std::strcmp(failure_stage,"NONE")==0)failure_stage="PERFORMANCE_OBSERVATION_OVERFLOW";
                }
            }
            std::printf("stage=PERFORMANCE_NATIVE_FINALIZED run=%s clean=%d dds_clean=%d runtime_clean=%d process_budget_settled=%d sdk_internal_settlement=UNOBSERVED\n",
                args_.run.c_str(),evidence.cleanup_observed,application_clean,runtime_clean,evidence.cleanup_observed);
            if(!evidence.cleanup_observed&&performance_data_)failure_stage="PERFORMANCE_FINAL_CLEANUP";
            return evidence.cleanup_observed;
        }
        if(!args_.keyed_case.empty()) {
            const bool application_clean=!keyed_ || keyed_->Close();
            const bool runtime_clean=!runtime_ || runtime_->Finalize();
            if(keyed_) for(const auto& control:keyed_->Controls()) control->PrintSummary();
            ex::PrintProcessSummary("KEYED_FINAL");const auto p=ex::GetProcessStats();
            evidence.cleanup_observed=application_clean && runtime_clean && !p.active_states && !p.reserved_workers && !p.native_owners &&
                !p.tx_records && !p.rx_records && !p.tx_bytes && !p.rx_bytes && !p.live_tx_blocks && !p.live_rx_blocks &&
                !p.live_tx_payload_bytes && !p.live_rx_payload_bytes;
            std::printf("stage=KEYED_NATIVE_FINALIZED run=%s case=%s clean=%d dds_clean=%d runtime_clean=%d process_budget_settled=%d sdk_internal_settlement=UNOBSERVED\n",
                args_.run.c_str(),args_.keyed_case.c_str(),evidence.cleanup_observed,application_clean,runtime_clean,evidence.cleanup_observed);
            if(!evidence.cleanup_observed && keyed_data_) failure_stage="KEYED_FINAL_CLEANUP";
            return evidence.cleanup_observed;
        }
        if(!args_.matrix_case.empty()) {
            const bool application_clean=!matrix_ || matrix_->Close();
            const bool runtime_clean=!runtime_ || runtime_->Finalize();
            if(matrix_) for(const auto& control:matrix_->Controls()) control->PrintSummary();
            ex::PrintProcessSummary("MATRIX_FINAL");const auto p=ex::GetProcessStats();
            evidence.cleanup_observed=application_clean && runtime_clean && !p.active_states && !p.reserved_workers && !p.native_owners &&
                !p.tx_records && !p.rx_records && !p.tx_bytes && !p.rx_bytes && !p.live_tx_blocks && !p.live_rx_blocks &&
                !p.live_tx_payload_bytes && !p.live_rx_payload_bytes;
            std::printf("stage=MATRIX_NATIVE_FINALIZED run=%s case=%s clean=%d dds_clean=%d runtime_clean=%d process_budget_settled=%d sdk_internal_settlement=UNOBSERVED\n",
                args_.run.c_str(),args_.matrix_case.c_str(),evidence.cleanup_observed,application_clean,runtime_clean,evidence.cleanup_observed);
            if(!evidence.cleanup_observed && matrix_data_) failure_stage="MATRIX_FINAL_CLEANUP";
            return evidence.cleanup_observed;
        }
        bool first_ok=true,second_ok=true;
        if(args_.hot_cycles) return CloseHot();
        if(args_.dual) {
            using R=ex::DualStepResult;
            const bool completed=ex::ExecuteDualCleanup(dual_execution_,[&](unsigned instance)->R {
                if(instance==1) {
                    if(first_ && !first_closed_) {first_ok=first_->Close();first_closed_=first_ok;}
                    return first_ok?R::Pass():R::Fail("P1_cleanup","true","false",1);
                }
                if(instance==2) {
                    if(second_) {second_ok=second_->Close();progress_.second_deleted=second_ok;}
                    return second_ok?R::Pass():R::Fail("P2_cleanup","true","false",2);
                }
                const bool runtime_clean=!runtime_ || runtime_->Finalize();
                if(first_)first_->PrintFinalNativeSummary();if(second_)second_->PrintFinalNativeSummary();
                ex::PrintProcessSummary();const auto p=ex::GetProcessStats();
                progress_.process_budget_settled=runtime_clean && !p.active_states && !p.reserved_workers && !p.native_owners &&
                    !p.tx_records && !p.rx_records && !p.tx_bytes && !p.rx_bytes &&
                    !p.live_tx_blocks && !p.live_rx_blocks && !p.live_tx_payload_bytes && !p.live_rx_payload_bytes;
                evidence.cleanup_observed=first_ok && second_ok && first_closed_ && progress_.second_deleted && progress_.process_budget_settled;
                std::printf("stage=DUAL_PROCESS_STOP clean=%d first_deleted=%d second_deleted=%d process_budget_settled=%d sdk_internal_settlement=UNOBSERVED\n",
                    evidence.cleanup_observed,first_closed_,progress_.second_deleted,progress_.process_budget_settled);
                return evidence.cleanup_observed?R::Pass():R::Fail("final_adapter_cleanup","P1=1,P2=1,process_settled=1",
                    "P1="+std::to_string(first_closed_)+",P2="+std::to_string(progress_.second_deleted)+",process_settled="+std::to_string(progress_.process_budget_settled),0);
            });
            evidence.cleanup_observed=evidence.cleanup_observed && completed;
            if(!evidence.cleanup_observed && !dual_execution_.first_failure.present) failure_stage="DUAL_FINAL_CLEANUP";
        } else if(first_) {
            if(!first_closed_) first_closed_=first_->Close();CopySingleEvidence();
            const bool native_clean=!runtime_ || runtime_->Finalize();first_->PrintFinalNativeSummary();ex::PrintProcessSummary("SINGLE_FINAL");const auto p=ex::GetProcessStats();
            evidence.cleanup_observed=evidence.cleanup_observed&&native_clean&&!p.active_states&&!p.reserved_workers&&!p.native_owners&&!p.tx_records&&!p.rx_records&&!p.live_tx_blocks&&!p.live_rx_blocks;
            std::printf("stage=PROCESS_RUNTIME_FINALIZED mode=SINGLE clean=%d sdk_internal_settlement=UNOBSERVED\n",evidence.cleanup_observed);
        }
        return evidence.cleanup_observed;
    }
private:
    void CopySingleEvidence() {
        evidence=first_->evidence;loss_evidence=first_->loss_evidence;failure_stage=first_->failure_stage;
    }
    void RefreshDual() {
        progress_.first_written=first_->Writes();progress_.first_received=first_->Mask();
        progress_.invalid=first_->Invalid();
        if(second_) {
            progress_.second_written=second_->Writes();progress_.second_received=second_->Mask();
            progress_.invalid+=second_->Invalid();progress_.second_identity_unchanged=second_->StableIdentity();
        }
    }
    bool HotResources(const char* boundary,unsigned cycle,unsigned expected_apps) {
        const auto r=runtime_->Snapshot();const auto p=ex::GetProcessStats();
        const auto threads=Count("/proc/self/task"),fds=Count("/proc/self/fd");
        const auto thread_text=threads<0?std::string("UNOBSERVED"):std::to_string(threads),fd_text=fds<0?std::string("UNOBSERVED"):std::to_string(fds);
        const bool valid=getpid()==initial_pid_ && r.runtime_id && !r.stopped && r.active_applications==expected_apps &&
            r.active_receivers==expected_apps && r.active_native_endpoints==2 && r.retained_applications<=2 &&
            p.active_states==2 && p.reserved_workers==4 && p.native_owners<=ex::ProcessStats::native_owner_limit &&
            p.tx_bytes+p.rx_bytes<=ex::ProcessStats::byte_limit;
        std::printf("stage=HOT_RESOURCE_SAMPLE run=%s side=%s pid=%d initial_pid=%d cycle=%u boundary=%s valid=%d thread_count=%s fd_count=%s runtime=%llu active_applications=%llu active_receivers=%llu active_native_endpoints=%llu retained_applications=%llu native_owners=%llu queued_tx=%llu queued_rx=%llu queued_bytes=%llu copy_active=%llu delivery_active=%llu exposed_entries=%llu tombstone_entries=%llu live_tx_blocks=%llu live_rx_blocks=%llu retired_token_count=UNOBSERVED sdk_internal_settlement=UNOBSERVED\n",
            args_.run.c_str(),args_.publisher?"A":"B",getpid(),initial_pid_,cycle,boundary,valid,thread_text.c_str(),fd_text.c_str(),(unsigned long long)r.runtime_id,(unsigned long long)r.active_applications,(unsigned long long)r.active_receivers,(unsigned long long)r.active_native_endpoints,(unsigned long long)r.retained_applications,(unsigned long long)r.native_owners,(unsigned long long)r.queued_tx,(unsigned long long)r.queued_rx,(unsigned long long)r.queued_bytes,(unsigned long long)r.copy_active,(unsigned long long)r.delivery_active,(unsigned long long)r.exposed_entries,(unsigned long long)r.tombstone_entries,(unsigned long long)p.live_tx_blocks,(unsigned long long)p.live_rx_blocks);
        return valid;
    }
    bool RunHot(const ex::Config& first_config) {
        using R=ex::DualStepResult;
        initial_pid_=getpid();
        const auto check=[](bool pass,const char* predicate,unsigned instance){return pass?R::Pass():R::Fail(predicate,"true","false",instance);};
        const auto step=[&](const char* stage,unsigned cycle,unsigned instance,auto action){
            const bool pass=hot_execution_.Step(stage,cycle,instance,action);
            if(!pass)failure_stage=hot_execution_.first_failure.stage.c_str();return pass;
        };
        ex::Config q_config=first_config;
        if(!step("HOT_INITIAL_CREATE",0,0,[&]{
            first_=std::make_unique<ParticipantRun>(args_,1);
            if(!first_->Open(first_config,api_,runtime_))return check(false,"P1_real_create",1);
            if(!ConfigureInstance(q_config,2))return check(false,"Q_config",2);
            second_=std::make_unique<ParticipantRun>(args_,2);
            return check(second_->Open(q_config,api_,runtime_),"Q_real_create",2);
        }))return false;
        if(!step("HOT_INITIAL_MATCH",0,0,[&]{return check(first_->WaitBothMatched()&&second_->WaitBothMatched(),"initial_exact_matches",0);}))return false;
        if(!step("HOT_INITIAL_DATA",0,0,[&]{
            return check(first_->HotWrite(1,0,"INITIAL")&&second_->HotWrite(1,0,"INITIAL")&&first_->HotWait(1)&&second_->HotWait(1)&&
                first_->AcknowledgeDual()&&second_->AcknowledgeDual()&&first_->HotPath(1)&&second_->HotPath(1),"initial_bidirectional_data_ack",0);
        }))return false;
        const auto q_identity=second_->HotIdentity("INITIAL");
        auto p_identity=first_->HotIdentity("INITIAL");
        std::vector<ex::HotIdentitySnapshot> audit; audit.reserve(args_.hot_cycles+1);audit.push_back(p_identity);
        if(!step("HOT_SHARED_RUNTIME_VALIDATE",0,0,[&]{return check(p_identity.runtime==q_identity.runtime&&p_identity.application!=q_identity.application&&
            p_identity.receiver!=q_identity.receiver&&p_identity.token!=q_identity.token&&HotResources("INITIAL",0,2),"two_attachments_one_runtime",0);}))return false;
        for(unsigned cycle=1;cycle<=args_.hot_cycles;++cycle) {
            hot_cycle_=cycle;
            if(!step("HOT_PRE_DELETE_SNAPSHOT",cycle,0,[&]{
                p_identity=first_->HotIdentity("PRE_DELETE");
                return check(first_->HotPath(1)&&second_->HotPath(2*cycle-1)&&
                    ex::HotContinuous(q_identity,second_->HotIdentity("PRE_DELETE")),"old_P1_and_original_Q_sealed",0);
            }))return false;
            if(!step("HOT_PRE_DELETE_READY",cycle,2,[&]{return check(second_->HotExchangeReady(cycle,0),"peer_pre_delete_sealed",2);}))return false;
            if(!step("HOT_OLD_DELETE",cycle,1,[&]{
                std::printf("stage=HOT_OLD_DELETE_BEGIN run=%s cycle=%u pid=%d application=%llu app_steady_us=%lld\n",args_.run.c_str(),cycle,getpid(),(unsigned long long)p_identity.application,MonotonicUs());
                first_closed_=first_->Close();
                if(!first_closed_||!first_->ApplicationDetached())return check(false,"old_P1_real_delete_and_drain",1);
                first_->HotIdentity("DETACHED_AUDIT");
                // Destroy the old facade/control/listener after its actual drain;
                // only this bounded plain identity audit survives the generation.
                first_.reset();
                std::printf("stage=HOT_OLD_DELETE_COMPLETE run=%s cycle=%u pid=%d old_application=%llu Q_rebuilt=0 old_active_attachment=0 app_steady_us=%lld\n",args_.run.c_str(),cycle,getpid(),(unsigned long long)p_identity.application,MonotonicUs());
                return check(HotResources("GAP_BEGIN",cycle,1)&&ex::HotContinuous(q_identity,second_->HotIdentity("GAP_BEGIN")),"Q_and_runtime_survive",2);
            }))return false;
            if(!step("HOT_GAP_DATA",cycle,2,[&]{return check(second_->HotWrite(2*cycle,cycle,"GAP")&&second_->HotWait(2*cycle)&&second_->AcknowledgeDual()&&second_->HotPath(2*cycle),"Q_new_gap_data_ack",2);}))return false;
            if(!step("HOT_GAP_VALIDATE",cycle,2,[&]{return check(ex::HotContinuous(q_identity,second_->HotIdentity("GAP_END"))&&HotResources("GAP_END",cycle,1)&&second_->HotExchangeReady(cycle,1),"both_real_application_gaps_witnessed",2);}))return false;
            if(!step("HOT_RECREATE",cycle,1,[&]{
                std::printf("stage=HOT_RECREATE_BEGIN run=%s cycle=%u pid=%d same_display_name=%s app_steady_us=%lld\n",args_.run.c_str(),cycle,getpid(),p_identity.name.c_str(),MonotonicUs());
                first_=std::make_unique<ParticipantRun>(args_,1,cycle);first_closed_=false;
                const bool created=first_->Open(first_config,api_,runtime_);
                std::printf("stage=HOT_RECREATE_COMPLETE run=%s cycle=%u pid=%d created=%d app_steady_us=%lld\n",args_.run.c_str(),cycle,getpid(),created,MonotonicUs());
                return check(created,"new_P1_real_create",1);
            }))return false;
            if(!step("HOT_REMATCH",cycle,1,[&]{return check(first_->WaitBothMatched(),"new_generation_exact_match",1);}))return false;
            if(!step("HOT_REDISCOVERY",cycle,1,[&]{return check(first_->HotFresh(),"new_current_discovery_events",1);}))return false;
            if(!step("HOT_IDENTITY_VALIDATE",cycle,1,[&]{
                const auto current=first_->HotIdentity("NEW_MATCHED");
                if(!ex::HotReplacement(p_identity,current))return check(false,"same_name_new_app_receiver_token_live_native",1);
                for(const auto& old:audit)if(old.application==current.application||old.receiver==current.receiver||old.token==current.token)
                    return check(false,"all_prior_lifecycle_ids_unique",1);
                audit.push_back(current);return check(HotResources("NEW_MATCHED",cycle,2),"bounded_active_runtime",1);
            }))return false;
            if(!step("HOT_NEW_DATA",cycle,1,[&]{return check(first_->HotWrite(1,cycle,"NEW")&&first_->HotWait(1)&&first_->AcknowledgeDual()&&first_->HotPath(1),"new_P1_bidirectional_data_ack",1);}))return false;
            if(!step("HOT_Q_POST_DATA",cycle,2,[&]{return check(second_->HotWrite(2*cycle+1,cycle,"POST_RECREATE")&&second_->HotWait(2*cycle+1)&&second_->AcknowledgeDual()&&second_->HotPath(2*cycle+1),"Q_new_post_recreate_data_ack",2);}))return false;
            if(!step("HOT_Q_CONTINUITY_VALIDATE",cycle,2,[&]{return check(ex::HotContinuous(q_identity,second_->HotIdentity("POST_RECREATE"))&&second_->StableIdentity()&&HotResources("CYCLE_COMPLETE",cycle,2),"original_Q_entire_lifecycle_unchanged",2);}))return false;
            if(!step("HOT_CYCLE_READY",cycle,2,[&]{return check(second_->HotExchangeReady(cycle,2),"both_cycle_data_snapshots_sealed",2);}))return false;
            std::printf("stage=HOT_CYCLE_RESULT run=%s cycle=%u result=PASS pid=%d generations_audited=%zu audit_bound=101 no_app_adoption=HOST_CONTRACT old_native_frame_isolation=BLOCKED_PF02\n",args_.run.c_str(),cycle,getpid(),audit.size());
        }
        hot_data_=true;failure_stage="NONE";
        std::printf("stage=HOT_DATA_RESULT run=%s cycles=%u result=PASS same_process=1 same_display_name=1 Q_continuity=1 functional_only=1 PF01=BLOCKED PF02=BLOCKED sdk_internal_settlement=UNOBSERVED\n",args_.run.c_str(),args_.hot_cycles);
        return true;
    }
    bool CloseHot() {
        using R=ex::DualStepResult;bool ok=true;
        const auto cleanup=[&](unsigned instance,auto action){try {if(!action()){hot_execution_.Cleanup(R::Fail("owned_cleanup","true","false",instance));ok=false;}}
            catch(...){hot_execution_.Cleanup(R::Fail("cleanup_exception","normal_return","exception",instance));ok=false;}};
        cleanup(1,[&]{if(first_&&!first_closed_)first_closed_=first_->Close();return !first_||first_closed_;});
        cleanup(2,[&]{return !second_||second_->Close();});
        cleanup(0,[&]{return !runtime_||runtime_->Finalize();});
        if(first_)first_->PrintFinalNativeSummary();if(second_)second_->PrintFinalNativeSummary();
        const auto p=ex::GetProcessStats();const auto r=runtime_?runtime_->Snapshot():ex::RuntimeStats{};
        const bool settled=!p.active_states&&!p.reserved_workers&&!p.native_owners&&!p.tx_records&&!p.rx_records&&!p.tx_bytes&&!p.rx_bytes&&!p.live_tx_blocks&&!p.live_rx_blocks&&!p.live_tx_payload_bytes&&!p.live_rx_payload_bytes;
        if(!settled){hot_execution_.Cleanup(R::Fail("final_adapter_budget","zero","nonzero",0));ok=false;}
        evidence.cleanup_observed=ok;ex::PrintProcessSummary("HOT_FINAL");
        std::printf("stage=HOT_FINAL_RUNTIME run=%s pid=%d clean=%d stopped=%d active_applications=%llu active_receivers=%llu retained_applications=%llu callback_tombstones=%llu sdk_internal_settlement=UNOBSERVED\n",args_.run.c_str(),getpid(),ok,r.stopped,(unsigned long long)r.active_applications,(unsigned long long)r.active_receivers,(unsigned long long)r.retained_applications,(unsigned long long)r.tombstone_entries);
        if(ok)hot_execution_.Cleanup(R::Pass());
        if(!ok&&!hot_execution_.first_failure.present)failure_stage="HOT_FINAL_CLEANUP";
        return ok;
    }
    bool RunDual(const ex::Config& first_config) {
        using S=ex::DualStage;using R=ex::DualStepResult;
        const auto check=[](bool ok,const char* predicate,unsigned instance){return ok?R::Pass():R::Fail(predicate,"true","false",instance);};
        const bool result=ex::ExecuteDual(dual_execution_,[&](S stage)->R {
            switch(stage) {
            case S::FirstWorking:
                if(!first_->WaitBothMatched()) return check(false,"P1_expected_match",1);
                progress_.first_matching=true;
                for(unsigned index=1;index<=2;++index) {
                    if(!first_->WriteDual(index,1)) return check(false,"P1_initial_write",1);
                    if(!first_->WaitMask((1u<<index)-1)) return check(false,"P1_initial_receive",1);
                }
                RefreshDual();return check(ex::CanCreateSecond(progress_),"P1_initial_data",1);
            case S::SecondCreate: {
                ex::Config config=first_config;
                if(!ConfigureInstance(config,2)) return check(false,"P2_address_configuration",2);
                second_=std::make_unique<ParticipantRun>(args_,2);
                return check(second_->Open(config,api_,runtime_),"P2_create",2);
            }
            case S::SecondMatch:
                progress_.second_matching=second_->WaitBothMatched();return check(progress_.second_matching,"P2_expected_match",2);
            case S::OverlapData:
                std::printf("stage=DUAL_OVERLAP_BEGIN board=%s both_instances_live=1 same_topic=1 no_rx_injection=1\n",args_.publisher?"A":"B");
                for(unsigned round=1;round<=4;++round) {
                    if(!first_->WriteDual(round+2,2)) return check(false,"P1_overlap_write",1);
                    if(!second_->WriteDual(round,2)) return check(false,"P2_overlap_write",2);
                    if(!first_->WaitMask((1u<<(round+2))-1)) return check(false,"P1_overlap_receive",1);
                    if(!second_->WaitMask((1u<<round)-1)) return check(false,"P2_overlap_receive",2);
                    ++progress_.overlap_rounds;
                }
                return R::Pass();
            case S::OverlapAck:
                progress_.first_ack=first_->AcknowledgeDual();
                if(!progress_.first_ack) return check(false,"P1_overlap_ack",1);
                progress_.second_ack=second_->AcknowledgeDual();
                return check(progress_.second_ack,"P2_overlap_ack",2);
            case S::PreDeleteValidate:
                RefreshDual();
                if(!ex::CanDeleteFirst(progress_)) return check(false,"overlap_prerequisites",0);
                if(!first_->DualPath(6)) return first_->DualPathFailure();
                if(!second_->DualPath(4)) return second_->DualPathFailure();
                if(!second_->IdentitySnapshot("PRE_DELETE")) return check(false,"P2_pre_delete_identity",2);
                second_->SealValidation(0);return R::Pass();
            case S::PreDeleteReady:
                return check(second_->ExchangeReady(0),"both_pre_delete_snapshots_sealed",2);
            case S::P1Delete: {
                std::printf("stage=DUAL_FIRST_DELETE_BEGIN second_unchanged=%d second_matching=%d\n",second_->StableIdentity(),second_->BothMatched());
                first_closed_=first_->Close();progress_.first_deleted=first_closed_;
                const auto p=ex::GetProcessStats();
                std::printf("stage=DUAL_FIRST_DELETE_RETURN first_clean=%d second_unchanged=%d second_matching=%d active_states=%llu reserved_workers=%llu process_permission_retained=1\n",
                    first_closed_,second_->StableIdentity(),second_->BothMatched(),static_cast<unsigned long long>(p.active_states),static_cast<unsigned long long>(p.reserved_workers));
                if(!first_closed_) return check(false,"P1_close_observed",1);
                if(!second_->StableIdentity()) return check(false,"P2_identity_unchanged",2);
                if(!second_->BothMatched()) return check(false,"P2_still_matched",2);
                if(p.active_states!=2 || p.reserved_workers!=4 || runtime_->Snapshot().active_applications!=1) return R::Fail("P2_application_on_live_runtime","native_endpoints=2,workers=4,applications=1",
                    "active_states="+std::to_string(p.active_states)+",workers="+std::to_string(p.reserved_workers),2);
                const auto boundary=MonotonicUs();second_->P1Deleted(boundary);
                std::printf("stage=P1_DELETE_COMPLETE run=%s side=%s app_steady_us=%lld P2_recreated=0\n",args_.run.c_str(),args_.publisher?"A":"B",boundary);
                return check(second_->IdentitySnapshot("AFTER_P1_DELETE"),"P2_after_delete_identity",2);
            }
            case S::PostDeleteData:
                if(!second_->WriteDual(5,3)) return check(false,"P2_post_delete_write_5",2);
                if(!second_->WaitMask(0x1f)) return check(false,"P2_post_delete_receive_5",2);
                RefreshDual();if(!ex::CanPublishAfterBothDeletes(progress_)) return check(false,"both_deletions_witnessed",2);
                std::printf("stage=DUAL_BOTH_DELETIONS_WITNESSED local_delete_returned=1 peer_sample5_received=1 second_unchanged=%d\n",second_->StableIdentity());
                for(unsigned index=6;index<=10;++index) {
                    if(!second_->WriteDual(index,4)) return check(false,"P2_post_delete_write",2);
                    if(!second_->WaitMask((1u<<index)-1)) return check(false,"P2_post_delete_receive",2);
                    ++progress_.after_both_delete_writes;
                }
                return R::Pass();
            case S::PostDeleteAck:
                progress_.second_ack=second_->AcknowledgeDual();return check(progress_.second_ack,"P2_post_delete_ack",2);
            case S::PostDeleteValidate:
                RefreshDual();
                if(!second_->DualPath(10)) return second_->DualPathFailure();
                if(!ex::CanPublishAfterBothDeletes(progress_)) return check(false,"both_deletions_data_prerequisites",2);
                if(!second_->IdentitySnapshot("POST_DELETE_COMPLETE")) return check(false,"P2_final_identity",2);
                second_->SealValidation(1);dual_data_=true;
                std::printf("stage=DUAL_DATA_RESULT pass=%d overlap_rounds=%u after_both_delete_writes=%u first_received=%03x second_received=%03x invalid=%u\n",
                    dual_data_,progress_.overlap_rounds,progress_.after_both_delete_writes,progress_.first_received,progress_.second_received,progress_.invalid);
                return check(dual_data_,"P2_post_delete_data_path",2);
            case S::FinalReady:
                return check(second_->ExchangeReady(1),"both_final_snapshots_sealed",2);
            default:return check(false,"unexpected_stage",0);
            }
        });
        failure_stage=result?"NONE":ex::DualStageName(dual_execution_.first_failure.stage);
        return result;
    }
    struct PerformanceBoundary {
        char point[24]{};unsigned stage=0;long long local_ns=0;
        ex::PerformanceObservationSnapshot observation;
        ex::Stats application;bool application_known=false;
        ex::ProcessStats process;ex::RuntimeStats runtime;
        ex::RxAdmissionSummary admission;
    };
    void PerformanceSnapshot(const char* point,unsigned stage) {
        if(std::strcmp(point,"STAGE_FLUSH")==0){FlushPerformanceSnapshots();return;}
        if(!performance_boundaries_)performance_boundaries_=std::make_unique<std::array<PerformanceBoundary,64>>();
        if(performance_boundary_count_>=performance_boundaries_->size()){performance_observation_overflow_=true;return;}
        auto& b=(*performance_boundaries_)[performance_boundary_count_++];
        std::snprintf(b.point,sizeof(b.point),"%s",point);b.stage=stage;
        b.local_ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if(auto* o=ex::CurrentPerformanceObservation())b.observation=o->Snapshot();
        if(performance_){const auto controls=performance_->Controls();if(!controls.empty()){b.application=controls.front()->Snapshot();b.application_known=true;}}
        b.process=ex::GetProcessStats();
        if(runtime_){b.runtime=runtime_->Snapshot();b.admission=runtime_->ReceiveAdmissionSummary();}
        if(std::strcmp(point,"AFTER_DDS_CLOSE")==0)FlushPerformanceSnapshots();
    }
    void FlushPerformanceSnapshots() {
        const auto prefix=[&](std::ostringstream& out){out<<std::boolalpha<<"{\"run\":\""<<args_.run<<"\",\"side\":\""<<(args_.publisher?"A":"B")<<"\",\"mode\":\""<<args_.performance.mode<<"\",\"repeat\":"<<args_.performance.repeat;};
        for(;performance_boundary_printed_<performance_boundary_count_;++performance_boundary_printed_) {
            const auto& b=(*performance_boundaries_)[performance_boundary_printed_];const auto& o=b.observation;
            std::ostringstream out;prefix(out);
            out<<",\"point\":\""<<b.point<<"\",\"stage\":"<<b.stage<<",\"steady_ns\":"<<b.local_ns
               <<",\"scope\":\"CUMULATIVE_PROCESS_OBSERVATION_NOT_ATOMIC\",\"spans\":"<<o.spans<<",\"suppressed\":"<<o.suppressed
               <<",\"send_calls\":"<<o.send_calls<<",\"send_attempt_bytes\":"<<o.send_attempt_bytes<<",\"send_returns\":"<<o.send_returns
               <<",\"send_success_bytes\":"<<o.send_success_bytes<<",\"send_failed_bytes\":"<<o.send_failed_bytes<<",\"send_failures\":"<<o.send_failures
               <<",\"owned_copy_messages\":"<<o.owned_copy_messages<<",\"owned_copy_bytes\":"<<o.owned_copy_bytes
               <<",\"nonzero_values\":"<<o.nonzero_values<<",\"error_details_stored\":"<<o.error_details_stored<<",\"error_details_overflow\":"<<o.error_details_overflow
               <<",\"counter_overflow\":"<<o.counter_overflow<<",\"quiet\":"<<o.quiet<<",\"queue_hwm_exact\":null,\"wire_bytes\":null}";
            std::printf("PERF_OBSERVATION %s\n",out.str().c_str());
            const auto& p=b.process;std::ostringstream adapter;prefix(adapter);
            adapter<<",\"point\":\""<<b.point<<"\",\"stage\":"<<b.stage<<",\"scope\":\"BOUNDARY_SAMPLE_NOT_HIGH_WATER\""
                <<",\"tx_records\":"<<p.tx_records<<",\"tx_bytes\":"<<p.tx_bytes<<",\"rx_records\":"<<p.rx_records<<",\"rx_bytes\":"<<p.rx_bytes
                <<",\"live_tx_payload_bytes\":"<<p.live_tx_payload_bytes<<",\"live_rx_payload_bytes\":"<<p.live_rx_payload_bytes
                <<",\"active_native_owners\":"<<p.native_owners<<",\"active_applications\":"<<b.runtime.active_applications<<",\"active_receivers\":"<<b.runtime.active_receivers
                <<",\"application_snapshot_known\":"<<b.application_known<<",\"tx_rejected\":";
            if(b.application_known)adapter<<b.application.tx_rejected;else adapter<<"null";
            adapter<<",\"rx_admission_total\":"<<b.admission.total<<",\"rx_admission_stored\":"<<b.admission.stored<<",\"rx_admission_overflow\":"<<b.admission.overflow<<",\"reason_totals\":[";
            for(std::size_t i=0;i<b.admission.reasons.size();++i)adapter<<(i?",":"")<<b.admission.reasons[i];
            adapter<<"]}";std::printf("PERF_ADAPTER %s\n",adapter.str().c_str());
        }
        if(auto* observation=ex::CurrentPerformanceObservation()) {
            ex::PerformanceErrorRecord e;
            while(performance_error_printed_<ex::PerformanceObservation::detail_capacity&&observation->ErrorRecord(performance_error_printed_,e)) {
                std::ostringstream out;prefix(out);
                out<<",\"index\":"<<performance_error_printed_<<",\"event\":\""<<e.event<<"\",\"value\":"<<e.value<<",\"invocation\":"<<e.invocation
                   <<",\"instance\":"<<e.instance<<",\"receiver\":"<<e.receiver<<",\"socket\":"<<e.socket<<",\"bytes\":"<<e.bytes<<",\"steady_ns\":"<<e.steady_ns
                   <<",\"classification\":\"OBSERVED_NONZERO_NOT_AUTOMATICALLY_ERROR\"}";
                std::printf("PERF_NATIVE_EVENT_VALUE %s\n",out.str().c_str());++performance_error_printed_;
            }
        }
        std::printf("PERF_OBSERVATION_STORAGE {\"run\":\"%s\",\"side\":\"%s\",\"capacity\":64,\"stored\":%zu,\"overflow\":%s,\"bytes\":%zu}\n",
            args_.run.c_str(),args_.publisher?"A":"B",performance_boundary_count_,performance_observation_overflow_?"true":"false",performance_boundaries_?sizeof(*performance_boundaries_):0);
        std::fflush(stdout);
    }
    bool EnvironmentIsolated() {
        for(const char* key : {"ROS_DISCOVERY_SERVER","ROS_SUPER_CLIENT","FASTDDS_BUILTIN_TRANSPORTS", "FASTDDS_DEFAULT_PROFILES_FILE", "FASTRTPS_DEFAULT_PROFILES_FILE", "FASTDDS_ENVIRONMENT_FILE"}) {
            const auto* value=std::getenv(key); if(value && *value) {
                std::printf("stage=ENVIRONMENT_REJECT key=%s value=NOT_LOGGED\n",key); return false;
            }
        }
        return setenv(ex::skip_default_xml_variable,"1",1)==0;
    }
    bool ConfigureNative(ex::Config& config, ex::SocketApi& api) {
        provider_=dlopen("/system/lib64/platformsdk/libsoftbus_client.z.so",RTLD_NOW|RTLD_LOCAL);
        if(!provider_) { std::printf("stage=SOFTBUS_LOAD_FAILED error=%s\n",dlerror()); return false; }
        native_socket=reinterpret_cast<decltype(native_socket)>(dlsym(provider_,"Socket"));
        api.create=native_socket?LoggedSocket:nullptr;
        api.listen=reinterpret_cast<decltype(api.listen)>(dlsym(provider_,"Listen"));
        api.bind=reinterpret_cast<decltype(api.bind)>(dlsym(provider_,"Bind"));
        api.send_bytes=reinterpret_cast<decltype(api.send_bytes)>(dlsym(provider_,"SendBytes"));
        api.shutdown=reinterpret_cast<decltype(api.shutdown)>(dlsym(provider_,"Shutdown"));
        const auto local=reinterpret_cast<decltype(&GetLocalNodeDeviceInfo)>(dlsym(provider_,"GetLocalNodeDeviceInfo"));
        const auto all=reinterpret_cast<decltype(&GetAllNodeDeviceInfo)>(dlsym(provider_,"GetAllNodeDeviceInfo"));
        const auto free_nodes=reinterpret_cast<decltype(&FreeNodeInfo)>(dlsym(provider_,"FreeNodeInfo"));
        const auto key=reinterpret_cast<decltype(&GetNodeKeyInfo)>(dlsym(provider_,"GetNodeKeyInfo"));
        if(!api.create || !api.listen || !api.bind || !api.send_bytes || !api.shutdown || !local || !all || !free_nodes || !key) return false;
        NodeBasicInfo me{}; const int local_rc=local(package_name,&me);
        std::printf("stage=GetLocalNodeDeviceInfo_return rc=%d\n",local_rc);
        if(local_rc || strnlen(me.networkId,sizeof(me.networkId))!=64 || args_.peer==me.networkId) return false;
        NodeBasicInfo* nodes=nullptr; int count=-1; const int all_rc=all(package_name,&nodes,&count);
        auto deleter=[free_nodes](NodeBasicInfo* p){ if(p) free_nodes(p); };
        std::unique_ptr<NodeBasicInfo,decltype(deleter)> owned(nodes,deleter);
        bool peer_online=false;
        if(!all_rc && nodes && count>=1 && count<=32) for(int i=0;i<count;++i)
            if(strnlen(nodes[i].networkId,sizeof(nodes[i].networkId))==64 && args_.peer==nodes[i].networkId) peer_online=true;
        std::printf("stage=PUBLIC_DIRECTORY local_rc=%d all_rc=%d count=%d configured_peer_online=%d\n",local_rc,all_rc,count,peer_online);
        if(!peer_online) return false;
        owned.reset();
        config.run_id=args_.run; config.peer_network_id=args_.peer;
        const std::string space="exp-"+args_.run;
const auto derive=[&](const char* network,const char* which, fastdds_dsoftbus::dsoftbus::EndpointId& out,unsigned identity_domain) {
            std::array<std::uint8_t,UDID_BUF_LEN> udid; udid.fill(0xa5);
            const int rc=key(package_name,network,NODE_KEY_UDID,udid.data(),static_cast<int>(udid.size()));
            const auto length=strnlen(reinterpret_cast<const char*>(udid.data()),udid.size());
            const bool valid=rc==0 && length>0 && length<udid.size() &&
                v1::AddressCodec::DeriveKey({space,identity_domain,std::string(reinterpret_cast<const char*>(udid.data()),length)},out);
            udid.fill(0);
            std::printf("stage=PUBLIC_IDENTITY which=%s rc=%d valid=%d identity_bytes=NOT_LOGGED\n",which,rc,valid);
            return valid;
        };
        auto& local_key=local_key_;auto& peer_key=peer_key_;
        if(!derive(me.networkId,"local",local_key,domain) || !derive(args_.peer.c_str(),"peer",peer_key,domain) || local_key==peer_key) return false;
        if(!args_.matrix_case.empty()) {
            matrix_keys_.push_back({domain,local_key,peer_key});
            if(args_.matrix_case=="I2") {
                ex::MatrixDomainKeys other;other.domain=74;
                if(!derive(me.networkId,"local_domain74",other.local,74) || !derive(args_.peer.c_str(),"peer_domain74",other.peer,74) || other.local==other.peer) return false;
                matrix_keys_.push_back(other);
            }
        }
        const auto local_slot=args_.publisher?1u:2u, peer_slot=args_.publisher?2u:1u;
        const bool encoded=v1::AddressCodec::EncodeLocator({local_key,local_slot,v1::Profile::BytesNative},config.local_locator) &&
            v1::AddressCodec::EncodeLocator({peer_key,peer_slot,v1::Profile::BytesNative},config.peer_locator) &&
            ex::EncodeNativeServiceName({space,domain,local_slot,v1::Profile::BytesNative},config.local_service) &&
            ex::EncodeNativeServiceName({space,domain,peer_slot,v1::Profile::BytesNative},config.peer_service);
        if(encoded) std::printf("stage=NATIVE_ADDRESS_MAP local_service=%s peer_service=%s rule_scope=com.kaihong.mdds.* locator_codec=V1_UNCHANGED\n",
            config.local_service.c_str(),config.peer_service.c_str());
        return encoded && (!args_.dual || ConfigureInstance(config,1));
    }

    bool ConfigureInstance(ex::Config& config,unsigned instance) {
        const std::string space="exp-"+args_.run;
        const auto local_slot=ex::DualSlot(args_.publisher,instance),peer_slot=ex::DualSlot(!args_.publisher,instance);
        if(!local_slot || !peer_slot) return false;
        config.instance_name=std::string(args_.publisher?"A":"B")+std::to_string(instance);
        config.enable_rx_drop=false;
        const bool encoded=v1::AddressCodec::EncodeLocator({local_key_,local_slot,v1::Profile::BytesNative},config.local_locator) &&
            v1::AddressCodec::EncodeLocator({peer_key_,peer_slot,v1::Profile::BytesNative},config.peer_locator) &&
            ex::EncodeNativeServiceName({space,domain,local_slot,v1::Profile::BytesNative},config.local_service) &&
            ex::EncodeNativeServiceName({space,domain,peer_slot,v1::Profile::BytesNative},config.peer_service);
        std::printf("stage=DUAL_NATIVE_ADDRESS instance=%u board=%s local_slot=%u peer_slot=%u local_service=%s peer_service=%s encoded=%d namespace_shared=1 domain=%u\n",
            instance,args_.publisher?"A":"B",local_slot,peer_slot,config.local_service.c_str(),config.peer_service.c_str(),encoded,domain);
        return encoded;
    }
    Arguments args_;void* provider_=nullptr; // Retained for process-lifetime callback tombstones.
ex::SocketApi api_;fastdds_dsoftbus::dsoftbus::EndpointId local_key_{},peer_key_{};
    std::vector<ex::MatrixDomainKeys> matrix_keys_;
    std::unique_ptr<ex::matrix::MatrixFixture> matrix_;
    bool matrix_data_=false;
    std::unique_ptr<ex::keyed::KeyedFixture> keyed_;
    bool keyed_data_=false;
    std::unique_ptr<ex::perf::PerformanceFixture> performance_;
    bool performance_data_=false,performance_observation_overflow_=false;
    std::unique_ptr<std::array<PerformanceBoundary,64>> performance_boundaries_;
    std::size_t performance_boundary_count_=0,performance_boundary_printed_=0,performance_error_printed_=0;
    std::unique_ptr<ParticipantRun> first_,second_;
    ex::DualProgress progress_;bool first_closed_=false,dual_data_=false;
    ex::DualExecution dual_execution_;ex::HotExecution hot_execution_;
    std::shared_ptr<ex::ProcessRuntime> runtime_;
    bool hot_data_=false;unsigned hot_cycle_=0;int initial_pid_=0;
};
}
int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    Arguments args;
    if(!Parse(argc,argv,args)) {
        std::fprintf(stderr,"stage=ARGUMENT_REJECT sdk_or_dds_call=NotCalled\nUsage: fastdds_socket_experiment --role publisher|subscriber --run-id HEX16 --peer-network-id HEX64 --expected-token OWNED_ID --preflight-confirmed --allow-unqualified-experiment [--performance-mode TRACE|MEASURE --repeat 1|2|3 --control-dir CURRENT_WINDOW | --keyed-case CASE --control-dir CURRENT_WINDOW | --matrix-case CASE --control-dir CURRENT_WINDOW | --hot-cycles 1|10|100 | --dual-participant | --sample-bytes 262144|1048576 [--controlled-loss reliable|best-effort]]\n");
        return 64;
    }
    std::printf("scope=REAL_FASTDDS_DSOFTBUS_ENGINEERING_EXPERIMENT role=%s run=%s profile=BYTES_NATIVE qualification=NOT_QUALIFIED P0=BLOCKED production_default=NativeUnavailable permission_writes=NONE watchdog_is_cleanup=0\n",args.publisher?"publisher":"subscriber",args.run.c_str());
    std::printf("stage=CONTROLLED_LOSS_CONFIGURATION build_enabled=%d requested=%s receiver_drop_capability=%d native_message_limit=60000 causality_verdict=OFFLINE_TRACE_REQUIRED\n",
        loss_build_enabled,args.loss_mode==ex::LossMode::Disabled?"DISABLED":args.loss_mode==ex::LossMode::Reliable?"RELIABLE":"BEST_EFFORT",
        args.loss_mode!=ex::LossMode::Disabled && !args.publisher);
    if(!args.performance.mode.empty()) std::printf("stage=PERFORMANCE_CONFIGURATION mode=%s repeat=%u control=HOST_PHASE_METADATA business_path=DSOFTBUS_ONLY record_capacity=8192 payload_window_cap=134217728 active_seconds_cap=180 exact_queue_hwm=UNOBSERVED receiver_drop=DISABLED\n",args.performance.mode.c_str(),args.performance.repeat);
    else if(!args.keyed_case.empty()) std::printf("stage=KEYED_CONFIGURATION case=%s control=HOST_PHASE_METADATA business_path=DSOFTBUS_ONLY max_participants_per_board=1 primary_peers=1 key_routing=NONE receiver_drop=DISABLED OLD2_PF02=BLOCKED\n",args.keyed_case.c_str());
    else if(args.matrix_case.empty()) {
        std::printf("stage=DUAL_CONFIGURATION enabled=%d instances_per_process=%d shared_topic=1 receiver_drop=DISABLED_FOR_DUAL\n",args.dual,args.dual?2:1);
        std::printf("stage=HOT_CONFIGURATION cycles=%u default_disabled=1 OLD2_PF02=BLOCKED full_native_rebuild=HISTORICAL_RED_NOT_TESTED\n",args.hot_cycles);
    } else std::printf("stage=MATRIX_CONFIGURATION case=%s control=HOST_PHASE_METADATA business_path=DSOFTBUS_ONLY max_participants_per_board=2 receiver_drop=DISABLED OLD2_PF02=BLOCKED\n",args.matrix_case.c_str());
Experiment experiment(args); fastdds_dsoftbus_probe::TokenFirstEntry entry(experiment);
const bool returned=entry.Run({fastdds_dsoftbus_probe::TokenEntryMode::Record,true,true,args.token});
    experiment.evidence.token_restored=entry.RestoreSucceeded();
    const bool success=returned && experiment.Complete();
    const bool not_exercised=!success && experiment.DataVerdict()==ex::LossVerdict::NotExercised &&
        experiment.evidence.cleanup_observed && experiment.evidence.token_restored;
    std::printf("result=%s run=%s data_path=%s close_observed=%d token_restored=%d failure_stage=%s native_internal_settlement=UNOBSERVED P0=BLOCKED production_qualification=NOT_QUALIFIED\n",
        success?"SCENARIO_PASS":not_exercised?"SCENARIO_NOT_EXERCISED":"SCENARIO_FAIL",args.run.c_str(),experiment.DataResultName(),
        experiment.evidence.cleanup_observed,experiment.evidence.token_restored,experiment.failure_stage);
    return success?0:not_exercised?7:6;
}
