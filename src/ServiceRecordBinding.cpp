// Composition layer over the record table, the native owner table and the
// application attachments. It adds no fourth source of truth: every identity it
// reports or forwards belongs to the component that owns it.
//
// What belongs to a record outlives any single binding of it. The record owns one
// ingress dispatch control (its sink and its per-(role, socket) routes) and one
// shared (application, local) registration per local. Both live in storage that
// is keyed by the record slot, that the record table keeps the sink address of for
// as long as the record is exposed, and that is never destroyed: a slot that was
// ever exposed is never recycled, so the address cannot dangle and a binding may
// be released and destroyed while its joiners keep being served. A binding
// registers into the control and removes only its own registration.
//
// This is a Host-verifiable model. It makes no SDK call: the provider interface
// is implemented by the caller (a test in Host, the real backend in production),
// and every value it returns is a model input rather than SDK evidence.
#include <fastdds_dsoftbus/dsoftbus/ServiceRecordBinding.hpp>
#include "ProcessReceiveInternal.hpp"

#include <array>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include <thread>

namespace fastdds_dsoftbus::dsoftbus {
namespace {
// Nonzero while this thread runs an ingress callback, so control operations that
// must never run on a callback stack can refuse instead of deadlocking.
thread_local int ingress_depth = 0;
// Nonzero while this thread runs a delivery of this composition's own admission
// path, for the same reason: control operations are external-only. The composing
// layer arms these on the admitted execution rather than around its own dispatch
// entry, so no entry point can run a delivery without marking the context.
thread_local int delivery_depth = 0;

// Provider shutdown owns the table's single close credit until it returns.
// Track only this synchronous execution stack; this is not a runtime registry.
struct ProviderShutdownFrame;
thread_local ProviderShutdownFrame* provider_shutdown_frame=nullptr;
struct ProviderShutdownFrame {
    const NativeOwnerTable* table;ProviderShutdownFrame* previous;
    explicit ProviderShutdownFrame(const NativeOwnerTable* t) noexcept:table(t),previous(provider_shutdown_frame){provider_shutdown_frame=this;++ingress_depth;}
    ~ProviderShutdownFrame(){--ingress_depth;provider_shutdown_frame=previous;}
    ProviderShutdownFrame(const ProviderShutdownFrame&)=delete;
    ProviderShutdownFrame& operator=(const ProviderShutdownFrame&)=delete;
};
bool HoldsCloseCredit(const NativeOwnerTable* table) noexcept {
    for(auto* p=provider_shutdown_frame;p;p=p->previous)if(p->table==table)return true;
    return false;
}

// Which applications this thread is currently delivering inside, innermost last, as
// the application identity the running delivery itself carries. That identity is the
// right one and the only exact one: the queue belongs to the attachments instance,
// so a frame can run work its own binding did not admit, and it is the *work's*
// receiver registration - not the issuing binding's - whose permit the frame holds.
// A delivery's registration cannot be settled while the delivery runs (the drain
// before the free is what guarantees that), so these identities are always live and
// are only ever compared.
constexpr std::size_t callback_registration_capacity = 16;
struct DeliveredApplication
{
    const void* owner = nullptr;
    std::uint64_t incarnation = 0;
    std::uint64_t token = 0;
    bool tracked = false;
};
thread_local DeliveredApplication delivered_applications[callback_registration_capacity] = {};
thread_local std::size_t delivered_application_count = 0;
thread_local bool delivered_application_overflow = false;
void EnterDeliveryContext(void* execution) noexcept
{
    ++delivery_depth;
    const auto* running = static_cast<const detail::ProcessReceiveExecution*>(execution);
    const auto identity = running->diagnostic.identity;
    if (delivered_application_count < callback_registration_capacity) {
        auto& entry = delivered_applications[delivered_application_count];
        entry.owner = running->owner;
        entry.incarnation = identity.application_incarnation;
        entry.token = identity.logical_token;
        entry.tracked = entry.owner != nullptr &&
                (identity.application_incarnation != 0 || identity.logical_token != 0);
        ++delivered_application_count;
    } else {
        // Deeper nesting than the tracking bound: stay fail-closed rather than
        // silently untracked.
        delivered_application_overflow = true;
    }
}
void ExitDeliveryContext(void* execution) noexcept
{
    (void)execution;
    --delivery_depth;
    if (delivered_application_count > 0) {
        --delivered_application_count;
        if (delivered_application_count == 0) delivered_application_overflow = false;
    }
}
// Whether settling that application from this thread would wait for a delivery this
// thread is currently inside.
bool SettleWaitsOnThisThread(const void* owner, const ApplicationIdentity& settled) noexcept
{
    if (owner == nullptr) return false;
    if (settled.application_incarnation == 0 && settled.logical_token == 0) return false;
    if (delivered_application_overflow) return true;
    for (std::size_t index = 0; index < delivered_application_count; ++index) {
        const auto& entry = delivered_applications[index];
        if (!entry.tracked || entry.owner != owner) continue;
        if (entry.incarnation == settled.application_incarnation && entry.token == settled.logical_token) {
            return true;
        }
    }
    return false;
}

NativeStatus FromRecordStatus(RecordStatus status) noexcept
{
    switch (status) {
    case RecordStatus::None: return NativeStatus::None;
    case RecordStatus::NotImplemented: return NativeStatus::NotImplemented;
    case RecordStatus::InvalidConfiguration: return NativeStatus::InvalidConfiguration;
    case RecordStatus::InvalidString: return NativeStatus::InvalidConfiguration;
    case RecordStatus::InvalidSocket: return NativeStatus::InvalidConfiguration;
    case RecordStatus::SlotExhausted: return NativeStatus::AdmissionSaturated;
    case RecordStatus::NameInUse: return NativeStatus::ExistingPending;
    case RecordStatus::ContinuityUnproven: return NativeStatus::Revoked;
    case RecordStatus::NotFound: return NativeStatus::NotFound;
    case RecordStatus::StaleHandle: return NativeStatus::StaleHandle;
    case RecordStatus::NotUnexposed: return NativeStatus::WrongState;
    case RecordStatus::AlreadyExposed: return NativeStatus::AlreadyEntered;
    case RecordStatus::AlreadyMember: return NativeStatus::ExistingPending;
    case RecordStatus::ForeignIdCollision: return NativeStatus::ForeignIdCollision;
    case RecordStatus::Capacity: return NativeStatus::AdmissionSaturated;
    case RecordStatus::UnknownSocket: return NativeStatus::UnknownSocket;
    case RecordStatus::Retired: return NativeStatus::Revoked;
    case RecordStatus::StillAnchored: return NativeStatus::WrongState;
    case RecordStatus::ExposureSettled: return NativeStatus::AlreadySettled;
    case RecordStatus::SinkMismatch: return NativeStatus::ConfigurationMismatch;
    case RecordStatus::IdentityExhausted: return NativeStatus::IdentityExhausted;
    case RecordStatus::AllocationFailure: return NativeStatus::AllocationFailure;
    }
    return NativeStatus::NotImplemented;
}
NativeStatus FromClaimResult(ApplicationClaimResult result) noexcept
{
    switch (result) {
    case ApplicationClaimResult::Claimed: return NativeStatus::None;
    case ApplicationClaimResult::InvalidLocals: return NativeStatus::InvalidConfiguration;
    case ApplicationClaimResult::InvalidDirectory: return NativeStatus::DirectoryRejected;
    case ApplicationClaimResult::InUse: return NativeStatus::ExistingPending;
    case ApplicationClaimResult::Detaching: return NativeStatus::ServiceClosing;
    case ApplicationClaimResult::Capacity: return NativeStatus::AdmissionSaturated;
    case ApplicationClaimResult::AllocationFailure: return NativeStatus::AllocationFailure;
    case ApplicationClaimResult::Stopped: return NativeStatus::ServiceClosing;
    }
    return NativeStatus::NotImplemented;
}
NativeStatus FromReceiverResult(ReceiverOpenResult result) noexcept
{
    switch (result) {
    case ReceiverOpenResult::Opened: return NativeStatus::None;
    case ReceiverOpenResult::AlreadyOpen: return NativeStatus::ExistingPending;
    case ReceiverOpenResult::InvalidHandle: return NativeStatus::InvalidHandle;
    case ReceiverOpenResult::InvalidLocal: return NativeStatus::NotFound;
    case ReceiverOpenResult::NullReceiver: return NativeStatus::InvalidConfiguration;
    case ReceiverOpenResult::Conflict: return NativeStatus::ForeignIdCollision;
    case ReceiverOpenResult::Busy: return NativeStatus::ExistingPending;
    case ReceiverOpenResult::Detached: return NativeStatus::Revoked;
    case ReceiverOpenResult::AllocationFailure: return NativeStatus::AllocationFailure;
    case ReceiverOpenResult::IncarnationExhausted: return NativeStatus::IdentityExhausted;
    }
    return NativeStatus::NotImplemented;
}
NativeStatus FromReceiveResult(ReceiveAdmissionResult result) noexcept
{
    switch (result) {
    case ReceiveAdmissionResult::Accepted: return NativeStatus::None;
    case ReceiveAdmissionResult::InvalidLocal: return NativeStatus::NotFound;
    case ReceiveAdmissionResult::InvalidRoute: return NativeStatus::NotFound;
    case ReceiveAdmissionResult::NoReceiver: return NativeStatus::Revoked;
    case ReceiveAdmissionResult::InvalidBuffer: return NativeStatus::InvalidConfiguration;
    case ReceiveAdmissionResult::TooLarge: return NativeStatus::InvalidLimits;
    case ReceiveAdmissionResult::Capacity: return NativeStatus::AdmissionSaturated;
    case ReceiveAdmissionResult::Revoked: return NativeStatus::Revoked;
    case ReceiveAdmissionResult::AllocationFailure: return NativeStatus::AllocationFailure;
    case ReceiveAdmissionResult::Stopped: return NativeStatus::ServiceClosing;
    case ReceiveAdmissionResult::InvalidDeadline: return NativeStatus::InvalidConfiguration;
    case ReceiveAdmissionResult::Expired: return NativeStatus::DeadlineExpired;
    }
    return NativeStatus::NotImplemented;
}
} // namespace

// One shared (application, local) registration of one record: the application
// claim, the receiver registration and the attachments that own them. Exactly one
// exists per (record, local) and every binding of the record on that local uses
// it. Its facts are immutable after construction, so an admitted call can read
// them while holding only its own binding's pin and no lock of this object, and
// no release ever writes another binding's state. `members` is the only mutable
// field: the release that finds itself last settles the registration, which then
// closes the receiver and detaches the claim exactly once.
struct ApplicationGroup final
{
    ApplicationGroup(ApplicationAttachments& a, const Locator& l, ApplicationClaim c, ReceiverBinding r,
            InputChannels::Receiver* p,bool shared=false) noexcept
        : attachments(a), local(l), claim(std::move(c)), receiver(std::move(r)), receiver_pointer(p),independent(shared) {}
    ~ApplicationGroup() { Settle(); }
    ApplicationGroup(const ApplicationGroup&) = delete;
    ApplicationGroup& operator=(const ApplicationGroup&) = delete;

