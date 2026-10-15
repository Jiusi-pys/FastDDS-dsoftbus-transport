#include <fastdds_dsoftbus/dsoftbus/ApplicationAttachments.hpp>
#include "ApplicationAttachmentsInternal.hpp"
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
#include <fastdds_dsoftbus/dsoftbus/HostReceiveJournal.hpp>
#endif
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail {
enum class ApplicationPhase { Active, Detaching, Detached };
enum class ReceiverPhase { Empty, Opening, Open, Closing };
enum class RecordPhase { Copying, Queued, Taken, Delivering };
struct AttachmentLocalBudget { std::size_t records = 0, bytes = 0; };
struct AttachmentBudget final
{
    AttachmentBudget(AttachmentLimits n, std::size_t slots) : limits(n), locals(slots)
    { usage.logical_slots = slots; }
    const AttachmentLimits limits;
    std::mutex mutex;
    std::condition_variable changed;
    AttachmentUsage usage;
    std::uint64_t next_identity=1;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    const std::uint64_t observation_scope=host_rx_observation::NewScope();
    std::uint64_t observation_gate=0,observation_record=0;
#endif
    std::vector<AttachmentLocalBudget> locals;
};
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
// Called only under the original budget mutex. Fixed scalar copy and bounded
// lock-free publication; no callback, allocation, payload or extra gate.
std::uint64_t ObserveBudget(AttachmentBudget& b,host_rx_observation::Kind kind,
        std::size_t index,const ReceiveDiagnostic& diagnostic,RecordPhase phase,
        std::size_t application_copying=0,std::uint32_t capacity_mask=0) noexcept
{
    if(!b.observation_scope)return 0;
    host_rx_observation::Event event;event.kind=kind;
    event.scope=b.observation_scope;event.record=diagnostic.observation_record;
    event.gate=host_rx_observation::Next(b.observation_gate);
    event.correlation=diagnostic.correlation;event.application=diagnostic.identity.application_incarnation;
    event.receiver=diagnostic.identity.receiver_incarnation;event.logical_token=diagnostic.identity.logical_token;
    event.local=index;event.size=diagnostic.size;event.phase=static_cast<int>(phase);
    event.records=b.usage.records;event.bytes=b.usage.bytes;event.copying=b.usage.copying;
    event.queued=b.usage.queued;event.taken=b.usage.taken;event.delivering=b.usage.delivering;
    event.local_records=b.locals[index].records;event.local_bytes=b.locals[index].bytes;
    event.records_limit=b.limits.records;event.bytes_limit=b.limits.bytes;
    event.local_records_limit=b.limits.local_records;event.local_bytes_limit=b.limits.local_bytes;
    event.copying_limit=b.limits.copying;
    if(kind==host_rx_observation::Kind::CapacityRefusal){
        event.application_copying_known=true;event.application_copying=application_copying;
        event.application_copying_limit=std::numeric_limits<std::size_t>::max();event.capacity_mask=capacity_mask;
        event.capacity_reason=static_cast<int>(capacity_mask&1u?RxAdmissionReason::GlobalRecords:
                capacity_mask&2u?RxAdmissionReason::GlobalBytes:capacity_mask&4u?RxAdmissionReason::LocalRecords:
                capacity_mask&8u?RxAdmissionReason::LocalBytes:capacity_mask&16u?RxAdmissionReason::CopyingLimit:
                RxAdmissionReason::ApplicationCopyingExhausted);
    }
    event.ingress=host_rx_observation::CurrentIngress();
    host_rx_observation::Append(event);return event.gate;
}
#endif
struct AttachmentLocal final
{
    Locator locator;
    std::size_t budget_index = 0;
    std::uint64_t incarnation = 0;
    std::uint64_t observed_incarnation = 0;
    ReceiverPhase phase = ReceiverPhase::Empty;
    InputChannels::Receiver* receiver = nullptr;
    InputChannelTicket ticket;
};
struct AttachmentApplication final
{
    AttachmentApplication(std::shared_ptr<AttachmentBudget> b, EndpointDirectory::Snapshot d,
            std::size_t count) : budget(std::move(b)), directory(std::move(d)),
        inputs(std::make_shared<InputChannels>(directory, budget->limits.max_record_bytes)), locals(count) {}
    ~AttachmentApplication()
    {
        // Member destruction, including the already drained core, is outside
        // this gate. Failed preparation never registered a raw receiver.
        if (charged) {
            std::lock_guard<std::mutex> lock(budget->mutex);
            --budget->usage.retained_applications;
        }
    }
    const std::shared_ptr<AttachmentBudget> budget;
    const EndpointDirectory::Snapshot directory;
    const std::shared_ptr<InputChannels> inputs;
    std::vector<AttachmentLocal> locals;
    ApplicationIdentity identity;
    ApplicationPhase phase = ApplicationPhase::Active;
    std::size_t controls = 0;
    // Producer lifetime, not RecordPhase::Copying. Includes commit/refund until
    // the admitting thread can no longer mutate this application's ledger.
    std::size_t copying_activity = 0;
    std::size_t dispatching_activity = 0;
    bool draining = false, charged = false;
};
void Refund(AttachmentBudget& b, std::size_t index, std::size_t size, RecordPhase phase)
{
    --b.usage.records; b.usage.bytes -= size;
    --b.locals[index].records; b.locals[index].bytes -= size;
    switch (phase) {
        case RecordPhase::Copying: --b.usage.copying; break;
        case RecordPhase::Queued: --b.usage.queued; break;
        case RecordPhase::Taken: --b.usage.taken; break;
        case RecordPhase::Delivering: --b.usage.delivering; break;
    }
}
struct AttachmentRecord final
{
    AttachmentRecord(std::shared_ptr<AttachmentApplication> a, InputChannelTicket t,
            Locator r, std::size_t index, std::uint64_t incarnation_value,
            std::size_t global_index, std::size_t size,NativeIdentityConstraint pin = {},
            ProcessClock::Clock::time_point expires=ProcessClock::Clock::time_point::max())
        : app(std::move(a)), ticket(std::move(t)), remote(r), local_index(index),
          incarnation(incarnation_value), budget_index(global_index), bytes(size),native(std::move(pin)),expiry(expires) {}
    ~AttachmentRecord()
    {
        if (charged) {
            std::lock_guard<std::mutex> lock(app->budget->mutex);
            Refund(*app->budget, budget_index, bytes.size(), phase);
            if(phase==RecordPhase::Delivering) --app->dispatching_activity;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
            ObserveBudget(*app->budget,host_rx_observation::Kind::RefundRecord,budget_index,diagnostic,phase);
#endif
        }
        // Payload storage is gone before reporting its actual release. This is
        // deliberately separate from invocation settlement and ledger counters.
        std::vector<std::uint8_t>().swap(bytes);
        if(observer) observer->OnReceive(ReceiveStage::RecordReleased,diagnostic,nullptr,0);
    }
    // No back-reference to the control object. Only queue ownership uses next.
    const std::shared_ptr<AttachmentApplication> app;
    const InputChannelTicket ticket;
    const Locator remote;
    const std::size_t local_index;
    const std::uint64_t incarnation;
    const std::size_t budget_index;
    std::vector<std::uint8_t> bytes;
    const NativeIdentityConstraint native;
    const ProcessClock::Clock::time_point expiry;
    ReceiveDiagnostic diagnostic;
    std::shared_ptr<ProcessReceiveObserver> observer;
    RecordPhase phase = RecordPhase::Copying;
    bool charged = false;
    std::unique_ptr<AttachmentRecord> next;
};
struct AttachmentSlot { Locator local; std::shared_ptr<AttachmentApplication> app; };
struct AttachmentsControl final
{
    AttachmentsControl(const std::vector<Locator>& locals, AttachmentLimits limits)
        : budget(std::make_shared<AttachmentBudget>(limits, locals.size())), slots(locals.size()),
          shared_apps(limits.retained_applications)
    { for (std::size_t i = 0; i < locals.size(); ++i) slots[i].local = locals[i]; }
    const std::shared_ptr<AttachmentBudget> budget;
    std::vector<AttachmentSlot> slots;
    // Explicit same-channel claims; they share the original local/global budget
    // and queue. Exclusive legacy claims never resolve one of these by locator.
    std::vector<std::shared_ptr<AttachmentApplication>> shared_apps;
    std::unique_ptr<AttachmentRecord> queue;
    AttachmentRecord* tail = nullptr;
    bool stopped = false;
};
AttachmentReceiveCapture::AttachmentReceiveCapture() noexcept = default;
AttachmentReceiveCapture::~AttachmentReceiveCapture() noexcept
{
    record_.reset(); // Charged record refund must finish before activity reaches zero.
    if(active_) {
        std::lock_guard<std::mutex> lock(app_->budget->mutex);
        if(reserved_) {
            Refund(*app_->budget,global_index_,size_,RecordPhase::Copying);
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
            ObserveBudget(*app_->budget,host_rx_observation::Kind::RefundCapture,global_index_,diagnostic_,RecordPhase::Copying);
#endif
        }
        --app_->copying_activity;app_->budget->changed.notify_all();
    }
}
} // namespace detail

