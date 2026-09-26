#include <mdds/dsoftbus/HostProviderLoop.hpp>
#include <condition_variable>
#include <mutex>
namespace mdds::dsoftbus::host_test {
using namespace phase0;
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
    std::shared_ptr<NativeSendProvider> send_provider;
    InputChannels::Receiver* receiver=nullptr;
    BackendCloseResult close;
    std::optional<BackendCloseResult> failed_open_cleanup;
    struct PumpGuard {
        State& state;bool receive=false;
        ~PumpGuard(){std::lock_guard<std::mutex> lock(state.gate);--state.active;if(receive)--state.active_rx;state.changed.notify_all();}
    };
};
HostProviderLoop::HostProviderLoop(std::shared_ptr<NativeBackend> p, std::shared_ptr<ProcessReceiveObserver> o) : state_(new State(std::move(p),std::move(o))) {}
HostProviderLoop::~HostProviderLoop() {Shutdown();}
bool HostProviderLoop::Start(const DSoftBusTransportDescriptor& d) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);if(s.attempted||s.closed)return false;s.attempted=true;}
    // Only the attempt that changed attempted owns failure cleanup. A second
    // facade rejected above must never stop the first facade's live backend.
    struct Rollback {HostProviderLoop& loop;bool done=false;~Rollback(){if(!done)loop.Shutdown();}} rollback{*this};
    try {
    if(!s.backend||s.backend->Kind()!=BackendKind::HostMock||d.directory.locals.size()!=1||d.directory.routes.size()!=1) return false;
    s.config.directory=d.directory;s.config.directions={RouteDirection::Dial};s.config.service_pkg_name="mdds.host.loop";
    auto native=NativeOwnerTable::Build(s.config);if(!native)return false;s.native=std::move(*native.value);
    AttachmentLimits receive_limits;
    receive_limits.max_record_bytes=d.maxMessageSize;
    s.attachments=std::make_unique<ApplicationAttachments>(std::vector<Locator>{d.directory.locals[0].local_locator},receive_limits);
    s.execution=std::make_unique<NativeSendExecution>(*s.native);
    auto reg=s.execution->CreateRuntime(s.config,d.egress_selections,d.send_limits);if(!reg)return false;
    s.registration=std::move(*reg.value);
    std::lock_guard<std::mutex> lock(s.gate);s.started=true;rollback.done=true;return true;
    } catch (...) {return false;}
}
InputOpenResult HostProviderLoop::Open(const Locator& local, InputChannels::Receiver* receiver) noexcept {
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);
        ++s.open_calls;
        if(!s.started||s.stopping)return InputOpenResult::Disabled;
        if(s.input_closing)return InputOpenResult::Closing;
        if(s.failed_open_cleanup)return InputOpenResult::Disabled;
        if(local!=s.config.directory.locals[0].local_locator)return InputOpenResult::InvalidLocal;
        if(!receiver)return InputOpenResult::NullReceiver;
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
            const auto receipt=pending->Close();
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
    auto result=ProviderAdapter::Build(s.domain,ServiceRecordTable::Process(),*s.native,*s.attachments,s.backend,request);
    if(result.status!=BackendStatus::None)return InputOpenResult::Conflict;
    // Unpublished ownership stays local until all steps succeed. Every failure
    // releases only this pending binding before false reaches the receiver owner.
    pending=std::move(result.adapter);
    if(pending->Initialize()!=BackendStatus::None||pending->Publish()!=BackendStatus::None) {
        settle_pending();return InputOpenResult::Disabled;
    }
    auto provider=pending->BindSendProvider(*s.native);
    if(!provider){settle_pending();return InputOpenResult::Disabled;}
    std::lock_guard<std::mutex> lock(s.gate);
    s.adapter=std::move(pending);s.send_provider=std::move(provider);s.receiver=receiver;s.input_open=true;return InputOpenResult::Opened;
    } catch(const std::bad_alloc&) {settle_pending();return InputOpenResult::AllocationFailure;}
      catch(...) {settle_pending();return InputOpenResult::Disabled;}
}
bool HostProviderLoop::CloseInput(const Locator& local) noexcept {
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
    {std::lock_guard<std::mutex> lock(s.gate);if(!s.started||s.stopping)return {SenderAttachStatus::RuntimeClosed};}
    return s.execution->Attach(s.registration,list);
}
NativeSendRunResult HostProviderLoop::PumpSend() noexcept {
    auto& s=*state_;
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
    {std::lock_guard<std::mutex> lock(s.gate);if(!s.input_open||s.stopping)return BackendStatus::InvalidState;++s.active;++s.active_rx;}
    State::PumpGuard guard{s,true};
    return s.adapter->DispatchOne();
}
bool HostProviderLoop::Shutdown() noexcept {
    auto& s=*state_;
    {
        std::unique_lock<std::mutex> lock(s.gate);
        ++s.shutdown_calls;if(!s.first_shutdown_order)s.first_shutdown_order=++s.control_order;
        if(s.closed)return s.close.status==BackendStatus::None;
        if(s.stopping){s.changed.wait(lock,[&]{return s.closed;});return s.close.status==BackendStatus::None;}
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
    return !ServiceRecordBinding::IsCallbackContext();
}
HostLoopObservation HostProviderLoop::Observe() const noexcept {
    auto& s=*state_;HostLoopObservation v;
    {std::lock_guard<std::mutex> lock(s.gate);v.started=s.started;v.input_open=s.input_open;v.input_closing=s.input_closing;v.sealed=s.sealed;v.closed=s.closed;v.active_pumps=s.active;v.close=s.close;v.failed_open_cleanup=s.failed_open_cleanup;
        v.open_calls=s.open_calls;v.close_calls=s.close_calls;v.shutdown_calls=s.shutdown_calls;v.first_close_order=s.first_close_order;v.first_shutdown_order=s.first_shutdown_order;}
    if(s.registration){v.tx=s.registration.SnapshotUsage();v.send=s.execution->Observe(s.registration);}
    if(s.attachments)v.receive=s.attachments->SnapshotUsage();
    if(s.adapter)v.adapter=s.adapter->Observe();return v;
}
bool HostProviderLoop::WaitSealed(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(state_->gate);
    return state_->changed.wait_until(lock,deadline,[&]{return state_->sealed;});
}
bool HostProviderLoop::WaitInputClosing(std::chrono::steady_clock::time_point deadline) const {
    std::unique_lock<std::mutex> lock(state_->gate);
    return state_->changed.wait_until(lock,deadline,[&]{return state_->input_closing;});
}
}
namespace mdds::dsoftbus {
std::unique_ptr<DSoftBusTransport> HostFacadeTestAccess::Create(const DSoftBusTransportDescriptor& d,
        std::shared_ptr<HostFacadeBackend> backend) {
    return std::unique_ptr<DSoftBusTransport>(new DSoftBusTransport(d,DSoftBusTransport::HostBackendTag{},std::move(backend)));
}
}
