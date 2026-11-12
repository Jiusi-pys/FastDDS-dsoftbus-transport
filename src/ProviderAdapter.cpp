#include <fastdds_dsoftbus/dsoftbus/ProviderAdapter.hpp>
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
#include "../probes/PublicSocketRuntimeTrace.hpp"
#define STARTUP_LOCAL(event,value) runtime_probe::TraceEvent(event,UINT64_MAX,-1,-1,int(value),0)
#endif
#endif
#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#ifndef STARTUP_LOCAL
#define STARTUP_LOCAL(event,value) ((void)0)
#endif
#include <cstdio>
#include <atomic>
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
namespace fastdds_dsoftbus::dsoftbus::runtime_probe {
void TraceEvent(const char*,std::uint64_t,int,std::int32_t,std::int64_t,std::uint64_t) noexcept;
}
// Circuit route-renewal milestones through the existing probe hook.
#define RENEW_PROBE(event,value) ::fastdds_dsoftbus::dsoftbus::runtime_probe::TraceEvent(event,UINT64_MAX,-1,-1,static_cast<std::int64_t>(value),0)
#else
#define RENEW_PROBE(event,value) ((void)0)
#endif
#include <stdexcept>
#include <cstdio>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>

namespace fastdds_dsoftbus::dsoftbus::phase0 {
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
    bool external_listener=false,outgoing_started=false,listener_only=false;
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
        bool CanCreateBeforeExposure() const noexcept override {return state.CanCreateBeforeExposure();}
        PreExposureSocketResult CreateSocketBeforeExposure() noexcept override {
            PreExposureSocketResult result;
            result.native=state.CreateListenerSocketAttempt(&result);
            return result;
        }
        NativeStatus ConfigureCallbackEntrance(const RecordEntrance& entry) noexcept override {
            return state.ConfigureCallbackEntrance(entry);
        }
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
    std::mutex control_gate;
    std::size_t controls=0;
    bool renewal_failed=false;
    std::array<std::uint64_t,2> normal_end_owner{};
    std::array<bool,2> normal_end_done{};
    std::function<bool()> replacement_gate;
    std::uint64_t renewals=0; // completed circuit/bounded dial replacements
    std::int64_t renewal_probe_mask=-1; // last reported renewal state (probe only)
    bool redial_pending=false; // circuit dial binding retired, replacement not yet published
    std::unique_ptr<ServiceRecordBinding> retained_member; // replaced dial binding kept as record member until a replacement publishes
    bool renewal_attempt=false; // a circuit renewal InitializeCircuitOutgoing is in progress
    BackendStatus renewal_status=BackendStatus::None; // first native failure of the current attempt
    unsigned renewal_failures=0; // consecutive failed circuit redial attempts
    std::chrono::steady_clock::time_point renewal_retry_at{}; // earliest next redial after a failure
    // The redial runs on its own worker so a Bind (up to its 10 s deadline) never
    // stalls the pump that serves the other circuit and the accepted lane.
    // renewal_spawn serializes spawn/join; Close joins after controls drain.
    std::mutex renewal_spawn;
    std::thread renewal_worker;
    bool renewal_worker_busy=false; // under gate: set at spawn, cleared as the worker's last action
    std::thread::id renewal_thread{}; // the worker running the current attempt (fault scoping)
    std::function<std::unique_lock<std::mutex>()> renewal_serializer;
    NativeOwnerLease pending_reset_owner; // a failed attempt's owner whose ID release must be retried
    static constexpr unsigned kRenewalResetAttempts=5; // consecutive reset failures before latching
    unsigned reset_failures=0; // consecutive failed owner resets (separate from redial failures)
    // A failed reset is retried with the redial backoff; only repeated failure latches.
    BackendStatus ResetFailedLocked(BackendStatus reset) noexcept {
        ++reset_failures;
        if(reset_failures>=kRenewalResetAttempts){renewal_failed=true;return reset;}
        const unsigned shift=reset_failures<4?reset_failures-1:3;
        renewal_retry_at=std::chrono::steady_clock::now()+std::chrono::seconds(1u<<shift);
        return BackendStatus::NoWork;
    }
    void JoinRenewalWorker() noexcept {
        std::lock_guard<std::mutex> spawn(renewal_spawn);
        if(renewal_worker.joinable()&&renewal_worker.get_id()!=std::this_thread::get_id())renewal_worker.join();
    }
    NativeOwnerObservation retired_outgoing,retired_accepted;
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

