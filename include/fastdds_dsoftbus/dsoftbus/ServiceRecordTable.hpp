#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_SERVICE_RECORD_TABLE_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_SERVICE_RECORD_TABLE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
namespace detail { struct ServiceRecordState; }
namespace phase0 { class PublicSocketBackend; }
class ServiceRecordTable;

// Exact public DSoftBus byte-callback shape (communication_dsoftbus
// interfaces/kits/transport/socket.h, ISocketListener::OnBytes). An entrance
// dispatches precisely these three public arguments. No numeric generation,
// context pointer, Participant epoch or native event token exists in this
// interface, and none is invented to fill the gap.
using RecordBytesCallback = void (*)(std::int32_t socket, const void* data, std::uint32_t size);

inline constexpr std::size_t invalid_record_slot = static_cast<std::size_t>(-1);

// A live service record owns two distinct role fields. They are NOT two
// listeners: the invariant "one active Participant, one DSoftBus listener" is
// unchanged. Client is the field registered by Bind (dial side); Server is the
// field registered by Listen, which also serves sockets accepted through it.
enum class RecordRole { Client, Server, Count };

// FREE -> RESERVED_UNEXPOSED -> EXPOSED -> TOMBSTONED. UNANCHORED is a sub-state
// of EXPOSED (zero anchors, continuity unproven): the slot stays non-reclaimable
// and its entrance stays non-reusable, and it additionally seals the service
// name until destruction is proved. A slot that was ever exposed never returns
// to FREE; only a reservation that was never exposed can be abandoned.
enum class RecordPhase { Free, ReservedUnexposed, Exposed, Unanchored, Tombstoned };

// NotImplemented is the default value of every result. No implemented rule
// produces it, so a result that was never set can never be read as success.
enum class RecordStatus
{
    None, NotImplemented, InvalidConfiguration, InvalidString, InvalidSocket,
    SlotExhausted, NameInUse, ContinuityUnproven, NotFound, StaleHandle,
    NotUnexposed, AlreadyExposed, AlreadyMember, ForeignIdCollision, Capacity,
    UnknownSocket, Retired, StillAnchored, ExposureSettled, SinkMismatch,
    IdentityExhausted, AllocationFailure
};

// Bounded model bound, not a product or SDK limit: the SDK publishes no such
// number, and the adapter's fixed entrance pool is a compile-time resource.
// Deliberate consequence: a process that keeps destroying records instead of
// preserving an anchor reaches this bound and is then refused new incarnations.
// That is the intended fail-closed behavior, not an opportunity to recycle a
// tombstoned entrance.
inline constexpr std::size_t record_slot_capacity = 8;

// Owner slots under one record across both roles. This is this model's own
// bound, restated from the native owner table's default budget because this
// model deliberately links neither that table nor Fast DDS; it is not a shared
// runtime value, not a cumulative Participant or connection quota, and it
// grants no additional connection authority.
inline constexpr std::size_t record_owner_capacity = 12;
// Service name bound, restating the directory's namespace/name rule for the
// same reason: the directory remains the owner of address and name validation
// and this model validates no address. No shared constant links the two copies,
// so a change to the rule has to be mirrored here deliberately.
inline constexpr std::size_t record_max_service_name_bytes = 255;

class RecordReservation;
class RecordEntrance;
class RecordMembership;
class RecordAttribution;

template<class T> struct RecordResult
{
    std::optional<T> value;
    RecordStatus status = RecordStatus::NotImplemented;
    explicit operator bool() const noexcept { return value.has_value() && status == RecordStatus::None; }
};

// Diagnostic views only. No field authorizes a lookup, registration, delivery,
// native call or SDK generation, and none is a continuity proof. An observation
// deliberately reports state, never an invocable entrance: the caller retains
// the entrance it received from Expose, so no diagnostic path can name a live
// callback pointer for a role the caller never exposed.
struct ServiceRecordObservation
{
    RecordStatus status = RecordStatus::NotFound;
    RecordPhase phase = RecordPhase::Free;
    std::size_t slot = invalid_record_slot;
    std::uint64_t record_incarnation = 0;
    std::size_t owners = 0, owner_high_water = 0;
    std::size_t role_owners[static_cast<std::size_t>(RecordRole::Count)]{};
    bool role_exposed[static_cast<std::size_t>(RecordRole::Count)]{};
    bool role_pending[static_cast<std::size_t>(RecordRole::Count)]{};
    bool sink_attached = false;
    bool name_sealed = false;
    // Callbacks already attributed under this record whose sink call has not
    // returned. A captured callback is never retracted, so after
    // ConfirmDestroyed the caller must not free its sink while this is nonzero.
    std::size_t callbacks_in_flight = 0;
};

