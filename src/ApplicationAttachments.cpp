#include <mdds/dsoftbus/ApplicationAttachments.hpp>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace mdds::dsoftbus {
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
    std::vector<AttachmentLocalBudget> locals;
};
struct AttachmentLocal final
{
    Locator locator;
    std::size_t budget_index = 0;
    std::uint64_t incarnation = 0;
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
    ApplicationPhase phase = ApplicationPhase::Active;
    std::size_t controls = 0;
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
            std::size_t global_index, std::size_t size)
        : app(std::move(a)), ticket(std::move(t)), remote(r), local_index(index),
          incarnation(incarnation_value), budget_index(global_index), bytes(size) {}
    ~AttachmentRecord()
    {
        if (charged) {
            std::lock_guard<std::mutex> lock(app->budget->mutex);
            Refund(*app->budget, budget_index, bytes.size(), phase);
        }
    }
    // No back-reference to the control object. Only queue ownership uses next.
    const std::shared_ptr<AttachmentApplication> app;
    const InputChannelTicket ticket;
    const Locator remote;
    const std::size_t local_index;
    const std::uint64_t incarnation;
    const std::size_t budget_index;
    std::vector<std::uint8_t> bytes;
    RecordPhase phase = RecordPhase::Copying;
    bool charged = false;
    std::unique_ptr<AttachmentRecord> next;
};
struct AttachmentSlot { Locator local; std::shared_ptr<AttachmentApplication> app; };
struct AttachmentsControl final
{
    AttachmentsControl(const std::vector<Locator>& locals, AttachmentLimits limits)
        : budget(std::make_shared<AttachmentBudget>(limits, locals.size())), slots(locals.size())
    { for (std::size_t i = 0; i < locals.size(); ++i) slots[i].local = locals[i]; }
    const std::shared_ptr<AttachmentBudget> budget;
    std::vector<AttachmentSlot> slots;
    std::unique_ptr<AttachmentRecord> queue;
    AttachmentRecord* tail = nullptr;
    bool stopped = false;
};
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
class ReceiveReservation final
{
public:
    ReceiveReservation(std::shared_ptr<AttachmentApplication> app, std::size_t index, std::size_t size)
        : app_(std::move(app)), index_(index), size_(size) {}
    ~ReceiveReservation() {
        if (active_) { std::lock_guard<std::mutex> lock(app_->budget->mutex);
            Refund(*app_->budget, index_, size_, RecordPhase::Copying); }
    }
    void Transfer() noexcept { active_ = false; }
private:
    const std::shared_ptr<AttachmentApplication> app_;
    const std::size_t index_, size_;
    bool active_ = true;
};
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
    {
        std::lock_guard<std::mutex> lock(record->app->budget->mutex);
        --record->app->budget->usage.taken;
        ++record->app->budget->usage.delivering;
        record->phase = detail::RecordPhase::Delivering;
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
}
ApplicationClaimOutcome ApplicationAttachments::ClaimApplication(const std::vector<Locator>& locals,
        EndpointDirectory::Snapshot directory)
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
        if (budget.usage.retained_applications == budget.limits.retained_applications)
            return {ApplicationClaimResult::Capacity, {}};
        // Preparation may allocate but cannot publish a receiver. No slot is
        // mutated until the complete application and all local entries exist.
        auto app = std::make_shared<detail::AttachmentApplication>(control.budget, directory, locals.size());
        for (std::size_t i = 0; i < locals.size(); ++i) {
            app->locals[i].locator = locals[i]; app->locals[i].budget_index = indices[i];
        }
        ++budget.usage.retained_applications; ++budget.usage.active_applications;
        budget.usage.high_applications = std::max(budget.usage.high_applications, budget.usage.retained_applications);
        app->charged = true;
        for (auto i : indices) control.slots[i].app = app;
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
            return {ReceiverOpenResult::AlreadyOpen, std::move(b)};
        }
        if (entry.phase != detail::ReceiverPhase::Empty) return {ReceiverOpenResult::Busy, {}};
        if (entry.incarnation == UINT64_MAX) return {ReceiverOpenResult::IncarnationExhausted, {}};
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
            if (app->phase == detail::ApplicationPhase::Active) {
                ++entry.incarnation; entry.ticket = std::move(ticket); entry.receiver = receiver;
                entry.phase = detail::ReceiverPhase::Open; --app->controls; budget.changed.notify_all();
                ReceiverBinding b; b.app_ = app; b.local_index_ = index; b.incarnation_ = entry.incarnation;
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
    app->phase = detail::ApplicationPhase::Detached; --budget.usage.detaching_applications;
    budget.changed.notify_all();
    return AttachmentControlResult::Completed;
}
ReceiveAdmissionResult ApplicationAttachments::AdmitReceive(const Locator& local, const Locator& remote,
        const void* bytes, std::size_t size)
{
    auto& control = *control_; auto& budget = *control.budget;
    if (!bytes || !size) return ReceiveAdmissionResult::InvalidBuffer;
    if (size > budget.limits.max_record_bytes) return ReceiveAdmissionResult::TooLarge;
    const auto slot_index = FindSlot(control, local);
    if (slot_index == absent) return ReceiveAdmissionResult::InvalidLocal;
    std::shared_ptr<detail::AttachmentApplication> app;
    InputChannelTicket ticket; std::size_t index; std::uint64_t incarnation;
    try {
        std::lock_guard<std::mutex> lock(budget.mutex);
        if (control.stopped) return ReceiveAdmissionResult::Stopped;
        app = control.slots[slot_index].app;
        if (!app || app->phase != detail::ApplicationPhase::Active) return ReceiveAdmissionResult::NoReceiver;
        index = FindLocal(*app, local);
        const auto& entry = app->locals[index];
        if (entry.phase != detail::ReceiverPhase::Open) return ReceiveAdmissionResult::NoReceiver;
        if (!app->directory->ResolveOutbound({local, remote}).value) return ReceiveAdmissionResult::InvalidRoute;
        const auto& n = budget.limits; const auto& l = budget.locals[slot_index];
        if (budget.usage.records == n.records || size > n.bytes - budget.usage.bytes ||
                l.records == n.local_records || size > n.local_bytes - l.bytes || budget.usage.copying == n.copying)
            return ReceiveAdmissionResult::Capacity;
        ticket = entry.ticket; incarnation = entry.incarnation;
        ++budget.usage.records; budget.usage.bytes += size; ++budget.usage.copying;
        ++budget.locals[slot_index].records; budget.locals[slot_index].bytes += size;
        budget.usage.high_records = std::max(budget.usage.high_records, budget.usage.records);
        budget.usage.high_bytes = std::max(budget.usage.high_bytes, budget.usage.bytes);
        budget.usage.high_copying = std::max(budget.usage.high_copying, budget.usage.copying);
    } catch (const std::bad_alloc&) { return ReceiveAdmissionResult::AllocationFailure; }
    ReceiveReservation reservation(app, slot_index, size);
    try {
        if (hooks_.copy_reserved) hooks_.copy_reserved();
        auto record = std::make_unique<detail::AttachmentRecord>(app, ticket, remote, index, incarnation, slot_index, size);
        record->charged = true; reservation.Transfer();
        std::memcpy(record->bytes.data(), bytes, size);
        if (hooks_.receive_publish) hooks_.receive_publish();
        std::lock_guard<std::mutex> lock(budget.mutex);
        const auto& entry = app->locals[index];
        if (app->phase != detail::ApplicationPhase::Active || entry.phase != detail::ReceiverPhase::Open ||
                entry.incarnation != incarnation) return ReceiveAdmissionResult::Revoked;
        --budget.usage.copying; ++budget.usage.queued; record->phase = detail::RecordPhase::Queued;
        auto* tail = record.get();
        if (control.tail) control.tail->next = std::move(record); else control.queue = std::move(record);
        control.tail = tail;
        return ReceiveAdmissionResult::Accepted;
    } catch (const std::bad_alloc&) { return ReceiveAdmissionResult::AllocationFailure; }
}
ReceiveWork ApplicationAttachments::TryTake()
{
    auto& control = *control_; auto& budget = *control.budget;
    std::lock_guard<std::mutex> lock(budget.mutex);
    auto record = std::move(control.queue);
    if (!record) return {};
    control.queue = std::move(record->next); if (!control.queue) control.tail = nullptr;
    --budget.usage.queued; ++budget.usage.taken; record->phase = detail::RecordPhase::Taken;
    return ReceiveWork(std::move(record));
}
AttachmentUsage ApplicationAttachments::SnapshotUsage() const
{ std::lock_guard<std::mutex> lock(control_->budget->mutex); return control_->budget->usage; }
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
} // namespace mdds::dsoftbus
