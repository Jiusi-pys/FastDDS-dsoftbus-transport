#ifndef MDDS_DSOFTBUS_SERVICE_RECORD_BINDING_HPP
#define MDDS_DSOFTBUS_SERVICE_RECORD_BINDING_HPP

#include <mdds/dsoftbus/ApplicationAttachments.hpp>
#include <mdds/dsoftbus/InputChannels.hpp>
#include <mdds/dsoftbus/NativeOwnerTable.hpp>
#include <mdds/dsoftbus/ProcessClock.hpp>
#include <mdds/dsoftbus/ProcessReceiveInvocation.hpp>
#include <mdds/dsoftbus/ServiceRecordTable.hpp>
#include <functional>
#include <memory>
#include <string>

namespace mdds::dsoftbus {
class ServiceRecordBinding;
class ServiceRecordBindingTestAccess;

// The adapter's own deployment state. It names no SDK fact: Published means this
// binding finished its own steps and will now admit ingress, never that the SDK
// created a record, completed a call or destroyed anything.
enum class BindingPhase { Empty, Prepared, Connected, Published, Closing, Closed, Failed };

// The provider is whoever performs the real SDK calls for one binding: the
// production backend, or a Host test supplying model inputs. A value returned
// here is a model input, not evidence about the SDK.
class ServiceRecordProvider
{
public:
    virtual ~ServiceRecordProvider() = default;
    virtual NativeSocketResult CreateSocket() noexcept = 0;
    virtual SyncListenResult Listen(NativeSocketId) noexcept = 0;
    virtual SyncBindResult Bind(NativeSocketId) noexcept = 0;
    virtual void Shutdown(NativeSocketId) noexcept = 0;
};

// What one binding is for. One of three topologies, all of which keep exactly
// one native owner per socket under one fixed entrance per (record, role):
//   create        - reserves the record, exposes both roles, serves request.role
//   join          - joins an entrance another binding already exposed for that
//                   role, without reserving a slot, and serves one more socket
//   accepted      - a Server-role socket accepted through a listener that another
//                   binding owns, adopted through ObserveInbound
struct BindingRequest
{
    // Record identity. The service name selects the SDK client-process record;
    // it is never used as an owner, receiver or lease identity.
    std::string service_name;
    RecordRole role = RecordRole::Client;
    // The entrance to join. Empty means this binding creates the record. A join
    // reuses the identical fixed entrance and consumes no record slot.
    RecordEntrance existing{};
    // Input local this binding serves, and the route for a Client (dial) role.
    Locator local{};
    RouteKey route{};
    bool has_route = false;
    // Accepted-socket deployment: the peer the listener's OnBind reported and
    // the socket id it delivered. The owner is then adopted through
    // ObserveInbound instead of a listener reservation.
    ObservedPeerKey accepted{};
    NativeSocketId accepted_socket = -1;
    bool is_accepted = false;
    // The binding that created this record. Required when joining one, so that a
    // join always names the record it joins, and read only while the joining
    // binding is being built. Ingress routing and the shared (application,
    // local) registration belong to the record itself, so this binding does not
    // have to outlive its joiners: a joiner keeps working after the creator is
    // released and destroyed. A joiner on the same local shares the record's
    // application claim and receiver registration, because those exist once per
    // (application, local), and it must then pass the same receiver.
    ServiceRecordBinding* record_owner = nullptr;
    // The native table's own configuration (needed for its namespace lease) and
    // the attachments' fixed local set with their directory snapshot.
    NativeTableConfiguration config{};
    EndpointDirectory::Snapshot directory{};
    InputChannels::Receiver* receiver = nullptr;
    // Host diagnostic seam, called by the existing receive path outside gates.
    // Contains real capture identity; it is not SDK callback metadata.
    std::shared_ptr<ProcessReceiveObserver> receive_observer;
};

// Read-only view. It reports state and identity, never a callable entrance: the
// caller retains the entrance it received from the record table.
struct BindingObservation
{
    ReceiverIdentity receiver_identity{};
    bool input_closing = false;
    NativeStatus status = NativeStatus::InvalidHandle;
    BindingPhase phase = BindingPhase::Empty;
    bool created_record = false, role_exposed = false, socket_known = false, setup_complete = false;
    bool membership = false, application_bound = false, receiver_open = false;
    // False when this binding shares the creator's application/receiver
    // registration because it joined a record on the same local. Reporting only:
    // the registration belongs to the record and is settled by its last user, so
    // this never licenses a write to another binding's state.
    bool owns_application = false;
    std::size_t role_members = 0;
    std::size_t callbacks_in_flight = 0;   // attributed ingress not yet returned
    std::size_t owner_callback_inflight = 0, owner_receive_inflight = 0;
    AttachmentUsage receive{};
    std::size_t admitted = 0, refused_phase = 0, refused_native = 0, refused_receive = 0;
    // Attributed callbacks the record could not route: no live binding of this
    // record owns that (role, socket) any more. Record-scoped, so every binding
    // of the record reports the same value. A terminal refusal, never a queue.
    std::size_t refused_no_route = 0;
    std::uint64_t ingress_seen = 0;
    std::uint64_t record_incarnation = 0, owner_slot = 0, owner_incarnation = 0;
    NativeSocketId socket = -1;
    bool owner_closed = false;
};

// One already-admitted delivery, bound to the binding that admitted it. It is
// the only thing NextAdmitted hands out: the composition keeps the underlying
// invocation private, so the delivery context cannot be entered around it. The
// binding identity is captured when the delivery is issued and is never looked
// up again, so a delivery can only be run through the binding that issued it.
// That identity is a weak handle on the issuing state, not an address: an
// expired one cannot be satisfied by a later binding that happens to be
// allocated where the issuer was.
class BindingDelivery final
{
public:
    BindingDelivery() noexcept;
    ~BindingDelivery();
    BindingDelivery(BindingDelivery&&) noexcept;
    BindingDelivery& operator=(BindingDelivery&&) noexcept;
    BindingDelivery(const BindingDelivery&) = delete;
    BindingDelivery& operator=(const BindingDelivery&) = delete;
    explicit operator bool() const noexcept;
    // The original identities this delivery re-validates against, as recorded at
    // admission. Never a current-state lookup.
    ReceiveDiagnostic Observation() const noexcept;
private:
    friend class ServiceRecordBinding;
    friend class ServiceRecordBindingTestAccess;
    explicit BindingDelivery(ProcessReceiveInvocation&&, std::weak_ptr<void>) noexcept;
    // Held inline, because issuing a delivery happens on the admission path,
    // where every other failure is reported as a status instead of raised: an
    // allocation here could otherwise end the process rather than fail the
    // delivery.
    ProcessReceiveInvocation invocation_;
    std::weak_ptr<void> issuer_;
    bool issued_ = false;
};

// One reusable composition entry over three existing components. It owns no
// state any of them already owns: it holds the association between one record
// and role, one native owner plan and one application-local receiver, and it
// forwards every identity decision to the component that owns it.
//
// Ingress order is fixed: the record table attributes under its own gate and
// calls the record's sink *after* releasing it, so the sink then takes the
// native owner admission and, inside that pin, the application/receiver capture.
// A callback admitted this way carries all four identities and never re-resolves
// a current owner or application later.
//
// What belongs to the record outlives any single binding of it. The record owns
// one ingress dispatch control (its sink and its per-(role, socket) routes) and
// one shared (application, local) registration per local; a binding registers
// into both and removes only its own registration when it is released. Releasing
// and destroying a binding never withdraws capacity another binding of the same
// record is still using, and the record is what its joiners are served by.
//
// Destruction and close are external control operations, with one callback-context
// entry of their own. From inside an ingress or delivery callback, ask for the close
// with CloseRequest(): it records the intent, closes logical admission and returns
// immediately. The owner then completes that close outside the callback - with
// DeferredClose(), or with Abandon(), or simply by destroying the binding - and that
// obligation is the owner's: a requested close that nobody completes stays in the
// closing phase holding its registrations, which Observe() reports as Closing.
//
// The synchronous forms must never be used from a callback: BeginClose(), Abandon()
// and DeferredClose() are refused there with WrongState rather than waited on. A
// destructor cannot refuse, so the one shape that would deadlock is enforced by
// failing fast: destroying a binding whose shared (application, local) registration
// this thread is currently executing inside - the binding whose own delivery is
// running, or another binding that shares that registration - aborts with a
// diagnostic naming CloseRequest(), because its release would have to wait for the
// receiver permit the running frame holds. Destroying an *unrelated* binding from a
// callback is not that shape and is allowed: its release waits on nothing this
// thread holds.
//
// A binding is released before it is destroyed, and it does not have to outlive the
// record or its joiners: the record's own dispatch and registrations are what keep
// serving them.
//
// The close-request path changes none of the lifetime rules: a requested close
// still drains before it releases, an already admitted delivery still runs to
// completion under its original identities, and the sink registration, the native
// owner lease, the receiver registration and the application claim are released
// exactly as they are on the synchronous path.
class ServiceRecordBinding final
{
public:
    static NativeResult<std::unique_ptr<ServiceRecordBinding>> Build(ServiceRecordTable& records,
            NativeOwnerTable& native, ApplicationAttachments& attachments, ServiceRecordProvider& provider,
            const BindingRequest&, ProcessClock clock = {});
    ~ServiceRecordBinding();
    ServiceRecordBinding(const ServiceRecordBinding&) = delete;
    ServiceRecordBinding& operator=(const ServiceRecordBinding&) = delete;
    ServiceRecordBinding(ServiceRecordBinding&&) = delete;
    ServiceRecordBinding& operator=(ServiceRecordBinding&&) = delete;