struct ServiceRecordUsage
{
    std::size_t slots_free = 0, slots_reserved = 0, slots_exposed = 0;
    std::size_t slots_unanchored = 0, slots_tombstoned = 0;
    std::size_t owners = 0, owner_high_water = 0, records_created = 0;
    // Callbacks attributed and not yet returned from the sink, process wide.
    std::size_t callbacks_in_flight = 0;
    // Trampoline-path dispositions. Every callback is accounted exactly once.
    // dropped_retired means the entrance is not current for that live role:
    // a tombstoned record, or a role whose entrance was never issued.
    std::size_t attributed = 0;
    std::size_t dropped_retired = 0, dropped_unanchored = 0;
    std::size_t dropped_unknown_socket = 0;
    std::size_t dropped_invalid = 0, callback_exceptions = 0;
};

// One held slot that has not been handed to the SDK. Move-only: it is the
// caller's unique pending record, and its lifetime spans both role exposures.
// Destroying a reservation that never reached the SDK returns its slot to FREE,
// so dropping a local cannot strand a slot in RESERVED_UNEXPOSED and seal the
// service name forever; a reservation whose slot was already exposed releases
// nothing. Move-assignment releases the previous slot first. Explicit
// AbandonUnexposed remains the reporting form of the same rule.
class RecordReservation final
{
public:
    RecordReservation() noexcept = default;
    ~RecordReservation() noexcept;
    RecordReservation(RecordReservation&&) noexcept;
    RecordReservation& operator=(RecordReservation&&) noexcept;
    RecordReservation(const RecordReservation&) = delete;
    RecordReservation& operator=(const RecordReservation&) = delete;
    explicit operator bool() const noexcept { return state_ != nullptr && slot_ != invalid_record_slot; }
private:
    friend class ServiceRecordTable;
    // The composition layer keys its record-scoped dispatch state by the record
    // slot. A slot that was ever exposed never returns to FREE and its entrance
    // is never reissued, so the key is stable for the record's whole life; the
    // slot is not otherwise part of the public identity of a handle.
    friend class ServiceRecordBinding;
    RecordReservation(detail::ServiceRecordState*, std::size_t, std::uint64_t) noexcept;
    void Reclaim() noexcept;
    detail::ServiceRecordState* state_ = nullptr;
    std::size_t slot_ = invalid_record_slot;
    std::uint64_t reservation_ = 0;
};

// The fixed callback entrance of one live (record, role). Copies pin the
// original record incarnation and never authorize a different record. The
// carried function pointer is exactly what the adapter hands to the SDK, and
// the identical pointer serves every registration into that live role.
class RecordEntrance final
{
public:
    RecordEntrance() noexcept = default;
    explicit operator bool() const noexcept { return function_ != nullptr; }
    RecordBytesCallback function() const noexcept { return function_; }
    RecordRole role() const noexcept { return role_; }
    bool operator==(const RecordEntrance& o) const noexcept
    { return function_ == o.function_ && record_incarnation_ == o.record_incarnation_; }
    bool operator!=(const RecordEntrance& o) const noexcept { return !(*this == o); }
private:
    friend class ServiceRecordTable;
    // The thin public-API shim derives its fixed bridge index from this original
    // record/role slot. It neither allocates another identity nor writes this one.
    friend class phase0::PublicSocketBackend;
    // Slot access for the composition layer's record-scoped dispatch key; see
    // RecordReservation.
    friend class ServiceRecordBinding;
    RecordEntrance(RecordBytesCallback, std::uint64_t, std::size_t, RecordRole) noexcept;
    RecordBytesCallback function_ = nullptr;
    std::uint64_t record_incarnation_ = 0;
    std::size_t slot_ = invalid_record_slot;
    RecordRole role_ = RecordRole::Client;
};

