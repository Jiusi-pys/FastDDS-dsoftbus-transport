#include <mdds/dsoftbus/ProviderAdapter.hpp>
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
#include <mdds/dsoftbus/RuntimeProbeAccess.hpp>
#endif
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <stdexcept>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace mdds::dsoftbus::phase0 {
namespace detail {
thread_local unsigned provider_callback_depth=0;
struct CallbackContext {CallbackContext(){++provider_callback_depth;}~CallbackContext(){--provider_callback_depth;}};
struct ProviderDomainState {
    std::mutex gate;
    std::array<std::weak_ptr<ProviderAdapterState>, record_slot_capacity> entries;
};
struct ProviderAdapterState final : ServiceRecordProvider, NativeSendProvider, std::enable_shared_from_this<ProviderAdapterState> {
    ProviderAdapterState(ServiceRecordTable& r, NativeOwnerTable& n, ApplicationAttachments& a,
            std::shared_ptr<NativeBackend> p, BindingRequest q, BackendIdentity i)
        : records(r), native(n), attachments(a), backend(std::move(p)), request(std::move(q)), identity(std::move(i)),dual(backend->Topology()==BackendTopology::ListenerAndOutgoing),listener_identity(dual?backend->ListenerIdentity():BackendIdentity{}),listener_bridge(*this) {}
    ServiceRecordTable& records;
    NativeOwnerTable& native;
    ApplicationAttachments& attachments;
    const std::shared_ptr<NativeBackend> backend;
    BindingRequest request; // receiver is cleared/replaced only by RX control.
    const BackendIdentity identity;
    const bool dual;
    const BackendIdentity listener_identity;
    bool listener_ready=false,accepted_closing=false,outgoing_stopped=false,listener_stopped=false;
    AcceptedPhase accepted_phase=AcceptedPhase::Empty;
    std::optional<std::int32_t> accepted_id;
    BackendPeer accepted_peer;
    std::size_t accepted_rejected=0,accepted_activation_count=0;
    BackendStatus accepted_last_rejection=BackendStatus::None;
    NativeStatus accepted_last_native_rejection=NativeStatus::None;
    BackendStatus RejectAcceptedLocked(BackendStatus status,NativeStatus native_status) noexcept {
        ++accepted_rejected;accepted_last_rejection=status;accepted_last_native_rejection=native_status;return status;
    }
    BindingRequest listener_request;
    struct ListenerBridge final:ServiceRecordProvider {
        ProviderAdapterState& state;
        explicit ListenerBridge(ProviderAdapterState& s):state(s){}
        NativeSocketResult CreateSocket() noexcept override {return state.CreateListenerSocket();}
        SyncListenResult Listen(NativeSocketId id) noexcept override {return state.Listen(id);}
        SyncBindResult Bind(NativeSocketId) noexcept override {return SyncBindResult::Error(-1);}
        void Shutdown(NativeSocketId id) noexcept override {state.Shutdown(id);}
    } listener_bridge;
    std::shared_ptr<ServiceRecordBinding> listener,accepted;
    struct EventActivity {
        ProviderAdapterState& state;bool armed=false;
        ~EventActivity(){if(armed){std::lock_guard<std::mutex> lock(state.gate);--state.active;state.idle.notify_all();}}
    };
    mutable std::mutex gate;
    std::condition_variable idle;
    AdapterPhase phase = AdapterPhase::Constructed;
    bool stopped = false, creating = false, pending_stop_conflict = false;
    bool input_open = false;
    std::optional<std::int32_t> pending_stop;
    std::int32_t socket = -1;
    std::size_t active = 0, rejected = 0, exceptions = 0;
    std::uint64_t bytes_observed = 0; // local diagnostic sequence, not SDK identity
    std::size_t dispatching = 0;
    struct DispatchActivity {
        ProviderAdapterState& state; bool armed=false;
        ~DispatchActivity(){if(armed){std::lock_guard<std::mutex> lock(state.gate);--state.dispatching;state.idle.notify_all();}}
    };
    std::size_t send_calls = 0;
    std::int32_t last_priority = 0;
    BackendStatus provider_status = BackendStatus::None;
    BackendStatus cleanup_status = BackendStatus::None;
    std::optional<std::int32_t> first_callback_error;
    // Destroyed first, while its ServiceRecordProvider bridge and backend live.
    std::unique_ptr<ServiceRecordBinding> binding;

    void Fault(BackendStatus status) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        if(provider_status==BackendStatus::None)provider_status=status;
        if (status == BackendStatus::ProviderException) ++exceptions;
    }
    NativeSocketResult CreateSocket() noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            creating = true;
        }
        try {
            auto result = backend->Create(ProviderCallbacks{weak_from_this(),request.role});
            if (result.status == BackendStatus::None && result.socket && *result.socket >= 0) {
                std::lock_guard<std::mutex> lock(gate);
                creating = false;
                socket = *result.socket;
                if (pending_stop_conflict) {
                    stopped = true;
                    provider_status = BackendStatus::InvalidIdentity;
                } else if (pending_stop) {
                    if (*pending_stop == socket) stopped = true;
                    else ++rejected; // foreign candidate never closes an owner
                }
                pending_stop.reset();
                return NativeSocketResult::Id(socket);
            }
            Fault(result.status == BackendStatus::None ? BackendStatus::InvalidIdentity : result.status);
            if(result.api_called&&result.api_returned&&result.provider_error&&*result.provider_error<0){
                std::lock_guard<std::mutex> lock(gate);creating=false;pending_stop.reset();
                return NativeSocketResult::Error(*result.provider_error);
            }
        } catch (...) { Fault(BackendStatus::ProviderException); }
        {
            std::lock_guard<std::mutex> lock(gate);
            creating = false;
            pending_stop.reset();
        }
        // Legacy model bridge requires a negative failure sentinel. It is never
        // exported as an SDK rc: provider_status carries the actual adapter fault.
        return NativeSocketResult::Error(-1);
    }
    SyncListenResult Listen(NativeSocketId id) noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            if (stopped) return SyncListenResult::Error(-1); // provider NotCalled
        }
        try {
            auto result = backend->Listen(id);
            if (result.status == BackendStatus::None) return SyncListenResult::Success();
            Fault(result.status);
            if(result.called&&result.returned&&result.provider_return&&*result.provider_return!=0)return SyncListenResult::Error(*result.provider_return);
        } catch (...) { Fault(BackendStatus::ProviderException); }
        return SyncListenResult::Error(-1);
    }
    SyncBindResult Bind(NativeSocketId id) noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            if (stopped) return SyncBindResult::Error(-1); // provider NotCalled
        }
        try {
            auto result = backend->Bind(id);
            if (result.status == BackendStatus::None) return SyncBindResult::Success();
            Fault(result.status);
            if(result.called&&result.returned&&result.provider_return&&*result.provider_return!=0)return SyncBindResult::Error(*result.provider_return);
        } catch (...) { Fault(BackendStatus::ProviderException); }
        return SyncBindResult::Error(-1);
    }
    void CleanupFault(BackendStatus status) noexcept {
        {std::lock_guard<std::mutex> lock(gate);if(cleanup_status==BackendStatus::None)cleanup_status=status;}
        Fault(status);
    }
    void Shutdown(NativeSocketId id) noexcept override {
        try {
            auto result = backend->Shutdown(id);
            if (result.status != BackendStatus::None) CleanupFault(result.status);
        } catch (...) { CleanupFault(BackendStatus::ProviderException); }
    }
    std::int32_t Send(NativeSocketId id, const Route& route, const std::uint8_t* data,
            std::size_t size, SendQueue::Clock::time_point deadline, std::int32_t priority) override {
        // Reachable only from the controlled Host loop's admitted invocation.
        // Closing permits an invocation that won native entry before revocation;
        // the controller waits for that entire invocation before binding close.
        {
            std::lock_guard<std::mutex> lock(gate);
            if ((phase != AdapterPhase::Published && phase != AdapterPhase::Closing) || stopped || outgoing_stopped ||
                    id != socket || route.key.local_locator != request.route.local_locator ||
                    route.key.remote_locator != request.route.remote_locator)
                throw std::runtime_error("Host provider identity/state rejected");
            ++send_calls; last_priority = priority;
        }
        try {
            const auto result = backend->Send(id, data, size, deadline);
            if (result.status != BackendStatus::None || !result.provider_return)
                throw std::runtime_error("Host provider returned no send result");
            return *result.provider_return;
        } catch (...) { Fault(BackendStatus::ProviderException); throw; }
    }
    NativeSocketResult CreateListenerSocket() noexcept {
        try {
            auto result=backend->CreateListener(ProviderCallbacks{weak_from_this(),RecordRole::Server});
            if(result.status==BackendStatus::None&&result.socket&&*result.socket>=0)return NativeSocketResult::Id(*result.socket);
            Fault(result.status);
            if(result.provider_error&&*result.provider_error<0)return NativeSocketResult::Error(*result.provider_error);
        }catch(...){Fault(BackendStatus::ProviderException);}
        return NativeSocketResult::Error(-1);
    }
    BackendStatus Negotiate(RecordRole role,std::int32_t id,const BackendPeer& peer) noexcept {
        EventActivity activity{*this};
        std::shared_ptr<ServiceRecordBinding> anchor,candidate;
        try {
            BackendPeer owned_peer=peer; // bounded normalization already happened in the shim
            {
                std::lock_guard<std::mutex> lock(gate);
                const bool control_live=phase==AdapterPhase::Deploying||phase==AdapterPhase::Connected||(phase==AdapterPhase::Published&&input_open);
                if(!dual||role!=RecordRole::Server||id<0||stopped||listener_stopped||!listener_ready||!control_live||accepted_closing){return RejectAcceptedLocked(BackendStatus::InvalidState,NativeStatus::WrongState);}
                if((accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Active)&&accepted_id==id&&accepted_peer==peer)return BackendStatus::None;
                if(accepted_phase!=AcceptedPhase::Empty){return RejectAcceptedLocked(BackendStatus::Capacity,NativeStatus::AdmissionSaturated);}
                ++active;activity.armed=true;accepted_phase=AcceptedPhase::Preparing;
                accepted_id=id;accepted_peer=std::move(owned_peer);anchor=listener;
            }
            BindingRequest q=listener_request;q.existing=anchor->RoleEntrance(RecordRole::Server);q.record_owner=anchor.get();
            q.is_accepted=true;q.defer_accepted_ownership=true;q.accepted_socket=id;
            q.accepted={q.config.directory.locals[0].callback_namespace,peer.network_id,peer.name,peer.data_type};
            auto built=ServiceRecordBinding::Build(records,native,attachments,*this,q);
            if(built)candidate=std::shared_ptr<ServiceRecordBinding>(std::move(*built.value));
            const auto ready=candidate?candidate->Prepare():NativeStatus::AllocationFailure;
            {
                std::lock_guard<std::mutex> lock(gate);
                if(ready==NativeStatus::None&&!stopped&&accepted_phase==AcceptedPhase::Preparing&&
                        (phase==AdapterPhase::Deploying||phase==AdapterPhase::Connected||(phase==AdapterPhase::Published&&input_open))) {
                    accepted=candidate;accepted_phase=AcceptedPhase::Prepared;return BackendStatus::None;
                }
                (void)RejectAcceptedLocked(ready==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected,ready);
                if(accepted_phase==AcceptedPhase::Preparing){accepted_phase=AcceptedPhase::Empty;accepted_id.reset();}
            }
            if(candidate)(void)candidate->Abandon();
            return ready==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected;
        }catch(...) {
            if(candidate)(void)candidate->Abandon();
            std::lock_guard<std::mutex> lock(gate);(void)RejectAcceptedLocked(BackendStatus::AllocationFailure,NativeStatus::AllocationFailure);
            if(accepted_phase==AcceptedPhase::Preparing){accepted_phase=AcceptedPhase::Empty;accepted_id.reset();}
            return BackendStatus::AllocationFailure;
        }
    }
    BackendStatus Bound(RecordRole role,std::int32_t id,const BackendPeer& peer) noexcept {
        EventActivity activity{*this};std::shared_ptr<ServiceRecordBinding> candidate;
        {
            std::lock_guard<std::mutex> lock(gate);
            if(!dual||role!=RecordRole::Server||stopped||listener_stopped||accepted_closing||
                    (phase!=AdapterPhase::Deploying&&phase!=AdapterPhase::Connected&&phase!=AdapterPhase::Published)||accepted_id!=id||!(accepted_peer==peer)) {return RejectAcceptedLocked(BackendStatus::InvalidIdentity,NativeStatus::InvalidConfiguration);}
            if(accepted_phase==AcceptedPhase::Active)return BackendStatus::None;
            if(accepted_phase!=AcceptedPhase::Prepared){return RejectAcceptedLocked(BackendStatus::InvalidState,NativeStatus::WrongState);}
            accepted_phase=AcceptedPhase::Committing;candidate=accepted;++active;activity.armed=true;
        }
        auto status=candidate->CommitAccepted(id);
        BackendStatus installed=BackendStatus::None;
        if(status==NativeStatus::None) {
            // Installation is inside the same event permit as native commit.
            // A control close cannot pass this event before its original opaque
            // owner is available at the public-call boundary for settlement.
            try{installed=backend->CommitAcceptedOwner(candidate->RoleEntrance(RecordRole::Server),candidate->OriginalOwner());}
            catch(...){installed=BackendStatus::ProviderException;}
        }
        bool publish=false;
        {std::lock_guard<std::mutex> lock(gate);publish=!stopped&&!listener_stopped&&accepted_phase==AcceptedPhase::Committing&&!accepted_closing&&phase!=AdapterPhase::Closing;}
        if(status==NativeStatus::None&&installed==BackendStatus::None&&publish){status=candidate->Connect();if(status==NativeStatus::None)status=candidate->Publish();}
        std::lock_guard<std::mutex> lock(gate);
        if(status==NativeStatus::None&&installed==BackendStatus::None&&publish&&!stopped&&!listener_stopped&&accepted_phase==AcceptedPhase::Committing&&phase!=AdapterPhase::Closing){accepted_phase=AcceptedPhase::Active;++accepted_activation_count;return BackendStatus::None;}
        if(accepted_phase==AcceptedPhase::Committing)accepted_phase=AcceptedPhase::Failed;
        (void)RejectAcceptedLocked(installed!=BackendStatus::None?installed:BackendStatus::BindingRejected,status);
        // Keep the committed original owner for controlled rollback, never give
        // its close responsibility to an unrelated callback or guessed ID.
        return installed!=BackendStatus::None?installed:BackendStatus::BindingRejected;
    }
    BackendStatus RejectNegotiation(RecordRole role,std::int32_t id) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        if(!dual||role!=RecordRole::Server||accepted_id!=id||accepted_phase!=AcceptedPhase::Prepared)return BackendStatus::InvalidState;
        accepted_phase=AcceptedPhase::Stopping;return BackendStatus::None;
    }
    BackendStatus NativeStop(RecordRole role,std::int32_t id) noexcept {
        EventActivity activity{*this};std::shared_ptr<ServiceRecordBinding> target;ServiceRecordBinding* primary=nullptr;
        {
            std::lock_guard<std::mutex> lock(gate);
            if(phase==AdapterPhase::Closed)return BackendStatus::InvalidState;
            if(dual&&role==RecordRole::Server&&accepted_id==id) {
                if(accepted_phase==AcceptedPhase::Closed||accepted_phase==AcceptedPhase::Empty)return BackendStatus::InvalidState;
                accepted_phase=AcceptedPhase::Stopping;target=accepted;
            } else if(dual&&role==RecordRole::Server&&listener&&listener->Observe().socket==id) {
                listener_stopped=true;target=listener;
                if(accepted_phase==AcceptedPhase::Preparing||accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Committing)accepted_phase=AcceptedPhase::Stopping;
            }
            else if(role==request.role&&socket==id&&id>=0){if(dual)outgoing_stopped=true;else stopped=true;primary=binding.get();}
            else return BackendStatus::ForeignSocket;
            ++active;activity.armed=true;
        }
        if(target)(void)target->ObserveProviderShutdown(id);
        if(primary)(void)primary->ObserveProviderShutdown(id);
        return BackendStatus::None;
    }
    BackendStatus CloseAccepted() noexcept {
        if(provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
        std::shared_ptr<ServiceRecordBinding> child;
        {
            std::unique_lock<std::mutex> lock(gate);
            if(!dual)return BackendStatus::None;
            if(accepted_phase==AcceptedPhase::Closed)return BackendStatus::None;
            accepted_closing=true;accepted_phase=AcceptedPhase::Stopping;
            idle.wait(lock,[&]{return active==0&&dispatching==0;});child=accepted;
        }
        // Dual-only invariant: listener/outgoing RX never enters this one queue.
        // Cancel captured queued work with its original leases, no external pump.
        // TryTake only unlinks the existing owned record; unlike NextAdmitted it
        // does not allocate an execution or admit a receiver. Destruction releases
        // original payload/identity credits outside the attachment gate.
        for(;;){auto work=attachments.TryTake();if(!work)break;}
        auto result=child?child->Abandon():NativeStatus::None;
        if(result!=NativeStatus::None&&result!=NativeStatus::AlreadySettled)return BackendStatus::BindingRejected;
        std::lock_guard<std::mutex> lock(gate);accepted_phase=AcceptedPhase::Closed;accepted_closing=false;
        return BackendStatus::None;
    }
    BackendStatus Bytes(RecordRole role,std::int32_t id, const void* data, std::uint32_t size) noexcept {
        struct DecisionEmission {
            std::shared_ptr<ProcessReceiveObserver> observer;
            ReceiveDecision value;
            ~DecisionEmission(){if(observer)observer->OnDecision(value);}
        } decision;
        ServiceRecordBinding* original = nullptr;
        {
            std::lock_guard<std::mutex> lock(gate);
            decision.observer=request.receive_observer;
            decision.value.layer=ReceiveDecisionLayer::Adapter;
            decision.value.sequence=++bytes_observed;
            decision.value.socket=id;decision.value.role=static_cast<int>(role);
            decision.value.phase=static_cast<int>(phase);decision.value.size=size;
            // The backend declaration is NOT the ServiceRecordTable incarnation.
            // No record field is filled using that different identity domain.
            auto reject = [&](BackendStatus s,ReceiveDecisionBranch branch) {
                ++rejected;decision.value.branch=branch;decision.value.status_known=true;
                decision.value.status=static_cast<int>(s);return s;
            };
            if (stopped) return reject(BackendStatus::ProviderStopped,ReceiveDecisionBranch::AdapterStopped);
            if (phase != AdapterPhase::Published) return reject(BackendStatus::InvalidState,ReceiveDecisionBranch::AdapterPhase);
            if (!input_open) return reject(BackendStatus::ChannelClosed,ReceiveDecisionBranch::ChannelClosed);
            if(dual) {
                if(role!=RecordRole::Server||accepted_phase!=AcceptedPhase::Active||accepted_closing||accepted_id!=id)return reject(BackendStatus::ForeignSocket,ReceiveDecisionBranch::ForeignSocket);
            }else if(role!=request.role||id!=socket)return reject(BackendStatus::ForeignSocket,ReceiveDecisionBranch::ForeignSocket);
            if (!data || !size) return reject(BackendStatus::InvalidBuffer,ReceiveDecisionBranch::InvalidBuffer);
            if (active == std::numeric_limits<std::size_t>::max()) return reject(BackendStatus::Capacity,ReceiveDecisionBranch::AdapterCapacity);
            original = dual?accepted.get():binding.get();
            decision.value.branch=ReceiveDecisionBranch::Forwarded;
            decision.value.status_known=true;decision.value.status=static_cast<int>(BackendStatus::None);
            ++active;
        }
        // The callback's shared State pin and Close's ingress barrier retain
        // exactly this binding. No lookup of a newer application is performed.
        original->Entrance()(id, data, size);
        {
            std::lock_guard<std::mutex> lock(gate);
            --active;
        }
        idle.notify_all();
        // Forwarded into binding, not a claim of receiver delivery/acceptance.
        return BackendStatus::None;
    }
    BackendStatus Stop(std::int32_t id) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        if (phase == AdapterPhase::Closed) return BackendStatus::InvalidState;
        if (creating && socket < 0 && id >= 0) {
            // One bounded candidate for this Create call, not a native generation
            // claim. Reconcile with its return before any binding publication.
            if (pending_stop && *pending_stop != id) pending_stop_conflict = true;
            else if (!pending_stop) pending_stop = id;
            return BackendStatus::PendingIdentity;
        }
        if (socket < 0 || id != socket) return BackendStatus::ForeignSocket;
        stopped = true;
        // No binding operation here: setup may still hold Binding::State::gate.
        return BackendStatus::None;
    }
    BackendStatus Error(RecordRole role,std::int32_t id,std::int32_t code) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        if(phase==AdapterPhase::Closed)return BackendStatus::InvalidState;
        if(dual&&role==RecordRole::Server&&accepted_id==id&&accepted_phase!=AcceptedPhase::Empty&&accepted_phase!=AcceptedPhase::Closed)accepted_phase=AcceptedPhase::Stopping;
        else if(dual&&role==RecordRole::Server&&listener&&listener->Observe().socket==id){listener_stopped=true;if(accepted_phase==AcceptedPhase::Preparing||accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Committing)accepted_phase=AcceptedPhase::Stopping;}
        else if(role==request.role&&socket>=0&&id==socket){if(dual)outgoing_stopped=true;else stopped=true;}
        else return BackendStatus::ForeignSocket;
        if(!first_callback_error)first_callback_error=code;
        return BackendStatus::None; // error is not a native shutdown observation
    }
    BackendCloseResult Close() noexcept {
        if(provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return {BackendStatus::InvalidState,false};
        {
            std::unique_lock<std::mutex> lock(gate);
            if (phase == AdapterPhase::Closed) return {provider_status,true,SdkDrain::Unproven,cleanup_status};
            phase = AdapterPhase::Closing;
            idle.wait(lock, [&] { return active == 0; });
        }
        if(dual) {
            const auto accepted_close=CloseAccepted();if(accepted_close!=BackendStatus::None)return {accepted_close,false};
        }
        if (binding) {
            const auto before = binding->Observe();
            if (before.phase != BindingPhase::Empty) {
                const auto status = binding->Abandon();
                if (status != NativeStatus::None && status != NativeStatus::AlreadySettled)
                    return {BackendStatus::BindingRejected, false};
            }
            if (!binding->DrainComplete()) return {BackendStatus::BindingRejected, false};
        }
        if(listener&&listener->Observe().phase!=BindingPhase::Empty) {
            const auto result=listener->Abandon();
            if(result!=NativeStatus::None&&result!=NativeStatus::AlreadySettled)return {BackendStatus::BindingRejected,false};
            if(!listener->DrainComplete())return {BackendStatus::BindingRejected,false};
        }
        std::lock_guard<std::mutex> lock(gate);
        phase = AdapterPhase::Closed;
        input_open = false; request.receiver = nullptr;
        // Adapter resources may settle even when provider shutdown fails. Never
        // translate that into native drain, successful cancellation or retirement.
        return {provider_status,true,SdkDrain::Unproven,cleanup_status};
    }
};
} // namespace detail

