#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_APPLICATION_ATTACHMENTS_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_APPLICATION_ATTACHMENTS_HPP

#include <fastdds_dsoftbus/dsoftbus/InputChannels.hpp>
#include <fastdds_dsoftbus/dsoftbus/NativeOwnerTable.hpp>
#include <fastdds_dsoftbus/dsoftbus/ProcessClock.hpp>
#include <fastdds_dsoftbus/dsoftbus/ReceiveAdmissionDiagnostic.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail {
struct AttachmentApplication;
struct AttachmentsControl;
struct AttachmentRecord;
class ProcessReceiveBridge;
class AttachmentReceiveCapture;
struct ReceiveApplicationActivity;
struct ProcessReceiveExecution;
}
class ApplicationAttachmentsTestAccess;

// Diagnostic views of identities minted by real claim/open publication. They
// never authorize a lookup, delivery, native operation, or SDK generation.
struct ApplicationIdentity
{
    std::uint64_t application_incarnation=0,logical_token=0;
};
struct ReceiverIdentity
{
    std::uint64_t application_incarnation=0,receiver_incarnation=0,logical_token=0;
    bool operator==(const ReceiverIdentity& r) const noexcept
    {return application_incarnation==r.application_incarnation&&receiver_incarnation==r.receiver_incarnation&&logical_token==r.logical_token;}
};
struct ReceiveDiagnostic
{
    std::uint64_t correlation=0;
    ReceiverIdentity identity;
    NativeIdentityConstraint native;
    std::size_t size=0;
};
enum class ReceiveStage {Captured,OwnedCopy,Queued,Admitted,Running,Settled,RecordReleased};
class ProcessReceiveObserver
{
public:
    virtual ~ProcessReceiveObserver()=default;
    // Always outside ownership gates. Bytes are borrowed only for this call;
    // diagnostic IDs are not callback identity supplied by DSoftBus.
    virtual void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept=0;
    // Optional diagnostics, outside gates but still on the original callback
    // stack. Must be bounded/nonblocking; never synchronously close here.
    virtual void OnDecision(const ReceiveDecision&) noexcept {}
};

// Strong, opaque application identity. Copies retain one charged application,
// not a native Socket or permission identity. Empty handles are never admitted.
class ApplicationClaim final
{
public:
    ApplicationClaim() noexcept = default;
    explicit operator bool() const noexcept { return bool(app_); }
private:
    friend class ApplicationAttachments;
    friend class ApplicationAttachmentsTestAccess;
    std::shared_ptr<detail::AttachmentApplication> app_;
};

// A value incarnation in a fixed application-local slot, not a heap object per
// Open. Old handles never cause a key-only Close against a replacement.
class ReceiverBinding final
{
public:
    ReceiverBinding() noexcept = default;
    explicit operator bool() const noexcept { return bool(app_) && incarnation_ != 0; }
    bool operator==(const ReceiverBinding& other) const noexcept
    { return app_ == other.app_ && local_index_ == other.local_index_ && incarnation_ == other.incarnation_; }
private:
    friend class ApplicationAttachments;
    friend class ApplicationAttachmentsTestAccess;
    std::shared_ptr<detail::AttachmentApplication> app_;
    std::size_t local_index_ = 0;
    std::uint64_t incarnation_ = 0;
    std::uint64_t observed_incarnation_ = 0;
};

struct AttachmentLimits
{
    // Experimental host/model limits, not SDK or product performance promises.
    std::size_t logical_slots = 8;
    std::size_t retained_applications = 16;
    std::size_t records = 64;
    std::size_t bytes = 512 * 1024;
    std::size_t local_records = 32;
    std::size_t local_bytes = 256 * 1024;
    std::size_t copying = 8;
    std::size_t max_record_bytes = 60000;
};

