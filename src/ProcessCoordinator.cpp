#include <fastdds_dsoftbus/dsoftbus/ProcessCoordinator.hpp>
#include "ProcessReceiveInternal.hpp"
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace fastdds_dsoftbus::dsoftbus {
namespace detail {
struct ProcessIdentity {};
struct ProcessApplicationState
{
    ProcessApplicationState(std::shared_ptr<const ProcessIdentity> identity,const std::vector<Locator>& l)
        :service(std::move(identity)),locals(l) {}
    const std::shared_ptr<const ProcessIdentity> service;
    const std::vector<Locator> locals;
    ApplicationClaim claim;
    EgressRegistration egress;
    NativeRevokeTicket revoke;
    ProcessApplicationPhase phase=ProcessApplicationPhase::Preparing;
    NativeStatus freeze_status=NativeStatus::None;
    bool progressing=false;
};
}
namespace {
template<class T> NativeResult<T> Failure(NativeStatus status) {return {std::nullopt,{status}};}
std::vector<Locator> Locals(const NativeTableConfiguration& config)
{
    std::vector<Locator> result(std::size_t{0});result.reserve(config.directory.locals.size());
    for(const auto& local:config.directory.locals) result.push_back(local.local_locator);
    return result;
}
void Add(std::size_t& total,std::size_t n)
{total=n>std::numeric_limits<std::size_t>::max()-total?std::numeric_limits<std::size_t>::max():total+n;}
}
struct ProcessCoordinator::State
{
    struct Slot {Locator local;std::shared_ptr<detail::ProcessApplicationState> app;};
    State(const NativeTableConfiguration& c,std::unique_ptr<NativeOwnerTable> n,EndpointDirectory::Snapshot d,
            AttachmentLimits limits,ProcessClock clock_value,ProcessClock::Clock::duration hold,TestHooks h)
        :identity(std::make_shared<detail::ProcessIdentity>()),config(c),directory(std::move(d)),locals(Locals(c)),
         native(std::move(n)),sends(*native),attachments(locals,limits),slots(std::size_t{0}),
         clock(std::move(clock_value)),binding_hold(hold),hooks(std::move(h))
    {
        slots.reserve(locals.size());for(const auto& local:locals) slots.push_back({local,{}});
    }
    std::mutex gate;
    const std::shared_ptr<const detail::ProcessIdentity> identity;
    const NativeTableConfiguration config;
    const EndpointDirectory::Snapshot directory;
    const std::vector<Locator> locals;
    std::unique_ptr<NativeOwnerTable> native;
    NativeSendExecution sends;
    ApplicationAttachments attachments;
    std::vector<Slot> slots;
    const ProcessClock clock;
    const ProcessClock::Clock::duration binding_hold;
    TestHooks hooks;
    std::shared_ptr<ProcessReceiveObserver> observer;
    bool stopping=false;
};
ProcessCoordinator::ProcessCoordinator(std::unique_ptr<State> state) noexcept:state_(std::move(state)) {}
ProcessCoordinator::~ProcessCoordinator()=default;
NativeResult<std::unique_ptr<ProcessCoordinator>> ProcessCoordinator::Build(const NativeTableConfiguration& config,
        AttachmentLimits limits,ProcessClock clock,ProcessClock::Clock::duration hold,std::shared_ptr<ProcessReceiveObserver> observer)
{return BuildWithHooks(config,limits,std::move(clock),hold,{},std::move(observer));}
NativeResult<std::unique_ptr<ProcessCoordinator>> ProcessCoordinator::BuildWithHooks(const NativeTableConfiguration& config,
        AttachmentLimits limits,ProcessClock clock,ProcessClock::Clock::duration hold,TestHooks hooks,
        std::shared_ptr<ProcessReceiveObserver> observer)
{
    try {
        if(hold<=ProcessClock::Clock::duration::zero()||hold==ProcessClock::Clock::duration::max())
            return Failure<std::unique_ptr<ProcessCoordinator>>(NativeStatus::InvalidLimits);
        auto native=NativeOwnerTable::Build(config);if(!native) return {std::nullopt,native.issue};
        auto directory=EndpointDirectory::Build(config.directory);
        if(!directory.value) return Failure<std::unique_ptr<ProcessCoordinator>>(NativeStatus::DirectoryRejected);
        auto state=std::make_unique<State>(config,std::move(*native.value),*directory.value,limits,std::move(clock),hold,std::move(hooks));
        state->attachments.hooks_.copy_reserved=state->hooks.copy_reserved;
        state->attachments.hooks_.receive_publish=state->hooks.receive_publish;
        state->observer=std::move(observer);
        return {std::unique_ptr<ProcessCoordinator>(new ProcessCoordinator(std::move(state))),{}};
    } catch(const std::bad_alloc&) {return Failure<std::unique_ptr<ProcessCoordinator>>(NativeStatus::AllocationFailure);}
      catch(const std::invalid_argument&) {return Failure<std::unique_ptr<ProcessCoordinator>>(NativeStatus::InvalidLimits);}
}
NativeResult<ProcessApplication> ProcessCoordinator::AttachApplication(const std::vector<Locator>& locals,
        const std::vector<EgressSelection>& selections,SendQueueLimits limits)
{
    std::shared_ptr<detail::ProcessApplicationState> app;bool reserved=false;
    NativeStatus failure=NativeStatus::None;
    try {
        if(locals.empty()) return Failure<ProcessApplication>(NativeStatus::InvalidConfiguration);
        for(std::size_t i=0;i<locals.size();++i)
            if(!LocatorCodec::IsValid(locals[i])||std::find(state_->locals.begin(),state_->locals.end(),locals[i])==state_->locals.end()||
                    std::find(locals.begin(),locals.begin()+i,locals[i])!=locals.begin()+i)
                return Failure<ProcessApplication>(NativeStatus::InvalidConfiguration);
        app=std::make_shared<detail::ProcessApplicationState>(state_->identity,locals);
        {
            std::lock_guard<std::mutex> lock(state_->gate);
            if(state_->stopping) return Failure<ProcessApplication>(NativeStatus::ServiceClosing);
            for(const auto& slot:state_->slots)
                if(slot.app&&std::find(locals.begin(),locals.end(),slot.local)!=locals.end())
                    return Failure<ProcessApplication>(NativeStatus::ExistingPending);
            for(auto& slot:state_->slots)
                if(std::find(locals.begin(),locals.end(),slot.local)!=locals.end()) slot.app=app;
            reserved=true;
        }
        auto claim=state_->attachments.ClaimApplication(locals,state_->directory);
        if(claim.result!=ApplicationClaimResult::Claimed) {
            failure=claim.result==ApplicationClaimResult::AllocationFailure?NativeStatus::AllocationFailure:
                    claim.result==ApplicationClaimResult::Capacity?NativeStatus::AdmissionSaturated:NativeStatus::DirectoryRejected;
        } else {
            app->claim=std::move(claim.claim);
            if(state_->hooks.after_claim) state_->hooks.after_claim();
            auto egress=state_->sends.CreateRuntimeForLocals(state_->config,locals,selections,limits);
            if(!egress) failure=egress.issue.code;
            else {
                app->egress=std::move(*egress.value);
                if(state_->hooks.before_publish) state_->hooks.before_publish();
                std::lock_guard<std::mutex> lock(state_->gate);
                if(state_->stopping) failure=NativeStatus::ServiceClosing;
                else {app->phase=ProcessApplicationPhase::Active;return {ProcessApplication(app),{}};}
            }
        }
    } catch(const std::bad_alloc&) {failure=NativeStatus::AllocationFailure;}
    if(reserved) {
        if(state_->hooks.before_rollback) state_->hooks.before_rollback();
        Rollback(app);
    }
    return Failure<ProcessApplication>(failure);
}
void ProcessCoordinator::Rollback(const std::shared_ptr<detail::ProcessApplicationState>& app)
{
    // Preparing was never externally published; there are no receiver/send
    // permits. Keep coordinator slots throughout actual component cleanup.
    if(app->egress) {auto ticket=state_->sends.BeginRevoke(app->egress);if(ticket) ticket.value->Progress();}
    if(app->claim) state_->attachments.AwaitDetached(app->claim);
    std::lock_guard<std::mutex> lock(state_->gate);
    app->phase=ProcessApplicationPhase::Detached;
    for(auto& slot:state_->slots) if(slot.app==app) slot.app.reset(); // app parameter pins final destruction outside gate.
}
ReceiverOpenOutcome ProcessCoordinator::OpenReceiver(const ProcessApplication& handle,const Locator& local,InputChannels::Receiver* receiver)
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return {ReceiverOpenResult::InvalidHandle,{}};
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(app->phase!=ProcessApplicationPhase::Active) return {ReceiverOpenResult::Detached,{}};
    }
    return state_->attachments.OpenReceiver(app->claim,local,receiver);
}
AttachmentControlResult ProcessCoordinator::CloseReceiver(const ReceiverBinding& binding)
{return state_->attachments.CloseReceiver(binding);}
SenderAttachResult ProcessCoordinator::AttachSender(const ProcessApplication& handle,eprosima::fastdds::rtps::SendResourceList& resources)
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return {SenderAttachStatus::InvalidRuntime};
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(app->phase!=ProcessApplicationPhase::Active) return {SenderAttachStatus::RuntimeClosed};
    }
    return state_->sends.Attach(app->egress,resources);
}
NativeResult<NativeSendWork> ProcessCoordinator::TakeSend(const ProcessApplication& handle)
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return Failure<NativeSendWork>(NativeStatus::InvalidHandle);
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(app->phase!=ProcessApplicationPhase::Active) return Failure<NativeSendWork>(NativeStatus::Revoked);
    }
    return state_->sends.Take(app->egress);
}
NativeResult<NativeSendInvocation> ProcessCoordinator::TryAdmitSend(NativeSendWork& work,std::shared_ptr<NativeSendProvider> provider)
{return state_->sends.TryAdmit(work,std::move(provider));}
ReceiveAdmissionResult ProcessCoordinator::Receive(const NativeIdentityConstraint& pin,const void* bytes,std::size_t size,
        ProcessClock::Clock::time_point bind_deadline,std::uint64_t correlation,ReceiveAdmissionDiagnostic* diagnostic)
{
    if(diagnostic) diagnostic->size=size;
    if(!pin) return diagnostic?diagnostic->Return(RxAdmissionStage::Coordinator,RxAdmissionReason::InvalidPin,
            ReceiveAdmissionResult::Revoked):ReceiveAdmissionResult::Revoked;
    std::shared_ptr<detail::ProcessApplicationState> app;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        for(const auto& slot:state_->slots)
            if(slot.local==pin.local()&&slot.app&&slot.app->phase==ProcessApplicationPhase::Active) {app=slot.app;break;}
        if(diagnostic) diagnostic->candidate_count=app?1:0;
    }
    if(!app) return diagnostic?diagnostic->Return(RxAdmissionStage::Coordinator,RxAdmissionReason::NoApplication,
            ReceiveAdmissionResult::NoReceiver):ReceiveAdmissionResult::NoReceiver;
    if(state_->hooks.after_receive_selection) state_->hooks.after_receive_selection();
    const auto now=state_->clock.Now();
    if(now>ProcessClock::Clock::time_point::max()-state_->binding_hold)
        return diagnostic?diagnostic->Return(RxAdmissionStage::Coordinator,RxAdmissionReason::ClockOverflow,
                ReceiveAdmissionResult::Expired):ReceiveAdmissionResult::Expired;
    const auto expiry=std::min(bind_deadline,now+state_->binding_hold);
    return detail::ProcessReceiveBridge::Admit(state_->attachments,*state_->native,app->claim,pin,bytes,size,expiry,state_->clock,
            correlation,state_->observer,diagnostic);
}
NativeResult<ProcessReceiveInvocation> ProcessCoordinator::NextReceive()
{return detail::ProcessReceiveBridge::Next(state_->attachments,*state_->native,state_->clock);}
NativeStatus ProcessCoordinator::FreezeLocked(detail::ProcessApplicationState& app)
{
    if(app.phase==ProcessApplicationPhase::Detached) return NativeStatus::None;
    if(app.phase==ProcessApplicationPhase::Detaching) return app.freeze_status;
    if(app.phase!=ProcessApplicationPhase::Active) return NativeStatus::ExistingPending;
    app.phase=ProcessApplicationPhase::Detaching;
    const auto receive=state_->attachments.FreezeApplication(app.claim);
    auto send=state_->sends.BeginRevoke(app.egress);
    if(send) app.revoke=std::move(*send.value); // Initially empty; no cleanup under coordinator gate.
    app.freeze_status=!send?send.issue.code:receive==AttachmentControlResult::InvalidHandle?NativeStatus::InvalidHandle:NativeStatus::None;
    return app.freeze_status;
}
NativeStatus ProcessCoordinator::BeginDetach(const ProcessApplication& handle)
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);return FreezeLocked(*app);
}
ProcessDetachSnapshot ProcessCoordinator::PollDetach(const ProcessApplication& handle) const
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return {};
    std::lock_guard<std::mutex> lock(state_->gate);
    ProcessDetachSnapshot result;result.status=app->freeze_status;result.phase=app->phase;result.progressing=app->progressing;
    if(app->phase!=ProcessApplicationPhase::Preparing) {
        result.send=state_->sends.Observe(app->egress);
        const auto receive=state_->attachments.ReceiveActivity(app->claim);
        result.copying=receive.copying;result.dispatching=receive.dispatching;result.controls=receive.controls;
    }
    return result;
}
ProcessDetachSnapshot ProcessCoordinator::ProgressDetach(const ProcessApplication& handle)
{
    auto app=handle.state_;if(!app||app->service!=state_->identity) return {};
    NativeRevokeTicket ticket;bool advance=false;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(app->phase==ProcessApplicationPhase::Detaching&&!app->progressing&&app->freeze_status==NativeStatus::None) {
            app->progressing=true;ticket=std::move(app->revoke);advance=true;
        }
    }
    if(!advance) return PollDetach(handle);
    // Cleanup may execute provider destruction. Never under coordinator gate.
    ticket.Progress();state_->attachments.RequestDetach(app->claim);
    const auto send=state_->sends.Observe(app->egress);
    const auto receive=state_->attachments.ReceiveActivity(app->claim);
    const bool drained=send.drained&&receive.valid&&receive.frozen&&!receive.copying&&!receive.dispatching&&!receive.controls;
    if(drained) state_->attachments.AwaitDetached(app->claim);
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(drained) {
            app->phase=ProcessApplicationPhase::Detached;
            for(auto& slot:state_->slots) if(slot.app==app) slot.app.reset();
        }
        app->progressing=false;
    }
    return PollDetach(handle);
}
void ProcessCoordinator::BeginShutdown()
{
    std::lock_guard<std::mutex> lock(state_->gate);state_->stopping=true;
    for(const auto& slot:state_->slots)
        if(slot.app&&slot.app->phase==ProcessApplicationPhase::Active) FreezeLocked(*slot.app);
    // Preparing creators see stopping at publication and roll back, keeping slots.
}
ProcessShutdownSnapshot ProcessCoordinator::PollShutdown() const
{
    std::lock_guard<std::mutex> lock(state_->gate);ProcessShutdownSnapshot result;
    for(std::size_t i=0;i<state_->slots.size();++i) {
        const auto& app=state_->slots[i].app;if(!app) continue;
        bool duplicate=false;for(std::size_t j=0;j<i;++j) if(state_->slots[j].app==app) {duplicate=true;break;}
        if(duplicate) continue;
        ++result.applications;
        if(app->phase==ProcessApplicationPhase::Preparing) {++result.preparing;continue;}
        if(app->progressing) ++result.progressing;
        const auto send=state_->sends.Observe(app->egress);const auto receive=state_->attachments.ReceiveActivity(app->claim);
        Add(result.admitted,send.admitted_outstanding);Add(result.submitting,static_cast<std::size_t>(send.submission_inflight));
        Add(result.copying,receive.copying);Add(result.dispatching,receive.dispatching);Add(result.controls,receive.controls);
    }
    result.phase=!state_->stopping?ProcessShutdownPhase::Open:
            result.applications?ProcessShutdownPhase::Pending:ProcessShutdownPhase::ApplicationsDrained;
    return result;
}
NativeOwnerTable& ProcessCoordinator::ModelNative() noexcept {return *state_->native;}
NativeOwnerTable& ProcessCoordinator::Native() noexcept {return *state_->native;}
ApplicationIdentity ProcessCoordinator::ObserveApplication(const ProcessApplication& handle) const noexcept
{
    const auto& app=handle.state_;
    return app&&app->service==state_->identity?state_->attachments.ObserveApplication(app->claim):ApplicationIdentity{};
}
ReceiverIdentity ProcessCoordinator::ObserveReceiver(const ReceiverBinding& binding) const noexcept
{return state_->attachments.ObserveReceiver(binding);}
NativeEgressUsage ProcessCoordinator::EgressUsage() const noexcept {return state_->sends.SnapshotUsage();}
AttachmentUsage ProcessCoordinator::ReceiveUsage() const {return state_->attachments.SnapshotUsage();}
}
