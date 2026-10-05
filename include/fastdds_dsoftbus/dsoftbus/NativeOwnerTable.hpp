#ifndef FASTDDS_DSOFTBUS_DSOFTBUS_NATIVE_OWNER_TABLE_HPP
#define FASTDDS_DSOFTBUS_DSOFTBUS_NATIVE_OWNER_TABLE_HPP

#include <fastdds_dsoftbus/dsoftbus/RuntimeSendPath.hpp>
#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace fastdds_dsoftbus::dsoftbus {
class NativeOwnerTable;
class NativeOwnerTableTestAccess;
class NamespaceLease;
class NativeOwnerLease;
class NativeCallbackLease;
class SocketCallTicket;
class ListenCallTicket;
class BindCallTicket;
class ShutdownCallTicket;
namespace detail { struct NativeState; class NativeOperationToken; class NativeSendCallTicket; class NativeEgressBridge; }
namespace detail { class NativeReceiveBridge; class NativeReceiveGuard; }

using NativeSocketId = std::int32_t;
enum class RouteDirection { Dial, Accept, Both };
struct NativeLimits
{
    // Socket/Listen/Bind share this finite pool. Default remains the original
    // single setup call; two declared endpoints may explicitly reserve two.
    static constexpr std::size_t setup_call_capacity = 2;
    std::size_t namespaces = 8, total_owners = 12;
    std::size_t setup_calls = 1, send_calls = 2, close_calls = 1;
};
struct NativeTableConfiguration
{
    DirectoryConfiguration directory;
    std::vector<RouteDirection> directions;
    // Ordinary native-name mode only: 1..64 bytes, no embedded NUL.
    std::string service_pkg_name;
    NativeLimits limits;
    // Explicit reconnect policy; only Both routes. Retained old owners/IDs
    // remain charged and never retarget old callbacks to the replacement.
    bool allow_closed_dial_retry=false;
    // Fixed directed-lane profile: accepted may receive but cannot be selected
    // as a replacement TX owner. Default Both fallback behavior is unchanged.
    bool dial_only_egress=false;
    bool bounded_reaccept_host=false;
};
enum class NativeStatus
{
    None, NotImplemented, AllocationFailure, InvalidConfiguration, InvalidLimits,
    InvalidDirection, InvalidString, UnsupportedSpecialSocketName,
    DirectoryRejected, CreateKeyConflict, CallbackNamespaceConflict,
    ConfigurationMismatch, NotFound, InvalidHandle, ForeignState, StaleHandle,
    ExistingPending, OwnerUnavailable, WrongRole, WrongState, ServiceClosing,
    SetupBusy, SendBusy, CloseBusy, InvalidResult, AlreadyEntered, NotEntered,
    AlreadySettled, Abandoned, Revoked, EnteredPending, AwaitSocketReturn,
    NoOwnedId, ShutdownAlreadyObserved, ShutdownAlreadyEntered, ShutdownAlreadyReturned,
    ForeignIdCollision, AdmissionSaturated, UnknownPeer, UnsupportedDataType,
    DirectionMismatch, DuplicateConnection, UnknownSocket, NotReady, StaleVersion,
    StaleGeneration, IdentityExhausted, CounterExhausted, WrongOperationKind,
    DeadlineExpired, ProviderContractFault
};
enum class ConfigIndexKind { None, Local, Route };
struct ConfigIndex
{
    ConfigIndexKind kind = ConfigIndexKind::None;
    std::size_t index = static_cast<std::size_t>(-1);
};
struct NativeIssue
{
    NativeStatus code = NativeStatus::None;
    ConfigIndex first{}, second{};
    DirectoryIssue directory_issue{};
};
template<class T> struct NativeResult
{
    std::optional<T> value;
    NativeIssue issue{};
    explicit operator bool() const noexcept { return value.has_value() && issue.code == NativeStatus::None; }
};
enum class NativeOwnerRole { Listener, Dial, Accepted, Rejected };
enum class NativeOwnerPhase
{
    Reserved, SocketKnown, Listening, Binding, Ready, ClosePending,
    RetainedNotQuiescent, FailedRetained, Count
};
enum class NativeCallKind { Socket, Listen, Bind, Send, Shutdown };
enum class NativeQuiescence { Unproven };
struct NativeCallUsage { std::size_t reserved = 0, entered = 0, limit = 0; };
struct NativeUsageSnapshot
{
    std::size_t namespaces = 0, owners = 0, owned_ids = 0, callbacks = 0, receives = 0;
    std::size_t owner_high_water = 0;
    std::uint64_t generation_high_water = 0;
    std::array<std::size_t, static_cast<std::size_t>(NativeOwnerPhase::Count)> phases{};
    NativeCallUsage setup{}, send{}, close{};
    NativeLimits limits{};
    bool admission_sealed = false, entered_context_lost = false;
    NativeStatus seal_reason = NativeStatus::None;
};
enum class NativeProviderFault { None, InvalidPositiveReturn, ExceptionUnwind };
struct NativeOwnerObservation
{
    NativeStatus status = NativeStatus::InvalidHandle;
    NativeOwnerRole role = NativeOwnerRole::Listener;
    NativeOwnerPhase phase = NativeOwnerPhase::Reserved;
    std::size_t slot = static_cast<std::size_t>(-1);
    std::uint64_t incarnation = 0, generation = 0, route_version = 0;
    std::optional<NativeSocketId> owned_id;
    bool close_intent = false, socket_entered = false, socket_returned = false;
    bool shutdown_reserved = false, shutdown_entered = false, shutdown_returned = false;
    bool on_shutdown_seen = false, entered_context_lost = false;
    std::size_t callback_inflight = 0;
    std::size_t receive_inflight = 0;
    std::int32_t error_code = 0;
    NativeStatus failure = NativeStatus::None;
    NativeQuiescence quiescence = NativeQuiescence::Unproven;
    NativeProviderFault provider_fault = NativeProviderFault::None;
    std::optional<std::int32_t> invalid_provider_return;
};

