#include <mdds/dsoftbus/PublicSocketBackend.hpp>
#include <socket.h>
#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
#include "../probes/PublicSocketRuntimeTrace.hpp"
#define MDDS_PROBE_EVENT(...) ::mdds::dsoftbus::runtime_probe::TraceEvent(__VA_ARGS__)
#else
#define MDDS_PROBE_EVENT(...) ((void)0)
#endif

#ifdef MDDS_PUBLIC_SOCKET_DDS_EXPERIMENT
#include "../probes/PublicSocketDdsTrace.hpp"
#define MDDS_DDS_FRAME(...) ::mdds::dsoftbus::dds_experiment::SocketFrame(__VA_ARGS__)
#else
#define MDDS_DDS_FRAME(...) ((void)0)
#endif

namespace mdds::dsoftbus::phase0 {
namespace detail {
constexpr std::size_t bridge_count=record_slot_capacity*static_cast<std::size_t>(RecordRole::Count);
constexpr std::size_t qos_capacity=8;
struct PublicSocketState {
    explicit PublicSocketState(PublicSocketConfiguration c):config(std::move(c)),dual(!config.listener_identity.service_name.empty()) {
        auto text=[](const std::string& s,std::size_t max,bool empty=false){return (empty||!s.empty())&&s.size()<=max&&s.find('\0')==std::string::npos;};
        auto qos_ok=[](const std::vector<PublicSocketQos>& q){
            if(q.empty()||q.size()>qos_capacity)return false;
            for(std::size_t i=0;i<q.size();++i){if(q[i].type<0||q[i].type>=QOS_TYPE_BUTT)return false;
                for(std::size_t j=0;j<i;++j)if(q[i].type==q[j].type&&q[i].value!=q[j].value)return false;}
            return true;
        };
        valid=config.identity.record.value&&config.identity.owner.value&&text(config.identity.service_name,255)&&text(config.package_name,64)&&config.max_message_size>0&&config.max_message_size<=UINT32_MAX;
        if(config.role==RecordRole::Client)valid=valid&&text(config.peer_name,255)&&text(config.peer_network_id,64)&&qos_ok(config.bind_qos)&&(dual?qos_ok(config.listen_qos):config.listen_qos.empty());
        else if(config.role==RecordRole::Server)valid=valid&&config.peer_name.empty()&&config.peer_network_id.empty()&&qos_ok(config.listen_qos)&&config.bind_qos.empty();
        else valid=false;
        if(dual)valid=valid&&config.role==RecordRole::Client&&config.listener_identity.record.value&&config.listener_identity.owner.value&&
                text(config.listener_identity.service_name,255)&&config.listener_identity.service_name!=config.identity.service_name&&config.listener_identity.record.value!=config.identity.record.value;
        observation.role=config.role;
#ifdef MDDS_PUBLIC_SOCKET_FAKE_SDK
        observation.fake_sdk=true;
#endif
        if(!valid)return;
        auto copy=[](auto& dst,const std::string& s){std::copy(s.begin(),s.end(),dst.begin());dst[s.size()]='\0';};
        copy(name,config.identity.service_name);copy(package,config.package_name);copy(peer,config.peer_name);copy(network,config.peer_network_id);
        info={name.data(),config.role==RecordRole::Client?peer.data():nullptr,config.role==RecordRole::Client?network.data():nullptr,package.data(),DATA_TYPE_BYTES};
        const auto& q=config.role==RecordRole::Client?config.bind_qos:config.listen_qos;
        qos_count=static_cast<std::uint32_t>(q.size());for(std::size_t i=0;i<q.size();++i)qos[i]={static_cast<QosType>(q[i].type),q[i].value};
        if(dual){copy(listener_name,config.listener_identity.service_name);listener_info={listener_name.data(),nullptr,nullptr,package.data(),DATA_TYPE_BYTES};listener_qos_count=static_cast<std::uint32_t>(config.listen_qos.size());for(std::size_t i=0;i<config.listen_qos.size();++i)listener_qos[i]={static_cast<QosType>(config.listen_qos[i].type),config.listen_qos[i].value};}
    }
    const PublicSocketConfiguration config;
    const bool dual;
#ifdef MDDS_PUBLIC_SOCKET_FAKE_SDK
    void(*before_capability)(void*) noexcept=nullptr;
    void* before_capability_context=nullptr;
#endif
    bool listener_attempted=false,primary_closed=false,listener_closed=false,accepted_closed=false;
    bool primary_observed=false,listener_observed=false,accepted_observed=false;
    bool primary_failed=false,listener_failed=false,accepted_failed=false;
    BackendStatus listener_close_status=BackendStatus::None,accepted_close_status=BackendStatus::None;
    std::int32_t listener_socket=-1;
    std::optional<std::int32_t> negotiated_id;
    RecordEntrance listener_entry;
    ProviderCallbacks listener_callbacks;
    NativeOwnerLease accepted_owner; // original table capability, never a second ownership registry
    std::array<char,256> listener_name{};
    SocketInfo listener_info{};
    std::array<QosTV,qos_capacity> listener_qos{};
    std::uint32_t listener_qos_count=0;
    void RefreshClosed(){observation.closed=(primary_closed||primary_observed)&&(!dual||((listener_closed||listener_observed)&&(!accepted_owner||accepted_closed||accepted_observed)));}
    mutable std::mutex gate;
    bool valid=false,attempted=false;
    BackendStatus close_status=BackendStatus::None;
    std::int32_t socket=-1;
    RecordEntrance entry;
    ProviderCallbacks callbacks;
    std::array<char,256> name{},peer{};
    std::array<char,65> package{},network{};
    SocketInfo info{};
    std::array<QosTV,qos_capacity> qos{};
    std::uint32_t qos_count=0;
    PublicSocketObservation observation;
    PublicSocketCall& Op(PublicSocketOperation op){return observation.operations[static_cast<std::size_t>(op)];}
    void Reject(PublicSocketOperation op,BackendStatus status){auto& r=Op(op);++r.attempts;r.last=BackendCallResult{status};}
    void Enter(PublicSocketOperation op){auto& r=Op(op);++r.attempts;++r.calls;r.last={BackendStatus::None,true,false,std::nullopt};r.last_call=r.last;r.handle.reset();}
    BackendCallResult Returned(PublicSocketOperation op,std::optional<std::int32_t> code,std::optional<std::int32_t> handle={}){
        auto& r=Op(op);++r.returns;
        BackendStatus status=code&&*code!=0?BackendStatus::NativeCallFailed:BackendStatus::None;
        if(status==BackendStatus::None&&observation.stopped&&op!=PublicSocketOperation::Shutdown)status=BackendStatus::ProviderStopped;
        r.last={status,true,true,code};r.last_call=r.last;r.handle=handle;
        if(code&&*code!=0&&!observation.first_api_error)observation.first_api_error=PublicSocketError{op,*code};
        return r.last;
    }
    BackendCallResult Exception(PublicSocketOperation op){++observation.exceptions;if(!observation.first_exception)observation.first_exception=op;auto& r=Op(op);r.last={BackendStatus::ProviderException,true,false,std::nullopt};r.last_call=r.last;return r.last;}
    static bool PeerValue(PeerSocketInfo in,BackendPeer& out) {
        const auto copy=[](const char* p,std::size_t limit,std::string& dst){if(!p)return false;std::size_t n=0;while(n<=limit&&p[n])++n;if(!n||n>limit)return false;dst.assign(p,n);return true;};
        if(in.dataType!=DATA_TYPE_BYTES||!copy(in.name,255,out.name)||!copy(in.networkId,64,out.network_id)||!copy(in.pkgName,64,out.package_name))return false;
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
        ::mdds::dsoftbus::runtime_probe::TraceText("validated_peer_name",out.name.c_str());
        ::mdds::dsoftbus::runtime_probe::TraceText("validated_peer_network",out.network_id.c_str());
        ::mdds::dsoftbus::runtime_probe::TraceText("validated_peer_package",out.package_name.c_str());
#endif
        out.data_type=RecordDataType::Bytes;return true;
    }
    void Bytes(RecordRole role,std::int32_t id,const void* data,std::uint32_t size){
        ProviderCallbacks cb;
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(observation.stopped||observation.closed||!data||!size||size>config.max_message_size){++observation.rejected_callbacks;return;}
            if(dual&&role==RecordRole::Server){if(listener_socket<0){++observation.rejected_callbacks;return;}cb=listener_callbacks;}
            else {if(role!=config.role||primary_closed||primary_observed||primary_failed||socket<0||id!=socket){++observation.rejected_callbacks;return;}cb=callbacks;}
        }
        MDDS_DDS_FRAME("SDK_INGRESS",id,static_cast<const std::uint8_t*>(data),size);
        const auto result=cb.OnBytes(id,data,size);
        if(result!=BackendStatus::None){std::lock_guard<std::mutex> lock(gate);++observation.rejected_callbacks;}
    }
    void Stop(RecordRole role,std::int32_t id,std::int32_t value,bool error){
        ProviderCallbacks cb;bool known=false,is_accepted=false;
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(dual&&role==RecordRole::Server){
                cb=listener_callbacks;
                is_accepted=negotiated_id==id||observation.accepted_socket==id;
                known=id==listener_socket||is_accepted;
                if(is_accepted){if(!error)accepted_observed=true;else accepted_failed=true;}
                if(id==listener_socket){if(!error)listener_observed=true;else listener_failed=true;}
            }else if(role==config.role&&socket>=0&&id==socket){known=true;cb=callbacks;if(!error)primary_observed=true;else primary_failed=true;if(!dual)observation.stopped=true;}
            if(!known){++observation.rejected_callbacks;return;}
            if(error){if(!observation.first_callback_error)observation.first_callback_error=value;}
            else if(!observation.shutdown_reason)observation.shutdown_reason=value;
        }
        if(error)(void)cb.OnError(id,value);else (void)cb.OnNativeShutdown(id);
        // The adapter has latched the exact terminal owner before this metadata
        // copy is released; control still holds the original close/drain state.
        if(!error){std::lock_guard<std::mutex> lock(gate);if(is_accepted)accepted_owner={};RefreshClosed();}
    }
    void Bound(RecordRole role,std::int32_t id,PeerSocketInfo peer){
        ProviderCallbacks cb;
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(dual&&role==RecordRole::Server){if(listener_closed||listener_socket<0){++observation.rejected_callbacks;return;}cb=listener_callbacks;}
            else {
                if(observation.closed||socket<0){++observation.rejected_callbacks;return;}
                if(config.role==RecordRole::Server){++observation.unsupported_accepts;observation.stopped=true;cb=callbacks;}
                else {if(id!=socket)++observation.rejected_callbacks;return;}
            }
        }
        if(!dual){(void)cb.OnShutdown(socket);return;}
        BackendPeer value;
        if(!PeerValue(peer,value)||cb.OnBind(id,value)!=BackendStatus::None){std::lock_guard<std::mutex> lock(gate);++observation.rejected_callbacks;}
    }
    bool Negotiate(RecordRole role,std::int32_t id,PeerSocketInfo peer,bool access_info){
        ProviderCallbacks cb;
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(!dual||role!=RecordRole::Server||listener_closed||listener_observed||listener_failed||observation.stopped||listener_socket<0){++observation.negotiations_denied;return false;}cb=listener_callbacks;}
        if(access_info){(void)cb.OnNegotiationRejected(id);std::lock_guard<std::mutex> lock(gate);++observation.negotiations_denied;return false;}
        BackendPeer value;
        if(!PeerValue(peer,value)||cb.OnNegotiate(id,value)!=BackendStatus::None){std::lock_guard<std::mutex> lock(gate);++observation.negotiations_denied;return false;}
        std::lock_guard<std::mutex> lock(gate);negotiated_id=id;return true;
    }

};
struct BridgeSlot {std::mutex gate;RecordEntrance original;std::shared_ptr<PublicSocketState> state;std::atomic<std::uint64_t> exceptions{0};};
std::array<BridgeSlot,bridge_count>& Slots(){
    // Bounded by the existing table/role slots, deliberately never destroyed or
    // rebound. SDK parameter/listener last-use is not proved by Shutdown return.
    static auto* value=new std::array<BridgeSlot,bridge_count>;return *value;
}
template<std::size_t N,class F>void Dispatch(F&& f)noexcept{
    try {std::shared_ptr<PublicSocketState> state;{auto& slot=Slots()[N];std::lock_guard<std::mutex> lock(slot.gate);state=slot.state;}
        if(state)f(*state);
    }catch(...){try{++Slots()[N].exceptions;}catch(...){}}
}
template<std::size_t N>struct Entry {
    static void Bytes(int32_t id,const void* p,uint32_t n)noexcept{MDDS_PROBE_EVENT("OnBytes",N,N%2,id,0,n);Dispatch<N>([&](auto& s){s.Bytes(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,n);});}
    static void Stop(int32_t id,ShutdownReason reason)noexcept{MDDS_PROBE_EVENT("OnShutdown",N,N%2,id,int(reason),0);Dispatch<N>([&](auto& s){s.Stop(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,static_cast<int32_t>(reason),false);});}
    static void Error(int32_t id,int32_t code)noexcept{MDDS_PROBE_EVENT("OnError",N,N%2,id,code,0);Dispatch<N>([&](auto& s){s.Stop(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,code,true);});}
    static void Bound(int32_t id,PeerSocketInfo p)noexcept{MDDS_PROBE_EVENT("OnBind",N,N%2,id,int(p.dataType),0);Dispatch<N>([&](auto& s){s.Bound(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p);});}
    static bool Negotiate(int32_t id,PeerSocketInfo p)noexcept{MDDS_PROBE_EVENT("OnNegotiate",N,N%2,id,int(p.dataType),0);bool accepted=false;Dispatch<N>([&](auto& s){accepted=s.Negotiate(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,false);});MDDS_PROBE_EVENT("OnNegotiate_result",N,N%2,id,accepted,0);return accepted;}
    static bool Negotiate2(int32_t id,PeerSocketInfo p,SocketAccessInfo*,SocketAccessInfo*)noexcept{MDDS_PROBE_EVENT("OnNegotiate2_unsupported",N,N%2,id,int(p.dataType),0);Dispatch<N>([&](auto& s){(void)s.Negotiate(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,true);});return false;}
    static ISocketListener Make(){ISocketListener l{};l.OnBind=Bound;l.OnShutdown=Stop;l.OnBytes=Bytes;l.OnError=Error;l.OnNegotiate=Negotiate;l.OnNegotiate2=Negotiate2;return l;}
};
template<std::size_t... N>auto Listeners(std::index_sequence<N...>){return std::array<ISocketListener,sizeof...(N)>{Entry<N>::Make()...};}
const ISocketListener& Listener(std::size_t index){static const auto value=Listeners(std::make_index_sequence<bridge_count>{});return value[index];}
static_assert(std::is_same<decltype(ISocketListener{}.OnBytes),RecordBytesCallback>::value,"authorized public byte callback type");
static_assert(std::is_same<decltype(::Shutdown(0)),void>::value,"Shutdown is void; never invent rc0");
}
PublicSocketBackend::PublicSocketBackend(PublicSocketConfiguration c):state_(std::make_shared<detail::PublicSocketState>(std::move(c))){}
PublicSocketBackend::~PublicSocketBackend()=default; // explicit owner/control performs shutdown; no hidden SDK operation
BackendKind PublicSocketBackend::Kind()const noexcept {
#ifdef MDDS_PUBLIC_SOCKET_FAKE_SDK
    return BackendKind::HostMock;
#else
    return BackendKind::DSoftBusProvider;
#endif
}
BackendIdentity PublicSocketBackend::Identity()const{return state_->config.identity;}
BackendTopology PublicSocketBackend::Topology()const noexcept{return state_->dual?BackendTopology::ListenerAndOutgoing:BackendTopology::SingleOwner;}
BackendIdentity PublicSocketBackend::ListenerIdentity()const{return state_->config.listener_identity;}
BackendStatus PublicSocketBackend::ConfigureEntrance(const RecordEntrance& entry){
    if(!entry||entry.slot_>=record_slot_capacity||!state_->valid||(!state_->dual&&entry.role()!=state_->config.role))return BackendStatus::InvalidIdentity;
    const bool listening=state_->dual&&entry.role()==RecordRole::Server;
    const auto index=entry.slot_*static_cast<std::size_t>(RecordRole::Count)+static_cast<std::size_t>(entry.role());
    auto& slot=detail::Slots()[index];std::lock_guard<std::mutex> fixed(slot.gate);std::lock_guard<std::mutex> state_lock(state_->gate);
    if(state_->observation.closed||state_->observation.stopped)return BackendStatus::InvalidState;
    auto& original=listening?state_->listener_entry:state_->entry;
    if(original)return original==entry?BackendStatus::None:BackendStatus::DuplicateRecord;
    if(slot.state)return BackendStatus::DuplicateRecord;
    slot.original=entry;slot.state=state_;original=entry;
    if(listening){state_->observation.listener_entry_index=index;state_->observation.listener_configured=true;}
    else{state_->observation.entry_index=index;state_->observation.configured=true;}
    return BackendStatus::None;
}
namespace {
BackendSocketResult CreateSocket(const std::shared_ptr<detail::PublicSocketState>& s,ProviderCallbacks callbacks,bool listening){
    {std::lock_guard<std::mutex> lock(s->gate);
        auto& attempted=listening?s->listener_attempted:s->attempted;
        const bool configured=listening?s->observation.listener_configured:s->observation.configured;
        if(!s->valid||!configured||attempted||s->observation.closed||!callbacks){s->Reject(PublicSocketOperation::Socket,BackendStatus::InvalidState);return {BackendStatus::InvalidState,{}};}
        attempted=true;(listening?s->listener_callbacks:s->callbacks)=std::move(callbacks);s->Enter(PublicSocketOperation::Socket);}
    try{MDDS_PROBE_EVENT("Socket_enter",listening?s->observation.listener_entry_index:s->observation.entry_index,int(listening?RecordRole::Server:s->config.role),-1,0,0);const auto id=::Socket(listening?s->listener_info:s->info);MDDS_PROBE_EVENT("Socket_return",listening?s->observation.listener_entry_index:s->observation.entry_index,int(listening?RecordRole::Server:s->config.role),id,id,0);std::lock_guard<std::mutex> lock(s->gate);
        if(id>=0){(listening?s->listener_socket:s->socket)=id;
            if(listening||s->config.role==RecordRole::Server)s->observation.listener_socket=id;else s->observation.outgoing_socket=id;
            const auto fact=s->Returned(PublicSocketOperation::Socket,{},id);return {fact.status,id,true,true,{}};}
        const auto fact=s->Returned(PublicSocketOperation::Socket,id);return {fact.status,{},true,true,id};
    }catch(...){std::lock_guard<std::mutex> lock(s->gate);auto fact=s->Exception(PublicSocketOperation::Socket);return {fact.status,{},true,false,{}};}
}
}
BackendSocketResult PublicSocketBackend::Create(ProviderCallbacks cb){return CreateSocket(state_,std::move(cb),false);}
BackendSocketResult PublicSocketBackend::CreateListener(ProviderCallbacks cb){if(!state_->dual)return {};return CreateSocket(state_,std::move(cb),true);}
#ifdef MDDS_PUBLIC_SOCKET_FAKE_SDK
void PublicSocketBackend::SetAcceptedCommitHookForTest(void(*fn)(void*) noexcept,void* context) noexcept {
    std::lock_guard<std::mutex> lock(state_->gate);state_->before_capability=fn;state_->before_capability_context=context;
}
#endif
BackendStatus PublicSocketBackend::CommitAcceptedOwner(const RecordEntrance& entry,const NativeOwnerLease& owner){
#ifdef MDDS_PUBLIC_SOCKET_FAKE_SDK
    void(*hook)(void*) noexcept=nullptr;void* context=nullptr;
    {std::lock_guard<std::mutex> lock(state_->gate);hook=state_->before_capability;context=state_->before_capability_context;}
    if(hook)hook(context);
#endif
    const auto facts=owner.Observe();
    std::lock_guard<std::mutex> lock(state_->gate);
    if(!state_->dual||entry!=state_->listener_entry||!facts.owned_id||facts.role!=NativeOwnerRole::Accepted||state_->negotiated_id!=facts.owned_id)return BackendStatus::InvalidIdentity;
    if(state_->accepted_owner&&!(state_->accepted_owner==owner))return BackendStatus::DuplicateRecord;
    state_->observation.accepted_socket=facts.owned_id;
    if(facts.on_shutdown_seen||state_->accepted_observed)return BackendStatus::ProviderStopped;
    state_->accepted_owner=owner; // original opaque lease, including cleanup on a later failure
    return state_->observation.stopped||state_->listener_closed?BackendStatus::ProviderStopped:BackendStatus::None;
}

