#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include "NativeSendExecutionInternal.hpp"
#include <new>
#include <stdexcept>
namespace mdds::dsoftbus {
namespace detail {
EgressState::~EgressState() noexcept
{
    NativeEgressBridge::Revoke(ceiling_->state_,*this);
    runtime_->Close(); // No native gate held, including unpublished cleanup.
}
bool NativeGenerationAdapter::TryResolve(const Route& r,SendConnectionSnapshot& out) const
{return NativeEgressBridge::Resolve(*ceiling_,r,out);}
}
namespace {
template<class T> NativeResult<T> Failure(NativeStatus s) noexcept {return {std::nullopt,{s}};}
}
EgressRegistration::EgressRegistration(std::shared_ptr<detail::EgressState> s) noexcept : state_(std::move(s)) {}
EgressRegistration::operator bool() const noexcept { return bool(state_); }
RuntimeIdentityHandle EgressRegistration::identity() const noexcept { return state_?state_->identity_:RuntimeIdentityHandle{}; }
SendQueueUsage EgressRegistration::SnapshotUsage() const { return state_?state_->runtime_->SnapshotUsage():SendQueueUsage{}; }
NativeSendWork::NativeSendWork() noexcept = default;
NativeSendWork::~NativeSendWork() = default;
NativeSendWork::NativeSendWork(NativeSendWork&&) noexcept = default;
NativeSendWork& NativeSendWork::operator=(NativeSendWork&&) noexcept = default;
NativeSendWork::NativeSendWork(std::unique_ptr<detail::NativeSendWorkState> s) noexcept : state_(std::move(s)) {}
NativeSendWork::operator bool() const noexcept { return bool(state_); }
NativeSendInvocation::NativeSendInvocation() noexcept = default;
NativeSendInvocation::~NativeSendInvocation() = default;
NativeSendInvocation::NativeSendInvocation(NativeSendInvocation&&) noexcept = default;
NativeSendInvocation& NativeSendInvocation::operator=(NativeSendInvocation&&) noexcept = default;
NativeSendInvocation::NativeSendInvocation(std::unique_ptr<detail::NativeSendInvocationState> s) noexcept : state_(std::move(s)) {}
NativeSendRunResult NativeSendInvocation::Run() noexcept
{
    if(!state_) return {NativeStatus::InvalidHandle};
    auto call=std::move(state_); // Once only; releases every borrow after actual return/unwind.
    const auto& record=*call->work_->record_;
    const auto entered=detail::NativeEgressBridge::Enter(call->ticket_,record.deadline());
    if(entered!=NativeStatus::None) return {entered};
    std::int32_t code;
    try {
        code=call->provider_->Send(call->socket_,record.route(),record.data(),record.size(),record.deadline(),record.priority());
    } catch(...) {
        const auto result=detail::NativeEgressBridge::Fault(call->ticket_,NativeProviderFault::ExceptionUnwind,std::nullopt);
        return {result.status,NativeSendOutcome::ReturnedByException,std::nullopt};
    }
    if(code>0) {
        const auto result=detail::NativeEgressBridge::Fault(call->ticket_,NativeProviderFault::InvalidPositiveReturn,code);
        return {result.status,NativeSendOutcome::ReturnedInvalidCode,code};
    }
    const auto result=call->ticket_.Complete(code);
    return {result.status,NativeSendOutcome::ReturnedSdkCode,code};
}
NativeSendExecution::NativeSendExecution(NativeOwnerTable& table) noexcept : state_(detail::NativeEgressBridge::Bind(table)) {}
NativeResult<EgressRegistration> NativeSendExecution::CreateRuntime(const NativeTableConfiguration& config,
        const std::vector<EgressSelection>& selections, SendQueueLimits limits) const noexcept
{
    try {
        auto ceiling=std::shared_ptr<detail::RuntimeCeilingLease>(new detail::RuntimeCeilingLease(state_,limits));
        const auto reserved=detail::NativeEgressBridge::Reserve(*ceiling,config);
        if(reserved!=NativeStatus::None) return Failure<EgressRegistration>(reserved);
        const auto plan=EgressPlan::Build(config.directory,selections);
        if(!plan.value) return Failure<EgressRegistration>(plan.issue.code==EgressError::AllocationFailure?
                NativeStatus::AllocationFailure:NativeStatus::DirectoryRejected);
        auto source=std::shared_ptr<detail::NativeGenerationAdapter>(new detail::NativeGenerationAdapter(ceiling));
        auto runtime=std::make_shared<RuntimeSendPath>(plan.value,limits,std::move(source));
        auto registration=std::shared_ptr<detail::EgressState>(new detail::EgressState(ceiling,std::move(runtime)));
        const auto published=detail::NativeEgressBridge::Publish(*registration);
        if(published!=NativeStatus::None) return Failure<EgressRegistration>(published);
        return {EgressRegistration(std::move(registration)),{}};
    } catch(const std::bad_alloc&) {return Failure<EgressRegistration>(NativeStatus::AllocationFailure);}
      catch(const std::invalid_argument&) {return Failure<EgressRegistration>(NativeStatus::InvalidLimits);}
}
SenderAttachResult NativeSendExecution::Attach(EgressRegistration r, eprosima::fastdds::rtps::SendResourceList& list) const
{
    if(!r) return {SenderAttachStatus::InvalidRuntime};
    const auto status=detail::NativeEgressBridge::Check(state_,*r.state_);
    if(status!=NativeStatus::None) return {status==NativeStatus::ForeignState?SenderAttachStatus::InvalidRuntime:SenderAttachStatus::RuntimeClosed};
    return DSoftBusSenderResource::Attach(list,r.state_->runtime_);
}
NativeResult<NativeSendWork> NativeSendExecution::Take(EgressRegistration r) const noexcept
{
    if(!r) return Failure<NativeSendWork>(NativeStatus::InvalidHandle);
    const auto status=detail::NativeEgressBridge::Check(state_,*r.state_);
    if(status!=NativeStatus::None) return Failure<NativeSendWork>(status);
    try {
        auto work=std::unique_ptr<detail::NativeSendWorkState>(new detail::NativeSendWorkState(r.state_));
        work->record_=r.state_->runtime_->TryTake();
        if(!work->record_) return Failure<NativeSendWork>(NativeStatus::NotFound);
        return {NativeSendWork(std::move(work)),{}};
    } catch(const std::bad_alloc&) {return Failure<NativeSendWork>(NativeStatus::AllocationFailure);}
}
NativeResult<NativeSendInvocation> NativeSendExecution::TryAdmit(NativeSendWork& work,std::shared_ptr<NativeSendProvider> provider) const noexcept
{
    if(!work||!provider) return Failure<NativeSendInvocation>(NativeStatus::InvalidHandle);
    try {
        auto call=std::unique_ptr<detail::NativeSendInvocationState>(new detail::NativeSendInvocationState);
        auto& reg=*work.state_->registration_;const auto& record=*work.state_->record_;
        const auto status=detail::NativeEgressBridge::Admit(state_,reg,reg.identity_,record.route(),record.generation(),
                record.deadline(),call->ticket_,call->socket_);
        if(status!=NativeStatus::None) return Failure<NativeSendInvocation>(status);
        // All fallible work precedes admission; these moves cannot allocate.
        call->work_=std::move(work.state_);call->provider_=std::move(provider);
        return {NativeSendInvocation(std::move(call)),{}};
    } catch(const std::bad_alloc&) {return Failure<NativeSendInvocation>(NativeStatus::AllocationFailure);}
}
NativeStatus NativeSendExecution::Revoke(EgressRegistration r) const noexcept
{
    if(!r) return NativeStatus::InvalidHandle;
    const auto status=detail::NativeEgressBridge::Revoke(state_,*r.state_);
    if(status==NativeStatus::None) r.state_->runtime_->Close(); // EVERY caller crosses publication barrier.
    return status;
}
NativeEgressUsage NativeSendExecution::SnapshotUsage() const noexcept { return detail::NativeEgressBridge::Usage(state_); }
}