// Immutable-in-record native identity, not an executing callback or delivery.
// Copies pin only original State metadata, never increment callback_inflight.
// This cannot disambiguate an SDK callback whose reused id was misidentified
// before capture. It is route ownership evidence, not peer authentication.
class NativeIdentityConstraint final
{
public:
    NativeIdentityConstraint() noexcept = default;
    explicit operator bool() const noexcept { return bool(state_); }
    const Locator& local() const noexcept { return local_; }
    const Locator& remote() const noexcept { return remote_; }
    // Diagnostic snapshot only, never an execution permission. Binding returns
    // NotReady; revoked/foreign generations cannot be replaced with new values.
    NativeOwnerObservation Observe() const noexcept;
private:
    friend class NativeCallbackLease;
    friend class NativeOwnerTableTestAccess;
    friend struct detail::NativeState;
    friend class detail::NativeReceiveBridge;
    friend class detail::NativeReceiveGuard;
    std::shared_ptr<detail::NativeState> state_;
    std::size_t slot_ = static_cast<std::size_t>(-1);
    std::uint64_t incarnation_ = 0, generation_ = 0, route_version_ = 0;
    Locator local_, remote_;
};
struct NativeCompletion
{
    NativeStatus status = NativeStatus::NotImplemented;
    NativeOwnerObservation owner{};
};
struct NativeSocketResult
{
    enum class Kind { Id, Error };
    Kind kind;
    std::int32_t value;
    static NativeSocketResult Id(NativeSocketId id) noexcept { return {Kind::Id, id}; }
    static NativeSocketResult Error(std::int32_t negative) noexcept { return {Kind::Error, negative}; }
};
struct SyncListenResult
{
    bool success;
    std::int32_t error;
    static SyncListenResult Success() noexcept { return {true, 0}; }
    static SyncListenResult Error(std::int32_t negative) noexcept { return {false, negative}; }
};
struct SyncBindResult
{
    bool success;
    std::int32_t error;
    static SyncBindResult Success() noexcept { return {true, 0}; }
    static SyncBindResult Error(std::int32_t negative) noexcept { return {false, negative}; }
};

