#ifndef MDDS_PROCESS_RECEIVE_INTERNAL_HPP
#define MDDS_PROCESS_RECEIVE_INTERNAL_HPP
#include <mdds/dsoftbus/ProcessReceiveInvocation.hpp>
#include "ApplicationAttachmentsInternal.hpp"
#include "NativeReceiveInternal.hpp"
namespace mdds::dsoftbus {class ProcessCoordinator;class ProcessReceiveTestAccess;}
namespace mdds::dsoftbus::detail {
struct ProcessReceiveExecution final
{
    ~ProcessReceiveExecution();
    ReceiveWork work;
    NativeReceiveLease native;
    InputReceiverLease receiver;
    ReceiveDiagnostic diagnostic;
};
class ProcessReceiveBridge final
{
    friend class ::mdds::dsoftbus::ProcessCoordinator;
    friend class ::mdds::dsoftbus::ProcessReceiveTestAccess;
    friend class ::mdds::dsoftbus::ProcessReceiveInvocation;
    static ReceiveAdmissionResult Admit(ApplicationAttachments&,NativeOwnerTable&,const ApplicationClaim&,
            const NativeIdentityConstraint&,const void*,std::size_t,ProcessClock::Clock::time_point,
            const ProcessClock&,std::uint64_t correlation=0,
            std::shared_ptr<ProcessReceiveObserver> observer={},ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    static NativeResult<ProcessReceiveInvocation> TryEnter(ApplicationAttachments&,NativeOwnerTable&,
            ReceiveWork&,const ProcessClock&);
    static NativeResult<ProcessReceiveInvocation> Next(ApplicationAttachments&,NativeOwnerTable&,const ProcessClock&);
    static InputDeliveryResult Invoke(ProcessReceiveExecution&);
};
}
#endif