    // Deployment, in order, each step all-or-nothing for its own boundary:
    // Prepare : record reservation + role exposure (fixes this role's entrance,
    //           and both roles when it creates the record) plus the native
    //           namespace + owner reservation. No provider call.
    // Connect : the provider's Socket and Listen/Bind calls, each wrapped in its
    //           own native call ticket.
    // Publish : the record-side membership, the application/receiver lease, and
    //           then the single point at which this binding starts admitting.
    NativeStatus Prepare() noexcept;
    NativeStatus Connect() noexcept;
    NativeStatus Publish() noexcept;
    // Independent RX-only control for a sole-member local registration. Native
    // owner/record/route/TX remain live. Queued RX is canceled without a pump.
    NativeStatus CloseInput() noexcept;
    NativeStatus ReopenInput(InputChannels::Receiver*) noexcept;
    // Whole-deployment rollback in reverse order. A created socket's close
    // responsibility is claimed once and the provider is shut down; a record slot
    // that was exposed is never reclaimed and its entrance is never rebound.
    NativeStatus Abandon() noexcept;

    // Callback-context close request. Legal from any context, including an ingress
    // or delivery callback of this binding: it closes logical admission for new
    // ingress and returns without draining, releasing or waiting, so the callback
    // thread never waits for its own in-flight permit. Returns None on the first
    // request; WrongState when the binding has not reached its published phase (an
    // empty, prepared, connected or failed deployment); and ServiceClosing when a
    // close is already in progress or already finished, because either way there is
    // nothing left for the caller to request. Completing the close is the owner's
    // job, outside a callback, and until it is completed the binding stays Closing
    // with its registrations held.
    NativeStatus CloseRequest() noexcept;
    // Completes a close that is in progress - from CloseRequest above or from
    // BeginClose - from a control context. It drains and releases exactly as Abandon
    // does and marks the binding closed. Refused with WrongState from a callback,
    // like every synchronous control operation, and refused with the same status
    // while the binding is in any other phase, including a deployment that was never
    // published. A binding that is already closed reports AlreadySettled, whether it
    // was closed by this call, by Abandon or by an earlier completion.
    NativeStatus DeferredClose() noexcept;