    // Adopts one reference for a new member binding. False once the registration
    // has been settled: the release that settled it owns it until it finishes, and
    // a binding that finds it in that window must not join it. The check and the
    // increment share one critical section with the last-member decision below,
    // so no join can slip between that decision and its effect.
    bool TryJoin() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (settled) return false;
        ++members;
        return true;
    }
    // Whether a release right now would be the one that settles this registration.
    bool WouldSettle() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        return !settled && members <= 1;
    }
    bool ReleaseIfSole() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (settled || members != 1) return false;
        members = 0;
        settled = true;
        return true;
    }
    // Gives back a reference taken by TryJoin when the caller then refuses the
    // registration. It never settles: the members that remain own it.
    void Leave() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (members > 0) --members;
    }
    // Called once by each member binding as it releases, outside every other
    // gate. Reports whether this member was the last one, so the caller settles
    // the registration. The decision is latched in the same critical section as
    // the member count, so a join cannot succeed after it and before the settle
    // has been applied. The scheduling hook runs after that decision and before
    // anything is applied to it, with no lock held, so the concurrent release of a
    // sibling member, or a new member arriving, can be driven deterministically.
    bool Release(const std::function<void()>& at_decision) noexcept
    {
        bool last = false;
        {
            std::lock_guard<std::mutex> lock(gate);
            if (members > 0) --members;
            last = members == 0;
            if (last) settled = true;
        }
        if (at_decision) at_decision();
        return last;
    }
    // Idempotent: closes the receiver and detaches the claim, outside this
    // object's gate because both can wait for admissions already in flight.
    void Settle() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(gate);
            // The destructor of a registration nobody settled is the last resort;
            // it latches the same flag the release path does.
            if (closed) return;
            closed = true;
            settled = true;
        }
        if(independent) {
            // No current-map lookup. Cancel this original claim's queue before
            // waiting for captured producers; no pump is needed to make progress.
            (void)attachments.RequestDetach(claim);
            std::unique_lock<std::mutex> lock(gate);
            changed.wait(lock,[&]{return snapshots==0;});
        }
        if (receiver) (void)attachments.CloseReceiver(receiver);
        if (claim) {
            (void)attachments.RequestDetach(claim);
            (void)attachments.AwaitDetached(claim);
        }
    }

    ApplicationAttachments& attachments;
    const Locator local;
    const ApplicationClaim claim;
    const ReceiverBinding receiver;
    InputChannels::Receiver* const receiver_pointer;
    // One admission pin on this registration. Non-copyable and move-only, so a
    // capture is released exactly once, by the destructor of the object that took
    // it: the release cannot be omitted, doubled or separated from its capture,
    // and no caller has to pair them by hand. Exactly one counting path exists.
    class Pin final
    {
    public:
        Pin() noexcept = default;
        ~Pin() { Reset(); }
        Pin(Pin&& other) noexcept : group_(std::move(other.group_)) {}
        Pin& operator=(Pin&& other) noexcept
        { if (this != &other) { Reset(); group_ = std::move(other.group_); } return *this; }
        Pin(const Pin&) = delete;
        Pin& operator=(const Pin&) = delete;
        explicit operator bool() const noexcept { return bool(group_); }
    private:
        friend struct ApplicationGroup;
        void Reset() noexcept
        {
            if (!group_) return;
            {
                std::lock_guard<std::mutex> lock(group_->gate);
                if (group_->snapshots == 0) {
                    // A release with no matching acquisition would wrap the count, and then
                    // every later Settle would wait on a predicate that can never become
                    // true -- a hang with no clue about its cause. A destructor cannot
                    // refuse, so the violation is made loud and located instead of silent.
                    std::fprintf(stderr, "Fast DDS DSoftBus shared registration: admission pin released "
                            "without a matching acquisition");
                    std::fputc('\n', stderr); std::fflush(stderr); std::abort();
                }
                --group_->snapshots;
                group_->changed.notify_all();
            }
            group_.reset();
        }
        std::shared_ptr<ApplicationGroup> group_;
    };
    // Takes this registration's admission pin, or reports that it has already
    // decided to close. A settled registration is never pinned again, so nothing
    // can enter it after its last member released it. The check and the increment
    // share the gate with the last-member decision in Settle, so a pin can never
    // slip between that decision and the wait that follows it.
    static Pin TryPin(const std::shared_ptr<ApplicationGroup>& group) noexcept
    {
        Pin pin;
        if (!group) return pin;
        std::lock_guard<std::mutex> lock(group->gate);
        if (group->settled) return pin;
        ++group->snapshots;
        pin.group_ = group;
        return pin;
    }
    const bool independent;
    std::condition_variable changed;
    std::size_t snapshots=0;
    std::mutex gate;
    std::size_t members = 0;
    // Latched when the last member decides to settle: no join may succeed after
    // it, even though the closing work runs later and outside this gate.
    bool settled = false;
    // Latched when that closing work has been started, so it runs exactly once.
    bool closed = false;
};

namespace {
// The one shape that can refuse a shared input close: this thread is currently
// executing inside a delivery callback, so closing here would have to wait for this
// very frame. A destructor cannot refuse, so the contract is enforced by failing
// fast -- with the legal alternative named, and without allocating, blocking or
// starting any reclaim work. Releasing an unrelated registration, or a legal
// non-last sibling, never reaches this path.
[[noreturn]] void RefuseSharedInputRelease() noexcept
{
    std::fprintf(stderr, "Fast DDS DSoftBus shared input: released from inside a delivery callback; "
            "close the facade from an external control context (CloseRequest)");
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::abort();
}
}
BindingInput::BindingInput() noexcept=default;
BindingInput::~BindingInput(){if(group_&&Close()!=NativeStatus::None)RefuseSharedInputRelease();}
BindingInput::BindingInput(BindingInput&&) noexcept=default;
BindingInput& BindingInput::operator=(BindingInput&& other) noexcept {
    if(this!=&other){if(group_&&Close()!=NativeStatus::None)RefuseSharedInputRelease();group_=std::move(other.group_);}return *this;
}
BindingInput::operator bool() const noexcept{return bool(group_);}
NativeStatus BindingInput::Close() noexcept {
    if(!group_)return NativeStatus::None;
    if(ingress_depth||delivery_depth)return NativeStatus::WrongState;
    group_->Settle();group_.reset();return NativeStatus::None;
}
ReceiverIdentity BindingInput::Identity() const noexcept {
    return group_?group_->attachments.ObserveReceiver(group_->receiver):ReceiverIdentity{};
}

// The record's own ingress dispatch: one sink and one route table per record,
// owned by the record rather than by the binding that created it. It lives in
// process-lifetime storage keyed by record slot, because the record table keeps
// this address as the record's sink for the record's whole exposed life and a
// slot that was ever exposed is never recycled: the address therefore never
// dangles, whatever happens to any binding, and no release has to wait for an
// in-flight callback before it may proceed. A route holds a weak pin on the
// target binding's state, so an admitted call keeps its target alive for the
// whole admission and a binding that is already gone is a terminal refusal
// instead of a call into freed state.
struct RecordControl final : RecordIngressSink
{
    // The binding state this control pins. Visible here because ServiceRecordBinding
    // grants this control access to it: both are implementation details of one
    // translation unit.
    using BindingState = ServiceRecordBinding::State;
    struct Route
    {
        RecordRole role = RecordRole::Client;
        NativeSocketId socket = -1;
        std::weak_ptr<BindingState> target;
    };

