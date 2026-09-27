#pragma once
#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mdds::dsoftbus {
// Value-only observations. No field grants authority or retains a payload,
// native pin, application, receiver, or execution lease.
enum class RxAdmissionStage { Unobserved, NativeEntry, Adapter, NativeCallback,
    NativeIdentity, Coordinator, CaptureGate, Capture, Copy, CommitGate, Commit };
enum class RxAdmissionReason { Unobserved, Retired, RuntimeMissing, OwnerMissing,
    WrongRole, CloseClaimed, NotReady, Closing, NullBuffer, ZeroSize, TooLarge,
    NativeCallbackRejected, NativeIdentityRejected, InvalidPin, NoApplication,
    ClockOverflow, NativeCaptureRejected, InvalidDeadline, InvalidClaim,
    InvalidLocal, Stopped, ApplicationInactive, ReceiverNotOpen, InvalidRoute,
    Expired, GlobalRecords, GlobalBytes, LocalRecords, LocalBytes, CopyingLimit,
    ApplicationCopyingExhausted, CaptureAllocation, InvalidCopy, CopyAllocation,
    NativeCommitRejected, InvalidCommit, CommitApplicationInactive,
    CommitReceiverNotOpen, CommitReceiverChanged, AcceptedQueue, CallbackException, Count };
inline const char* RxAdmissionStageName(RxAdmissionStage value) noexcept {
    constexpr const char* names[]={"UNOBSERVED","NATIVE_ENTRY","ADAPTER","NATIVE_CALLBACK",
        "NATIVE_IDENTITY","COORDINATOR","CAPTURE_GATE","CAPTURE","COPY","COMMIT_GATE","COMMIT"};
    const auto i=static_cast<std::size_t>(value);return i<sizeof(names)/sizeof(*names)?names[i]:"UNOBSERVED";
}
inline const char* RxAdmissionReasonName(RxAdmissionReason value) noexcept {
    constexpr const char* names[]={"UNOBSERVED","RETIRED","RUNTIME_MISSING","OWNER_MISSING",
        "WRONG_ROLE","CLOSE_CLAIMED","NOT_READY","CLOSING","NULL_BUFFER","ZERO_SIZE","TOO_LARGE",
        "NATIVE_CALLBACK_REJECTED","NATIVE_IDENTITY_REJECTED","INVALID_PIN","NO_APPLICATION",
        "CLOCK_OVERFLOW","NATIVE_CAPTURE_REJECTED","INVALID_DEADLINE","INVALID_CLAIM",
        "INVALID_LOCAL","STOPPED","APPLICATION_INACTIVE","RECEIVER_NOT_OPEN","INVALID_ROUTE",
        "EXPIRED","GLOBAL_RECORDS","GLOBAL_BYTES","LOCAL_RECORDS","LOCAL_BYTES","COPYING_LIMIT",
        "APPLICATION_COPYING_EXHAUSTED","CAPTURE_ALLOCATION","INVALID_COPY","COPY_ALLOCATION",
        "NATIVE_COMMIT_REJECTED","INVALID_COMMIT","COMMIT_APPLICATION_INACTIVE",
        "COMMIT_RECEIVER_NOT_OPEN","COMMIT_RECEIVER_CHANGED","ACCEPTED_QUEUE","CALLBACK_EXCEPTION"};
    const auto i=static_cast<std::size_t>(value);return i<sizeof(names)/sizeof(*names)?names[i]:"UNOBSERVED";
}
struct ReceiveAdmissionDiagnostic {
    RxAdmissionStage stage=RxAdmissionStage::Unobserved;
    RxAdmissionReason reason=RxAdmissionReason::Unobserved;
    bool result_known=false,native_status_known=false,native_known=false;
    bool application_known=false,receiver_known=false,capacity_known=false,lifecycle_known=false;
    int result=-1;NativeStatus native_status=NativeStatus::NotImplemented;
    std::uint32_t candidate_count=0,capacity_mask=0;
    std::size_t size=0;
    bool record_limit_known=false;
    std::size_t record_limit=0;
    std::uint64_t native_slot=0,native_incarnation=0,native_generation=0,native_route_version=0;
    std::uint64_t application_incarnation=0,logical_token=0,receiver_incarnation=0;
    int application_phase=-1,receiver_phase=-1;
    bool stopped=false;
    std::size_t records_used=0,records_limit=0,bytes_used=0,bytes_limit=0;
    std::size_t local_records_used=0,local_records_limit=0,local_bytes_used=0,local_bytes_limit=0;
    std::size_t copying_used=0,copying_limit=0,application_copying=0,application_copying_limit=0;
    void Decide(RxAdmissionStage s,RxAdmissionReason r) noexcept {stage=s;reason=r;}
    template<class Result> Result Return(RxAdmissionStage s,RxAdmissionReason r,Result value) noexcept {
        Decide(s,r);result_known=true;result=static_cast<int>(value);return value;
    }
};
static_assert(std::is_trivially_copyable<ReceiveAdmissionDiagnostic>::value,"diagnostic must be value-only");
// Local observations, never SDK identities or authority to resolve a receiver.
// Each branch names the check actually executed; a skipped bridge has no result.
enum class ReceiveDecisionLayer { Binding, Adapter };
enum class ReceiveDecisionBranch {
    Unknown, BindingPhase, NoRegistration, NativeAdmission, NativeIdentity,
    LocalMismatch, BridgeAccepted, BridgeRefused,
    AdapterStopped, AdapterPhase, ChannelClosed, ForeignSocket, InvalidBuffer,
    AdapterCapacity, Forwarded, Count
};
struct ReceiveDecision {
    ReceiveDecisionLayer layer=ReceiveDecisionLayer::Binding;
    ReceiveDecisionBranch branch=ReceiveDecisionBranch::Unknown;
    std::uint64_t sequence=0,record_incarnation=0,correlation=0;
    std::int32_t socket=-1;
    int role=-1,phase=-1;
    std::size_t size=0;
    bool bridge_called=false,refused_receive=false;
    std::size_t refused_receive_total=0; // captured only on the incrementing branch
    bool status_known=false;
    int status=-1; // NativeStatus for Binding; BackendStatus for Adapter, never SDK rc.
    ReceiveAdmissionDiagnostic bridge;
};
static_assert(std::is_trivially_copyable<ReceiveDecision>::value,"decision must be value-only");
}