class NativeOwnerLease
{
public:
    NativeOwnerLease() noexcept = default;
    explicit operator bool() const noexcept;
    bool operator==(const NativeOwnerLease&) const noexcept;
    bool operator!=(const NativeOwnerLease& other) const noexcept { return !(*this == other); }
    NativeStatus RequestClose() const noexcept;
    NativeResult<ShutdownCallTicket> TryClaimShutdown() const noexcept;
    NativeOwnerObservation Observe() const noexcept;
    // Adapter counters and original close responsibility only, never SDK drain.
    bool AdapterSettled() const noexcept;
    std::optional<NativeSocketId> RetainedId() const noexcept;
private:
    friend class NativeOwnerTable;
    friend class NamespaceLease;
    friend class NativeCallbackLease;
    friend class NativeOwnerTableTestAccess;
    friend struct detail::NativeState;
    friend class detail::NativeOperationToken;
    NativeOwnerLease(std::shared_ptr<detail::NativeState>, std::size_t, std::uint64_t) noexcept;
    std::shared_ptr<detail::NativeState> state_;
    std::size_t slot_ = static_cast<std::size_t>(-1);
    std::uint64_t incarnation_ = 0;
};

namespace detail {
// No public operation factory. Typed tickets alone expose completion, and every
// populated token is minted by the original State under its finite gate.
class NativeOperationToken
{
public:
    NativeOperationToken() noexcept = default;
    NativeOperationToken(NativeOperationToken&&) noexcept;
    NativeOperationToken(const NativeOperationToken&) = delete;
    NativeOperationToken& operator=(const NativeOperationToken&) = delete;
    NativeOperationToken& operator=(NativeOperationToken&&) = delete;
    ~NativeOperationToken() noexcept;
private:
friend class ::fastdds_dsoftbus::dsoftbus::NativeOwnerTable;
friend class ::fastdds_dsoftbus::dsoftbus::NativeOwnerLease;
friend class ::fastdds_dsoftbus::dsoftbus::SocketCallTicket;
friend class ::fastdds_dsoftbus::dsoftbus::ListenCallTicket;
friend class ::fastdds_dsoftbus::dsoftbus::BindCallTicket;
friend class ::fastdds_dsoftbus::dsoftbus::ShutdownCallTicket;
friend class ::fastdds_dsoftbus::dsoftbus::NativeOwnerTableTestAccess;
    friend class NativeSendCallTicket;
    friend class NativeEgressBridge;
    friend struct NativeState;
    NativeStatus Enter(NativeCallKind) noexcept;
    NativeCompletion Complete(NativeCallKind, bool success, std::int32_t result) noexcept;
    NativeStatus Abandon(NativeCallKind) noexcept;
    std::shared_ptr<NativeState> state_;
    std::size_t owner_slot_ = static_cast<std::size_t>(-1), operation_slot_ = static_cast<std::size_t>(-1);
    std::uint64_t owner_incarnation_ = 0, operation_incarnation_ = 0;
    NativeCallKind kind_ = NativeCallKind::Socket;
    bool settled_ = false;
};
}

