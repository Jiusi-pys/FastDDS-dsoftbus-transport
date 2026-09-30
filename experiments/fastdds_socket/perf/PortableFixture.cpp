#include "PortableFixture.hpp"
#include "Resources.hpp"
#include "../DualParticipantPolicy.hpp"
#include "../DualValidation.hpp"
#include "ExperimentLargeSamplePubSubTypes.hpp"
#include <fastdds/LibrarySettings.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/WriteParams.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <fastdds/utils/IPLocator.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#ifdef FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS
#include <filesystem>
#endif

namespace fastdds_dsoftbus::experiment::perf {
namespace dds = eprosima::fastdds::dds;
namespace rtps = eprosima::fastdds::rtps;
namespace {
constexpr int64_t Second = 1000000000;
std::string Hex(const std::array<uint8_t,16>& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text; text.reserve(32);
    for (auto byte : value) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}
std::string Guid(const rtps::GUID_t& value) { return Hex(DualBytes(value)); }
int64_t Sequence(const rtps::SequenceNumber_t& sn) {
    return sn.high < 0 ? -1 : (static_cast<int64_t>(sn.high) << 32) | sn.low;
}
std::string JsonTime(int64_t n) { return n < 0 ? "null" : std::to_string(n); }
std::string JsonRc(int32_t n) { return n == kUnobservedRc ? "null" : std::to_string(n); }
std::string JsonGuid(const std::array<uint8_t,16>& g) {
    return std::all_of(g.begin(),g.end(),[](uint8_t v){ return v == 0; }) ? "null" : "\"" + Hex(g) + "\"";
}
std::string JsonObserved(const ObservedU64& n) { return n.observed ? std::to_string(n.value) : "null"; }
bool SafeRun(const std::string& text) {
    return text.size() == 16 && std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
uint64_t RunNumber(const std::string& text) {
    uint64_t value = 0; for (char c : text) value = value * 16 + (c<='9' ? c-'0' : c-'a'+10); return value;
}
template<class Q> void Configure(Q& q, unsigned entity) {
    q.reliability().kind=dds::RELIABLE_RELIABILITY_QOS; q.reliability().max_blocking_time={1,0};
    q.durability().kind=dds::VOLATILE_DURABILITY_QOS; q.history().kind=dds::KEEP_ALL_HISTORY_QOS;
    q.resource_limits().max_samples=16; q.resource_limits().max_instances=1;
    q.resource_limits().max_samples_per_instance=16; q.resource_limits().allocated_samples=16;
    q.data_sharing().off(); q.ownership().kind=dds::SHARED_OWNERSHIP_QOS;
    q.deadline().period=dds::c_TimeInfinite; q.lifespan().duration=dds::c_TimeInfinite;
    q.liveliness().kind=dds::AUTOMATIC_LIVELINESS_QOS; q.liveliness().lease_duration=dds::c_TimeInfinite;
    q.representation().m_value={dds::XCDR_DATA_REPRESENTATION};
    q.endpoint().history_memory_policy=rtps::PREALLOCATED_WITH_REALLOC_MEMORY_MODE;
    q.endpoint().entity_id=entity;
}
template<class Q> bool ValidQos(const Q& q, unsigned entity) {
    return q.reliability().kind==dds::RELIABLE_RELIABILITY_QOS && q.reliability().max_blocking_time==dds::Duration_t(1,0) &&
        q.durability().kind==dds::VOLATILE_DURABILITY_QOS && q.history().kind==dds::KEEP_ALL_HISTORY_QOS &&
        q.resource_limits().max_samples==16 && q.resource_limits().max_instances==1 &&
        q.resource_limits().max_samples_per_instance==16 && q.resource_limits().allocated_samples==16 &&
        q.data_sharing().kind()==dds::OFF && q.ownership().kind==dds::SHARED_OWNERSHIP_QOS &&
        q.deadline().period.is_infinite() && q.lifespan().duration.is_infinite() &&
        q.liveliness().kind==dds::AUTOMATIC_LIVELINESS_QOS && q.liveliness().lease_duration.is_infinite() &&
        q.representation().m_value==std::vector<dds::DataRepresentationId_t>{dds::XCDR_DATA_REPRESENTATION} &&
        q.endpoint().history_memory_policy==rtps::PREALLOCATED_WITH_REALLOC_MEMORY_MODE && q.endpoint().entity_id==entity;
}
}

struct PerformanceFixture::Impl {
    struct Stage {
        CaseId id=CaseId::RTT_1K; Phase phase=Phase::Formal; unsigned number=0; uint32_t count=0;
        bool armed=false, admitting=false, done=false, flushed=false, incomplete=false;
        int64_t armed_ns=-1, first_deadline_ns=-1, phase_deadline_ns=-1, start_ns=-1, end_ns=-1;
        int64_t observation_deadline_ns=-1, observation_end_ns=-1;
        int32_t ack_rc=kUnobservedRc;
        ResourceSnapshot before, after; CpuBoundary cpu_before, cpu_after;
    };
    Options options;
    uint64_t run=0;
    dds::DomainParticipant* participant=nullptr;
    dds::Publisher* publisher=nullptr; dds::Subscriber* subscriber=nullptr;
    dds::DataWriter* writer=nullptr; dds::DataReader* reader=nullptr;
    dds::TypeSupport type{new ExperimentLargeSamplePubSubType()};
    std::shared_ptr<Control> control;
    std::shared_ptr<ProcessRuntime> runtime;
    ExperimentLargeSample inbound, outbound;
    Ledger ledger;
    std::array<Stage,12> stages{}; unsigned stage_count=0, current_stage=0;
    bool reference=false, opened=false, closed=false, global_stop=false, contract_direct=false;
    bool counter_overflow=false;
    uint64_t submitted_vector_bytes=0, taken_samples=0, state_only_takes=0;
    int64_t active_begin_ns=-1;
    Identity pending_rtt{}; bool has_pending_rtt=false;
    std::string first_failure, cleanup_failure;
    std::array<ResourceSnapshot,3> lifecycle_resources{};
    std::array<ProcessStats,3> process_boundaries{};
    uint64_t sampled_tx_records=0,sampled_rx_records=0,sampled_tx_bytes=0,sampled_rx_bytes=0;
#ifdef FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS
    // Contract tests inject boundary timestamps after a real public DDS take.
    // These hooks are absent from native builds and inaccessible to either CLI.
    std::function<int64_t(unsigned)> test_clock;
    std::function<void(const char*)> test_event;
#endif
    int64_t BoundaryClock(unsigned boundary) const {
#ifdef FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS
        if(test_clock) return test_clock(boundary);
#endif
        (void)boundary; return ClockNowNs();
    }
    void TestEvent(const char* event) const {
#ifdef FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS
        if(test_event) test_event(event);
#endif
        (void)event;
    }

