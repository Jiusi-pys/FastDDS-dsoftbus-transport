#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
#include <fastdds_dsoftbus/dsoftbus/PublicSocketBackend.hpp>
#include <fastdds_dsoftbus/dsoftbus/DomainCircuitName.hpp>
#include <limits>
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
#include <fastdds_dsoftbus/dsoftbus/RuntimeProbeAccess.hpp>
#ifdef FASTDDS_DSOFTBUS_STARTUP_OBSERVATION
#include "../probes/PublicSocketRuntimeTrace.hpp"
#define STARTUP_CAPTURE(...) runtime_probe::TraceEvent(__VA_ARGS__)
#else
#define STARTUP_CAPTURE(...) ((void)0)
#endif
#endif
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#ifndef STARTUP_CAPTURE
#define STARTUP_CAPTURE(...) ((void)0)
#endif
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
namespace fastdds_dsoftbus::dsoftbus::phase0 {
namespace {
thread_local const std::function<bool()>* armed_pre_connect = nullptr;
thread_local const std::function<void()>* armed_domain_input_publication = nullptr;
}
ArmedDomainInputPublicationGate::ArmedDomainInputPublicationGate(std::function<void()> gate) noexcept
        :gate_(std::move(gate)),previous_(armed_domain_input_publication) {
    armed_domain_input_publication=&gate_;
}
ArmedDomainInputPublicationGate::~ArmedDomainInputPublicationGate(){armed_domain_input_publication=previous_;}
void InvokeArmedDomainInputPublicationGate() {
    const auto* gate=armed_domain_input_publication;
    if(gate&&*gate)(*gate)();
}
// Guard storage belongs to the common loop assembly, including probe variants
// that do not link the public Runtime archive. Only that Runtime's hook invokes
// this callable; other probe hooks retain their existing contracts.
bool InvokeArmedPreConnectGate() noexcept {
    const auto* gate=armed_pre_connect;
    if(!gate||!*gate)return true;
    try{const bool result=(*gate)();STARTUP_CAPTURE("GateDecision_result",UINT64_MAX,-1,-1,result?0:1,0);return result;}catch(...){STARTUP_CAPTURE("GateException_result",UINT64_MAX,-1,-1,1,0);return false;}
}
ArmedPreConnectGate::ArmedPreConnectGate(std::function<bool()> gate) noexcept
        :gate_(std::move(gate)),previous_(armed_pre_connect) {
    if(gate_)armed_pre_connect=&gate_;
}
ArmedPreConnectGate::~ArmedPreConnectGate(){armed_pre_connect=previous_;}
}
#endif
namespace fastdds_dsoftbus::dsoftbus::host_test {
using namespace phase0;
namespace {
struct ShutdownObserverFrame {
    const void* state;
    ShutdownObserverFrame* previous;
    static thread_local ShutdownObserverFrame* current;
    explicit ShutdownObserverFrame(const void* value) noexcept:state(value),previous(current){current=this;}
    ~ShutdownObserverFrame(){current=previous;}
    static bool Contains(const void* value) noexcept {
        for(auto* p=current;p;p=p->previous)if(p->state==value)return true;
        return false;
    }
};
thread_local ShutdownObserverFrame* ShutdownObserverFrame::current=nullptr;
// The same contract the shared input release enforces, for the path that has no input to
// refuse on: a member view released from a context that cannot close an input never gives
// its membership back, so the surviving views would compute a non-last departure, report
// success and leave the runtime unsealed with this view's egress registration unrevoked.
[[noreturn]] void RefuseSharedRuntimeRelease() noexcept
{
    std::fprintf(stderr,"Fast DDS DSoftBus shared runtime: a member view was destroyed from a context that "
            "cannot close an input; close every facade from an external control context (CloseRequest) "
            "before releasing it");
    std::fputc('\n',stderr);std::fflush(stderr);std::abort();
}
}

struct HostProviderLoop::View {
    bool attempted=false,started=false,member=false,closing=false,closed=false,input_closing=false;
    bool owner=false,facade_published=false;
    std::size_t active_tx=0,controls=0;
    std::mutex domain_control; // Serialize this view's preparation/binding, outside root gate.
    std::optional<std::uint32_t> explicit_expected_domain;
    std::uint64_t input_incarnation=0; // Monotone publication/retirement token; never wraps.
    EgressRegistration registration;
    std::shared_ptr<Circuit> circuit;
    std::shared_ptr<RuntimeSenderBinding> sender_binding;
    BindingInput input;
    InputChannels::Receiver* receiver=nullptr;
    // The active receiver identity, and the identity this view retired when it
    // closed its input. Keeping them apart is what lets an observation report a
    // closed view without presenting its retired identity as a live one.
    ReceiverIdentity identity{};
    ReceiverIdentity retired_identity{};
    std::function<void()> before_publish,after_seal;
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
    std::function<void(bool)> send_hook;
#endif
};
// Upper bound on concurrent Participants (views) one runtime serves; the runtime's own
// capacity is the starting descriptor's max_participants (default 2). Each view holds
// its own egress registration and one attachment application (logical_slots 8).
constexpr std::size_t kSharedViews=4;
struct HostProviderLoop::Circuit {
    std::uint32_t domain=0;
    NativeTableConfiguration config;
    ProviderAdapterDomain declarations;
    std::unique_ptr<NativeOwnerTable> native;
    std::unique_ptr<NativeSendExecution> execution;
    std::shared_ptr<PublicSocketBackend> backend;
    std::unique_ptr<ProviderAdapter> adapter;
    std::shared_ptr<NativeSendProvider> provider;
    std::mutex control; // External outgoing startup only; never held by SDK callbacks.
};
struct HostProviderLoop::State {
    State(std::shared_ptr<NativeBackend> p,std::shared_ptr<ProcessReceiveObserver> o):backend(std::move(p)),observer(std::move(o)){}
    const std::shared_ptr<NativeBackend> backend;
    const std::shared_ptr<ProcessReceiveObserver> observer;
    const std::shared_ptr<RuntimeWorkSignal> work=std::make_shared<RuntimeWorkSignal>();
    mutable std::mutex gate;
    mutable std::condition_variable changed;
    bool attempted=false,started=false,input_open=false,stopping=false,sealed=false,closed=false;
    std::size_t active=0;
    std::size_t active_rx=0;
    bool input_closing=false;
    std::uint64_t open_calls=0,close_calls=0,shutdown_calls=0,control_order=0;
    std::uint64_t first_close_order=0,first_shutdown_order=0;
    NativeTableConfiguration config;
    bool domain_mode=false;
    EgressPlan::Snapshot domain_plan;
    std::vector<EgressSelection> domain_selections;
    std::array<std::shared_ptr<Circuit>,2> circuits;
    std::mutex domain_setup;
    std::size_t active_native_sends=0;
    std::unique_ptr<NativeOwnerTable> native;
    std::unique_ptr<ApplicationAttachments> attachments;
    std::unique_ptr<NativeSendExecution> execution;
    EgressRegistration registration;
    ProviderAdapterDomain domain;
    std::unique_ptr<ProviderAdapter> adapter;
    // Owned by this loop, so only this runtime's connect can consult it.
    std::function<bool()> pre_connect;
    std::function<bool()> replacement_gate;
    std::function<void(const RuntimeShutdownPoint&)> before_native_close;
    bool shutdown_observer_installed=false,shutdown_observer_fault=false;
    std::shared_ptr<NativeSendProvider> send_provider;
    InputChannels::Receiver* receiver=nullptr;
    BackendCloseResult close;
    std::optional<BackendCloseResult> failed_open_cleanup;
    bool shared=false,opening=false,owner_pinned=false;
    bool exact_owner_admission=false,ever_facade_published=false;
    std::size_t live_views=0,next_tx=0,view_capacity=2;
    std::array<std::weak_ptr<View>,kSharedViews> views;
    SendQueueLimits shared_limits;
    struct PumpGuard {
        State& state;bool receive=false;
        ~PumpGuard(){std::lock_guard<std::mutex> lock(state.gate);--state.active;if(receive)--state.active_rx;state.changed.notify_all();}
    };
};
HostProviderLoop::HostProviderLoop(std::shared_ptr<NativeBackend> p, std::shared_ptr<ProcessReceiveObserver> o) : state_(new State(std::move(p),std::move(o))),view_(std::make_shared<View>()) {}
bool HostProviderLoop::EnableDomainCircuits() {
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    if(s.domain_mode)return !s.attempted&&!s.closed;
    if(!s.shared||s.attempted||s.closed)return false;
    auto backend=std::dynamic_pointer_cast<PublicSocketBackend>(s.backend);if(!backend)return false;
    const std::weak_ptr<State> weak=state_;
    if(!backend->SetCircuitResolver([weak](const BackendPeer& peer)->std::shared_ptr<PublicSocketBackend> {
        auto root=weak.lock();if(!root)return {};
        std::string_view base;std::uint32_t domain=0;
        if(!DecodeDomainCircuitName(peer.name,base,domain))return {};
        {std::lock_guard<std::mutex> lock(root->gate);
            if(!root->started||root->stopping||root->closed||root->config.directory.routes.size()!=1)return {};
            const auto& route=root->config.directory.routes.front().fields;
            if(peer.network_id!=route.remote_network_id||peer.data_type!=RecordDataType::Bytes||peer.package_name!=std::static_pointer_cast<PublicSocketBackend>(root->backend)->PackageName())return {};
            if(std::find(route.expected_remote_initiator_names.begin(),route.expected_remote_initiator_names.end(),base)==route.expected_remote_initiator_names.end())return {};
        }
        auto circuit=EnsureCircuit(root,domain);return circuit?circuit->backend:nullptr;
    }))return false;
    s.domain_mode=true;return true;
}
bool HostProviderLoop::RequiresDomainBinding() const noexcept {return state_->domain_mode;}
std::shared_ptr<HostProviderLoop::Circuit> HostProviderLoop::EnsureCircuit(const std::shared_ptr<State>& root,std::uint32_t domain) {
    auto& s=*root;std::lock_guard<std::mutex> lock(s.gate);
    if(!s.domain_mode||!s.started||s.stopping||s.closed)return {};
    std::size_t slot=s.circuits.size();
    for(std::size_t i=0;i<s.circuits.size();++i){if(s.circuits[i]&&s.circuits[i]->domain==domain)return s.circuits[i];if(!s.circuits[i]&&slot==s.circuits.size())slot=i;}
    if(slot==s.circuits.size())return {};
    auto circuit=std::make_shared<Circuit>();circuit->domain=domain;circuit->config=s.config;
    // Route renewal: each circuit owns its dial and accepted lanes, so after a
    // native shutdown (e.g. LINK_DOWN) it retires the settled owner and redials /
    // re-accepts with a fresh SDK ID. Root configuration is unchanged.
    circuit->config.bounded_reaccept_host=true;circuit->config.allow_closed_dial_retry=true;
    auto backend=std::dynamic_pointer_cast<PublicSocketBackend>(s.backend);if(!backend)return {};
    circuit->backend=backend->CreateCircuit(domain);if(!circuit->backend)return {};
    auto& route=circuit->config.directory.routes.front().fields;
    route.local_initiator_name=circuit->backend->Identity().service_name;
    for(auto& name:route.expected_remote_initiator_names){std::string encoded;if(!EncodeDomainCircuitName(name,domain,encoded))return {};name.swap(encoded);}
    auto native=NativeOwnerTable::Build(circuit->config);if(!native)return {};
    circuit->native=std::move(*native.value);circuit->execution=std::make_unique<NativeSendExecution>(*circuit->native);
    BindingRequest request;request.service_name=route.local_initiator_name;request.role=RecordRole::Client;
    request.config=circuit->config;request.local=s.config.directory.locals.front().local_locator;
    request.route=circuit->config.directory.routes.front().key;request.has_route=true;
    auto directory=EndpointDirectory::Build(circuit->config.directory);if(!directory.value)return {};
    request.directory=*directory.value;request.shared_input_channel=true;request.receive_observer=s.observer;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    auto built=RuntimeProbeAccess::Build(circuit->declarations,ServiceRecordTable::Process(),*circuit->native,*s.attachments,circuit->backend,request);
#else
    auto built=ProviderAdapter::Build(circuit->declarations,ServiceRecordTable::Process(),*circuit->native,*s.attachments,circuit->backend,request);
#endif
    if(built.status!=BackendStatus::None)return {};
    circuit->adapter=std::move(built.adapter);
    {   // Redial only after the configured peer has been continuously online for 1 s.
        const std::weak_ptr<PublicSocketBackend> weak_backend=circuit->backend;
        auto since=std::make_shared<std::optional<std::chrono::steady_clock::time_point>>();
        if(!circuit->adapter->SetBoundedReplacementGate([weak_backend,since]{
            auto b=weak_backend.lock();if(!b)return false;
            if(!b->PeerOnline()){since->reset();return false;}
            const auto now=std::chrono::steady_clock::now();
            if(!*since){*since=now;return false;}
            if(now-**since<std::chrono::seconds(1))return false;
            since->reset();return true;}))return {};
    }
    {   // A renewal redial takes the same root setup serialization as the first outgoing setup.
        // The root outlives the adapter: its Close joins the renewal worker first.
        State* root_state=root.get();
        if(!circuit->adapter->SetCircuitRenewalSerializer([root_state]{return std::unique_lock<std::mutex>(root_state->domain_setup,std::try_to_lock);}))return {};
    }
    if(circuit->adapter->PrepareCircuitAnchor()!=BackendStatus::None||!circuit->backend->SetIncomingCallbacks(circuit->adapter->IncomingCallbacks()))return {};
    s.circuits[slot]=circuit;return circuit;
}
bool HostProviderLoop::BindDomain(std::uint32_t domain,const std::shared_ptr<ViewDomainGuard>& guard) noexcept {
    auto& s=*state_;
    struct Control {State& s;View& v;bool armed=false;~Control(){if(armed){std::lock_guard<std::mutex> lock(s.gate);--v.controls;s.changed.notify_all();}}} control{s,*view_};
    // Destruction order matters: release the binding mutex, drain any unpublished
    // input, then refund controls. Close cannot free its proxy before that refund.
    BindingInput input;
    EgressRegistration registration;
    std::unique_lock<std::mutex> binding(view_->domain_control);
    InputChannels::Receiver* receiver=nullptr;std::uint64_t incarnation=0;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(!s.domain_mode||!guard||!view_->started||!view_->member||view_->closing||view_->closed||s.stopping)return false;
        if(view_->circuit)return view_->circuit->domain==domain&&view_->sender_binding&&view_->sender_binding->guard()==guard;
        receiver=view_->input_closing?nullptr:view_->receiver;incarnation=view_->input_incarnation;
        ++view_->controls;control.armed=true;
    }
    try {
        auto circuit=EnsureCircuit(state_,domain);if(!circuit)return false;
        std::shared_ptr<RuntimeSenderBinding> pending;
        {std::lock_guard<std::mutex> lock(s.gate);pending=view_->sender_binding;}
        if(!pending)pending=std::make_shared<RuntimeSenderBinding>(s.domain_plan,guard);
        if(pending->guard()!=guard)return false;
        auto built=circuit->execution->CreateRuntime(circuit->config,s.domain_selections,s.shared_limits,s.work);
        if(!built)return false;registration=std::move(*built.value);
        if(receiver){
            auto attached=circuit->adapter->AttachSharedInput(receiver);if(!attached)return false;input=std::move(*attached.value);
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
            // Exact post-Attach/pre-publication window; root gate is not held.
            // Exceptions use the existing transaction rollback and control drain.
            InvokeArmedDomainInputPublicationGate();
#endif
        }
        if(!circuit->execution->BindPendingSender(registration,pending))return false;
        const std::weak_ptr<State> weak_state=state_;const std::weak_ptr<View> weak_view=view_;
        const std::weak_ptr<Circuit> weak_circuit=circuit;
        if(!DSoftBusSenderResource::SetPreparation(pending,[weak_state,weak_view,weak_circuit](RuntimeSendPath::Clock::time_point deadline) {
            auto root=weak_state.lock();auto view=weak_view.lock();auto selected=weak_circuit.lock();
            if(!root||!view||!selected)return RuntimeSendStatus::RuntimeClosed;
            {std::lock_guard<std::mutex> lock(root->gate);
                if(!root->started||root->stopping||root->closed||!view->started||!view->member||view->closing||view->closed||view->circuit!=selected)return RuntimeSendStatus::RuntimeClosed;
                if(view->controls==std::numeric_limits<std::size_t>::max()||root->active==std::numeric_limits<std::size_t>::max())return RuntimeSendStatus::PreparationFailed;
                ++view->controls;++root->active;
                // Both last-facade and explicit Runtime close must drain setup.
            }
            struct Pin {State& s;View& v;~Pin(){std::lock_guard<std::mutex> lock(s.gate);--v.controls;--s.active;s.changed.notify_all();}} pin{*root,*view};
            if(RuntimeSendPath::Clock::now()>=deadline)return RuntimeSendStatus::DeadlineExpired;
            if(std::atomic_load_explicit(&selected->provider,std::memory_order_acquire))return RuntimeSendStatus::Accepted;
            if(ServiceRecordBinding::IsCallbackContext()||ProviderAdapter::IsProviderCallbackContext())return RuntimeSendStatus::PreparationFailed;
            // Preserve aggregate root-before-circuit setup serialization.
            std::unique_lock<std::mutex> setup(root->domain_setup,std::try_to_lock);
            if(!setup.owns_lock())return RuntimeSendStatus::UnresolvedConnection;
            std::unique_lock<std::mutex> startup(selected->control,std::try_to_lock);
            if(!startup.owns_lock())return RuntimeSendStatus::UnresolvedConnection;
            const auto status=selected->adapter->InitializeCircuitOutgoing(deadline);
            if(status==BackendStatus::NoWork)return RuntimeSendStatus::UnresolvedConnection;
            if(status!=BackendStatus::None)return RuntimeSendStatus::PreparationFailed;
            auto provider=selected->adapter->BindSendProvider(*selected->native);
            if(!provider)return RuntimeSendStatus::PreparationFailed;
            {std::lock_guard<std::mutex> lock(root->gate);
                if(root->stopping||root->closed||view->closing||view->closed)return RuntimeSendStatus::RuntimeClosed;
                std::atomic_store_explicit(&selected->provider,std::move(provider),std::memory_order_release);
            }
            return RuntimeSendStatus::Accepted;
        }))return false;
        const auto identity=input.Identity();
        {std::lock_guard<std::mutex> lock(s.gate);
            if(view_->closing||view_->closed||s.stopping||view_->input_incarnation!=incarnation||
                    (receiver&&(view_->input_closing||view_->receiver!=receiver)))return false;
            view_->sender_binding=std::move(pending);view_->registration=std::move(registration);
            view_->circuit=std::move(circuit);view_->input=std::move(input);view_->identity=identity;
        }
        s.work->Notify();return true;
    }catch(...){return false;}
}
NativeStatus HostProviderLoop::ObserveNodeOffline(const std::string& network_id) noexcept
{
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    if(!s.started||s.stopping||!s.native)return NativeStatus::ServiceClosing;
    return s.native->ObserveNodeOffline(network_id);
}
HostProviderLoop::HostProviderLoop(std::shared_ptr<State> s,std::shared_ptr<View> v):state_(std::move(s)),view_(std::move(v)){}
NativeRouteSnapshot HostProviderLoop::ObserveRoute() const noexcept {
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    if(!s.native||s.config.directory.routes.size()!=1)return {};
    return s.native->ObserveRoute(s.config.directory.routes.front().key);
}
HostProviderLoop::~HostProviderLoop() {
    if(Shutdown())return;
    // A refused shutdown leaves this view a runtime member. Failing loudly here is what
    // keeps the two release paths consistent: with an open input the release already
    // aborts, and without one it used to leak the runtime silently.
    bool member=false;
    {std::lock_guard<std::mutex> lock(state_->gate);member=view_->member;}
    if(member)RefuseSharedRuntimeRelease();
}
std::shared_ptr<HostProviderLoop> HostProviderLoop::CreateShared(std::shared_ptr<NativeBackend> backend,
        std::shared_ptr<ProcessReceiveObserver> observer) {
    auto loop=std::make_shared<HostProviderLoop>(std::move(backend),std::move(observer));
    loop->state_->shared=true;return loop;
}
std::shared_ptr<HostProviderLoop> HostProviderLoop::CreateOwnedShared(std::shared_ptr<NativeBackend> backend) {
    auto loop=CreateShared(std::move(backend));
    loop->state_->owner_pinned=true;loop->state_->exact_owner_admission=true;loop->view_->owner=true;
    return loop;
}
std::shared_ptr<HostProviderLoop> HostProviderLoop::CreateParticipantShared(std::shared_ptr<NativeBackend> backend,
        std::shared_ptr<ProcessReceiveObserver> observer) {
    auto loop=CreateShared(std::move(backend),std::move(observer));
    loop->state_->exact_owner_admission=true;loop->view_->owner=true;
    return loop;
}
std::shared_ptr<HostProviderLoop> HostProviderLoop::Join(std::shared_ptr<HostProviderLoop> owner) noexcept {
    auto& s=*state_;
    try {
        auto v=std::make_shared<View>();
        auto result=std::shared_ptr<HostProviderLoop>(new HostProviderLoop(state_,v));
        std::lock_guard<std::mutex> lock(s.gate);
        if(!s.shared||!s.started||s.stopping||s.closed||s.live_views>=s.view_capacity)return {};
        if(s.exact_owner_admission){
            if(!view_->owner||owner.get()!=this)return {};
            if(!s.live_views&&!s.owner_pinned&&s.ever_facade_published)return {};
        }else if(!s.live_views)return {};
        result->owner_keepalive_=std::move(owner);
        for(auto& slot:s.views){auto old=slot.lock();if(!old||!old->member){slot=v;v->member=true;++s.live_views;return result;}}
    }catch(...){return {};}
    return {};
}
bool HostProviderLoop::PublishFacade() noexcept {
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    if(!s.exact_owner_admission||view_->owner||!view_->member||view_->closing||view_->closed||s.stopping||s.closed)return false;
    view_->facade_published=true;s.ever_facade_published=true;return true;
}
bool HostProviderLoop::StartShared(const DSoftBusTransportDescriptor& d) noexcept {
    auto& s=*state_;bool joining=false;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(view_->attempted||view_->closed||s.stopping||s.closed)return false;
        view_->attempted=true;joining=view_->member;
    }
    if(!joining){
        if(!d.max_participants||d.max_participants>kSharedViews)return false;
        if(!StartRuntime(d))return false;
        std::lock_guard<std::mutex> lock(s.gate);
        view_->registration=std::move(s.registration);view_->member=true;view_->started=true;
        view_->explicit_expected_domain=d.expected_domain_id;
        s.views[0]=view_;s.live_views=1;s.shared_limits=d.send_limits;s.view_capacity=d.max_participants;return true;
    }
    try {
        const auto& expected=s.shared_limits;const auto& given=d.send_limits;
        if(d.maxMessageSize!=expected.max_record_bytes||given.max_record_bytes!=expected.max_record_bytes||
                given.peer_records!=expected.peer_records||given.peer_bytes!=expected.peer_bytes||
                given.global_records!=expected.global_records||given.global_bytes!=expected.global_bytes)
            {ShutdownShared();return false;}
        auto exact=s.config;exact.directory=d.directory;
        auto checked=s.native->AcquireNamespace(s.config.directory.locals[0].local_locator,exact);
        if(!checked){ShutdownShared();return false;}
        if(s.domain_mode){std::lock_guard<std::mutex> lock(s.gate);view_->explicit_expected_domain=d.expected_domain_id;view_->started=true;return true;}
        auto reg=s.execution->CreateRuntime(exact,d.egress_selections,d.send_limits,s.work);
        if(!reg){ShutdownShared();return false;}
        std::lock_guard<std::mutex> lock(s.gate);
        view_->registration=std::move(*reg.value);view_->started=true;return true;
    }catch(...){ShutdownShared();return false;}
}
InputOpenResult HostProviderLoop::OpenShared(const Locator& local,InputChannels::Receiver* receiver) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(!view_->started||view_->closing||view_->closed||s.stopping)return InputOpenResult::Disabled;
        if(local!=s.config.directory.locals[0].local_locator)return InputOpenResult::InvalidLocal;
        if(!receiver)return InputOpenResult::NullReceiver;
        if(view_->input_closing)return InputOpenResult::Closing;
        if(view_->receiver)return view_->receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
        ++view_->controls;
    }
    struct ControlPin {State& s;View& v;~ControlPin(){std::lock_guard<std::mutex> lock(s.gate);--v.controls;s.changed.notify_all();}} pin{s,*view_};
    bool initialize=false;
    {std::unique_lock<std::mutex> lock(s.gate);
        s.changed.wait(lock,[&]{return !s.opening||view_->closing;});
        if(view_->closing||s.stopping)return InputOpenResult::Closing;
        if(!s.adapter){s.opening=true;initialize=true;}
    }
    if(initialize){
        const auto opened=OpenRuntime(local,nullptr);
        {std::lock_guard<std::mutex> lock(s.gate);s.opening=false;s.changed.notify_all();}
        if(opened!=InputOpenResult::Opened)return opened;
    }
    if(s.domain_mode) {
        // A pending input may dispatch while Open is committing. Its rollback
        // drains after releasing domain_control, while this Open's pin still
        // prevents Close from releasing the prepared public receiver proxy.
        BindingInput input;
        std::unique_lock<std::mutex> binding(view_->domain_control);
        std::optional<std::uint32_t> expected;std::shared_ptr<Circuit> selected;
        std::uint64_t incarnation=0;
        {
            std::lock_guard<std::mutex> lock(s.gate);
            if(view_->closing||view_->closed||view_->input_closing||s.stopping)return InputOpenResult::Closing;
            if(view_->receiver)return view_->receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
            if(view_->input_incarnation==std::numeric_limits<std::uint64_t>::max())return InputOpenResult::Disabled;
            expected=view_->explicit_expected_domain;selected=view_->circuit;incarnation=view_->input_incarnation;
        }
        try {
            if(expected) {
                auto circuit=EnsureCircuit(state_,*expected);if(!circuit)return InputOpenResult::Disabled;
                // Expected-domain metadata may prepare the anchor, never dial.
                // Only the genuine DID callback publishes a bound view.
            }
            // Only a genuine earlier actual-DID transaction sets selected.
            // Preparation alone leaves Await without an application attachment.
            if(selected){auto attached=selected->adapter->AttachSharedInput(receiver);
                if(!attached)return attached.issue.code==NativeStatus::AllocationFailure?InputOpenResult::AllocationFailure:InputOpenResult::Conflict;
                input=std::move(*attached.value);
            }
        }catch(const std::bad_alloc&){return InputOpenResult::AllocationFailure;}
        catch(...){return InputOpenResult::Disabled;}
        const auto identity=input.Identity();
        std::lock_guard<std::mutex> lock(s.gate);
        if(view_->closing||view_->closed||view_->input_closing||s.stopping||view_->input_incarnation!=incarnation)return InputOpenResult::Closing;
        if(view_->receiver)return view_->receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
        view_->input=std::move(input);view_->identity=identity;view_->receiver=receiver;++view_->input_incarnation;
        return InputOpenResult::Opened;
    }
    auto input=s.adapter->AttachSharedInput(receiver);
    if(!input)return input.issue.code==NativeStatus::AllocationFailure?InputOpenResult::AllocationFailure:InputOpenResult::Conflict;
    try{if(view_->before_publish)view_->before_publish();}catch(...){return InputOpenResult::AllocationFailure;}
    const auto identity=input.value->Identity();
    std::lock_guard<std::mutex> lock(s.gate);
    if(view_->closing||view_->closed||s.stopping)return InputOpenResult::Closing;
    // A concurrent control can publish a receiver between the entry check and here. The
    // pending registration is dropped outside this gate rather than closing a live one
    // inside it: moving into a non-empty view_->input would run that close here, waiting
    // on receiver detach while holding the runtime gate. The local input is destroyed
    // after this guard is released.
    if(view_->receiver)return view_->receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
    view_->input=std::move(*input.value);view_->identity=identity;view_->receiver=receiver;return InputOpenResult::Opened;
}
bool HostProviderLoop::CloseSharedInput(const Locator& local) noexcept {
    if(!InputCloseAllowedOnThisThread())return false;
    auto& s=*state_;
    if(s.domain_mode) {
        BindingInput retired;
        InputChannels::Receiver* receiver=nullptr;ReceiverIdentity identity{};
        std::unique_lock<std::mutex> binding(view_->domain_control);
        {
            std::unique_lock<std::mutex> lock(s.gate);
            if(!s.started||local!=s.config.directory.locals[0].local_locator)return false;
            ++s.close_calls;if(!s.first_close_order)s.first_close_order=++s.control_order;
            if(view_->input_closing){
                binding.unlock();s.changed.wait(lock,[&]{return !view_->input_closing;});return view_->receiver==nullptr;
            }
            if(!view_->receiver)return true;
            view_->input_closing=true;++view_->controls;
            if(view_->input_incarnation!=std::numeric_limits<std::uint64_t>::max())++view_->input_incarnation;
            receiver=view_->receiver;identity=view_->identity;
            retired=std::move(view_->input);view_->receiver=nullptr;view_->identity=ReceiverIdentity{};
            s.changed.notify_all();
        }
        // No binding/root mutex during waits. An entered receiver may reenter
        // the same DID callback; it must not deadlock with its own retirement.
        binding.unlock();
        {std::unique_lock<std::mutex> lock(s.gate);s.changed.wait(lock,[&]{return view_->controls==1;});}
        const auto result=retired.Close();
        binding.lock();
        std::lock_guard<std::mutex> lock(s.gate);
        if(result==NativeStatus::None)view_->retired_identity=identity;
        else {view_->input=std::move(retired);view_->receiver=receiver;view_->identity=identity;}
        // Even a refused close never rewinds the token. The public proxy stays
        // owned when false is returned; a successful close never restores it.
        view_->input_closing=false;--view_->controls;s.changed.notify_all();return result==NativeStatus::None;
    }
    {std::unique_lock<std::mutex> lock(s.gate);
        if(!s.started||local!=s.config.directory.locals[0].local_locator)return false;
        ++s.close_calls;if(!s.first_close_order)s.first_close_order=++s.control_order;
        if(view_->input_closing){s.changed.wait(lock,[&]{return !view_->input_closing;});return view_->receiver==nullptr;}
        if(!view_->receiver)return true;
        view_->input_closing=true;++view_->controls;s.changed.notify_all();
    }
    const auto result=view_->input.Close();
    std::lock_guard<std::mutex> lock(s.gate);
    if(result==NativeStatus::None){
        // The receiver is gone, so its identity is no longer active. It is retained
        // as a retired identity rather than left in the active field, so that a
        // closed view can never be observed as still holding a live receiver.
        view_->retired_identity=view_->identity;view_->identity=ReceiverIdentity{};view_->receiver=nullptr;}
    view_->input_closing=false;--view_->controls;s.changed.notify_all();return result==NativeStatus::None;
}
bool HostProviderLoop::ShutdownShared() noexcept {
    if(!InputCloseAllowedOnThisThread())return false;
    auto& s=*state_;
    {std::unique_lock<std::mutex> lock(s.gate);
        if(view_->closed)return !s.closed||(s.close.status==BackendStatus::None&&s.close.adapter_drained&&!s.shutdown_observer_fault);
        if(view_->closing){
            // Wait for a definite state, not only for success. The owner either
            // publishes Closed, or publishes that it retained this view's receiver;
            // a waiter is never left on a latch that only the success path clears.
            s.changed.wait(lock,[&]{return view_->closed||!view_->closing;});
            if(!view_->closing)return false;
            return !s.closed||(s.close.status==BackendStatus::None&&s.close.adapter_drained&&!s.shutdown_observer_fault);
        }
        if(!view_->member){view_->closed=true;s.changed.notify_all();return true;}
        view_->closing=true;
        s.changed.notify_all();
    }
    if(view_->after_seal)view_->after_seal();
    {std::unique_lock<std::mutex> lock(s.gate);s.changed.wait(lock,[&]{return view_->controls==0;});}
    EgressRegistration registration;
    {std::lock_guard<std::mutex> lock(s.gate);registration=view_->registration;}
    if(registration){auto* execution=view_->circuit?view_->circuit->execution.get():s.execution.get();auto revoke=execution->BeginRevoke(registration);if(revoke)revoke.value->Progress();}
    if(!CloseSharedInput(s.config.directory.locals[0].local_locator)) {
        // This view keeps its receiver, its claim and its membership. Publish that
        // decision and wake every waiter, then report failure, so a legal close
        // failure retains responsibility and a later Shutdown() may retry. Nothing
        // is presented as Closed, and no receiver, claim or membership is given up here.
        // This view's own egress registration was already revoked above and is not
        // restored, so a retained view is receive-capable but no longer send-capable; a
        // retry re-runs the revocation, which is idempotent.
        std::lock_guard<std::mutex> lock(s.gate);
        view_->closing=false;
        s.changed.notify_all();
        return false;
    }
    bool last=false,owns_stop=false;
    {std::unique_lock<std::mutex> lock(s.gate);
        s.changed.wait(lock,[&]{return view_->active_tx==0;});
        view_->member=false;last=--s.live_views==0;
        // A failed first facade refunds only its reservation, leaving the unused
        // metadata seed available. After any publication, even an unpublished
        // last refund must close Participant lifetime. Pinned owners survive.
        if(s.owner_pinned||(s.exact_owner_admission&&!s.ever_facade_published))last=false;
        if(last&&!s.stopping){s.stopping=true;owns_stop=true;}
    }
    const bool result=!last||ShutdownRuntime(owns_stop);
    {std::lock_guard<std::mutex> lock(s.gate);view_->closed=true;s.changed.notify_all();}
    return result;
}
void HostProviderLoop::SetSharedControlHooks(std::function<void()> before_publish,std::function<void()> after_seal){
    view_->before_publish=std::move(before_publish);view_->after_seal=std::move(after_seal);
}
bool HostProviderLoop::Start(const DSoftBusTransportDescriptor& d) noexcept {
    if(view_->owner){
        if(!d.max_participants||d.max_participants>kSharedViews)return false;
        const auto result=StartRuntime(d);
        if(result){std::lock_guard<std::mutex> lock(state_->gate);view_->started=true;state_->shared_limits=d.send_limits;state_->view_capacity=d.max_participants;}
        if(result)state_->work->Notify();
        return result;
    }
    const auto result=state_->shared?StartShared(d):StartRuntime(d);
    if(result)state_->work->Notify();
    return result;
}
bool HostProviderLoop::SetPreConnectGate(std::function<bool()> gate) {
    auto& s=*state_;
    std::lock_guard<std::mutex> lock(s.gate);
    // Refuse once this loop owns a started runtime; never replace a gate that an
    // Initialize() call could already be using.
    if(s.attempted||s.started)return false;
    s.pre_connect=std::move(gate);
    return true;
}
bool HostProviderLoop::StartRuntime(const DSoftBusTransportDescriptor& d) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);if(s.attempted||s.closed){STARTUP_CAPTURE("LoopStart_StateRefused_result",UINT64_MAX,-1,-1,1,(s.attempted?1:0)|(s.closed?2:0));return false;}s.attempted=true;}
    // Only the attempt that changed attempted owns failure cleanup. A second
    // facade rejected above must never stop the first facade's live backend.
    struct Rollback {HostProviderLoop& loop;bool done=false;~Rollback(){if(!done)loop.ShutdownRuntime();}} rollback{*this};
    try {
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    constexpr auto admitted_kind=BackendKind::DSoftBusProvider;
#else
    constexpr auto admitted_kind=BackendKind::HostMock;
#endif
    if(!s.backend||s.backend->Kind()!=admitted_kind||d.directory.locals.size()!=1||d.directory.routes.size()!=1){STARTUP_CAPTURE("LoopStart_ConfigRefused_result",UINT64_MAX,-1,-1,1,0);return false;}
    // The descriptor's maxMessageSize is what Fast DDS is told it may send without
    // fragmenting. A backend that rejects anything smaller would accept the write
    // and then refuse it at the SDK, so refuse the mismatch at startup instead.
    if(const auto bound=s.backend->MaxMessageSize();bound!=0&&bound<d.maxMessageSize){STARTUP_CAPTURE("LoopStart_BackendBoundRefused_result",UINT64_MAX,-1,-1,1,0);return false;}
    if(s.domain_mode){auto plan=EgressPlan::Build(d.directory,d.egress_selections);if(!plan.value)return false;s.domain_plan=plan.value;s.domain_selections=d.egress_selections;}
    s.config.directory=d.directory;s.config.directions={s.backend->Topology()==BackendTopology::ListenerAndOutgoing?RouteDirection::Both:RouteDirection::Dial};s.config.service_pkg_name="mdds.host.loop";
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    s.config.service_pkg_name="com.kaihong.mdds";
#endif
    if(s.domain_mode)s.config.service_pkg_name=std::static_pointer_cast<PublicSocketBackend>(s.backend)->PackageName();
    s.config.dial_only_egress=s.backend->Topology()==BackendTopology::ListenerAndOutgoing;
    // Historical internal field names the shared bounded owner algorithm;
    // real selection requires its separate compile/config capability.
    s.config.bounded_reaccept_host=s.backend->BoundedReacceptExperiment();
    if(s.config.bounded_reaccept_host)s.config.allow_closed_dial_retry=true;
    auto native=NativeOwnerTable::Build(s.config);if(!native){STARTUP_CAPTURE("LoopStart_NativeStatus_result",UINT64_MAX,-1,-1,int(native.issue.code),0);return false;}s.native=std::move(*native.value);
    AttachmentLimits receive_limits;
    receive_limits.max_record_bytes=d.maxMessageSize;
    if(d.receive_records){receive_limits.local_records=d.receive_records;receive_limits.records=std::max(receive_limits.records,2*d.receive_records);}
    if(d.receive_bytes){receive_limits.local_bytes=d.receive_bytes;receive_limits.bytes=std::max(receive_limits.bytes,2*d.receive_bytes);}
    s.attachments=std::make_unique<ApplicationAttachments>(std::vector<Locator>{d.directory.locals[0].local_locator},receive_limits,s.work);
    s.execution=std::make_unique<NativeSendExecution>(*s.native);
    auto reg=s.execution->CreateRuntime(s.config,d.egress_selections,d.send_limits,s.work);if(!reg){STARTUP_CAPTURE("LoopStart_RegistrationRefused_result",UINT64_MAX,-1,-1,1,0);return false;}
    s.registration=std::move(*reg.value);
    std::lock_guard<std::mutex> lock(s.gate);s.started=true;rollback.done=true;return true;
    } catch (...) {STARTUP_CAPTURE("LoopStart_Exception_result",UINT64_MAX,-1,-1,1,0);return false;}
}
InputOpenResult HostProviderLoop::Open(const Locator& local, InputChannels::Receiver* receiver) noexcept {
    const auto result=state_->shared?OpenShared(local,receiver):OpenRuntime(local,receiver);
    // Delegate publication and its local lifetime guards have all completed.
    if(result==InputOpenResult::Opened)state_->work->Notify();
    return result;
}
InputOpenResult HostProviderLoop::OpenRuntime(const Locator& local, InputChannels::Receiver* receiver) noexcept {
    auto& s=*state_;
    struct NodeOpenActivity{State& s;bool armed=false;~NodeOpenActivity(){if(armed){std::lock_guard<std::mutex> lock(s.gate);--s.active;s.changed.notify_all();}}} node_open{s};
    {std::lock_guard<std::mutex> lock(s.gate);
        ++s.open_calls;
        if(!s.started||s.stopping){STARTUP_CAPTURE("LoopOpen_Disabled_result",UINT64_MAX,-1,-1,int(InputOpenResult::Disabled),0);return InputOpenResult::Disabled;}
        if(s.input_closing){STARTUP_CAPTURE("LoopOpen_Closing_result",UINT64_MAX,-1,-1,int(InputOpenResult::Closing),0);return InputOpenResult::Closing;}
        if(s.failed_open_cleanup){STARTUP_CAPTURE("LoopOpen_Disabled_result",UINT64_MAX,-1,-1,int(InputOpenResult::Disabled),0);return InputOpenResult::Disabled;}
        if(local!=s.config.directory.locals[0].local_locator){STARTUP_CAPTURE("LoopOpen_InvalidLocal_result",UINT64_MAX,-1,-1,int(InputOpenResult::InvalidLocal),0);return InputOpenResult::InvalidLocal;}
        if(!receiver&&!s.shared){STARTUP_CAPTURE("LoopOpen_NullReceiver_result",UINT64_MAX,-1,-1,int(InputOpenResult::NullReceiver),0);return InputOpenResult::NullReceiver;}
        if(s.input_open)return s.receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
        if(s.backend->RouteLifecycleEnabled()){
            if(s.active==std::numeric_limits<std::size_t>::max())return InputOpenResult::Disabled;
            ++s.active;node_open.armed=true;
        }
    }
    if(s.adapter) {
        const auto result=s.adapter->ReopenInput(receiver);
        if(result!=BackendStatus::None)return result==BackendStatus::AllocationFailure?InputOpenResult::AllocationFailure:InputOpenResult::Disabled;
        std::lock_guard<std::mutex> lock(s.gate);s.receiver=receiver;s.input_open=true;return InputOpenResult::Opened;
    }
    std::unique_ptr<ProviderAdapter> pending;
    const auto settle_pending=[&]() noexcept {
        if(pending) {
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
            STARTUP_CAPTURE("LocalCleanup_enter",UINT64_MAX,-1,-1,0,0);
#endif
            auto receipt=pending->Close();
            if(s.backend->RouteLifecycleEnabled()){
                try{const auto node=s.backend->UnsubscribeNodes();if(node!=BackendStatus::None)receipt.status=node;}
                catch(...){receipt.status=BackendStatus::ProviderException;}
            }
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
            STARTUP_CAPTURE("Close_primary_BackendStatus_result",UINT64_MAX,-1,-1,int(receipt.status),0);
            STARTUP_CAPTURE("Close_cleanup_BackendStatus_result",UINT64_MAX,-1,-1,int(receipt.cleanup_status),0);
            STARTUP_CAPTURE("Close_adapter_drained_result",UINT64_MAX,-1,-1,receipt.adapter_drained?0:1,0);
#endif
            std::lock_guard<std::mutex> lock(s.gate);s.failed_open_cleanup=receipt;
        }
    };
    try {
    BindingRequest request;request.service_name=s.backend->Identity().service_name;
    request.role=RecordRole::Client;request.local=local;request.route=s.config.directory.routes[0].key;request.has_route=true;
    request.config=s.config;
    const auto directory=EndpointDirectory::Build(s.config.directory);
    if(!directory.value)return InputOpenResult::InvalidLocal;
    request.directory=*directory.value;request.receiver=receiver;
    request.receive_observer=s.observer;
    request.shared_input_channel=s.shared;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    auto result=RuntimeProbeAccess::Build(s.domain,ServiceRecordTable::Process(),*s.native,*s.attachments,s.backend,request);
#else
    auto result=ProviderAdapter::Build(s.domain,ServiceRecordTable::Process(),*s.native,*s.attachments,s.backend,request);
#endif
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
    STARTUP_CAPTURE("AdapterBuild_BackendStatus_result",UINT64_MAX,-1,-1,int(result.status),0);
#endif
    if(result.status!=BackendStatus::None)return InputOpenResult::Conflict;
    // Unpublished ownership stays local until all steps succeed. Every failure
    // releases only this pending binding before false reaches the receiver owner.
    pending=std::move(result.adapter);
    if(s.replacement_gate&&!pending->SetBoundedReplacementGate(s.replacement_gate))return InputOpenResult::Disabled;
    {
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
        // Armed only for this call, on this thread: the gate runs outside every
        // management lock below it and cannot be reached by another runtime.
        std::function<bool()> gate;
        {std::lock_guard<std::mutex> lock(s.gate);gate=s.pre_connect;}
        ArmedPreConnectGate armed(std::move(gate));
#endif
        const auto initialized=s.domain_mode?pending->InitializeListenerOnly():pending->Initialize();
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
        STARTUP_CAPTURE("Initialize_BackendStatus_result",UINT64_MAX,-1,-1,int(initialized),0);
#endif
        const auto published=initialized==BackendStatus::None?pending->Publish():initialized;
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
        if(initialized==BackendStatus::None)STARTUP_CAPTURE("Publish_BackendStatus_result",UINT64_MAX,-1,-1,int(published),0);
#endif
        if(initialized!=BackendStatus::None||published!=BackendStatus::None) {
            settle_pending();return InputOpenResult::Disabled;
        }
        if(s.backend->RouteLifecycleEnabled()) {
            const std::weak_ptr<State> weak=state_;
            const auto subscribed=s.backend->SubscribeNodes([weak](bool online,std::string_view network){
                if(auto current=weak.lock()) {
                    std::lock_guard<std::mutex> lock(current->gate);
                    if(current->stopping||current->closed||!current->native)return;
                    const auto processed=online?current->native->ObserveNodeOnline(network):current->native->ObserveNodeOffline(network);
                    STARTUP_CAPTURE(online?"NodeOnline_processed":"NodeOffline_processed",UINT64_MAX,-1,-1,int(processed),0);
                }
            });
            if(subscribed!=BackendStatus::None){settle_pending();return InputOpenResult::Disabled;}
        }
    }
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    auto provider=RuntimeProbeAccess::SendBridge(*pending,*s.native);
#else
    auto provider=pending->BindSendProvider(*s.native);
#endif
    if(!provider){settle_pending();return InputOpenResult::Disabled;}
    {
        std::unique_lock<std::mutex> lock(s.gate);
        // Initialize/Publish ran outside the gate, so a Shutdown may have sealed and
        // closed the runtime meanwhile (it saw no adapter and reported success).
        // Publishing now would install a live adapter nothing will ever drain.
        if(!s.stopping&&!s.closed) {
            s.adapter=std::move(pending);s.send_provider=std::move(provider);s.receiver=receiver;s.input_open=true;return InputOpenResult::Opened;
        }
    }
    settle_pending();return InputOpenResult::Disabled;
    } catch(const std::bad_alloc&) {settle_pending();return InputOpenResult::AllocationFailure;}
      catch(...) {settle_pending();return InputOpenResult::Disabled;}
}
bool HostProviderLoop::CloseInput(const Locator& local) noexcept {
    if(state_->shared)return CloseSharedInput(local);
    auto& s=*state_;
    {
        std::lock_guard<std::mutex> lock(s.gate);
        ++s.close_calls;if(!s.first_close_order)s.first_close_order=++s.control_order;
        if(!s.started||local!=s.config.directory.locals[0].local_locator)return false;
        if(!s.input_open)return true;
        s.input_closing=true;s.input_open=false;s.changed.notify_all();
    }
    const auto result=s.adapter->CloseInput();
    std::unique_lock<std::mutex> lock(s.gate);
    if(result!=BackendStatus::None){s.input_open=true;s.input_closing=false;s.changed.notify_all();return false;}
    s.changed.wait(lock,[&]{return s.active_rx==0;});
    s.receiver=nullptr;s.input_closing=false;s.changed.notify_all();return true;
}
SenderAttachResult HostProviderLoop::Attach(eprosima::fastdds::rtps::SendResourceList& list) {
    return AttachWithDomainGuard(list, {});
}
SenderAttachResult HostProviderLoop::AttachWithDomainGuard(eprosima::fastdds::rtps::SendResourceList& list,
        const std::shared_ptr<ViewDomainGuard>& guard) {
    auto& s=*state_;
    if(s.shared){
        if(s.domain_mode) {
            std::shared_ptr<RuntimeSenderBinding> pending;
            {std::lock_guard<std::mutex> lock(s.gate);
                if(!view_->started||view_->closing||view_->closed||s.stopping||!guard)return {SenderAttachStatus::RuntimeClosed};
                if(!view_->sender_binding)view_->sender_binding=std::make_shared<RuntimeSenderBinding>(s.domain_plan,guard);
                pending=view_->sender_binding;
            }
            if(pending->guard()!=guard)return {SenderAttachStatus::DomainGuardConflict};
            return DSoftBusSenderResource::AttachPending(list,pending);
        }
        EgressRegistration registration;
        {std::lock_guard<std::mutex> lock(s.gate);if(!view_->started||view_->closing||view_->closed||s.stopping)return {SenderAttachStatus::RuntimeClosed};registration=view_->registration;}
        return s.execution->Attach(registration,list,guard);
    }
    {std::lock_guard<std::mutex> lock(s.gate);if(!s.started||s.stopping)return {SenderAttachStatus::RuntimeClosed};}
    return s.execution->Attach(s.registration,list,guard);
}
NativeSendRunResult HostProviderLoop::PumpSend() noexcept {
    return PumpSendImpl(nullptr);
}
NativeSendRunResult HostProviderLoop::PumpSendObserved(HostSendObservation& observation) noexcept {
    observation={};return PumpSendImpl(&observation);
}
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
void HostProviderLoop::SetSharedSendHook(std::function<void(bool)> hook) {
    view_->send_hook=std::move(hook); // Installed before starting test workers.
}
#endif
NativeSendRunResult HostProviderLoop::PumpSendImpl(HostSendObservation* observation) noexcept {
    auto& s=*state_;
    {   // Domain-circuit route renewal. Runs before view gating: in owned/persistent
        // assemblies the runtime-level pump has no started view of its own.
        std::array<std::shared_ptr<Circuit>,2> renew;bool live=false;
        {std::lock_guard<std::mutex> lock(s.gate);live=s.domain_mode&&s.started&&!s.stopping&&!s.closed;if(live){renew=s.circuits;++s.active;}}
        if(live){State::PumpGuard pin{s,false};for(const auto& circuit:renew)if(circuit&&circuit->adapter&&circuit->adapter->CircuitRenewalPending())(void)circuit->adapter->AdvanceCircuitRenewal();}
    }
    if(s.shared){
        std::array<std::shared_ptr<View>,kSharedViews> eligible;std::size_t count=0;
        {std::lock_guard<std::mutex> lock(s.gate);
            // The runtime owns the send queue, so driving it is a runtime-level
            // capability. Who may drive it is one rule shared with PumpReceive: a view
            // that actually started belongs to this runtime and may keep advancing it
            // even after it closes, because the retained controller is the only
            // progress owner during the final drain; a view whose Start never
            // succeeded is not a member here and must not drive this runtime's work on
            // another view's behalf.
            if(!view_->started)return {NativeStatus::WrongState};
            if(s.stopping||(!s.domain_mode&&!s.send_provider))return {NativeStatus::ServiceClosing};
            for(std::size_t i=0;i<s.views.size();++i){auto v=s.views[(s.next_tx+i)%s.views.size()].lock();if(v&&v->started&&!v->closing&&!v->closed)eligible[count++]=std::move(v);}
            s.next_tx=(s.next_tx+1)%s.views.size();++s.active;
        }
        State::PumpGuard pump{s};
        for(const auto& v:eligible)if(v){
            EgressRegistration registration;ReceiverIdentity identity;std::shared_ptr<Circuit> circuit;
            {std::lock_guard<std::mutex> lock(s.gate);if(v->closing||v->closed)continue;++v->active_tx;registration=v->registration;circuit=v->circuit;identity=v->identity==ReceiverIdentity{}?v->retired_identity:v->identity;}
            struct Pin {State& s;View& v;~Pin(){std::lock_guard<std::mutex> lock(s.gate);--v.active_tx;s.changed.notify_all();}} pin{s,*v};
            // Unbound domain views have no circuit work; root input is not a send prerequisite.
            if(s.domain_mode&&!circuit)continue;
            // Preserve the existing no-work/Take-failure behavior. This receipt
            // describes only successfully taken work, not every Take rejection.
            bool send_credit=false;
            if(s.domain_mode){std::lock_guard<std::mutex> lock(s.gate);if(s.active_native_sends==s.config.limits.send_calls)continue;++s.active_native_sends;send_credit=true;}
            struct SendCredit {State& s;bool active;~SendCredit(){if(active){std::lock_guard<std::mutex> lock(s.gate);--s.active_native_sends;s.changed.notify_all();}}} credit{s,send_credit};
            auto* execution=circuit?circuit->execution.get():s.execution.get();
            auto provider=circuit?std::atomic_load_explicit(&circuit->provider,std::memory_order_acquire):s.send_provider;
            // Refuse before Take so a missing provider cannot consume queued work.
            if(!execution)return {NativeStatus::ServiceClosing};
            // A confirmed receive-only circuit has no outgoing provider until
            // its first Send. It owns no takeable TX; keep scanning other views.
            if(!provider){if(circuit)continue;return {NativeStatus::ServiceClosing};}
            auto work=execution->Take(registration);if(!work)continue;
            if(observation){observation->selected=true;observation->receiver=identity;}
            const auto observe=[&]{if(!observation)return;
                {std::lock_guard<std::mutex> lock(s.gate);observation->view_closing=v->closing;}
                const auto activity=execution->Observe(registration);
                observation->registration_status=activity.status;observation->admission_closed=activity.admission_closed;
            };
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
            if(v->send_hook)v->send_hook(false);
#endif
            auto invocation=execution->TryAdmit(*work.value,provider);
            if(!invocation){observe();return {invocation.issue.code};}
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
            if(v->send_hook)v->send_hook(true);
#endif
            auto result=invocation.value->Run();observe();return result;
        }
        return {NativeStatus::NotFound};
    }
    {
        std::lock_guard<std::mutex> lock(s.gate);
        if(!s.send_provider||s.stopping)return {NativeStatus::ServiceClosing};
        ++s.active;
    }
    // Declared before work/invocation: their destructors run before pump credit
    // is returned, including every early error/cancellation path.
    State::PumpGuard guard{s};
    auto work=s.execution->Take(s.registration);if(!work)return {work.issue.code};
    auto invocation=s.execution->TryAdmit(*work.value,s.send_provider);
    if(!invocation)return {invocation.issue.code};
    return invocation.value->Run();
}
BackendStatus HostProviderLoop::PumpReceive() noexcept {
    auto& s=*state_;
    {   // Domain-circuit route renewal. Runs before view gating: in owned/persistent
        // assemblies the runtime-level pump has no started view of its own.
        std::array<std::shared_ptr<Circuit>,2> renew;bool live=false;
        {std::lock_guard<std::mutex> lock(s.gate);live=s.domain_mode&&s.started&&!s.stopping&&!s.closed;if(live){renew=s.circuits;++s.active;}}
        if(live){State::PumpGuard pin{s,false};for(const auto& circuit:renew)if(circuit&&circuit->adapter&&circuit->adapter->CircuitRenewalPending())(void)circuit->adapter->AdvanceCircuitRenewal();}
    }
    if(s.shared)
    {
        // The record and the accepted socket belong to the runtime, so this stays a
        // runtime-level pump, gated by the same rule as PumpSend: a view that
        // actually started may drive it (including after it closes, so Q and the
        // final drain never lose their only progress owner), while a view whose
        // Start never succeeded may not drive this runtime's record at all.
        std::lock_guard<std::mutex> lock(s.gate);
        if(!view_->started||!s.input_open||s.stopping)return BackendStatus::InvalidState;
        ++s.active;++s.active_rx;
    }
    else
    {
        std::lock_guard<std::mutex> lock(s.gate);
        if(!s.input_open||s.stopping)return BackendStatus::InvalidState;
        ++s.active;++s.active_rx;
    }
    State::PumpGuard guard{s,true};
    if(s.domain_mode) {
        std::array<std::shared_ptr<Circuit>,2> circuits;
        {std::lock_guard<std::mutex> lock(s.gate);circuits=s.circuits;}
        std::array<NativeOwnerTable*,2> tables{};
        for(std::size_t i=0;i<tables.size();++i)if(circuits[i])tables[i]=circuits[i]->native.get();
        return s.adapter->DispatchAuthorized(tables);
    }
    return s.adapter->DispatchOne();
}
BackendStatus HostProviderLoop::RequestNormalEnd(RecordRole role) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(s.shared||!s.input_open||s.stopping||!s.adapter)return BackendStatus::InvalidState;
        ++s.active;
    }
    State::PumpGuard guard{s};
    return s.adapter->RequestNormalEnd(role);
}
std::uint64_t HostProviderLoop::WorkGeneration() const noexcept
{ return state_->work->Generation(); }
WorkWaitResult HostProviderLoop::WaitForWork(std::uint64_t& generation)
{
    // Those separate, default-off experiments have non-queue control work
    // (native lease settlement/replacement and node resolution). Until they
    // publish all of those transitions, a sleeping driver must refuse them.
    if(state_->backend&&(state_->backend->BoundedReacceptExperiment()||state_->backend->RouteLifecycleEnabled()))
        return WorkWaitResult::UnsupportedConfiguration;
    // Waiting is not an active pump: natural Transport shutdown must never
    // wait for an idle worker which is itself waiting for shutdown notification.
    // Owner retains this loop; Start/Close never replace or reset attachments.
    ApplicationAttachments* attachments=nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(state_->started)attachments=state_->attachments.get();
    }
    auto deadline=attachments?attachments->NextReceiveDeadline():ProcessClock::Clock::time_point::max();
    {   // A pending circuit renewal waits on peer reachability, which signals no
        // queue work; bound the sleep so the pump keeps polling it.
        std::array<std::shared_ptr<Circuit>,2> circuits;
        {std::lock_guard<std::mutex> lock(state_->gate);if(state_->domain_mode&&state_->started&&!state_->stopping)circuits=state_->circuits;}
        for(const auto& circuit:circuits)if(circuit&&circuit->adapter&&circuit->adapter->CircuitRenewalPending()){
            deadline=std::min(deadline,ProcessClock::Clock::now()+std::chrono::milliseconds(250));break;}
    }
    return state_->work->Wait(generation,deadline);
}
void HostProviderLoop::RequestPumpStop() noexcept
{ state_->work->RequestStop(); }
bool HostProviderLoop::SetBoundedReplacementGate(std::function<bool()> gate){
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    if(s.shared||s.attempted||s.started)return false;
    s.replacement_gate=std::move(gate);return true;
}
void HostProviderLoop::SealBoundedAdmission() noexcept {
    ProviderAdapter* adapter=nullptr;
    {std::lock_guard<std::mutex> lock(state_->gate);if(state_->backend->BoundedReacceptExperiment())adapter=state_->adapter.get();}
    if(adapter)adapter->SealIngress();
}
bool HostProviderLoop::Shutdown() noexcept {
    if(view_->owner)return ShutdownOwner();
    return state_->shared?ShutdownShared():ShutdownRuntime();
}
bool HostProviderLoop::ShutdownOwner() noexcept {
    if(!InputCloseAllowedOnThisThread())return false;
    auto& s=*state_;bool owns_stop=false;
    {std::lock_guard<std::mutex> lock(s.gate);
        // Reservations and unused published facades both hold the owner busy.
        if(s.live_views)return false;
        // A concurrent last-view departure may already own stop. Only the gate
        // transition elects a closer; every other caller waits for its receipt.
        if(!s.closed&&!s.stopping){s.owner_pinned=false;s.stopping=true;owns_stop=true;}
    }
    return ShutdownRuntime(owns_stop);
}
bool HostProviderLoop::SetBeforeNativeClose(std::function<void(const RuntimeShutdownPoint&)> observer) {
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.gate);
    const bool participant_owner=s.exact_owner_admission&&view_->owner&&!s.owner_pinned;
    if(!observer||(s.shared&&!participant_owner)||s.attempted||s.stopping||s.closed||s.shutdown_observer_installed)return false;
    s.before_native_close=std::move(observer);s.shutdown_observer_installed=true;return true;
}
bool HostProviderLoop::ShutdownRuntime(bool owns_stop) noexcept {
    if(!InputCloseAllowedOnThisThread())return false;
    const auto pin=state_;auto& s=*pin;
    {
        std::unique_lock<std::mutex> lock(s.gate);
        ++s.shutdown_calls;if(!s.first_shutdown_order)s.first_shutdown_order=++s.control_order;
        // Before stopping->wait: detect direct and A->B->A observer reentry.
        if(ShutdownObserverFrame::Contains(&s)){s.shutdown_observer_fault=true;return false;}
        if(s.closed)return s.close.status==BackendStatus::None&&!s.shutdown_observer_fault;
        if(s.stopping&&!owns_stop){s.changed.wait(lock,[&]{return s.closed;});return s.close.status==BackendStatus::None&&!s.shutdown_observer_fault;}
        s.stopping=true;
    }
    if(s.execution&&s.registration) {
        auto revoke=s.execution->BeginRevoke(s.registration);
        if(revoke)revoke.value->Progress();
    }
    if(s.adapter)s.adapter->SealIngress();
    BackendStatus node_close=BackendStatus::None;
    if(s.backend->RouteLifecycleEnabled()) {
        if(s.native)s.native->RequestServiceClose();
        try{node_close=s.backend->UnsubscribeNodes();}catch(...){node_close=BackendStatus::ProviderException;}
    }
    RuntimeShutdownPoint point;
    {
        std::unique_lock<std::mutex> lock(s.gate);s.sealed=true;s.changed.notify_all();
        s.changed.wait(lock,[&]{return s.active==0;});
        point={s.stopping,s.sealed,s.active};
    }
    if(s.backend->RouteLifecycleEnabled()){
        // A registration that was in flight at seal reconciles before active
        // Open exits. Read its sticky cleanup result after that lifetime settles.
        try{const auto late=s.backend->UnsubscribeNodes();if(late!=BackendStatus::None)node_close=late;}
        catch(...){node_close=BackendStatus::ProviderException;}
    }
    if(s.before_native_close) {
        ShutdownObserverFrame frame(&s);
        // The callable is immutable after installation; do not allocate a copy
        // in this noexcept path. No loop/adapter/ownership lock held here.
        try{s.before_native_close(point);}
        catch(...){std::lock_guard<std::mutex> lock(s.gate);s.shutdown_observer_fault=true;}
    }
    // failed_open_cleanup is written under the gate by an Open that lost the race with this
    // Shutdown, so it is read as a snapshot under the gate too. A cleanup failure recorded after
    // this snapshot is not folded into `close`; it stays visible only through Observe().
    std::optional<BackendCloseResult> failed_cleanup;
    {std::lock_guard<std::mutex> lock(s.gate);failed_cleanup=s.failed_open_cleanup;}
    BackendCloseResult circuit_result{BackendStatus::None,true};
    if(s.domain_mode)for(const auto& circuit:s.circuits)if(circuit) {
        circuit->adapter->SealIngress();
        const auto closed=circuit->adapter->Close();
        if(closed.status!=BackendStatus::None)circuit_result.status=closed.status;
        circuit_result.adapter_drained=circuit_result.adapter_drained&&closed.adapter_drained;
        if(closed.cleanup_status!=BackendStatus::None)circuit_result.cleanup_status=closed.cleanup_status;
    }
    auto result=s.adapter?s.adapter->Close():
        failed_cleanup.value_or(BackendCloseResult{BackendStatus::None,true});
    if(circuit_result.status!=BackendStatus::None)result.status=circuit_result.status;
    result.adapter_drained=result.adapter_drained&&circuit_result.adapter_drained;
    if(circuit_result.cleanup_status!=BackendStatus::None)result.cleanup_status=circuit_result.cleanup_status;
    if(node_close!=BackendStatus::None)result.status=node_close;
    bool success=false;
    {std::lock_guard<std::mutex> lock(s.gate);s.close=result;s.input_open=false;s.closed=true;s.changed.notify_all();
        success=result.status==BackendStatus::None&&result.adapter_drained&&!s.shutdown_observer_fault;
        if(!success){std::fprintf(stderr,"FDDST_SHUTDOWN_RUNTIME_NOT_CLEAN status=%d drained=%d observer_fault=%d node_close=%d\n",static_cast<int>(result.status),result.adapter_drained?1:0,s.shutdown_observer_fault?1:0,static_cast<int>(node_close));std::fflush(stderr);}}
    s.work->Close();
    return success;
}
bool HostProviderLoop::InputCloseAllowedOnThisThread() const noexcept {
    return !ServiceRecordBinding::IsCallbackContext()&&!ProviderAdapter::IsProviderCallbackContext();
}
BackendStatus HostProviderLoop::CloseAccepted() noexcept {
    if(!InputCloseAllowedOnThisThread())return BackendStatus::InvalidState;
    ProviderAdapter* adapter=nullptr;
    {std::lock_guard<std::mutex> lock(state_->gate);adapter=state_->adapter.get();}
    return adapter?adapter->CloseAccepted():BackendStatus::InvalidState;
}
DomainCircuitObservations HostProviderLoop::ObserveDomainCircuits() const noexcept {
    DomainCircuitObservations result;
    std::array<std::shared_ptr<Circuit>,2> pinned;
    const auto root=state_;
    {
        std::lock_guard<std::mutex> lock(root->gate);
        result.root_closed=root->closed;
        if(!root->domain_mode)return result;
        if(!root->started){result.status=DomainCircuitObservationStatus::NotStarted;return result;}
        result.status=DomainCircuitObservationStatus::Available;
        result.terminal_adapters_available=root->closed&&root->close.status==BackendStatus::None&&
            root->close.cleanup_status==BackendStatus::None&&root->close.adapter_drained&&!root->shutdown_observer_fault;
        pinned=root->circuits;
    }
    // EnsureCircuit publishes each fully prepared adapter/native pointer once
    // under the root gate. Pins retain them, but their observations are separate
    // instants. Native usage has its own lock. Adapter::Observe reads its first
    // binding pointer without a setup lock, so only successful terminal roots
    // may expose it: setup controls and every adapter Close have then settled.
    // A late callback cannot start a new binding after Closed. No control mutex
    // is held across Observe, and active/failed roots never fabricate Empty.
    for(std::size_t i=0;i<pinned.size();++i)if(pinned[i]) {
        DomainCircuitObservation value;value.domain=pinned[i]->domain;
        if(pinned[i]->native)value.native=pinned[i]->native->SnapshotUsage();
        if(result.terminal_adapters_available&&pinned[i]->adapter)value.adapter=pinned[i]->adapter->Observe();
        result.entries[i]=std::move(value);++result.count;
    }
    return result;
}
HostLoopObservation HostProviderLoop::Observe() const noexcept {
    auto& s=*state_;HostLoopObservation v;
    // Start assigns these members before it sets `started` under the gate, and Open
    // assigns the adapter under the gate; read them only through a snapshot taken
    // there so Observe never races a half-built runtime.
    const NativeOwnerTable* native=nullptr;const ApplicationAttachments* attachments=nullptr;
    const NativeSendExecution* execution=nullptr;const EgressRegistration* registration_ptr=nullptr;ProviderAdapter* adapter=nullptr;
    {std::lock_guard<std::mutex> lock(s.gate);adapter=s.adapter.get();
        if(s.started){native=s.native.get();attachments=s.attachments.get();execution=s.execution.get();registration_ptr=&s.registration;}
        v.started=s.started;v.input_open=s.input_open;v.input_closing=s.input_closing;v.sealed=s.sealed;v.closed=s.closed;v.active_pumps=s.active;v.close=s.close;v.failed_open_cleanup=s.failed_open_cleanup;
        v.open_calls=s.open_calls;v.close_calls=s.close_calls;v.shutdown_calls=s.shutdown_calls;v.first_close_order=s.first_close_order;v.first_shutdown_order=s.first_shutdown_order;v.shutdown_observer_fault=s.shutdown_observer_fault;}
    // A shared runtime moves its original registration into the first view, so this
    // state-level handle is deliberately empty there: it must never be published as
    // a per-view zero. Each shared view reports its own registration below.
    // `shared` is fixed before the loop is published; test it first, because a shared runtime moves its
    // registration handle out under the gate (StartShared) and reading it here would race that move.
    if(!s.shared&&registration_ptr&&*registration_ptr){v.tx=registration_ptr->SnapshotUsage();v.send=execution->Observe(*registration_ptr);}
    if(attachments)v.receive=attachments->SnapshotUsage();
    if(native)v.native=native->SnapshotUsage();
    if(adapter)v.adapter=adapter->Observe();
    if(s.shared){
        EgressRegistration registration;
        std::array<EgressRegistration,kSharedViews> all;
        {std::lock_guard<std::mutex> lock(s.gate);v.shared_runtime=true;v.live_views=s.live_views;v.receiver_identity=view_->identity;v.retired_receiver_identity=view_->retired_identity;v.input_open=view_->receiver!=nullptr;v.input_closing=view_->input_closing;v.view_controls=view_->controls;v.view_closing=view_->closing;registration=view_->registration;for(std::size_t i=0;i<s.views.size();++i)if(auto live=s.views[i].lock())all[i]=live->registration;}
        std::array<std::shared_ptr<Circuit>,2> circuits;
        std::shared_ptr<Circuit> selected;
        std::array<std::shared_ptr<Circuit>,kSharedViews> owners;
        {std::lock_guard<std::mutex> lock(s.gate);circuits=s.circuits;selected=view_->circuit;for(std::size_t i=0;i<s.views.size();++i)if(auto live=s.views[i].lock())owners[i]=live->circuit;}
        if(registration){v.tx=registration.SnapshotUsage();v.send=(selected?selected->execution.get():s.execution.get())->Observe(registration);}
        for(std::size_t i=0;i<all.size();++i)if(all[i])v.runtime_admitted_outstanding+=(owners[i]?owners[i]->execution.get():s.execution.get())->Observe(all[i]).admitted_outstanding;
        if(s.domain_mode)for(const auto& circuit:circuits)if(circuit) {
            const auto n=circuit->native->SnapshotUsage();
            v.native.namespaces+=n.namespaces;v.native.owners+=n.owners;v.native.owned_ids+=n.owned_ids;v.native.callbacks+=n.callbacks;v.native.receives+=n.receives;
            v.native.owner_high_water+=n.owner_high_water;v.native.generation_high_water=std::max(v.native.generation_high_water,n.generation_high_water);
            for(std::size_t i=0;i<n.phases.size();++i)v.native.phases[i]+=n.phases[i];
            v.native.setup.reserved+=n.setup.reserved;v.native.setup.entered+=n.setup.entered;
            v.native.send.reserved+=n.send.reserved;v.native.send.entered+=n.send.entered;
            v.native.close.reserved+=n.close.reserved;v.native.close.entered+=n.close.entered;
            v.native.entered_context_lost=v.native.entered_context_lost||n.entered_context_lost;
        }
    }
    return v;
}
bool HostProviderLoop::WaitSealed(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(state_->gate);
    return state_->changed.wait_until(lock,deadline,[&]{return state_->sealed;});
}
bool HostProviderLoop::WaitInputClosing(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(state_->gate);
    return state_->changed.wait_until(lock,deadline,[&]{return state_->shared?view_->input_closing:state_->input_closing;});
}
}
namespace fastdds_dsoftbus::dsoftbus {
std::unique_ptr<DSoftBusTransport> HostFacadeTestAccess::Create(const DSoftBusTransportDescriptor& d,
        std::shared_ptr<HostFacadeBackend> backend) {
    return std::unique_ptr<DSoftBusTransport>(new DSoftBusTransport(d,DSoftBusTransport::HostBackendTag{},std::move(backend)));
}
}