class SocketCallTicket
{
public:
    SocketCallTicket() noexcept = default;
    SocketCallTicket(SocketCallTicket&&) noexcept = default;
    SocketCallTicket(const SocketCallTicket&) = delete;
    SocketCallTicket& operator=(const SocketCallTicket&) = delete;
    SocketCallTicket& operator=(SocketCallTicket&&) = delete;
    NativeStatus CallEntered() noexcept;
    NativeCompletion Complete(NativeSocketResult) noexcept;
    NativeStatus Abandon() noexcept;
private:
    friend class NativeOwnerTable;
    friend class NativeOwnerTableTestAccess;
    friend struct detail::NativeState;
    detail::NativeOperationToken token_;
};
class ListenCallTicket
{
public:
    ListenCallTicket() noexcept = default;
    ListenCallTicket(ListenCallTicket&&) noexcept = default;
    ListenCallTicket(const ListenCallTicket&) = delete;
    ListenCallTicket& operator=(const ListenCallTicket&) = delete;
    ListenCallTicket& operator=(ListenCallTicket&&) = delete;
    NativeStatus CallEntered() noexcept;
    NativeCompletion Complete(SyncListenResult) noexcept;
    NativeStatus Abandon() noexcept;
private:
    friend class NativeOwnerTable;
    friend struct detail::NativeState;
    detail::NativeOperationToken token_;
};
class BindCallTicket
{
public:
    BindCallTicket() noexcept = default;
    BindCallTicket(BindCallTicket&&) noexcept = default;
    BindCallTicket(const BindCallTicket&) = delete;
    BindCallTicket& operator=(const BindCallTicket&) = delete;
    BindCallTicket& operator=(BindCallTicket&&) = delete;
    NativeStatus CallEntered() noexcept;
    NativeCompletion Complete(SyncBindResult) noexcept;
    NativeStatus Abandon() noexcept;
private:
    friend class NativeOwnerTable;
    friend struct detail::NativeState;
    detail::NativeOperationToken token_;
};
class ShutdownCallTicket
{
public:
    ShutdownCallTicket() noexcept = default;
    ShutdownCallTicket(ShutdownCallTicket&&) noexcept = default;
    ShutdownCallTicket(const ShutdownCallTicket&) = delete;
    ShutdownCallTicket& operator=(const ShutdownCallTicket&) = delete;
    ShutdownCallTicket& operator=(ShutdownCallTicket&&) = delete;
    NativeStatus CallEntered() noexcept;
    NativeStatus CompleteShutdownReturned() noexcept;
    NativeStatus Abandon() noexcept;
private:
    friend class NativeOwnerLease;
    friend struct detail::NativeState;
    detail::NativeOperationToken token_;
};
namespace detail {
// Only the test friend/future separately reviewed coordinator may mint this.
class NativeSendCallTicket
{
public:
    NativeSendCallTicket() noexcept = default;
    NativeSendCallTicket(NativeSendCallTicket&&) noexcept = default;
    NativeSendCallTicket(const NativeSendCallTicket&) = delete;
    NativeSendCallTicket& operator=(const NativeSendCallTicket&) = delete;
    NativeSendCallTicket& operator=(NativeSendCallTicket&&) = delete;
    NativeStatus CallEntered() noexcept;
    NativeCompletion Complete(std::int32_t actual_return_code) noexcept;
    NativeStatus Abandon() noexcept;
private:
friend class ::fastdds_dsoftbus::dsoftbus::NativeOwnerTable;
    friend struct NativeState;
    friend class NativeEgressBridge;
    NativeOperationToken token_;
};
}

