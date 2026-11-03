#include "NativeObservation.hpp"
#include "../../../probes/PublicSocketRtpsObservation.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <limits>

namespace {
constexpr std::size_t capacity = 8192;
struct Frame { std::uint64_t observation; bool sending; std::int32_t socket;
    std::array<std::uint8_t,16> writer; std::int32_t high; std::uint32_t low;
    bool fragment=false,bounds_known=false,key=false;
    std::uint32_t first=0,sample_size=0;
    std::uint16_t fragment_count=0,fragment_size=0;
    std::uint64_t payload_bytes=0; };
struct ObserverState {
    std::array<Frame,capacity> frames{};
    std::size_t count=0;
    std::mutex mutex;
    fastdds_dsoftbus::ros2_target_observation::V1LocatorSnapshot v1_locators;
    fastdds_dsoftbus::ros2_target_observation::EndpointSnapshot endpoints;
    fastdds_dsoftbus::ros2_target_observation::SubmissionSnapshot submissions;
    fastdds_dsoftbus::ros2_target_observation::ReceiveDecisionSnapshot receive_decisions;
    std::string run;
    unsigned side=0;
    std::atomic<std::uint64_t> observations{0},send_enter{0},send_return{0},send_error{0},ingress{0},overflow{0},malformed{0};
    std::mutex startup_mutex;
    fastdds_dsoftbus::ros2_diagnostics::Snapshot startup;
    std::atomic<std::uint64_t> capture_faults{0},exported_failure{0},output_errors{0};
    std::atomic<bool> admissions_exported{false};
    std::atomic<std::uint64_t> receive_capture_dropped{0};
    bool cleanup_started=false;
};
// Begin publishes one fully initialized, fixed-capacity metadata store. It owns
// no Runtime, payload, receiver, SDK handle or thread. A failed public deletion
// may retain the pump/callback module until process exit, so this store must not
// register a static destructor. Callback paths never allocate or initialize it.
std::atomic<ObserverState*> published{nullptr};
std::atomic<bool> begin_claimed{false};
std::atomic<std::uint64_t> prebegin_faults{0};
void increment(std::uint64_t& value,std::uint64_t& overflow) noexcept {
    if(value!=(std::numeric_limits<std::uint64_t>::max)())++value;
    else if(overflow!=(std::numeric_limits<std::uint64_t>::max)())++overflow;
}
ObserverState* trace_state() noexcept {
    auto* state=published.load(std::memory_order_acquire);
    if(!state)++prebegin_faults;
    return state;
}
bool suffix(const char* text,const char* ending) {
    const auto n=std::strlen(text),m=std::strlen(ending);return n>=m&&!std::strcmp(text+n-m,ending);
}
void capture(const char* name,std::uint64_t entry,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept {
    using namespace fastdds_dsoftbus::ros2_diagnostics;
    auto* state=trace_state();if(!state)return;
    auto& startup=state->startup;
    auto& cleanup_started=state->cleanup_started;
    try {
        std::lock_guard<std::mutex> lock(state->startup_mutex);
        if(!name){++startup.omitted;return;}
        // Sender admission is recorded in its separate bounded metadata ledger.
        // Expected offline refusals/retries must not consume the SDK startup
        // error capacity or become an SDK first failure. Real SendBytes errors
        // and capture faults retain their existing diagnostics and counters.
        if(!std::strcmp(name,"Sender_submit_result")){++startup.omitted;return;}
        Event e;e.seq=++startup.last_seq;e.entry=entry;e.role=role;e.socket=socket;e.length=length;e.value=value;
        std::size_t n=0;while(n<sizeof(e.operation)-1&&name[n]){const char ch=name[n];e.operation[n]=(ch>='A'&&ch<='Z')||(ch>='a'&&ch<='z')||(ch>='0'&&ch<='9')||ch=='_'?ch:'_';++n;}
        e.operation[n]='\0';e.truncated=name[n]!='\0';
        const bool node_sdk=!std::strncmp(name,"RegNode_",8)||!std::strncmp(name,"UnregNode_",10)||!std::strncmp(name,"GetAllNodes_",12)||!std::strncmp(name,"GetNodeUdid_",12);
        const bool sdk=node_sdk||!std::strncmp(name,"Socket_",7)||!std::strncmp(name,"Listen_",7)||!std::strncmp(name,"Bind_",5)||!std::strncmp(name,"SendBytes_",10)||!std::strncmp(name,"Shutdown_",9);
        int operation=-1;
        if(!std::strncmp(name,"Socket_",7))operation=0;else if(!std::strncmp(name,"Listen_",7))operation=1;
        else if(!std::strncmp(name,"Bind_",5))operation=2;else if(!std::strncmp(name,"SendBytes_",10))operation=3;else if(!std::strncmp(name,"Shutdown_",9))operation=4;
        bool failed=false;
        if(sdk&&suffix(name,"_enter")){e.entered=true;if(operation>=0)++startup.entered[operation];}
        else if(sdk&&suffix(name,"_return")){
            e.entered=true;e.returned=true;if(operation>=0)++startup.returned[operation];
            if(!std::strcmp(name,"Shutdown_void_return"))e.type=ValueType::VoidReturn;
            else if(operation==0&&value>=0)e.type=ValueType::SocketHandle;
            else {e.type=ValueType::SdkReturn;failed=operation==0?value<0:value!=0;if(operation==0)e.socket=-1;}
        }else if(!std::strncmp(name,"On",2)){e.type=ValueType::CallbackStatus;e.returned=true;failed=!std::strcmp(name,"OnError");}
        else {e.type=ValueType::LocalStatus;e.returned=true;failed=suffix(name,"_result")&&value!=0;}
        if(!std::strcmp(name,"LocalCleanup_enter"))cleanup_started=true;
        e.cleanup=cleanup_started;
        // A diagnosis is not automatically the application's primary failure.
        if(failed&&!e.cleanup&&!startup.first_present){startup.first=e;startup.first_present=true;}
        const bool ordinary_data=!std::strcmp(name,"OnBytes")||!std::strncmp(name,"SendBytes_",10)||!std::strcmp(name,"Sender_submit_result");
        if(ordinary_data&&!failed){++startup.omitted;return;}
        if(startup.count<startup_capacity)startup.events[startup.count++]=e;else ++startup.overflow;
    }catch(...){++state->capture_faults;}
}
}
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
void TraceEvent(const char* event,std::uint64_t entry,int role,std::int32_t socket,std::int64_t value,std::uint64_t length) noexcept {
    auto* state=trace_state();if(!state)return;
    if(!event){++state->capture_faults;return;}
    if(!std::strcmp(event,"SendBytes_enter"))++state->send_enter;
    else if(!std::strcmp(event,"SendBytes_return")){++state->send_return;if(value!=0)++state->send_error;}
    capture(event,entry,role,socket,value,length);
}
void TraceText(const char* key,const char* borrowed) noexcept {
    // The interface supplies a valid borrowed C string. Do not retain pointers,
    // peer identities or arbitrary text; bounded scanning records only truncation.
    auto* state=trace_state();if(!state)return;
    if(key&&!std::strcmp(key,"V1_SELECTED_LOCATOR")) {
        // Exact value-only diagnostic key; arbitrary text stays redacted below.
        try {
            std::lock_guard<std::mutex> lock(state->mutex);
            auto& output=state->v1_locators;
            std::size_t length=0;if(borrowed)while(length<51&&borrowed[length])++length;
            if(length!=50||!borrowed||borrowed[8]!=':'||borrowed[17]!=':'||output.selected_count==output.selected.size()){++output.faults;return;}
            auto nibble=[](char ch)->int {return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:-1;};
            ros2_target_observation::V1LocatorSnapshot::Selected value;
            for(unsigned i=0;i<50;++i)if(i!=8&&i!=17&&nibble(borrowed[i])<0){++output.faults;return;}
            for(unsigned i=0;i<8;++i){value.kind=(value.kind<<4)|unsigned(nibble(borrowed[i]));value.port=(value.port<<4)|unsigned(nibble(borrowed[9+i]));}
            for(unsigned i=0;i<16;++i)value.address[i]=std::uint8_t((nibble(borrowed[18+2*i])<<4)|nibble(borrowed[19+2*i]));
            output.selected[output.selected_count++]=value;return;
        }catch(...){++state->capture_faults;return;}
    }
    auto& startup=state->startup;
    try {
        std::lock_guard<std::mutex> lock(state->startup_mutex);ros2_diagnostics::Event e;
        e.seq=++startup.last_seq;e.type=ros2_diagnostics::ValueType::RedactedText;e.redacted=true;e.returned=true;e.cleanup=state->cleanup_started;
        std::strcpy(e.operation,"TraceText_redacted");
        std::size_t n=0;if(borrowed){while(n<64&&borrowed[n])++n;e.truncated=n==64&&borrowed[n]!='\0';}else e.truncated=false;
        e.length=n;if(e.truncated)++startup.text_truncated;
        if(!key||!borrowed){++startup.omitted;std::strcpy(e.operation,"TraceText_unknown");e.type=ros2_diagnostics::ValueType::NotObserved;e.returned=false;}
        if(startup.count<ros2_diagnostics::startup_capacity)startup.events[startup.count++]=e;else ++startup.overflow;
    }catch(...){++state->capture_faults;}
}
}
namespace fastdds_dsoftbus::dsoftbus::dds_experiment {
std::uint64_t SocketFrame(const char* event,std::int32_t socket,const std::uint8_t* data,std::size_t size) noexcept {
    auto* state=trace_state();if(!state)return 0;
    const auto observation=++state->observations;
    const bool sending=!std::strcmp(event,"SDK_SEND");
    if(!sending)++state->ingress;
    const auto decoded=fastdds_dsoftbus::experiment::ObserveRtps(data,size);
    if(!decoded.valid){++state->malformed;return observation;}
    // Bounded structural metadata only. Never store payload or alter/forward it.
    std::lock_guard<std::mutex> lock(state->mutex);
    if(sending){
        const auto endpoints=fastdds_dsoftbus::experiment::ObserveSedpEndpoints(data,size,decoded);
        if(!endpoints.valid)state->endpoints.complete=false;
        else for(std::size_t i=0;i<endpoints.count;++i){const auto& value=endpoints.values[i];auto& stored=state->endpoints;
            bool known=false;for(std::size_t j=0;j<stored.count;++j)if(stored.values[j].guid==value.guid){known=true;if(stored.values[j].topic!=value.topic||stored.values[j].reader!=value.reader)stored.complete=false;break;}
            if(!known){if(stored.count==stored.values.size())stored.complete=false;else stored.values[stored.count++]=value;}
        }
    }
    const auto locators=fastdds_dsoftbus::experiment::ObserveSpdpLocators(data,size);
    if(!locators.valid||locators.unsupported||locators.overflow)++state->v1_locators.faults;
    else for(std::size_t i=0;i<locators.count;++i) {
        auto& output=state->v1_locators;
        if(output.advert_count==output.adverts.size()){++output.faults;break;}
        output.adverts[output.advert_count++]={observation,sending,socket,locators.values[i]};
    }
    for(const auto& item:decoded.data) {
        if(state->count==capacity){++state->overflow;continue;}
        state->frames[state->count++]={observation,sending,socket,item.writer_guid,item.sequence_hi,item.sequence_lo};
    }
    for(const auto& item:decoded.fragments) {
        if(state->count==capacity){++state->overflow;continue;}
        state->frames[state->count++]={observation,sending,socket,item.writer_guid,item.sequence_hi,item.sequence_lo,
            true,item.payload_bounds_known,item.key_flag,item.fragment_start,item.sample_size,
            item.fragment_count,item.fragment_size,item.payload_bytes};
    }
    return observation;
}
}
extern "C" void FastddsDsoftbusObserveSenderSubmission(std::uint64_t sender,const void* data,std::size_t bytes,bool decode_complete,int status,int queue_result,bool route_known,bool route_state_known,bool route_available,std::uint64_t route_revision) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return;
    // Decoding may allocate, so it stays outside both the sender gate and the
    // leaf metadata gate. Scalar admission capture does not depend on decoding.
    const auto packet=decode_complete&&data&&bytes?
        fastdds_dsoftbus::experiment::ObserveRtps(data,bytes):fastdds_dsoftbus::experiment::RtpsObservation{};
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& ledger=state->submissions;
    increment(ledger.total,ledger.counter_overflow);
    const bool queue_known=queue_result!=-1,queue_admitted=queue_result==0;
    if(status>=0&&std::size_t(status)<ledger.status_counts.size())increment(ledger.status_counts[std::size_t(status)],ledger.counter_overflow);
    else increment(ledger.unknown_status,ledger.counter_overflow);
    if(!queue_known)increment(ledger.queue_unknown,ledger.counter_overflow);
    else if(queue_result>=0&&std::size_t(queue_result)<ledger.queue_counts.size())increment(ledger.queue_counts[std::size_t(queue_result)],ledger.counter_overflow);
    else increment(ledger.queue_invalid,ledger.counter_overflow);
    if(status!=0){
        increment(ledger.refused,ledger.counter_overflow);
        if(!ledger.first_refusal_present){ledger.first_refusal_present=true;
            ledger.first_refusal={ledger.total,sender,bytes,route_revision,status,queue_result,
                queue_known,route_known,route_state_known,route_available,decode_complete};}
    }
    if(!packet.valid||packet.data.empty()){increment(ledger.uncorrelated,ledger.counter_overflow);return;}
    for(const auto& frame:packet.data){
        const auto observation=++state->observations;
        auto found=std::find_if(ledger.values.begin(),ledger.values.begin()+ledger.count,[&](const auto& item){return item.sender==sender&&item.status==status&&item.writer==frame.writer_guid&&item.high==frame.sequence_hi&&item.low==frame.sequence_lo&&item.route_known==route_known&&item.route_available==route_available&&item.route_revision==route_revision&&item.queue_admitted==queue_admitted;});
        if(found!=ledger.values.begin()+ledger.count){++found->attempts;found->last_observation=observation;continue;}
        if(ledger.count==ledger.values.size()){++ledger.overflow;continue;}
        ledger.values[ledger.count++]={observation,observation,1,sender,frame.writer_guid,frame.sequence_hi,status,frame.sequence_lo,route_known,route_available,route_revision,queue_admitted};
    }
}
namespace fastdds_dsoftbus::ros2_target_observation {
void ObserveReceiveDecision(const ReceiveDecisionSnapshot::Item& original) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return;
    // Original callback stack: never wait behind a structural metadata copy.
    // A contended leaf store is an explicit coverage loss, not a green zero.
    std::unique_lock<std::mutex> lock(state->mutex,std::try_to_lock);
    if(!lock.owns_lock()){++state->receive_capture_dropped;return;}
    auto& ledger=state->receive_decisions;
    increment(ledger.total,ledger.counter_overflow);
    if(original.branch_known&&original.branch>=0&&std::size_t(original.branch)<ledger.branch_counts.size())increment(ledger.branch_counts[std::size_t(original.branch)],ledger.counter_overflow);
    else increment(ledger.unknown_branch,ledger.counter_overflow);
    if(original.bridge_called&&original.reason>=0&&std::size_t(original.reason)<ledger.reason_counts.size())increment(ledger.reason_counts[std::size_t(original.reason)],ledger.counter_overflow);
    else if(original.bridge_called)increment(ledger.unknown_reason,ledger.counter_overflow);
    if(original.rejected){
        increment(ledger.refused,ledger.counter_overflow);
        if(!ledger.first_refusal_present){ledger.first_refusal_present=true;ledger.first_refusal=original;}
    }
}
bool CaptureReceiveDecisions(ReceiveDecisionSnapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    std::lock_guard<std::mutex> lock(state->mutex);output=state->receive_decisions;
    output.capture_dropped=state->receive_capture_dropped.load();return true;
}
bool CaptureSubmissions(SubmissionSnapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    std::lock_guard<std::mutex> lock(state->mutex);output=state->submissions;return true;
}
bool ExportAdmissions() noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    if(state->admissions_exported.exchange(true))return state->output_errors.load()==0;
    SubmissionSnapshot sends;ReceiveDecisionSnapshot receives;
    if(!CaptureSubmissions(sends)||!CaptureReceiveDecisions(receives)){++state->capture_faults;return false;}
    const char* run=state->run.c_str();const char side=state->side?'B':'A';
    std::fprintf(stdout,"ROS_SEND_ADMISSION_SUMMARY run=%s side=%c total=%llu refused=%llu uncorrelated=%llu structural_stored=%zu structural_overflow=%llu counter_overflow=%llu unknown_status=%llu queue_unknown=%llu queue_invalid=%llu scope=ORIGINAL_LOCAL_SUBMISSION native_entry=NOT_INFERRED final_host_threads_joined=1\n",
        run,side,static_cast<unsigned long long>(sends.total),static_cast<unsigned long long>(sends.refused),static_cast<unsigned long long>(sends.uncorrelated),sends.count,static_cast<unsigned long long>(sends.overflow),static_cast<unsigned long long>(sends.counter_overflow),static_cast<unsigned long long>(sends.unknown_status),static_cast<unsigned long long>(sends.queue_unknown),static_cast<unsigned long long>(sends.queue_invalid));
    for(std::size_t i=0;i<sends.status_counts.size();++i)if(sends.status_counts[i])std::fprintf(stdout,"ROS_SEND_ADMISSION_STATUS run=%s side=%c status=%zu count=%llu result_domain=RuntimeSendStatus\n",run,side,i,static_cast<unsigned long long>(sends.status_counts[i]));
    for(std::size_t i=0;i<sends.queue_counts.size();++i)if(sends.queue_counts[i])std::fprintf(stdout,"ROS_SEND_QUEUE_STATUS run=%s side=%c status=%zu count=%llu result_domain=SendAdmission sdk_return=NOT_APPLICABLE\n",run,side,i,static_cast<unsigned long long>(sends.queue_counts[i]));
    if(sends.first_refusal_present){const auto& f=sends.first_refusal;
        std::fprintf(stdout,"ROS_SEND_ADMISSION_FIRST_REFUSAL run=%s side=%c operation=%llu sender=%llu bytes=%llu status=%d queue_known=%d queue_result=%d route_known=%d route_state_known=%d available=%d route_revision=%llu decode_complete=%d state_source=ORIGINAL_ADMISSION_REPORT native_entry=NOT_INFERRED diagnostic_identity=LOCAL_ONLY\n",run,side,static_cast<unsigned long long>(f.operation),static_cast<unsigned long long>(f.sender),static_cast<unsigned long long>(f.bytes),f.status,int(f.queue_known),f.queue_result,int(f.route_known),int(f.route_state_known),int(f.route_available),static_cast<unsigned long long>(f.route_revision),int(f.decode_complete));}
    std::fprintf(stdout,"ROS_RECEIVE_DECISION_SUMMARY run=%s side=%c total=%llu refused=%llu counter_overflow=%llu unknown_branch=%llu unknown_reason=%llu capture_dropped=%llu scope=ORIGINAL_BINDING_AND_ADAPTER_DECISIONS final_host_threads_joined=1 sdk_epoch=NOT_PROVEN\n",run,side,static_cast<unsigned long long>(receives.total),static_cast<unsigned long long>(receives.refused),static_cast<unsigned long long>(receives.counter_overflow),static_cast<unsigned long long>(receives.unknown_branch),static_cast<unsigned long long>(receives.unknown_reason),static_cast<unsigned long long>(receives.capture_dropped));
    for(std::size_t i=0;i<receives.branch_counts.size();++i)if(receives.branch_counts[i])std::fprintf(stdout,"ROS_RECEIVE_DECISION_BRANCH run=%s side=%c branch=%zu count=%llu\n",run,side,i,static_cast<unsigned long long>(receives.branch_counts[i]));
    for(std::size_t i=0;i<receives.reason_counts.size();++i)if(receives.reason_counts[i])std::fprintf(stdout,"ROS_RECEIVE_ADMISSION_REASON run=%s side=%c reason=%zu count=%llu\n",run,side,i,static_cast<unsigned long long>(receives.reason_counts[i]));
    if(receives.first_refusal_present){const auto& f=receives.first_refusal;
        std::fprintf(stdout,"ROS_RECEIVE_DECISION_FIRST_REFUSAL run=%s side=%c layer=%d branch=%d sequence=%llu record_incarnation=%llu correlation=%llu socket=%d role=%d phase=%d bytes=%llu bridge_called=%d refused_receive=%d refused_receive_total=%llu status_known=%d status=%d stage=%d stage_name=%s reason=%d reason_name=%s result_known=%d result=%d native_status_known=%d native_status=%d native_known=%d native_slot=%llu native_incarnation=%llu native_generation=%llu native_route_version=%llu application_known=%d application_incarnation=%llu logical_token=%llu receiver_known=%d receiver_incarnation=%llu capacity_known=%d lifecycle_known=%d application_phase=%d receiver_phase=%d stopped=%d record_limit_known=%d record_limit=%zu records_used=%zu records_limit=%zu bytes_used=%zu bytes_limit=%zu local_records_used=%zu local_records_limit=%zu local_bytes_used=%zu local_bytes_limit=%zu copying_used=%zu copying_limit=%zu application_copying=%zu application_copying_limit=%zu state_source=ORIGINAL_DECISION_GATES diagnostic_identity=LOCAL_ONLY\n",
            run,side,f.layer,f.branch,static_cast<unsigned long long>(f.sequence),static_cast<unsigned long long>(f.record_incarnation),static_cast<unsigned long long>(f.correlation),f.socket,f.role,f.phase,static_cast<unsigned long long>(f.bytes),int(f.bridge_called),int(f.refused_receive),static_cast<unsigned long long>(f.refused_receive_total),int(f.status_known),f.status,f.stage,f.stage_name,f.reason,f.reason_name,int(f.result_known),f.result,int(f.native_status_known),f.native_status,int(f.native_known),static_cast<unsigned long long>(f.native_slot),static_cast<unsigned long long>(f.native_incarnation),static_cast<unsigned long long>(f.native_generation),static_cast<unsigned long long>(f.native_route_version),int(f.application_known),static_cast<unsigned long long>(f.application_incarnation),static_cast<unsigned long long>(f.logical_token),int(f.receiver_known),static_cast<unsigned long long>(f.receiver_incarnation),int(f.capacity_known),int(f.lifecycle_known),f.application_phase,f.receiver_phase,int(f.stopped),int(f.record_limit_known),f.record_limit,f.records_used,f.records_limit,f.bytes_used,f.bytes_limit,f.local_records_used,f.local_records_limit,f.local_bytes_used,f.local_bytes_limit,f.copying_used,f.copying_limit,f.application_copying,f.application_copying_limit);}
    if(std::fflush(stdout)||std::ferror(stdout)){++state->output_errors;return false;}
    return true;
}
bool CaptureEndpoints(EndpointSnapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    try {std::lock_guard<std::mutex> lock(state->mutex);output=state->endpoints;return true;}catch(...){return false;}
}
bool CaptureV1(V1LocatorSnapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    try {std::lock_guard<std::mutex> lock(state->mutex);output=state->v1_locators;return true;}catch(...){++state->capture_faults;return false;}
}
void Begin(const std::string& identifier,unsigned board) {
    if(identifier.size()!=16||board>1||identifier.find_first_not_of("0123456789abcdef")!=std::string::npos)
        throw std::invalid_argument("Invalid observer identity");
    bool expected=false;
    if(!begin_claimed.compare_exchange_strong(expected,true))throw std::logic_error("Observer already initialized");
    try {
        std::unique_ptr<ObserverState> candidate(new ObserverState);
        candidate->run=identifier;candidate->side=board;
        candidate->startup.side=board;candidate->startup.source=ros2_diagnostics::Source::RealTarget;
        candidate->startup.initialized=true;
        std::memcpy(candidate->startup.run,identifier.c_str(),17);
        published.store(candidate.release(),std::memory_order_release);
    }catch(...){begin_claimed=false;throw;}
}
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void SetSourceForHostInjection() noexcept {auto* state=trace_state();if(!state)return;try{std::lock_guard<std::mutex> lock(state->startup_mutex);state->startup.source=ros2_diagnostics::Source::HostInjected;}catch(...){++state->capture_faults;}}
#endif
CaptureState TryCapture(ros2_diagnostics::Snapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return CaptureState::Unavailable;
    try{std::unique_lock<std::mutex> lock(state->startup_mutex,std::try_to_lock);if(!lock.owns_lock())return CaptureState::Busy;output=state->startup;output.overflow+=state->capture_faults.load()+prebegin_faults.load();output.output_errors=state->output_errors.load();return output.initialized?CaptureState::Ready:CaptureState::Unavailable;}catch(...){++state->capture_faults;return CaptureState::Error;}
}
bool Capture(ros2_diagnostics::Snapshot& output) noexcept { return TryCapture(output)==CaptureState::Ready; }
#ifdef FASTDDS_DSOFTBUS_OBSERVATION_HOST_TEST
void WithStartupGateForHostTest(void (*call)(void*),void* context) {
    auto* state=published.load(std::memory_order_acquire);if(!state)throw std::logic_error("observer not initialized");
    std::lock_guard<std::mutex> lock(state->startup_mutex);call(context);
}
#endif
bool ExportPending(ros2_diagnostics::Writer writer,void* context) noexcept {
    // Control-side prefix export is separate from this first-error exporter.
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    ros2_diagnostics::Snapshot snapshot;
    if(!Capture(snapshot)||!snapshot.first_present)return true;
    auto previous=state->exported_failure.load();if(previous>=snapshot.first.seq)return state->output_errors.load()==0;
    if(!state->exported_failure.compare_exchange_strong(previous,snapshot.first.seq))return true;
    if(!ros2_diagnostics::Write(snapshot,writer,context)||(writer==ros2_diagnostics::Stdout&&std::fflush(stdout))){++state->output_errors;return false;}
    return true;
}
bool CapturePrefix(ros2_diagnostics::NativePrefix& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    try {
        std::unique_lock<std::mutex> lock(state->mutex,std::try_to_lock);if(!lock.owns_lock())return false;
        output={};std::memcpy(output.run,state->run.c_str(),17);output.side=state->side;output.valid=true;
        output.send_enter=state->send_enter;output.send_return=state->send_return;output.send_error=state->send_error;
        output.ingress=state->ingress;output.overflow=state->overflow;output.malformed=state->malformed;
        // Existing prefix ABI and DATA label remain DATA-only. Fragment rows
        // are exported separately by Flush, with their actual wire geometry.
        for(std::size_t i=0;i<state->count;++i)if(!state->frames[i].fragment)++output.total_stored;
        output.stored=(std::min)(output.total_stored,ros2_diagnostics::prefix_capacity);
        output.first_index=output.total_stored-output.stored;
        std::size_t ordinal=0,stored=0;
        for(std::size_t i=0;i<state->count;++i){const auto& frame=state->frames[i];
            if(frame.fragment)continue;
            if(ordinal++<output.first_index)continue;
            output.frames[stored++]={frame.observation,frame.sending,frame.socket,frame.writer,frame.high,frame.low};}
        return true;
    }catch(...){++state->capture_faults;return false;}
}
bool Flush() {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    // Snapshot under the leaf observation mutex, release before any I/O. A late
    // SDK callback is not declared physically drained by this local snapshot.
    std::array<Frame,capacity> snapshot;
    std::size_t stored;
    {std::lock_guard<std::mutex> lock(state->mutex);stored=state->count;std::copy_n(state->frames.begin(),stored,snapshot.begin());}
    V1LocatorSnapshot locators;const bool locator_capture=CaptureV1(locators);
    for(std::size_t i=0;i<locators.advert_count;++i) {
        const auto& frame=locators.adverts[i];char writer[33]{},address[33]{};
        for(unsigned j=0;j<16;++j){std::snprintf(writer+2*j,3,"%02x",frame.value.writer[j]);std::snprintf(address+2*j,3,"%02x",frame.value.address[j]);}
        std::fprintf(stdout,"ROS_V1_SPDP run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_high=%u sequence_low=%u data_offset=%zu pid=%04x bytes=%u kind=%08x address=%s port=%08x\n",state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(frame.observation),frame.sending?"SDK_SEND":"SDK_INGRESS",frame.socket,writer,frame.value.sequence_high,frame.value.sequence_low,frame.value.data_submessage_offset,unsigned(frame.value.pid),unsigned(frame.value.serialized_bytes),frame.value.kind,address,frame.value.port);
    }
    for(std::size_t i=0;i<locators.selected_count;++i) {
        const auto& value=locators.selected[i];char address[33]{};for(unsigned j=0;j<16;++j)std::snprintf(address+2*j,3,"%02x",value.address[j]);
        std::fprintf(stdout,"ROS_V1_SELECTED run=%s side=%c kind=%08x address=%s port=%08x scope=ACTUAL_SELECTOR_VALUES\n",state->run.c_str(),state->side?'B':'A',value.kind,address,value.port);
    }
    std::fprintf(stdout,"ROS_V1_OBSERVATION run=%s side=%c captured=%u adverts=%zu selected=%zu faults=%llu sdk_epoch=NOT_PROVEN\n",state->run.c_str(),state->side?'B':'A',unsigned(locator_capture),locators.advert_count,locators.selected_count,static_cast<unsigned long long>(locators.faults));
    for(std::size_t i=0;i<stored;++i) {
        char guid[33]{};for(std::size_t j=0;j<16;++j)std::snprintf(guid+2*j,3,"%02x",snapshot[i].writer[j]);
        if(snapshot[i].fragment) {
            const auto& f=snapshot[i];
            std::fprintf(stdout,"ROS_NATIVE_FRAGMENT run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_hi=%d sequence_lo=%u scope=STRUCTURAL_APP_OBSERVER first=%u count=%u fragment_size=%u sample_size=%u payload_bounds_known=%u key_flag=%u payload_bytes=%llu\n",
                state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(f.observation),f.sending?"SDK_SEND":"SDK_INGRESS",f.socket,guid,f.high,f.low,
                f.first,unsigned(f.fragment_count),unsigned(f.fragment_size),f.sample_size,unsigned(f.bounds_known),unsigned(f.key),static_cast<unsigned long long>(f.payload_bytes));
        }else std::fprintf(stdout,"ROS_NATIVE_DATA run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_hi=%d sequence_lo=%u scope=STRUCTURAL_APP_OBSERVER\n",
            state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(snapshot[i].observation),snapshot[i].sending?"SDK_SEND":"SDK_INGRESS",snapshot[i].socket,guid,snapshot[i].high,snapshot[i].low);
    }
    std::fprintf(stdout,"ROS_NATIVE_SUMMARY run=%s side=%c send_enter=%llu send_return=%llu send_error=%llu ingress=%llu detailed_stored=%zu metadata_limit=%zu overflow=%llu malformed=%llu sdk_epoch=NOT_PROVEN\n",
        state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(state->send_enter.load()),static_cast<unsigned long long>(state->send_return.load()),static_cast<unsigned long long>(state->send_error.load()),static_cast<unsigned long long>(state->ingress.load()),stored,capacity,static_cast<unsigned long long>(state->overflow.load()),static_cast<unsigned long long>(state->malformed.load()));
    std::fflush(stdout);
    ros2_diagnostics::Snapshot startup_copy;
    const bool startup_ok=Capture(startup_copy)&&ros2_diagnostics::Write(startup_copy,ros2_diagnostics::Stdout,nullptr);
    return locator_capture&&!locators.faults&&startup_ok&&!startup_copy.overflow&&!startup_copy.output_errors&&state->overflow.load()==0&&state->malformed.load()==0&&state->send_error.load()==0&&state->send_enter.load()==state->send_return.load();
}
}