    // Diagnostic only. The first fault latches provider_status, so naming that one
    // site and its status attributes the sticky verdict to the call that produced it.
    // Exactly one line per process; no state beyond the existing labels is observed.
    void Fault(BackendStatus status,int site=0) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        // A circuit renewal attempt's native failure (e.g. the peer process is gone)
        // is a renewal outcome retried later, not a sticky provider fault.
        // Only faults raised by the attempt itself (its worker thread) are scoped to it;
        // a fault on any other lane during the attempt stays a real provider fault.
        if(renewal_attempt&&std::this_thread::get_id()==renewal_thread){if(renewal_status==BackendStatus::None)renewal_status=status;return;}
        const bool first=provider_status==BackendStatus::None;
        if(first)provider_status=status;
        if (status == BackendStatus::ProviderException) ++exceptions;
        if(first){std::fprintf(stderr,"FDDST_ADAPTER_FAULT site=%d status=%d dispatching=%llu\n",site,static_cast<int>(status),static_cast<unsigned long long>(dispatching));std::fflush(stderr);}
    }
    bool CanCreateBeforeExposure() const noexcept override {
        return backend->CanCreateBeforeEntrance();
    }
    NativeStatus ConfigureCallbackEntrance(const RecordEntrance& entry) noexcept override {
        try {
            const auto configured=backend->ConfigureEntrance(entry);
            if(configured==BackendStatus::None)return NativeStatus::None;
            Fault(configured);
        }catch(...){Fault(BackendStatus::ProviderException);}
        return NativeStatus::InvalidConfiguration;
    }
    static void CopyCreationEvidence(PreExposureSocketResult& out,const BackendSocketResult& in) noexcept {
        out.api_called=in.api_called;out.api_returned=in.api_returned;
        out.returned_handle=in.socket;out.provider_error=in.provider_error;
        out.callback_unexposed=in.callback_unexposed;
    }
    NativeSocketResult CreateSocket() noexcept override {return CreateSocketAttempt(nullptr);}
    PreExposureSocketResult CreateSocketBeforeExposure() noexcept override {
        PreExposureSocketResult result;
        result.native=CreateSocketAttempt(&result);
        return result;
    }
    NativeSocketResult CreateSocketAttempt(PreExposureSocketResult* evidence) noexcept {
        {
            std::lock_guard<std::mutex> lock(gate);
            creating = true;
        }
        try {
            if(backend->RouteLifecycleEnabled()&&request.role==RecordRole::Client) {
                auto route=native.OperationalRoute(binding->OriginalOwner());
                if(!route||backend->ConfigurePeerRoute(*route.value)!=BackendStatus::None) {
                    std::lock_guard<std::mutex> lock(gate);creating=false;return NativeSocketResult::Error(-1);
                }
            }
            auto callbacks=ProviderCallbacks{weak_from_this(),request.role};
            auto result = evidence ? backend->CreateBeforeEntrance(std::move(callbacks),request.role)
                    : backend->Create(std::move(callbacks));
            if(evidence)CopyCreationEvidence(*evidence,result);
            const bool actual_handle=result.status==BackendStatus::None ||
                    (evidence&&result.api_called&&result.api_returned);
            if (actual_handle && result.socket && *result.socket >= 0) {
                if(result.status!=BackendStatus::None)Fault(result.status);
                std::lock_guard<std::mutex> lock(gate);
                creating = false;
                socket = *result.socket;
                if (pending_stop_conflict) {
                    if(request.config.bounded_reaccept_host){
                        outgoing_stopped=true;provider_status=BackendStatus::InvalidIdentity;
                        (void)native.QuarantineHostId(request.local,*result.socket);
                        // Ambiguous first effect: no owner/Shutdown right is inferred
                        // from an indistinguishable callback and positive return.
                        socket=-1;pending_stop.reset();return NativeSocketResult::Error(-1);
                    }
                    stopped = true;
                    provider_status = BackendStatus::InvalidIdentity;
                } else if (pending_stop) {
                    if (*pending_stop == socket) {if(external_listener)outgoing_stopped=true;else stopped=true;}
                    else ++rejected; // foreign candidate never closes an owner
                }
                pending_stop.reset();
                return NativeSocketResult::Id(socket);
            }
            Fault(result.status == BackendStatus::None ? BackendStatus::InvalidIdentity : result.status,132);
            if(result.api_called&&result.api_returned&&result.provider_error&&*result.provider_error<0){
                std::lock_guard<std::mutex> lock(gate);creating=false;pending_stop.reset();
                return NativeSocketResult::Error(*result.provider_error);
            }
        } catch (...) { Fault(BackendStatus::ProviderException,137); }
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
            Fault(result.status,155);
            if(result.called&&result.returned&&result.provider_return&&*result.provider_return!=0)return SyncListenResult::Error(*result.provider_return);
        } catch (...) { Fault(BackendStatus::ProviderException,157); }
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
            Fault(result.status,168);
            if(result.called&&result.returned&&result.provider_return&&*result.provider_return!=0)return SyncBindResult::Error(*result.provider_return);
        } catch (...) { Fault(BackendStatus::ProviderException,170); }
        return SyncBindResult::Error(-1);
    }
    SyncBindResult Bind(NativeSocketId id,std::chrono::steady_clock::time_point deadline) noexcept override {
        {std::lock_guard<std::mutex> lock(gate);if(stopped||outgoing_stopped)return SyncBindResult::Error(-1);}
        try {
            const auto result=backend->Bind(id,deadline);
            if(result.status==BackendStatus::None)return SyncBindResult::Success();
            Fault(result.status);
            if(result.called&&result.returned&&result.provider_return&&*result.provider_return!=0)return SyncBindResult::Error(*result.provider_return);
        }catch(...){Fault(BackendStatus::ProviderException);}
        return SyncBindResult::Error(-1);
    }
    void CleanupFault(BackendStatus status,int site=0) noexcept {
        {std::lock_guard<std::mutex> lock(gate);if(cleanup_status==BackendStatus::None)cleanup_status=status;}
        Fault(status,site);
    }
    void Shutdown(NativeSocketId id) noexcept override {
        try {
            auto result = backend->Shutdown(id);
            if (result.status != BackendStatus::None) CleanupFault(result.status,180);
        } catch (...) { CleanupFault(BackendStatus::ProviderException,181); }
    }
    std::int32_t Send(NativeSocketId id, const Route& route, const std::uint8_t* data,
            std::size_t size, SendQueue::Clock::time_point deadline, std::int32_t priority) override {
        bool deadline_refused=false;
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
            if (result.status == BackendStatus::None && result.provider_return) return *result.provider_return;
            // Diagnostic only: the provider's own verdict behind this rejection.
            // A returned status is a decision, not a thrown exception; without this
            // the two are indistinguishable in the adapter's sticky verdict.
            static std::atomic<int> once{0};
            if(once.fetch_add(1)==0){std::fprintf(stderr,"FDDST_ADAPTER_SEND_REJECT status=%d provider_return=%d api_called=%d size=%llu\n",static_cast<int>(result.status),result.provider_return?static_cast<int>(*result.provider_return):std::numeric_limits<std::int32_t>::min(),result.api_called?1:0,static_cast<unsigned long long>(size));std::fflush(stderr);}
            // A status returned without the SDK ever being entered is a decision about this
            // one invocation -- the backend refused it (buffer outside the descriptor bound,
            // socket no longer usable, or a deadline that had already expired when a reliable
            // retransmission reached the front of the queue). It is not the unclean-close
            // condition this verdict exists to record: the SDK was never entered, so nothing
            // inside it can have been left half-done, and the invocation's own ticket is
            // settled by the caller's returned-code path either way. A refusal is therefore
            // reported outside the latch, exactly as the identity/state pre-check above
            // reports its own rejection, and the pump still records the execution failure.
            // Only a status returned *after* the SDK was entered (api_called) still latches.
            if (result.api_called) throw std::runtime_error("Host provider returned no send result");
            deadline_refused=result.status==BackendStatus::InvalidState&&
                result.refusal==BackendSendRefusal::DeadlineExpired&&!result.api_returned&&!result.provider_return;
        } catch (...) { Fault(BackendStatus::ProviderException,201); throw; }
        if(deadline_refused)throw NativeSendDeadlineRefusal{};
        throw std::runtime_error("Host provider refused the send before SDK entry");
    }
    NativeSocketResult CreateListenerSocket() noexcept {return CreateListenerSocketAttempt(nullptr);}
    NativeSocketResult CreateListenerSocketAttempt(PreExposureSocketResult* evidence) noexcept {
        try {
            auto callbacks=ProviderCallbacks{weak_from_this(),RecordRole::Server};
            auto result=evidence?backend->CreateBeforeEntrance(std::move(callbacks),RecordRole::Server)
                    :backend->CreateListener(std::move(callbacks));
            if(evidence)CopyCreationEvidence(*evidence,result);
            if((result.status==BackendStatus::None||(evidence&&result.api_called&&result.api_returned))&&
                    result.socket&&*result.socket>=0){
                if(result.status!=BackendStatus::None)Fault(result.status);
                return NativeSocketResult::Id(*result.socket);
            }
            Fault(result.status);
            if(result.provider_error&&*result.provider_error<0)return NativeSocketResult::Error(*result.provider_error);
        }catch(...){Fault(BackendStatus::ProviderException,209);}
        return NativeSocketResult::Error(-1);
    }
    ProviderCallbacks IncomingCallbacks() noexcept { return ProviderCallbacks{weak_from_this(),RecordRole::Server}; }
    BackendStatus Negotiate(RecordRole role,std::int32_t id,const BackendPeer& peer) noexcept {
        EventActivity activity{*this};
        std::shared_ptr<ServiceRecordBinding> anchor,candidate;
        try {
            BackendPeer owned_peer=peer; // bounded normalization already happened in the shim
            {
                std::lock_guard<std::mutex> lock(gate);
                const bool control_live=phase==AdapterPhase::Deploying||phase==AdapterPhase::Connected||(phase==AdapterPhase::Published&&input_open);
                if((!dual&&!external_listener)||role!=RecordRole::Server||id<0||stopped||listener_stopped||!listener_ready||!control_live||accepted_closing){if(role==RecordRole::Server)(void)native.QuarantineHostId(request.local,id);return RejectAcceptedLocked(BackendStatus::InvalidState,NativeStatus::WrongState);}
                if((accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Active)&&accepted_id==id&&accepted_peer==peer)return BackendStatus::None;
                if(accepted_phase!=AcceptedPhase::Empty){(void)native.QuarantineHostId(request.local,id);return RejectAcceptedLocked(BackendStatus::Capacity,NativeStatus::AdmissionSaturated);}
                ++active;activity.armed=true;accepted_phase=AcceptedPhase::Preparing;
                accepted_id=id;accepted_peer=std::move(owned_peer);anchor=listener;
            }
            BindingRequest q=external_listener?request:listener_request;q.role=RecordRole::Server;q.has_route=false;
            q.existing=anchor->RoleEntrance(RecordRole::Server);q.record_owner=anchor.get();
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
            (void)native.QuarantineHostId(request.local,id);
            return ready==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected;
        }catch(...) {
            if(candidate)(void)candidate->Abandon();
            (void)native.QuarantineHostId(request.local,id);
            std::lock_guard<std::mutex> lock(gate);(void)RejectAcceptedLocked(BackendStatus::AllocationFailure,NativeStatus::AllocationFailure);
            if(accepted_phase==AcceptedPhase::Preparing){accepted_phase=AcceptedPhase::Empty;accepted_id.reset();}
            return BackendStatus::AllocationFailure;
        }
    }
    BackendStatus Bound(RecordRole role,std::int32_t id,const BackendPeer& peer) noexcept {
        EventActivity activity{*this};std::shared_ptr<ServiceRecordBinding> candidate;
        {
            std::lock_guard<std::mutex> lock(gate);
            if((!dual&&!external_listener)||role!=RecordRole::Server||stopped||listener_stopped||accepted_closing||
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
        if(role==RecordRole::Server)(void)native.QuarantineHostId(request.local,id);
        if(!dual||role!=RecordRole::Server||accepted_id!=id||accepted_phase!=AcceptedPhase::Prepared)return BackendStatus::InvalidState;
        accepted_phase=AcceptedPhase::Stopping;return BackendStatus::None;
    }
    BackendStatus NativeStop(RecordRole role,std::int32_t id) noexcept {
        EventActivity activity{*this};std::shared_ptr<ServiceRecordBinding> target;ServiceRecordBinding* primary=nullptr;
        {
            std::lock_guard<std::mutex> lock(gate);
            if(phase==AdapterPhase::Closed)return BackendStatus::InvalidState;
            if(request.config.bounded_reaccept_host&&role==RecordRole::Client&&creating&&socket<0){
                if(native.HostIdWasSeen(id))return BackendStatus::ForeignSocket;
                (void)native.QuarantineHostId(request.local,id);
                pending_stop_conflict=true;return BackendStatus::PendingIdentity;
            }
            if((dual||external_listener)&&role==RecordRole::Server&&accepted_id==id) {
                if(accepted_phase==AcceptedPhase::Closed||accepted_phase==AcceptedPhase::Empty)return BackendStatus::InvalidState;
                accepted_phase=AcceptedPhase::Stopping;target=accepted;
            } else if(dual&&role==RecordRole::Server&&listener&&listener->Observe().socket==id) {
                listener_stopped=true;target=listener;
                if(accepted_phase==AcceptedPhase::Preparing||accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Committing)accepted_phase=AcceptedPhase::Stopping;
            }
            else if(role==request.role&&socket==id&&id>=0){if(dual||external_listener)outgoing_stopped=true;else stopped=true;primary=binding.get();}
            else return BackendStatus::ForeignSocket;
            ++active;activity.armed=true;
        }
        if(target)(void)target->ObserveProviderShutdown(id);
        if(primary)(void)primary->ObserveProviderShutdown(id);
        // A circuit lane that ended needs renewal work from a sleeping pump.
        if(external_listener&&request.config.bounded_reaccept_host)attachments.NotifyWork();
        return BackendStatus::None;
    }
    BackendStatus CloseAccepted() noexcept {
        if(provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
        std::shared_ptr<ServiceRecordBinding> child;
        {
            std::unique_lock<std::mutex> lock(gate);
            if(!dual&&!external_listener)return BackendStatus::None;
            if(accepted_phase==AcceptedPhase::Closed)return BackendStatus::None;
            accepted_closing=true;accepted_phase=AcceptedPhase::Stopping;
            idle.wait(lock,[&]{return active==0&&dispatching==0;});child=accepted;
        }
        // Cancel this accepted connection's captured queued work, no external pump.
        // Domain circuits share this queue, so select by the original owner: a
        // sibling circuit's queued records stay deliverable. Discarding only unlinks
        // existing owned records; unlike NextAdmitted it does not allocate an
        // execution or admit a receiver. Destruction releases original
        // payload/identity credits outside the attachment gate.
        if(child)(void)attachments.DiscardNativeOwner(child->OriginalOwner());
        auto result=child?child->Abandon():NativeStatus::None;
        if(result!=NativeStatus::None&&result!=NativeStatus::AlreadySettled)return BackendStatus::BindingRejected;
        std::lock_guard<std::mutex> lock(gate);accepted_phase=AcceptedPhase::Closed;accepted_closing=false;
        return BackendStatus::None;
    }
BackendStatus RequestNormalEnd(RecordRole role) noexcept {
    if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
    std::unique_lock<std::mutex> control(control_gate,std::try_to_lock);
    if(!control.owns_lock())return BackendStatus::NoWork;
    ServiceRecordBinding* selected=nullptr;std::shared_ptr<ServiceRecordBinding> child;
    const auto lane=static_cast<std::size_t>(role);
    {std::lock_guard<std::mutex> lock(gate);
        if(lane>=2||!request.config.bounded_reaccept_host||!dual||
           phase!=AdapterPhase::Published||stopped||listener_stopped)return BackendStatus::InvalidState;
        child=accepted;selected=role==RecordRole::Client?binding.get():child.get();
        if(!selected)return BackendStatus::InvalidState;
        const auto incarnation=selected->OriginalOwner().Observe().incarnation;
        if(backend->RouteLifecycleEnabled()&&normal_end_owner[lane]!=incarnation){normal_end_done[lane]=false;normal_end_owner[lane]=0;}
        if(normal_end_done[lane])return BackendStatus::None;
        if(normal_end_owner[lane]&&normal_end_owner[lane]!=incarnation)return BackendStatus::InvalidIdentity;
        normal_end_owner[lane]=incarnation;
    }
    const auto result=selected->RequestNormalConnectionEnd();
    if(result==NativeStatus::EnteredPending||result==NativeStatus::CloseBusy||result==NativeStatus::ExistingPending)return BackendStatus::NoWork;
    if(result!=NativeStatus::None)return BackendStatus::BindingRejected;
    {std::lock_guard<std::mutex> lock(gate);
        if(cleanup_status!=BackendStatus::None)return cleanup_status;
        normal_end_done[lane]=true;
        if(role==RecordRole::Client)outgoing_stopped=true;
        else accepted_phase=AcceptedPhase::Stopping;
    }
    return BackendStatus::None;
}

    BackendStatus AdvanceNodeLifecycle() noexcept {
    if(backend->RouteLifecycleEnabled()) {
        NativeResult<NodeRouteRequest> resolution;
        bool resolving=false;
        {
            std::unique_lock<std::mutex> control(control_gate,std::try_to_lock);
            if(control.owns_lock()) {
                std::lock_guard<std::mutex> lock(gate);
                if(phase==AdapterPhase::Published&&!stopped&&!listener_stopped) {
                    resolution=native.TakeNodeResolution();
                    if(resolution){++controls;resolving=true;}
                }
            }
        }
        if(resolving){
            struct ResolutionDone{ProviderAdapterState& s;~ResolutionDone(){std::lock_guard<std::mutex> lock(s.gate);--s.controls;s.idle.notify_all();}} done{*this};
            v1::RouteResult result;
            // Keep lifetime accounting, without holding an adapter/control gate
            // across a potentially reentrant public directory call.
            try{result=backend->ResolveNode(resolution.value->identity(),resolution.value->network_hint());}catch(...){result={};}
            (void)native.CompleteNodeResolution(*resolution.value,result);
        }
        bool dial=false,accepted=false;
        {std::lock_guard<std::mutex> lock(gate);
            if(phase==AdapterPhase::Published&&!stopped&&!listener_stopped) {
                dial=binding&&!outgoing_stopped&&binding->OriginalOwner().Observe().close_intent;
                accepted=this->accepted&&accepted_phase==AcceptedPhase::Active&&this->accepted->OriginalOwner().Observe().close_intent;
            }
        }
        if(dial){const auto ended=RequestNormalEnd(RecordRole::Client);if(ended!=BackendStatus::None&&ended!=BackendStatus::NoWork)return ended;}
        if(accepted){const auto ended=RequestNormalEnd(RecordRole::Server);if(ended!=BackendStatus::None&&ended!=BackendStatus::NoWork)return ended;}
    }
    return BackendStatus::None;
    }
    NativeOwnerLease CurrentDialOwner() const {return binding?binding->OriginalOwner():NativeOwnerLease{};} // caller holds gate
    BackendStatus AdvanceHostReaccept() noexcept {
        if(!request.config.bounded_reaccept_host)return BackendStatus::None;
        if(provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
        std::unique_lock<std::mutex> control(control_gate,std::try_to_lock);
        if(!control.owns_lock())return BackendStatus::NoWork;
        bool retire=false,dial=false;std::shared_ptr<ServiceRecordBinding> child;
        {std::lock_guard<std::mutex> lock(gate);
            if(stopped||listener_stopped||phase!=AdapterPhase::Published)return BackendStatus::None;
            ++controls;retire=accepted_phase==AcceptedPhase::Stopping;child=accepted;dial=outgoing_stopped&&!renewal_failed&&!external_listener;
        }
        struct ControlDone {ProviderAdapterState& s;~ControlDone(){std::lock_guard<std::mutex> lock(s.gate);--s.controls;s.idle.notify_all();}} done{*this};
        if(external_listener&&retire)RENEW_PROBE("CircuitRenew_retire",0);
        if(retire){
            const auto original=child?child->OriginalOwner():NativeOwnerLease{};
            // A domain circuit's accepted lane ended by a native shutdown (e.g.
            // LINK_DOWN) has no local normal-end request; record the close intent.
            if(external_listener&&original&&!original.Observe().close_intent)(void)original.RequestClose();
            // A prepared-only candidate gets its close intent from Abandon;
            // entered/captured old work still prevents any ready publication.
            if(original&&original.Observe().owned_id&&!original.AdapterSettled()){if(external_listener)RENEW_PROBE("CircuitRenew_retire_unsettled",0);return BackendStatus::NoWork;}
            const auto closed=CloseAccepted();if(closed!=BackendStatus::None){if(external_listener)RENEW_PROBE("CircuitRenew_retire_close",closed);return closed;}
            if(original){std::lock_guard<std::mutex> lock(gate);retired_accepted=original.Observe();}
            if(original){const auto reset=backend->ResetBoundedConnection(RecordRole::Server,original);if(external_listener)RENEW_PROBE("CircuitRenew_retire_reset",reset);if(reset!=BackendStatus::None)return reset;}
            std::lock_guard<std::mutex> lock(gate);
            if(phase!=AdapterPhase::Published||stopped||listener_stopped)return BackendStatus::ProviderStopped;
            accepted.reset();accepted_id.reset();accepted_peer={};accepted_phase=AcceptedPhase::Empty;
        }
        if(dial){
            const auto original=binding->OriginalOwner();
            if(!original.AdapterSettled())return BackendStatus::NoWork;
            {std::lock_guard<std::mutex> lock(gate);retired_outgoing=original.Observe();}
            // Settlement belongs to the retired owner even while its route is
            // unavailable. Only creating the replacement requires a fresh route.
            if(backend->RouteLifecycleEnabled()&&!native.RouteAvailable(request.route))return BackendStatus::NoWork;
            if(replacement_gate){
                try {if(!replacement_gate())return BackendStatus::NoWork;}
                catch(...){std::lock_guard<std::mutex> lock(gate);renewal_failed=true;return BackendStatus::CallbackFailure;}
            }
            auto status=binding->PrepareHostDialReplacement();
            if(status!=NativeStatus::None){std::lock_guard<std::mutex> lock(gate);renewal_failed=true;return BackendStatus::BindingRejected;}
            {std::lock_guard<std::mutex> lock(gate);if(phase!=AdapterPhase::Published||stopped||listener_stopped)return BackendStatus::ProviderStopped;socket=-1;outgoing_stopped=false;}
            const auto reset=backend->ResetBoundedConnection(RecordRole::Client,original);
            if(reset!=BackendStatus::None){std::lock_guard<std::mutex> lock(gate);outgoing_stopped=true;renewal_failed=true;return reset;}
            status=binding->Connect();
            std::lock_guard<std::mutex> lock(gate);
            if(status!=NativeStatus::None||phase!=AdapterPhase::Published||stopped||listener_stopped||outgoing_stopped){outgoing_stopped=true;renewal_failed=true;return BackendStatus::BindingRejected;}
            ++renewals;
        }
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
            if(dual||external_listener) {
                if(role!=RecordRole::Server||accepted_phase!=AcceptedPhase::Active||accepted_closing||accepted_id!=id)return reject(BackendStatus::ForeignSocket,ReceiveDecisionBranch::ForeignSocket);
            }else if(role!=request.role||id!=socket)return reject(BackendStatus::ForeignSocket,ReceiveDecisionBranch::ForeignSocket);
            if (!data || !size) return reject(BackendStatus::InvalidBuffer,ReceiveDecisionBranch::InvalidBuffer);
            if (active == std::numeric_limits<std::size_t>::max()) return reject(BackendStatus::Capacity,ReceiveDecisionBranch::AdapterCapacity);
            original = (dual||external_listener)?accepted.get():binding.get();
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
        std::unique_lock<std::mutex> lock(gate);
        if (phase == AdapterPhase::Closed) return BackendStatus::InvalidState;
        if (creating && socket < 0 && id >= 0) {
            // One bounded candidate for this Create call, not a native generation
            // claim. Reconcile with its return before any binding publication.
            if (pending_stop && *pending_stop != id) pending_stop_conflict = true;
            else if (!pending_stop) pending_stop = id;
            return BackendStatus::PendingIdentity;
        }
        if (socket < 0 || id != socket) return BackendStatus::ForeignSocket;
        if(external_listener)outgoing_stopped=true;else stopped=true;
        // No binding operation here: setup may still hold Binding::State::gate.
        const bool wake=external_listener&&request.config.bounded_reaccept_host;lock.unlock();
        if(wake)attachments.NotifyWork(); // only the work signal, no binding call
        return BackendStatus::None;
    }
    BackendStatus Error(RecordRole role,std::int32_t id,std::int32_t code) noexcept {
        std::unique_lock<std::mutex> lock(gate);
        if(phase==AdapterPhase::Closed)return BackendStatus::InvalidState;
        if((dual||external_listener)&&role==RecordRole::Server&&accepted_id==id&&accepted_phase!=AcceptedPhase::Empty&&accepted_phase!=AcceptedPhase::Closed)accepted_phase=AcceptedPhase::Stopping;
        else if(dual&&role==RecordRole::Server&&listener&&listener->Observe().socket==id){listener_stopped=true;if(accepted_phase==AcceptedPhase::Preparing||accepted_phase==AcceptedPhase::Prepared||accepted_phase==AcceptedPhase::Committing)accepted_phase=AcceptedPhase::Stopping;}
        else if(role==request.role&&socket>=0&&id==socket){if(dual||external_listener)outgoing_stopped=true;else stopped=true;}
        else return BackendStatus::ForeignSocket;
        if(!first_callback_error)first_callback_error=code;
        const bool wake=external_listener&&request.config.bounded_reaccept_host;lock.unlock();
        if(wake)attachments.NotifyWork(); // a circuit lane that ended needs renewal work
        return BackendStatus::None; // error is not a native shutdown observation
    }
    // One bounded line on a path that already reports not-drained; no state is read or changed beyond the labels.
    static void CloseUndrained(int site,BackendStatus status) noexcept {
        std::fprintf(stderr,"FDDST_ADAPTER_CLOSE_UNDRAINED site=%d status=%d\n",site,static_cast<int>(status));std::fflush(stderr);
    }
    BackendCloseResult Close() noexcept {
        if(provider_callback_depth||ServiceRecordBinding::IsCallbackContext()){CloseUndrained(1,BackendStatus::InvalidState);return {BackendStatus::InvalidState,false};}
        {std::lock_guard<std::mutex> lock(gate);if(phase!=AdapterPhase::Closed)phase=AdapterPhase::Closing;}
        std::unique_lock<std::mutex> control(control_gate);
        {
            std::unique_lock<std::mutex> lock(gate);
            if (phase == AdapterPhase::Closed) return {provider_status,true,SdkDrain::Unproven,cleanup_status};
            phase = AdapterPhase::Closing;
            idle.wait(lock, [&] { return active == 0&&controls==0; });
        }
        JoinRenewalWorker(); // its attempt has released its control; only thread exit remains
        if(dual||external_listener) {
            const auto accepted_close=CloseAccepted();if(accepted_close!=BackendStatus::None){CloseUndrained(2,accepted_close);return {accepted_close,false};}
            // Close runs only at runtime end, after every pump stopped. Nothing can
            // deliver what is left, and DrainComplete below reads the whole shared
            // queue, so sibling circuits' records are cancelled here as well.
            for(;;){auto work=attachments.TryTake();if(!work)break;}
        }
        if (binding) {
            const auto before = binding->Observe();
            if (before.phase != BindingPhase::Empty) {
                const auto status = binding->Abandon();
                if (status != NativeStatus::None && status != NativeStatus::AlreadySettled){
                    CloseUndrained(3,BackendStatus::BindingRejected);return {BackendStatus::BindingRejected, false};}
            }
            if (!binding->DrainComplete()){CloseUndrained(4,BackendStatus::BindingRejected);return {BackendStatus::BindingRejected, false};}
        }
        if(retained_member){(void)retained_member->Abandon();retained_member.reset();}
        if(listener&&listener->Observe().phase!=BindingPhase::Empty) {
            const auto result=listener->Abandon();
            if(result!=NativeStatus::None&&result!=NativeStatus::AlreadySettled){CloseUndrained(5,BackendStatus::BindingRejected);return {BackendStatus::BindingRejected,false};}
            if(!listener->DrainComplete()){CloseUndrained(6,BackendStatus::BindingRejected);return {BackendStatus::BindingRejected,false};}
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
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
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
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
AdapterBuildResult RuntimeProbeAccess::Build(ProviderAdapterDomain& domain,ServiceRecordTable& records,
        NativeOwnerTable& native,ApplicationAttachments& attachments,std::shared_ptr<NativeBackend> backend,const BindingRequest& request){
    return ProviderAdapter::BuildImpl(domain,records,native,attachments,std::move(backend),request,true);
}
std::shared_ptr<NativeSendProvider> RuntimeProbeAccess::SendBridge(ProviderAdapter& adapter,NativeOwnerTable& native){return adapter.BindSendProvider(native);}
#endif
BackendStatus ProviderAdapter::Initialize() noexcept { return InitializeImpl(false); }
BackendStatus ProviderAdapter::InitializeListenerOnly() noexcept { return InitializeImpl(true); }
BackendStatus ProviderAdapter::InitializeImpl(bool listener_only) noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->phase != AdapterPhase::Constructed) return BackendStatus::InvalidState;
        state_->phase = AdapterPhase::Deploying;state_->listener_only=listener_only;
    }
    if(state_->dual) {
        NativeStatus setup=NativeStatus::AllocationFailure;
        try {
            auto q=state_->request;q.service_name=state_->listener_identity.service_name;q.role=RecordRole::Server;q.has_route=false;
            state_->listener_request=q;
            auto candidate=ServiceRecordBinding::Build(state_->records,state_->native,state_->attachments,state_->listener_bridge,q);
            STARTUP_LOCAL("ListenerBuild_NativeStatus_result",candidate.issue.code);
            if(candidate) {
                auto listener=std::shared_ptr<ServiceRecordBinding>(std::move(*candidate.value));
                {std::lock_guard<std::mutex> lock(state_->gate);state_->listener=listener;}
                setup=listener->Prepare();STARTUP_LOCAL("ListenerPrepare_NativeStatus_result",setup);
                if(setup==NativeStatus::None&&!listener->CallbackPublicationDeferred()){const auto configured=state_->backend->ConfigureEntrance(listener->RoleEntrance(RecordRole::Server));STARTUP_LOCAL("ListenerEntrance_BackendStatus_result",configured);if(configured!=BackendStatus::None)setup=NativeStatus::InvalidConfiguration;}
                if(setup==NativeStatus::None){setup=listener->Connect();STARTUP_LOCAL("ListenerConnect_NativeStatus_result",setup);}
                if(setup==NativeStatus::None){setup=listener->Publish();STARTUP_LOCAL("ListenerPublish_NativeStatus_result",setup);}
            }
        }catch(...){state_->Fault(BackendStatus::ProviderException,645);}
        {std::lock_guard<std::mutex> lock(state_->gate);
            if(setup!=NativeStatus::None||state_->stopped||state_->listener_stopped){state_->phase=AdapterPhase::Failed;return state_->provider_status==BackendStatus::None?BackendStatus::BindingRejected:state_->provider_status;}
            state_->listener_ready=true;
        }
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
        // Probe control handshake outside all management gates. Not an SDK proof.
        if(!RuntimeProbeBeforeConnect()){state_->Fault(BackendStatus::InvalidState,652);return BackendStatus::InvalidState;}
#endif
    }
    if(listener_only) {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->dual||state_->stopped||state_->listener_stopped)return BackendStatus::InvalidState;
        state_->phase=AdapterPhase::Connected;return BackendStatus::None;
    }
    auto built = ServiceRecordBinding::Build(state_->records, state_->native, state_->attachments,
            *state_, state_->request);
    NativeStatus status = built.issue.code;
    if (built) {
        state_->binding = std::move(*built.value);
        status = state_->binding->Prepare();
        if(status==NativeStatus::None&&!state_->binding->CallbackPublicationDeferred()){
            try {const auto configured=state_->backend->ConfigureEntrance(state_->binding->RoleEntrance(state_->request.role));
                if(configured!=BackendStatus::None){state_->Fault(configured,663);status=NativeStatus::InvalidConfiguration;}}
            catch(...){state_->Fault(BackendStatus::ProviderException,664);status=NativeStatus::InvalidConfiguration;}
        }
        if (status == NativeStatus::None) status = state_->binding->Connect();
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->phase = status == NativeStatus::None ? AdapterPhase::Connected : AdapterPhase::Failed;
    if (state_->stopped||(state_->dual&&(state_->outgoing_stopped||state_->listener_stopped))) return BackendStatus::ProviderStopped;
    if (state_->provider_status != BackendStatus::None) return state_->provider_status;
    return status == NativeStatus::None ? BackendStatus::None : BackendStatus::BindingRejected;
}
BackendStatus ProviderAdapter::PrepareCircuitAnchor() noexcept {
    {std::lock_guard<std::mutex> lock(state_->gate);
        if(state_->dual||state_->phase!=AdapterPhase::Constructed||!state_->request.shared_input_channel)return BackendStatus::InvalidState;
        state_->phase=AdapterPhase::Deploying;
    }
    auto built=ServiceRecordBinding::Build(state_->records,state_->native,state_->attachments,*state_,state_->request);
    NativeStatus result=built.issue.code;
    if(built) {
        auto anchor=std::shared_ptr<ServiceRecordBinding>(std::move(*built.value));
        result=anchor->PrepareRecordAnchor();
        std::lock_guard<std::mutex> lock(state_->gate);state_->listener=std::move(anchor);
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    if(result!=NativeStatus::None){state_->phase=AdapterPhase::Failed;return BackendStatus::BindingRejected;}
    state_->external_listener=true;state_->listener_ready=true;
    state_->phase=AdapterPhase::Published;state_->input_open=true;return BackendStatus::None;
}
BackendStatus ProviderAdapter::InitializeCircuitOutgoing(std::chrono::steady_clock::time_point deadline) noexcept {
    std::unique_lock<std::mutex> control(state_->control_gate,std::try_to_lock);
    if(!control.owns_lock())return BackendStatus::NoWork;
    {std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->external_listener||state_->phase!=AdapterPhase::Published||state_->stopped)return BackendStatus::InvalidState;
        if(state_->outgoing_started)return state_->binding&&state_->binding->Observe().phase==BindingPhase::Published&&state_->socket>=0&&state_->provider_status==BackendStatus::None?BackendStatus::None:BackendStatus::InvalidState;
        if(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count()<=0)return BackendStatus::NoWork;
    }
    auto q=state_->request;q.existing=state_->listener->RoleEntrance(RecordRole::Client);q.record_owner=state_->listener.get();
    auto built=ServiceRecordBinding::Build(state_->records,state_->native,state_->attachments,*state_,q);
    NativeStatus status=built.issue.code;
    RENEW_PROBE("CircuitOutgoing_build",status);
    if(built) {
        state_->binding=std::move(*built.value); // Retain cleanup owner before any positive SDK ID.
        status=state_->binding->Prepare();
        RENEW_PROBE("CircuitOutgoing_prepare",status);
        if(status==NativeStatus::None) {
            try{const auto configured=state_->backend->ConfigureEntrance(state_->binding->RoleEntrance(RecordRole::Client));RENEW_PROBE("CircuitOutgoing_entrance",configured);if(configured!=BackendStatus::None)status=NativeStatus::InvalidConfiguration;}
            catch(...){status=NativeStatus::ProviderContractFault;}
        }
        if(status==NativeStatus::None) {
            // Before entering Connect no native attempt has happened. Expiry
            // here is transient; retire the unused reservation, never redial an ID.
            if(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count()<=0) {
                const auto cleanup=state_->binding->Abandon();
                if(cleanup!=NativeStatus::None&&cleanup!=NativeStatus::AlreadySettled){std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=true;return BackendStatus::BindingRejected;}
                state_->binding.reset();return BackendStatus::NoWork;
            }
            {std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=true;}
            bool attempt_entered=false;status=state_->binding->Connect(deadline,attempt_entered);RENEW_PROBE("CircuitOutgoing_connect",status);
            if(!attempt_entered&&status==NativeStatus::NotReady) {
                const auto cleanup=state_->binding->Abandon();
                if(cleanup!=NativeStatus::None&&cleanup!=NativeStatus::AlreadySettled){std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=true;return BackendStatus::BindingRejected;}
                state_->binding.reset();
                {std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=false;}
                return BackendStatus::NoWork;
            }
        }
        if(status==NativeStatus::None){status=state_->binding->Publish();RENEW_PROBE("CircuitOutgoing_publish",status);}
    } else {
        // Allocation/preparation failure is retained, never an implicit retry.
        std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=true;
    }
    if(status!=NativeStatus::None){std::lock_guard<std::mutex> lock(state_->gate);state_->outgoing_started=true;}
    return status==NativeStatus::None?BackendStatus::None:BackendStatus::BindingRejected;
}
ProviderCallbacks ProviderAdapter::IncomingCallbacks() const noexcept { return state_->IncomingCallbacks(); }
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
    const auto node_control=state_->AdvanceNodeLifecycle();
    if(node_control!=BackendStatus::None&&node_control!=BackendStatus::NoWork)return node_control;
    const auto control=state_->AdvanceHostReaccept();
    if(control!=BackendStatus::None&&control!=BackendStatus::NoWork)return control;
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
BackendStatus ProviderAdapter::DispatchAuthorized(const std::array<NativeOwnerTable*,2>& tables) noexcept {
    detail::ProviderAdapterState::DispatchActivity activity{*state_};
    {std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->listener_only||!state_->dual||state_->phase!=AdapterPhase::Published||state_->stopped||!state_->input_open)return BackendStatus::InvalidState;
        ++state_->dispatching;activity.armed=true;
    }
    auto work=state_->listener->NextAdmittedAuthorized(tables);
    if(!work)return BackendStatus::NoWork;
    const auto result=state_->listener->RunAdmitted(*work.value);
    return result==InputDeliveryResult::Delivered?BackendStatus::None:
        result==InputDeliveryResult::CallbackFailure?BackendStatus::CallbackFailure:BackendStatus::BindingRejected;
}
NativeResult<BindingInput> ProviderAdapter::AttachSharedInput(InputChannels::Receiver* receiver) noexcept {
    {std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->request.shared_input_channel||state_->phase!=AdapterPhase::Published||state_->stopped)
            return {std::nullopt,{NativeStatus::WrongState}};
    }
    return (state_->dual||state_->external_listener?state_->listener.get():state_->binding.get())->AttachSharedInput(receiver);
}
BackendStatus ProviderAdapter::CloseAccepted() noexcept {if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;std::lock_guard<std::mutex> control(state_->control_gate);return state_->CloseAccepted();}
BackendStatus ProviderAdapter::RequestNormalEnd(RecordRole role) noexcept {return state_->RequestNormalEnd(role);}
bool ProviderAdapter::CircuitRenewalPending() const noexcept {
    std::lock_guard<std::mutex> lock(state_->gate);const auto& s=*state_;
    return s.external_listener&&s.request.config.bounded_reaccept_host&&s.phase==AdapterPhase::Published&&!s.stopped&&!s.listener_stopped&&
        (((s.outgoing_stopped||s.redial_pending)&&!s.renewal_failed)||s.accepted_phase==AcceptedPhase::Stopping);
}
BackendStatus ProviderAdapter::AdvanceCircuitRenewal() noexcept {
    if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
    {std::lock_guard<std::mutex> lock(state_->gate);auto& s=*state_;
        if(!s.external_listener||!s.request.config.bounded_reaccept_host)return BackendStatus::InvalidState;
        const std::int64_t mask=(s.stopped?4:0)|(s.listener_stopped?8:0)|(s.outgoing_stopped?16:0)|(s.renewal_failed?32:0)|
            (static_cast<std::int64_t>(s.phase)<<8)|(static_cast<std::int64_t>(s.accepted_phase)<<16);
        if(mask!=s.renewal_probe_mask){s.renewal_probe_mask=mask;RENEW_PROBE("CircuitRenew_state",mask);}
    }
    // Accepted lane: retire the settled record so a new peer dial is admitted.
    const auto accepted_result=state_->AdvanceHostReaccept();
    if(accepted_result!=BackendStatus::None&&accepted_result!=BackendStatus::NoWork){RENEW_PROBE("CircuitRenew_retire_error",accepted_result);return accepted_result;}
    // Dial lane: a circuit dial binding is a record-group member, so it is
    // replaced as a whole binding (not by owner replacement) after its owner
    // settles and the peer is reachable again. The replaced binding is retained
    // as the record member until a replacement publishes: the shared record
    // seals when its last owner leaves, and a failed attempt never joins it.
    {
        std::unique_lock<std::mutex> control(state_->control_gate,std::try_to_lock);
        if(!control.owns_lock())return BackendStatus::NoWork;
        NativeOwnerLease original;
        {std::lock_guard<std::mutex> lock(state_->gate);auto& s=*state_;
            if(s.stopped||s.listener_stopped||s.phase!=AdapterPhase::Published||s.renewal_failed)return BackendStatus::None;
            // Another pump owns the attempt, or the previous worker is still cleaning up:
            // never join a busy worker here, its cleanup may need this pump.
            if(s.renewal_attempt||s.renewal_worker_busy)return BackendStatus::NoWork;
            if(!s.redial_pending){if(!s.outgoing_stopped||!s.binding)return BackendStatus::None;original=s.CurrentDialOwner();}
            if(std::chrono::steady_clock::now()<s.renewal_retry_at)return BackendStatus::NoWork;
        }
        NativeOwnerLease pending;
        {std::lock_guard<std::mutex> lock(state_->gate);pending=state_->pending_reset_owner;}
        if(pending){
            const auto reset=state_->backend->ResetBoundedConnection(RecordRole::Client,pending);
            RENEW_PROBE("CircuitRenew_pending_reset",reset);
            std::lock_guard<std::mutex> lock(state_->gate);
            if(reset!=BackendStatus::None)return state_->ResetFailedLocked(reset);
            state_->pending_reset_owner=NativeOwnerLease{};state_->reset_failures=0;
        }
        if(original){
            // A native shutdown (e.g. LINK_DOWN) has no local normal-end request.
            if(!original.Observe().close_intent)(void)original.RequestClose();
            if(!original.AdapterSettled())return BackendStatus::NoWork;
        }
        if(state_->replacement_gate){
            try{if(!state_->replacement_gate())return BackendStatus::NoWork;}
            catch(...){std::lock_guard<std::mutex> lock(state_->gate);state_->renewal_failed=true;RENEW_PROBE("CircuitRenew_gate_error",0);return BackendStatus::CallbackFailure;}
        }
        if(original){
            const auto reset=state_->backend->ResetBoundedConnection(RecordRole::Client,original);
            RENEW_PROBE("CircuitRenew_dial_reset",reset);
            if(reset!=BackendStatus::None){std::lock_guard<std::mutex> lock(state_->gate);return state_->ResetFailedLocked(reset);}
            std::lock_guard<std::mutex> lock(state_->gate);auto& s=*state_;
            s.retired_outgoing=original.Observe();s.reset_failures=0;
            if(!s.retained_member)s.retained_member=std::move(s.binding);else s.binding.reset();
            s.socket=-1;s.outgoing_stopped=false;s.outgoing_started=false;s.redial_pending=true;
        }
        // Pin the attempt as a control while control_gate is still held: Close()
        // then waits until binding/retained_member are no longer touched here.
        std::lock_guard<std::mutex> lock(state_->gate);
        state_->renewal_attempt=true;state_->renewal_worker_busy=true;state_->renewal_status=BackendStatus::None;++state_->controls;
    }
    auto* self=this;
    auto& st=*state_;
    const auto worker=[self]{
        auto& st=*self->state_;
        // Declared first, so it runs last: after this the thread only exits, and a join is short.
        struct WorkerDone{std::remove_reference_t<decltype(st)>& s;~WorkerDone(){{std::lock_guard<std::mutex> lock(s.gate);s.renewal_worker_busy=false;s.renewal_thread={};s.idle.notify_all();}
            s.attachments.NotifyWork();}} worker_done{st}; // wake the pump: a replacement can carry queued sends, a failure has a retry time
        struct AttemptDone{std::remove_reference_t<decltype(st)>& s;~AttemptDone(){std::lock_guard<std::mutex> lock(s.gate);--s.controls;s.idle.notify_all();}} attempt_done{st};
        {std::lock_guard<std::mutex> lock(st.gate);st.renewal_thread=std::this_thread::get_id();}
        // Same root-before-circuit setup serialization as the first outgoing setup.
        std::unique_lock<std::mutex> setup;
        if(st.renewal_serializer){try{setup=st.renewal_serializer();}catch(...){}}
        const bool serialized=!st.renewal_serializer||setup.owns_lock();
        const auto status=serialized?self->InitializeCircuitOutgoing(std::chrono::steady_clock::now()+std::chrono::seconds(10)):BackendStatus::NoWork;
        if(setup.owns_lock())setup.unlock();
        RENEW_PROBE("CircuitRenew_dial_result",status);
        std::unique_ptr<ServiceRecordBinding> release,failed;NativeOwnerLease failed_owner;
        {std::lock_guard<std::mutex> lock(st.gate);auto& s=st;
            s.renewal_attempt=false;
            if(status==BackendStatus::None){++s.renewals;s.redial_pending=false;s.renewal_failures=0;s.renewal_retry_at={};release=std::move(s.retained_member);}
            else if(status==BackendStatus::NoWork)s.renewal_retry_at=std::chrono::steady_clock::now()+std::chrono::milliseconds(50); // setup busy: short retry
            else if(status!=BackendStatus::NoWork){
                // The peer may be gone (process exit) while its node stays online:
                // free the failed attempt now and retry with backoff (1 s .. 8 s).
                failed_owner=s.CurrentDialOwner();failed=std::move(s.binding);
                s.socket=-1;s.outgoing_stopped=false;s.outgoing_started=false;s.redial_pending=true;
                ++s.renewal_failures;const unsigned shift=s.renewal_failures<4?s.renewal_failures-1:3;
                s.renewal_retry_at=std::chrono::steady_clock::now()+std::chrono::seconds(1u<<shift);
                RENEW_PROBE("CircuitRenew_retry_scheduled",s.renewal_failures);
            }
        }
        release.reset(); // the replacement is published; outside every adapter gate
        if(failed){(void)failed->Abandon();failed.reset();} // shuts its returned Socket, settles its owner
        if(failed_owner){
            const auto reset=st.backend->ResetBoundedConnection(RecordRole::Client,failed_owner);
            RENEW_PROBE("CircuitRenew_failed_reset",reset);
            if(reset!=BackendStatus::None){std::lock_guard<std::mutex> lock(st.gate);st.pending_reset_owner=failed_owner;(void)st.ResetFailedLocked(reset);return;}
        }
    };
    try {
        std::lock_guard<std::mutex> spawn(st.renewal_spawn);
        if(st.renewal_worker.joinable())st.renewal_worker.join(); // not busy (checked above): it is only exiting
        st.renewal_worker=std::thread(worker);
    } catch(...) {
        std::lock_guard<std::mutex> lock(st.gate);st.renewal_attempt=false;st.renewal_worker_busy=false;--st.controls;st.idle.notify_all();
        RENEW_PROBE("CircuitRenew_worker_unavailable",0);
    }
    return BackendStatus::NoWork;
}
bool ProviderAdapter::SetBoundedReplacementGate(std::function<bool()> gate){
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->phase!=AdapterPhase::Constructed||!state_->request.config.bounded_reaccept_host)return false;
    state_->replacement_gate=std::move(gate);return true;
}
bool ProviderAdapter::SetCircuitRenewalSerializer(std::function<std::unique_lock<std::mutex>()> serializer){
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->phase!=AdapterPhase::Constructed||!state_->request.config.bounded_reaccept_host)return false;
    state_->renewal_serializer=std::move(serializer);return true;
}
bool ProviderAdapter::IsProviderCallbackContext() noexcept {return detail::provider_callback_depth!=0;}
BackendStatus ProviderAdapter::CloseInput() noexcept {
    if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
    std::lock_guard<std::mutex> control(state_->control_gate);
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
    if(detail::provider_callback_depth||ServiceRecordBinding::IsCallbackContext())return BackendStatus::InvalidState;
    std::lock_guard<std::mutex> control(state_->control_gate);
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(state_->stopped)return BackendStatus::ProviderStopped;
        if(state_->phase!=AdapterPhase::Published||state_->input_open||!receiver)return BackendStatus::InvalidState;
    }
    auto* receiver_binding=state_->dual?state_->listener.get():state_->binding.get();
    const auto result=receiver_binding->ReopenInput(receiver);
    if(result!=NativeStatus::None)return result==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->stopped&&state_->phase==AdapterPhase::Published) {
            state_->request.receiver=receiver;state_->input_open=true;return BackendStatus::None;
        }
    }
    (void)receiver_binding->CloseInput(); // stop during reopen: roll back the binding that was reopened
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
        result.retired_outgoing=state_->retired_outgoing;result.retired_accepted=state_->retired_accepted;
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