class NativeCallbackLease
{
public:
    NativeCallbackLease() noexcept = default;
    NativeCallbackLease(NativeCallbackLease&&) noexcept;
    NativeCallbackLease(const NativeCallbackLease&) = delete;
    NativeCallbackLease& operator=(const NativeCallbackLease&) = delete;
    NativeCallbackLease& operator=(NativeCallbackLease&&) = delete;
    ~NativeCallbackLease() noexcept;
    NativeStatus ObserveShutdown() noexcept;
    NativeOwnerObservation CapturedOwner() const noexcept;
    NativeResult<NativeIdentityConstraint> CaptureIdentity() const noexcept;
private:
    friend class NamespaceLease;
    friend struct detail::NativeState;
    NativeOwnerLease owner_;
    NativeOwnerObservation captured_{};
    bool active_ = false;
};
class NamespaceLease
{
public:
    NamespaceLease() noexcept = default;
    explicit operator bool() const noexcept;
    NativeResult<NativeCallbackLease> AdmitCallback(NativeSocketId) const noexcept;
private:
    friend class NativeOwnerTable;
    friend class NativeOwnerTableTestAccess;
    NamespaceLease(std::shared_ptr<detail::NativeState>, std::size_t) noexcept;
    std::shared_ptr<detail::NativeState> state_;
    std::size_t namespace_index_ = static_cast<std::size_t>(-1);
};
enum class NativeCreation { NoCreate, CreateRequired };
struct NativeReservation
{
    NativeOwnerLease owner;
    NativeCreation creation = NativeCreation::NoCreate;
    std::optional<SocketCallTicket> socket_call;
};
struct NativeInboundResult
{
    NativeStatus status = NativeStatus::NotImplemented;
    std::optional<NativeOwnerLease> owner;
    bool accepted = false, stop_ingress_required = false;
};

// A bounded model core, NOT an SDK backend. CallEntered -> actual Complete must
// retain its unique operation context. Controller destruction seals admission,
// never invokes SDK, waits, cancels entered calls or proves callback quiescence.
// Handles retain original small State; State has no back-reference to handles.
class NativeOwnerTable final
{
public:
    static NativeResult<std::unique_ptr<NativeOwnerTable>> Build(const NativeTableConfiguration&) noexcept;
    NativeOwnerTable(const NativeOwnerTable&) = delete;
    NativeOwnerTable& operator=(const NativeOwnerTable&) = delete;
    NativeOwnerTable(NativeOwnerTable&&) = delete;
    NativeOwnerTable& operator=(NativeOwnerTable&&) = delete;
    ~NativeOwnerTable() noexcept;
    NativeResult<NamespaceLease> AcquireNamespace(const Locator&, const NativeTableConfiguration&) const noexcept;
    NativeResult<NativeReservation> ReserveListener(const NamespaceLease&, const Locator&) noexcept;
    NativeResult<NativeReservation> ReserveDial(const NamespaceLease&, const RouteKey&) noexcept;
    NativeResult<ListenCallTicket> BeginListen(const NativeOwnerLease&) noexcept;
    NativeResult<BindCallTicket> BeginBind(const NativeOwnerLease&) noexcept;
    NativeInboundResult ObserveInbound(const NamespaceLease&, NativeSocketId, const ObservedPeerKey&) noexcept;
    // Negotiation reserves the existing owner budget, NOT a socket/close right.
    // RequestClose cancels an uncommitted reservation without an SDK operation.
    NativeResult<NativeOwnerLease> PrepareInbound(const NamespaceLease&, NativeSocketId,
            const ObservedPeerKey&) noexcept;
    NativeStatus CommitInbound(const NativeOwnerLease&, NativeSocketId) noexcept;
    NativeStatus QuarantineHostId(const Locator& local, NativeSocketId) noexcept;
    bool HostIdWasSeen(NativeSocketId) const noexcept;
    NativeResult<SendConnectionSnapshot> TryResolve(const RouteKey&, std::uint64_t expected_route_version) const noexcept;
    void RequestServiceClose() noexcept;
    NativeUsageSnapshot SnapshotUsage() const noexcept;
private:
    friend class NativeOwnerTableTestAccess;
    explicit NativeOwnerTable(std::shared_ptr<detail::NativeState>) noexcept;
    friend class detail::NativeEgressBridge;
    friend class detail::NativeReceiveBridge;
    NativeResult<detail::NativeSendCallTicket> TestReserveSend(const NativeOwnerLease&,
            std::uint64_t route_version, std::uint64_t generation) noexcept;
    void TestSetNextIdentity(std::uint64_t) noexcept;
    void TestSetCallbackCount(const NativeOwnerLease&, std::size_t) noexcept;
    std::shared_ptr<detail::NativeState> state_;
};
} // namespace fastdds_dsoftbus::dsoftbus
#endif
