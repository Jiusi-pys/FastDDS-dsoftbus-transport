// Bounded implementation of the shared service-record table contracts in
// PLAN.md section 18. It makes no SDK call, starts no thread and observes no
// real record creation or destruction: it models the adapter's side of the
// SDK client-process service record (identity unit: record incarnation + role)
// and its single attribution linearization point.
#include <mdds/dsoftbus/ServiceRecordTable.hpp>

#include <algorithm>
#include <limits>
#include <mutex>
#include <new>

namespace mdds::dsoftbus {
namespace {
constexpr std::size_t no_index = static_cast<std::size_t>(-1);
constexpr std::size_t role_count = static_cast<std::size_t>(RecordRole::Count);

std::size_t RoleIndex(RecordRole role) noexcept { return static_cast<std::size_t>(role); }
bool RoleValid(RecordRole role) noexcept { return RoleIndex(role) < role_count; }
bool NameValid(const std::string& name) noexcept
{
    return !name.empty() && name.size() <= record_max_service_name_bytes &&
            name.find('\0') == std::string::npos;
}
template<class T> RecordResult<T> Fail(RecordStatus status) noexcept
{
    return {std::nullopt, status};
}
} // namespace

namespace detail {
struct RecordOwnerCell
{
    bool used = false;
    std::int32_t socket = -1;
    std::uint64_t incarnation = 0;
    // The role field this socket registered under. One numeric id is never two
    // roles of one record: the model cannot tell a reused id from a distinct
    // socket, so it refuses instead of guessing.
    RecordRole role = RecordRole::Client;
};

struct RecordSlot
{
    bool used = false;
    RecordPhase phase = RecordPhase::Free;
    std::string name;
    std::uint64_t incarnation = 0, reservation = 0;
    bool role_exposed[role_count] = {false, false};
    // An exposure is an anchor until its first owner registers or the caller
    // reports that the registration failed. A slot that was ever exposed never
    // returns to FREE.
    bool role_pending[role_count] = {false, false};
    RecordIngressSink* sink = nullptr;
    std::size_t sink_calls_in_flight = 0;
    std::array<RecordOwnerCell, record_owner_capacity> owners{};
    std::size_t owner_count = 0, owner_high_water = 0;
    std::uint64_t next_owner_incarnation = 1;

