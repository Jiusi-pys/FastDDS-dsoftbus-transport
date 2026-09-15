#include "ProcessReceiveInternal.hpp"
#include <new>
namespace mdds::dsoftbus {
namespace {
template<class T> NativeResult<T> Failure(NativeStatus status) {return {std::nullopt,{status}};}
}
namespace detail {
ProcessReceiveExecution::~ProcessReceiveExecution()
{
    receiver={};native={};work={}; // Receiver/native leases before dispatch ledger refund.
}
ReceiveAdmissionResult ProcessReceiveBridge::Admit(ApplicationAttachments& a,NativeOwnerTable& table,
        const ApplicationClaim& claim,const NativeIdentityConstraint& pin,const void* bytes,std::size_t size,
        ProcessClock::Clock::time_point expiry,const ProcessClock& clock)
{
    if(!bytes||!size) return ReceiveAdmissionResult::InvalidBuffer;
    AttachmentReceiveCapture capture; // Must die AFTER both native guard scopes.
    {
        auto guard=NativeReceiveBridge::Acquire(table,pin);
        if(guard.status()!=NativeStatus::None&&guard.status()!=NativeStatus::NotReady) return ReceiveAdmissionResult::Revoked;
        if(guard.status()==NativeStatus::NotReady&&expiry==ProcessClock::Clock::time_point::max()) return ReceiveAdmissionResult::InvalidDeadline;
        if(guard.status()==NativeStatus::None) expiry=ProcessClock::Clock::time_point::max();
        const auto result=a.CaptureReceive(claim,pin.local(),pin.remote(),pin,expiry,size,clock,capture);
        if(result!=ReceiveAdmissionResult::Accepted) return result;
    }
    const auto copied=a.CopyReceive(capture,bytes);
    if(copied!=ReceiveAdmissionResult::Accepted) return copied;
    {
        auto guard=NativeReceiveBridge::Acquire(table,capture.native_);
        if(guard.status()!=NativeStatus::None&&guard.status()!=NativeStatus::NotReady) return ReceiveAdmissionResult::Revoked;
        return a.CommitReceive(capture,clock);
    }
}
NativeResult<ProcessReceiveInvocation> ProcessReceiveBridge::TryEnter(ApplicationAttachments& a,NativeOwnerTable& table,
        ReceiveWork& work,const ProcessClock& clock)
{
    const auto pin=a.ReceiveIdentity(work);
    if(!pin) return Failure<ProcessReceiveInvocation>(NativeStatus::InvalidHandle);
    // Terminal records must be collectible even during persistent OOM. This
    // nonallocating preflight is NOT permission: final entry repeats validation
    // against the original identities and obtains both real execution leases.
    {
        auto guard=NativeReceiveBridge::Acquire(table,pin);
        if(guard.status()!=NativeStatus::None&&guard.status()!=NativeStatus::NotReady)
            return Failure<ProcessReceiveInvocation>(guard.status());
        const auto pending=a.CheckPendingReceive(work,clock);
        if(pending!=NativeStatus::None) return Failure<ProcessReceiveInvocation>(pending);
        if(guard.status()==NativeStatus::NotReady)
            return Failure<ProcessReceiveInvocation>(NativeStatus::NotReady);
    }
    try {
        auto execution=std::make_unique<ProcessReceiveExecution>();
        {
            auto guard=NativeReceiveBridge::Acquire(table,pin);
            if(guard.status()==NativeStatus::NotReady) {
                const auto pending=a.CheckPendingReceive(work,clock);
                return Failure<ProcessReceiveInvocation>(pending==NativeStatus::None?NativeStatus::NotReady:pending);
            }
            if(guard.status()!=NativeStatus::None) return Failure<ProcessReceiveInvocation>(guard.status());
            execution->native=NativeReceiveBridge::Enter(guard);
            if(!execution->native) return Failure<ProcessReceiveInvocation>(NativeStatus::CounterExhausted);
            const auto result=a.EnterReceive(work,execution->receiver,clock);
            if(result!=NativeStatus::None) return Failure<ProcessReceiveInvocation>(result);
            execution->work=std::move(work);
        }
        return {ProcessReceiveInvocation(std::move(execution)),{}};
    } catch(const std::bad_alloc&) {return Failure<ProcessReceiveInvocation>(NativeStatus::AllocationFailure);}
}
NativeResult<ProcessReceiveInvocation> ProcessReceiveBridge::Next(ApplicationAttachments& a,NativeOwnerTable& table,const ProcessClock& clock)
{
    const auto count=a.SnapshotUsage().queued;
    for(std::size_t i=0;i<count;++i) {
        auto work=a.TryTake();if(!work) break;
        auto result=TryEnter(a,table,work,clock);
        if(result) return result;
        if(result.issue.code==NativeStatus::NotReady||result.issue.code==NativeStatus::AllocationFailure) {
            a.RequeueReceive(work);
            if(result.issue.code==NativeStatus::AllocationFailure) return result;
        }
    }
    return Failure<ProcessReceiveInvocation>(NativeStatus::NotFound);
}
InputDeliveryResult ProcessReceiveBridge::Invoke(ProcessReceiveExecution& execution)
{return ApplicationAttachments::InvokeReceive(execution.work,execution.receiver);}
}
ProcessReceiveInvocation::ProcessReceiveInvocation() noexcept=default;
ProcessReceiveInvocation::~ProcessReceiveInvocation()=default;
ProcessReceiveInvocation::ProcessReceiveInvocation(ProcessReceiveInvocation&&) noexcept=default;
ProcessReceiveInvocation& ProcessReceiveInvocation::operator=(ProcessReceiveInvocation&&) noexcept=default;
ProcessReceiveInvocation::ProcessReceiveInvocation(std::unique_ptr<detail::ProcessReceiveExecution> state) noexcept : state_(std::move(state)) {}
InputDeliveryResult ProcessReceiveInvocation::Run()
{
    auto execution=std::move(state_);if(!execution) return InputDeliveryResult::InvalidTicket;
    return detail::ProcessReceiveBridge::Invoke(*execution);
}
}
