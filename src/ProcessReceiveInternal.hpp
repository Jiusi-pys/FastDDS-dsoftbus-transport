#ifndef MDDS_PROCESS_RECEIVE_INTERNAL_HPP
#define MDDS_PROCESS_RECEIVE_INTERNAL_HPP
#include <mdds/dsoftbus/ProcessReceiveInvocation.hpp>
#include "ApplicationAttachmentsInternal.hpp"
#include "NativeReceiveInternal.hpp"
namespace mdds::dsoftbus {class ProcessCoordinator;class ProcessReceiveTestAccess;class ServiceRecordBinding;}
namespace mdds::dsoftbus::detail {
struct ProcessReceiveExecution final
{
    ~ProcessReceiveExecution();
    ReceiveWork work;
    NativeReceiveLease native;
    InputReceiverLease receiver;
    ReceiveDiagnostic diagnostic;
    // The attachments instance that owns this work's receiver registration. Two
    // instances mint application identities from independent sequences, so the
    // identity alone does not name a registration: the owner has to be part of it.
    const void* owner = nullptr;
    // Delivery-context hooks, armed by whichever composition issued this
    // invocation. They run around the actual delivery, outside every component
    // gate, on whichever thread runs it and through whichever entry point, so a
    // context that must not reenter control operations is marked even when the
    // caller invokes ProcessReceiveInvocation::Run() itself. Both must not throw,
    // and both receive this execution, whose diagnostic carries the application
    // identity the delivery runs under: the work a frame runs is not necessarily
    // the work its own binding admitted, because the queue is shared by every
    // registration on one attachments instance.
    void (*enter_context)(void*) noexcept = nullptr;
    void (*exit_context)(void*) noexcept = nullptr;
};
class ProcessReceiveBridge final
{
    friend class ::mdds::dsoftbus::ProcessCoordinator;
    friend class ::mdds::dsoftbus::ProcessReceiveTestAccess;
    // The record/native-owner/attachments composition uses the same pin-based
    // admission and delivery path instead of reimplementing it.
    friend class ::mdds::dsoftbus::ServiceRecordBinding;
    friend class ::mdds::dsoftbus::ProcessReceiveInvocation;
    static ReceiveAdmissionResult Admit(ApplicationAttachments&,NativeOwnerTable&,const ApplicationClaim&,
            const NativeIdentityConstraint&,const void*,std::size_t,ProcessClock::Clock::time_point,
            const ProcessClock&,std::uint64_t correlation=0,
            std::shared_ptr<ProcessReceiveObserver> observer={},ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    static NativeResult<ProcessReceiveInvocation> TryEnter(ApplicationAttachments&,NativeOwnerTable&,
            ReceiveWork&,const ProcessClock&);
    static NativeResult<ProcessReceiveInvocation> Next(ApplicationAttachments&,NativeOwnerTable&,const ProcessClock&);
    static InputDeliveryResult Invoke(ProcessReceiveExecution&);
    // Arms the delivery context of one already-admitted invocation. Only the
    // composition that issued it may arm it, and the hooks must be callable without
    // holding any component gate. Both hooks are called with the execution itself.
    static void ArmDeliveryContext(ProcessReceiveInvocation&, void (*)(void*) noexcept,
            void (*)(void*) noexcept) noexcept;
};
}
#endif
