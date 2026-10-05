#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <socket.h>
#include <softbus_bus_center.h>
#include <fastdds_dsoftbus/dsoftbus/NativeOwnerTable.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
#include "../probes/PublicSocketRuntimeTrace.hpp"
#define FASTDDS_DSOFTBUS_PROBE_EVENT(...) ::fastdds_dsoftbus::dsoftbus::runtime_probe::TraceEvent(__VA_ARGS__)
#else
#define FASTDDS_DSOFTBUS_PROBE_EVENT(...) ((void)0)
#endif

#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
#include "../probes/PublicSocketDdsTrace.hpp"
#define FASTDDS_DSOFTBUS_DDS_FRAME(...) ::fastdds_dsoftbus::dsoftbus::dds_experiment::SocketFrame(__VA_ARGS__)
#else
#define FASTDDS_DSOFTBUS_DDS_FRAME(...) ((void)0)
#endif

namespace fastdds_dsoftbus::dsoftbus::phase0 {
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
#if defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK) || defined(FASTDDS_DSOFTBUS_PUBLIC_SOCKET_HOST_C_SEAM)
        observation.fake_sdk=true;
#endif
        if(config.bounded_reaccept_host&&(!observation.fake_sdk||!dual))valid=false;
        if(config.route_lifecycle&&(!dual||!(config.bounded_reaccept_host||config.bounded_native_reaccept_experiment)))valid=false;
        if(config.bounded_native_reaccept_experiment){
#ifdef FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT
            valid=valid&&dual&&!observation.fake_sdk&&!config.bounded_reaccept_host;
#else
            valid=false;
#endif
        }
        if(!valid)return;
        auto copy=[](auto& dst,const std::string& s){std::copy(s.begin(),s.end(),dst.begin());dst[s.size()]='\0';};
        copy(name,config.identity.service_name);copy(package,config.package_name);copy(peer,config.peer_name);copy(network,config.peer_network_id);
        info={name.data(),config.role==RecordRole::Client?peer.data():nullptr,config.role==RecordRole::Client?network.data():nullptr,package.data(),DATA_TYPE_BYTES};
        const auto& q=config.role==RecordRole::Client?config.bind_qos:config.listen_qos;
        qos_count=static_cast<std::uint32_t>(q.size());for(std::size_t i=0;i<q.size();++i)qos[i]={static_cast<QosType>(q[i].type),q[i].value};
        if(dual){copy(listener_name,config.listener_identity.service_name);listener_info={listener_name.data(),nullptr,nullptr,package.data(),DATA_TYPE_BYTES};listener_qos_count=static_cast<std::uint32_t>(config.listen_qos.size());for(std::size_t i=0;i<config.listen_qos.size();++i)listener_qos[i]={static_cast<QosType>(config.listen_qos[i].type),config.listen_qos[i].value};}
    }
    const PublicSocketConfiguration config;
    std::shared_ptr<const std::function<void(bool,std::string_view)>> node_sink;
    bool node_subscribed=false,node_sealed=false,node_registering=false;
    BackendStatus node_cleanup_status=BackendStatus::None;
    std::shared_ptr<const std::array<char,65>> peer_snapshot;
    const bool dual;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK
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
    std::array<unsigned,2> native_resets{};
    bool BoundedRequested()const noexcept {return config.bounded_reaccept_host||config.bounded_native_reaccept_experiment;}
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
    void Reject(PublicSocketOperation op,BackendStatus status){FASTDDS_DSOFTBUS_PROBE_EVENT("BackendReject_BackendStatus_result",UINT64_MAX,-1,-1,int(status),static_cast<unsigned>(op));auto& r=Op(op);++r.attempts;r.last=BackendCallResult{status};}
    void Enter(PublicSocketOperation op){auto& r=Op(op);++r.attempts;++r.calls;r.last={BackendStatus::None,true,false,std::nullopt};r.last_call=r.last;r.handle.reset();}
    BackendCallResult Returned(PublicSocketOperation op,std::optional<std::int32_t> code,std::optional<std::int32_t> handle={}){
        auto& r=Op(op);++r.returns;
        BackendStatus status=code&&*code!=0?BackendStatus::NativeCallFailed:BackendStatus::None;
        if(status==BackendStatus::None&&observation.stopped&&op!=PublicSocketOperation::Shutdown)status=BackendStatus::ProviderStopped;
        r.last={status,true,true,code};r.last_call=r.last;r.handle=handle;
        if(code&&*code!=0&&!observation.first_api_error)observation.first_api_error=PublicSocketError{op,*code};
        return r.last;
    }
    BackendCallResult Exception(PublicSocketOperation op){FASTDDS_DSOFTBUS_PROBE_EVENT("SdkException_BackendStatus_result",UINT64_MAX,-1,-1,int(BackendStatus::ProviderException),static_cast<unsigned>(op));++observation.exceptions;if(!observation.first_exception)observation.first_exception=op;auto& r=Op(op);r.last={BackendStatus::ProviderException,true,false,std::nullopt};r.last_call=r.last;return r.last;}
    static bool PeerValue(PeerSocketInfo in,BackendPeer& out) {
        const auto copy=[](const char* p,std::size_t limit,std::string& dst){if(!p)return false;std::size_t n=0;while(n<=limit&&p[n])++n;if(!n||n>limit)return false;dst.assign(p,n);return true;};
        if(in.dataType!=DATA_TYPE_BYTES||!copy(in.name,255,out.name)||!copy(in.networkId,64,out.network_id)||!copy(in.pkgName,64,out.package_name))return false;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
::fastdds_dsoftbus::dsoftbus::runtime_probe::TraceText("validated_peer_name",out.name.c_str());
::fastdds_dsoftbus::dsoftbus::runtime_probe::TraceText("validated_peer_network",out.network_id.c_str());
::fastdds_dsoftbus::dsoftbus::runtime_probe::TraceText("validated_peer_package",out.package_name.c_str());
#endif
        out.data_type=RecordDataType::Bytes;return true;
    }
    void Bytes(RecordRole role,std::int32_t id,const void* data,std::uint32_t size){
        ProviderCallbacks cb;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_BOUNDED_REGRESSION
        bool experiment_accepted=false;
#endif
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(observation.stopped||observation.closed||!data||!size||size>config.max_message_size){++observation.rejected_callbacks;return;}
            if(dual&&role==RecordRole::Server){if(listener_socket<0){++observation.rejected_callbacks;return;}cb=listener_callbacks;}
            else {if(role!=config.role||primary_closed||primary_observed||primary_failed||socket<0||id!=socket){++observation.rejected_callbacks;return;}cb=callbacks;}
        }
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_BOUNDED_REGRESSION
        {std::lock_guard<std::mutex> lock(gate);experiment_accepted=dual&&role==RecordRole::Server&&observation.accepted_socket==id&&!accepted_closed&&!accepted_observed&&!accepted_failed;}