namespace {
using namespace detail;
constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();
std::size_t FindLocal(const AttachmentApplication& app, const Locator& local)
{
    for (std::size_t i = 0; i < app.locals.size(); ++i) if (app.locals[i].locator == local) return i;
    return absent;
}
std::size_t FindSlot(const AttachmentsControl& control, const Locator& local)
{
    for (std::size_t i = 0; i < control.slots.size(); ++i) if (control.slots[i].local == local) return i;
    return absent;
}
void DestroyRecords(std::unique_ptr<AttachmentRecord> records)
{
    // Outside the gate and iterative: neither recursive destruction nor budget
    // mutex reentry is hidden in an arbitrary configured queue capacity.
    while (records) { auto next = std::move(records->next); records.reset(); records = std::move(next); }
}
void ValidateLimits(const AttachmentLimits& n)
{
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (!n.logical_slots || !n.retained_applications || !n.records || !n.bytes ||
            !n.local_records || !n.local_bytes || !n.copying || !n.max_record_bytes ||
            n.local_records > n.records || n.local_bytes > n.bytes || n.copying > n.records ||
            n.max_record_bytes > n.local_bytes || n.max_record_bytes > UINT32_MAX ||
            n.logical_slots > maximum / sizeof(AttachmentLocal) ||
            n.retained_applications > maximum / sizeof(AttachmentApplication) ||
            n.records > maximum / sizeof(AttachmentRecord) || n.bytes > maximum ||
            n.retained_applications > maximum / n.logical_slots) {
        throw std::invalid_argument("invalid attachment limits");
    }
}
} // namespace

ReceiveWork::ReceiveWork() noexcept = default;
ReceiveWork::~ReceiveWork() = default;
ReceiveWork::ReceiveWork(ReceiveWork&&) noexcept = default;
ReceiveWork& ReceiveWork::operator=(ReceiveWork&&) noexcept = default;
ReceiveWork::ReceiveWork(std::unique_ptr<detail::AttachmentRecord> record) noexcept : record_(std::move(record)) {}
ReceiveWork::operator bool() const noexcept { return bool(record_); }
InputDeliveryResult ReceiveWork::Deliver()
{
    auto record = std::move(record_);
    if (!record) return InputDeliveryResult::InvalidTicket;
    if (record->native) return InputDeliveryResult::Disabled; // No native-admission bypass.
    {
        std::lock_guard<std::mutex> lock(record->app->budget->mutex);
        --record->app->budget->usage.taken;
        ++record->app->budget->usage.delivering;
        ++record->app->dispatching_activity;
        record->phase = detail::RecordPhase::Delivering;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
        detail::ObserveBudget(*record->app->budget,host_rx_observation::Kind::Enter,
                record->budget_index,record->diagnostic,record->phase);
#endif
    }
    return record->app->inputs->Deliver(record->ticket, record->remote,
            record->bytes.data(), record->bytes.size());
}

ApplicationAttachments::ApplicationAttachments(const std::vector<Locator>& locals, AttachmentLimits limits)
    : ApplicationAttachments(locals, limits, {}) {}
