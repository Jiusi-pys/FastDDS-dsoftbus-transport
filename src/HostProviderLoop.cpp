#include <fastdds_dsoftbus/dsoftbus/HostProviderLoop.hpp>
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
namespace { thread_local const std::function<bool()>* armed_pre_connect = nullptr; }
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
    std::size_t active_tx=0,controls=0;
    EgressRegistration registration;
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
struct HostProviderLoop::State {
    State(std::shared_ptr<NativeBackend> p,std::shared_ptr<ProcessReceiveObserver> o):backend(std::move(p)),observer(std::move(o)){}
    const std::shared_ptr<NativeBackend> backend;
    const std::shared_ptr<ProcessReceiveObserver> observer;
    mutable std::mutex gate;
    mutable std::condition_variable changed;
    bool attempted=false,started=false,input_open=false,stopping=false,sealed=false,closed=false;
    std::size_t active=0;
    std::size_t active_rx=0;
    bool input_closing=false;
    std::uint64_t open_calls=0,close_calls=0,shutdown_calls=0,control_order=0;
    std::uint64_t first_close_order=0,first_shutdown_order=0;
    NativeTableConfiguration config;
    std::unique_ptr<NativeOwnerTable> native;
    std::unique_ptr<ApplicationAttachments> attachments;
    std::unique_ptr<NativeSendExecution> execution;
    EgressRegistration registration;
    ProviderAdapterDomain domain;
    std::unique_ptr<ProviderAdapter> adapter;
    // Owned by this loop, so only this runtime's connect can consult it.
    std::function<bool()> pre_connect;
    std::shared_ptr<NativeSendProvider> send_provider;
    InputChannels::Receiver* receiver=nullptr;
    BackendCloseResult close;
    std::optional<BackendCloseResult> failed_open_cleanup;
    bool shared=false,opening=false;
    std::size_t live_views=0,next_tx=0;
    std::array<std::weak_ptr<View>,2> views;
    SendQueueLimits shared_limits;
    struct PumpGuard {
        State& state;bool receive=false;
        ~PumpGuard(){std::lock_guard<std::mutex> lock(state.gate);--state.active;if(receive)--state.active_rx;state.changed.notify_all();}
    };
};
HostProviderLoop::HostProviderLoop(std::shared_ptr<NativeBackend> p, std::shared_ptr<ProcessReceiveObserver> o) : state_(new State(std::move(p),std::move(o))),view_(std::make_shared<View>()) {}
HostProviderLoop::HostProviderLoop(std::shared_ptr<State> s,std::shared_ptr<View> v):state_(std::move(s)),view_(std::move(v)){}
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
std::shared_ptr<HostProviderLoop> HostProviderLoop::Join() noexcept {
    auto& s=*state_;
    try {
        auto v=std::make_shared<View>();
        auto result=std::shared_ptr<HostProviderLoop>(new HostProviderLoop(state_,v));
        std::lock_guard<std::mutex> lock(s.gate);
        if(!s.shared||!s.started||s.stopping||s.closed||!s.live_views||s.live_views==s.views.size())return {};
        for(auto& slot:s.views){auto old=slot.lock();if(!old||!old->member){slot=v;v->member=true;++s.live_views;return result;}}
    }catch(...){return {};}
    return {};
}
bool HostProviderLoop::StartShared(const DSoftBusTransportDescriptor& d) noexcept {
    auto& s=*state_;bool joining=false;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(view_->attempted||view_->closed||s.stopping||s.closed)return false;
        view_->attempted=true;joining=view_->member;
    }
    if(!joining){
        if(!StartRuntime(d))return false;
        std::lock_guard<std::mutex> lock(s.gate);
        view_->registration=std::move(s.registration);view_->member=true;view_->started=true;
        s.views[0]=view_;s.live_views=1;s.shared_limits=d.send_limits;return true;
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
        auto reg=s.execution->CreateRuntime(exact,d.egress_selections,d.send_limits);
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
        if(view_->closed)return !s.closed||s.close.status==BackendStatus::None;
        if(view_->closing){
            // Wait for a definite state, not only for success. The owner either
            // publishes Closed, or publishes that it retained this view's receiver;
            // a waiter is never left on a latch that only the success path clears.
            s.changed.wait(lock,[&]{return view_->closed||!view_->closing;});
            if(!view_->closing)return false;
            return !s.closed||s.close.status==BackendStatus::None;
        }
        if(!view_->member){view_->closed=true;s.changed.notify_all();return true;}
        view_->closing=true;
        s.changed.notify_all();
    }
    if(view_->after_seal)view_->after_seal();
    {std::unique_lock<std::mutex> lock(s.gate);s.changed.wait(lock,[&]{return view_->controls==0;});}
    EgressRegistration registration;
    {std::lock_guard<std::mutex> lock(s.gate);registration=view_->registration;}
    if(registration){auto revoke=s.execution->BeginRevoke(registration);if(revoke)revoke.value->Progress();}
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
    bool last=false;
    {std::unique_lock<std::mutex> lock(s.gate);
        s.changed.wait(lock,[&]{return view_->active_tx==0;});
        view_->member=false;last=--s.live_views==0;
        if(last)s.stopping=true; // same gate as Join: no resurrection window
    }
    const bool result=!last||ShutdownRuntime(true);
    {std::lock_guard<std::mutex> lock(s.gate);view_->closed=true;s.changed.notify_all();}
    return result;
}
void HostProviderLoop::SetSharedControlHooks(std::function<void()> before_publish,std::function<void()> after_seal){
    view_->before_publish=std::move(before_publish);view_->after_seal=std::move(after_seal);
}
bool HostProviderLoop::Start(const DSoftBusTransportDescriptor& d) noexcept {
    return state_->shared?StartShared(d):StartRuntime(d);
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
    s.config.directory=d.directory;s.config.directions={s.backend->Topology()==BackendTopology::ListenerAndOutgoing?RouteDirection::Both:RouteDirection::Dial};s.config.service_pkg_name="mdds.host.loop";
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    s.config.service_pkg_name="com.kaihong.mdds";
#endif
    s.config.dial_only_egress=s.backend->Topology()==BackendTopology::ListenerAndOutgoing;
    auto native=NativeOwnerTable::Build(s.config);if(!native){STARTUP_CAPTURE("LoopStart_NativeStatus_result",UINT64_MAX,-1,-1,int(native.issue.code),0);return false;}s.native=std::move(*native.value);
    AttachmentLimits receive_limits;
    receive_limits.max_record_bytes=d.maxMessageSize;
    s.attachments=std::make_unique<ApplicationAttachments>(std::vector<Locator>{d.directory.locals[0].local_locator},receive_limits);
    s.execution=std::make_unique<NativeSendExecution>(*s.native);
    auto reg=s.execution->CreateRuntime(s.config,d.egress_selections,d.send_limits);if(!reg){STARTUP_CAPTURE("LoopStart_RegistrationRefused_result",UINT64_MAX,-1,-1,1,0);return false;}
    s.registration=std::move(*reg.value);
    std::lock_guard<std::mutex> lock(s.gate);s.started=true;rollback.done=true;return true;
    } catch (...) {STARTUP_CAPTURE("LoopStart_Exception_result",UINT64_MAX,-1,-1,1,0);return false;}
}
InputOpenResult HostProviderLoop::Open(const Locator& local, InputChannels::Receiver* receiver) noexcept {
    return state_->shared?OpenShared(local,receiver):OpenRuntime(local,receiver);
}
InputOpenResult HostProviderLoop::OpenRuntime(const Locator& local, InputChannels::Receiver* receiver) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);
        ++s.open_calls;
        if(!s.started||s.stopping){STARTUP_CAPTURE("LoopOpen_Disabled_result",UINT64_MAX,-1,-1,int(InputOpenResult::Disabled),0);return InputOpenResult::Disabled;}
        if(s.input_closing){STARTUP_CAPTURE("LoopOpen_Closing_result",UINT64_MAX,-1,-1,int(InputOpenResult::Closing),0);return InputOpenResult::Closing;}
        if(s.failed_open_cleanup){STARTUP_CAPTURE("LoopOpen_Disabled_result",UINT64_MAX,-1,-1,int(InputOpenResult::Disabled),0);return InputOpenResult::Disabled;}
        if(local!=s.config.directory.locals[0].local_locator){STARTUP_CAPTURE("LoopOpen_InvalidLocal_result",UINT64_MAX,-1,-1,int(InputOpenResult::InvalidLocal),0);return InputOpenResult::InvalidLocal;}
        if(!receiver&&!s.shared){STARTUP_CAPTURE("LoopOpen_NullReceiver_result",UINT64_MAX,-1,-1,int(InputOpenResult::NullReceiver),0);return InputOpenResult::NullReceiver;}
        if(s.input_open)return s.receiver==receiver?InputOpenResult::AlreadyOpen:InputOpenResult::Conflict;
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
            const auto receipt=pending->Close();
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
    {
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_RUNTIME_PROBE
        // Armed only for this call, on this thread: the gate runs outside every
        // management lock below it and cannot be reached by another runtime.
        std::function<bool()> gate;
        {std::lock_guard<std::mutex> lock(s.gate);gate=s.pre_connect;}
        ArmedPreConnectGate armed(std::move(gate));
#endif
        const auto initialized=pending->Initialize();
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
    }