// One socket's registration under one live (record, role). Move-only: the owner
// releases it exactly once, so a copy could double-release. The handle owns the
// association: destroying it, or move-assigning over it, releases that socket
// exactly like ReleaseOwner: the caller therefore retains one handle per
// registered socket for as long as that socket stays registered. Without that,
// a dropped handle would strand an owner that could never be released, pinning
// the record below UNANCHORED and making it permanently undestroyable. A stale
// handle releases nothing.
class RecordMembership final
{
public:
    RecordMembership() noexcept = default;
    ~RecordMembership() noexcept;
    RecordMembership(RecordMembership&&) noexcept;
    RecordMembership& operator=(RecordMembership&&) noexcept;
    RecordMembership(const RecordMembership&) = delete;
    RecordMembership& operator=(const RecordMembership&) = delete;
    explicit operator bool() const noexcept { return state_ != nullptr && slot_ != invalid_record_slot; }
    std::int32_t socket() const noexcept { return socket_; }
    RecordRole role() const noexcept { return role_; }
private:
    friend class ServiceRecordTable;
    RecordMembership(detail::ServiceRecordState*, std::size_t, std::uint64_t, std::uint64_t,
            std::int32_t, RecordRole) noexcept;
    void Release() noexcept;
    detail::ServiceRecordState* state_ = nullptr;
    std::size_t slot_ = invalid_record_slot;
    std::uint64_t record_incarnation_ = 0, membership_incarnation_ = 0;
    std::int32_t socket_ = -1;
    RecordRole role_ = RecordRole::Client;
};

// The adapter's single attribution linearization point, captured once under the
// table gate as (record incarnation, role, membership incarnation, socket).
// Move-only and purely observational: it owns no association, so destroying it
// releases nothing. Delivery never consults the current table: an old pin
// finishes only with its original attribution and is never re-resolved onto a
// replacement owner, role or record.
class RecordAttribution final
{
public:
    RecordAttribution() noexcept = default;
    ~RecordAttribution() noexcept = default;
    RecordAttribution(RecordAttribution&&) noexcept = default;
    RecordAttribution& operator=(RecordAttribution&&) noexcept = default;
    RecordAttribution(const RecordAttribution&) = delete;
    RecordAttribution& operator=(const RecordAttribution&) = delete;
    explicit operator bool() const noexcept { return state_ != nullptr; }
    RecordRole role() const noexcept { return role_; }
    std::int32_t socket() const noexcept { return socket_; }
    // Whether that original membership is still current. An observation, never
    // an execution permission and never a re-resolution.
    bool Current() const noexcept;
    // Reports the original record's state. It never names an invocable
    // entrance, so an old capture cannot become a callback pointer either.
    ServiceRecordObservation Observe() const noexcept;
private:
    friend class ServiceRecordTable;
    RecordAttribution(detail::ServiceRecordState*, std::size_t, std::uint64_t,
            std::uint64_t, std::int32_t, RecordRole) noexcept;
    detail::ServiceRecordState* state_ = nullptr;
    std::size_t slot_ = invalid_record_slot;
    std::uint64_t record_incarnation_ = 0, membership_incarnation_ = 0;
    std::int32_t socket_ = -1;
    RecordRole role_ = RecordRole::Client;
};

// Caller-owned ingress destination of one record. It is invoked outside the
// table gate, with borrowed callback bytes that are valid only for the call,
// and only for a successful attribution under a live anchored (record, role).
// The sink may reenter the table (for example ReleaseOwner) and must not assume
// any table lock is held. It may not re-resolve the attribution into current
// state; whatever copy or lease policy it applies afterwards belongs to it
// (bounded payload ownership stays with the application/receiver layer).
// It is deliberately not noexcept: the entrance's exception barrier absorbs and
// accounts a throwing sink, so nothing propagates back into the SDK callback.
// The caller owns the sink and keeps it alive while the record is exposed AND
// until every callback it started has returned: confirming destruction clears
// the record's sink binding but never retracts an already-attributed callback.
class RecordIngressSink
{
public:
    virtual ~RecordIngressSink() = default;
    virtual void OnAttributedBytes(const RecordAttribution&, std::int32_t socket,
            const void* data, std::size_t size) = 0;
};

// A bounded model core of the SDK client-process shared callback table, NOT an
// SDK backend: it makes no SDK call, starts no thread, and observes no record
// creation or destruction. One table exists per process because the adapter's
// fixed entrance pool cannot be reset; a service name selects which record a
// slot currently represents. Caller keeps the sink alive while exposed.
class ServiceRecordTable final
{
public:
    static ServiceRecordTable& Process() noexcept;
    ServiceRecordTable(const ServiceRecordTable&) = delete;
    ServiceRecordTable& operator=(const ServiceRecordTable&) = delete;
    ServiceRecordTable(ServiceRecordTable&&) = delete;
    ServiceRecordTable& operator=(ServiceRecordTable&&) = delete;
    ~ServiceRecordTable() noexcept;

    // FREE -> RESERVED_UNEXPOSED. Refused by NameInUse while another slot holds
    // the same name, and by ContinuityUnproven while an unanchored record of
    // that name might still be alive. Slot exhaustion is decided here, before
    // any entrance can reach the SDK.
    RecordResult<RecordReservation> ReserveRecord(const std::string& service_name) noexcept;
    // Only a never-exposed reservation returns to FREE.
    RecordStatus AbandonUnexposed(const RecordReservation&) noexcept;