    std::size_t FindOwner(std::int32_t socket) const noexcept
    {
        for (std::size_t i = 0; i < owners.size(); ++i) {
            if (owners[i].used && owners[i].socket == socket) return i;
        }
        return no_index;
    }
    std::size_t FindOwner(std::int32_t socket, RecordRole role) const noexcept
    {
        for (std::size_t i = 0; i < owners.size(); ++i) {
            if (owners[i].used && owners[i].socket == socket && owners[i].role == role) return i;
        }
        return no_index;
    }
    std::size_t FreeOwner() const noexcept
    {
        if (owner_count >= record_owner_capacity) return no_index;
        for (std::size_t i = 0; i < owners.size(); ++i) {
            if (!owners[i].used) return i;
        }
        return no_index;
    }
    std::size_t Anchors() const noexcept
    {
        std::size_t anchors = owner_count;
        for (std::size_t i = 0; i < role_count; ++i) {
            if (role_pending[i]) ++anchors;
        }
        return anchors;
    }
    bool Sealed() const noexcept { return phase == RecordPhase::Unanchored; }
};

struct ServiceRecordState
{
    std::mutex gate;
    std::array<RecordSlot, record_slot_capacity> slots{};
    // Slot transitions shared by the reporting operations and the RAII handle
    // paths, so both can never diverge. The caller holds the gate. Both are
    // idempotent and report nothing: a destructor has no way to report.
    void ReclaimReservation(std::size_t slot, std::uint64_t reservation) noexcept
    {
        if (slot >= slots.size()) return;
        auto& cell = slots[slot];
        if (!cell.used || cell.reservation != reservation) return;
        if (cell.phase != RecordPhase::ReservedUnexposed) return;
        cell = RecordSlot{};
    }
    void ReleaseMembership(std::size_t slot, std::uint64_t record_incarnation,
            std::uint64_t membership_incarnation, std::int32_t socket, RecordRole role) noexcept
    {
        if (slot >= slots.size()) return;
        auto& cell = slots[slot];
        if (!cell.used || cell.incarnation != record_incarnation) return;
        const auto owner = cell.FindOwner(socket, role);
        if (owner == no_index || cell.owners[owner].incarnation != membership_incarnation) return;
        cell.owners[owner] = RecordOwnerCell{};
        --cell.owner_count;
        --usage.owners;
        if (cell.phase == RecordPhase::Exposed && cell.Anchors() == 0) cell.phase = RecordPhase::Unanchored;
    }
    // Monotonic: a retired slot is never recycled, so every record incarnation
    // and every fixed entrance is unique for the process lifetime.
    std::uint64_t next_record_incarnation = 1, next_reservation = 1;
    std::size_t owner_high_water = 0;
    // Attributed callbacks whose sink call has not returned yet. Snapshot only;
    // nothing waits on it, because a captured callback is never retracted.
    std::size_t callbacks_in_flight = 0;
    ServiceRecordUsage usage{};
};
} // namespace detail

namespace {
// The record space is process-lifetime: the SDK service record belongs to the
// client process and the adapter's fixed entrance pool cannot be reset. It is
// constructed once in static storage and never destroyed, moved or reallocated,
// so handles point at it directly: no allocation, no deleter, no reference
// counting on a callback path, and no destruction order to get wrong.
detail::ServiceRecordState& ProcessState() noexcept
{
    // Intentionally never destroyed. A handle may outlive static destruction
    // (a caller is allowed to keep memberships for as long as its sockets are
    // registered), and its destructor locks this state's gate, so the object
    // must never be destroyed at all. Placement construction in static storage
    // gives that with no allocation and no destructor to order against.
    alignas(detail::ServiceRecordState) static unsigned char storage[sizeof(detail::ServiceRecordState)];
    static detail::ServiceRecordState* const state = new (storage) detail::ServiceRecordState();
    return *state;
}
// Entrance validation is shared by the public operations and the trampolines.
// It never consults a current mapping to reinterpret an old entrance: the
// pointer, the slot and the record incarnation must all describe the same live
// role, otherwise the entrance is not current.
RecordStatus CheckEntrance(const detail::ServiceRecordState& state, std::size_t slot,
        RecordBytesCallback function, std::uint64_t incarnation, RecordRole role,
        RecordBytesCallback expected) noexcept
{
    if (function == nullptr || expected == nullptr || function != expected) return RecordStatus::NotFound;
    if (slot >= state.slots.size()) return RecordStatus::NotFound;
    const auto& cell = state.slots[slot];
    if (!cell.used || cell.incarnation != incarnation) return RecordStatus::StaleHandle;
    if (cell.phase == RecordPhase::Tombstoned) return RecordStatus::Retired;
    if (!cell.role_exposed[RoleIndex(role)]) return RecordStatus::StaleHandle;
    return RecordStatus::None;
}
} // namespace

ServiceRecordTable::ServiceRecordTable(detail::ServiceRecordState* s) noexcept : state_(s) {}
ServiceRecordTable::~ServiceRecordTable() noexcept = default;
ServiceRecordTable& ServiceRecordTable::Process() noexcept
{
    static ServiceRecordTable table(&ProcessState());
    return table;
}

RecordResult<RecordReservation> ServiceRecordTable::ReserveRecord(const std::string& service_name) noexcept
{
    if (!NameValid(service_name)) return Fail<RecordReservation>(RecordStatus::InvalidString);
    try {
        std::string stored(service_name); // May throw; no state is touched yet.
        std::lock_guard<std::mutex> lock(state_->gate);
        for (const auto& cell : state_->slots) {
            if (!cell.used || cell.name != service_name) continue;
            if (cell.phase == RecordPhase::ReservedUnexposed || cell.phase == RecordPhase::Exposed) {
                return Fail<RecordReservation>(RecordStatus::NameInUse);
            }
            if (cell.phase == RecordPhase::Unanchored) {
                // The record might still be alive and serving another owner:
                // never classify that by assumption and never overwrite it.
                return Fail<RecordReservation>(RecordStatus::ContinuityUnproven);
            }
            // Tombstoned: a proved destruction released the name.
        }
        std::size_t slot = no_index;
        for (std::size_t i = 0; i < state_->slots.size(); ++i) {
            if (!state_->slots[i].used) { slot = i; break; }
        }
        if (slot == no_index) return Fail<RecordReservation>(RecordStatus::SlotExhausted);
        if (state_->next_record_incarnation == std::numeric_limits<std::uint64_t>::max() ||
                state_->next_reservation == std::numeric_limits<std::uint64_t>::max()) {
            return Fail<RecordReservation>(RecordStatus::IdentityExhausted);
        }
        auto& cell = state_->slots[slot];
        cell = detail::RecordSlot{};
        cell.used = true;
        cell.phase = RecordPhase::ReservedUnexposed;
        cell.name = std::move(stored);
        cell.incarnation = state_->next_record_incarnation++;
        cell.reservation = state_->next_reservation++;
        return {RecordReservation(state_, slot, cell.reservation), RecordStatus::None};
    } catch (const std::bad_alloc&) {
        return Fail<RecordReservation>(RecordStatus::AllocationFailure);
    }
}

#ifdef MDDS_SERVICE_RECORD_TABLE_TEST_HOOKS
void ServiceRecordTable::TestSetNextIdentities(std::uint64_t record_incarnation,
        std::uint64_t reservation) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->next_record_incarnation = record_incarnation;
    state_->next_reservation = reservation;
}
void ServiceRecordTable::TestSetNextOwnerIncarnation(std::size_t slot, std::uint64_t value) noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    if (slot < state_->slots.size() && state_->slots[slot].used) {
        state_->slots[slot].next_owner_incarnation = value;
    }
}
#endif
RecordStatus ServiceRecordTable::AbandonUnexposed(const RecordReservation& reservation) noexcept
{
    if (reservation.state_ != state_ || reservation.slot_ >= state_->slots.size()) {
        return RecordStatus::NotFound;
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    // Only a reservation that never reached the SDK returns to FREE.
    const auto phase = state_->slots[reservation.slot_].phase;
    const bool same = state_->slots[reservation.slot_].used &&
            state_->slots[reservation.slot_].reservation == reservation.reservation_;
    if (!same) return RecordStatus::StaleHandle;
    if (phase != RecordPhase::ReservedUnexposed) return RecordStatus::NotUnexposed;
    state_->ReclaimReservation(reservation.slot_, reservation.reservation_);
    return RecordStatus::None;
}

RecordResult<RecordEntrance> ServiceRecordTable::Expose(const RecordReservation& reservation,
        RecordRole role, RecordIngressSink& sink) noexcept
{
    if (!RoleValid(role)) return Fail<RecordEntrance>(RecordStatus::InvalidConfiguration);
    if (reservation.state_ != state_ || reservation.slot_ >= state_->slots.size()) {
        return Fail<RecordEntrance>(RecordStatus::NotFound);
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    auto& cell = state_->slots[reservation.slot_];
    if (!cell.used || cell.reservation != reservation.reservation_) {
        return Fail<RecordEntrance>(RecordStatus::StaleHandle);
    }
    if (cell.phase == RecordPhase::Tombstoned) return Fail<RecordEntrance>(RecordStatus::Retired);
    const auto index = RoleIndex(role);
    // An already-issued role entrance is reported first: the caller retains it
    // and must not derive it again, whatever the record's continuity state is.
    if (cell.role_exposed[index]) return Fail<RecordEntrance>(RecordStatus::AlreadyExposed);
    if (cell.phase == RecordPhase::Unanchored) {
        return Fail<RecordEntrance>(RecordStatus::ContinuityUnproven);
    }
    if (cell.sink != nullptr && cell.sink != &sink) return Fail<RecordEntrance>(RecordStatus::SinkMismatch);
    // The slot is EXPOSED before this entrance can reach the SDK, so a later
    // registration failure can never reclaim it.
    const bool first_exposure = cell.phase == RecordPhase::ReservedUnexposed;
    cell.phase = RecordPhase::Exposed;
    cell.role_exposed[index] = true;
    cell.role_pending[index] = true;
    cell.sink = &sink;
    if (first_exposure) ++state_->usage.records_created;
    return {RecordEntrance(EntranceFor(reservation.slot_, role), cell.incarnation, reservation.slot_, role),
            RecordStatus::None};
}

RecordStatus ServiceRecordTable::AbandonExposure(const RecordEntrance& entrance) noexcept
{
    if (!RoleValid(entrance.role_)) return RecordStatus::InvalidConfiguration;
    if (entrance.slot_ >= state_->slots.size()) return RecordStatus::NotFound;
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto status = CheckEntrance(*state_, entrance.slot_, entrance.function_,
            entrance.record_incarnation_, entrance.role_, EntranceFor(entrance.slot_, entrance.role_));
    if (status != RecordStatus::None) return status;
    auto& cell = state_->slots[entrance.slot_];
    const auto index = RoleIndex(entrance.role_);
    if (!cell.role_pending[index]) return RecordStatus::ExposureSettled;
    cell.role_pending[index] = false;
    // An entrance handed to the SDK without a known owner leaves continuity
    // unproven: the SDK may still hold the record and its callback table.
    if (cell.phase == RecordPhase::Exposed && cell.Anchors() == 0) cell.phase = RecordPhase::Unanchored;
    return RecordStatus::None;
}

RecordResult<RecordMembership> ServiceRecordTable::RegisterOwner(const RecordEntrance& entrance,
        std::int32_t socket) noexcept
{
    if (!RoleValid(entrance.role_)) return Fail<RecordMembership>(RecordStatus::InvalidConfiguration);
    if (entrance.slot_ >= state_->slots.size()) return Fail<RecordMembership>(RecordStatus::NotFound);
    if (socket < 0) return Fail<RecordMembership>(RecordStatus::InvalidSocket);
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto status = CheckEntrance(*state_, entrance.slot_, entrance.function_,
            entrance.record_incarnation_, entrance.role_, EntranceFor(entrance.slot_, entrance.role_));
    if (status != RecordStatus::None) return Fail<RecordMembership>(status);
    auto& cell = state_->slots[entrance.slot_];
    if (cell.phase == RecordPhase::Unanchored) {
        // No new owner is accepted under an incarnation whose continuity is
        // unproven, and no replacement may overwrite a possibly live table.
        return Fail<RecordMembership>(RecordStatus::ContinuityUnproven);
    }
    if (cell.phase != RecordPhase::Exposed) return Fail<RecordMembership>(RecordStatus::StaleHandle);
    const auto existing = cell.FindOwner(socket);
    if (existing != no_index) {
        return Fail<RecordMembership>(cell.owners[existing].role == entrance.role_
                ? RecordStatus::AlreadyMember : RecordStatus::ForeignIdCollision);
    }
    const auto slot = cell.FreeOwner();
    if (slot == no_index) return Fail<RecordMembership>(RecordStatus::Capacity);
    if (cell.next_owner_incarnation == std::numeric_limits<std::uint64_t>::max()) {
        return Fail<RecordMembership>(RecordStatus::IdentityExhausted);
    }
    auto& owner = cell.owners[slot];
    owner.used = true;
    owner.socket = socket;
    owner.incarnation = cell.next_owner_incarnation++;
    owner.role = entrance.role_;
    ++cell.owner_count;
    cell.owner_high_water = std::max(cell.owner_high_water, cell.owner_count);
    state_->owner_high_water = std::max(state_->owner_high_water, cell.owner_count);
    ++state_->usage.owners;
    // The first owner consumes this role's pending exposure anchor.
    cell.role_pending[RoleIndex(entrance.role_)] = false;
    return {RecordMembership(state_, entrance.slot_, cell.incarnation, owner.incarnation, socket,
            entrance.role_), RecordStatus::None};
}

RecordStatus ServiceRecordTable::ReleaseOwner(const RecordMembership& membership) noexcept
{
    if (!RoleValid(membership.role_)) return RecordStatus::InvalidConfiguration;
    if (membership.state_ != state_ || membership.slot_ >= state_->slots.size()) {
        return RecordStatus::NotFound;
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    // Releasing one owner never retires a table that still serves a sibling.
    const auto& cell = state_->slots[membership.slot_];
    if (!cell.used || cell.incarnation != membership.record_incarnation_) return RecordStatus::StaleHandle;
    const auto index = cell.FindOwner(membership.socket_, membership.role_);
    if (index == no_index || cell.owners[index].incarnation != membership.membership_incarnation_) {
        return RecordStatus::StaleHandle;
    }
    state_->ReleaseMembership(membership.slot_, membership.record_incarnation_,
            membership.membership_incarnation_, membership.socket_, membership.role_);
    return RecordStatus::None;
}

RecordResult<RecordAttribution> ServiceRecordTable::Attribute(const RecordEntrance& entrance,
        std::int32_t socket) noexcept
{
    if (!RoleValid(entrance.role_)) return Fail<RecordAttribution>(RecordStatus::InvalidConfiguration);
    if (entrance.slot_ >= state_->slots.size()) return Fail<RecordAttribution>(RecordStatus::NotFound);
    if (socket < 0) return Fail<RecordAttribution>(RecordStatus::InvalidSocket);
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto status = CheckEntrance(*state_, entrance.slot_, entrance.function_,
            entrance.record_incarnation_, entrance.role_, EntranceFor(entrance.slot_, entrance.role_));
    if (status != RecordStatus::None) return Fail<RecordAttribution>(status);
    auto& cell = state_->slots[entrance.slot_];
    if (cell.phase == RecordPhase::Unanchored) return Fail<RecordAttribution>(RecordStatus::ContinuityUnproven);
    if (cell.phase != RecordPhase::Exposed) return Fail<RecordAttribution>(RecordStatus::StaleHandle);
    const auto owner = cell.FindOwner(socket, entrance.role_);
    if (owner == no_index) return Fail<RecordAttribution>(RecordStatus::UnknownSocket);
    return {RecordAttribution(state_, entrance.slot_, cell.incarnation, cell.owners[owner].incarnation,
            socket, entrance.role_), RecordStatus::None};
}

RecordStatus ServiceRecordTable::ConfirmDestroyed(const RecordEntrance& entrance) noexcept
{
    if (!RoleValid(entrance.role_)) return RecordStatus::InvalidConfiguration;
    if (entrance.slot_ >= state_->slots.size()) return RecordStatus::NotFound;
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto status = CheckEntrance(*state_, entrance.slot_, entrance.function_,
            entrance.record_incarnation_, entrance.role_, EntranceFor(entrance.slot_, entrance.role_));
    if (status != RecordStatus::None) return status;
    auto& cell = state_->slots[entrance.slot_];
    // A still-anchored table is never retired by assumption.
    if (cell.phase != RecordPhase::Unanchored) return RecordStatus::StillAnchored;
    // Both role incarnations end here, the entrances become permanent
    // tombstones, and the name seal is released so a replacement can be created
    // on a new slot with a new incarnation and new entrances.
    cell.phase = RecordPhase::Tombstoned;
    cell.sink = nullptr;
    for (std::size_t i = 0; i < role_count; ++i) cell.role_pending[i] = false;
    return RecordStatus::None;
}

ServiceRecordObservation ServiceRecordTable::Observe(const RecordEntrance& entrance) const noexcept
{
    ServiceRecordObservation view;
    if (!RoleValid(entrance.role_)) { view.status = RecordStatus::InvalidConfiguration; return view; }
    if (entrance.slot_ >= state_->slots.size()) {
        view.status = RecordStatus::NotFound;
        return view;
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto& cell = state_->slots[entrance.slot_];
    if (!cell.used || cell.incarnation != entrance.record_incarnation_) {
        view.status = RecordStatus::StaleHandle;
        return view;
    }
    view.status = cell.phase == RecordPhase::Tombstoned ? RecordStatus::Retired : RecordStatus::None;
    view.phase = cell.phase;
    view.slot = entrance.slot_;
    view.record_incarnation = cell.incarnation;
    view.owners = cell.owner_count;
    view.owner_high_water = cell.owner_high_water;
    view.sink_attached = cell.sink != nullptr;
    view.name_sealed = cell.Sealed();
    view.callbacks_in_flight = cell.sink_calls_in_flight;
    for (std::size_t i = 0; i < role_count; ++i) {
        view.role_exposed[i] = cell.role_exposed[i];
        view.role_pending[i] = cell.role_pending[i];
    }
    for (const auto& owner : cell.owners) {
        if (owner.used) ++view.role_owners[RoleIndex(owner.role)];
    }
    return view;
}

ServiceRecordUsage ServiceRecordTable::SnapshotUsage() const noexcept
{
    std::lock_guard<std::mutex> lock(state_->gate);
    auto usage = state_->usage;
    usage.owner_high_water = state_->owner_high_water;
    usage.callbacks_in_flight = state_->callbacks_in_flight;
    for (const auto& cell : state_->slots) {
        switch (cell.phase) {
        case RecordPhase::Free: if (!cell.used) ++usage.slots_free; break;
        case RecordPhase::ReservedUnexposed: ++usage.slots_reserved; break;
        case RecordPhase::Exposed: ++usage.slots_exposed; break;
        case RecordPhase::Unanchored: ++usage.slots_unanchored; break;
        case RecordPhase::Tombstoned: ++usage.slots_tombstoned; break;
        }
    }
    return usage;
}

bool RecordAttribution::Current() const noexcept
{
    if (state_ == nullptr || slot_ >= state_->slots.size()) return false;
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto& cell = state_->slots[slot_];
    if (!cell.used || cell.incarnation != record_incarnation_ || cell.phase == RecordPhase::Tombstoned) {
        return false;
    }
    const auto owner = cell.FindOwner(socket_, role_);
    return owner != no_index && cell.owners[owner].incarnation == membership_incarnation_;
}

ServiceRecordObservation RecordAttribution::Observe() const noexcept
{
    ServiceRecordObservation view;
    if (state_ == nullptr || slot_ >= state_->slots.size()) {
        view.status = RecordStatus::NotFound;
        return view;
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    const auto& cell = state_->slots[slot_];
    if (!cell.used || cell.incarnation != record_incarnation_) {
        view.status = RecordStatus::StaleHandle;
        return view;
    }
    // Always the original record: an old capture never follows a replacement.
    view.status = cell.phase == RecordPhase::Tombstoned ? RecordStatus::Retired : RecordStatus::None;
    view.phase = cell.phase;
    view.slot = slot_;
    view.record_incarnation = cell.incarnation;
    view.owners = cell.owner_count;
    view.owner_high_water = cell.owner_high_water;
    view.sink_attached = cell.sink != nullptr;
    view.name_sealed = cell.Sealed();
    view.callbacks_in_flight = cell.sink_calls_in_flight;
    for (std::size_t i = 0; i < role_count; ++i) {
        view.role_exposed[i] = cell.role_exposed[i];
        view.role_pending[i] = cell.role_pending[i];
    }
    for (const auto& owner : cell.owners) {
        if (owner.used) ++view.role_owners[RoleIndex(owner.role)];
    }
    return view;
}

RecordReservation::RecordReservation(detail::ServiceRecordState* s, std::size_t n,
        std::uint64_t r) noexcept : state_(s), slot_(n), reservation_(r) {}
RecordReservation::~RecordReservation() noexcept { Reclaim(); }
RecordReservation::RecordReservation(RecordReservation&& other) noexcept
    : state_(other.state_), slot_(other.slot_), reservation_(other.reservation_)
{
    other.state_ = nullptr; other.slot_ = invalid_record_slot; other.reservation_ = 0;
}
RecordReservation& RecordReservation::operator=(RecordReservation&& other) noexcept
{
    if (this != &other) {
        Reclaim(); // The previous slot is released before this handle adopts another.
        state_ = other.state_; slot_ = other.slot_; reservation_ = other.reservation_;
        other.state_ = nullptr; other.slot_ = invalid_record_slot; other.reservation_ = 0;
    }
    return *this;
}
void RecordReservation::Reclaim() noexcept
{
    if (state_ == nullptr || slot_ == invalid_record_slot) return;
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->ReclaimReservation(slot_, reservation_);
}
RecordEntrance::RecordEntrance(RecordBytesCallback function, std::uint64_t incarnation, std::size_t slot,
        RecordRole role) noexcept : function_(function), record_incarnation_(incarnation), slot_(slot),
        role_(role) {}
RecordMembership::RecordMembership(detail::ServiceRecordState* s, std::size_t slot,
        std::uint64_t record_incarnation, std::uint64_t membership_incarnation, std::int32_t socket,
        RecordRole role) noexcept : state_(s), slot_(slot), record_incarnation_(record_incarnation),
        membership_incarnation_(membership_incarnation), socket_(socket), role_(role) {}
RecordMembership::~RecordMembership() noexcept { Release(); }
RecordMembership::RecordMembership(RecordMembership&& other) noexcept
    : state_(other.state_), slot_(other.slot_), record_incarnation_(other.record_incarnation_),
      membership_incarnation_(other.membership_incarnation_), socket_(other.socket_), role_(other.role_)
{
    other.state_ = nullptr; other.slot_ = invalid_record_slot; other.membership_incarnation_ = 0;
}
RecordMembership& RecordMembership::operator=(RecordMembership&& other) noexcept
{
    if (this != &other) {
        Release(); // The previous association is released before this handle adopts another.
        state_ = other.state_; slot_ = other.slot_; record_incarnation_ = other.record_incarnation_;
        membership_incarnation_ = other.membership_incarnation_; socket_ = other.socket_;
        role_ = other.role_;
        other.state_ = nullptr; other.slot_ = invalid_record_slot; other.membership_incarnation_ = 0;
    }
    return *this;
}
void RecordMembership::Release() noexcept
{
    if (state_ == nullptr || slot_ == invalid_record_slot) return;
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->ReleaseMembership(slot_, record_incarnation_, membership_incarnation_, socket_, role_);
}
RecordAttribution::RecordAttribution(detail::ServiceRecordState* s, std::size_t slot,
        std::uint64_t record_incarnation, std::uint64_t membership_incarnation, std::int32_t socket,
        RecordRole role) noexcept : state_(s), slot_(slot), record_incarnation_(record_incarnation),
        membership_incarnation_(membership_incarnation), socket_(socket), role_(role) {}

template<std::size_t Index>
void ServiceRecordTable::Trampoline(std::int32_t socket, const void* data, std::uint32_t size) noexcept
{
    Dispatch(Index / role_count, static_cast<RecordRole>(Index % role_count), socket, data, size);
}

template<std::size_t... Index>
std::array<RecordBytesCallback, sizeof...(Index)>
ServiceRecordTable::MakeEntrances(std::index_sequence<Index...>) noexcept
{
    return {{&ServiceRecordTable::Trampoline<Index>...}};
}

RecordBytesCallback ServiceRecordTable::EntranceFor(std::size_t slot, RecordRole role) noexcept
{
    static const std::array<RecordBytesCallback, record_slot_capacity * role_count> entrances =
            MakeEntrances(std::make_index_sequence<record_slot_capacity * role_count>{});
    if (slot >= record_slot_capacity || !RoleValid(role)) return nullptr;
    return entrances[slot * role_count + RoleIndex(role)];
}

void ServiceRecordTable::Dispatch(std::size_t slot, RecordRole role, std::int32_t socket,
        const void* data, std::uint32_t size) noexcept
{
    auto state = Process().state_;
    RecordAttribution attribution;
    RecordIngressSink* sink = nullptr;
    try {
        std::lock_guard<std::mutex> lock(state->gate);
        auto& usage = state->usage;
        if (slot >= state->slots.size() || !RoleValid(role)) { ++usage.dropped_invalid; return; }
        auto& cell = state->slots[slot];
        if (!cell.used || cell.phase == RecordPhase::Tombstoned) { ++usage.dropped_retired; return; }
        if (socket < 0 || data == nullptr || size == 0) { ++usage.dropped_invalid; return; }
        if (cell.phase == RecordPhase::Unanchored) { ++usage.dropped_unanchored; return; }
        const auto index = RoleIndex(role);
        if (cell.phase != RecordPhase::Exposed || !cell.role_exposed[index] || cell.sink == nullptr) {
            ++usage.dropped_retired;
            return;
        }
        const auto owner = cell.FindOwner(socket, role);
        if (owner == no_index) { ++usage.dropped_unknown_socket; return; }
        // Attribution linearization: the record incarnation, role, membership
        // incarnation and socket are captured once, under this gate.
        attribution = RecordAttribution(state, slot, cell.incarnation, cell.owners[owner].incarnation,
                socket, role);
        sink = cell.sink;
        ++cell.sink_calls_in_flight;
        ++state->callbacks_in_flight;
        ++usage.attributed;
    } catch (...) {
        // A public callback barrier: nothing may escape into the SDK.
        return;
    }
    // Outside the gate: borrowed bytes, no current-state lookup, and both
    // reentrancy and a throwing sink are accounted instead of escaping.
    try {
        sink->OnAttributedBytes(attribution, socket, data, size);
    } catch (...) {
        std::lock_guard<std::mutex> lock(state->gate);
        ++state->usage.callback_exceptions;
    }
    // The record slot is never recycled after exposure, so this cell identity is
    // still the original one even if the record was tombstoned meanwhile.
    {
        std::lock_guard<std::mutex> lock(state->gate);
        auto& cell = state->slots[slot];
        --cell.sink_calls_in_flight;
        --state->callbacks_in_flight;
    }
}
} // namespace mdds::dsoftbus