#ifdef FASTDDS_DSOFTBUS_PUBLIC_SOCKET_DDS_EXPERIMENT
    auto provider=RuntimeProbeAccess::SendBridge(*pending,*s.native);
#else
    auto provider=pending->BindSendProvider(*s.native);
#endif
    if(!provider){settle_pending();return InputOpenResult::Disabled;}
    std::lock_guard<std::mutex> lock(s.gate);
    s.adapter=std::move(pending);s.send_provider=std::move(provider);s.receiver=receiver;s.input_open=true;return InputOpenResult::Opened;
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
    auto& s=*state_;
    if(s.shared){
        EgressRegistration registration;
        {std::lock_guard<std::mutex> lock(s.gate);if(!view_->started||view_->closing||view_->closed||s.stopping)return {SenderAttachStatus::RuntimeClosed};registration=view_->registration;}
        return s.execution->Attach(registration,list);
    }
    {std::lock_guard<std::mutex> lock(s.gate);if(!s.started||s.stopping)return {SenderAttachStatus::RuntimeClosed};}
    return s.execution->Attach(s.registration,list);
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
    if(s.shared){
        std::array<std::shared_ptr<View>,2> eligible;std::size_t count=0;
        {std::lock_guard<std::mutex> lock(s.gate);
            // The runtime owns the send queue, so driving it is a runtime-level
            // capability. Who may drive it is one rule shared with PumpReceive: a view
            // that actually started belongs to this runtime and may keep advancing it
            // even after it closes, because the retained controller is the only
            // progress owner during the final drain; a view whose Start never
            // succeeded is not a member here and must not drive this runtime's work on
            // another view's behalf.
            if(!view_->started)return {NativeStatus::WrongState};
            if(!s.send_provider||s.stopping)return {NativeStatus::ServiceClosing};
            for(std::size_t i=0;i<s.views.size();++i){auto v=s.views[(s.next_tx+i)%s.views.size()].lock();if(v&&v->started&&!v->closing&&!v->closed)eligible[count++]=std::move(v);}
            s.next_tx=(s.next_tx+1)%s.views.size();++s.active;
        }
        State::PumpGuard pump{s};
        for(const auto& v:eligible)if(v){
            EgressRegistration registration;ReceiverIdentity identity;
            {std::lock_guard<std::mutex> lock(s.gate);if(v->closing||v->closed)continue;++v->active_tx;registration=v->registration;identity=v->identity==ReceiverIdentity{}?v->retired_identity:v->identity;}
            struct Pin {State& s;View& v;~Pin(){std::lock_guard<std::mutex> lock(s.gate);--v.active_tx;s.changed.notify_all();}} pin{s,*v};
            // Preserve the existing no-work/Take-failure behavior. This receipt
            // describes only successfully taken work, not every Take rejection.
            auto work=s.execution->Take(registration);if(!work)continue;
            if(observation){observation->selected=true;observation->receiver=identity;}
            const auto observe=[&]{if(!observation)return;
                {std::lock_guard<std::mutex> lock(s.gate);observation->view_closing=v->closing;}
                const auto activity=s.execution->Observe(registration);
                observation->registration_status=activity.status;observation->admission_closed=activity.admission_closed;
            };
#ifdef FASTDDS_DSOFTBUS_HOST_BACKEND_TESTING
            if(v->send_hook)v->send_hook(false);
#endif
            auto invocation=s.execution->TryAdmit(*work.value,s.send_provider);
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
    return s.adapter->DispatchOne();
}
bool HostProviderLoop::Shutdown() noexcept {
    return state_->shared?ShutdownShared():ShutdownRuntime();
}
bool HostProviderLoop::ShutdownRuntime(bool owns_stop) noexcept {
    if(!InputCloseAllowedOnThisThread())return false;
    auto& s=*state_;
    {
        std::unique_lock<std::mutex> lock(s.gate);
        ++s.shutdown_calls;if(!s.first_shutdown_order)s.first_shutdown_order=++s.control_order;
        if(s.closed)return s.close.status==BackendStatus::None;
        if(s.stopping&&!owns_stop){s.changed.wait(lock,[&]{return s.closed;});return s.close.status==BackendStatus::None;}
        s.stopping=true;
    }
    if(s.execution&&s.registration) {
        auto revoke=s.execution->BeginRevoke(s.registration);
        if(revoke)revoke.value->Progress();
    }
    if(s.adapter)s.adapter->SealIngress();
    {
        std::unique_lock<std::mutex> lock(s.gate);s.sealed=true;s.changed.notify_all();
        s.changed.wait(lock,[&]{return s.active==0;});
    }
    const auto result=s.adapter?s.adapter->Close():
        s.failed_open_cleanup.value_or(BackendCloseResult{BackendStatus::None,true});
    {std::lock_guard<std::mutex> lock(s.gate);s.close=result;s.input_open=false;s.closed=true;s.changed.notify_all();}
    return result.status==BackendStatus::None&&result.adapter_drained;
}
bool HostProviderLoop::InputCloseAllowedOnThisThread() const noexcept {
    return !ServiceRecordBinding::IsCallbackContext()&&!ProviderAdapter::IsProviderCallbackContext();
}
BackendStatus HostProviderLoop::CloseAccepted() noexcept {
    if(!InputCloseAllowedOnThisThread())return BackendStatus::InvalidState;
    return state_->adapter?state_->adapter->CloseAccepted():BackendStatus::InvalidState;
}
HostLoopObservation HostProviderLoop::Observe() const noexcept {
    auto& s=*state_;HostLoopObservation v;
    {std::lock_guard<std::mutex> lock(s.gate);v.started=s.started;v.input_open=s.input_open;v.input_closing=s.input_closing;v.sealed=s.sealed;v.closed=s.closed;v.active_pumps=s.active;v.close=s.close;v.failed_open_cleanup=s.failed_open_cleanup;
        v.open_calls=s.open_calls;v.close_calls=s.close_calls;v.shutdown_calls=s.shutdown_calls;v.first_close_order=s.first_close_order;v.first_shutdown_order=s.first_shutdown_order;}
    // A shared runtime moves its original registration into the first view, so this
    // state-level handle is deliberately empty there: it must never be published as
    // a per-view zero. Each shared view reports its own registration below.
    if(s.registration&&!s.shared){v.tx=s.registration.SnapshotUsage();v.send=s.execution->Observe(s.registration);}
    if(s.attachments)v.receive=s.attachments->SnapshotUsage();
    if(s.native)v.native=s.native->SnapshotUsage();
    if(s.adapter)v.adapter=s.adapter->Observe();
    if(s.shared){
        EgressRegistration registration;
        std::array<EgressRegistration,2> all;
        {std::lock_guard<std::mutex> lock(s.gate);v.shared_runtime=true;v.live_views=s.live_views;v.receiver_identity=view_->identity;v.retired_receiver_identity=view_->retired_identity;v.input_open=view_->receiver!=nullptr;v.input_closing=view_->input_closing;v.view_controls=view_->controls;v.view_closing=view_->closing;registration=view_->registration;for(std::size_t i=0;i<s.views.size();++i)if(auto live=s.views[i].lock())all[i]=live->registration;}
        if(registration){v.tx=registration.SnapshotUsage();v.send=s.execution->Observe(registration);}
        for(const auto& r:all)if(r)v.runtime_admitted_outstanding+=s.execution->Observe(r).admitted_outstanding;
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