namespace {
BackendCallResult Setup(const std::shared_ptr<detail::PublicSocketState>& s,std::int32_t id,RecordRole role){
    const auto op=role==RecordRole::Client?PublicSocketOperation::Bind:PublicSocketOperation::Listen;
    const bool listening=s->dual&&role==RecordRole::Server;
    {std::lock_guard<std::mutex> lock(s->gate);
        if(!(listening?s->observation.listener_configured:s->observation.configured)||(!s->dual&&s->config.role!=role)||(listening?s->listener_socket:s->socket)<0||id!=(listening?s->listener_socket:s->socket)||s->observation.stopped||s->observation.closed){s->Reject(op,BackendStatus::InvalidState);return BackendStatus::InvalidState;}
        s->Enter(op);}
    try {const auto& listener=detail::Listener(listening?s->observation.listener_entry_index:s->observation.entry_index);
        MDDS_PROBE_EVENT(role==RecordRole::Client?"Bind_enter":"Listen_enter",listening?s->observation.listener_entry_index:s->observation.entry_index,int(role),id,0,0);
        const auto rc=role==RecordRole::Client ? ::Bind(id,s->qos.data(),s->qos_count,&listener) : ::Listen(id,listening?s->listener_qos.data():s->qos.data(),listening?s->listener_qos_count:s->qos_count,&listener);
        MDDS_PROBE_EVENT(role==RecordRole::Client?"Bind_return":"Listen_return",listening?s->observation.listener_entry_index:s->observation.entry_index,int(role),id,rc,0);
        std::lock_guard<std::mutex> lock(s->gate);return s->Returned(op,rc);
    }catch(...){std::lock_guard<std::mutex> lock(s->gate);return s->Exception(op);}
}
}
BackendCallResult PublicSocketBackend::Listen(std::int32_t id){return Setup(state_,id,RecordRole::Server);}
BackendCallResult PublicSocketBackend::Bind(std::int32_t id){return Setup(state_,id,RecordRole::Client);}
BackendSendResult PublicSocketBackend::Send(std::int32_t id,const std::uint8_t* p,std::size_t size,std::chrono::steady_clock::time_point deadline){
    auto s=state_;
    {std::lock_guard<std::mutex> lock(s->gate);
        if(!p||!size||size>s->config.max_message_size||size>UINT32_MAX){s->Reject(PublicSocketOperation::Send,BackendStatus::InvalidBuffer);return {BackendStatus::InvalidBuffer,{}};}
        if(s->socket<0||id!=s->socket||s->primary_closed||s->primary_observed||s->primary_failed||s->observation.stopped||s->observation.closed||std::chrono::steady_clock::now()>=deadline){s->Reject(PublicSocketOperation::Send,BackendStatus::InvalidState);return {BackendStatus::InvalidState,{}};}
        s->Enter(PublicSocketOperation::Send);}
    try {MDDS_DDS_FRAME("SDK_SEND",id,p,size);MDDS_PROBE_EVENT("SendBytes_enter",s->observation.entry_index,int(RecordRole::Client),id,0,size);const auto rc=::SendBytes(id,p,static_cast<std::uint32_t>(size));MDDS_PROBE_EVENT("SendBytes_return",s->observation.entry_index,int(RecordRole::Client),id,rc,size);std::lock_guard<std::mutex> lock(s->gate);s->Returned(PublicSocketOperation::Send,rc);
        // NativeSendExecution must see even negative or unexpected positive raw results.
        return {BackendStatus::None,rc,true,true};
    }catch(...){std::lock_guard<std::mutex> lock(s->gate);s->Exception(PublicSocketOperation::Send);return {BackendStatus::ProviderException,{},true,false};}
}
BackendCallResult PublicSocketBackend::Shutdown(std::int32_t id){
    auto s=state_;int lane=-1;
    {std::lock_guard<std::mutex> lock(s->gate);
        if(s->socket>=0&&id==s->socket)lane=0;
        else if(s->dual&&s->listener_socket>=0&&id==s->listener_socket)lane=1;
        else if(s->dual&&s->observation.accepted_socket==id&&(s->accepted_owner||s->accepted_closed||s->accepted_observed))lane=2;
        if(lane<0){s->Reject(PublicSocketOperation::Shutdown,BackendStatus::ForeignSocket);return BackendStatus::ForeignSocket;}
        auto& closed=lane==0?s->primary_closed:lane==1?s->listener_closed:s->accepted_closed;
        const auto observed=lane==0?s->primary_observed:lane==1?s->listener_observed:s->accepted_observed;
        const auto previous=lane==0?s->close_status:lane==1?s->listener_close_status:s->accepted_close_status;
        if(closed||observed){closed=true;s->RefreshClosed();s->Reject(PublicSocketOperation::Shutdown,previous);return previous;}
        closed=true;if(lane==0&&!s->dual)s->observation.stopped=true;s->RefreshClosed();s->Enter(PublicSocketOperation::Shutdown);}
    BackendCallResult result;
    try{MDDS_PROBE_EVENT("Shutdown_enter",lane? s->observation.listener_entry_index:s->observation.entry_index,int(lane?RecordRole::Server:RecordRole::Client),id,0,0);::Shutdown(id);MDDS_PROBE_EVENT("Shutdown_void_return",lane?s->observation.listener_entry_index:s->observation.entry_index,int(lane?RecordRole::Server:RecordRole::Client),id,0,0);std::lock_guard<std::mutex> lock(s->gate);result=s->Returned(PublicSocketOperation::Shutdown,{});}
    catch(...){std::lock_guard<std::mutex> lock(s->gate);result=s->Exception(PublicSocketOperation::Shutdown);}
    {std::lock_guard<std::mutex> lock(s->gate);(lane==0?s->close_status:lane==1?s->listener_close_status:s->accepted_close_status)=result.status;
        if(lane==2)s->accepted_owner={};s->RefreshClosed();}
    return result;
}
PublicSocketObservation PublicSocketBackend::Observe()const{
    std::lock_guard<std::mutex> lock(state_->gate);auto result=state_->observation;
    result.outgoing_stopped=state_->primary_closed||state_->primary_observed||state_->primary_failed;
    result.listener_stopped=state_->listener_closed||state_->listener_observed||state_->listener_failed;
    result.accepted_stopped=state_->accepted_closed||state_->accepted_observed||state_->accepted_failed;
    if(result.configured)result.exceptions+=detail::Slots()[result.entry_index].exceptions.load();
    if(result.listener_configured)result.exceptions+=detail::Slots()[result.listener_entry_index].exceptions.load();
    return result;
}
}
