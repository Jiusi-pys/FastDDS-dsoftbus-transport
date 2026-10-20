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

namespace {
constexpr std::size_t capacity = 8192;
struct Frame { std::uint64_t observation; bool sending; std::int32_t socket;
    std::array<std::uint8_t,16> writer; std::int32_t high; std::uint32_t low; };
struct ObserverState {
    std::array<Frame,capacity> frames{};
    std::size_t count=0;
    std::mutex mutex;
    fastdds_dsoftbus::ros2_target_observation::V1LocatorSnapshot v1_locators;
    fastdds_dsoftbus::ros2_target_observation::EndpointSnapshot endpoints;
    fastdds_dsoftbus::ros2_target_observation::SubmissionSnapshot submissions;
    std::string run;
    unsigned side=0;
    std::atomic<std::uint64_t> observations{0},send_enter{0},send_return{0},send_error{0},ingress{0},overflow{0},malformed{0};
    std::mutex startup_mutex;
    fastdds_dsoftbus::ros2_diagnostics::Snapshot startup;
    std::atomic<std::uint64_t> capture_faults{0},exported_failure{0},output_errors{0};
    bool cleanup_started=false;
};
// Begin publishes one fully initialized, fixed-capacity metadata store. It owns
// no Runtime, payload, receiver, SDK handle or thread. A failed public deletion
// may retain the pump/callback module until process exit, so this store must not
// register a static destructor. Callback paths never allocate or initialize it.
std::atomic<ObserverState*> published{nullptr};
std::atomic<bool> begin_claimed{false};
std::atomic<std::uint64_t> prebegin_faults{0};
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
            if(length!=50||!borrowed||borrowed[8]!=':'||borrowed[17]!=':'){output.RecordFault(ros2_target_observation::V1LocatorSnapshot::Fault::InvalidSelection);return;}
            if(output.selected_count==output.selected.size()){output.RecordFault(ros2_target_observation::V1LocatorSnapshot::Fault::SelectionCapacity);return;}
            auto nibble=[](char ch)->int {return ch>='0'&&ch<='9'?ch-'0':ch>='a'&&ch<='f'?ch-'a'+10:-1;};
            ros2_target_observation::V1LocatorSnapshot::Selected value;
            for(unsigned i=0;i<50;++i)if(i!=8&&i!=17&&nibble(borrowed[i])<0){output.RecordFault(ros2_target_observation::V1LocatorSnapshot::Fault::InvalidSelection);return;}
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
    state->v1_locators.RecordAdverts(observation,sending,socket,locators);
    for(const auto& item:decoded.data) {
        if(state->count==capacity){++state->overflow;continue;}
        state->frames[state->count++]={observation,sending,socket,item.writer_guid,item.sequence_hi,item.sequence_lo};
    }
    return observation;
}
}
extern "C" void FastddsDsoftbusObserveSenderSubmission(std::uint64_t sender,const void* data,std::size_t bytes,int status,bool route_known,bool route_available,std::uint64_t route_revision,bool queue_admitted) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return;
    if(!data||!bytes){std::lock_guard<std::mutex> lock(state->mutex);++state->submissions.uncorrelated;return;}
    const auto packet=fastdds_dsoftbus::experiment::ObserveRtps(data,bytes);
    std::lock_guard<std::mutex> lock(state->mutex);
    auto& ledger=state->submissions;
    if(!packet.valid){++ledger.uncorrelated;return;}
    for(const auto& frame:packet.data){
        const auto observation=++state->observations;
        auto found=std::find_if(ledger.values.begin(),ledger.values.begin()+ledger.count,[&](const auto& item){return item.sender==sender&&item.status==status&&item.writer==frame.writer_guid&&item.high==frame.sequence_hi&&item.low==frame.sequence_lo&&item.route_known==route_known&&item.route_available==route_available&&item.route_revision==route_revision&&item.queue_admitted==queue_admitted;});
        if(found!=ledger.values.begin()+ledger.count){++found->attempts;found->last_observation=observation;continue;}
        if(ledger.count==ledger.values.size()){++ledger.overflow;continue;}
        ledger.values[ledger.count++]={observation,observation,1,sender,frame.writer_guid,frame.sequence_hi,status,frame.sequence_lo,route_known,route_available,route_revision,queue_admitted};
    }
}
namespace fastdds_dsoftbus::ros2_target_observation {
bool CaptureSubmissions(SubmissionSnapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    std::lock_guard<std::mutex> lock(state->mutex);output=state->submissions;return true;
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
bool Capture(ros2_diagnostics::Snapshot& output) noexcept {
    auto* state=published.load(std::memory_order_acquire);if(!state)return false;
    try{std::unique_lock<std::mutex> lock(state->startup_mutex,std::try_to_lock);if(!lock.owns_lock())return false;output=state->startup;output.overflow+=state->capture_faults.load()+prebegin_faults.load();output.output_errors=state->output_errors.load();return output.initialized;}catch(...){++state->capture_faults;return false;}
}
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
        output.total_stored=state->count;output.stored=(std::min)(state->count,ros2_diagnostics::prefix_capacity);
        output.first_index=state->count-output.stored;
        for(std::size_t i=0;i<output.stored;++i){const auto& frame=state->frames[output.first_index+i];
            output.frames[i]={frame.observation,frame.sending,frame.socket,frame.writer,frame.high,frame.low};}
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
        std::fprintf(stdout,"ROS_V1_SPDP run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_high=%u sequence_low=%u data_offset=%zu pid=%04x bytes=%u kind=%08x address=%s port=%08x occurrences=%llu last_observation=%llu last_sequence_high=%u last_sequence_low=%u batch_size=%zu\n",state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(frame.observation),frame.sending?"SDK_SEND":"SDK_INGRESS",frame.socket,writer,frame.value.sequence_high,frame.value.sequence_low,frame.value.data_submessage_offset,unsigned(frame.value.pid),unsigned(frame.value.serialized_bytes),frame.value.kind,address,frame.value.port,static_cast<unsigned long long>(frame.occurrences),static_cast<unsigned long long>(frame.last_observation),frame.last_sequence_high,frame.last_sequence_low,frame.batch_size);
    }
    for(std::size_t i=0;i<locators.selected_count;++i) {
        const auto& value=locators.selected[i];char address[33]{};for(unsigned j=0;j<16;++j)std::snprintf(address+2*j,3,"%02x",value.address[j]);
        std::fprintf(stdout,"ROS_V1_SELECTED run=%s side=%c kind=%08x address=%s port=%08x scope=ACTUAL_SELECTOR_VALUES\n",state->run.c_str(),state->side?'B':'A',value.kind,address,value.port);
    }
    std::fprintf(stdout,"ROS_V1_OBSERVATION run=%s side=%c captured=%u adverts=%zu selected=%zu faults=%llu sdk_epoch=NOT_PROVEN\n",state->run.c_str(),state->side?'B':'A',unsigned(locator_capture),locators.advert_count,locators.selected_count,static_cast<unsigned long long>(locators.faults));
    std::fprintf(stdout,"ROS_V1_AGGREGATION schema=1 run=%s side=%c mode=EXACT_ORDERED_BATCH_IDENTITY slots=%zu observed=%llu repeated=%llu repeated_batches=%llu invalid_frame=%llu unsupported_frame=%llu parser_capacity=%llu advert_capacity=%llu invalid_selection=%llu selection_capacity=%llu counter_capacity=%llu\n",
        state->run.c_str(),state->side?'B':'A',locators.adverts.size(),
        static_cast<unsigned long long>(locators.adverts_observed),static_cast<unsigned long long>(locators.adverts_repeated),static_cast<unsigned long long>(locators.batches_repeated),
        static_cast<unsigned long long>(locators.fault_counts[0]),static_cast<unsigned long long>(locators.fault_counts[1]),static_cast<unsigned long long>(locators.fault_counts[2]),
        static_cast<unsigned long long>(locators.fault_counts[3]),static_cast<unsigned long long>(locators.fault_counts[4]),static_cast<unsigned long long>(locators.fault_counts[5]),static_cast<unsigned long long>(locators.fault_counts[6]));
    for(std::size_t i=0;i<stored;++i) {
        char guid[33]{};for(std::size_t j=0;j<16;++j)std::snprintf(guid+2*j,3,"%02x",snapshot[i].writer[j]);
        std::fprintf(stdout,"ROS_NATIVE_DATA run=%s side=%c observation=%llu direction=%s socket=%d writer=%s sequence_hi=%d sequence_lo=%u scope=STRUCTURAL_APP_OBSERVER\n",
            state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(snapshot[i].observation),snapshot[i].sending?"SDK_SEND":"SDK_INGRESS",snapshot[i].socket,guid,snapshot[i].high,snapshot[i].low);
    }
    std::fprintf(stdout,"ROS_NATIVE_SUMMARY run=%s side=%c send_enter=%llu send_return=%llu send_error=%llu ingress=%llu detailed_stored=%zu metadata_limit=%zu overflow=%llu malformed=%llu sdk_epoch=NOT_PROVEN\n",
        state->run.c_str(),state->side?'B':'A',static_cast<unsigned long long>(state->send_enter.load()),static_cast<unsigned long long>(state->send_return.load()),static_cast<unsigned long long>(state->send_error.load()),static_cast<unsigned long long>(state->ingress.load()),stored,capacity,static_cast<unsigned long long>(state->overflow.load()),static_cast<unsigned long long>(state->malformed.load()));
    const bool native_output_ok=std::fflush(stdout)==0&&std::ferror(stdout)==0;
    ros2_diagnostics::Snapshot startup_copy;
    const bool startup_ok=Capture(startup_copy)&&ros2_diagnostics::Write(startup_copy,ros2_diagnostics::Stdout,nullptr);
    return locator_capture&&!locators.faults&&startup_ok&&native_output_ok&&!startup_copy.overflow&&!startup_copy.output_errors&&state->overflow.load()==0&&state->malformed.load()==0&&state->send_error.load()==0&&state->send_enter.load()==state->send_return.load();
}
}
