#ifndef MDDS_DSOFTBUS_PROCESS_COORDINATOR_HPP
#define MDDS_DSOFTBUS_PROCESS_COORDINATOR_HPP
#include <mdds/dsoftbus/ApplicationAttachments.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <mdds/dsoftbus/ProcessReceiveInvocation.hpp>
namespace mdds::dsoftbus {
namespace detail {struct ProcessApplicationState;}
class ProcessCoordinatorTestAccess;
class ProcessApplication final
{
public:
    ProcessApplication() noexcept=default;
    explicit operator bool() const noexcept {return bool(state_);}
private:
    friend class ProcessCoordinator;
    explicit ProcessApplication(std::shared_ptr<detail::ProcessApplicationState> s) noexcept:state_(std::move(s)) {}
    std::shared_ptr<detail::ProcessApplicationState> state_;
};
enum class ProcessApplicationPhase {Invalid,Preparing,Active,Detaching,Detached};
struct ProcessDetachSnapshot
{
    NativeStatus status=NativeStatus::InvalidHandle;
    ProcessApplicationPhase phase=ProcessApplicationPhase::Invalid;
    NativeEgressActivity send;
    std::size_t copying=0,dispatching=0,controls=0;
    bool progressing=false;
};
enum class ProcessShutdownPhase {Open,Pending,ApplicationsDrained};
struct ProcessShutdownSnapshot
{
    ProcessShutdownPhase phase=ProcessShutdownPhase::Open;
    std::size_t applications=0,preparing=0,progressing=0;
    std::size_t admitted=0,submitting=0,copying=0,dispatching=0,controls=0;
};
// Controlled composition model, NOT the production backend. No SDK calls or
// worker threads. The driver keeps this object alive across all member calls.
// Before destruction: BeginShutdown, externally ProgressDetach every application
// until ApplicationsDrained, and join all member callers. Raw receivers remain
// alive through their application's joint drain. Destruction is not a hidden
// drain loop; never destroy/wait on a callback, provider or cleanup stack, or
// while the same thread still holds an unreturned delivery permit.
class ProcessCoordinator final
{
public:
    static NativeResult<std::unique_ptr<ProcessCoordinator>> Build(const NativeTableConfiguration&,
            AttachmentLimits={},ProcessClock={},
            ProcessClock::Clock::duration binding_hold=std::chrono::seconds(1));
    ~ProcessCoordinator();
    ProcessCoordinator(const ProcessCoordinator&)=delete;
    ProcessCoordinator& operator=(const ProcessCoordinator&)=delete;
    // Explicit record capability/queue limits are required; no SDK-size guess.
    NativeResult<ProcessApplication> AttachApplication(const std::vector<Locator>&,
            const std::vector<EgressSelection>&,SendQueueLimits);
    ReceiverOpenOutcome OpenReceiver(const ProcessApplication&,const Locator&,InputChannels::Receiver*);
    AttachmentControlResult CloseReceiver(const ReceiverBinding&);
    SenderAttachResult AttachSender(const ProcessApplication&,eprosima::fastdds::rtps::SendResourceList&);
    NativeResult<NativeSendWork> TakeSend(const ProcessApplication&);
    NativeResult<NativeSendInvocation> TryAdmitSend(NativeSendWork&,std::shared_ptr<NativeSendProvider>);
    ReceiveAdmissionResult Receive(const NativeIdentityConstraint&,const void*,std::size_t,
            ProcessClock::Clock::time_point bind_deadline=ProcessClock::Clock::time_point::max());
    NativeResult<ProcessReceiveInvocation> NextReceive();
    // Short freeze only; T_revoke is the LAST entrance frozen before return.
    // Safe to request from a callback. No ticket cleanup occurs in this call.
    NativeStatus BeginDetach(const ProcessApplication&);
    ProcessDetachSnapshot PollDetach(const ProcessApplication&) const;
    // External control driver only. Can run cleanup (including provider dtor),
    // never concurrently progresses the same bundle. Pending is not success.
    ProcessDetachSnapshot ProgressDetach(const ProcessApplication&);
    void BeginShutdown();
    ProcessShutdownSnapshot PollShutdown() const;
    // Model event injection only. This cannot prove real SDK callback identity,
    // Socket quiescence, or service shutdown. App rebuild leaves N1 alive.
    NativeOwnerTable& ModelNative() noexcept;
    NativeEgressUsage EgressUsage() const noexcept;
    AttachmentUsage ReceiveUsage() const;
private:
    friend class ProcessCoordinatorTestAccess;
    struct State;
    struct TestHooks
    {
        // Outside coordinator gates. Only after_claim/before_publish and the
        // existing copy fault points may throw bad_alloc; other hooks don't throw.
        std::function<void()> after_claim,before_publish,before_rollback,after_receive_selection;
        std::function<void()> copy_reserved,receive_publish;
    };
    static NativeResult<std::unique_ptr<ProcessCoordinator>> BuildWithHooks(const NativeTableConfiguration&,
            AttachmentLimits,ProcessClock,ProcessClock::Clock::duration,TestHooks);
    explicit ProcessCoordinator(std::unique_ptr<State>) noexcept;
    NativeStatus FreezeLocked(detail::ProcessApplicationState&);
    void Rollback(const std::shared_ptr<detail::ProcessApplicationState>&);
    std::unique_ptr<State> state_;
};
}
#endif