    // Binds this control to the record of that incarnation. A slot carries at
    // most one exposed record for the process lifetime, so this runs at most
    // once; the clear is defensive and can never be reached with a live
    // registration.
    void Bind(std::uint64_t record_incarnation) noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        for (auto& route : routes) route = Route{};
        for (auto& group : groups) group.reset();
        refused_no_route = 0;
        incarnation = record_incarnation;
        bound = true;
    }
    bool AddRoute(RecordRole role, NativeSocketId id, const std::shared_ptr<BindingState>& target) noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        for (auto& route : routes) {
            if (route.target.expired()) {
                route = Route{role, id, target};
                return true;
            }
        }
        return false;
    }
    // One record never holds two routes for one (role, socket): the record table
    // refuses a second registration of one socket under one role.
    void RemoveRoute(RecordRole role, NativeSocketId id) noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        for (auto& route : routes) {
            if (!route.target.expired() && route.role == role && route.socket == id) {
                route = Route{};
                return;
            }
        }
    }
    // The only place a (record, local) registration is adopted, under one gate:
    // a registration whose last member has decided to settle it is finished, so
    // its entry is dropped and the caller claims a fresh one instead of joining
    // a receiver registration that is already closing.
    enum class Adopt { Fresh, Adopted, ReceiverMismatch };
    Adopt AdoptGroup(const Locator& local, InputChannels::Receiver* receiver,
            std::shared_ptr<ApplicationGroup>& adopted) noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        for (auto& slot : groups) {
            auto live = slot.lock();
            if (!live || live->independent || live->local != local) continue;
            if (!live->TryJoin()) {
                slot.reset();
                return Adopt::Fresh;
            }
            if (live->receiver_pointer != receiver) {
                live->Leave();
                return Adopt::ReceiverMismatch;
            }
            adopted = std::move(live);
            return Adopt::Adopted;
        }
        return Adopt::Fresh;
    }
    void RegisterGroup(const std::shared_ptr<ApplicationGroup>& group) noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        for (auto& slot : groups) {
            if (slot.expired()) { slot = group; return; }
        }
    }
    bool RegisterSharedGroup(const std::shared_ptr<ApplicationGroup>& group) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        for(auto& slot:groups)if(slot.expired()){slot=group;return true;}
        return false;
    }
    // The candidate set fixed at native admission: the original registrations of
    // this record on this local, in registration order. Fixing the set here is what
    // makes the receiving set an admission-time fact, and holding each candidate
    // weakly is what keeps a close from having to wait for a target this record has
    // not reached yet. The set carries no admission pin and no raw receiver: it can
    // only be used to find the original registration again, never to call one.
    struct CandidateSet {
        std::array<std::weak_ptr<ApplicationGroup>,record_owner_capacity> values{};
    };
    void FixCandidates(const Locator& local,CandidateSet& candidates) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        std::size_t n=0;
        for(const auto& slot:groups) {
            auto group=slot.lock();
            if(group&&group->independent&&group->local==local)
                candidates.values[n++]=std::move(group);
        }
    }
    std::size_t RefusedNoRoute() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        return refused_no_route;
    }
    void OnAttributedBytes(const RecordAttribution&, std::int32_t socket, const void* data,
            std::size_t size) noexcept override;

    std::mutex gate;
    std::uint64_t incarnation = 0;
    bool bound = false;
    std::array<Route, record_owner_capacity> routes{};
    // Every (application, local) registration of this record, held weakly: the
    // member bindings own it and its last one settles it.
    std::array<std::weak_ptr<ApplicationGroup>, record_owner_capacity> groups{};
    std::size_t refused_no_route = 0;
};

namespace {
// One control per record slot, in process-lifetime storage that is never
// destroyed. The record table keeps this address as the record's sink while the
// record is exposed, so it must never dangle, and a binding handle may outlive
// static destruction. Nothing here is a dynamic business object: routes and
// groups are bounded by the record's own owner capacity, and no payload, claim
// or receiver is ever retained by this object.
RecordControl& ControlForSlot(std::size_t slot) noexcept
{
    alignas(RecordControl) static unsigned char storage[sizeof(RecordControl) * record_slot_capacity];
    static RecordControl* const controls = [] {
        auto* base = reinterpret_cast<RecordControl*>(storage);
        for (std::size_t index = 0; index < record_slot_capacity; ++index) {
            new (base + index) RecordControl();
        }
        return base;
    }();
    // The caller validates the slot range, so this index is total. It is left as a
    // range check rather than an unchecked read because a bad key must not be able
    // to alias another record's control silently.
    return controls[slot < record_slot_capacity ? slot : 0];
}
} // namespace

struct ServiceRecordBinding::State final : std::enable_shared_from_this<ServiceRecordBinding::State>
{
    State(ServiceRecordTable& r, NativeOwnerTable& n, ApplicationAttachments& a, ServiceRecordProvider& p,
            const BindingRequest& q, ProcessClock c, TestHooks h)
        : records(r), native(n), attachments(a), provider(p), request(q), clock(std::move(c)),
          hooks(std::move(h)), application_owner(&a)
    {
    }

    ServiceRecordTable& records;
    NativeOwnerTable& native;
    ApplicationAttachments& attachments;
    ServiceRecordProvider& provider;
    BindingRequest request; // Only receiver changes, under gate, on RX reopen.
    ProcessClock clock;
    TestHooks hooks;

    // Guards every mutable field below. Held only for short state transitions and
    // for capturing what an operation needs, never across a receiver or sink
    // callback, across a wait, or across a call into another component that can
    // wait on something that needs this gate. Lock order, where more than one of
    // these is taken on one thread: State::gate, then the record table's, the
    // native table's, the attachments' or the record control's gate. Nothing
    // takes State::gate while holding one of those.
    mutable std::mutex gate;
    std::condition_variable receive_changed;
    std::size_t receive_admissions = 0, receive_dispatches = 0;
    bool input_closing = false;
    struct ReceiveActivity {
        State& state; bool dispatch = false; bool armed = true;
        ~ReceiveActivity() {
            if (!armed) return;
            std::lock_guard<std::mutex> lock(state.gate);
            if (dispatch) --state.receive_dispatches; else --state.receive_admissions;
            state.receive_changed.notify_all();
        }
    };
    // The record's dispatch control. Record-scoped, process lifetime, never
    // owned here: this binding only registers its own route in it.
    RecordControl* control = nullptr;
    BindingPhase phase = BindingPhase::Empty;
    bool created_record = false;
    RecordReservation reservation;
    RecordEntrance entrance;          // the role this binding serves
    RecordEntrance sibling_entrance;  // the record's other role, when created here
    RecordMembership membership;
    NamespaceLease ns;
    NativeOwnerLease owner;
    std::optional<SocketCallTicket> socket_call;
    std::optional<ListenCallTicket> listen_call;
    std::optional<BindCallTicket> bind_call;
    // The shared (application, local) registration this binding uses. It lives in
    // the record control as well, so a binding that joins the same local adopts
    // it instead of claiming a second one.
    std::shared_ptr<ApplicationGroup> group;
    // A registration that is installed but does not have its group object yet:
    // it exists only inside Publish, between ClaimApplication/OpenReceiver and
    // the group that takes them over. The release path settles it exactly like a
    // group, outside the gate, so no failure in that window -- including an
    // allocation failure while the group is being built -- can strand an
    // installed claim and leave its local unclaimable.
    ApplicationClaim pending_claim;
    ReceiverBinding pending_receiver;
    ApplicationAttachments* pending_owner = nullptr;
    // The attachments that own that registration, kept for reporting after the
    // binding has given its reference back. It is the same object the registration
    // lives in, and the caller keeps it alive for the binding's whole life.
    ApplicationAttachments* application_owner = nullptr;
    // Reporting only: whether this binding created the registration rather than
    // adopting one. It is never a licence to write another binding's state.
    bool owns_application = false;
    bool routed = false;
    bool provider_terminal = false;
    NativeSocketId socket = -1;
    NativeStatus last_status = NativeStatus::None;
    NativeStatus cleanup_status = NativeStatus::None;
    ReceiveAdmissionDiagnostic last_diagnostic;
    std::size_t admitted = 0, refused_phase = 0, refused_native = 0, refused_receive = 0;
    std::uint64_t ingress_seen = 0, correlation = 0;