    // Binds the role's fixed entrance and, for the first role, the sink. The
    // slot becomes EXPOSED before the entrance can reach the SDK, so a later
    // registration failure cannot reclaim it. A second call for the same role
    // is refused: the caller retains the first entrance instead of deriving it
    // again.
    RecordResult<RecordEntrance> Expose(const RecordReservation&, RecordRole,
            RecordIngressSink& sink) noexcept;
    // Reports that the registration attempt through this entrance did not
    // produce an owner. The pending anchor is dropped and the slot is never
    // reclaimed; if it was the record's last anchor the record becomes
    // UNANCHORED, because the SDK may hold the entrance without a known owner.
    RecordStatus AbandonExposure(const RecordEntrance&) noexcept;
    // Ends the caller's sink binding for this record without retiring the record
    // and without reclaiming a slot. Refused as StillAnchored while the record
    // still has any anchor, so it cannot withdraw the sink from a live record and
    // it cannot be undone: a record that reaches UNANCHORED never accepts a new
    // owner, so there is no re-registration to make. After it returns None no new
    // callback can be captured for this record, so a later attribution never
    // reaches that sink again; a callback already captured under the gate is never
    // retracted, and the caller must therefore still keep its sink object alive
    // until the in-flight count Observe() reports has reached zero. The sink
    // object itself may outlive the record: this call only ends its registration.
    RecordStatus ReleaseSink(const RecordEntrance&, RecordIngressSink& sink) noexcept;

    // Registers one socket under the live role of this entrance. Repeated
    // registration on a confirmed live table consumes no new slot and never
    // changes the incarnation or the entrance.
    RecordResult<RecordMembership> RegisterOwner(const RecordEntrance&,
            std::int32_t socket) noexcept;
    RecordStatus ReleaseOwner(const RecordMembership&) noexcept;

    // The ingress attribution gate. Successful only under a live anchored
    // (record, role) whose current membership carries this socket; every other
    // outcome is terminal and is never queued for a later record.
    RecordResult<RecordAttribution> Attribute(const RecordEntrance&,
            std::int32_t socket) noexcept;

    // Ends both role incarnations of an UNANCHORED record: permanent entrance
    // tombstones plus release of the name seal, so a same-name replacement can
    // receive a new slot, new incarnation and new entrances. Refused while any
    // anchor remains: a still-live table is never retired by assumption.
    RecordStatus ConfirmDestroyed(const RecordEntrance&) noexcept;

    // Reports the original record's state. It never names an invocable
    // entrance: the caller retains the entrance it received from Expose.
    ServiceRecordObservation Observe(const RecordEntrance&) const noexcept;
    ServiceRecordUsage SnapshotUsage() const noexcept;
private:
    explicit ServiceRecordTable(detail::ServiceRecordState*) noexcept;
    // Fixed process-lifetime entrance pool: Trampoline<Index> is the exact
    // pointer handed to the SDK for (slot, role) = (Index / 2, Index % 2).
    template<std::size_t Index>
    static void Trampoline(std::int32_t socket, const void* data, std::uint32_t size) noexcept;
    template<std::size_t... Index>
    static std::array<RecordBytesCallback, sizeof...(Index)>
    MakeEntrances(std::index_sequence<Index...>) noexcept;
    static RecordBytesCallback EntranceFor(std::size_t slot, RecordRole role) noexcept;
    static void Dispatch(std::size_t slot, RecordRole role, std::int32_t socket,
            const void* data, std::uint32_t size) noexcept;
    // Test-only injection of the bounded counter conditions, compiled only into
    // builds that define FASTDDS_DSOFTBUS_SERVICE_RECORD_TABLE_TEST_HOOKS (this library and
    // its test target). A production consumer never sees these declarations, so
    // no same-named class can reach private state. Single threaded use only.
#ifdef FASTDDS_DSOFTBUS_SERVICE_RECORD_TABLE_TEST_HOOKS
    friend class ServiceRecordTableTestAccess;
    void TestSetNextIdentities(std::uint64_t record_incarnation, std::uint64_t reservation) noexcept;
    void TestSetNextOwnerIncarnation(std::size_t slot, std::uint64_t value) noexcept;
#endif
    // The record space has static storage duration and is never destroyed or
    // reallocated before process exit, so handles point at it directly.
    detail::ServiceRecordState* state_ = nullptr;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