ProviderAdapterDomain::ProviderAdapterDomain() : state_(std::make_shared<detail::ProviderDomainState>()) {}
ProviderAdapter::ProviderAdapter(std::shared_ptr<detail::ProviderAdapterState> s) noexcept : state_(std::move(s)) {}
ProviderAdapter::~ProviderAdapter() { if (state_ && !state_->Close().adapter_drained) std::terminate(); }
AdapterBuildResult ProviderAdapter::Build(ProviderAdapterDomain& domain, ServiceRecordTable& records,
        NativeOwnerTable& native, ApplicationAttachments& attachments, std::shared_ptr<NativeBackend> backend,
        const BindingRequest& request) {
    return BuildImpl(domain,records,native,attachments,std::move(backend),request,false);
}
AdapterBuildResult ProviderAdapter::BuildImpl(ProviderAdapterDomain& domain, ServiceRecordTable& records,
        NativeOwnerTable& native, ApplicationAttachments& attachments, std::shared_ptr<NativeBackend> backend,
        const BindingRequest& request, bool isolated_probe) {
    bool admitted = backend && backend->Kind()==BackendKind::HostMock;
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
    if(isolated_probe)admitted=backend&&backend->Kind()==BackendKind::DSoftBusProvider;
#else
    (void)isolated_probe;
#endif
    if (!admitted)
        return {BackendStatus::NativeBackendUnavailable, nullptr};
    BackendIdentity identity;
    try { identity = backend->Identity(); }
    catch (...) { return {BackendStatus::ProviderException, nullptr}; }
    if (!identity.record.value || !identity.owner.value || identity.service_name.empty() ||
            identity.service_name != request.service_name)
        return {BackendStatus::InvalidIdentity, nullptr};
    if (request.existing || request.record_owner || request.is_accepted)
        return {BackendStatus::InvalidState, nullptr}; // not this phase's topology
    if(backend->Topology()==BackendTopology::ListenerAndOutgoing) {
        BackendIdentity listener;
        try{listener=backend->ListenerIdentity();}catch(...){return {BackendStatus::ProviderException,nullptr};}
        if(request.role!=RecordRole::Client||!request.has_route||request.config.directory.locals.size()!=1||request.config.directory.routes.size()!=1||
                request.config.directions!=std::vector<RouteDirection>{RouteDirection::Both}||!request.config.dial_only_egress||!listener.record.value||!listener.owner.value||
                listener.service_name!=request.config.directory.locals[0].local_listen_name||
                identity.service_name!=request.config.directory.routes[0].fields.local_initiator_name||
                listener.service_name==identity.service_name||listener.record.value==identity.record.value)
            return {BackendStatus::InvalidIdentity,nullptr};
    }
    auto state = std::make_shared<detail::ProviderAdapterState>(records, native, attachments,
            std::move(backend), request, std::move(identity));
    {
        std::lock_guard<std::mutex> lock(domain.state_->gate);
        std::weak_ptr<detail::ProviderAdapterState>* free = nullptr;
        for (auto& entry : domain.state_->entries) {
            if (auto other = entry.lock()) {
                if (other->identity.record.value == state->identity.record.value ||
                        other->identity.service_name == state->identity.service_name)
                    return {BackendStatus::DuplicateRecord, nullptr};
            } else if (!free) free = &entry;
        }
        if (!free) return {BackendStatus::Capacity, nullptr};
        *free = state;
    }
    return {BackendStatus::None, std::unique_ptr<ProviderAdapter>(new ProviderAdapter(std::move(state)))};
}
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
AdapterBuildResult RuntimeProbeAccess::Build(ProviderAdapterDomain& domain,ServiceRecordTable& records,
        NativeOwnerTable& native,ApplicationAttachments& attachments,std::shared_ptr<NativeBackend> backend,const BindingRequest& request){
    return ProviderAdapter::BuildImpl(domain,records,native,attachments,std::move(backend),request,true);
}
std::shared_ptr<NativeSendProvider> RuntimeProbeAccess::SendBridge(ProviderAdapter& adapter,NativeOwnerTable& native){return adapter.BindSendProvider(native);}
#endif
BackendStatus ProviderAdapter::Initialize() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->phase != AdapterPhase::Constructed) return BackendStatus::InvalidState;
        state_->phase = AdapterPhase::Deploying;
    }
    if(state_->dual) {
        NativeStatus setup=NativeStatus::AllocationFailure;
        try {
            auto q=state_->request;q.service_name=state_->listener_identity.service_name;q.role=RecordRole::Server;q.has_route=false;
            state_->listener_request=q;
            auto candidate=ServiceRecordBinding::Build(state_->records,state_->native,state_->attachments,state_->listener_bridge,q);
            if(candidate) {
                auto listener=std::shared_ptr<ServiceRecordBinding>(std::move(*candidate.value));
                {std::lock_guard<std::mutex> lock(state_->gate);state_->listener=listener;}
                setup=listener->Prepare();
                if(setup==NativeStatus::None&&state_->backend->ConfigureEntrance(listener->RoleEntrance(RecordRole::Server))!=BackendStatus::None)setup=NativeStatus::InvalidConfiguration;
                if(setup==NativeStatus::None)setup=listener->Connect();
                if(setup==NativeStatus::None)setup=listener->Publish();
            }
        }catch(...){state_->Fault(BackendStatus::ProviderException);}
        {std::lock_guard<std::mutex> lock(state_->gate);
            if(setup!=NativeStatus::None||state_->stopped||state_->listener_stopped){state_->phase=AdapterPhase::Failed;return state_->provider_status==BackendStatus::None?BackendStatus::BindingRejected:state_->provider_status;}
            state_->listener_ready=true;
        }
#ifdef MDDS_PUBLIC_SOCKET_RUNTIME_PROBE
        // Probe control handshake outside all management gates. Not an SDK proof.
        if(!RuntimeProbeBeforeConnect()){state_->Fault(BackendStatus::InvalidState);return BackendStatus::InvalidState;}
#endif
    }
    auto built = ServiceRecordBinding::Build(state_->records, state_->native, state_->attachments,
            *state_, state_->request);
    NativeStatus status = built.issue.code;
    if (built) {
        state_->binding = std::move(*built.value);
        status = state_->binding->Prepare();
        if(status==NativeStatus::None){
            try {const auto configured=state_->backend->ConfigureEntrance(state_->binding->RoleEntrance(state_->request.role));
                if(configured!=BackendStatus::None){state_->Fault(configured);status=NativeStatus::InvalidConfiguration;}}
            catch(...){state_->Fault(BackendStatus::ProviderException);status=NativeStatus::InvalidConfiguration;}
        }
        if (status == NativeStatus::None) status = state_->binding->Connect();
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->phase = status == NativeStatus::None ? AdapterPhase::Connected : AdapterPhase::Failed;
    if (state_->stopped||(state_->dual&&(state_->outgoing_stopped||state_->listener_stopped))) return BackendStatus::ProviderStopped;
    if (state_->provider_status != BackendStatus::None) return state_->provider_status;
    return status == NativeStatus::None ? BackendStatus::None : BackendStatus::BindingRejected;
}
BackendStatus ProviderAdapter::Publish() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->stopped||(state_->dual&&(state_->outgoing_stopped||state_->listener_stopped))) return BackendStatus::ProviderStopped;
        if (state_->phase != AdapterPhase::Connected) return BackendStatus::InvalidState;
    }
    const auto status = state_->dual?NativeStatus::None:state_->binding->Publish();
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->stopped||(state_->dual&&(state_->outgoing_stopped||state_->listener_stopped))) return BackendStatus::ProviderStopped;
    if (status != NativeStatus::None) { state_->phase = AdapterPhase::Failed; return BackendStatus::BindingRejected; }
    state_->phase = AdapterPhase::Published;
    state_->input_open = true;
    return BackendStatus::None;
}
BackendStatus ProviderAdapter::DispatchOne() noexcept {
    detail::ProviderAdapterState::DispatchActivity activity{*state_};
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->stopped) return BackendStatus::ProviderStopped;
        if (state_->phase != AdapterPhase::Published) return BackendStatus::InvalidState;
        if (!state_->input_open) return BackendStatus::ChannelClosed;
        if(state_->dual&&state_->accepted_closing)return BackendStatus::NoWork;
        ++state_->dispatching; activity.armed=true;
    }
    auto* receiver_binding=state_->dual?state_->listener.get():state_->binding.get();
    auto work = receiver_binding->NextAdmitted();
    if (!work) return BackendStatus::NoWork;
    const auto result = receiver_binding->RunAdmitted(*work.value);
    if (result == InputDeliveryResult::CallbackFailure) return BackendStatus::CallbackFailure;
    return result == InputDeliveryResult::Delivered ? BackendStatus::None : BackendStatus::BindingRejected;
}
BackendStatus ProviderAdapter::CloseAccepted() noexcept {return state_->CloseAccepted();}
bool ProviderAdapter::IsProviderCallbackContext() noexcept {return detail::provider_callback_depth!=0;}
BackendStatus ProviderAdapter::CloseInput() noexcept {
    if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
    if(state_->dual){const auto result=state_->CloseAccepted();if(result!=BackendStatus::None)return result;}
    bool was_open=false;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        was_open=state_->input_open;state_->input_open=false;
    }
    auto* receiver_binding=state_->dual?state_->listener.get():state_->binding.get();
    const auto result=receiver_binding?receiver_binding->CloseInput():NativeStatus::None;
    if(result!=NativeStatus::None) {
        std::lock_guard<std::mutex> lock(state_->gate);state_->input_open=was_open;
        return BackendStatus::BindingRejected;
    }
    std::unique_lock<std::mutex> lock(state_->gate);
    state_->idle.wait(lock,[&]{return state_->active==0&&state_->dispatching==0;});
    state_->request.receiver=nullptr;
    return BackendStatus::None;
}
BackendStatus ProviderAdapter::ReopenInput(InputChannels::Receiver* receiver) noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(state_->stopped)return BackendStatus::ProviderStopped;
        if(state_->phase!=AdapterPhase::Published||state_->input_open||!receiver)return BackendStatus::InvalidState;
    }
    const auto result=(state_->dual?state_->listener.get():state_->binding.get())->ReopenInput(receiver);
    if(result!=NativeStatus::None)return result==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->stopped&&state_->phase==AdapterPhase::Published) {
            state_->request.receiver=receiver;state_->input_open=true;return BackendStatus::None;
        }
    }
    (void)state_->binding->CloseInput(); // stop during reopen: no half-published receiver
    return BackendStatus::ProviderStopped;
}
BackendCloseResult ProviderAdapter::Close() noexcept { return state_->Close(); }
AdapterObservation ProviderAdapter::Observe() const noexcept {
    AdapterObservation result;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        result.phase = state_->phase; result.provider_stopped = state_->stopped;result.outgoing_stopped=state_->outgoing_stopped;result.listener_stopped=state_->listener_stopped;
        result.input_open = state_->input_open;
        result.ingress_active = state_->active; result.ingress_rejected = state_->rejected;
        result.provider_exceptions = state_->exceptions; result.provider_status = state_->provider_status;
        result.first_callback_error = state_->first_callback_error;
        result.send_calls = state_->send_calls; result.last_priority = state_->last_priority;
    }
    if (state_->binding) result.binding = state_->binding->Observe();
    std::shared_ptr<ServiceRecordBinding> listener,accepted;
    {std::lock_guard<std::mutex> lock(state_->gate);listener=state_->listener;accepted=state_->accepted;result.accepted_phase=state_->accepted_phase;result.accepted_id=state_->accepted_id;result.accepted_rejected=state_->accepted_rejected;result.accepted_activation_count=state_->accepted_activation_count;result.accepted_closing=state_->accepted_closing;result.accepted_last_rejection=state_->accepted_last_rejection;result.accepted_last_native_rejection=state_->accepted_last_native_rejection;}
    if(listener)result.listener=listener->Observe();if(accepted)result.accepted=accepted->Observe();
    return result;
}
BackendStatus ProviderCallbacks::OnBytes(std::int32_t id, const void* data, std::uint32_t size) const noexcept {
    auto state = state_.lock();
    detail::CallbackContext context;
    return state ? state->Bytes(role_,id,data,size) : BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnShutdown(std::int32_t id) const noexcept {
    auto state = state_.lock();
    return state ? state->Stop(id) : BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnError(std::int32_t id,std::int32_t error) const noexcept {
    auto state=state_.lock();detail::CallbackContext context;return state?state->Error(role_,id,error):BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnNegotiate(std::int32_t id,const BackendPeer& p)const noexcept {
    auto s=state_.lock();detail::CallbackContext context;return s?s->Negotiate(role_,id,p):BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnBind(std::int32_t id,const BackendPeer& p)const noexcept {
    auto s=state_.lock();detail::CallbackContext context;return s?s->Bound(role_,id,p):BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnNegotiationRejected(std::int32_t id)const noexcept {
    auto s=state_.lock();detail::CallbackContext context;return s?s->RejectNegotiation(role_,id):BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnNativeShutdown(std::int32_t id)const noexcept {
    auto s=state_.lock();detail::CallbackContext context;return s?s->NativeStop(role_,id):BackendStatus::InvalidState;
}
std::shared_ptr<NativeSendProvider> ProviderAdapter::BindSendProvider(NativeOwnerTable& table) const noexcept {
    return &table == &state_->native ? std::static_pointer_cast<NativeSendProvider>(state_) : nullptr;
}
void ProviderAdapter::SealIngress() noexcept {
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->phase == AdapterPhase::Published) state_->phase = AdapterPhase::Closing;
}
}