    // The application whose receiver registration this binding's release would
    // close, and whether that release is really the last one: a release with another
    // member left hands the registration over and waits for nothing. Read under the
    // gate.
    // The registration's owner and application identity, and whether a release now
    // would be the last one: a release with another member left hands the
    // registration over and waits for nothing. Read under the gate.
    ApplicationAttachments* SettledOwner() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        return group ? &group->attachments : nullptr;
    }
    ApplicationIdentity SettledApplication() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (!group) return ApplicationIdentity{};
        return group->attachments.ObserveApplication(group->claim);
    }
    bool ReleaseWouldSettle() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        return group != nullptr && group->WouldSettle();
    }

    // The record table calls the control, which pins this state and then admits.
    // It never runs under this gate, and the pin is what keeps this object alive
    // for the whole admission even if the binding is released meanwhile.
    void Admit(NativeSocketId socket_id, const void* data, std::size_t size) noexcept
    {
        if(!request.shared_input_channel){AdmitOne(socket_id,data,size,{});return;}
        // A: the candidate set is fixed here, at native admission, under the record's
        //    own gate, from the registrations qualified at this moment. A
        //    registration created after this point is not in it and cannot join it.
        RecordControl::CandidateSet candidates;
        control->FixCandidates(request.local,candidates);
        bool any=false;
        for(const auto& candidate:candidates.values) {
            // B/C: reach each original target under its own identity. A target that
            //      is already gone, or that has decided to close, ends as that
            //      original target: it is neither replaced by a current-application
            //      lookup nor pinned on behalf of a target reached later.
            auto original=candidate.lock();
            if(!original)continue;
            // D: exactly this target's own admission pin, taken immediately before
            //    its own admission and released when that admission ends. This is
            //    the target's own activity lease, so its observer, copy, queue and
            //    execution keep the original drain contract, while no other
            //    target's close waits on work this record has not started yet.
            auto pin=ApplicationGroup::TryPin(original);
            if(!pin)continue;
            any=true;
            AdmitOne(socket_id,data,size,std::move(original));
        }
        if(!any)AdmitOne(socket_id,data,size,{});
    }
    void AdmitOne(NativeSocketId socket_id,const void* data,std::size_t size,
            std::shared_ptr<ApplicationGroup> original) noexcept
    {
        ReceiveActivity activity{*this, false, false};
        struct DecisionEmission {
            std::shared_ptr<ProcessReceiveObserver> observer;
            ReceiveDecision value;
            ~DecisionEmission(){if(observer)observer->OnDecision(value);}
        } decision;
        NamespaceLease namespace_lease;
        std::shared_ptr<ApplicationGroup> registration;
        std::uint64_t correlation_value = 0;
        {
            std::lock_guard<std::mutex> lock(gate);
            ++ingress_seen;
            decision.observer=request.receive_observer;
            decision.value.sequence=ingress_seen;
            decision.value.record_incarnation=entrance.record_incarnation_;
            decision.value.role=static_cast<int>(request.role);
            decision.value.socket=socket_id;
            decision.value.size=size;
            decision.value.phase=static_cast<int>(phase);
            if (phase != BindingPhase::Published) {
                ++refused_phase;
                decision.value.branch=ReceiveDecisionBranch::BindingPhase;
                return;
            }
            registration = request.shared_input_channel?std::move(original):group;
            if (!registration) {
                ++refused_receive;
                last_status = NativeStatus::NotReady;
                decision.value.branch=ReceiveDecisionBranch::NoRegistration;
                decision.value.refused_receive=true;
                decision.value.refused_receive_total=refused_receive;
                decision.value.status_known=true;
                decision.value.status=static_cast<int>(last_status);
                return;
            }
            namespace_lease = ns;
            correlation_value = ++correlation;
            decision.value.correlation=correlation_value;
            ++receive_admissions;
            activity.armed = true;
        }
        // Native owner admission. The lease pins this owner's in-flight count for
        // the whole admission and releases it when this scope ends, including on
        // every early return below.
        auto lease = namespace_lease.AdmitCallback(socket_id);
        if (!lease) {
            std::lock_guard<std::mutex> lock(gate);
            ++refused_native;
            last_status = lease.issue.code;
            decision.value.branch=ReceiveDecisionBranch::NativeAdmission;
            decision.value.status_known=true;
            decision.value.status=static_cast<int>(last_status);
            return;
        }
        auto pin = lease.value->CaptureIdentity();
        if (!pin) {
            std::lock_guard<std::mutex> lock(gate);
            ++refused_native;
            last_status = pin.issue.code;
            decision.value.branch=ReceiveDecisionBranch::NativeIdentity;
            decision.value.status_known=true;
            decision.value.status=static_cast<int>(last_status);
            return;
        }
        // The record-side membership and the native owner must describe the same
        // socket in the same local; anything else is a misconfiguration, refused
        // rather than admitted under a mismatched identity.
        if (pin.value->local() != registration->local) {
            std::lock_guard<std::mutex> lock(gate);
            ++refused_native;
            last_status = NativeStatus::InvalidHandle;
            decision.value.branch=ReceiveDecisionBranch::LocalMismatch;
            decision.value.status_known=true;
            decision.value.status=static_cast<int>(last_status);
            return;
        }
        ReceiveAdmissionDiagnostic diagnostic;
        decision.value.bridge_called=true;
        const auto result = detail::ProcessReceiveBridge::Admit(registration->attachments, native,
                registration->claim, *pin.value, data, size, ProcessClock::Clock::time_point::max(),
                clock, correlation_value, request.receive_observer, &diagnostic);
        std::lock_guard<std::mutex> lock(gate);
        last_diagnostic = diagnostic;
        last_status = FromReceiveResult(result);
        decision.value.bridge=diagnostic;
        decision.value.status_known=true;
        decision.value.status=static_cast<int>(last_status);
        decision.value.refused_receive=result!=ReceiveAdmissionResult::Accepted;
        decision.value.branch=result==ReceiveAdmissionResult::Accepted?
                ReceiveDecisionBranch::BridgeAccepted:ReceiveDecisionBranch::BridgeRefused;
        if (result == ReceiveAdmissionResult::Accepted) ++admitted;
        else {++refused_receive;decision.value.refused_receive_total=refused_receive;}
    }

    RecordEntrance EntranceFor(RecordRole role) const noexcept
    {
        if (entrance && entrance.role() == role) return entrance;
        if (sibling_entrance && sibling_entrance.role() == role) return sibling_entrance;
        return RecordEntrance{};
    }

    NativeStatus Prepare() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (phase != BindingPhase::Empty) return NativeStatus::WrongState;
        const bool joining = static_cast<bool>(request.existing);
        if(request.defer_accepted_ownership&&(!request.is_accepted||!joining||request.role!=RecordRole::Server))
            return NativeStatus::InvalidConfiguration;
        if (joining) {
            // Join: the entrance must be an issued one for a live record and role
            // that matches this binding's role. No slot is reserved and no
            // entrance is derived again. The creator is named but never read past
            // this point: the record's dispatch control is found from the record
            // itself, so the creator does not have to outlive this binding.
            if (request.record_owner == nullptr || request.existing.role() != request.role) {
                phase = BindingPhase::Failed;
                return NativeStatus::InvalidConfiguration;
            }
            const auto observed = records.Observe(request.existing);
            if (observed.status != RecordStatus::None) {
                phase = BindingPhase::Failed;
                return NativeStatus::Revoked;
            }
            if (request.existing.slot_ >= record_slot_capacity) {
                phase = BindingPhase::Failed;
                return NativeStatus::InvalidConfiguration;
            }
            entrance = request.existing;
            control = &ControlForSlot(request.existing.slot_);
        } else {
            if (request.service_name.empty() || request.record_owner != nullptr) {
                phase = BindingPhase::Failed;
                return NativeStatus::InvalidConfiguration;
            }
            auto reserved = records.ReserveRecord(request.service_name);
            if (!reserved) { phase = BindingPhase::Failed; return FromRecordStatus(reserved.status); }
            reservation = std::move(*reserved.value);
            created_record = true;
            if (reservation.slot_ >= record_slot_capacity) {
                phase = BindingPhase::Failed;
                ReleaseLocalLocked();
                return NativeStatus::InvalidConfiguration;
            }
            control = &ControlForSlot(reservation.slot_);
            if (hooks.after_record_reserve) hooks.after_record_reserve();
            // Both roles are exposed here, before anything can reach the SDK. The
            // sibling entrance leaves this object only through RoleEntrance().
            auto exposed = records.Expose(reservation, request.role, *control);
            if (!exposed) { phase = BindingPhase::Failed; return FromRecordStatus(exposed.status); }
            entrance = std::move(*exposed.value);
            control->Bind(entrance.record_incarnation_);
            const auto sibling = request.role == RecordRole::Client ? RecordRole::Server : RecordRole::Client;
            auto other = records.Expose(reservation, sibling, *control);
            if (!other) { phase = BindingPhase::Failed; return FromRecordStatus(other.status); }
            sibling_entrance = std::move(*other.value);
        }
        auto namespace_lease = native.AcquireNamespace(request.local, request.config);
        if (!namespace_lease) {
            phase = BindingPhase::Failed;
            ReleaseLocalLocked();
            return namespace_lease.issue.code;
        }
        ns = std::move(*namespace_lease.value);
        if (request.is_accepted) {
            if (request.role != RecordRole::Server || request.accepted_socket < 0) {
                phase = BindingPhase::Failed;
                ReleaseLocalLocked();
                return NativeStatus::InvalidConfiguration;
            }
            if(request.defer_accepted_ownership) {
                auto prepared=native.PrepareInbound(ns,request.accepted_socket,request.accepted);
                if(!prepared){phase=BindingPhase::Failed;ReleaseLocalLocked();return prepared.issue.code;}
                owner=std::move(*prepared.value);
                phase=BindingPhase::Prepared;
                return NativeStatus::None; // no owned socket, membership or Shutdown right
            }
            auto inbound = native.ObserveInbound(ns, request.accepted_socket, request.accepted);
            if (inbound.status != NativeStatus::None || !inbound.owner) {
                phase = BindingPhase::Failed;
                ReleaseLocalLocked();
                return inbound.status;
            }
            owner = std::move(*inbound.owner);
            socket = request.accepted_socket;
            phase = BindingPhase::Prepared;
            return NativeStatus::None;
        }
        auto reserved_owner = request.role == RecordRole::Server
                ? native.ReserveListener(ns, request.local)
                : native.ReserveDial(ns, request.route);
        if (!reserved_owner) {
            phase = BindingPhase::Failed;
            ReleaseLocalLocked();
            return reserved_owner.issue.code;
        }
        owner = std::move(reserved_owner.value->owner);
        socket_call.emplace(std::move(*reserved_owner.value->socket_call));
        if (hooks.after_owner_reserve) hooks.after_owner_reserve();
        phase = BindingPhase::Prepared;
        return NativeStatus::None;
    }

    NativeStatus Connect() noexcept
    {
        std::unique_lock<std::mutex> lock(gate);
        if (phase != BindingPhase::Prepared) return NativeStatus::WrongState;
        if(provider_terminal)return NativeStatus::OwnerUnavailable;
        if (request.is_accepted) {
            if(socket<0)return NativeStatus::NotReady;
            // The accepted socket already exists; its owner was adopted whole.
            phase = BindingPhase::Connected;
            return NativeStatus::None;
        }
        if (!socket_call || !owner) { phase = BindingPhase::Failed; return NativeStatus::WrongState; }
        auto entered = socket_call->CallEntered();
        if (entered != NativeStatus::None) { phase = BindingPhase::Failed; return entered; }
        lock.unlock(); // Original ticket remains owned; controls are externally serialized.
        const auto created = provider.CreateSocket();
        lock.lock();
        const auto socket_completion = socket_call->Complete(created);
        socket_call.reset();
        if (socket_completion.status != NativeStatus::None) {
            phase = BindingPhase::Failed;
            return socket_completion.status;
        }
        if (created.kind != NativeSocketResult::Kind::Id) {
            phase = BindingPhase::Failed;
            return NativeStatus::OwnerUnavailable;
        }
        socket = created.value;
        if (hooks.after_socket) hooks.after_socket();
        if (request.role == RecordRole::Server) {
            auto ticket = native.BeginListen(owner);
            if (!ticket) { phase = BindingPhase::Failed; return ticket.issue.code; }
            listen_call.emplace(std::move(*ticket.value));
            entered = listen_call->CallEntered();
            if (entered != NativeStatus::None) { phase = BindingPhase::Failed; return entered; }
            lock.unlock();
            const auto result = provider.Listen(socket);
            lock.lock();
            const auto completion = listen_call->Complete(result);
            listen_call.reset();
            if (completion.status != NativeStatus::None) { phase = BindingPhase::Failed; return completion.status; }
            if (!result.success) { phase = BindingPhase::Failed; return NativeStatus::OwnerUnavailable; }
        } else {
            auto ticket = native.BeginBind(owner);
            if (!ticket) { phase = BindingPhase::Failed; return ticket.issue.code; }
            bind_call.emplace(std::move(*ticket.value));
            entered = bind_call->CallEntered();
            if (entered != NativeStatus::None) { phase = BindingPhase::Failed; return entered; }
            lock.unlock();
            const auto result = provider.Bind(socket);
            lock.lock();
            const auto completion = bind_call->Complete(result);
            bind_call.reset();
            if (completion.status != NativeStatus::None) { phase = BindingPhase::Failed; return completion.status; }
            if (!result.success) { phase = BindingPhase::Failed; return NativeStatus::OwnerUnavailable; }
        }
        if(provider_terminal){phase=BindingPhase::Failed;return NativeStatus::OwnerUnavailable;}
        phase = BindingPhase::Connected;
        return NativeStatus::None;
    }

    NativeStatus Publish() noexcept
    {
        std::lock_guard<std::mutex> lock(gate);
        if (phase != BindingPhase::Connected) return NativeStatus::WrongState;
        if(provider_terminal)return NativeStatus::OwnerUnavailable;
        if (socket < 0) { phase = BindingPhase::Failed; return NativeStatus::NoOwnedId; }
        if (control == nullptr) { phase = BindingPhase::Failed; return NativeStatus::WrongState; }
        try {
            // 1) The record-side membership. After this the table can attribute
            //    this socket, and this binding refuses ingress until it is
            //    published, so a callback never sees a half-built combination.
            if (hooks.before_membership) hooks.before_membership();
            auto member = records.RegisterOwner(entrance, socket);
            if (!member) { phase = BindingPhase::Failed; return FromRecordStatus(member.status); }
            membership = std::move(*member.value);
            // 2) The application/receiver registration. It is per (application,
            //    local) and belongs to the record, so a binding on a local this
            //    record already serves adopts that registration instead of
            //    claiming a second one; a different local gets its own. The
            //    adoption is decided by the record control under its own gate, so
            //    a registration whose last member is already settling it is never
            //    adopted: it is finished, and this binding claims a fresh one.
            if(!request.shared_input_channel) {
            if (hooks.before_receiver) hooks.before_receiver();
            std::shared_ptr<ApplicationGroup> adopted;
            switch (control->AdoptGroup(request.local, request.receiver, adopted)) {
            case RecordControl::Adopt::Adopted:
                group = std::move(adopted);
                application_owner = &group->attachments;
                owns_application = false;
                break;
            case RecordControl::Adopt::ReceiverMismatch:
                // One receiver registration exists per (application, local); a
                // binding adopting a different receiver would believe it owns a
                // registration it does not.
                phase = BindingPhase::Failed;
                if(!request.defer_accepted_ownership)ReleaseLocalLocked();
                return NativeStatus::InvalidConfiguration;
            case RecordControl::Adopt::Fresh:
                break;
            }
            if (!group) {
                auto claim_outcome = attachments.ClaimApplication({request.local}, request.directory);
                if (claim_outcome.result != ApplicationClaimResult::Claimed) {
                    phase = BindingPhase::Failed;
                    if(!request.defer_accepted_ownership)ReleaseLocalLocked();
                    return FromClaimResult(claim_outcome.result);
                }
                // The claim is installed by ClaimApplication, so this binding owns
                // its teardown from this point on: a failure below must detach the
                // claim, not drop the handle. Only AwaitDetached clears the
                // attachments' local slot, so dropping the handle would leave the
                // application Active and that local permanently unclaimable. It is
                // parked in the state immediately, so every failure below is
                // settled by the release path after the gate is released, exactly
                // like a group.
                ApplicationClaim claim = std::move(claim_outcome.claim);
                pending_claim = claim;
                pending_owner = &attachments;
                auto open_outcome = attachments.OpenReceiver(claim, request.local, request.receiver);
                if (open_outcome.result != ReceiverOpenResult::Opened) {
                    phase = BindingPhase::Failed;
                    if(!request.defer_accepted_ownership)ReleaseLocalLocked();
                    return FromReceiverResult(open_outcome.result);
                }
                pending_receiver = std::move(open_outcome.binding);
                if (hooks.before_registration) hooks.before_registration();
                auto created = std::make_shared<ApplicationGroup>(attachments, request.local,
                        std::move(pending_claim), std::move(pending_receiver), request.receiver);
                if (!created->TryJoin()) {
                    // Unreachable for a freshly built registration; refuse rather
                    // than publish on a registration that is already settling.
                    phase = BindingPhase::Failed;
                    if(!request.defer_accepted_ownership)ReleaseLocalLocked();
                    return NativeStatus::WrongState;
                }
                pending_owner = nullptr;
                group = created;
                application_owner = &group->attachments;
                owns_application = true;
                control->RegisterGroup(created);
            }
            }
            // 3) Route this socket to this binding in the record's own control,
            //    then the single publication point. A joiner registers in the
            //    same record control, so the record serves every owner of it.
            if (!control->AddRoute(request.role, socket, shared_from_this())) {
                phase = BindingPhase::Failed;
                if(!request.defer_accepted_ownership)ReleaseLocalLocked();
                return NativeStatus::AdmissionSaturated;
            }
            routed = true;
            if (hooks.before_publish) hooks.before_publish();
            phase = BindingPhase::Published;
            return NativeStatus::None;
        } catch (const std::bad_alloc&) {
            phase = BindingPhase::Failed;
            if(!request.defer_accepted_ownership)ReleaseLocalLocked();
            return NativeStatus::AllocationFailure;
        } catch (const std::invalid_argument&) {
            phase = BindingPhase::Failed;
            if(!request.defer_accepted_ownership)ReleaseLocalLocked();
            return NativeStatus::InvalidConfiguration;
        }
    }

    void ReleaseLocal() noexcept
    {
        // The callback-context decision belongs to the destructor, which is the only
        // caller and which knows the registration identity this thread is inside; by
        // the time this runs that decision is already made.
        std::unique_lock<std::mutex> lock(gate);
        ReleaseLocalLocked();
    }

    NativeStatus CloseInput() noexcept
    {
        if (ingress_depth > 0 || delivery_depth > 0) return NativeStatus::WrongState;
        std::shared_ptr<ApplicationGroup> old;
        {
            std::lock_guard<std::mutex> lock(gate);
            if (phase == BindingPhase::Closed) return NativeStatus::None;
            if (phase != BindingPhase::Published || input_closing) return NativeStatus::WrongState;
            if (!group) return NativeStatus::None;
            // Atomic with TryJoin. Refusal does not alter a shared registration.
            if (!group->ReleaseIfSole()) return NativeStatus::ExistingPending;
            old = std::move(group); input_closing = true;
            request.receiver = nullptr; owns_application = false;
        }
        // Stop commit and cancel queued records first. This is a reachable
        // cancellation path even when there is no manual RX pump/worker.
        (void)old->attachments.RequestDetach(old->claim);
        old->Settle();
        {
            std::unique_lock<std::mutex> lock(gate);
            receive_changed.wait(lock, [&] { return receive_admissions == 0 && receive_dispatches == 0; });
        }
        old.reset(); // Release every old callable receiver reference before return.
        std::lock_guard<std::mutex> lock(gate); input_closing = false;
        return NativeStatus::None;
    }

    NativeStatus ReopenInput(InputChannels::Receiver* receiver) noexcept
    {
        if (ingress_depth > 0 || delivery_depth > 0 || !receiver) return NativeStatus::WrongState;
        {
            std::lock_guard<std::mutex> lock(gate);
            if (phase != BindingPhase::Published || input_closing || group) return NativeStatus::WrongState;
        }
        ApplicationClaim claim;
        ReceiverBinding receiver_binding;
        std::shared_ptr<ApplicationGroup> candidate;
        const auto rollback = [&]() noexcept {
            if (candidate) { candidate->Release({}); candidate->Settle(); candidate.reset(); }
            else if (claim) {
                (void)attachments.RequestDetach(claim);
                (void)attachments.AwaitDetached(claim);
            }
        };
        try {
            auto claimed = attachments.ClaimApplication({request.local}, request.directory);
            if (claimed.result != ApplicationClaimResult::Claimed) return FromClaimResult(claimed.result);
            claim = std::move(claimed.claim);
            auto opened = attachments.OpenReceiver(claim, request.local, receiver);
            if (opened.result != ReceiverOpenResult::Opened) { rollback(); return FromReceiverResult(opened.result); }
            receiver_binding = std::move(opened.binding);
            candidate = std::make_shared<ApplicationGroup>(attachments, request.local, claim, receiver_binding, receiver);
            if (!candidate->TryJoin()) { rollback(); return NativeStatus::WrongState; }
            control->RegisterGroup(candidate);
            {
                std::lock_guard<std::mutex> lock(gate);
                group = candidate; request.receiver = receiver; owns_application = true;
                application_owner = &attachments;
            }
            return NativeStatus::None;
        } catch (const std::bad_alloc&) { rollback(); return NativeStatus::AllocationFailure; }
          catch (...) { rollback(); return NativeStatus::InvalidConfiguration; }
    }

    // Releases everything this binding holds locally, in reverse order. It never
    // reclaims a slot that was exposed, never rebinds an entrance, and never
    // withdraws a registration another binding of the same record still uses: it
    // removes only its own route, gives back its own reference to the shared
    // (application, local) registration, and releases its own membership.
    // The caller holds `gate`; the steps that can wait for another admission run
    // after it is released.
    void ReleaseLocalLocked() noexcept
    {
        if (phase == BindingPhase::Published) phase = BindingPhase::Closing;
        RecordControl* record_control = control;
        const RecordRole role = request.role;
        const NativeSocketId released_socket = socket;
        const bool was_routed = routed;
        std::shared_ptr<ApplicationGroup> registration = std::move(group);
        // A registration that never reached its group object is settled here too,
        // so an allocation failure inside Publish cannot strand an installed claim.
        ApplicationClaim orphan_claim = std::move(pending_claim);
        ReceiverBinding orphan_receiver = std::move(pending_receiver);
        ApplicationAttachments* orphan_owner = pending_owner;
        pending_owner = nullptr;
        NamespaceLease namespace_lease = std::move(ns);
        NativeOwnerLease owned = std::move(owner);
        std::optional<SocketCallTicket> pending_socket = std::move(socket_call);
        std::optional<ListenCallTicket> pending_listen = std::move(listen_call);
        std::optional<BindCallTicket> pending_bind = std::move(bind_call);
        RecordMembership released_membership = std::move(membership);
        const RecordEntrance released_entrance = entrance;
        const RecordEntrance released_sibling = sibling_entrance;
        const bool exposed_here = created_record;
        RecordReservation released_reservation = std::move(reservation);
        owns_application = false;
        routed = false;
        socket = -1;

        gate.unlock();

        // The binding leaves the record's dispatch first: once its route is gone
        // the control can no longer pin this state, so no new ingress can reach
        // it, and an ingress that already pinned it completes under the pin.
        if (record_control != nullptr && was_routed) {
            record_control->RemoveRoute(role, released_socket);
        }
        // The shared registration is settled by whichever release is really last.
        // No release writes a sibling binding's state.
        if (registration) {
            const bool last = registration->Release(hooks.before_group_settle);
            if (last) registration->Settle();
            registration.reset();
        }
        // Same settlement for a registration that never got its group object. It
        // cannot be shared: it was never registered with the control.
        if (orphan_owner != nullptr) {
            if (orphan_receiver) (void)orphan_owner->CloseReceiver(orphan_receiver);
            if (orphan_claim) {
                (void)orphan_owner->RequestDetach(orphan_claim);
                (void)orphan_owner->AwaitDetached(orphan_claim);
            }
        }
        // The socket close responsibility is claimed exactly once, and the
        // provider's shutdown is an external call, so it runs here, outside the
        // gate.
        NativeStatus close_failure=NativeStatus::None;
        if (owned) {
            (void)owned.RequestClose();
            // Close credit belongs to the native table, not this record. A
            // concurrent control close may hold its sole credit. Keep this
            // original owner/namespace until it can claim responsibility.
            // No component lock is held while waiting or calling the provider;
            // this is not an SDK timeout or a cancellation guarantee.
            auto claim=[&]() -> NativeResult<ShutdownCallTicket> {
                for(;;){
                    auto candidate=owned.TryClaimShutdown();
                    if(candidate||candidate.issue.code!=NativeStatus::CloseBusy)return candidate;
                    // NativeValidateConfiguration requires close_calls==1.
                    // In this precise reentrant destructor shape, only returning
                    // from this same provider stack could make progress.
                    if(HoldsCloseCredit(&native)){
                        std::fputs("Fast DDS DSoftBus ServiceRecordBinding: destroyed from inside a callback; native close credit held by this stack; call CloseRequest() and destroy outside the callback\n",stderr);
                        std::fflush(stderr);std::abort();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            auto ticket=claim();
            if (ticket && released_socket >= 0) {
                const auto entered=ticket.value->CallEntered();
                if(entered==NativeStatus::None){
                    // A synchronous provider callback cannot close a sibling
                    // and wait for the credit held by this very call stack.
                    ProviderShutdownFrame context(&native);
                    provider.Shutdown(released_socket);
                    const auto completed=ticket.value->CompleteShutdownReturned();
                    if(completed!=NativeStatus::None)close_failure=completed;
                }else{
                    const auto facts=owned.Observe();
                    if(!facts.on_shutdown_seen&&!facts.shutdown_returned)close_failure=entered;
                }
            }else if(!ticket){
                const auto facts=owned.Observe();
                if(facts.owned_id&&!facts.on_shutdown_seen&&!facts.shutdown_returned)
                    close_failure=ticket.issue.code;
            }
        }
        pending_socket.reset();
        pending_listen.reset();
        pending_bind.reset();
        owned = NativeOwnerLease{};
        namespace_lease = NamespaceLease{};
        // This binding's own record-side registrations. The exposure is abandoned,
        // but the issued entrance identity is kept in the member: it belongs to
        // the record, which stays alive for other roles and other owners, and a
        // released binding must not hand out a null for a record that still
        // exists.
        if (released_membership) (void)records.ReleaseOwner(released_membership);
        // Only the binding that exposed a role reports that its registration
        // attempt produced no owner. A joiner holds a copy of the exposing
        // binding's entrance; reporting abandonment for it would drop the
        // exposing binding's own pending anchor and could seal the record and
        // withdraw its sink while that binding is still alive and about to
        // publish.
        if (exposed_here) {
            if (released_entrance) (void)records.AbandonExposure(released_entrance); // ExposureSettled once an owner registered
            if (released_sibling) (void)records.AbandonExposure(released_sibling);
        }
        if (released_reservation) (void)records.AbandonUnexposed(released_reservation); // NotUnexposed once exposed: never reclaimed
        // With its last anchor gone the record has no user of its dispatch left,
        // so it ends the sink registration. The control object itself is
        // process-lifetime storage and is not freed here; the table refuses this
        // while the record still has any anchor.
        if (record_control != nullptr) {
            const RecordEntrance& identity = released_entrance ? released_entrance : released_sibling;
            if (identity) (void)records.ReleaseSink(identity, *record_control);
        }
        // The caller still holds the gate it handed in; the steps that could wait
        // for another admission ran with it released.
        gate.lock();
        if(cleanup_status==NativeStatus::None)cleanup_status=close_failure;
    }

    NativeStatus Abandon() noexcept
    {
        if (ingress_depth > 0 || delivery_depth > 0) return NativeStatus::WrongState;
        std::lock_guard<std::mutex> lock(gate);
        if (phase == BindingPhase::Closed) return NativeStatus::AlreadySettled;
        const auto previous = phase;
        if (previous == BindingPhase::Empty) return NativeStatus::WrongState;
        if (hooks.before_rollback) hooks.before_rollback();
        ReleaseLocalLocked();
        phase = BindingPhase::Closed;
        return cleanup_status;
    }
};

void RecordControl::OnAttributedBytes(const RecordAttribution& attribution, std::int32_t socket,
        const void* data, std::size_t size) noexcept
{
    // The record table's gate is already released; this runs outside it.
    ++ingress_depth;
    struct DepthGuard
    {
        ~DepthGuard() { --ingress_depth; }
    } guard;
    std::shared_ptr<BindingState> target;
    {
        std::lock_guard<std::mutex> lock(gate);
        for (const auto& route : routes) {
            if (route.role != attribution.role() || route.socket != socket) continue;
            target = route.target.lock();
            break;
        }
        if (!target) {
            // No binding of this record owns that (role, socket): a terminal
            // refusal, never a queued record kept for a later binding.
            ++refused_no_route;
            return;
        }
    }
    // The pin is held for the whole admission: the binding may be released on
    // another thread meanwhile, and then refuses at its own phase gate instead of
    // being read after it is gone.
    target->Admit(socket, data, size);
}

BindingDelivery::BindingDelivery() noexcept = default;
BindingDelivery::~BindingDelivery() = default;
// The issuer is the *state* of the binding that issued this delivery, captured
// here and never looked up again, so it cannot be run through another binding. It
// is weak, and the comparison is made against a freshly locked handle, so a
// binding allocated where the issuer was cannot satisfy a stale delivery. A
// moved-from handle reports false and offers no execution.
BindingDelivery::BindingDelivery(ProcessReceiveInvocation&& invocation,
        std::weak_ptr<void> issuer) noexcept
    : invocation_(std::move(invocation)), issuer_(std::move(issuer)), issued_(true) {}
BindingDelivery::BindingDelivery(BindingDelivery&& other) noexcept
    : invocation_(std::move(other.invocation_)), issuer_(std::move(other.issuer_)),
      issued_(other.issued_)
{
    other.issued_ = false;
}
BindingDelivery& BindingDelivery::operator=(BindingDelivery&& other) noexcept
{
    if (this != &other) {
        invocation_ = std::move(other.invocation_);
        issuer_ = std::move(other.issuer_);
        issued_ = other.issued_;
        other.issued_ = false;
    }
    return *this;
}
BindingDelivery::operator bool() const noexcept { return issued_; }
ReceiveDiagnostic BindingDelivery::Observation() const noexcept
{
    return issued_ ? invocation_.Observation() : ReceiveDiagnostic{};
}

ServiceRecordBinding::ServiceRecordBinding(std::shared_ptr<State> state) noexcept : state_(std::move(state)) {}
ServiceRecordBinding::~ServiceRecordBinding()
{
    if (!state_) return;
    const bool would_settle = state_->ReleaseWouldSettle();
    if (would_settle &&
            SettleWaitsOnThisThread(state_->SettledOwner(), state_->SettledApplication())) {
        // Contract violation, and the one shape that can be named exactly: this
        // release would have to wait for the in-flight receiver permit of the very
        // registration this thread is currently executing inside, so it would
        // deadlock rather than release, and a destructor cannot refuse. The contract
        // is therefore enforced by failing fast, with the legal alternative named: a
        // callback asks for the close with CloseRequest(), and the owner destroys
        // the binding outside the callback. Destroying an *unrelated* binding from a
        // callback does not take this path -- its release waits on nothing this
        // frame holds -- and neither does any normal lifetime, such as the creator
        // leaving before its joiners.
        std::fprintf(stderr, "Fast DDS DSoftBus ServiceRecordBinding: destroyed from inside a callback; "
                "call CloseRequest() and destroy the binding outside the callback");
        std::fputc('\n', stderr);
        std::fflush(stderr);
        std::abort();
    }
    state_->ReleaseLocal();
}
NativeResult<std::unique_ptr<ServiceRecordBinding>> ServiceRecordBinding::Build(ServiceRecordTable& records,
        NativeOwnerTable& native, ApplicationAttachments& attachments, ServiceRecordProvider& provider,
        const BindingRequest& request, ProcessClock clock)
{
    return BuildWithHooks(records, native, attachments, provider, request, std::move(clock), {});
}
NativeResult<std::unique_ptr<ServiceRecordBinding>> ServiceRecordBinding::BuildWithHooks(
        ServiceRecordTable& records, NativeOwnerTable& native, ApplicationAttachments& attachments,
        ServiceRecordProvider& provider, const BindingRequest& request, ProcessClock clock, TestHooks hooks)
{
    try {
        auto state = std::make_shared<State>(records, native, attachments, provider, request,
                std::move(clock), std::move(hooks));
        return {std::unique_ptr<ServiceRecordBinding>(new ServiceRecordBinding(std::move(state))), {}};
    } catch (const std::bad_alloc&) {
        return {std::nullopt, {NativeStatus::AllocationFailure}};
    }
}
NativeStatus ServiceRecordBinding::Prepare() noexcept
{ return state_ ? state_->Prepare() : NativeStatus::InvalidHandle; }
NativeResult<BindingInput> ServiceRecordBinding::AttachSharedInput(InputChannels::Receiver* receiver) noexcept {
    if(!state_||!receiver||IsCallbackContext())return {std::nullopt,{NativeStatus::WrongState}};
    auto& s=*state_;
    {std::lock_guard<std::mutex> lock(s.gate);
        if(!s.request.shared_input_channel||s.phase!=BindingPhase::Published||s.provider_terminal)
            return {std::nullopt,{NativeStatus::WrongState}};
    }
    ApplicationClaim claim;
    const auto rollback=[&]() noexcept {if(claim){s.attachments.RequestDetach(claim);s.attachments.AwaitDetached(claim);}};
    try {
        auto result=s.attachments.ClaimSharedApplication({s.request.local},s.request.directory);
        if(result.result!=ApplicationClaimResult::Claimed)return {std::nullopt,{FromClaimResult(result.result)}};
        claim=std::move(result.claim);
        auto opened=s.attachments.OpenReceiver(claim,s.request.local,receiver);
        if(opened.result!=ReceiverOpenResult::Opened){rollback();return {std::nullopt,{FromReceiverResult(opened.result)}};}
        BindingInput input;
        input.group_=std::make_shared<ApplicationGroup>(s.attachments,s.request.local,claim,std::move(opened.binding),receiver,true);
        if(!s.control->RegisterSharedGroup(input.group_))return {std::nullopt,{NativeStatus::AdmissionSaturated}};
        return {std::move(input),{}};
    }catch(const std::bad_alloc&){rollback();return {std::nullopt,{NativeStatus::AllocationFailure}};}
}
NativeOwnerLease ServiceRecordBinding::OriginalOwner() const noexcept
{ if(!state_)return {};std::lock_guard<std::mutex> lock(state_->gate);return state_->owner; }
NativeStatus ServiceRecordBinding::CommitAccepted(NativeSocketId id) noexcept
{
    if(!state_)return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);
    if(state_->provider_terminal)return NativeStatus::Revoked;
    if(!state_->request.is_accepted||!state_->request.defer_accepted_ownership||state_->phase!=BindingPhase::Prepared)
        return NativeStatus::WrongState;
    const auto result=state_->native.CommitInbound(state_->owner,id);
    if(result==NativeStatus::None)state_->socket=id;
    return result;
}
NativeStatus ServiceRecordBinding::ObserveProviderShutdown(NativeSocketId id) noexcept
{
    if(!state_)return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);
    if(!state_->owner||state_->socket<0||id!=state_->socket)return NativeStatus::UnknownSocket;
    auto callback=state_->ns.AdmitCallback(id);
    if(!callback)return callback.issue.code;
    // The fixed binding's original owner must match; never accept a replacement
    // found by numeric ID alone. This is adapter capture, not SDK epoch proof.
    if(callback.value->CapturedOwner().incarnation!=state_->owner.Observe().incarnation)
        return NativeStatus::StaleHandle;
    const auto result=callback.value->ObserveShutdown();
    if(result==NativeStatus::None){state_->provider_terminal=true;if(state_->phase==BindingPhase::Published)state_->phase=BindingPhase::Closing;}
    return result;
}
NativeStatus ServiceRecordBinding::Connect() noexcept
{ return state_ ? state_->Connect() : NativeStatus::InvalidHandle; }
NativeStatus ServiceRecordBinding::Publish() noexcept
{ return state_ ? state_->Publish() : NativeStatus::InvalidHandle; }
NativeStatus ServiceRecordBinding::Abandon() noexcept
{ return state_ ? state_->Abandon() : NativeStatus::InvalidHandle; }
NativeStatus ServiceRecordBinding::CloseInput() noexcept
{ return state_ ? state_->CloseInput() : NativeStatus::InvalidHandle; }
NativeStatus ServiceRecordBinding::ReopenInput(InputChannels::Receiver* receiver) noexcept
{ return state_ ? state_->ReopenInput(receiver) : NativeStatus::InvalidHandle; }
NativeStatus ServiceRecordBinding::CloseRequest() noexcept
{
    if (!state_) return NativeStatus::InvalidHandle;
    std::lock_guard<std::mutex> lock(state_->gate);
    // Deliberately no callback guard: this is the one control entry a callback may
    // use, and it only records the intent. Taking the gate is safe from a callback
    // because no callback of this composition runs with it held.
    if (state_->phase == BindingPhase::Closing || state_->phase == BindingPhase::Closed) {
        return NativeStatus::ServiceClosing;
    }
    if (state_->phase != BindingPhase::Published) return NativeStatus::WrongState;
    state_->phase = BindingPhase::Closing;
    return NativeStatus::None;
}
NativeStatus ServiceRecordBinding::DeferredClose() noexcept
{
    if (!state_) return NativeStatus::InvalidHandle;
    if (ingress_depth > 0 || delivery_depth > 0) return NativeStatus::WrongState;  // refused, never waited on
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->phase == BindingPhase::Closed) return NativeStatus::AlreadySettled;
    if (state_->phase != BindingPhase::Closing) return NativeStatus::WrongState;  // nothing was requested
    state_->ReleaseLocalLocked();
    state_->phase = BindingPhase::Closed;
    return state_->cleanup_status;
}
RecordBytesCallback ServiceRecordBinding::Entrance() const noexcept
{
    if (!state_) return nullptr;
    std::lock_guard<std::mutex> lock(state_->gate);
    return state_->entrance ? state_->entrance.function() : nullptr;
}
RecordEntrance ServiceRecordBinding::RoleEntrance(RecordRole role) const noexcept
{
    if (!state_) return RecordEntrance{};
    std::lock_guard<std::mutex> lock(state_->gate);
    return state_->EntranceFor(role);
}
NativeResult<BindingDelivery> ServiceRecordBinding::NextAdmitted() noexcept
{
    if (!state_) return {std::nullopt, {NativeStatus::InvalidHandle}};
    ApplicationAttachments* owner = nullptr;
    ProcessClock clock;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->phase != BindingPhase::Published && state_->phase != BindingPhase::Closing) {
            return {std::nullopt, {NativeStatus::WrongState}};
        }
        if (!state_->group&&!state_->request.shared_input_channel) return {std::nullopt, {NativeStatus::NotReady}};
        owner = state_->request.shared_input_channel?&state_->attachments:&state_->group->attachments;
        clock = state_->clock;
        ++state_->receive_dispatches;
    }
    State::ReceiveActivity activity{*state_, true};
    auto next = detail::ProcessReceiveBridge::Next(*owner, state_->native, clock);
    if (!next) return {std::nullopt, next.issue};
    // Mark the delivery context on the execution itself, so the context is entered by
    // whichever thread runs it and through whichever entry point. The hooks read the
    // application the delivery runs under from that execution.
    detail::ProcessReceiveBridge::ArmDeliveryContext(*next.value, &EnterDeliveryContext,
            &ExitDeliveryContext);
    return {BindingDelivery(std::move(*next.value), std::weak_ptr<void>(state_)), {}};
}
InputDeliveryResult ServiceRecordBinding::RunAdmitted(BindingDelivery& delivery) noexcept
{
    if (!state_ || !delivery.issued_) return InputDeliveryResult::InvalidTicket;
    auto issuer = delivery.issuer_.lock();
    if (!issuer || issuer.get() != static_cast<void*>(state_.get())) {
        return InputDeliveryResult::InvalidTicket;
    }
    return delivery.invocation_.Run();
}
NativeStatus ServiceRecordBinding::BeginClose() noexcept
{
    if (!state_) return NativeStatus::InvalidHandle;
    if (ingress_depth > 0 || delivery_depth > 0) return NativeStatus::WrongState;  // refused, never waited on
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->phase == BindingPhase::Closing || state_->phase == BindingPhase::Closed) {
        return NativeStatus::ServiceClosing;
    }
    if (state_->phase != BindingPhase::Published) return NativeStatus::WrongState;
    // Logical admission is closed for new ingress only. Already admitted work
    // keeps its original owner and lease, so the native owner is not closed here:
    // its close responsibility is claimed by the release path, after the caller
    // has drained.
    state_->phase = BindingPhase::Closing;
    return NativeStatus::None;
}
bool ServiceRecordBinding::DrainComplete() const noexcept
{
    if (!state_) return true;
    const auto observation = Observe();
    return observation.callbacks_in_flight == 0 && observation.owner_callback_inflight == 0 &&
            observation.owner_receive_inflight == 0 && observation.receive.copying == 0 &&
            observation.receive.taken == 0 && observation.receive.delivering == 0 &&
            observation.receive.queued == 0;
}
RecordStatus ServiceRecordBinding::RetireRecord() noexcept
{
    if (!state_) return RecordStatus::NotFound;
    RecordEntrance identity;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->phase != BindingPhase::Closed) return RecordStatus::StillAnchored;
        identity = state_->entrance;
    }
    if (!identity) return RecordStatus::NotFound;
    // DrainComplete takes the same gate, so it is asked after this one is
    // released, and only from the control context the contract allows.
    if (!DrainComplete()) return RecordStatus::StillAnchored;
    // The caller declares the destruction premise by calling this; it is never
    // derived from a name, a count or an assumption.
    return state_->records.ConfirmDestroyed(identity);
}
bool ServiceRecordBinding::IsCallbackContext() noexcept
{ return ingress_depth > 0 || delivery_depth > 0; }
BindingObservation ServiceRecordBinding::Observe() const noexcept
{
    BindingObservation view;
    if (!state_) return view;
    // The binding's own scalars are captured under its gate; every component
    // observation is asked afterwards, so no component gate is ever held while
    // another is taken from here.
    RecordEntrance identity;
    NativeOwnerLease owner;
    std::shared_ptr<ApplicationGroup> registration;
    RecordControl* control = nullptr;
    ApplicationAttachments* registration_owner = nullptr;
    std::size_t role = 0;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        const auto& s = *state_;
        view.phase = s.phase;
        view.input_closing = s.input_closing;
        view.created_record = s.created_record;
        view.socket = s.socket;
        view.admitted = s.admitted;
        view.refused_phase = s.refused_phase;
        view.refused_native = s.refused_native;
        view.refused_receive = s.refused_receive;
        view.ingress_seen = s.ingress_seen;
        view.membership = static_cast<bool>(s.membership);
        view.application_bound = static_cast<bool>(s.group);
        view.owns_application = s.owns_application;
        identity = s.entrance;
        owner = s.owner;
        registration = s.group;
        control = s.control;
        registration_owner = s.application_owner;
        role = static_cast<std::size_t>(s.request.role);
    }
    view.status = NativeStatus::None;
    if (identity) {
        const auto record = state_->records.Observe(identity);
        view.role_exposed = record.role_exposed[role];
        view.role_members = record.role_owners[role];
        view.record_incarnation = record.record_incarnation;
        view.callbacks_in_flight = record.callbacks_in_flight;
    }
    if (owner) {
        const auto observation = owner.Observe();
        view.owner_slot = observation.slot;
        view.owner_incarnation = observation.incarnation;
        view.owner_callback_inflight = observation.callback_inflight;
        view.owner_receive_inflight = observation.receive_inflight;
        view.owner_closed = observation.close_intent;
        view.socket_known = observation.owned_id.has_value();
        view.setup_complete = observation.phase == NativeOwnerPhase::Ready ||
                observation.phase == NativeOwnerPhase::Listening;
    }
    if (control != nullptr) view.refused_no_route = control->RefusedNoRoute();
    if (registration) {
        view.receiver_open = static_cast<bool>(registration->receiver);
        view.receiver_identity = registration->attachments.ObserveReceiver(registration->receiver);
    }
    if (registration_owner != nullptr) view.receive = registration_owner->SnapshotUsage();
    return view;
}
} // namespace fastdds_dsoftbus::dsoftbus