ApplicationAttachments::ApplicationAttachments(const std::vector<Locator>& locals,
        AttachmentLimits limits, TestHooks hooks) : hooks_(std::move(hooks))
{
    ValidateLimits(limits);
    if (locals.empty() || locals.size() > limits.logical_slots) throw std::invalid_argument("invalid logical slots");
    for (std::size_t i = 0; i < locals.size(); ++i) {
        if (!LocatorCodec::IsValid(locals[i]) ||
                std::find(locals.begin(), locals.begin() + i, locals[i]) != locals.begin() + i) {
            throw std::invalid_argument("duplicate or invalid logical slot");
        }
    }
    control_ = std::make_unique<detail::AttachmentsControl>(locals, limits);
}
ApplicationAttachments::~ApplicationAttachments()
{
    auto& control = *control_;
    { std::lock_guard<std::mutex> lock(control.budget->mutex); control.stopped = true; }
    for (auto& slot : control.slots) {
        ApplicationClaim claim;
        { std::lock_guard<std::mutex> lock(control.budget->mutex); claim.app_ = slot.app; }
        if (claim) AwaitDetached(claim);
    }
    // Outstanding work retains only app/core/budget, never this or hook closures.
    for (auto& entry : control.shared_apps) {
        ApplicationClaim claim;
        { std::lock_guard<std::mutex> lock(control.budget->mutex); claim.app_=entry; }
        if (claim) AwaitDetached(claim);
    }
}
ApplicationClaimOutcome ApplicationAttachments::ClaimApplication(const std::vector<Locator>& locals,
        EndpointDirectory::Snapshot directory)
{ return ClaimApplicationImpl(locals,std::move(directory),false); }
ApplicationClaimOutcome ApplicationAttachments::ClaimSharedApplication(const std::vector<Locator>& locals,
        EndpointDirectory::Snapshot directory)
{ return ClaimApplicationImpl(locals,std::move(directory),true); }
ApplicationClaimOutcome ApplicationAttachments::ClaimApplicationImpl(const std::vector<Locator>& locals,
        EndpointDirectory::Snapshot directory,bool shared)
{
    auto& control = *control_; auto& budget = *control.budget;
    if (!directory) return {ApplicationClaimResult::InvalidDirectory, {}};
    if (locals.empty() || locals.size() > control.slots.size()) return {ApplicationClaimResult::InvalidLocals, {}};
    try {
        std::vector<std::size_t> indices(locals.size());
        for (std::size_t i = 0; i < locals.size(); ++i) {
            indices[i] = FindSlot(control, locals[i]);
            if (indices[i] == absent || std::find(indices.begin(), indices.begin() + i, indices[i]) != indices.begin() + i)
                return {ApplicationClaimResult::InvalidLocals, {}};
            if (!directory->FindLocal(locals[i]).value) return {ApplicationClaimResult::InvalidDirectory, {}};
        }
        std::unique_lock<std::mutex> lock(budget.mutex);
        if (control.stopped) return {ApplicationClaimResult::Stopped, {}};
        for (auto i : indices) if (control.slots[i].app) {
            return {control.slots[i].app->phase == detail::ApplicationPhase::Active ?
                    ApplicationClaimResult::InUse : ApplicationClaimResult::Detaching, {}};
        }
        if (!shared) for (const auto& existing : control.shared_apps) if (existing) {
            for (const auto& local : locals) if (FindLocal(*existing,local)!=absent)
                return {existing->phase==detail::ApplicationPhase::Active?
                        ApplicationClaimResult::InUse:ApplicationClaimResult::Detaching,{}};
        }
        auto shared_slot=control.shared_apps.end();
        if (shared) {
            shared_slot=std::find(control.shared_apps.begin(),control.shared_apps.end(),nullptr);
            if(shared_slot==control.shared_apps.end())return {ApplicationClaimResult::Capacity,{}};
        }
        if (budget.usage.retained_applications == budget.limits.retained_applications)
            return {ApplicationClaimResult::Capacity, {}};
        if(budget.next_identity>UINT64_MAX-2) return {ApplicationClaimResult::Capacity,{}};
        // Preparation may allocate but cannot publish a receiver. No slot is
        // mutated until the complete application and all local entries exist.
        auto app = std::make_shared<detail::AttachmentApplication>(control.budget, directory, locals.size());
        app->identity={budget.next_identity++,budget.next_identity++};
        for (std::size_t i = 0; i < locals.size(); ++i) {
            app->locals[i].locator = locals[i]; app->locals[i].budget_index = indices[i];
        }
        ++budget.usage.retained_applications; ++budget.usage.active_applications;
        budget.usage.high_applications = std::max(budget.usage.high_applications, budget.usage.retained_applications);
        app->charged = true;
        if(shared)*shared_slot=app;
        else for (auto i : indices) control.slots[i].app = app;
        ApplicationClaim claim; claim.app_ = std::move(app);
        return {ApplicationClaimResult::Claimed, std::move(claim)};
    } catch (const std::bad_alloc&) { return {ApplicationClaimResult::AllocationFailure, {}}; }
}
ReceiverOpenOutcome ApplicationAttachments::OpenReceiver(const ApplicationClaim& claim,
        const Locator& local, Receiver* receiver)
{
    auto app = claim.app_; auto& budget = *control_->budget;
    if (!app || app->budget != control_->budget) return {ReceiverOpenResult::InvalidHandle, {}};
    if (!receiver) return {ReceiverOpenResult::NullReceiver, {}};
    const auto index = FindLocal(*app, local);
    if (index == absent) return {ReceiverOpenResult::InvalidLocal, {}};
    auto& entry = app->locals[index];
    {
        std::lock_guard<std::mutex> lock(budget.mutex);
        if (app->phase != detail::ApplicationPhase::Active) return {ReceiverOpenResult::Detached, {}};
        if (entry.phase == detail::ReceiverPhase::Open) {
            if (entry.receiver != receiver) return {ReceiverOpenResult::Conflict, {}};
            ReceiverBinding b; b.app_ = app; b.local_index_ = index; b.incarnation_ = entry.incarnation;
            b.observed_incarnation_=entry.observed_incarnation;
            return {ReceiverOpenResult::AlreadyOpen, std::move(b)};
        }
        if (entry.phase != detail::ReceiverPhase::Empty) return {ReceiverOpenResult::Busy, {}};
        if (entry.incarnation == UINT64_MAX||budget.next_identity==UINT64_MAX) return {ReceiverOpenResult::IncarnationExhausted, {}};
        entry.phase = detail::ReceiverPhase::Opening; ++app->controls;
    }
    InputChannelTicket ticket;
    bool registered = false;
    auto result = ReceiverOpenResult::AllocationFailure;
    try {
        const auto opened = app->inputs->Open(local, receiver);
        registered = opened == InputOpenResult::Opened || opened == InputOpenResult::AlreadyOpen;
        if (registered) {
            ticket = app->inputs->Capture(local);
            if (hooks_.open_registered) hooks_.open_registered();
            std::lock_guard<std::mutex> lock(budget.mutex);
            if (app->phase == detail::ApplicationPhase::Active&&budget.next_identity!=UINT64_MAX) {
                ++entry.incarnation; entry.ticket = std::move(ticket); entry.receiver = receiver;
                entry.observed_incarnation=budget.next_identity++;
                entry.phase = detail::ReceiverPhase::Open; --app->controls; budget.changed.notify_all();
                ReceiverBinding b; b.app_ = app; b.local_index_ = index; b.incarnation_ = entry.incarnation;
                b.observed_incarnation_=entry.observed_incarnation;
                return {ReceiverOpenResult::Opened, std::move(b)};
            }
            result = ReceiverOpenResult::Detached;
        } else if (opened == InputOpenResult::Conflict) result = ReceiverOpenResult::Conflict;
        else if (opened == InputOpenResult::Disabled) result = ReceiverOpenResult::Detached;
        else if (opened == InputOpenResult::Closing) result = ReceiverOpenResult::Busy;
        else if (opened == InputOpenResult::InvalidLocal) result = ReceiverOpenResult::InvalidLocal;
    } catch (const std::bad_alloc&) { result = ReceiverOpenResult::AllocationFailure; }
    // No new Open is admitted until rollback crosses the real core barrier.
    if (registered) app->inputs->Close(local);
    {
        std::lock_guard<std::mutex> lock(budget.mutex);
        entry.phase = detail::ReceiverPhase::Empty; --app->controls; budget.changed.notify_all();
    }
    return {result, {}};
}
AttachmentControlResult ApplicationAttachments::CloseReceiver(const ReceiverBinding& binding)
{
    auto app = binding.app_; auto& budget = *control_->budget;
    if (!app || app->budget != control_->budget || binding.local_index_ >= app->locals.size() || !binding.incarnation_)
        return AttachmentControlResult::InvalidHandle;
    std::unique_lock<std::mutex> lock(budget.mutex);
    auto& entry = app->locals[binding.local_index_];
    if (binding.incarnation_ > entry.incarnation) return AttachmentControlResult::InvalidHandle;
    if (app->phase == detail::ApplicationPhase::Detached || entry.incarnation != binding.incarnation_ ||
            entry.phase == detail::ReceiverPhase::Empty || entry.phase == detail::ReceiverPhase::Opening)
        // Opening is reachable only from an actually drained Empty entry. No
        // binding for the pending Open exists yet; this handle is already old.
        return AttachmentControlResult::Completed;
    if (app->phase == detail::ApplicationPhase::Detaching) {
        lock.unlock(); ApplicationClaim claim; claim.app_ = app; AwaitDetached(claim);
        return AttachmentControlResult::Completed;
    }
    const bool owner = entry.phase == detail::ReceiverPhase::Open;
    if (owner) { entry.phase = detail::ReceiverPhase::Closing; ++app->controls; }
    lock.unlock();
    if (hooks_.close_wait) hooks_.close_wait();
    if (owner) {
        app->inputs->Close(entry.locator);
        lock.lock(); entry.phase = detail::ReceiverPhase::Empty; entry.receiver = nullptr; entry.ticket = {};
        --app->controls; budget.changed.notify_all();
    } else {
        lock.lock();
        // An existing waiter may miss Empty while unlocked: the replacement
        // can already be Opening, which still proves this old binding drained.
        // Closing alone never proves completion of the real input barrier.
        budget.changed.wait(lock, [&] { return entry.incarnation != binding.incarnation_ ||
                entry.phase == detail::ReceiverPhase::Empty || entry.phase == detail::ReceiverPhase::Opening ||
                app->phase == detail::ApplicationPhase::Detached; });
    }
    return AttachmentControlResult::Completed;
}
AttachmentControlResult ApplicationAttachments::RequestDetach(const ApplicationClaim& claim)
{
    auto app = claim.app_; auto& control = *control_; auto& budget = *control.budget;
    if (!app || app->budget != control.budget) return AttachmentControlResult::InvalidHandle;
    std::unique_ptr<detail::AttachmentRecord> discarded;
    {
        std::lock_guard<std::mutex> lock(budget.mutex);
        if (app->phase == detail::ApplicationPhase::Detached) return AttachmentControlResult::AlreadyDetached;
        if (app->phase == detail::ApplicationPhase::Active) {
            app->phase = detail::ApplicationPhase::Detaching;
            --budget.usage.active_applications; ++budget.usage.detaching_applications;
        }
        auto* link = &control.queue; control.tail = nullptr;
        while (*link) {
            if ((*link)->app == app) {
                auto record = std::move(*link); *link = std::move(record->next);
                record->next = std::move(discarded); discarded = std::move(record);
            } else { control.tail = link->get(); link = &(*link)->next; }
        }
    }
    DestroyRecords(std::move(discarded));
    return AttachmentControlResult::Requested;
}
AttachmentControlResult ApplicationAttachments::AwaitDetached(const ApplicationClaim& claim)
{
    const auto requested = RequestDetach(claim);
    if (requested == AttachmentControlResult::InvalidHandle || requested == AttachmentControlResult::AlreadyDetached) return requested;
    auto app = claim.app_; auto& control = *control_; auto& budget = *control.budget;
    std::unique_lock<std::mutex> lock(budget.mutex);
    const bool owner = !app->draining && app->phase != detail::ApplicationPhase::Detached;
    if (owner) app->draining = true;
    lock.unlock(); if (hooks_.detach_wait) hooks_.detach_wait(); lock.lock();
    if (!owner) {
        budget.changed.wait(lock, [&] { return app->phase == detail::ApplicationPhase::Detached; });
        return AttachmentControlResult::Completed;
    }
    budget.changed.wait(lock, [&] { return app->controls == 0; });
    lock.unlock(); app->inputs->DisableAll(); lock.lock();
    for (auto& entry : app->locals) {
        entry.phase = detail::ReceiverPhase::Empty; entry.receiver = nullptr; entry.ticket = {};
        auto& slot = control.slots[entry.budget_index];
        // Release by identity; never rediscover a current application by name.
        if (slot.app == app) slot.app.reset();
    }
    for(auto& shared : control.shared_apps)if(shared==app)shared.reset();
    app->phase = detail::ApplicationPhase::Detached; --budget.usage.detaching_applications;
    budget.changed.notify_all();
    return AttachmentControlResult::Completed;
}
ReceiveAdmissionResult ApplicationAttachments::AdmitReceive(const Locator& local, const Locator& remote,
        const void* bytes, std::size_t size,ReceiveAdmissionDiagnostic* diagnostic)
{
    auto& control = *control_; auto& budget = *control.budget;
    if(diagnostic) diagnostic->size=size;
    if (!bytes || !size) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            !bytes?RxAdmissionReason::NullBuffer:RxAdmissionReason::ZeroSize,
            ReceiveAdmissionResult::InvalidBuffer):ReceiveAdmissionResult::InvalidBuffer;
    if(diagnostic) {diagnostic->record_limit_known=true;diagnostic->record_limit=budget.limits.max_record_bytes;}
    if (size > budget.limits.max_record_bytes) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::TooLarge,ReceiveAdmissionResult::TooLarge):ReceiveAdmissionResult::TooLarge;
    const auto slot_index = FindSlot(control, local);
    if (slot_index == absent) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::InvalidLocal,ReceiveAdmissionResult::InvalidLocal):ReceiveAdmissionResult::InvalidLocal;
    ApplicationClaim claim;
    {
        std::lock_guard<std::mutex> lock(budget.mutex);
        if(diagnostic) {diagnostic->lifecycle_known=true;diagnostic->stopped=control.stopped;}
        if(control.stopped) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::Stopped,ReceiveAdmissionResult::Stopped):ReceiveAdmissionResult::Stopped;
        claim.app_=control.slots[slot_index].app;
        if(diagnostic) diagnostic->candidate_count=claim?1:0;
    }
    if(!claim) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::NoApplication,ReceiveAdmissionResult::NoReceiver):ReceiveAdmissionResult::NoReceiver;
    detail::AttachmentReceiveCapture capture;
    const ProcessClock clock;
    auto result=CaptureReceive(claim,local,remote,{},ProcessClock::Clock::time_point::max(),size,clock,capture,diagnostic);
    if(result!=ReceiveAdmissionResult::Accepted) return result;
    result=CopyReceive(capture,bytes,diagnostic);
    if(result!=ReceiveAdmissionResult::Accepted) return result;
    return CommitReceive(capture,clock,diagnostic);
}
ReceiveAdmissionResult ApplicationAttachments::CaptureReceive(const ApplicationClaim& claim,const Locator& local,
        const Locator& remote,const NativeIdentityConstraint& pin,ProcessClock::Clock::time_point expiry,
        std::size_t size,const ProcessClock& clock,detail::AttachmentReceiveCapture& capture,ReceiveAdmissionDiagnostic* diagnostic)
{
    auto app=claim.app_;auto& budget=*control_->budget;
    if(diagnostic) diagnostic->size=size;
    if(!app||app->budget!=control_->budget) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::InvalidClaim,ReceiveAdmissionResult::Revoked):ReceiveAdmissionResult::Revoked;
    if(!size) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::ZeroSize,ReceiveAdmissionResult::InvalidBuffer):ReceiveAdmissionResult::InvalidBuffer;
    if(diagnostic) {diagnostic->record_limit_known=true;diagnostic->record_limit=budget.limits.max_record_bytes;}
    if(size>budget.limits.max_record_bytes) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::TooLarge,ReceiveAdmissionResult::TooLarge):ReceiveAdmissionResult::TooLarge;
    const auto index=FindLocal(*app,local);
    if(index==absent) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::InvalidLocal,ReceiveAdmissionResult::InvalidLocal):ReceiveAdmissionResult::InvalidLocal;
    const auto slot_index=app->locals[index].budget_index;
    try {
        std::lock_guard<std::mutex> lock(budget.mutex);
        if(diagnostic) {
            diagnostic->application_known=true;
            diagnostic->application_incarnation=app->identity.application_incarnation;
            diagnostic->logical_token=app->identity.logical_token;
            diagnostic->lifecycle_known=true;diagnostic->stopped=control_->stopped;
            diagnostic->application_phase=static_cast<int>(app->phase);
            const auto& observed=app->locals[index];
            diagnostic->receiver_known=observed.observed_incarnation!=0;
            diagnostic->receiver_incarnation=observed.observed_incarnation;
            diagnostic->receiver_phase=static_cast<int>(observed.phase);
        }
        if (control_->stopped) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::Stopped,ReceiveAdmissionResult::Stopped):ReceiveAdmissionResult::Stopped;
        if (app->phase != detail::ApplicationPhase::Active) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::ApplicationInactive,ReceiveAdmissionResult::NoReceiver):ReceiveAdmissionResult::NoReceiver;
        const auto& entry = app->locals[index];
        if (entry.phase != detail::ReceiverPhase::Open) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::ReceiverNotOpen,ReceiveAdmissionResult::NoReceiver):ReceiveAdmissionResult::NoReceiver;
        // Native caller already holds the original validated route/owner gate;
        // only the legacy path needs the allocating directory lookup here.
        if (!pin&&!app->directory->ResolveOutbound({local, remote}).value) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::InvalidRoute,ReceiveAdmissionResult::InvalidRoute):ReceiveAdmissionResult::InvalidRoute;
        if(pin&&(pin.local()!=local||pin.remote()!=remote)) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::InvalidRoute,ReceiveAdmissionResult::InvalidRoute):ReceiveAdmissionResult::InvalidRoute;
        if(expiry!=ProcessClock::Clock::time_point::max()&&clock.Now()>=expiry) return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
                RxAdmissionReason::Expired,ReceiveAdmissionResult::Expired):ReceiveAdmissionResult::Expired;
        const auto& n = budget.limits; const auto& l = budget.locals[slot_index];
        if (budget.usage.records == n.records || size > n.bytes - budget.usage.bytes ||
                l.records == n.local_records || size > n.local_bytes - l.bytes || budget.usage.copying == n.copying ||
                app->copying_activity == std::numeric_limits<std::size_t>::max()) {
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
            ReceiveDiagnostic refused;refused.correlation=capture.diagnostic_.correlation;
            refused.identity={app->identity.application_incarnation,entry.observed_incarnation,app->identity.logical_token};
            refused.size=size;refused.observation_scope=budget.observation_scope;
            const auto refused_gate=detail::ObserveBudget(budget,host_rx_observation::Kind::CapacityRefusal,
                    slot_index,refused,detail::RecordPhase::Copying,app->copying_activity,
                    (budget.usage.records==n.records?1u:0u)|(size>n.bytes-budget.usage.bytes?2u:0u)|
                    (l.records==n.local_records?4u:0u)|(size>n.local_bytes-l.bytes?8u:0u)|
                    (budget.usage.copying==n.copying?16u:0u)|
                    (app->copying_activity==std::numeric_limits<std::size_t>::max()?32u:0u));
            if(diagnostic){diagnostic->observation_scope=budget.observation_scope;diagnostic->observation_gate=refused_gate;}
#endif
            if(diagnostic) {
                // Observe all capacity predicates only after the original
                // short-circuit expression has rejected, under its same gate.
                diagnostic->capacity_known=true;
                diagnostic->capacity_mask=(budget.usage.records==n.records?1u:0u)|
                        (size>n.bytes-budget.usage.bytes?2u:0u)|(l.records==n.local_records?4u:0u)|
                        (size>n.local_bytes-l.bytes?8u:0u)|(budget.usage.copying==n.copying?16u:0u)|
                        (app->copying_activity==std::numeric_limits<std::size_t>::max()?32u:0u);
                diagnostic->records_used=budget.usage.records;diagnostic->records_limit=n.records;
                diagnostic->bytes_used=budget.usage.bytes;diagnostic->bytes_limit=n.bytes;
                diagnostic->local_records_used=l.records;diagnostic->local_records_limit=n.local_records;
                diagnostic->local_bytes_used=l.bytes;diagnostic->local_bytes_limit=n.local_bytes;
                diagnostic->copying_used=budget.usage.copying;diagnostic->copying_limit=n.copying;
                diagnostic->application_copying=app->copying_activity;
                diagnostic->application_copying_limit=std::numeric_limits<std::size_t>::max();
                const auto mask=diagnostic->capacity_mask;
                const auto reason=mask&1u?RxAdmissionReason::GlobalRecords:mask&2u?RxAdmissionReason::GlobalBytes:
                        mask&4u?RxAdmissionReason::LocalRecords:mask&8u?RxAdmissionReason::LocalBytes:
                        mask&16u?RxAdmissionReason::CopyingLimit:RxAdmissionReason::ApplicationCopyingExhausted;
                return diagnostic->Return(RxAdmissionStage::Capture,reason,ReceiveAdmissionResult::Capacity);
            }
            return ReceiveAdmissionResult::Capacity;
        }
        capture.app_=app;capture.ticket_=entry.ticket;capture.native_=pin;capture.remote_=remote;
        capture.index_=index;capture.global_index_=slot_index;capture.incarnation_=entry.incarnation;
        capture.expiry_=expiry;capture.size_=size;
        capture.diagnostic_.identity={app->identity.application_incarnation,entry.observed_incarnation,app->identity.logical_token};
        capture.diagnostic_.native=pin;capture.diagnostic_.size=size;
        ++budget.usage.records; budget.usage.bytes += size; ++budget.usage.copying; ++app->copying_activity;
        capture.active_=true;capture.reserved_=true; // No fallible work after charging.
        ++budget.locals[slot_index].records; budget.locals[slot_index].bytes += size;
        budget.usage.high_records = std::max(budget.usage.high_records, budget.usage.records);
        budget.usage.high_bytes = std::max(budget.usage.high_bytes, budget.usage.bytes);
        budget.usage.high_copying = std::max(budget.usage.high_copying, budget.usage.copying);
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
        if(budget.observation_scope) {
            capture.diagnostic_.observation_scope=budget.observation_scope;
            capture.diagnostic_.observation_record=host_rx_observation::Next(budget.observation_record);
            const auto gate=detail::ObserveBudget(budget,host_rx_observation::Kind::Charge,
                    slot_index,capture.diagnostic_,detail::RecordPhase::Copying);
            if(diagnostic){diagnostic->observation_scope=budget.observation_scope;
                diagnostic->observation_record=capture.diagnostic_.observation_record;diagnostic->observation_gate=gate;}
        }