    // The exact callback pointer this binding was issued for its role. It
    // belongs to the record, not to this binding's live state: it stays valid
    // for the record's life, so it is also what a late call after a release
    // arrives on, and what the caller handed to the SDK. Empty only before
    // Prepare. Host tests call it directly and claim no SDK ABI equivalence.
    RecordBytesCallback Entrance() const noexcept;
    // The entrance issued for a role of this record, for handing to another
    // binding that joins that role. Empty for a role this binding never had
    // issued, which for a joiner is the role it did not join.
    RecordEntrance RoleEntrance(RecordRole) const noexcept;

    // Delivery side of the same composition: takes one queued admission into a
    // real execution lease and hands it back bound to this binding. The delivery
    // context is marked on the execution, so a control call made from inside a
    // receiver callback is refused instead of deadlocking, whatever entry point
    // ran the delivery; a delivery this binding did not issue is refused.
    NativeResult<BindingDelivery> NextAdmitted() noexcept;
    InputDeliveryResult RunAdmitted(BindingDelivery&) noexcept;

    // Close and retirement are separate premises, each requested explicitly.
    // BeginClose stops logical admission: new ingress is refused and the native
    // owner stops accepting new operations, while already admitted work keeps
    // running under the identities it was admitted with.
    NativeStatus BeginClose() noexcept;
    // Adapter-side drain condition, decidable from the existing counters. It
    // proves nothing about the SDK, which is why it is only one of the premises
    // retirement needs.
    bool DrainComplete() const noexcept;
    // Explicit model retirement boundary. It requires a closed, drained binding
    // and the caller's declaration that the destruction premise holds: this call
    // never derives that premise from a name or an owner count, and a Host run
    // reports no SDK destruction receipt.
    RecordStatus RetireRecord() noexcept;