    explicit Impl(Options value):options(std::move(value)) {
        if (!SafeRun(options.run) || options.control_directory.empty() || options.repeat<1 || options.repeat>3 ||
                (options.mode==Mode::Trace && options.repeat!=1)) {
            first_failure="invalid_options"; return;
        }
        run=RunNumber(options.run);
        if (!ledger.Initialize(run,options.mode)) { first_failure="ledger_init"; return; }
        inbound.data().reserve(kInboundCapacity); outbound.data().reserve(kOutboundCapacity);
        if (!Storage()) { first_failure="measurement_storage_capacity"; return; }
        for (auto id : CaseOrder(options.repeat)) {
            for (auto phase : {Phase::Warmup,Phase::Formal}) {
                const auto count=Count(id,phase); if (!count) continue;
                auto& s=stages[stage_count]; s.id=id; s.phase=phase; s.number=stage_count; s.count=count; ++stage_count;
            }
        }
    }
    const char* Scope() const { return options.host_contract ? "HOST_CONTRACT" : reference ? "HOST_REFERENCE_UDP" : "BYTES_NATIVE"; }
    const char* Side() const { return options.board_a ? "A" : "B"; }
    uint32_t Count(CaseId id,Phase phase) const {
        const auto& spec=GetCase(id);
        if (options.host_contract) return phase==Phase::Warmup ? (options.mode==Mode::Measure ? 2 : 0) : spec.trace_count;
        return PlannedCount(spec,phase,options.mode);
    }
    int64_t Duration(const Stage& s) const {
        if(options.host_contract) return 200000000;
        return options.mode==Mode::Trace ? 2*Second : s.phase==Phase::Formal ? 10*Second :
            std::max<int64_t>(Second,static_cast<int64_t>(s.count)*Second/GetCase(s.id).hz);
    }
    int64_t LateDuration() const { return options.host_contract ? 100000000 : 2*Second; }
    std::size_t Storage() const { return MeasurementStorageBytes(inbound.data().capacity(),outbound.data().capacity()); }
    std::string Common(bool include_clock_counter=false) const {
        return "\"run\":\""+options.run+"\",\"side\":\""+Side()+"\",\"mode\":\""+ModeName(options.mode)+
            "\",\"repeat\":"+std::to_string(options.repeat)+",\"scope\":\""+Scope()+"\""+
            (include_clock_counter ? ",\"invalid_clock\":"+std::to_string(ledger.counters().invalid_clock) : "");
    }
    void Event(const char* event, const Stage* s=nullptr) const {
        std::printf("PERF_CONTROL event=%s run=%s side=%s stage=%u case=%s phase=%s\n",event,options.run.c_str(),Side(),
            s?s->number:0,s?GetCase(s->id).name:"ALL",s?PhaseName(s->phase):"NONE"); std::fflush(stdout);
        TestEvent(event);
    }
    bool Fail(const std::string& reason) {
        global_stop=true;
        if(first_failure.empty()) first_failure=reason;
        return false;
    }
    bool Good() const { return first_failure.empty(); }
    std::array<uint8_t,16> Expected(bool side,unsigned entity,bool writing) const {
        auto value=DualIdentity(options.run,side,1,EndpointKind::Participant);
        value[12]=static_cast<uint8_t>(entity>>16); value[13]=static_cast<uint8_t>(entity>>8);
        value[14]=static_cast<uint8_t>(entity); value[15]=writing?3:4; return value;
    }
    std::string Topic(bool request) const { return "mdds-perf-"+options.run+(request?"-Request":"-Response"); }
    void BaseQos(dds::DomainParticipantQos& q) {
        q.name("perf-"+options.run+"-"+Side()); ApplyDualParticipantPolicy(q); ApplyDualIdentityFixture(q,options.run,options.board_a,1);
        q.transport().use_builtin_transports=false; q.transport().user_transports.clear();
        auto& w=q.wire_protocol(); w.builtin.discovery_config.discoveryProtocol=rtps::DiscoveryProtocol::SIMPLE;
        w.builtin.discovery_config.leaseDuration_announcementperiod={1,0};
        w.builtin.discovery_config.initial_announcements.count=10; w.builtin.discovery_config.initial_announcements.period={0,300000000};
        w.builtin.metatrafficMulticastLocatorList.clear(); w.builtin.metatrafficUnicastLocatorList.clear();
        w.default_multicast_locator_list.clear(); w.default_unicast_locator_list.clear(); w.default_external_unicast_locators.clear();
        w.builtin.initialPeersList.clear(); w.builtin.mutation_tries=0;
    }
    bool Initialize() {
        if(!Good() || opened || participant) return Fail("open_precondition");
        if(!std::chrono::steady_clock::is_steady) return Fail("clock_not_steady");
        auto* f=dds::DomainParticipantFactory::get_instance();
        eprosima::fastdds::LibrarySettings settings; settings.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
        if(!options.test_factory_preconfigured && f->set_library_settings(settings)!=dds::RETCODE_OK) return Fail("intraprocess_set");
        if(f->get_library_settings(settings)!=dds::RETCODE_OK || settings.intraprocess_delivery!=eprosima::fastdds::INTRAPROCESS_OFF) return Fail("intraprocess_readback");
        lifecycle_resources[0]=CaptureResources(); process_boundaries[0]=GetProcessStats();
        if(options.observation_snapshot) options.observation_snapshot("BEFORE_INIT",0);
        return true;
    }
    bool Create(const dds::DomainParticipantQos& q) {
        participant=dds::DomainParticipantFactory::get_instance()->create_participant(0,q);
        if(!participant) return Fail("create_participant");
        const auto& actual=participant->get_qos();
        const bool participant_valid=DualBytes(participant->guid())==DualIdentity(options.run,options.board_a,1,EndpointKind::Participant) &&
            !actual.transport().use_builtin_transports && actual.transport().user_transports.size()==1 &&
            actual.transport().user_transports==q.transport().user_transports && DualParticipantPolicyRequested(actual) &&
            actual.wire_protocol().builtin.metatrafficUnicastLocatorList==q.wire_protocol().builtin.metatrafficUnicastLocatorList &&
            actual.wire_protocol().default_unicast_locator_list==q.wire_protocol().default_unicast_locator_list &&
            actual.wire_protocol().builtin.initialPeersList==q.wire_protocol().builtin.initialPeersList &&
            actual.wire_protocol().builtin.metatrafficMulticastLocatorList.empty() && actual.wire_protocol().default_multicast_locator_list.empty();
        if(!participant_valid) return Fail("participant_readback");
        if(type.register_type(participant)!=dds::RETCODE_OK) return Fail("type_registration");
        publisher=participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
        subscriber=participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        auto* request=participant->create_topic(Topic(true),type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        auto* response=participant->create_topic(Topic(false),type.get_type_name(),dds::TOPIC_QOS_DEFAULT);
        if(!publisher||!subscriber||!request||!response) return Fail("entities_create");
        dds::DataWriterQos wq; Configure(wq,options.board_a?1:3); wq.publish_mode().kind=dds::SYNCHRONOUS_PUBLISH_MODE;
        dds::DataReaderQos rq; Configure(rq,options.board_a?4:2);
        writer=publisher->create_datawriter(options.board_a?request:response,wq);
        reader=subscriber->create_datareader(options.board_a?response:request,rq);
        if(!writer||!reader) return Fail("endpoint_create");
        const bool wvalid=ValidQos(writer->get_qos(),options.board_a?1:3) && writer->get_qos().publish_mode().kind==dds::SYNCHRONOUS_PUBLISH_MODE &&
            DualBytes(writer->guid())==Expected(options.board_a,options.board_a?1:3,true);
        const bool rvalid=ValidQos(reader->get_qos(),options.board_a?4:2) && DualBytes(reader->guid())==Expected(options.board_a,options.board_a?4:2,false);
        std::string native;
        if(control) {
            const auto s=control->Snapshot();
            const auto& local=*actual.wire_protocol().default_unicast_locator_list.begin();
            const auto& peer=*actual.wire_protocol().builtin.initialPeersList.begin();
            std::array<uint8_t,16> local_key{},peer_key{}; std::copy(local.address,local.address+16,local_key.begin()); std::copy(peer.address,peer.address+16,peer_key.begin());
            native=",\"runtime\":"+std::to_string(s.runtime_id)+",\"native_instance\":"+std::to_string(s.instance_id)+
                ",\"application\":"+std::to_string(s.application_incarnation)+",\"receiver\":"+std::to_string(s.receiver_incarnation)+
                ",\"token\":"+std::to_string(s.logical_token)+",\"local_kind\":"+std::to_string(local.kind)+",\"local_key\":\""+Hex(local_key)+
                "\",\"local_port\":"+std::to_string(local.port)+",\"peer_kind\":"+std::to_string(peer.kind)+",\"peer_key\":\""+Hex(peer_key)+"\",\"peer_port\":"+std::to_string(peer.port);
        }
        std::printf("PERF_IDENTITY {%s,\"participant_guid\":\"%s\",\"request_guid\":\"%s\",\"response_guid\":\"%s\",\"writer_guid\":\"%s\",\"reader_guid\":\"%s\",\"participant_readback\":true,\"builtin_transports\":false,\"intraprocess\":\"OFF\",\"transport_count\":1%s}\n",
            Common().c_str(),Guid(participant->guid()).c_str(),Hex(Expected(true,1,true)).c_str(),Hex(Expected(false,3,true)).c_str(),Guid(writer->guid()).c_str(),Guid(reader->guid()).c_str(),native.c_str());
        for(bool writing:{true,false}) std::printf("PERF_QOS {%s,\"endpoint\":\"%s\",\"guid\":\"%s\",\"readback\":%s,\"reliability\":\"RELIABLE\",\"durability\":\"VOLATILE\",\"history\":\"KEEP_ALL\",\"max_samples\":16,\"max_instances\":1,\"max_samples_per_instance\":16,\"allocated_samples\":16,\"max_blocking_time_s\":1,\"publish_mode\":\"%s\",\"data_sharing\":\"OFF\",\"representation\":\"XCDR1\",\"memory_policy\":\"PREALLOCATED_WITH_REALLOC\",\"ownership\":\"SHARED\",\"builtin_transports\":false,\"intraprocess\":\"OFF\"}\n",
            Common().c_str(),writing?(options.board_a?"request_writer":"response_writer"):(options.board_a?"response_reader":"request_reader"),writing?Guid(writer->guid()).c_str():Guid(reader->guid()).c_str(),(writing?wvalid:rvalid)?"true":"false",writing?"SYNCHRONOUS":"NA");
        if(!wvalid||!rvalid) return Fail("qos_readback");
        opened=true; lifecycle_resources[1]=CaptureResources(); process_boundaries[1]=GetProcessStats();
        if(options.observation_snapshot) options.observation_snapshot("AFTER_INIT",0);
        std::fflush(stdout); return true;
    }
    template<class Q> bool Remote(const Q& q, bool request) const {
        return std::string(q.topic_name.c_str())==Topic(request) && std::string(q.type_name.c_str())==type.get_type_name() &&
            q.reliability.kind==dds::RELIABLE_RELIABILITY_QOS && q.durability.kind==dds::VOLATILE_DURABILITY_QOS &&
            q.data_sharing.kind()==dds::OFF && q.partition.names().empty();
    }
    bool Matches(bool emit=false) {
        std::vector<dds::InstanceHandle_t> wr,rd;
        if(writer->get_matched_subscriptions(wr)!=dds::RETCODE_OK || reader->get_matched_publications(rd)!=dds::RETCODE_OK) return Fail("matched_query");
        if(wr.size()!=1||rd.size()!=1) return false;
        dds::SubscriptionBuiltinTopicData wdata; dds::PublicationBuiltinTopicData rdata;
        if(writer->get_matched_subscription_data(wdata,wr[0])!=dds::RETCODE_OK || reader->get_matched_publication_data(rdata,rd[0])!=dds::RETCODE_OK) return false;
        const bool exact=DualBytes(wdata.guid)==Expected(!options.board_a,options.board_a?2:4,false) &&
            DualBytes(rdata.guid)==Expected(!options.board_a,options.board_a?3:1,true) && Remote(wdata,options.board_a) && Remote(rdata,!options.board_a);
        if(!exact) return Fail("matched_exact_identity_qos");
        if(emit) std::printf("PERF_MATCH {%s,\"exact\":true,\"writer_peer\":\"%s\",\"reader_peer\":\"%s\",\"source\":\"get_matched\"}\n",Common().c_str(),Guid(wdata.guid).c_str(),Guid(rdata.guid).c_str());
        return true;
    }
    Stage* FindStage(const Identity& id) {
        for(unsigned i=0;i<stage_count;++i) if(stages[i].id==id.case_id && stages[i].phase==id.phase) return &stages[i]; return nullptr;
    }
    void SnapshotMax() {
        const auto p=GetProcessStats();
        sampled_tx_records=std::max(sampled_tx_records,p.tx_records); sampled_rx_records=std::max(sampled_rx_records,p.rx_records);
        sampled_tx_bytes=std::max(sampled_tx_bytes,p.tx_bytes); sampled_rx_bytes=std::max(sampled_rx_bytes,p.rx_bytes);
    }
    bool Guard() {
        if(!Storage()) return Fail("measurement_storage_capacity");
        if(ledger.overflowed()||counter_overflow) return Fail("measurement_counter_or_record_overflow");
        if(active_begin_ns>=0 && ClockNowNs()-active_begin_ns>=180*Second) return Fail("active_180s_limit");
        std::ifstream abort(options.control_directory+"/ABORT.release");
        if(abort) return Fail("controller_abort");
        return Good();
    }
    bool Budget(uint32_t bytes) {
        if(!CheckedAdd(submitted_vector_bytes,bytes)) { counter_overflow=true; return Fail("submission_counter_overflow"); }
        return submitted_vector_bytes<=134217728 || Fail("submission_cap");
    }
    static bool Reached(int64_t now,int64_t deadline) { return deadline>=0 && now>=deadline; }
    static int64_t Earlier(int64_t a,int64_t b) { return a<0?b:b<0?a:std::min(a,b); }
    int64_t GlobalDeadline() const { return active_begin_ns<0?-1:active_begin_ns+180*Second; }
    int64_t RttDeadline(const Stage& s,const SampleRecord& record) const {
        auto deadline=Earlier(s.phase_deadline_ns,GlobalDeadline());
        if(options.board_a && record.write_before_ns>=0) deadline=Earlier(deadline,record.write_before_ns+2*Second);
        return deadline;
    }
    void Write(Identity id,int64_t latest_start=-1) {
        auto* record=ledger.Find(id); if(!record || global_stop) return;
        if(!FillPayload(id,outbound.data()) || !Storage()) { Fail("outbound_contract"); return; }
        outbound.index(id.sequence);
        if(!Budget(static_cast<uint32_t>(outbound.data().size()))) return;
        record->flags=(record->flags & ~NotAttempted)|Attempted;
        rtps::WriteParams params;
        const auto before=BoundaryClock(0);
        if(Reached(before,Earlier(latest_start,GlobalDeadline()))) {
            // Preparation/scheduling may cross the chosen absolute slot. No
            // submission occurred, so undo only this exact local reservation.
            submitted_vector_bytes-=static_cast<uint32_t>(outbound.data().size());
            record->flags=(record->flags&~Attempted)|NotAttempted;
            if(GetCase(id.case_id).kind==Kind::Load) record->flags|=MissedSlot;
            if(Reached(before,GlobalDeadline())) Fail("active_180s_limit");
            return;
        }
        const auto rc=writer->write(&outbound,params);
        record->write_after_ns=ClockNowNs();
        record->write_before_ns=before;
        record->write_rc=static_cast<int32_t>(rc);
        record->writer_guid=DualBytes(params.sample_identity().writer_guid()); record->writer_sn=Sequence(params.sample_identity().sequence_number());
        if(rc!=dds::RETCODE_OK) ledger.RecordError(1,id,record->write_after_ns);
    }
    void Pump() {
        if(!reader) return;
        for(unsigned batch=0;batch<16;++batch) {
            dds::SampleInfo info;
            const auto rc=reader->take_next_sample(&inbound,&info);
            const auto take_ns=BoundaryClock(1); // Provisional t1 before every validation step.
            if(rc==dds::RETCODE_NO_DATA) break;
            if(rc!=dds::RETCODE_OK) { Fail("dds_take"); break; }
            // Normal DDS dispose/no-writers state samples have no business data.
            // They are counted separately and never interpreted as a response.
            if(!info.valid_data) { if(!CheckedAdd(state_only_takes,1)) counter_overflow=true; continue; }
            if(!CheckedAdd(taken_samples,1)) counter_overflow=true;
            Identity id;
            if(!Storage() || inbound.data().size()>kInboundCapacity || !ValidatePayload(inbound.index(),inbound.data(),id)) {
                ledger.CountInvalid(); Fail("invalid_payload_or_capacity"); continue;
            }
            auto* stage=FindStage(id);
            auto* record=ledger.Find(id);
            const auto source=DualBytes(info.sample_identity.writer_guid()); const auto sn=Sequence(info.sample_identity.sequence_number());
            const bool exact=source==Expected(!options.board_a,options.board_a?3:1,true) && sn>0;
            const bool current=stage && stage->armed && stage->number==current_stage && !stage->done && id.sequence<=stage->count;
            const bool rtt_expired=stage && record && GetCase(id.case_id).kind==Kind::Rtt && Reached(take_ns,RttDeadline(*stage,*record));
            const bool global_expired=Reached(take_ns,GlobalDeadline());
            const bool active=current && !rtt_expired && !global_expired;
            // Older immutable stages remain classifiable while metadata waits pump.
            // A previously unseen older request is late, never a new-stage sample.
            const bool known_older=stage && stage->armed && stage->number<=current_stage;
            const auto arrival=ledger.Observe(id,take_ns,exact,active || (!options.board_a && known_older));
            if(!exact || id.run!=run || !stage || !record || id.sequence>stage->count) { Fail("wrong_source_or_identity"); continue; }
            if(arrival==Arrival::Duplicate) continue;
            if(arrival==Arrival::Inactive) { ledger.RecordError(2,id,take_ns); Fail("future_or_inactive_identity"); continue; }
            if(arrival!=Arrival::First && arrival!=Arrival::Late) { Fail("arrival_admission"); continue; }
            record->source_guid=source; record->source_sn=sn;
            if(record->flags&InvalidClock) { Fail("invalid_rtt_clock"); continue; }
            if(rtt_expired) {
                stage->incomplete=true; stage->admitting=false;
                if(options.board_a) { ledger.MarkTimeout(id); pending_rtt=id; has_pending_rtt=true; }
            }
            const bool no_first_expired=!options.board_a && GetCase(id.case_id).kind==Kind::Load &&
                stage->start_ns<0 && Reached(take_ns,stage->first_deadline_ns);
            const bool after_observation=Reached(take_ns,stage->observation_deadline_ns);
            if(!active || no_first_expired || after_observation || Reached(take_ns,stage->end_ns)) ledger.MarkLate(id,after_observation);
            if(global_expired) { Fail("active_180s_limit"); continue; }
            if(!options.board_a && stage->start_ns<0 && active && stage->admitting && !no_first_expired) {
                stage->start_ns=take_ns;
                if(GetCase(id.case_id).kind==Kind::Load) {
                    stage->end_ns=take_ns+Duration(*stage);
                    stage->observation_deadline_ns=Earlier(stage->end_ns+LateDuration(),GlobalDeadline());
                }
            }
            if(!options.board_a && GetCase(id.case_id).kind==Kind::Rtt && ledger.ReserveEcho(id,active&&stage->admitting&&!global_stop)) {
                if(!Budget(record->vector_bytes)) continue;
                rtps::WriteParams params;
                const auto echo_before=BoundaryClock(2);
                if(Reached(echo_before,RttDeadline(*stage,*record))) {
                    // Validation or scheduling can consume the remaining phase.
                    // The clock nearest the real write decides admission.
                    submitted_vector_bytes-=record->vector_bytes;
                    record->flags&=~EchoReserved;
                    stage->incomplete=true; stage->admitting=false;
                    ledger.MarkLate(id,Reached(echo_before,stage->observation_deadline_ns));
                    if(Reached(echo_before,GlobalDeadline())) Fail("active_180s_limit");
                    continue;
                }
                record->echo_before_ns=echo_before;
                const auto echo_rc=writer->write(&inbound,params);
                record->echo_after_ns=ClockNowNs(); record->echo_rc=static_cast<int32_t>(echo_rc);
                record->echo_guid=DualBytes(params.sample_identity().writer_guid()); record->echo_sn=Sequence(params.sample_identity().sequence_number());
                if(echo_rc!=dds::RETCODE_OK) ledger.RecordError(3,id,record->echo_after_ns);
            }
        }
    }
    void Tick() { Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    bool ReadControl(const Stage& s,bool flush) {
        const auto suffix=flush?".flush":".release";
        const auto path=options.control_directory+"/stage-"+std::to_string(s.number)+suffix;
        const std::string expected=flush ? "PERF_FLUSH "+options.run+" "+std::to_string(s.number)+"\n" :
            "PERF_RELEASE "+options.run+" "+std::to_string(s.number)+" "+GetCase(s.id).name+" "+PhaseName(s.phase)+"\n";
        // An A timeout at 2 s must still allow B's declared 30 s RTT phase plus
        // 2 s late observation to finish; this is metadata wait, not RTT timeout.
        const int64_t deadline=ClockNowNs()+(flush?35:20)*Second;
        while(ClockNowNs()<deadline && Guard()) {
            Pump();
            for(unsigned i=0;i<stage_count;++i) {
                std::ifstream other(options.control_directory+"/stage-"+std::to_string(i)+".release");
                if(other && (i!=s.number || flush)) return Fail(i<=s.number?"duplicate_release":"future_release");
                std::ifstream other_flush(options.control_directory+"/stage-"+std::to_string(i)+".flush");
                if(other_flush && (i!=s.number || !flush)) return Fail(i<s.number?"duplicate_flush":"premature_flush");
            }
            std::ifstream file(path,std::ios::binary);
            if(file) {
                const std::string body((std::istreambuf_iterator<char>(file)),{}); file.close();
                if(body!=expected) return Fail("control_content");
                if(std::rename(path.c_str(),(path+".consumed").c_str())!=0) return Fail("control_consume");
                if(flush) TestEvent("FLUSH_ACCEPTED");
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return Fail("control_timeout");
    }
    uint32_t Unique(const Stage& s) const {
        uint32_t count=0; for(uint32_t seq=1;seq<=s.count;++seq) { auto* r=ledger.Find({run,s.id,s.phase,seq}); if(r && (r->flags&Validated)) ++count; } return count;
    }
    void RunA(Stage& s) {
        s.start_ns=ClockNowNs(); const auto& spec=GetCase(s.id);
        if(spec.kind==Kind::Rtt) s.phase_deadline_ns=s.start_ns+30*Second;
        if(has_pending_rtt) {
            const auto* previous=ledger.Find(pending_rtt);
            if(previous && (previous->flags&Validated)) has_pending_rtt=false;
        }
        if(has_pending_rtt) {
            // DDS writes cannot cancel an older timed-out request. Preserve all
            // future identities as not attempted until that response is taken.
            s.incomplete=true; s.end_ns=ClockNowNs(); s.cpu_after=CaptureCpuBoundary();
            s.observation_deadline_ns=s.observation_end_ns=s.end_ns; s.done=true; s.admitting=false; return;
        }
        if(spec.kind==Kind::Rtt) {
            const auto limit=Earlier(s.phase_deadline_ns,GlobalDeadline()); int64_t next=s.start_ns;
            for(uint32_t seq=1;seq<=s.count && Guard();++seq) {
                while(ClockNowNs()<next && Guard()) Tick();
                if(ClockNowNs()>=limit) { s.incomplete=true; break; }
                const Identity id{run,s.id,s.phase,seq}; Write(id,limit); auto* r=ledger.Find(id);
                if(!r || !(r->flags&Attempted)) { s.incomplete=true; break; }
                const auto deadline=std::min(limit,r->write_before_ns+2*Second);
                while(!(r->flags&Validated) && ClockNowNs()<deadline && Guard()) Tick();
                if(!(r->flags&Validated)) { ledger.MarkTimeout(id); pending_rtt=id; has_pending_rtt=true; s.incomplete=true; break; }
                if(r->flags&(TimedOut|Late|AfterObservation)) { s.incomplete=true; break; }
                if(r->write_rc!=0) { s.incomplete=true; break; }
                next=r->write_before_ns+Second/spec.hz;
            }
            s.end_ns=ClockNowNs();
        } else {
            const int64_t duration=Duration(s); s.end_ns=s.start_ns+duration;
            const uint32_t hz=options.host_contract ? static_cast<uint32_t>((s.count*Second+duration-1)/duration) : spec.hz;
            AbsolutePacer pacer(s.start_ns,hz,s.count);
            while(ClockNowNs()<s.end_ns && Guard()) {
                Pump(); const auto decision=pacer.Poll(ClockNowNs());
                for(uint32_t i=0;i<decision.missed_count;++i) if(auto* r=ledger.Find({run,s.id,s.phase,decision.missed_first+i})) r->flags|=MissedSlot;
                if(decision.finished) break;
                if(decision.ready) Write({run,s.id,s.phase,decision.sequence},std::min(s.end_ns,s.start_ns+static_cast<int64_t>(decision.sequence)*(Second/hz)));
                else std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // Absolute horizon, even a short TRACE offering never changes B's fixed window.
            while(ClockNowNs()<s.end_ns && Guard()) Tick();
            const auto tail=pacer.Poll(s.end_ns);
            for(uint32_t i=0;i<tail.missed_count;++i) if(auto* r=ledger.Find({run,s.id,s.phase,tail.missed_first+i})) r->flags|=MissedSlot;
            s.end_ns=ClockNowNs(); // Actual local offering interval, including final scheduling delay.
        }
        s.admitting=false; s.cpu_after=CaptureCpuBoundary();
        s.ack_rc=static_cast<int32_t>(writer->wait_for_acknowledgments({2,0}));
        if(s.ack_rc!=0) { ledger.RecordError(4,{run,s.id,s.phase,0},ClockNowNs()); s.incomplete=true; }
        s.observation_deadline_ns=s.observation_end_ns=ClockNowNs(); s.done=true;
    }
    void RunB(Stage& s) {
        const auto first_limit=s.first_deadline_ns;
        const auto phase_limit=Earlier(s.phase_deadline_ns,GlobalDeadline());
        while(Guard()) {
            Pump(); const auto now=ClockNowNs();
            if(GetCase(s.id).kind==Kind::Rtt) {
                if(Unique(s)==s.count) { s.end_ns=Earlier(now,phase_limit); break; }
                if(Reached(now,phase_limit)) { s.incomplete=true; s.end_ns=phase_limit; break; }
            } else if(s.start_ns>=0 && now>=s.end_ns) break;
            else if(s.start_ns<0 && now>=first_limit) { s.incomplete=true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        s.admitting=false; s.cpu_after=CaptureCpuBoundary();
        // Freeze the nominal observation boundary before any late-loop Pump.
        // The actual return timestamp is retained separately for scheduling delay.
        const auto observation_limit=Earlier((s.end_ns>=0?s.end_ns:first_limit)+LateDuration(),GlobalDeadline());
        s.observation_deadline_ns=observation_limit;
        while(ClockNowNs()<observation_limit && Guard()) Tick();
        s.observation_end_ns=ClockNowNs();
        if(GetCase(s.id).kind==Kind::Rtt) {
            s.ack_rc=static_cast<int32_t>(writer->wait_for_acknowledgments({2,0}));
            if(s.ack_rc!=0) { s.incomplete=true; ledger.RecordError(5,{run,s.id,s.phase,0},ClockNowNs()); }
            for(uint32_t seq=1;seq<=s.count;++seq) if(ledger.Find({run,s.id,s.phase,seq})->echo_rc!=0) s.incomplete=true;
        }
        s.done=true;
    }
    void PrintResource(const char* label,const ResourceSnapshot& r,const Stage* s=nullptr) const {
        std::printf("PERF_RESOURCE {%s,\"label\":\"%s\",\"stage\":%s,\"wall_ns\":%s,\"cpu_ns\":%s,\"rss_bytes\":%s,\"hwm_bytes\":%s,\"threads\":%s,\"fd_count\":%s}\n",
            Common().c_str(),label,s?std::to_string(s->number).c_str():"null",JsonTime(r.wall_ns).c_str(),JsonObserved(r.cpu_ns).c_str(),JsonObserved(r.rss_bytes).c_str(),JsonObserved(r.hwm_bytes).c_str(),JsonObserved(r.threads).c_str(),JsonObserved(r.fd_count).c_str());
    }
    void CaptureStageAfter(Stage& s) {
        TestEvent("HEAVY_AFTER");
        s.after=CaptureResources(); SnapshotMax();
        if(options.observation_snapshot) options.observation_snapshot("STAGE_AFTER",s.number);
    }
    void Flush(Stage& s) {
        uint32_t attempted=0,write_ok=0,valid=0,timeouts=0,late=0,after=0,missed=0,not_attempted=0,invalid_clock=0;
        for(uint32_t seq=1;seq<=s.count;++seq) {
            const auto& r=*ledger.Find({run,s.id,s.phase,seq});
            attempted+=(r.flags&Attempted)!=0; write_ok+=r.write_rc==0; valid+=(r.flags&Validated)!=0; timeouts+=(r.flags&TimedOut)!=0;
            late+=(r.flags&Late)!=0; after+=(r.flags&AfterObservation)!=0; missed+=(r.flags&MissedSlot)!=0; not_attempted+=(r.flags&NotAttempted)!=0;
            invalid_clock+=(r.flags&InvalidClock)!=0;
            std::printf("PERF_SAMPLE {%s,\"case\":\"%s\",\"phase\":\"%s\",\"sequence\":%u,\"business_bytes\":%u,\"vector_bytes\":%u,\"serialized_bytes\":%u,\"flags\":%u,\"write_before_ns\":%s,\"write_after_ns\":%s,\"write_rc\":%s,\"writer_guid\":%s,\"writer_sn\":%s,\"first_take_ns\":%s,\"source_guid\":%s,\"source_sn\":%s,\"echo_before_ns\":%s,\"echo_after_ns\":%s,\"echo_rc\":%s,\"echo_guid\":%s,\"echo_sn\":%s,\"duplicates\":%llu}\n",
                Common().c_str(),GetCase(s.id).name,PhaseName(s.phase),seq,r.business_bytes,r.vector_bytes,r.serialized_bytes,r.flags,
                JsonTime(r.write_before_ns).c_str(),JsonTime(r.write_after_ns).c_str(),JsonRc(r.write_rc).c_str(),JsonGuid(r.writer_guid).c_str(),JsonTime(r.writer_sn).c_str(),
                JsonTime(r.first_take_ns).c_str(),JsonGuid(r.source_guid).c_str(),JsonTime(r.source_sn).c_str(),JsonTime(r.echo_before_ns).c_str(),JsonTime(r.echo_after_ns).c_str(),
                JsonRc(r.echo_rc).c_str(),JsonGuid(r.echo_guid).c_str(),JsonTime(r.echo_sn).c_str(),static_cast<unsigned long long>(r.duplicates));
        }
        const bool rtt=GetCase(s.id).kind==Kind::Rtt;
        const bool complete=Good()&&!s.incomplete && (options.board_a ? (rtt ? valid==s.count&&write_ok==s.count : attempted+missed==s.count) : (rtt ? valid==s.count : s.start_ns>=0));
        const auto cpu=CpuPercent(s.cpu_before,s.cpu_after);
        const auto stage_common=Common()+",\"invalid_clock_count\":"+std::to_string(invalid_clock)+
            ",\"phase_deadline_ns\":"+JsonTime(s.phase_deadline_ns)+",\"observation_deadline_ns\":"+JsonTime(s.observation_deadline_ns);
        std::printf("PERF_SUMMARY {%s,\"case\":\"%s\",\"phase\":\"%s\",\"stage\":%u,\"planned\":%u,\"attempted\":%u,\"write_ok\":%u,\"valid_unique\":%u,\"missing\":%u,\"timed_out\":%u,\"late\":%u,\"after_observation\":%u,\"missed_slots\":%u,\"not_attempted\":%u,\"window_start_ns\":%s,\"window_end_ns\":%s,\"observation_end_ns\":%s,\"cpu_start_ns\":%s,\"cpu_end_ns\":%s,\"cpu_wall_start_ns\":%s,\"cpu_wall_end_ns\":%s,\"cpu_percent\":%s,\"ack_rc\":%s,\"clock\":\"steady\",\"clock_unit\":\"ns\",\"no_first_sample\":%s,\"complete\":%s,\"result\":\"%s\"}\n",
            stage_common.c_str(),GetCase(s.id).name,PhaseName(s.phase),s.number,s.count,attempted,write_ok,valid,s.count-valid,timeouts,late,after,missed,not_attempted,
            JsonTime(s.start_ns).c_str(),JsonTime(s.end_ns).c_str(),JsonTime(s.observation_end_ns).c_str(),JsonObserved(s.cpu_before.cpu_ns).c_str(),JsonObserved(s.cpu_after.cpu_ns).c_str(),JsonTime(s.cpu_before.wall_ns).c_str(),JsonTime(s.cpu_after.wall_ns).c_str(),cpu.observed?std::to_string(cpu.value).c_str():"null",JsonRc(s.ack_rc).c_str(),(!options.board_a&&!rtt&&s.start_ns<0)?"true":"false",complete?"true":"false",complete?(missed?"LOAD_NOT_SUSTAINED":"COMPLETE"):"INCOMPLETE");
        PrintResource("stage_before",s.before,&s); PrintResource("stage_after",s.after,&s); s.flushed=true; std::fflush(stdout);
    }
    bool Run() {
        if(!opened || !Good()) return false;
        active_begin_ns=ClockNowNs();
        const auto match_deadline=ClockNowNs()+20*Second;
        while(!Matches() && ClockNowNs()<match_deadline && Guard()) Tick();
        if(!Good()||!Matches(true)) return Fail("exact_match_timeout");
        if(options.observation_snapshot) options.observation_snapshot("MATCHED",0);
        Event("READY");
        for(current_stage=0;current_stage<stage_count && Guard();++current_stage) {
            auto& s=stages[current_stage];
            if(options.quiet_observer) options.quiet_observer(options.mode==Mode::Measure && s.phase==Phase::Formal);
            if(!ReadControl(s,false)) break;
            s.before=CaptureResources(); SnapshotMax();
            if(options.observation_snapshot) options.observation_snapshot("STAGE_BEFORE",s.number);
            s.cpu_before=CaptureCpuBoundary(); s.armed_ns=ClockNowNs(); s.first_deadline_ns=s.armed_ns+(options.host_contract?3:15)*Second;
            s.phase_deadline_ns=s.armed_ns+30*Second;
            s.observation_deadline_ns=Earlier((GetCase(s.id).kind==Kind::Rtt?s.phase_deadline_ns:s.first_deadline_ns)+LateDuration(),GlobalDeadline());
            s.armed=true; s.admitting=true;
            if(!options.board_a) Event("B_ARMED",&s);
            if(options.board_a) RunA(s); else RunB(s);
            Event(options.board_a?"A_DONE":"B_DONE",&s);
            if(!ReadControl(s,true)) break;
            // Both local observation and the peer's declared observation have
            // closed. Keep routine output quiet while heavy end reads execute.
            CaptureStageAfter(s);
            if(options.quiet_observer) options.quiet_observer(false);
            if(options.observation_snapshot) options.observation_snapshot("STAGE_FLUSH",s.number);
            // Rows are emitted once after final DDS close so later background
            // arrivals cannot leave an already printed old-stage row stale.
            Event("FLUSHED",&s);
        }
        if(!Good()) Event("FAILURE");
        return Good();
    }
    bool Close() {
        if(closed) return cleanup_failure.empty();
        global_stop=true;
        if(participant) {
            Pump();
            const auto contained=participant->delete_contained_entities();
            const auto removed=contained==dds::RETCODE_OK ? dds::DomainParticipantFactory::get_instance()->delete_participant(participant) : dds::RETCODE_PRECONDITION_NOT_MET;
            if(contained!=dds::RETCODE_OK || removed!=dds::RETCODE_OK) cleanup_failure="dds_delete";
            else { participant=nullptr; writer=nullptr; reader=nullptr; publisher=nullptr; subscriber=nullptr; }
        }
        if(options.quiet_observer) options.quiet_observer(false);
        lifecycle_resources[2]=CaptureResources(); process_boundaries[2]=GetProcessStats(); SnapshotMax();
        if(options.observation_snapshot) options.observation_snapshot("AFTER_DDS_CLOSE",stage_count);
        if(!contract_direct) {
            for(unsigned i=0;i<stage_count;++i) if(!stages[i].flushed) Flush(stages[i]);
            std::printf("PERF_DDS_STATE {%s,\"state_only_takes\":%llu,\"meaning\":\"valid_data_false_never_business\"}\n",Common().c_str(),static_cast<unsigned long long>(state_only_takes));
            for(std::size_t i=0;i<ledger.error_count();++i) {
                const auto& e=ledger.errors()[i];
                std::printf("PERF_ERROR {%s,\"case\":\"%s\",\"phase\":\"%s\",\"sequence\":%u,\"time_ns\":%s,\"reason\":%u}\n",Common().c_str(),GetCase(e.identity.case_id).name,PhaseName(e.identity.phase),e.identity.sequence,JsonTime(e.time_ns).c_str(),e.reason);
            }
            for(unsigned i=0;i<3;++i) PrintResource(i==0?"before_init":i==1?"after_init":"after_dds_close",lifecycle_resources[i]);
            const auto& c=ledger.counters();
            std::printf("PERF_COUNTERS {%s,\"wrong_run\":%llu,\"wrong_source\":%llu,\"out_of_range\":%llu,\"inactive\":%llu,\"duplicates\":%llu,\"invalid\":%llu,\"late\":%llu,\"errors\":%llu,\"lost_records\":%llu,\"lost_error_details\":%llu,\"measurement_record_overflow\":%s,\"counter_overflow\":%s,\"inbound_capacity\":%zu,\"outbound_capacity\":%zu,\"storage_bytes\":%zu,\"record_capacity\":%zu,\"record_size\":%zu,\"submitted_vector_bytes\":%llu,\"taken_samples\":%llu,\"queue_hwm_scope\":\"sampled-lower-bound\",\"sampled_tx_records_max\":%llu,\"sampled_rx_records_max\":%llu,\"sampled_tx_bytes_max\":%llu,\"sampled_rx_bytes_max\":%llu,\"first_failure\":\"%s\",\"cleanup_failure\":\"%s\"}\n",
                Common(true).c_str(),static_cast<unsigned long long>(c.wrong_run),static_cast<unsigned long long>(c.wrong_source),static_cast<unsigned long long>(c.out_of_range),static_cast<unsigned long long>(c.inactive),static_cast<unsigned long long>(c.duplicates),static_cast<unsigned long long>(c.invalid),static_cast<unsigned long long>(c.late),static_cast<unsigned long long>(c.errors),static_cast<unsigned long long>(c.lost_records),static_cast<unsigned long long>(c.lost_error_details),ledger.overflowed()?"true":"false",counter_overflow?"true":"false",inbound.data().capacity(),outbound.data().capacity(),Storage(),kRecordCapacity,sizeof(SampleRecord),static_cast<unsigned long long>(submitted_vector_bytes),static_cast<unsigned long long>(taken_samples),static_cast<unsigned long long>(sampled_tx_records),static_cast<unsigned long long>(sampled_rx_records),static_cast<unsigned long long>(sampled_tx_bytes),static_cast<unsigned long long>(sampled_rx_bytes),first_failure.c_str(),cleanup_failure.c_str());
            Event("CLOSED");
        }
        closed=true; return cleanup_failure.empty();
    }
};

PerformanceFixture::PerformanceFixture(Options options):impl_(std::make_unique<Impl>(std::move(options))) {}
PerformanceFixture::~PerformanceFixture() { impl_->Close(); }
bool PerformanceFixture::OpenNative(const std::vector<Config>& configs,SocketApi api,std::shared_ptr<ProcessRuntime> runtime) {
    if(impl_->options.host_contract || configs.size()!=1 || !runtime || configs[0].enable_rx_drop || !configs[0].additional_peers.empty()) return impl_->Fail("native_config");
    if(!impl_->Initialize()) return false;
    impl_->runtime=runtime; impl_->control=std::make_shared<Control>();
    dds::DomainParticipantQos q; impl_->BaseQos(q); const auto& c=configs[0];
    q.transport().user_transports.push_back(std::make_shared<ExperimentalDescriptor>(c,api,impl_->control,runtime));
    q.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(c.local_locator);
    q.wire_protocol().default_unicast_locator_list.push_back(c.local_locator);
    q.wire_protocol().builtin.initialPeersList.push_back(c.peer_locator);
    return impl_->Create(q);
}
bool PerformanceFixture::OpenReference(uint16_t port) {
    impl_->reference=true; if(port<1024 || port>65000) return impl_->Fail("reference_port");
    if(!impl_->Initialize()) return false;
    dds::DomainParticipantQos q; impl_->BaseQos(q);
    auto udp=std::make_shared<rtps::UDPv4TransportDescriptor>(); udp->interfaceWhiteList.push_back("127.0.0.1"); q.transport().user_transports.push_back(udp);
    const auto locator=[&](bool a,unsigned extra){rtps::Locator_t l; l.kind=LOCATOR_KIND_UDPv4; l.port=port+(a?0:2)+extra; rtps::IPLocator::setIPv4(l,127,0,0,1); return l;};
    q.wire_protocol().builtin.metatrafficUnicastLocatorList.push_back(locator(impl_->options.board_a,0));
    q.wire_protocol().default_unicast_locator_list.push_back(locator(impl_->options.board_a,10));
    q.wire_protocol().builtin.initialPeersList.push_back(locator(!impl_->options.board_a,0));
    return impl_->Create(q);
}
bool PerformanceFixture::Run() { return impl_->Run(); }
bool PerformanceFixture::Close() { return impl_->Close(); }
std::string PerformanceFixture::FirstFailure() const { return impl_->first_failure; }
std::string PerformanceFixture::CleanupFailure() const { return impl_->cleanup_failure; }
std::vector<std::shared_ptr<Control>> PerformanceFixture::Controls() const { return impl_->control ? std::vector<std::shared_ptr<Control>>{impl_->control} : std::vector<std::shared_ptr<Control>>{}; }

#ifdef FASTDDS_DSOFTBUS_PERF_HOST_CONTRACT_TESTS
bool PerformanceFixture::ContractTest(const std::string& test,uint16_t port) {
    const bool deadline_test=test.rfind("deadline-",0)==0;
    if(test!="wrong-length" && test!="real-echo" && test!="cleanup" && test!="late-original" && test!="no-first" && test!="future-echo" && test!="expired-slot" && test!="phase-order" && !deadline_test) return false;
    auto* factory=dds::DomainParticipantFactory::get_instance();
    eprosima::fastdds::LibrarySettings settings; settings.intraprocess_delivery=eprosima::fastdds::INTRAPROCESS_OFF;
    if(factory->set_library_settings(settings)!=dds::RETCODE_OK) return false;
    Options oa{"c001c001c001c001",true,".",Mode::Trace,1,true,true,{}};
    if(test=="phase-order") oa.mode=Mode::Measure;
    auto ob=oa; ob.board_a=false;
    PerformanceFixture a(oa),b(ob); a.impl_->contract_direct=true; b.impl_->contract_direct=true;
    bool ok=a.OpenReference(port)&&b.OpenReference(port);
    const auto deadline=ClockNowNs()+10*Second;
    while(ok && !(a.impl_->Matches()&&b.impl_->Matches()) && ClockNowNs()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ok=ok&&a.impl_->Matches()&&b.impl_->Matches();
    if(ok && test=="phase-order") {
        const auto directory=std::filesystem::temp_directory_path()/("mdds-perf-phase-"+std::to_string(port)+"-"+std::to_string(ClockNowNs()));
        ok=std::filesystem::create_directory(directory)&&ok;
        a.impl_->options.control_directory=directory.string(); a.impl_->stage_count=1;
        auto& s=a.impl_->stages[0]; s.id=CaseId::LOAD_1K; s.phase=Phase::Formal; s.number=0; s.count=1;
        auto& bs=b.impl_->stages[0]; bs=s; bs.armed=true; bs.admitting=true; b.impl_->current_stage=0;
        bs.first_deadline_ns=ClockNowNs()+15*Second;
        bool quiet=false,done=false,flushed=false,heavy_seen=false,heavy_safe=true;
        a.impl_->options.quiet_observer=[&](bool value){quiet=value;};
        a.impl_->test_event=[&](const char* event){
            const std::string name=event;
            if(name=="A_DONE") { done=true; std::ofstream f(directory/"stage-0.flush",std::ios::binary); f<<"PERF_FLUSH "<<oa.run<<" 0\n"; }
            if(name=="FLUSH_ACCEPTED") flushed=true;
            if(name=="HEAVY_AFTER") { heavy_seen=true; heavy_safe=heavy_safe&&done&&flushed&&quiet&&s.cpu_after.wall_ns>=0; }
        };
        { std::ofstream f(directory/"stage-0.release",std::ios::binary); f<<"PERF_RELEASE "<<oa.run<<" 0 LOAD_1K FORMAL\n"; }
        ok=a.Run()&&ok; b.impl_->Pump();
        ok=ok&&heavy_seen&&heavy_safe&&done&&flushed&&!quiet;
        a.impl_->test_event={}; a.impl_->options.quiet_observer={};
        for(const char* file:{"stage-0.release","stage-0.release.consumed","stage-0.flush","stage-0.flush.consumed"}) std::filesystem::remove(directory/file);
        std::filesystem::remove(directory);
    } else if(ok && test!="cleanup") {
        for(auto* p:{a.impl_.get(),b.impl_.get()}) { p->current_stage=0; p->stages[0].armed=true; p->stages[0].admitting=true; p->stages[0].armed_ns=ClockNowNs(); }
        const Identity id{a.impl_->run,CaseId::RTT_1K,Phase::Formal,1};
        if(deadline_test) {
            const int64_t offset=test.find("-before")!=std::string::npos?-1:test.find("-after")!=std::string::npos?1:0;
            const bool a_response=test.rfind("deadline-a-",0)==0;
            const bool b_echo=test.rfind("deadline-b-",0)==0;
            const bool load=test.rfind("deadline-load-",0)==0;
            if(a_response) {
                a.impl_->Write(id); auto* ar=a.impl_->ledger.Find(id);
                const int64_t limit=ar->write_before_ns+(test.find("-request-")!=std::string::npos?2:1)*Second;
                if(test.find("-phase-")!=std::string::npos) a.impl_->stages[0].phase_deadline_ns=limit;
                if(test.find("-global-")!=std::string::npos) a.impl_->active_begin_ns=limit-180*Second;
                a.impl_->test_clock=[&](unsigned kind){return kind==1?limit+offset:ClockNowNs();};
                const auto until=ClockNowNs()+5*Second;
                while(!(ar->flags&Validated)&&ClockNowNs()<until) { b.impl_->Pump(); a.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
                ok=ok&&(ar->flags&Validated)&&ar->first_take_ns==limit+offset;
                if(offset<0) ok=ok&&SuccessfulRtt(*ar);
                else ok=ok&&!SuccessfulRtt(*ar)&&(ar->flags&Late)&&(ar->flags&TimedOut)&&a.impl_->stages[0].incomplete;
                a.impl_->test_clock={};
            } else if(b_echo) {
                const int64_t limit=ClockNowNs()+Second;
                const bool delay=test.find("-delay")!=std::string::npos;
                if(test.find("-global-")!=std::string::npos) b.impl_->active_begin_ns=limit-180*Second;
                else b.impl_->stages[0].phase_deadline_ns=limit;
                b.impl_->test_clock=[&](unsigned kind){return kind==1?limit+(delay?-1:offset):kind==2?limit+(delay?1:offset):ClockNowNs();};
                a.impl_->Write(id); auto* br=b.impl_->ledger.Find(id); const auto until=ClockNowNs()+5*Second;
                while(!(br->flags&Validated)&&ClockNowNs()<until) { b.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
                ok=ok&&(br->flags&Validated);
                if(offset<0&&!delay) ok=ok&&br->echo_rc==0;
                else ok=ok&&br->echo_rc==kUnobservedRc&&br->echo_before_ns==kUnobservedNs&&(br->flags&Late)&&b.impl_->submitted_vector_bytes==0;
                b.impl_->test_clock={};
            } else if(load) {
                const Identity load_id{a.impl_->run,CaseId::LOAD_1K,Phase::Formal,1};
                auto* stage=b.impl_->FindStage(load_id); const auto limit=ClockNowNs()+Second;
                b.impl_->current_stage=stage->number; stage->armed=true; stage->admitting=true;
                if(test=="deadline-load-first-equal") stage->first_deadline_ns=limit;
                else { stage->start_ns=limit-Second; stage->end_ns=limit; }
                if(test=="deadline-load-observation-equal") { stage->end_ns=limit-1; stage->observation_deadline_ns=stage->observation_end_ns=limit; stage->admitting=false; }
                b.impl_->test_clock=[&](unsigned kind){return kind==1?limit:ClockNowNs();};
                a.impl_->Write(load_id); auto* br=b.impl_->ledger.Find(load_id); const auto until=ClockNowNs()+5*Second;
                while(!(br->flags&Validated)&&ClockNowNs()<until) { b.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
                ok=ok&&(br->flags&Validated)&&(br->flags&Late);
                if(test=="deadline-load-first-equal") ok=ok&&stage->start_ns<0&&stage->end_ns<0;
                if(test=="deadline-load-observation-equal") ok=ok&&(br->flags&AfterObservation);
                b.impl_->test_clock={};
            } else if(test=="deadline-write-global-equal") {
                const auto limit=ClockNowNs()+Second; a.impl_->active_begin_ns=limit-180*Second;
                a.impl_->test_clock=[&](unsigned kind){return kind==0?limit:ClockNowNs();};
                a.impl_->Write(id); const auto* ar=a.impl_->ledger.Find(id);
                ok=ok&&!(ar->flags&Attempted)&&(ar->flags&NotAttempted)&&ar->write_before_ns==kUnobservedNs&&a.impl_->submitted_vector_bytes==0;
                a.impl_->test_clock={};
            } else ok=false;
        } else if(test=="wrong-length") {
            ok=FillPayload(id,a.impl_->outbound.data()); a.impl_->outbound.index(1);
            a.impl_->outbound.data().resize(4096); // Valid DDS/IDL length, invalid benchmark length.
            rtps::WriteParams params;
            ok=ok&&a.impl_->writer->write(&a.impl_->outbound,params)==dds::RETCODE_OK;
            const auto until=ClockNowNs()+5*Second;
            while(b.impl_->taken_samples==0 && ClockNowNs()<until) { b.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            ok=ok&&b.impl_->taken_samples==1&&b.impl_->ledger.counters().invalid==1&&b.impl_->inbound.data().size()==4096&&
                b.impl_->inbound.data().capacity()==kInboundCapacity&&b.impl_->Storage()>0&&b.impl_->first_failure=="invalid_payload_or_capacity";
        } else if(test=="real-echo") {
            a.impl_->Write(id); const auto until=ClockNowNs()+5*Second;
            while(ClockNowNs()<until && !(a.impl_->ledger.Find(id)->flags&Validated)) { b.impl_->Pump(); a.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            const auto* ar=a.impl_->ledger.Find(id); const auto* br=b.impl_->ledger.Find(id);
            ok=ok&&SuccessfulRtt(*ar)&&br->echo_rc==0&&br->echo_before_ns>=br->first_take_ns&&
                ar->first_take_ns>=ar->write_before_ns&&ar->source_guid==br->echo_guid&&ar->source_sn==br->echo_sn;
            // A duplicate benchmark identity arrives via a distinct real DDS SN.
            rtps::WriteParams p; ok=ok&&a.impl_->writer->write(&a.impl_->outbound,p)==dds::RETCODE_OK;
            const auto prior=br->echo_before_ns, duplicate_until=ClockNowNs()+5*Second;
            while(br->duplicates==0 && ClockNowNs()<duplicate_until) { b.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            ok=ok&&br->duplicates==1&&br->echo_before_ns==prior;
        } else if(test=="expired-slot") {
            const Identity missed{a.impl_->run,CaseId::LOAD_1K,Phase::Formal,1};
            a.impl_->Write(missed,ClockNowNs()-1);
            const auto* row=a.impl_->ledger.Find(missed);
            ok=ok&&!(row->flags&Attempted)&&(row->flags&NotAttempted)&&(row->flags&MissedSlot)&&
                row->write_before_ns==kUnobservedNs&&row->write_rc==kUnobservedRc&&a.impl_->submitted_vector_bytes==0;
        } else {
            const bool future=test=="future-echo";
            const Identity other{a.impl_->run,future?CaseId::RTT_4K:CaseId::LOAD_1K,Phase::Formal,1};
            auto* stage=b.impl_->FindStage(other);
            if(!future) {
                stage->armed=true; stage->admitting=test=="no-first"; stage->done=test=="late-original";
                stage->first_deadline_ns=ClockNowNs()-1; stage->observation_deadline_ns=stage->observation_end_ns=test=="late-original"?ClockNowNs()-1:-1;
                b.impl_->current_stage=test=="late-original"?2:1;
            }
            a.impl_->Write(other); const auto until=ClockNowNs()+5*Second;
            while(b.impl_->taken_samples==0 && ClockNowNs()<until) { b.impl_->Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            const auto* row=b.impl_->ledger.Find(other);
            ok=ok&&b.impl_->taken_samples==1&&row->echo_rc==kUnobservedRc;
            if(future) ok=ok&&b.impl_->first_failure=="future_or_inactive_identity"&&!(row->flags&Validated)&&b.impl_->ledger.counters().inactive==1;
            else ok=ok&&(row->flags&Validated)&&(row->flags&Late)&&b.impl_->ledger.counters().late==1&&stage->start_ns<0&&stage->end_ns<0&&
                (test!="late-original"||(row->flags&AfterObservation));
        }
    }
    const bool clean_a=a.Close(),clean_b=b.Close();
    return ok&&clean_a&&clean_b&&a.Close()&&b.Close()&&a.impl_->participant==nullptr&&b.impl_->participant==nullptr;
}
#endif
}