#endif
    } catch (const std::bad_alloc&) { return diagnostic?diagnostic->Return(RxAdmissionStage::Capture,
            RxAdmissionReason::CaptureAllocation,ReceiveAdmissionResult::AllocationFailure):ReceiveAdmissionResult::AllocationFailure; }
    return ReceiveAdmissionResult::Accepted;
}
ReceiveAdmissionResult ApplicationAttachments::CopyReceive(detail::AttachmentReceiveCapture& capture,const void* bytes,
        ReceiveAdmissionDiagnostic* diagnostic)
{
    if(!capture.active_||!capture.reserved_||capture.record_||!bytes) return diagnostic?diagnostic->Return(RxAdmissionStage::Copy,
            RxAdmissionReason::InvalidCopy,ReceiveAdmissionResult::InvalidBuffer):ReceiveAdmissionResult::InvalidBuffer;
    try {
        if (hooks_.copy_reserved) hooks_.copy_reserved();
        capture.record_=std::make_unique<detail::AttachmentRecord>(capture.app_,capture.ticket_,capture.remote_,
                capture.index_,capture.incarnation_,capture.global_index_,capture.size_,capture.native_,capture.expiry_);
        capture.record_->charged=true;capture.reserved_=false;
        capture.record_->diagnostic=capture.diagnostic_;capture.record_->observer=capture.observer_;
        std::memcpy(capture.record_->bytes.data(),bytes,capture.size_);
        if (hooks_.receive_publish) hooks_.receive_publish();
        return ReceiveAdmissionResult::Accepted;
    } catch (const std::bad_alloc&) { return diagnostic?diagnostic->Return(RxAdmissionStage::Copy,
            RxAdmissionReason::CopyAllocation,ReceiveAdmissionResult::AllocationFailure):ReceiveAdmissionResult::AllocationFailure; }
}
ReceiveAdmissionResult ApplicationAttachments::CommitReceive(detail::AttachmentReceiveCapture& capture,const ProcessClock& clock,
        ReceiveAdmissionDiagnostic* diagnostic)
{
    if(!capture.active_||!capture.record_||capture.app_->budget!=control_->budget) return diagnostic?diagnostic->Return(RxAdmissionStage::Commit,
            RxAdmissionReason::InvalidCommit,ReceiveAdmissionResult::Revoked):ReceiveAdmissionResult::Revoked;
    auto& control=*control_;auto& budget=*control.budget;
    std::lock_guard<std::mutex> lock(budget.mutex);
    const auto& entry=capture.app_->locals[capture.index_];
    if(diagnostic) {
        // Identity remains the original captured receiver even if this slot
        // now contains a replacement. Only lifecycle phases are current here.
        diagnostic->application_known=true;diagnostic->receiver_known=true;diagnostic->lifecycle_known=true;
        diagnostic->application_incarnation=capture.diagnostic_.identity.application_incarnation;
        diagnostic->logical_token=capture.diagnostic_.identity.logical_token;
        diagnostic->receiver_incarnation=capture.diagnostic_.identity.receiver_incarnation;
        diagnostic->application_phase=static_cast<int>(capture.app_->phase);
        diagnostic->receiver_phase=static_cast<int>(entry.phase);diagnostic->stopped=control.stopped;
    }
    if(capture.app_->phase!=detail::ApplicationPhase::Active||entry.phase!=detail::ReceiverPhase::Open||
            entry.incarnation!=capture.incarnation_) {
        if(diagnostic) {
            const auto reason=capture.app_->phase!=detail::ApplicationPhase::Active?RxAdmissionReason::CommitApplicationInactive:
                    entry.phase!=detail::ReceiverPhase::Open?RxAdmissionReason::CommitReceiverNotOpen:RxAdmissionReason::CommitReceiverChanged;
            return diagnostic->Return(RxAdmissionStage::Commit,reason,ReceiveAdmissionResult::Revoked);
        }
        return ReceiveAdmissionResult::Revoked;
    }
    if(capture.expiry_!=ProcessClock::Clock::time_point::max()&&clock.Now()>=capture.expiry_)
        return diagnostic?diagnostic->Return(RxAdmissionStage::Commit,RxAdmissionReason::Expired,
                ReceiveAdmissionResult::Expired):ReceiveAdmissionResult::Expired;
    --budget.usage.copying;++budget.usage.queued;capture.record_->phase=detail::RecordPhase::Queued;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    detail::ObserveBudget(budget,host_rx_observation::Kind::Queue,capture.global_index_,
            capture.diagnostic_,detail::RecordPhase::Queued);
#endif
    auto* tail=capture.record_.get();
    if(control.tail) control.tail->next=std::move(capture.record_);else control.queue=std::move(capture.record_);
    control.tail=tail;
    return diagnostic?diagnostic->Return(RxAdmissionStage::Commit,RxAdmissionReason::AcceptedQueue,
            ReceiveAdmissionResult::Accepted):ReceiveAdmissionResult::Accepted;
}
NativeIdentityConstraint ApplicationAttachments::ReceiveIdentity(const ReceiveWork& work) const
{return work.record_&&work.record_->app->budget==control_->budget?work.record_->native:NativeIdentityConstraint{};}
ReceiveDiagnostic ApplicationAttachments::ReceiveObservation(const ReceiveWork& work) const
{return work.record_&&work.record_->app->budget==control_->budget?work.record_->diagnostic:ReceiveDiagnostic{};}
void ApplicationAttachments::ObserveReceive(const ReceiveWork& work,ReceiveStage stage) noexcept
{
    if(work.record_&&work.record_->observer) {
        const auto& r=*work.record_;
        r.observer->OnReceive(stage,r.diagnostic,r.bytes.data(),r.bytes.size());
    }
}
void ApplicationAttachments::ObserveOwnedCopy(const detail::AttachmentReceiveCapture& capture) noexcept
{
    if(capture.observer_&&capture.record_) capture.observer_->OnReceive(ReceiveStage::OwnedCopy,
            capture.diagnostic_,capture.record_->bytes.data(),capture.record_->bytes.size());
}
ApplicationIdentity ApplicationAttachments::ObserveApplication(const ApplicationClaim& claim) const noexcept
{return claim.app_&&claim.app_->budget==control_->budget?claim.app_->identity:ApplicationIdentity{};}
ReceiverIdentity ApplicationAttachments::ObserveReceiver(const ReceiverBinding& binding) const noexcept
{
    if(!binding.app_||binding.app_->budget!=control_->budget||!binding.incarnation_) return {};
    const auto id=binding.app_->identity;
    return {id.application_incarnation,binding.observed_incarnation_,id.logical_token};
}
NativeStatus ApplicationAttachments::CheckPendingReceive(const ReceiveWork& work,const ProcessClock& clock) const
{
    if(!work.record_||work.record_->app->budget!=control_->budget||!work.record_->native) return NativeStatus::InvalidHandle;
    const auto& record=*work.record_;const auto& app=*record.app;
    std::lock_guard<std::mutex> lock(app.budget->mutex);const auto& entry=app.locals[record.local_index];
    if(record.phase!=detail::RecordPhase::Taken||app.phase!=detail::ApplicationPhase::Active||
            entry.phase!=detail::ReceiverPhase::Open||entry.incarnation!=record.incarnation) return NativeStatus::Revoked;
    return record.expiry!=ProcessClock::Clock::time_point::max()&&clock.Now()>=record.expiry?
            NativeStatus::DeadlineExpired:NativeStatus::None;
}
NativeStatus ApplicationAttachments::EnterReceive(ReceiveWork& work,InputReceiverLease& lease,const ProcessClock& clock)
{
    if(!work.record_||work.record_->app->budget!=control_->budget||!work.record_->native) return NativeStatus::InvalidHandle;
    auto& record=*work.record_;auto& app=*record.app;auto& budget=*app.budget;
    std::lock_guard<std::mutex> lock(budget.mutex);const auto& entry=app.locals[record.local_index];
    if(record.phase!=detail::RecordPhase::Taken||app.phase!=detail::ApplicationPhase::Active||
            entry.phase!=detail::ReceiverPhase::Open||entry.incarnation!=record.incarnation) return NativeStatus::Revoked;
    if(record.expiry!=ProcessClock::Clock::time_point::max()&&clock.Now()>=record.expiry) return NativeStatus::DeadlineExpired;
    if(app.dispatching_activity==std::numeric_limits<std::size_t>::max()) return NativeStatus::CounterExhausted;
    lease=app.inputs->TryAcquireReceiver(record.ticket); // Required initially empty output.
    if(!lease) return NativeStatus::Revoked;
    --budget.usage.taken;++budget.usage.delivering;++app.dispatching_activity;record.phase=detail::RecordPhase::Delivering;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    detail::ObserveBudget(budget,host_rx_observation::Kind::Enter,record.budget_index,record.diagnostic,record.phase);
#endif
    return NativeStatus::None;
}
InputDeliveryResult ApplicationAttachments::InvokeReceive(ReceiveWork& work,InputReceiverLease& lease)
{
    if(!work.record_) return InputDeliveryResult::InvalidTicket;
    const auto& record=*work.record_;
    return lease.Deliver(record.remote,record.bytes.data(),record.bytes.size());
}
bool ApplicationAttachments::RequeueReceive(ReceiveWork& work)
{
    if(!work.record_||work.record_->app->budget!=control_->budget) return false;
    auto& record=*work.record_;auto& app=*record.app;auto& control=*control_;auto& budget=*control.budget;
    std::lock_guard<std::mutex> lock(budget.mutex);const auto& entry=app.locals[record.local_index];
    if(record.phase!=detail::RecordPhase::Taken||app.phase!=detail::ApplicationPhase::Active||
            entry.phase!=detail::ReceiverPhase::Open||entry.incarnation!=record.incarnation) return false;
    --budget.usage.taken;++budget.usage.queued;record.phase=detail::RecordPhase::Queued;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    detail::ObserveBudget(budget,host_rx_observation::Kind::Requeue,record.budget_index,record.diagnostic,record.phase);
#endif
    auto* tail=work.record_.get();
    if(control.tail) control.tail->next=std::move(work.record_);else control.queue=std::move(work.record_);
    control.tail=tail;return true;
}
AttachmentControlResult ApplicationAttachments::FreezeApplication(const ApplicationClaim& claim)
{
    auto app=claim.app_;if(!app||app->budget!=control_->budget) return AttachmentControlResult::InvalidHandle;
    std::lock_guard<std::mutex> lock(app->budget->mutex);
    if(app->phase==detail::ApplicationPhase::Detached) return AttachmentControlResult::AlreadyDetached;
    if(app->phase==detail::ApplicationPhase::Active) {
        app->phase=detail::ApplicationPhase::Detaching;
        --app->budget->usage.active_applications;++app->budget->usage.detaching_applications;
    }
    return AttachmentControlResult::Requested; // No queue cleanup, wait or input close.
}
detail::ReceiveApplicationActivity ApplicationAttachments::ReceiveActivity(const ApplicationClaim& claim) const
{
    auto app=claim.app_;if(!app||app->budget!=control_->budget) return {};
    std::lock_guard<std::mutex> lock(app->budget->mutex);
    return {true,app->phase!=detail::ApplicationPhase::Active,app->controls,app->copying_activity,app->dispatching_activity};
}
ReceiveWork ApplicationAttachments::TryTake()
{
    auto& control = *control_; auto& budget = *control.budget;
    std::lock_guard<std::mutex> lock(budget.mutex);
    auto record = std::move(control.queue);
    if (!record) return {};
    control.queue = std::move(record->next); if (!control.queue) control.tail = nullptr;
    --budget.usage.queued; ++budget.usage.taken; record->phase = detail::RecordPhase::Taken;
#ifdef FASTDDS_DSOFTBUS_HOST_RX_OBSERVATION
    detail::ObserveBudget(budget,host_rx_observation::Kind::Take,record->budget_index,record->diagnostic,record->phase);
#endif
    return ReceiveWork(std::move(record));
}
AttachmentUsage ApplicationAttachments::SnapshotUsage() const
{ std::lock_guard<std::mutex> lock(control_->budget->mutex); return control_->budget->usage; }
ReceiveProducerState ApplicationAttachments::PollReceiveProducers(const ApplicationClaim& claim) const
{
    auto app = claim.app_;
    if (!app || app->budget != control_->budget) return ReceiveProducerState::InvalidHandle;
    std::lock_guard<std::mutex> lock(app->budget->mutex);
    if (app->phase == detail::ApplicationPhase::Active) return ReceiveProducerState::Open;
    // AlreadyDetached through the legacy API is not proof that producers left.
    return app->copying_activity ? ReceiveProducerState::Pending : ReceiveProducerState::Drained;
}
std::size_t ApplicationAttachments::TestCopying(const ApplicationClaim& claim) const
{
    std::lock_guard<std::mutex> lock(control_->budget->mutex);
    if (!claim.app_ || claim.app_->budget != control_->budget) throw std::logic_error("invalid test claim");
    return claim.app_->copying_activity;
}
void ApplicationAttachments::TestSetCopyingActivity(const ApplicationClaim& claim,std::size_t value)
{
    std::lock_guard<std::mutex> lock(control_->budget->mutex);
    if(!claim.app_||claim.app_->budget!=control_->budget) throw std::logic_error("invalid test claim");
    claim.app_->copying_activity=value;
}
void ApplicationAttachments::TestSetIncarnation(const ApplicationClaim& claim, const Locator& local, std::uint64_t n)
{
    std::lock_guard<std::mutex> lock(control_->budget->mutex);
    if (!claim.app_ || claim.app_->budget != control_->budget) throw std::logic_error("invalid test claim");
    const auto i = FindLocal(*claim.app_, local);
    if (i == absent || claim.app_->locals[i].phase != detail::ReceiverPhase::Empty) throw std::logic_error("test requires empty local");
    claim.app_->locals[i].incarnation = n;
}
std::weak_ptr<InputChannels> ApplicationAttachments::TestCore(const ApplicationClaim& claim) const
{ return claim.app_ && claim.app_->budget == control_->budget ? claim.app_->inputs : std::weak_ptr<InputChannels>{}; }
std::weak_ptr<void> ApplicationAttachments::TestBudget() const { return control_->budget; }
} // namespace fastdds_dsoftbus::dsoftbus