    // Nonblocking, thread-local context check for outer control adapters.
    static bool IsCallbackContext() noexcept;
    BindingObservation Observe() const noexcept;
private:
    friend class ServiceRecordBindingTestAccess;
    // The record-scoped ingress dispatch control, defined in the implementation.
    // It pins one binding's state and is the only other reader of the private
    // State; both are implementation details of the same translation unit.
    friend struct RecordControl;
    struct State;
    struct TestHooks
    {
        // Outside every component gate. Only before_membership, before_publish and
        // before_registration may throw bad_alloc; the others must not throw.
        std::function<void()> after_record_reserve, after_owner_reserve, after_socket,
                before_membership, before_receiver, before_publish, before_rollback;
        // Outside every component gate, at the point where the application claim
        // and the receiver registration are installed on this local and their
        // shared registration object does not exist yet, so a failure here must be
        // settled by the release path. May throw bad_alloc and nothing else.
        std::function<void()> before_registration;
        // Scheduling point of the release path, outside every gate: a release has
        // decided the fate of the record's shared (application, local)
        // registration and has not yet applied it. It exists so the concurrent
        // release of a sibling binding can be driven deterministically; it must
        // not throw, and a release that is refused reentrantly does not reach it.
        std::function<void()> before_group_settle;
    };
    static NativeResult<std::unique_ptr<ServiceRecordBinding>> BuildWithHooks(ServiceRecordTable&,
            NativeOwnerTable&, ApplicationAttachments&, ServiceRecordProvider&, const BindingRequest&,
            ProcessClock, TestHooks);
    explicit ServiceRecordBinding(std::shared_ptr<State>) noexcept;
    std::shared_ptr<State> state_;
};
} // namespace mdds::dsoftbus
#endif