struct AttachmentUsage
{
    std::size_t logical_slots = 0;
    std::size_t active_applications = 0;
    std::size_t detaching_applications = 0;
    std::size_t retained_applications = 0;
    std::size_t records = 0, bytes = 0, copying = 0, queued = 0, taken = 0, delivering = 0;
    std::size_t high_applications = 0, high_records = 0, high_bytes = 0, high_copying = 0;
};

enum class ApplicationClaimResult { Claimed, InvalidLocals, InvalidDirectory, InUse,
    Detaching, Capacity, AllocationFailure, Stopped };
enum class ReceiverOpenResult { Opened, AlreadyOpen, InvalidHandle, InvalidLocal,
    NullReceiver, Conflict, Busy, Detached, AllocationFailure, IncarnationExhausted };
enum class AttachmentControlResult { Completed, Requested, AlreadyDetached, InvalidHandle };
enum class ReceiveAdmissionResult { Accepted, InvalidLocal, InvalidRoute, NoReceiver,
    InvalidBuffer, TooLarge, Capacity, Revoked, AllocationFailure, Stopped, InvalidDeadline, Expired };
enum class ReceiveProducerState { InvalidHandle, Open, Pending, Drained };
struct ApplicationClaimOutcome { ApplicationClaimResult result; ApplicationClaim claim; };
struct ReceiverOpenOutcome { ReceiverOpenResult result; ReceiverBinding binding; };

class ReceiveWork final
{
public:
    ReceiveWork() noexcept;
    ~ReceiveWork();
    ReceiveWork(ReceiveWork&&) noexcept;
    ReceiveWork& operator=(ReceiveWork&&) noexcept;
    ReceiveWork(const ReceiveWork&) = delete;
    ReceiveWork& operator=(const ReceiveWork&) = delete;
    explicit operator bool() const noexcept;
    // Consumes this work exactly once, including on callback failure. External
    // delivery thread only; never call from a native SDK callback to enter RTPS.
    // May outlive the control object: its original core is retained and disabled
    // by control destruction. Never looks up a current application or receiver.
    InputDeliveryResult Deliver();
private:
    friend class ApplicationAttachments;
    explicit ReceiveWork(std::unique_ptr<detail::AttachmentRecord> record) noexcept;
    std::unique_ptr<detail::AttachmentRecord> record_;
};

class ApplicationAttachments final
{
public:
    using Receiver = InputChannels::Receiver;
    // Throws invalid_argument for empty/noncanonical/duplicate slots or invalid
    // limits; throws bad_alloc without publication. Slots never grow afterward.
    explicit ApplicationAttachments(const std::vector<Locator>& fixed_locals,
            AttachmentLimits limits = {});
    ~ApplicationAttachments();
    ApplicationAttachments(const ApplicationAttachments&) = delete;
    ApplicationAttachments& operator=(const ApplicationAttachments&) = delete;
    ApplicationAttachments(ApplicationAttachments&&) = delete;
    ApplicationAttachments& operator=(ApplicationAttachments&&) = delete;

