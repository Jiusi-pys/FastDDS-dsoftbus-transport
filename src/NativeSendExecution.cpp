#include <fastdds_dsoftbus/dsoftbus/NativeSendExecution.hpp>
#include "NativeSendExecutionInternal.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace fastdds_dsoftbus::dsoftbus {
namespace detail {
EgressState::~EgressState() noexcept
{
    NativeEgressBridge::Revoke(ceiling_->state_,*this);
    runtime_->Close(); // No native gate held, including unpublished cleanup.
}
bool NativeGenerationAdapter::TryResolve(const Route& r,SendConnectionSnapshot& out) const
{return NativeEgressBridge::Resolve(*ceiling_,r,out);}
NativeSendResources::~NativeSendResources() noexcept = default;
NativeSendInvocationState::~NativeSendInvocationState() noexcept
{
    std::shared_ptr<NativeSendResources> cleanup;
    NativeEgressBridge::CancelInvocation(*this,cleanup);
    if(cleanup) {
        cleanup.reset(); // Ticket/provider/record cleanup precedes terminal count.
        NativeEgressBridge::SettleInvocation(*this);
    }
}
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
NativeSendInvocation::NativeSendInvocation(std::shared_ptr<detail::NativeSendInvocationState> s) noexcept : state_(std::move(s)) {}
NativeSendRunResult NativeSendInvocation::Run() noexcept
{
    if(!state_) return {NativeStatus::InvalidHandle};
    auto call=std::move(state_); // Once only; releases every borrow after actual return/unwind.
    std::shared_ptr<detail::NativeSendResources> resources;
    const auto entered=detail::NativeEgressBridge::EnterInvocation(*call,resources);
    NativeSendRunResult result{entered};
    if(entered==NativeStatus::None) {
        const auto& record=*resources->work_->record_;
        try {
            const auto code=resources->provider_->Send(resources->socket_,record.route(),record.data(),
                    record.size(),record.deadline(),record.priority());
            if(code>0) {
                const auto fault=detail::NativeEgressBridge::Fault(resources->ticket_,NativeProviderFault::InvalidPositiveReturn,code);
                result={fault.status,NativeSendOutcome::ReturnedInvalidCode,code};
            } else {
                const auto completed=resources->ticket_.Complete(code);
                result={completed.status,NativeSendOutcome::ReturnedSdkCode,code};
            }
        } catch(...) {
            const auto fault=detail::NativeEgressBridge::Fault(resources->ticket_,NativeProviderFault::ExceptionUnwind,std::nullopt);
            result={fault.status,NativeSendOutcome::ReturnedByException,std::nullopt};
        }
    }
    if(resources) {
        resources.reset();
        detail::NativeEgressBridge::SettleInvocation(*call);
    } // Otherwise the cancellation winner owns cleanup and terminal settlement.
    return result;
}
NativeRevokeTicket::~NativeRevokeTicket() noexcept { Progress(); }
NativeRevokeTicket::NativeRevokeTicket(NativeRevokeTicket&& other) noexcept
    : registration_(std::move(other.registration_)),controls_(std::move(other.controls_)),resources_(std::move(other.resources_)) {}
NativeRevokeTicket& NativeRevokeTicket::operator=(NativeRevokeTicket&& other) noexcept
{
    if(this!=&other) {
        Progress();
        registration_=std::move(other.registration_);
        controls_=std::move(other.controls_);resources_=std::move(other.resources_);
    }
    return *this;
}
void NativeRevokeTicket::Progress() noexcept
{
    if(!registration_) return;
    registration_->runtime_->Close();
    for(std::size_t i=0;i<resources_.size();++i) {
        if(resources_[i]) {
            resources_[i].reset();
            detail::NativeEgressBridge::SettleInvocation(*controls_[i]);
        }
    }
    detail::NativeEgressBridge::QueueBarrier(*registration_);
}
NativeSendExecution::NativeSendExecution(NativeOwnerTable& table) noexcept : state_(detail::NativeEgressBridge::Bind(table)) {}
NativeResult<EgressRegistration> NativeSendExecution::CreateRuntime(const NativeTableConfiguration& config,
        const std::vector<EgressSelection>& selections, SendQueueLimits limits) const noexcept
{return CreateRuntimeImpl(config,nullptr,selections,limits);}
NativeResult<EgressRegistration> NativeSendExecution::CreateRuntimeForLocals(const NativeTableConfiguration& config,
        const std::vector<Locator>& locals,const std::vector<EgressSelection>& selections,SendQueueLimits limits) const noexcept
{return CreateRuntimeImpl(config,&locals,selections,limits);}
NativeResult<EgressRegistration> NativeSendExecution::CreateRuntimeImpl(const NativeTableConfiguration& config,
        const std::vector<Locator>* locals,const std::vector<EgressSelection>& selections,SendQueueLimits limits) const noexcept
{
    try {
        auto ceiling=std::shared_ptr<detail::RuntimeCeilingLease>(new detail::RuntimeCeilingLease(state_,limits));
        const auto reserved=detail::NativeEgressBridge::Reserve(*ceiling,config);
        if(reserved!=NativeStatus::None) return Failure<EgressRegistration>(reserved);
        std::unique_ptr<DirectoryConfiguration> projection;
        if(locals) {
            if(locals->empty()) return Failure<EgressRegistration>(NativeStatus::InvalidConfiguration);
            // Throwing vector construction avoids Debug STL's noexcept default
            // constructor allocation trap. All copying remains outside gates.
            projection.reset(new DirectoryConfiguration{
                std::vector<LocalBinding>(std::size_t{0}),std::vector<Route>(std::size_t{0})});
            projection->locals.reserve(locals->size());
            for(std::size_t i=0;i<locals->size();++i) {
                const auto& local=(*locals)[i];
                if(!LocatorCodec::IsValid(local)||std::find(locals->begin(),locals->begin()+i,local)!=locals->begin()+i)
                    return Failure<EgressRegistration>(NativeStatus::InvalidConfiguration);
                const auto found=std::find_if(config.directory.locals.begin(),config.directory.locals.end(),
                        [&](const LocalBinding& binding){return binding.local_locator==local;});
                if(found==config.directory.locals.end()) return Failure<EgressRegistration>(NativeStatus::InvalidConfiguration);
                projection->locals.push_back(*found);
            }
            for(const auto& route:config.directory.routes)
                if(std::find(locals->begin(),locals->end(),route.key.local_locator)!=locals->end())
                    projection->routes.push_back(route);
        }
        const auto plan=EgressPlan::Build(projection?*projection:config.directory,selections);
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
        auto call=std::shared_ptr<detail::NativeSendInvocationState>(new detail::NativeSendInvocationState);
        auto resources=std::shared_ptr<detail::NativeSendResources>(new detail::NativeSendResources);
        auto& reg=*work.state_->registration_;const auto& record=*work.state_->record_;
        call->registration_=work.state_->registration_;call->deadline_=record.deadline();
        resources->provider_=std::move(provider);resources->work_=std::move(work.state_);
        call->resources_=std::move(resources);
        const auto status=detail::NativeEgressBridge::Admit(state_,reg,reg.identity_,record.route(),record.generation(),
                record.deadline(),call->resources_->ticket_,call->resources_->socket_,call);
        if(status!=NativeStatus::None) {
            work.state_=std::move(call->resources_->work_);
            return Failure<NativeSendInvocation>(status);
        }
        // Published resources may already have been removed by strong revoke.
        // No post-admission dereference of the record or resource package.
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
NativeResult<NativeRevokeTicket> NativeSendExecution::BeginRevoke(EgressRegistration r) const noexcept
{
    if(!r) return Failure<NativeRevokeTicket>(NativeStatus::InvalidHandle);
    NativeRevokeTicket ticket;
    const auto status=detail::NativeEgressBridge::BeginRevoke(state_,r.state_,ticket);
    if(status!=NativeStatus::None) return Failure<NativeRevokeTicket>(status);
    return {std::move(ticket),{}};
}
NativeEgressActivity NativeSendExecution::Observe(EgressRegistration r) const noexcept
{return r?detail::NativeEgressBridge::Observe(state_,*r.state_):NativeEgressActivity{};}
NativeEgressUsage NativeSendExecution::SnapshotUsage() const noexcept { return detail::NativeEgressBridge::Usage(state_); }
}