#endif
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_BOUNDED_REGRESSION
const auto ingress_observation=::fastdds_dsoftbus::dsoftbus::dds_experiment::SocketFrame("SDK_INGRESS",id,static_cast<const std::uint8_t*>(data),size);
::fastdds_dsoftbus::dsoftbus::dds_experiment::IngressObservationScope scope(ingress_observation);
if(experiment_accepted&&::fastdds_dsoftbus::dsoftbus::dds_experiment::DropRegressionRecord(id,static_cast<const std::uint8_t*>(data),size))return;
#else
        FASTDDS_DSOFTBUS_DDS_FRAME("SDK_INGRESS",id,static_cast<const std::uint8_t*>(data),size);
#endif
        const auto result=cb.OnBytes(id,data,size);
        if(result!=BackendStatus::None){FASTDDS_DSOFTBUS_PROBE_EVENT("OnBytesRejected",0,int(role),id,int(result),size);std::lock_guard<std::mutex> lock(gate);++observation.rejected_callbacks;}
    }
    void Stop(RecordRole role,std::int32_t id,std::int32_t value,bool error){
        ProviderCallbacks cb;bool known=false,is_accepted=false,ambiguous_first=false;
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(dual&&role==RecordRole::Server){
                cb=listener_callbacks;
                is_accepted=negotiated_id==id||observation.accepted_socket==id;
                known=id==listener_socket||is_accepted;
                if(is_accepted){if(!error)accepted_observed=true;else accepted_failed=true;}
                if(id==listener_socket){if(!error)listener_observed=true;else listener_failed=true;}
            }else if(role==config.role&&socket>=0&&id==socket){known=true;cb=callbacks;if(!error)primary_observed=true;else primary_failed=true;if(!dual)observation.stopped=true;}
            if(!known){++observation.rejected_callbacks;
                ambiguous_first=BoundedRequested()&&role==config.role&&attempted&&socket<0;
                if(!ambiguous_first)return;cb=callbacks;}
            if(error){if(!observation.first_callback_error)observation.first_callback_error=value;}
            else if(!observation.shutdown_reason)observation.shutdown_reason=value;
        }
        if(ambiguous_first){(void)cb.OnNativeShutdown(id);return;}
        if(error)(void)cb.OnError(id,value);else (void)cb.OnNativeShutdown(id);
        // The adapter has latched the exact terminal owner before this metadata
        // copy is released; control still holds the original close/drain state.
        if(!error){std::lock_guard<std::mutex> lock(gate);if(is_accepted&&!BoundedRequested())accepted_owner={};RefreshClosed();}
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
        if(!PeerValue(peer,value)||cb.OnBind(id,value)!=BackendStatus::None){if(BoundedRequested())(void)cb.OnNegotiationRejected(id);std::lock_guard<std::mutex> lock(gate);++observation.rejected_callbacks;}
    }
    bool Negotiate(RecordRole role,std::int32_t id,PeerSocketInfo peer,bool access_info){
        ProviderCallbacks cb;
        {std::lock_guard<std::mutex> lock(gate);if(role==RecordRole::Server)++observation.negotiation_callbacks;}
        {std::lock_guard<std::mutex> lock(gate);++observation.callback_entries;
            if(!dual||role!=RecordRole::Server||listener_closed||listener_observed||listener_failed||observation.stopped||listener_socket<0){++observation.negotiations_denied;return false;}cb=listener_callbacks;}
        if(access_info){(void)cb.OnNegotiationRejected(id);std::lock_guard<std::mutex> lock(gate);++observation.negotiations_denied;return false;}
        BackendPeer value;
        if(!PeerValue(peer,value)||cb.OnNegotiate(id,value)!=BackendStatus::None){if(BoundedRequested())(void)cb.OnNegotiationRejected(id);std::lock_guard<std::mutex> lock(gate);++observation.negotiations_denied;return false;}
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
    static void Node(bool online,NodeBasicInfo* info) noexcept {
        if(!info)return;
        const auto* end=static_cast<const char*>(std::memchr(info->networkId,0,sizeof(info->networkId)));
        if(!end||end==info->networkId||end-info->networkId>64)return;
        Dispatch<N>([&](auto& s){std::shared_ptr<const std::function<void(bool,std::string_view)>> sink;
            {std::lock_guard<std::mutex> lock(s.gate);if(s.node_sealed||!s.node_sink)return;sink=s.node_sink;}
            (*sink)(online,std::string_view(info->networkId,static_cast<std::size_t>(end-info->networkId)));});
    }
    static void Online(NodeBasicInfo* info)noexcept{Node(true,info);}
    static void Offline(NodeBasicInfo* info)noexcept{Node(false,info);}
    static INodeStateCb MakeNode(){INodeStateCb c{};c.events=EVENT_NODE_STATE_ONLINE|EVENT_NODE_STATE_OFFLINE;c.onNodeOnline=Online;c.onNodeOffline=Offline;return c;}
    static void Bytes(int32_t id,const void* p,uint32_t n)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnBytes",N,N%2,id,0,n);Dispatch<N>([&](auto& s){s.Bytes(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,n);});}
    static void Stop(int32_t id,ShutdownReason reason)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnShutdown",N,N%2,id,int(reason),0);Dispatch<N>([&](auto& s){s.Stop(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,static_cast<int32_t>(reason),false);});}
    static void Error(int32_t id,int32_t code)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnError",N,N%2,id,code,0);Dispatch<N>([&](auto& s){s.Stop(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,code,true);});}
    static void Bound(int32_t id,PeerSocketInfo p)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnBind",N,N%2,id,int(p.dataType),0);Dispatch<N>([&](auto& s){s.Bound(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p);});}
    static bool Negotiate(int32_t id,PeerSocketInfo p)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnNegotiate",N,N%2,id,int(p.dataType),0);bool accepted=false;Dispatch<N>([&](auto& s){accepted=s.Negotiate(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,false);});FASTDDS_DSOFTBUS_PROBE_EVENT("OnNegotiate_result",N,N%2,id,accepted,0);return accepted;}
    static bool Negotiate2(int32_t id,PeerSocketInfo p,SocketAccessInfo*,SocketAccessInfo*)noexcept{FASTDDS_DSOFTBUS_PROBE_EVENT("OnNegotiate2_unsupported",N,N%2,id,int(p.dataType),0);Dispatch<N>([&](auto& s){(void)s.Negotiate(static_cast<RecordRole>(N%static_cast<std::size_t>(RecordRole::Count)),id,p,true);});return false;}
    static ISocketListener Make(){ISocketListener l{};l.OnBind=Bound;l.OnShutdown=Stop;l.OnBytes=Bytes;l.OnError=Error;l.OnNegotiate=Negotiate;l.OnNegotiate2=Negotiate2;return l;}
};
template<std::size_t... N>auto Listeners(std::index_sequence<N...>){return std::array<ISocketListener,sizeof...(N)>{Entry<N>::Make()...};}
const ISocketListener& Listener(std::size_t index){static const auto value=Listeners(std::make_index_sequence<bridge_count>{});return value[index];}
template<std::size_t... N>auto NodeListeners(std::index_sequence<N...>){return std::array<INodeStateCb,sizeof...(N)>{Entry<N>::MakeNode()...};}
INodeStateCb& NodeListener(std::size_t index){static auto value=NodeListeners(std::make_index_sequence<bridge_count>{});return value[index];}
static_assert(std::is_same<decltype(ISocketListener{}.OnBytes),RecordBytesCallback>::value,"authorized public byte callback type");
static_assert(std::is_same<decltype(::Shutdown(0)),void>::value,"Shutdown is void; never invent rc0");
}
PublicSocketBackend::PublicSocketBackend(PublicSocketConfiguration c):state_(std::make_shared<detail::PublicSocketState>(std::move(c))){}
PublicSocketBackend::~PublicSocketBackend()=default; // explicit owner/control performs shutdown; no hidden SDK operation
BackendKind PublicSocketBackend::Kind()const noexcept {
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK
    return BackendKind::HostMock;
#else
    return BackendKind::DSoftBusProvider;
#endif
}
BackendIdentity PublicSocketBackend::Identity()const{return state_->config.identity;}
BackendTopology PublicSocketBackend::Topology()const noexcept{return state_->dual?BackendTopology::ListenerAndOutgoing:BackendTopology::SingleOwner;}
std::size_t PublicSocketBackend::MaxMessageSize()const noexcept{return state_->config.max_message_size;}
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
    SocketInfo info{};std::shared_ptr<const std::array<char,65>> peer_snapshot;
    {std::lock_guard<std::mutex> lock(s->gate);
        auto& attempted=listening?s->listener_attempted:s->attempted;
        const bool configured=listening?s->observation.listener_configured:s->observation.configured;
        if(!s->valid||!configured||attempted||s->observation.closed||!callbacks){s->Reject(PublicSocketOperation::Socket,BackendStatus::InvalidState);return {BackendStatus::InvalidState,{}};}
        attempted=true;(listening?s->listener_callbacks:s->callbacks)=std::move(callbacks);s->Enter(PublicSocketOperation::Socket);
        info=listening?s->listener_info:s->info;peer_snapshot=s->peer_snapshot;if(!listening&&peer_snapshot)info.peerNetworkId=const_cast<char*>(peer_snapshot->data());}
    try{FASTDDS_DSOFTBUS_PROBE_EVENT("Socket_enter",listening?s->observation.listener_entry_index:s->observation.entry_index,int(listening?RecordRole::Server:s->config.role),-1,0,0);const auto id=::Socket(info);FASTDDS_DSOFTBUS_PROBE_EVENT("Socket_return",listening?s->observation.listener_entry_index:s->observation.entry_index,int(listening?RecordRole::Server:s->config.role),id,id,0);std::lock_guard<std::mutex> lock(s->gate);
        if(id>=0){(listening?s->listener_socket:s->socket)=id;
            if(listening||s->config.role==RecordRole::Server)s->observation.listener_socket=id;else s->observation.outgoing_socket=id;
            const auto fact=s->Returned(PublicSocketOperation::Socket,{},id);return {fact.status,id,true,true,{}};}
        const auto fact=s->Returned(PublicSocketOperation::Socket,id);return {fact.status,{},true,true,id};
    }catch(...){std::lock_guard<std::mutex> lock(s->gate);auto fact=s->Exception(PublicSocketOperation::Socket);return {fact.status,{},true,false,{}};}
}
}
BackendSocketResult PublicSocketBackend::Create(ProviderCallbacks cb){return CreateSocket(state_,std::move(cb),false);}
bool PublicSocketBackend::RouteLifecycleEnabled()const noexcept{return state_->valid&&state_->config.route_lifecycle&&BoundedReacceptExperiment();}
BackendStatus PublicSocketBackend::SubscribeNodes(std::function<void(bool,std::string_view)> sink){
    if(!RouteLifecycleEnabled()||!sink)return BackendStatus::InvalidState;
    std::size_t index;
    {std::lock_guard<std::mutex> lock(state_->gate);if(state_->node_subscribed||state_->node_sealed||state_->node_registering)return BackendStatus::InvalidState;
        index=state_->observation.entry_index;if(index>=detail::bridge_count)return BackendStatus::InvalidState;state_->node_sink=std::make_shared<const std::function<void(bool,std::string_view)>>(std::move(sink));state_->node_registering=true;}
    const auto code=::RegNodeDeviceStateCb(state_->package.data(),&detail::NodeListener(index));
    {std::lock_guard<std::mutex> lock(state_->gate);state_->node_registering=false;
        if(code!=0){state_->node_sink={};state_->node_sealed=true;return BackendStatus::CallbackFailure;}
        if(!state_->node_sealed){state_->node_subscribed=true;return BackendStatus::None;}}
    // Close may seal during Reg. Reconcile the successful original registration
    // before Open finishes; never publish it into a closed runtime.
    const auto cleanup=::UnregNodeDeviceStateCb(&detail::NodeListener(index));
    if(cleanup!=0){std::lock_guard<std::mutex> lock(state_->gate);state_->node_cleanup_status=BackendStatus::CallbackFailure;return BackendStatus::CallbackFailure;}
    return BackendStatus::ProviderStopped;
}
BackendStatus PublicSocketBackend::UnsubscribeNodes(){
    std::size_t index;
    {std::lock_guard<std::mutex> lock(state_->gate);state_->node_sealed=true;state_->node_sink={};
        if(!state_->node_subscribed)return state_->node_cleanup_status;state_->node_subscribed=false;index=state_->observation.entry_index;}
    const auto cleanup=::UnregNodeDeviceStateCb(&detail::NodeListener(index));
    std::lock_guard<std::mutex> lock(state_->gate);if(cleanup!=0)state_->node_cleanup_status=BackendStatus::CallbackFailure;
    return state_->node_cleanup_status;
}
BackendStatus PublicSocketBackend::ConfigurePeerRoute(const Route& route){
    if(!RouteLifecycleEnabled())return BackendStatus::None;
    if(route.route_version==0||route.fields.remote_network_id.empty()||route.fields.remote_network_id.size()>64||
       route.fields.remote_network_id.find('\0')!=std::string::npos||route.fields.remote_listen_name!=state_->config.peer_name||
       route.fields.local_initiator_name!=state_->config.identity.service_name)return BackendStatus::InvalidIdentity;
    auto buffer=std::make_shared<std::array<char,65>>();std::copy(route.fields.remote_network_id.begin(),route.fields.remote_network_id.end(),buffer->begin());
    std::lock_guard<std::mutex> lock(state_->gate);if(state_->attempted||state_->observation.closed)return BackendStatus::InvalidState;
    state_->peer_snapshot=std::move(buffer);return BackendStatus::None;
}
v1::RouteResult PublicSocketBackend::ResolveNode(const v1::DeviceIdentity& expected,const std::string&){
    if(!RouteLifecycleEnabled())return {};
    NodeBasicInfo* nodes=nullptr;int32_t count=0;
    if(::GetAllNodeDeviceInfo(state_->package.data(),&nodes,&count)!=0)return {};
    struct Release{NodeBasicInfo* p;~Release(){if(p)::FreeNodeInfo(p);}} release{nodes};
    if(count<0||count>32||(count&&!nodes))return {};
    v1::RouteResult result;
    for(int32_t i=0;i<count;++i){
        const auto* end=static_cast<const char*>(std::memchr(nodes[i].networkId,0,sizeof(nodes[i].networkId)));
        if(!end||end==nodes[i].networkId||end-nodes[i].networkId>64)continue;
        std::array<uint8_t,UDID_BUF_LEN> identity{};
        if(::GetNodeKeyInfo(state_->package.data(),nodes[i].networkId,NODE_KEY_UDID,identity.data(),identity.size())!=0)continue;
        const auto* finish=static_cast<const uint8_t*>(std::memchr(identity.data(),0,identity.size()));
        if(!finish||std::string(reinterpret_cast<const char*>(identity.data()),reinterpret_cast<const char*>(finish))!=expected.udid)continue;
        if(result.route)return {v1::DirectoryStatus::Collision,{}};
        result={v1::DirectoryStatus::Ok,v1::DeviceRoute{expected,std::string(nodes[i].networkId,static_cast<std::size_t>(end-nodes[i].networkId))}};
    }
    return result;
}
bool PublicSocketBackend::BoundedReacceptHost()const noexcept {
    return state_->valid&&state_->dual&&state_->observation.fake_sdk&&state_->config.bounded_reaccept_host;
}
bool PublicSocketBackend::BoundedReacceptExperiment()const noexcept {
    if(BoundedReacceptHost())return true;
#ifdef FASTDDS_DSOFTBUS_BOUNDED_NATIVE_REACCEPT_EXPERIMENT
    return state_->valid&&state_->dual&&!state_->observation.fake_sdk&&state_->config.bounded_native_reaccept_experiment;
#else
    return false;
#endif
}
BackendStatus PublicSocketBackend::ResetHostConnection(RecordRole role,const NativeOwnerLease& old){
    if(!BoundedReacceptHost())return BackendStatus::InvalidState;
    return ResetBoundedConnection(role,old);
}
BackendStatus PublicSocketBackend::ResetBoundedConnection(RecordRole role,const NativeOwnerLease& old){
    if(!BoundedReacceptExperiment()||!old.AdapterSettled())return BackendStatus::InvalidState;
    const auto facts=old.Observe();const auto id=old.RetainedId();
    std::lock_guard<std::mutex> lock(state_->gate);auto& s=*state_;
    if(!id||s.listener_closed||s.listener_observed||s.listener_failed||s.observation.stopped)return BackendStatus::ProviderStopped;
    if(role!=RecordRole::Client&&role!=RecordRole::Server)return BackendStatus::InvalidState;
    const auto lane=static_cast<std::size_t>(role);
    if(s.config.bounded_native_reaccept_experiment&&s.native_resets[lane]>=1)return BackendStatus::Capacity;
    if(role==RecordRole::Client){
        if(facts.role!=NativeOwnerRole::Dial||s.socket!=*id||(!s.primary_observed&&!s.primary_closed)||s.primary_failed||s.close_status!=BackendStatus::None)return BackendStatus::InvalidIdentity;
        s.attempted=false;s.socket=-1;s.primary_observed=false;s.primary_closed=false;s.close_status=BackendStatus::None;
        s.observation.outgoing_socket.reset();
    }else{
        if(facts.role!=NativeOwnerRole::Accepted||(s.negotiated_id!=id&&s.observation.accepted_socket!=id)||
           (s.accepted_owner&&s.accepted_owner!=old)||s.accepted_failed||s.accepted_close_status!=BackendStatus::None)return BackendStatus::InvalidIdentity;
        s.accepted_owner={};s.negotiated_id.reset();s.observation.accepted_socket.reset();
        s.accepted_observed=false;s.accepted_closed=false;s.accepted_close_status=BackendStatus::None;
    }
    if(s.config.bounded_native_reaccept_experiment)++s.native_resets[lane];
    s.RefreshClosed();return BackendStatus::None;
}
BackendSocketResult PublicSocketBackend::CreateListener(ProviderCallbacks cb){if(!state_->dual)return {};return CreateSocket(state_,std::move(cb),true);}
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK
void PublicSocketBackend::SetAcceptedCommitHookForTest(void(*fn)(void*) noexcept,void* context) noexcept {
    std::lock_guard<std::mutex> lock(state_->gate);state_->before_capability=fn;state_->before_capability_context=context;
}
#endif
BackendStatus PublicSocketBackend::CommitAcceptedOwner(const RecordEntrance& entry,const NativeOwnerLease& owner){
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_FAKE_SDK
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
        FASTDDS_DSOFTBUS_PROBE_EVENT(role==RecordRole::Client?"Bind_enter":"Listen_enter",listening?s->observation.listener_entry_index:s->observation.entry_index,int(role),id,0,0);
        const auto rc=role==RecordRole::Client ? ::Bind(id,s->qos.data(),s->qos_count,&listener) : ::Listen(id,listening?s->listener_qos.data():s->qos.data(),listening?s->listener_qos_count:s->qos_count,&listener);
        FASTDDS_DSOFTBUS_PROBE_EVENT(role==RecordRole::Client?"Bind_return":"Listen_return",listening?s->observation.listener_entry_index:s->observation.entry_index,int(role),id,rc,0);
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
    try {FASTDDS_DSOFTBUS_DDS_FRAME("SDK_SEND",id,p,size);FASTDDS_DSOFTBUS_PROBE_EVENT("SendBytes_enter",s->observation.entry_index,int(RecordRole::Client),id,0,size);const auto rc=::SendBytes(id,p,static_cast<std::uint32_t>(size));FASTDDS_DSOFTBUS_PROBE_EVENT("SendBytes_return",s->observation.entry_index,int(RecordRole::Client),id,rc,size);std::lock_guard<std::mutex> lock(s->gate);s->Returned(PublicSocketOperation::Send,rc);
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
    try{FASTDDS_DSOFTBUS_PROBE_EVENT("Shutdown_enter",lane? s->observation.listener_entry_index:s->observation.entry_index,int(lane?RecordRole::Server:RecordRole::Client),id,0,0);::Shutdown(id);FASTDDS_DSOFTBUS_PROBE_EVENT("Shutdown_void_return",lane?s->observation.listener_entry_index:s->observation.entry_index,int(lane?RecordRole::Server:RecordRole::Client),id,0,0);std::lock_guard<std::mutex> lock(s->gate);result=s->Returned(PublicSocketOperation::Shutdown,{});}
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