    ApplicationClaimOutcome ClaimApplication(const std::vector<Locator>& local_set,
            EndpointDirectory::Snapshot directory);
    ReceiverOpenOutcome OpenReceiver(const ApplicationClaim& claim,
            const Locator& local, Receiver* receiver);
    AttachmentControlResult CloseReceiver(const ReceiverBinding& binding);
    AttachmentControlResult RequestDetach(const ApplicationClaim& claim);
    AttachmentControlResult AwaitDetached(const ApplicationClaim& claim);
    // Immediate bounded admission, no queue-space wait. Input bytes must remain
    // valid and unchanged throughout the entire call. A successful admission is
    // queued, not delivered/authenticated. No-app bytes are never saved for P1.
    // Optional diagnostic is caller-owned and fresh for each invocation; nested
    // gates append scalar observations without retaining its address.
    ReceiveAdmissionResult AdmitReceive(const Locator& local, const Locator& remote,
            const void* bytes, std::size_t size,ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    ReceiveWork TryTake();
    AttachmentUsage SnapshotUsage() const;
    ApplicationIdentity ObserveApplication(const ApplicationClaim&) const noexcept;
    ReceiverIdentity ObserveReceiver(const ReceiverBinding&) const noexcept;

    // Raw receivers belong to the caller and remain alive through the entire
    // Open operation and successful registration's Close/Detach drain. All
    // control/destruction is external: never synchronously on ANY receiver
    // callback stack (including another local). There is no drain timeout or
    // callback cancellation guarantee. Keep this object alive during member
    // calls; concurrent destruction is unsupported. This module starts no
    // threads and makes no SDK calls.
private:
    friend class ApplicationAttachmentsTestAccess;
    friend class ServiceRecordBinding;
    ApplicationClaimOutcome ClaimSharedApplication(const std::vector<Locator>&,
            EndpointDirectory::Snapshot);
    ApplicationClaimOutcome ClaimApplicationImpl(const std::vector<Locator>&,
            EndpointDirectory::Snapshot, bool shared);
    friend class detail::ProcessReceiveBridge;
    friend struct detail::ProcessReceiveExecution;
    friend class ProcessCoordinator;
    // N3 nonblocking observation of the original app, not a full Detach result.
    // Open never means drained; RequestDetach must close admission first. No
    // cleanup, wait, local-slot release or current-application lookup here.
    ReceiveProducerState PollReceiveProducers(const ApplicationClaim&) const;
    std::size_t TestCopying(const ApplicationClaim&) const;
    ReceiveAdmissionResult CaptureReceive(const ApplicationClaim&,const Locator&,const Locator&,
            const NativeIdentityConstraint&,ProcessClock::Clock::time_point,std::size_t,
            const ProcessClock&,detail::AttachmentReceiveCapture&,ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    ReceiveAdmissionResult CopyReceive(detail::AttachmentReceiveCapture&,const void*,ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    ReceiveAdmissionResult CommitReceive(detail::AttachmentReceiveCapture&,const ProcessClock&,ReceiveAdmissionDiagnostic* diagnostic=nullptr);
    NativeIdentityConstraint ReceiveIdentity(const ReceiveWork&) const;
    ReceiveDiagnostic ReceiveObservation(const ReceiveWork&) const;
    static void ObserveReceive(const ReceiveWork&,ReceiveStage) noexcept;
    static void ObserveOwnedCopy(const detail::AttachmentReceiveCapture&) noexcept;
    NativeStatus CheckPendingReceive(const ReceiveWork&,const ProcessClock&) const;
    NativeStatus EnterReceive(ReceiveWork&,InputReceiverLease&,const ProcessClock&);
    static InputDeliveryResult InvokeReceive(ReceiveWork&,InputReceiverLease&);
    bool RequeueReceive(ReceiveWork&);
    AttachmentControlResult FreezeApplication(const ApplicationClaim&);
    detail::ReceiveApplicationActivity ReceiveActivity(const ApplicationClaim&) const;
    struct TestHooks
    {
        // Outside the global gate; synchronization hooks must not throw except
        // the two named fault points, which may inject bad_alloc only.
        std::function<void()> open_registered;
        std::function<void()> copy_reserved;
        std::function<void()> receive_publish;
        std::function<void()> close_wait;
        std::function<void()> detach_wait;
    };
    ApplicationAttachments(const std::vector<Locator>&, AttachmentLimits, TestHooks);
    void TestSetIncarnation(const ApplicationClaim&, const Locator&, std::uint64_t);
    void TestSetCopyingActivity(const ApplicationClaim&,std::size_t);
    std::weak_ptr<InputChannels> TestCore(const ApplicationClaim&) const;
    std::weak_ptr<void> TestBudget() const;
    std::unique_ptr<detail::AttachmentsControl> control_;
    TestHooks hooks_;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
