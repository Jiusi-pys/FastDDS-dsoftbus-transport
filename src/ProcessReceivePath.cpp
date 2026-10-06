#include "ProcessReceiveInternal.hpp"
#include <new>
namespace fastdds_dsoftbus::dsoftbus {
namespace {
template<class T> NativeResult<T> Failure(NativeStatus status) {return {std::nullopt,{status}};}
}
namespace detail {
ProcessReceiveExecution::~ProcessReceiveExecution()
{
    receiver={};native={};
    ApplicationAttachments::ObserveReceive(work,ReceiveStage::Settled);
    work={}; // Receiver/native leases before dispatch ledger refund.
}
ReceiveAdmissionResult ProcessReceiveBridge::Admit(ApplicationAttachments& a,NativeOwnerTable& table,
        const ApplicationClaim& claim,const NativeIdentityConstraint& pin,const void* bytes,std::size_t size,
        ProcessClock::Clock::time_point expiry,const ProcessClock& clock,std::uint64_t correlation,
        std::shared_ptr<ProcessReceiveObserver> observer,ReceiveAdmissionDiagnostic* diagnostic)
{
    if(diagnostic) diagnostic->size=size;
    if(!bytes||!size) return diagnostic?diagnostic->Return(RxAdmissionStage::CaptureGate,
            !bytes?RxAdmissionReason::NullBuffer:RxAdmissionReason::ZeroSize,
            ReceiveAdmissionResult::InvalidBuffer):ReceiveAdmissionResult::InvalidBuffer;
    AttachmentReceiveCapture capture; // Must die AFTER both native guard scopes.
    capture.diagnostic_.correlation=correlation;capture.observer_=std::move(observer);
    {
        auto guard=NativeReceiveBridge::Acquire(table,pin);
        if(diagnostic) {diagnostic->native_status_known=true;diagnostic->native_status=guard.status();}
        if(guard.status()!=NativeStatus::None&&guard.status()!=NativeStatus::NotReady)
            return diagnostic?diagnostic->Return(RxAdmissionStage::CaptureGate,RxAdmissionReason::NativeCaptureRejected,
                    ReceiveAdmissionResult::Revoked):ReceiveAdmissionResult::Revoked;
        if(guard.status()==NativeStatus::NotReady&&expiry==ProcessClock::Clock::time_point::max())
            return diagnostic?diagnostic->Return(RxAdmissionStage::CaptureGate,RxAdmissionReason::InvalidDeadline,
                    ReceiveAdmissionResult::InvalidDeadline):ReceiveAdmissionResult::InvalidDeadline;
        if(guard.status()==NativeStatus::None) expiry=ProcessClock::Clock::time_point::max();
        const auto result=a.CaptureReceive(claim,pin.local(),pin.remote(),pin,expiry,size,clock,capture,diagnostic);
        if(result!=ReceiveAdmissionResult::Accepted) return result;
    }
    if(capture.observer_) capture.observer_->OnReceive(ReceiveStage::Captured,capture.diagnostic_,
            static_cast<const std::uint8_t*>(bytes),size);
    const auto copied=a.CopyReceive(capture,bytes,diagnostic);
    if(copied!=ReceiveAdmissionResult::Accepted) return copied;
    ApplicationAttachments::ObserveOwnedCopy(capture);
    ReceiveAdmissionResult result;
    {
        auto guard=NativeReceiveBridge::Acquire(table,capture.native_);
        if(diagnostic) {diagnostic->native_status_known=true;diagnostic->native_status=guard.status();}
        if(guard.status()!=NativeStatus::None&&guard.status()!=NativeStatus::NotReady)
            return diagnostic?diagnostic->Return(RxAdmissionStage::CommitGate,RxAdmissionReason::NativeCommitRejected,
                    ReceiveAdmissionResult::Revoked):ReceiveAdmissionResult::Revoked;
        result=a.CommitReceive(capture,clock,diagnostic);
    }
    if(result==ReceiveAdmissionResult::Accepted&&capture.observer_)
        capture.observer_->OnReceive(ReceiveStage::Queued,capture.diagnostic_,nullptr,0);
    return result;
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
            execution->diagnostic=a.ReceiveObservation(execution->work);
            execution->owner=&a;
        }
        ApplicationAttachments::ObserveReceive(execution->work,ReceiveStage::Admitted);
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
{
    ApplicationAttachments::ObserveReceive(execution.work,ReceiveStage::Running);
    return ApplicationAttachments::InvokeReceive(execution.work,execution.receiver);
}
void ProcessReceiveBridge::ArmDeliveryContext(ProcessReceiveInvocation& invocation,
        void (*enter)(void*) noexcept,void (*exit)(void*) noexcept) noexcept
{
    if(!invocation.state_) return;
    invocation.state_->enter_context=enter;invocation.state_->exit_context=exit;
}
}
ProcessReceiveInvocation::ProcessReceiveInvocation() noexcept=default;
ProcessReceiveInvocation::~ProcessReceiveInvocation()=default;
ProcessReceiveInvocation::ProcessReceiveInvocation(ProcessReceiveInvocation&&) noexcept=default;
ProcessReceiveInvocation& ProcessReceiveInvocation::operator=(ProcessReceiveInvocation&&) noexcept=default;
ProcessReceiveInvocation::ProcessReceiveInvocation(std::unique_ptr<detail::ProcessReceiveExecution> state) noexcept : state_(std::move(state)) {}
InputDeliveryResult ProcessReceiveInvocation::Run()
{
    auto execution=std::move(state_);if(!execution) return InputDeliveryResult::InvalidTicket;
    // The delivery context belongs to the execution, not to RunAdmitted: a caller
    // that runs the invocation directly enters exactly the same context.
    if(execution->enter_context) execution->enter_context(execution.get());
    struct ExitGuard
    {
        detail::ProcessReceiveExecution* execution;
        ~ExitGuard() {if(execution->exit_context) execution->exit_context(execution);}
    } guard{execution.get()};
    return detail::ProcessReceiveBridge::Invoke(*execution);
}
ReceiveDiagnostic ProcessReceiveInvocation::Observation() const noexcept
{return state_?state_->diagnostic:ReceiveDiagnostic{};}
}
