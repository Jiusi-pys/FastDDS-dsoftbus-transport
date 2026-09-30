// Composition contracts for the record table, the native owner table and the
// application attachments. Every case drives the composition entry a backend
// would use; none of them mutates the two tables directly to assemble a success
// state, and none claims SDK ABI, SDK behaviour or device evidence. Provider
// results here are model inputs. One case per process unless the case itself
// loops in-process (C11), which is reported separately from the per-case runs.
#include <fastdds_dsoftbus/dsoftbus/ServiceRecordBinding.hpp>
#include "../probes/PublicSocketDecisionStore.hpp"
#include "socket.h" // Current source header, for the callback-shape assertion only.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// Allocation denial for the exceptional-admission case, the same technique the
// repository already uses for the other cores.
thread_local bool deny_allocations = false;
void* operator new(std::size_t size)
{
    if (deny_allocations) throw std::bad_alloc();
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

using namespace fastdds_dsoftbus::dsoftbus;

// The case that runs itself as a child process needs to name itself.
std::string executable_path;

// Test access to the composition's scheduling hooks. It grants nothing a
// production caller has: the same Build path, the same phases, the same
// contract, plus the ability to drive one interleaving deterministically.
namespace fastdds_dsoftbus::dsoftbus {
class ServiceRecordBindingTestAccess
{
public:
    // The composition's scheduling hooks, under a name the test can spell.
    using Hooks = ServiceRecordBinding::TestHooks;
    static NativeResult<std::unique_ptr<ServiceRecordBinding>> BuildWithHooks(ServiceRecordTable& records,
            NativeOwnerTable& native, ApplicationAttachments& attachments, ServiceRecordProvider& provider,
            const BindingRequest& request, Hooks hooks)
    {
        return ServiceRecordBinding::BuildWithHooks(records, native, attachments, provider, request,
                ProcessClock{}, std::move(hooks));
    }
};
} // namespace fastdds_dsoftbus::dsoftbus

namespace {
void Require(bool value, const char* reason)
{
    if (!value) throw std::runtime_error(reason);
}
void RequireEqual(std::size_t actual, std::size_t expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "count=" << actual << " expected=" << expected << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
void RequireStatus(NativeStatus actual, NativeStatus expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "status=" << static_cast<int>(actual) << " expected=" << static_cast<int>(expected)
                  << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
void RequireDelivery(InputDeliveryResult actual, InputDeliveryResult expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "delivery=" << static_cast<int>(actual) << " expected=" << static_cast<int>(expected)
                  << " (" << reason << ")" << std::endl;
        throw std::runtime_error(reason);
    }
}
void RequireRecord(RecordStatus actual, RecordStatus expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "record_status=" << static_cast<int>(actual) << " expected=" << static_cast<int>(expected)
                  << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
template<class T> T RequireOk(NativeResult<T>&& result, const char* reason)
{
    Require(result.issue.code == NativeStatus::None, reason);
    Require(result.value.has_value(), reason);
    return std::move(*result.value);
}
bool SameEntrance(RecordBytesCallback left, const RecordEntrance& right)
{
    return left != nullptr && right.function() == left;
}
std::vector<std::uint8_t> PayloadBytes(std::size_t size = 32, std::uint8_t seed = 5)
{
    return std::vector<std::uint8_t>(size, seed);
}
Locator MakeLocator(std::uint8_t seed)
{
    EndpointId id{};
    id[0] = 0x0A;
    id[14] = 0x7F;
    id[15] = seed;
    Locator out{};
    Require(LocatorCodec::Encode(id, out), "encode locator");
    return out;
}

// The provider is a model input in Host: canned results plus exact call counts,
// so a hidden SDK call or worker would show up as an unexpected count.
struct Provider final : ServiceRecordProvider
{
    NativeSocketResult socket = NativeSocketResult::Id(11);
    SyncListenResult listen = SyncListenResult::Success();
    SyncBindResult bind = SyncBindResult::Success();
    // Written from the control threads of the concurrent cases, read and judged
    // on the test thread, so they are atomic rather than plain ints.
    std::atomic<int> created{0}, listened{0}, bound{0}, shutdown{0};
    NativeSocketResult CreateSocket() noexcept override { ++created; return socket; }
    SyncListenResult Listen(NativeSocketId) noexcept override { ++listened; return listen; }
    SyncBindResult Bind(NativeSocketId) noexcept override { ++bound; return bind; }
    std::function<void(NativeSocketId)> shutdown_hook;
    void Shutdown(NativeSocketId id) noexcept override { ++shutdown; if(shutdown_hook)shutdown_hook(id); }
};

struct Receiver final : InputChannels::Receiver
{
    std::mutex gate;
    std::vector<std::size_t> sizes;
    std::function<void()> on_receive;
    void OnDataReceived(const eprosima::fastdds::rtps::octet*, const std::uint32_t size, const Locator&,
            const Locator&) override
    {
        {
            std::lock_guard<std::mutex> lock(gate);
            sizes.push_back(size);
        }
        if (on_receive) on_receive();
    }
    std::size_t Count()
    {
        std::lock_guard<std::mutex> lock(gate);
        return sizes.size();
    }
};

// One process-wide record table plus the per-instance native table and
// attachments these bindings compose.
struct Fixture
{
    Locator local = MakeLocator(1);
    // A second local of the same participant: one attachments instance, so one
    // delivery queue shared by every registration on it.
    Locator local_b = MakeLocator(5);
    Locator remote_a = MakeLocator(2);
    Locator remote_b = MakeLocator(3);
    Locator remote_c = MakeLocator(4);
    NativeTableConfiguration config;
    std::unique_ptr<NativeOwnerTable> native;
    std::unique_ptr<ApplicationAttachments> attachments;
    EndpointDirectory::Snapshot snapshot;
    Provider provider;
    std::shared_ptr<Receiver> receiver = std::make_shared<Receiver>();

    Fixture()
    {
        config.directory.locals.push_back(LocalBinding{local, "ns.srtc", "listen.srtc"});
        config.directory.locals.push_back(LocalBinding{local_b, "ns.srtc.b", "listen.srtc.b"});
        // Each route names a distinct peer: the directory proves reverse
        // uniqueness, so two routes with one observed identity are rejected.
        config.directory.routes.push_back(Route{{local, remote_a},
                {"net.a", "remote.a", "init.a", {"peer.a"}, RecordDataType::Bytes}, 1});
        config.directory.routes.push_back(Route{{local, remote_b},
                {"net.b", "remote.b", "init.b", {"peer.b"}, RecordDataType::Bytes}, 1});
        config.directory.routes.push_back(Route{{local, remote_c},
                {"net.c", "remote.c", "init.c", {"peer.c"}, RecordDataType::Bytes}, 1});
        config.directions = {RouteDirection::Dial, RouteDirection::Dial, RouteDirection::Accept};
        config.service_pkg_name = "com.kaihong.mdds";
        config.limits.setup_calls = 2;
        auto built = NativeOwnerTable::Build(config);
        if (built.issue.code != NativeStatus::None) {
            std::cerr << "native build status=" << static_cast<int>(built.issue.code)
                      << " directory=" << static_cast<int>(built.issue.directory_issue.code)
                      << " entry=" << built.issue.directory_issue.entry << "\n";
        }
        Require(built.issue.code == NativeStatus::None, "build native owner table");
        native = std::move(*built.value);
        attachments = std::make_unique<ApplicationAttachments>(std::vector<Locator>{local});
        auto directory = EndpointDirectory::Build(config.directory);
        Require(directory.value != nullptr, "build directory");
        snapshot = *directory.value;
    }

    BindingRequest Request(RecordRole role, bool use_b) const
    {
        BindingRequest request;
        request.role = role;
        request.local = local;
        request.config = config;
        request.directory = snapshot;
        request.receiver = receiver.get();
        if (role == RecordRole::Client) {
            request.route = config.directory.routes[use_b ? 1 : 0].key;
            request.has_route = true;
        }
        return request;
    }
    BindingRequest CreateRequest(const std::string& name, RecordRole role, bool use_b) const
    {
        auto request = Request(role, use_b);
        request.service_name = name;
        return request;
    }
    BindingRequest JoinRequest(const ServiceRecordBinding& creator, RecordRole role, bool use_b) const
    {
        auto request = Request(role, use_b);
        request.record_owner = const_cast<ServiceRecordBinding*>(&creator);
        request.existing = creator.RoleEntrance(role);
        return request;
    }
    std::unique_ptr<ServiceRecordBinding> Build(const BindingRequest& request)
    {
        auto built = ServiceRecordBinding::Build(ServiceRecordTable::Process(), *native, *attachments,
                provider, request);
        Require(built.issue.code == NativeStatus::None, "build binding");
        return std::move(*built.value);
    }
    void Arm(NativeSocketId id) { provider.socket = NativeSocketResult::Id(id); }
    // One native owner exists per (local, route) plan, not per record, so a
    // record that needs its own owner and socket id gets its own native table
    // and attachments, exactly as one Participant per record would.
    std::vector<std::unique_ptr<NativeOwnerTable>> extra_native;
    std::vector<std::unique_ptr<ApplicationAttachments>> extra_attachments;
    NativeOwnerTable& AddNative()
    {
        auto built = NativeOwnerTable::Build(config);
        Require(built.issue.code == NativeStatus::None, "build a per-record native table");
        extra_native.push_back(std::move(*built.value));
        extra_attachments.push_back(std::make_unique<ApplicationAttachments>(std::vector<Locator>{local}));
        return *extra_native.back();
    }
    ApplicationAttachments& NewestAttachments() { return *extra_attachments.back(); }
    std::unique_ptr<ServiceRecordBinding> DeployFresh(const std::string& name, NativeSocketId id)
    {
        auto& fresh_native = AddNative();
        Arm(id);
        auto request = CreateRequest(name, RecordRole::Client, false);
        auto built = ServiceRecordBinding::Build(ServiceRecordTable::Process(), fresh_native,
                NewestAttachments(), provider, request);
        Require(built.issue.code == NativeStatus::None, "build a per-record binding");
        auto binding = std::move(*built.value);
        RequireStatus(binding->Prepare(), NativeStatus::None, "prepare a per-record binding");
        RequireStatus(binding->Connect(), NativeStatus::None, "connect a per-record binding");
        RequireStatus(binding->Publish(), NativeStatus::None, "publish a per-record binding");
        return binding;
    }
    std::unique_ptr<ServiceRecordBinding> Deploy(const BindingRequest& request, NativeSocketId id)
    {
        Arm(id);
        auto binding = Build(request);
        RequireStatus(binding->Prepare(), NativeStatus::None, "prepare");
        RequireStatus(binding->Connect(), NativeStatus::None, "connect");
        RequireStatus(binding->Publish(), NativeStatus::None, "publish");
        return binding;
    }
    std::unique_ptr<ServiceRecordBinding> BuildWithHooks(const BindingRequest& request,
            ServiceRecordBindingTestAccess::Hooks hooks)
    {
        auto built = ServiceRecordBindingTestAccess::BuildWithHooks(ServiceRecordTable::Process(), *native,
                *attachments, provider, request, std::move(hooks));
        Require(built.issue.code == NativeStatus::None, "build a binding with scheduling hooks");
        return std::move(*built.value);
    }
    // A binding of an existing record that brings its own native table, exactly as
    // a second Participant joining a live record would: one native owner plan per
    // (local, route), so a further owner needs its own table.
    std::unique_ptr<ServiceRecordBinding> BuildJoinerOn(const BindingRequest& request,
            ApplicationAttachments& registrations)
    {
        auto& fresh_native = AddNative();
        auto built = ServiceRecordBinding::Build(ServiceRecordTable::Process(), fresh_native,
                registrations, provider, request);
        Require(built.issue.code == NativeStatus::None, "build a joining binding");
        return std::move(*built.value);
    }
    std::unique_ptr<ServiceRecordBinding> DeployWithHooks(const BindingRequest& request, NativeSocketId id,
            ServiceRecordBindingTestAccess::Hooks hooks)
    {
        Arm(id);
        auto binding = BuildWithHooks(request, std::move(hooks));
        RequireStatus(binding->Prepare(), NativeStatus::None, "prepare with hooks");
        RequireStatus(binding->Connect(), NativeStatus::None, "connect with hooks");
        RequireStatus(binding->Publish(), NativeStatus::None, "publish with hooks");
        return binding;
    }
    void Emit(const ServiceRecordBinding& binding, NativeSocketId socket, const std::vector<std::uint8_t>& bytes)
    {
        EmitPointer(binding.Entrance(), socket, bytes);
    }
    // The caller retains the pointer it handed to the SDK; after a release the
    // binding reports no entrance, so this is the only honest way to exercise a
    // late call on a pointer the SDK could still hold.
    static void EmitPointer(RecordBytesCallback entrance, NativeSocketId socket,
            const std::vector<std::uint8_t>& bytes)
    {
        Require(entrance != nullptr, "the retained entrance pointer is not empty");
        entrance(socket, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    }
    std::size_t Drain(ServiceRecordBinding& binding)
    {
        std::size_t delivered = 0;
        while (true) {
            auto invocation = binding.NextAdmitted();
            if (!invocation) {
                if (invocation.issue.code != NativeStatus::NotFound) {
                    std::cerr << "drain stopped with status="
                              << static_cast<int>(invocation.issue.code) << " after " << delivered << "\n";
                }
                break;
            }
            (void)binding.RunAdmitted(*invocation.value);
            ++delivered;
        }
        return delivered;
    }
    // Everything the attachments hold must be back to zero once a binding is
    // released: they are the only owner of payload copies. This reads the object
    // the case actually drives, never a spare one.
    static void RequireAttachmentsEmpty(const ApplicationAttachments& used, const char* reason)
    {
        const auto usage = used.SnapshotUsage();
        RequireEqual(usage.records, 0, reason);
        RequireEqual(usage.bytes, 0, reason);
        RequireEqual(usage.queued, 0, reason);
        RequireEqual(usage.taken, 0, reason);
        RequireEqual(usage.delivering, 0, reason);
        RequireEqual(usage.copying, 0, reason);
        RequireEqual(usage.active_applications, 0, reason);
        RequireEqual(usage.detaching_applications, 0, reason);
        RequireEqual(usage.retained_applications, 0, reason);
    }
    void RequireReceiveBudgetZero(const char* reason)
    {
        RequireAttachmentsEmpty(*attachments, reason);
    }
};

ServiceRecordTable& Table() { return ServiceRecordTable::Process(); }

// A failed composition assertion says what it saw.
void Dump(const ServiceRecordBinding& binding, const char* label)
{
    const auto view = binding.Observe();
    std::cerr << "dump[" << label << "] phase=" << static_cast<int>(view.phase)
              << " socket=" << view.socket << " exposed=" << view.role_exposed
              << " socket_known=" << view.socket_known << " setup=" << view.setup_complete
              << " membership=" << view.membership << " app=" << view.application_bound
              << " receiver=" << view.receiver_open << " owns_app=" << view.owns_application
              << " members=" << view.role_members << " admitted=" << view.admitted
              << " refused_phase=" << view.refused_phase << " refused_native=" << view.refused_native
              << " refused_receive=" << view.refused_receive << " in_flight=" << view.callbacks_in_flight
              << " owner_inflight=" << view.owner_callback_inflight << " queued=" << view.receive.queued
              << " taken=" << view.receive.taken << " delivering=" << view.receive.delivering
              << " copying=" << view.receive.copying << " records=" << view.receive.records << "\n";
}
// Defined with the forbidden-shape cases below; declared here for the ones that use it.
void RequireChildStops(const char* child_case, const char* reason,const char* detail=nullptr);

void RequireDrained(const ServiceRecordBinding& binding, const char* reason)
{
    if (!binding.DrainComplete()) {
        Dump(binding, reason);
        throw std::runtime_error(reason);
    }
}

// C1: two owners on one record. Closing A must not disturb B, which keeps
// admitting and delivering through the same composition entry.
void TwoOwnersSameRecord()
{
    Fixture fixture;
    auto a = fixture.Deploy(fixture.CreateRequest("pkg.srtc.two", RecordRole::Client, false), 11);
    // What the joiner is held to, saved from the creation result before the
    // joiner exists, so the expectations are not read back off the thing under
    // test.
    const auto created = Table().Observe(a->RoleEntrance(RecordRole::Client));
    RequireEqual(created.role_owners[0], 1, "the created role starts with one owner");
    Require(created.record_incarnation != 0, "the created record has an incarnation");
    const auto created_role = a->RoleEntrance(RecordRole::Client);
    auto b = fixture.Deploy(fixture.JoinRequest(*a, RecordRole::Client, true), 12);
    RequireEqual(Table().SnapshotUsage().slots_exposed, 1, "one record, one slot");
    RequireEqual(Table().Observe(b->RoleEntrance(RecordRole::Client)).role_owners[0], 2,
            "two owners under one record");
    // This one is an echo of the case's own input: a join names that role, so the
    // joiner must hand back exactly it. It is a precondition, not a defect check.
    // What is real evidence for "one record, two owners" is the table's own
    // counts below plus the distinct owner identities read from the native table.
    Require(b->RoleEntrance(RecordRole::Client) == created_role,
            "the joiner is served by the role the join named");
    // Once the two entrances are equal, comparing the incarnation read through
    // them would compare a value with itself, so the created record is instead
    // pinned by what the table reports about the role and by the distinct owner
    // identities below.
    Require(a->Entrance() == b->Entrance(), "one live role has one fixed entrance");
    // Two owners are two identities, each reported by the component that owns it.
    Require(a->Observe().owner_incarnation != b->Observe().owner_incarnation,
            "the two owners have distinct native owner incarnations");
    Require(a->Observe().owner_slot != b->Observe().owner_slot,
            "and occupy distinct native owner cells");
    Require(a->Observe().socket != b->Observe().socket, "and own distinct sockets");
    auto payload = PayloadBytes();
    fixture.Emit(*a, 11, payload);
    fixture.Emit(*b, 12, payload);
    RequireEqual(a->Observe().admitted, 1, "A admitted its own socket");
    RequireEqual(b->Observe().admitted, 1, "B admitted its own socket");
    RequireEqual(fixture.Drain(*b), 2,
            "one drain delivers every record this application admitted, both owners' included");
    RequireEqual(fixture.receiver->Count(), 2, "the receiver saw both payloads");
    const auto a_entrance = a->Entrance();
    RequireStatus(a->BeginClose(), NativeStatus::None, "close A");
    RequireStatus(a->Abandon(), NativeStatus::None, "abandon A");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1,
            "A's socket close responsibility was claimed once");
    const auto dropped = Table().SnapshotUsage().dropped_unknown_socket;
    Fixture::EmitPointer(a_entrance, 11, payload);
    RequireEqual(Table().SnapshotUsage().dropped_unknown_socket - dropped, 1,
            "A's socket is no longer a member: a terminal drop");
    fixture.Emit(*b, 12, payload);
    RequireEqual(b->Observe().admitted, 2, "B keeps admitting after A closed");
    RequireEqual(fixture.Drain(*b), 1, "B keeps delivering after A closed");
    RequireEqual(fixture.receiver->Count(), 3, "B's delivery reached the receiver");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "B did not shut anything down");
    RequireStatus(b->BeginClose(), NativeStatus::None, "close B");
    RequireStatus(b->Abandon(), NativeStatus::None, "abandon B");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2,
            "B's close responsibility was claimed once");
    fixture.RequireReceiveBudgetZero("no payload outlives the released bindings");
}

// C2: cross-role survival, and retirement only at the explicit boundary.
void CrossRole()
{
    Fixture fixture;
    auto client = fixture.Deploy(fixture.CreateRequest("pkg.srtc.roles", RecordRole::Client, false), 31);
    // Both role identities, saved from the creator before anything joins them.
    const auto client_role = client->RoleEntrance(RecordRole::Client);
    const auto server_role = client->RoleEntrance(RecordRole::Server);
    Require(server_role != client_role, "the two roles of one record are distinct entrances");
    auto listener = fixture.Build(fixture.JoinRequest(*client, RecordRole::Server, false));
    RequireStatus(listener->Prepare(), NativeStatus::None, "join the sibling role");
    Require(SameEntrance(listener->Entrance(), client->RoleEntrance(RecordRole::Server)),
            "the sibling role has one fixed entrance");
    Require(!SameEntrance(listener->Entrance(), client_role),
            "and it is not the client role's entrance");
    fixture.Arm(33);
    RequireStatus(listener->Connect(), NativeStatus::None, "listen");
    RequireStatus(listener->Publish(), NativeStatus::None, "publish the listener");
    RequireEqual(static_cast<std::size_t>(fixture.provider.listened), 1, "one listener call");
    RequireStatus(client->BeginClose(), NativeStatus::None, "close the client role");
    RequireStatus(client->Abandon(), NativeStatus::None, "release the client role");
    const auto after = Table().Observe(listener->RoleEntrance(RecordRole::Server));
    Require(after.role_exposed[1], "the empty role keeps its incarnation");
    RequireEqual(after.role_owners[1], 1, "the server role still has its owner");
    Require(after.phase == RecordPhase::Exposed, "the record is still live");
    auto second = fixture.Deploy(fixture.JoinRequest(*client, RecordRole::Client, true), 32);
    // The client role's issued entrance comes from the binding that exposed it,
    // and it is still handed out after that binding released.
    // Also an input echo: the join named this role. The independent evidence that
    // the record survived the creator is the listener's live owner, the record
    // phase, and the delivery below.
    Require(SameEntrance(second->Entrance(), client_role),
            "the emptied role reuses its issued entrance");
    Require(!SameEntrance(second->Entrance(), server_role),
            "and the rejoined client role does not become the server role");
    Require(second->Observe().owner_incarnation != listener->Observe().owner_incarnation,
            "the rejoined client owner is not the listener's owner");
    fixture.Emit(*second, 32, PayloadBytes());
    RequireEqual(fixture.Drain(*second), 1, "the rejoined client role delivers");
    RequireStatus(second->BeginClose(), NativeStatus::None, "close the second client");
    RequireStatus(second->Abandon(), NativeStatus::None, "release the second client");
    RequireRecord(second->RetireRecord(), RecordStatus::StillAnchored,
            "a record another role still serves is never retired");
    RequireStatus(listener->BeginClose(), NativeStatus::None, "close the listener");
    RequireStatus(listener->Abandon(), NativeStatus::None, "release the listener");
    RequireRecord(listener->RetireRecord(), RecordStatus::None, "the explicit boundary retires the record");
    RequireRecord(listener->RetireRecord(), RecordStatus::Retired, "retirement is settled once");
}

// C3: partial preparation and publication failures roll back locally and leave
// other bindings untouched.
void PartialFailure()
{
    Fixture fixture;
    auto first = fixture.Deploy(fixture.CreateRequest("pkg.srtc.partial", RecordRole::Client, false), 41);
    // (a) the record reservation fails: the name is in use.
    auto second = fixture.Build(fixture.CreateRequest("pkg.srtc.partial", RecordRole::Client, true));
    RequireStatus(second->Prepare(), NativeStatus::ExistingPending, "a second create for the same name is refused");
    RequireEqual(fixture.native->SnapshotUsage().owners, 1, "no native owner was reserved");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 1, "no hidden provider call");
    // (b) the native owner reservation fails: an unknown route.
    auto bad = fixture.CreateRequest("pkg.srtc.partial.route", RecordRole::Client, true);
    bad.route = RouteKey{fixture.local, MakeLocator(9)};
    auto bad_binding = fixture.Build(bad);
    RequireStatus(bad_binding->Prepare(), NativeStatus::NotFound, "an unknown route is refused");
    RequireEqual(Table().SnapshotUsage().slots_unanchored, 1,
            "the exposed slot is left unanchored, never reclaimed");
    RequireEqual(Table().SnapshotUsage().slots_free, record_slot_capacity - 2,
            "no slot was reclaimed and only two were exposed");
    RequireRecord(Table().ReserveRecord("pkg.srtc.partial.route").status, RecordStatus::ContinuityUnproven,
            "the name of the failed record stays sealed");
    // (c) a second application claim for one local is refused before any
    // receiver is examined: the claim is per (application, local).
    auto second_claim = fixture.CreateRequest("pkg.srtc.partial.claim", RecordRole::Client, true);
    auto claimed = fixture.Build(second_claim);
    RequireStatus(claimed->Prepare(), NativeStatus::None, "prepare a second claim");
    fixture.Arm(42);
    RequireStatus(claimed->Connect(), NativeStatus::None, "connect a second claim");
    RequireStatus(claimed->Publish(), NativeStatus::ExistingPending,
            "a second application claim for the same local is refused");
    RequireEqual(claimed->Observe().receive.records, 0, "no payload was retained");
    RequireStatus(claimed->Abandon(), NativeStatus::None, "roll the refused claim back");
    // (d) publication fails at the receiver: the claim succeeds, the receiver
    // does not exist. This needs its own fixture, because one local has one
    // application claim.
    Fixture receiver_fixture;
    auto no_receiver = receiver_fixture.CreateRequest("pkg.srtc.partial.receiver", RecordRole::Client, false);
    no_receiver.receiver = nullptr;
    auto missing = receiver_fixture.Build(no_receiver);
    RequireStatus(missing->Prepare(), NativeStatus::None, "prepare without a receiver");
    receiver_fixture.Arm(43);
    RequireStatus(missing->Connect(), NativeStatus::None, "connect without a receiver");
    RequireStatus(missing->Publish(), NativeStatus::InvalidConfiguration,
            "publication without a receiver is refused");
    RequireEqual(missing->Observe().admitted, 0, "no ingress was ever admitted");
    receiver_fixture.RequireReceiveBudgetZero("no payload was retained by the failed publication");
    RequireEqual(missing->Observe().owns_application, false, "the failed publication released its claim");
    // The claim was installed before the receiver failed, so the rollback must
    // detach it: a dropped handle would leave the application Active and this
    // local permanently unclaimable.
    {
        const auto usage = receiver_fixture.attachments->SnapshotUsage();
        RequireEqual(usage.active_applications, 0, "the failed publication left no active application");
        RequireEqual(usage.retained_applications, 0, "the failed publication retained no application");
        auto again = receiver_fixture.attachments->ClaimApplication(
                std::vector<Locator>{receiver_fixture.local}, receiver_fixture.snapshot);
        Require(again.result == ApplicationClaimResult::Claimed,
                "the local is claimable again after the failed publication");
    }
    RequireStatus(missing->Abandon(), NativeStatus::None, "roll the failed publication back");
    // The healthy binding is untouched by every failure above.
    fixture.Emit(*first, 41, PayloadBytes());
    RequireEqual(first->Observe().admitted, 1, "the healthy binding still admits");
    RequireEqual(fixture.Drain(*first), 1, "the healthy binding still delivers");
    RequireEqual(fixture.receiver->Count(), 1, "the healthy binding's delivery arrived");
}

// C4: an admission fixes the original owner, and a closed owner refuses new
// admission rather than letting the work be re-attributed.
void CloseVersusCapture()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.capture", RecordRole::Client, false), 51);
    fixture.Emit(*binding, 51, PayloadBytes());
    const auto admitted = binding->Observe();
    RequireEqual(admitted.admitted, 1, "the callback was admitted under the live owner");
    RequireEqual(admitted.callbacks_in_flight, 0, "the ingress call returned");
    RequireEqual(admitted.owner_callback_inflight, 0, "the native lease was released");
    RequireEqual(admitted.receive.queued, 1, "the admitted work is queued, not run");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "stop new admission");
    const auto refused_before = binding->Observe().refused_phase;
    const auto dropped = Table().SnapshotUsage().dropped_unknown_socket;
    const auto unanchored = Table().SnapshotUsage().dropped_unanchored;
    fixture.Emit(*binding, 51, PayloadBytes());
    RequireEqual(binding->Observe().refused_phase - refused_before, 1,
            "a closing binding refuses new ingress at the composition boundary");
    RequireEqual(Table().SnapshotUsage().dropped_unknown_socket - dropped, 0,
            "the record still attributes it: the refusal is the binding's, not the table's");
    // The work admitted before the close is still served by its original owner
    // and delivered to its original receiver.
    RequireEqual(fixture.Drain(*binding), 1, "already admitted work still runs");
    RequireEqual(fixture.receiver->Count(), 1, "and still reaches its original receiver");
    const auto kept_entrance = binding->Entrance();
    RequireStatus(binding->Abandon(), NativeStatus::None, "release the binding");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "one close, claimed once");
    Fixture::EmitPointer(kept_entrance, 51, PayloadBytes());
    // With its only membership released the record is UNANCHORED, so the
    // terminal drop is counted there, before any owner lookup.
    RequireEqual(Table().SnapshotUsage().dropped_unanchored - unanchored, 1,
            "once released, the socket's callbacks are terminal drops");
    RequireEqual(Table().SnapshotUsage().dropped_unknown_socket - dropped, 0,
            "the table never reaches an owner lookup for an unanchored record");
    RequireEqual(fixture.receiver->Count(), 1, "and no delivery enters the retired receiver");
}

// C5: admitted but not yet run counts toward the drain, so a quiet run cannot
// make a close look complete.
void AdmittedButNotRun()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.drain", RecordRole::Client, false), 61);
    fixture.Emit(*binding, 61, PayloadBytes());
    RequireEqual(binding->Observe().receive.queued, 1, "one admitted record is queued");
    RequireEqual(binding->Observe().receive.delivering, 0, "nothing is running");
    Require(!binding->DrainComplete(), "an admitted but unrun record keeps the drain incomplete");
    RequireRecord(binding->RetireRecord(), RecordStatus::StillAnchored,
            "retirement refuses a binding that is not closed");
    RequireEqual(fixture.Drain(*binding), 1, "the queued work runs");
    RequireEqual(fixture.receiver->Count(), 1, "and reaches the receiver");
    Require(binding->DrainComplete(), "the drain completes once the admitted work returned");
    // A second round: closing with work still queued keeps that work admissible.
    fixture.Emit(*binding, 61, PayloadBytes());
    RequireEqual(binding->Observe().receive.queued, 1, "one more record is queued");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "close while work is queued");
    Require(!binding->DrainComplete(), "closing does not drop queued work");
    RequireEqual(fixture.Drain(*binding), 1, "the queued work still runs after the close");
    RequireEqual(fixture.receiver->Count(), 2, "and still reaches the receiver");
    Require(binding->DrainComplete(), "the drain completes");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release");
}

// C6: a close waits for a receiver that is already running, and no delivery
// enters the retired receiver afterwards. The barrier is the receiver itself, and
// the "no delivery" claims are checked against an event counter that lives
// outside the receiver, with a real late ingress between the two readings.
void CloseWaitsForRunningReceiver()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.running", RecordRole::Client, false), 71);
    fixture.Emit(*binding, 71, PayloadBytes(24));
    std::mutex gate;
    std::condition_variable signal;
    bool running = false, release = false;
    bool closing_returned = false, release_started = false, allow_release = false;
    std::atomic<bool> returned{false};
    // The event record this case judges by: it belongs to the test, not to the
    // receiver object, so it cannot be confused with a receiver reading.
    std::atomic<int> entered{0};
    fixture.receiver->on_receive = [&] {
        entered.fetch_add(1);
        std::unique_lock<std::mutex> lock(gate);
        running = true;
        signal.notify_all();
        signal.wait(lock, [&] { return release; });
        returned.store(true);
    };
    auto invocation = binding->NextAdmitted();
    Require(static_cast<bool>(invocation), "take the admitted work");
    std::thread runner([&] { (void)binding->RunAdmitted(*invocation.value); });
    {
        std::unique_lock<std::mutex> lock(gate);
        signal.wait(lock, [&] { return running; });
    }
    Require(!returned.load(), "the receiver is inside its callback");
    const auto kept_entrance = binding->Entrance();
    std::atomic<bool> abandoned{false};
    // The control thread reports its two statuses for the test thread to judge:
    // an assertion thrown here would abort the process instead of failing the
    // case, and a build that swallowed it would look green.
    std::atomic<int> close_status{static_cast<int>(NativeStatus::InvalidHandle)};
    std::atomic<int> release_status{static_cast<int>(NativeStatus::InvalidHandle)};
    std::thread closer([&] {
        // The control close returns first, while the delivery is still inside the
        // receiver; only then is the release allowed to start, so the late
        // ingress below really lands on a closed but still registered binding.
        close_status.store(static_cast<int>(binding->BeginClose()));
        {
            std::lock_guard<std::mutex> lock(gate);
            closing_returned = true;
        }
        signal.notify_all();
        {
            std::unique_lock<std::mutex> lock(gate);
            signal.wait(lock, [&] { return allow_release; });
            release_started = true;
        }
        signal.notify_all();
        release_status.store(static_cast<int>(binding->Abandon()));
        abandoned.store(true);
    });
    {
        std::unique_lock<std::mutex> lock(gate);
        signal.wait(lock, [&] { return closing_returned; });
    }
    // A real ingress on the live entrance, after the close and before the
    // release: the composition refuses it, the record table still attributes it,
    // and nothing is queued or delivered. The refusing counter is what can fail
    // here (a binding that still admitted would leave it unchanged), and the queue
    // reading rules out a refusal that quietly queued the record anyway.
    const auto refused_before = binding->Observe().refused_phase;
    const auto entered_before = entered.load();
    const auto usage_before_late_call = binding->Observe().receive;
    fixture.Emit(*binding, 71, PayloadBytes(24));
    RequireEqual(binding->Observe().refused_phase - refused_before, 1,
            "a closed binding refuses new ingress at the composition boundary");
    // The receiver-entry counter cannot move here (nothing was admitted, so nothing
    // can run a receiver); it is recorded, not used as an oracle. What fails if this
    // refusal is broken is the refusing counter above and the queue reading below.
    RequireEqual(entered.load(), entered_before, "and no receiver entry was made (recorded, not an oracle)");
    RequireEqual(binding->Observe().receive.queued, usage_before_late_call.queued,
            "and the refused ingress queued nothing");
    {
        std::lock_guard<std::mutex> lock(gate);
        allow_release = true;
    }
    signal.notify_all();
    {
        std::unique_lock<std::mutex> lock(gate);
        signal.wait(lock, [&] { return release_started; });
    }
    // The closer is inside its release. The receiver is still blocked, so a
    // release that returned here would prove it did not wait for it.
    std::this_thread::yield();
    Require(!returned.load(), "the receiver is still inside its callback");
    Require(!abandoned.load(), "the release has not completed while the receiver runs");
    {
        std::lock_guard<std::mutex> lock(gate);
        release = true;
    }
    signal.notify_all();
    runner.join();
    closer.join();
    Require(returned.load(), "the delivery finished");
    RequireStatus(static_cast<NativeStatus>(close_status.load()), NativeStatus::None,
            "close from the control thread succeeded");
    RequireStatus(static_cast<NativeStatus>(release_status.load()), NativeStatus::None,
            "release from the control thread succeeded");
    // Not a false fix that drops everything: the work admitted before the close
    // ran and reached the receiver exactly once.
    RequireEqual(static_cast<std::size_t>(entered.load()), 1,
            "the work admitted before the close was delivered, exactly once");
    RequireEqual(fixture.receiver->Count(), 1, "and reached the receiver body");
    RequireStatus(binding->BeginClose(), NativeStatus::ServiceClosing, "the binding is already closed");
    // After the whole release, a call on the pointer the SDK could still hold must
    // produce nothing at all. A receiver entry counter alone could not show that:
    // only a delivery runs a receiver, so the assertions that can fail are the
    // record's terminal-drop count, the absence of any queued work afterwards, and
    // the reported usage. A late call that wrongly queued a record would fail the
    // last two.
    const auto entered_after_release = entered.load();
    const auto dropped = Table().SnapshotUsage().dropped_unanchored;
    const auto usage_after_release = binding->Observe().receive;
    Fixture::EmitPointer(kept_entrance, 71, PayloadBytes(24));
    RequireEqual(Table().SnapshotUsage().dropped_unanchored - dropped, 1,
            "the late call is a terminal drop at the record");
    RequireEqual(static_cast<std::size_t>(entered.load()), static_cast<std::size_t>(entered_after_release),
            "and it entered no receiver (recorded; the oracles here are the drop count and the queue)");
    const auto usage_after_late_call = binding->Observe().receive;
    RequireEqual(usage_after_late_call.queued, usage_after_release.queued,
            "and it left nothing queued");
    RequireEqual(usage_after_late_call.records, usage_after_release.records,
            "and it retained no record");
    Require(!static_cast<bool>(binding->NextAdmitted()),
            "and there is no admitted work to run for it");
}

// Saturate the unchanged table-wide single close credit behind a provider barrier.
void CloseCredit(bool separate_record)
{
    Fixture f;
    auto a=f.Deploy(f.CreateRequest("pkg.credit.a",RecordRole::Client,false),81);
    ApplicationAttachments other({f.local});
    std::mutex gate;std::condition_variable cv;bool first_entered=false,release=false,second_ready=false;
    ServiceRecordBindingTestAccess::Hooks hooks;
    hooks.before_group_settle=[&]{std::lock_guard<std::mutex> l(gate);second_ready=true;cv.notify_all();};
    auto request=separate_record?f.CreateRequest("pkg.credit.b",RecordRole::Client,true):f.JoinRequest(*a,RecordRole::Client,true);
    f.Arm(82);
    auto built=ServiceRecordBindingTestAccess::BuildWithHooks(ServiceRecordTable::Process(),*f.native,separate_record?other:*f.attachments,f.provider,request,std::move(hooks));
    Require(built.issue.code==NativeStatus::None,"build close credit sibling");auto b=std::move(*built.value);
    RequireStatus(b->Prepare(),NativeStatus::None,"prepare close credit");RequireStatus(b->Connect(),NativeStatus::None,"connect close credit");RequireStatus(b->Publish(),NativeStatus::None,"publish close credit");
    f.provider.shutdown_hook=[&](NativeSocketId id){if(id==81){std::unique_lock<std::mutex> l(gate);first_entered=true;cv.notify_all();cv.wait(l,[&]{return release;});}};
    auto first=std::async(std::launch::async,[&]{auto status=a->BeginClose();return status==NativeStatus::None?a->Abandon():status;});
    bool entered=false;{std::unique_lock<std::mutex> l(gate);entered=cv.wait_for(l,std::chrono::seconds(2),[&]{return first_entered;});}
    auto second=std::async(std::launch::async,[&]{auto status=b->BeginClose();return status==NativeStatus::None?b->Abandon():status;});
    bool reached=false;{std::unique_lock<std::mutex> l(gate);reached=cv.wait_for(l,std::chrono::seconds(2),[&]{return second_ready;});}
    const bool premature=second.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    {std::lock_guard<std::mutex> l(gate);release=true;}cv.notify_all();auto x=first.get(),y=second.get();f.provider.shutdown_hook={};
    Require(entered&&reached,"both deterministic release boundaries reached");
    Require(!premature,"busy close credit must not be silently abandoned");
    RequireStatus(x,NativeStatus::None,"first close complete");RequireStatus(y,NativeStatus::None,"second close complete");
    RequireEqual(f.provider.shutdown,2,"two original owners closed after capacity released");
}
void CloseCreditSameRecord(){CloseCredit(false);}
void CloseCreditDifferentRecord(){CloseCredit(true);}
void ShutdownReentrantSibling()
{
    Fixture f;auto a=f.Deploy(f.CreateRequest("pkg.credit.reenter",RecordRole::Client,false),81);auto b=f.Deploy(f.JoinRequest(*a,RecordRole::Client,true),82);
    NativeStatus request=NativeStatus::WrongState,begin=NativeStatus::None,abandon=NativeStatus::None,deferred=NativeStatus::None;
    f.provider.shutdown_hook=[&](NativeSocketId id){if(id==81){(void)b->Observe();request=b->CloseRequest();begin=b->BeginClose();abandon=b->Abandon();deferred=b->DeferredClose();}};
    RequireStatus(a->BeginClose(),NativeStatus::None,"outer request");RequireStatus(a->Abandon(),NativeStatus::None,"outer close");f.provider.shutdown_hook={};
    RequireStatus(request,NativeStatus::None,"nonwaiting reentrant close request remains legal");
    RequireStatus(begin,NativeStatus::WrongState,"provider reentrant begin refused");RequireStatus(abandon,NativeStatus::WrongState,"provider reentrant abandon refused");RequireStatus(deferred,NativeStatus::WrongState,"provider reentrant deferred refused");
    RequireStatus(b->DeferredClose(),NativeStatus::None,"external sibling completes");RequireEqual(f.provider.shutdown,2,"no recursive close stolen");
}

// C7: each owner's close responsibility is claimed exactly once, including when
// two owners are closed from two threads.
void UniqueCloseResponsibility()
{
    Fixture fixture;
    auto a = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close", RecordRole::Client, false), 81);
    auto b = fixture.Deploy(fixture.JoinRequest(*a, RecordRole::Client, true), 82);
    fixture.Emit(*a, 81, PayloadBytes());
    fixture.Emit(*b, 82, PayloadBytes());
    RequireEqual(a->Observe().admitted + b->Observe().admitted, 2, "both owners admitted");
    RequireEqual(fixture.Drain(*a), 2, "both admitted records run before the close");
    std::atomic<int> failures{0};
    std::thread first([&] {
        if (a->BeginClose() != NativeStatus::None || a->Abandon() != NativeStatus::None) ++failures;
    });
    std::thread second([&] {
        if (b->BeginClose() != NativeStatus::None || b->Abandon() != NativeStatus::None) ++failures;
    });
    first.join();
    second.join();
    RequireEqual(static_cast<std::size_t>(failures.load()), 0, "both closes succeeded");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2, "two sockets, two closes, no more");
    // A second release is settled, not repeated.
    RequireStatus(a->Abandon(), NativeStatus::AlreadySettled, "release is settled once");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2, "no repeat settlement");
    RequireEqual(a->Observe().membership ? 1u : 0u, 0u, "A released its membership");
    RequireEqual(b->Observe().membership ? 1u : 0u, 0u, "B released its membership");
    fixture.RequireReceiveBudgetZero("no payload outlives the close");
}

// C8: the call boundary reenters legal queries, and refuses the control calls
// that the contract forbids instead of deadlocking.
void ReentrantBoundary()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.reentrant", RecordRole::Client, false), 91);
    // (a) from inside a delivery: legal queries work, control calls are refused.
    NativeStatus observed_phase = NativeStatus::None;
    NativeStatus close_status = NativeStatus::None;
    NativeStatus abandon_status = NativeStatus::None;
    std::size_t observed_members = 0;
    fixture.receiver->on_receive = [&] {
        const auto view = binding->Observe();
        observed_phase = view.status;
        observed_members = view.role_members;
        close_status = binding->BeginClose();
        abandon_status = binding->Abandon();
    };
    fixture.Emit(*binding, 91, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran");
    RequireStatus(observed_phase, NativeStatus::None, "a query from inside the delivery is not deadlocked");
    RequireEqual(observed_members, 1, "and reports the live membership");
    RequireStatus(close_status, NativeStatus::WrongState, "closing from inside a delivery is refused");
    RequireStatus(abandon_status, NativeStatus::WrongState, "releasing from inside a delivery is refused");
    fixture.receiver->on_receive = nullptr;
    // The refusals changed nothing: the binding still works.
    fixture.Emit(*binding, 91, PayloadBytes());
    RequireEqual(binding->Observe().admitted, 2, "the binding still admits after the refused calls");
    RequireEqual(fixture.Drain(*binding), 1, "and still delivers");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "closing from the control context is accepted");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 0, "no shutdown before release");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "one shutdown");
}

// C9: an exceptional admission releases every in-flight count and delivers
// nothing. The assertions live in the test thread, never in a swallowed hook, and
// they read the attachments this binding actually drives.
void ExceptionalAdmission()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.exception", RecordRole::Client, false), 101);
    const auto before = binding->Observe();
    const auto table_before = Table().SnapshotUsage();
    RequireEqual(before.receive.copying, 0, "no copy is in flight at the baseline");
    RequireEqual(before.receive.bytes, 0, "no bytes are charged at the baseline");
    const auto payload = PayloadBytes();
    deny_allocations = true;
    fixture.Emit(*binding, 101, payload);
    deny_allocations = false;
    const auto after = binding->Observe();
    const auto table_after = Table().SnapshotUsage();
    RequireEqual(after.callbacks_in_flight, 0, "no attributed callback stayed in flight");
    RequireEqual(after.owner_callback_inflight, 0, "the native owner released its in-flight count");
    RequireEqual(after.receive.records, 0, "no payload survived the failure");
    RequireEqual(after.receive.queued, 0, "nothing was queued");
    // The admission really charged a copying record and its bytes on this
    // attachments object before it failed, and the failure returned both: a
    // refund that skipped either counter would leave a residue here.
    Require(after.receive.high_copying >= 1, "the admission charged a copying record");
    RequireEqual(after.receive.copying, 0, "the failed admission left no copying record");
    RequireEqual(after.receive.bytes, 0, "the failed admission left no charged bytes");
    RequireEqual(after.receive.taken, 0, "nothing was taken");
    RequireEqual(after.receive.delivering, 0, "nothing was delivering");
    RequireEqual(fixture.receiver->Count(), 0, "the receiver was never entered");
    const auto refused = (after.refused_receive - before.refused_receive) +
            (table_after.callback_exceptions - table_before.callback_exceptions);
    RequireEqual(refused, 1, "exactly one refusal or absorbed exception accounted the attempt");
    RequireEqual(Table().SnapshotUsage().attributed - table_before.attributed, 1,
            "the record table still attributed the callback it admitted");
    // The binding recovers: the next ingress is admitted normally, and that
    // admission owns the bytes it charges until its work is run.
    fixture.Emit(*binding, 101, PayloadBytes(48));
    const auto queued = binding->Observe();
    RequireEqual(binding->Observe().admitted, before.admitted + 1, "the binding recovers after the failure");
    RequireEqual(queued.receive.queued, 1, "the recovered admission is queued");
    RequireEqual(queued.receive.records, 1, "and owns exactly one record");
    RequireEqual(queued.receive.bytes, 48, "and owns exactly its own bytes");
    RequireEqual(queued.receive.copying, 0, "the copy is complete, so nothing is copying");
    RequireEqual(queued.receive.delivering, 0, "and nothing is delivering yet");
    RequireEqual(fixture.Drain(*binding), 1, "and delivers");
    RequireEqual(binding->Observe().receive.bytes, 0, "the delivered record returned its bytes");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release");
    fixture.RequireReceiveBudgetZero("the attachments are empty after the release");
}

// C10: after the explicit model retirement boundary, a same-name record gets a
// new incarnation and the old entrance cannot reach it.
void OldEntryNewIncarnation()
{
    Fixture fixture;
    auto old_binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.replace", RecordRole::Client, false), 111);
    const auto old_entrance = old_binding->Entrance();
    const auto old_record = Table().Observe(old_binding->RoleEntrance(RecordRole::Client));
    fixture.Emit(*old_binding, 111, PayloadBytes());
    RequireEqual(fixture.Drain(*old_binding), 1, "the old record delivers");
    RequireStatus(old_binding->BeginClose(), NativeStatus::None, "close the old binding");
    RequireStatus(old_binding->Abandon(), NativeStatus::None, "release the old binding");
    RequireDrained(*old_binding, "the old binding drains before retirement");
    {
        // A release must leave nothing pending on the instance it used.
        const auto usage = fixture.attachments->SnapshotUsage();
        RequireEqual(usage.active_applications, 0, "the release left no active application");
        RequireEqual(usage.detaching_applications, 0, "the release left no application mid-detach");
        RequireEqual(usage.records, 0, "the release left no payload");
    }
    RequireRecord(old_binding->RetireRecord(), RecordStatus::None, "the explicit boundary retires the record");
    const auto retired = Table().SnapshotUsage().dropped_retired;
    Fixture::EmitPointer(old_entrance, 111, PayloadBytes());
    RequireEqual(Table().SnapshotUsage().dropped_retired - retired, 1, "the old entrance is a terminal drop");
    // The retired record's successor is a new Participant-shaped instance: its
    // own native table and attachments, exactly as C11's rounds build them.
    auto fresh = fixture.DeployFresh("pkg.srtc.replace", 112);
    const auto fresh_record = Table().Observe(fresh->RoleEntrance(RecordRole::Client));
    Require(fresh->Entrance() != old_entrance, "the replacement never inherits the retired entrance");
    Require(fresh_record.slot != old_record.slot, "the replacement uses a new slot");
    Require(fresh_record.record_incarnation != old_record.record_incarnation, "and a new incarnation");
    fixture.Emit(*fresh, 112, PayloadBytes());
    RequireEqual(fresh->Observe().admitted, 1, "the new entrance works, so the protection is not a blanket refusal");
    RequireEqual(fixture.Drain(*fresh), 1, "and delivers to the new receiver");
    const auto dropped_for_old = Table().SnapshotUsage().dropped_unknown_socket;
    fixture.Emit(*fresh, 111, PayloadBytes());
    RequireEqual(Table().SnapshotUsage().dropped_unknown_socket - dropped_for_old, 1,
            "the retired record's socket is not a member of the new one");
}

// C11: repeated lifecycles inside one process and one management instance, with
// the business resources returning to zero each round. Slots are consumed by
// design; nothing dynamic is.
void RepeatedLifecycles()
{
    Fixture fixture;
    std::size_t previous_slots = Table().SnapshotUsage().slots_tombstoned;
    for (int round = 0; round < 4; ++round) {
        auto binding = fixture.DeployFresh("pkg.srtc.cycle", 121);
        // The attachments this round actually drives, not a spare one: with the
        // per-round management instance, the fixture member is never used here.
        auto& round_attachments = fixture.NewestAttachments();
        fixture.Emit(*binding, 121, PayloadBytes(64, static_cast<std::uint8_t>(round)));
        // The round really charges and returns its own payload on that object.
        const auto charged = round_attachments.SnapshotUsage();
        RequireEqual(charged.queued, 1, "each round queues exactly one record");
        RequireEqual(charged.bytes, 64, "each round charges exactly its own bytes");
        RequireEqual(charged.copying, 0, "and the copy is complete");
        RequireEqual(charged.active_applications, 1, "each round holds exactly its own application");
        RequireEqual(fixture.Drain(*binding), 1, "each round delivers");
        const auto usage = binding->Observe().receive;
        RequireEqual(usage.records, 0, "no payload is retained after the round");
        RequireEqual(usage.bytes, 0, "no bytes are retained after the round");
        RequireStatus(binding->BeginClose(), NativeStatus::None, "close each round");
        RequireStatus(binding->Abandon(), NativeStatus::None, "release each round");
        RequireDrained(*binding, "each round drains before retirement");
        RequireRecord(binding->RetireRecord(), RecordStatus::None, "retire each round");
        // Nothing dynamic may accumulate across rounds: the round's own
        // attachments are empty, and its record slot is the only thing retained.
        Fixture::RequireAttachmentsEmpty(round_attachments,
                "the round's own attachments are empty after every round");
        const auto observed = Table().Observe(binding->RoleEntrance(RecordRole::Client));
        Require(!observed.sink_attached, "the retired record holds no sink registration");
        Require(observed.phase == RecordPhase::Tombstoned, "and stays a tombstone");
        const auto now = Table().SnapshotUsage();
        RequireEqual(now.slots_tombstoned, previous_slots + 1,
                "each round consumes exactly one record slot, by design");
        previous_slots = now.slots_tombstoned;
        RequireEqual(now.owners, 0, "the record table holds no owners between rounds");
        RequireEqual(now.callbacks_in_flight, 0, "and no attributed callback between rounds");
        RequireEqual(fixture.receiver->Count(), round + 1, "every round delivered to the live receiver");
    }
    RequireEqual(fixture.receiver.use_count(), 1, "the attachments never took ownership of the receiver");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 4, "one close per round");
    // Four rounds of dynamic use retained one bounded record tombstone each and no
    // dynamic object at all. The fixture's own attachments member is deliberately
    // unused here (every round is charged and settled on its own instance), so it
    // is not asserted on: an assertion about an object nothing charges could not
    // fail, which is exactly the defect this case used to have.
}

// C12: slot exhaustion is decided before exposure, and reusing a live record's
// role costs no slot.
void SlotExhaustion()
{
    Fixture fixture;
    std::vector<std::unique_ptr<ServiceRecordBinding>> live;
    const auto start = Table().SnapshotUsage();
    const auto remaining = start.slots_free;
    for (std::size_t i = 0; i < remaining; ++i) {
        live.push_back(fixture.DeployFresh("pkg.srtc.slot." + std::to_string(i),
                130 + static_cast<NativeSocketId>(i)));
    }
    RequireEqual(Table().SnapshotUsage().slots_free, 0, "every slot is consumed");
    const auto created = Table().SnapshotUsage().records_created;
    const int provider_calls = fixture.provider.created.load();
    auto overflow = fixture.Build(fixture.CreateRequest("pkg.srtc.slot.overflow", RecordRole::Client, true));
    RequireStatus(overflow->Prepare(), NativeStatus::AdmissionSaturated,
            "a new incarnation is refused before exposure");
    RequireEqual(Table().SnapshotUsage().records_created, created, "the refusal created no record");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), static_cast<std::size_t>(provider_calls),
            "the refusal reached no provider");
    // A live record's role still accepts another owner without a new slot. The
    // joiner needs its own native table, because the record it joins lives on
    // one of the per-record tables above.
    auto& joiner_native = fixture.AddNative();
    fixture.Arm(150);
    auto joined_result = ServiceRecordBinding::Build(ServiceRecordTable::Process(), joiner_native,
            fixture.NewestAttachments(), fixture.provider,
            fixture.JoinRequest(*live[0], RecordRole::Client, true));
    Require(joined_result.issue.code == NativeStatus::None, "build the joiner");
    auto joined = std::move(*joined_result.value);
    RequireStatus(joined->Prepare(), NativeStatus::None, "joining a live role does not need a slot");
    RequireEqual(Table().SnapshotUsage().slots_free, 0, "and consumes none");
    RequireStatus(joined->Connect(), NativeStatus::None, "connect the joiner");
    RequireStatus(joined->Publish(), NativeStatus::None, "publish the joiner");
    fixture.Emit(*joined, 150, PayloadBytes());
    if (joined->Observe().admitted != 1) Dump(*joined, "joiner");
    RequireEqual(joined->Observe().admitted, 1, "the joiner admits");
    RequireEqual(fixture.Drain(*joined), 1, "the joiner delivers");
}

// C13: the sink, receiver and payload are released, and only declared bounded
// metadata survives.
void ResourceRelease()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.release", RecordRole::Client, false), 141);
    fixture.Emit(*binding, 141, PayloadBytes(128));
    RequireEqual(binding->Observe().receive.bytes, 128, "the payload is charged while queued");
    RequireEqual(fixture.receiver.use_count(), 1, "the binding never owns the receiver");
    RequireEqual(fixture.Drain(*binding), 1, "the delivery runs");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "close");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release");
    const auto view = binding->Observe();
    Require(!view.membership, "the membership is released");
    Require(!view.application_bound, "the application claim is released");
    Require(!view.receiver_open, "the receiver registration is released");
    Require(!view.owns_application, "this binding no longer owns an application registration");
    RequireEqual(view.receive.logical_slots, 1, "the configured logical slot count is unchanged");
    // With its last binding gone the record has no user of its ingress left, so
    // it ends the sink registration instead of holding a freed binding's state.
    Require(!Table().Observe(binding->RoleEntrance(RecordRole::Client)).sink_attached,
            "the record released its sink registration with its last binding");
    fixture.RequireReceiveBudgetZero("no payload survives the release");
    RequireEqual(fixture.receiver.use_count(), 1, "still no ownership of the receiver");
    // What remains is declared, bounded metadata: the tombstoned record slot and
    // the native owner cell, neither of which carries business state.
    RequireDrained(*binding, "the binding drains before retirement");
    RequireRecord(binding->RetireRecord(), RecordStatus::None, "retire the record at the explicit boundary");
    const auto table = Table().SnapshotUsage();
    Require(table.slots_tombstoned >= 1, "the retired slot stays a tombstone, never recycled");
    RequireEqual(view.callbacks_in_flight, 0, "no ingress is still in flight");
}

// C14: the composition performs exactly the provider calls it was asked to, and
// nothing else: no SDK call, no hidden worker, no extra socket.
void FailClosedDefault()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.fail_closed", RecordRole::Client, false), 151);
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 1, "exactly one socket call");
    RequireEqual(static_cast<std::size_t>(fixture.provider.bound), 1, "exactly one bind call");
    RequireEqual(static_cast<std::size_t>(fixture.provider.listened), 0, "no listener call for a client role");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 0, "no shutdown before release");
    // A refused deployment performs nothing.
    auto refused = fixture.Build(fixture.CreateRequest("pkg.srtc.fail_closed", RecordRole::Client, true));
    RequireStatus(refused->Prepare(), NativeStatus::ExistingPending, "the duplicate name is refused");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 1, "a refused deployment calls nothing");
    // A provider failure is reported and nothing is published.
    auto failing = fixture.CreateRequest("pkg.srtc.fail_closed.socket", RecordRole::Client, true);
    fixture.provider.socket = NativeSocketResult::Error(-1);
    auto failed = fixture.Build(failing);
    RequireStatus(failed->Prepare(), NativeStatus::None, "prepare succeeds without a socket");
    fixture.provider.socket = NativeSocketResult::Error(-1);
    RequireStatus(failed->Connect(), NativeStatus::OwnerUnavailable, "a socket failure is reported");
    RequireStatus(failed->Publish(), NativeStatus::WrongState, "a failed deployment cannot publish");
    RequireEqual(failed->Observe().admitted, 0, "nothing was admitted");
    fixture.provider.socket = NativeSocketResult::Id(11);
    RequireStatus(failed->Abandon(), NativeStatus::None, "the failed deployment is released");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 0,
            "no shutdown is invented for a socket that was never created");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "close the healthy binding");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release the healthy binding");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "exactly one real close");
}
// R-A: the creator is destroyed while a joiner of the same record and role still
// serves its socket. Nothing keeps the creator alive to make that true, and the
// record's own dispatch must keep reaching the joiner.
void CreatorExitsJoinerLives()
{
    Fixture fixture;
    auto a = fixture.Deploy(fixture.CreateRequest("pkg.srtc.creator_exit", RecordRole::Client, false), 161);
    auto b = fixture.Deploy(fixture.JoinRequest(*a, RecordRole::Client, true), 162);
    const auto record_entrance = a->RoleEntrance(RecordRole::Client);
    const auto record_callback = a->Entrance();
    Require(SameEntrance(record_callback, b->RoleEntrance(RecordRole::Client)),
            "both bindings serve the record's own issued entrance");
    // The creator leaves under its legal contract: close, release, destroy.
    RequireStatus(a->BeginClose(), NativeStatus::None, "close the creator");
    RequireStatus(a->Abandon(), NativeStatus::None, "release the creator");
    a.reset();
    // The joiner is still served, through the entrance the record issued.
    const auto before = b->Observe();
    fixture.Emit(*b, 162, PayloadBytes());
    RequireEqual(b->Observe().admitted, before.admitted + 1,
            "the joiner still admits after the creator is gone");
    RequireEqual(fixture.Drain(*b), 1, "and still delivers");
    RequireEqual(fixture.receiver->Count(), 1, "the delivery reached the receiver");
    // The creator's socket left the record with the creator: a late call on it is
    // a terminal drop, never a delivery.
    const auto dropped = Table().SnapshotUsage().dropped_unknown_socket;
    Fixture::EmitPointer(record_callback, 161, PayloadBytes());
    RequireEqual(Table().SnapshotUsage().dropped_unknown_socket - dropped, 1,
            "the creator's socket is no longer a member of the record");
    RequireStatus(b->BeginClose(), NativeStatus::None, "close the joiner");
    RequireStatus(b->Abandon(), NativeStatus::None, "release the joiner");
    // Independent lifecycle criterion: with its last legitimate user gone the
    // record holds no sink registration at all, so nothing can call into a
    // released binding's dispatch state.
    const auto record = Table().Observe(record_entrance);
    Require(record.phase == RecordPhase::Unanchored, "the record has no anchor left");
    Require(!record.sink_attached, "the record released its sink with its last binding");
    fixture.RequireReceiveBudgetZero("no payload outlives the last release");
}

// R-C: the delivery-context guard belongs to the delivery, not to the dispatch
// entry that produced it. The composition's delivery handle has no execution
// entry of its own, so the guard cannot be run around; the cases below drive the
// remaining reachable shapes: the legal one, a delivery handed to a binding that
// did not issue it, a moved-from handle, and a repeat run.
void DeliveryContextBypass()
{
    Fixture fixture;
    auto creator = fixture.Deploy(fixture.CreateRequest("pkg.srtc.context", RecordRole::Client, false), 171);
    auto joiner = fixture.Deploy(fixture.JoinRequest(*creator, RecordRole::Client, true), 172);
    // The legal shape: a control call made from inside a delivery is refused
    // instead of deadlocking, and a legal query still works.
    NativeStatus observed_phase = NativeStatus::InvalidHandle;
    NativeStatus close_status = NativeStatus::InvalidHandle;
    NativeStatus abandon_status = NativeStatus::InvalidHandle;
    std::size_t observed_members = 0;
    fixture.receiver->on_receive = [&] {
        const auto view = creator->Observe();
        observed_phase = view.status;
        observed_members = view.role_members;
        close_status = creator->BeginClose();
        abandon_status = creator->Abandon();
    };
    fixture.Emit(*creator, 171, PayloadBytes());
    auto first = creator->NextAdmitted();
    Require(static_cast<bool>(first), "the admitted work was taken");
    // A delivery belongs to the binding that issued it: another binding of the
    // same record refuses it, and refusing it does not consume it.
    RequireDelivery(joiner->RunAdmitted(*first.value), InputDeliveryResult::InvalidTicket,
            "a delivery issued by another binding is refused by this one");
    RequireEqual(fixture.receiver->Count(), 0, "the refused delivery ran nothing");
    RequireDelivery(creator->RunAdmitted(*first.value), InputDeliveryResult::Delivered,
            "the issuing binding runs the same delivery");
    RequireStatus(observed_phase, NativeStatus::None, "a query from inside the delivery is not deadlocked");
    RequireEqual(observed_members, 2, "and reports both live memberships");
    RequireStatus(close_status, NativeStatus::WrongState, "closing from inside a delivery is refused");
    RequireStatus(abandon_status, NativeStatus::WrongState, "releasing from inside a delivery is refused");
    fixture.receiver->on_receive = nullptr;
    // The refused calls changed nothing: the binding still works.
    fixture.Emit(*creator, 171, PayloadBytes());
    RequireEqual(creator->Observe().admitted, 2, "the binding still admits after the refused calls");
    auto second = creator->NextAdmitted();
    Require(static_cast<bool>(second), "the second admitted work was taken");
    // A moved-from handle and a repeated run are refused, in Release too.
    BindingDelivery moved = std::move(*second.value);
    RequireDelivery(creator->RunAdmitted(*second.value), InputDeliveryResult::InvalidTicket,
            "a moved-from delivery is refused");
    RequireDelivery(creator->RunAdmitted(moved), InputDeliveryResult::Delivered,
            "the moved-to delivery runs");
    RequireDelivery(creator->RunAdmitted(moved), InputDeliveryResult::InvalidTicket,
            "a delivery runs at most once");
    RequireEqual(fixture.receiver->Count(), 2, "exactly the two legal deliveries reached the receiver");
    RequireStatus(creator->BeginClose(), NativeStatus::None, "closing from the control context is accepted");
    RequireStatus(creator->Abandon(), NativeStatus::None, "release the creator");
    RequireStatus(joiner->BeginClose(), NativeStatus::None, "close the joiner");
    RequireStatus(joiner->Abandon(), NativeStatus::None, "release the joiner");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2, "one shutdown per socket");
    fixture.RequireReceiveBudgetZero("no payload outlives the two releases");
}
// R-B: two bindings of one record and one local share a single (application,
// local) registration and are released on two threads. Whichever release is
// really last must settle that registration exactly once, and no release may
// write a sibling binding's state. The interleaving here is the reviewed one:
// the first release has already chosen its successor, and the successor finishes
// its whole release before that choice is applied. Both bindings stay alive
// afterwards, so the assertion is about what the release paths left behind, not
// about what a later destructor happens to clean up.
void SharedGroupReleaseRace()
{
    Fixture fixture;
    std::mutex gate;
    std::condition_variable signal;
    bool at_decision = false, resume = false;
    ServiceRecordBindingTestAccess::Hooks hooks;
    hooks.before_group_settle = [&] {
        // A scheduling point only: it never throws, and it holds no component
        // gate while it waits.
        std::unique_lock<std::mutex> lock(gate);
        at_decision = true;
        signal.notify_all();
        signal.wait(lock, [&] { return resume; });
    };
    auto a = fixture.DeployWithHooks(fixture.CreateRequest("pkg.srtc.group_race", RecordRole::Client, false),
            181, std::move(hooks));
    auto b = fixture.Deploy(fixture.JoinRequest(*a, RecordRole::Client, true), 182);
    RequireEqual(a->Observe().receiver_open ? 1u : 0u, 1u, "the creator holds the receiver registration");
    Require(b->Observe().application_bound, "the joiner reports an application registration");
    // The premise of this case: two bindings, one shared (application, local)
    // registration. Without this the case would also pass if each binding held its
    // own, which is not the shape under test.
    {
        const auto shared = fixture.attachments->SnapshotUsage();
        RequireEqual(shared.active_applications, 1, "one shared application holds the local");
        RequireEqual(shared.retained_applications, 1, "and exactly one application object exists");
    }
    std::atomic<bool> first_done{false};
    std::thread first([&] {
        if (a->BeginClose() != NativeStatus::None || a->Abandon() != NativeStatus::None) {
            first_done.store(false);
            return;
        }
        first_done.store(true);
    });
    bool reached = false;
    {
        std::unique_lock<std::mutex> lock(gate);
        reached = signal.wait_for(lock, std::chrono::seconds(10), [&] { return at_decision; });
    }
    // The assertions from here to the join run while the first release is still
    // joinable, so a failure is recorded and thrown only after it has been
    // released and joined: an assertion thrown here would abort the process and
    // hide which check failed.
    std::string failure;
    try {
        Require(reached, "the first release reached its shared-registration decision");
        // The sibling now runs its entire release while the first release is
        // paused on that decision.
        RequireStatus(b->BeginClose(), NativeStatus::None, "close the sibling");
        RequireStatus(b->Abandon(), NativeStatus::None, "release the sibling");
    } catch (const std::exception& error) {
        failure = error.what();
    }
    {
        std::lock_guard<std::mutex> lock(gate);
        resume = true;
    }
    signal.notify_all();
    first.join();
    Require(failure.empty(), failure.empty() ? "" : failure.c_str());
    Require(first_done.load(), "the first release completed");
    // Both bindings are still alive. Nothing dynamic may survive the last
    // release: the shared registration ended with it, exactly once.
    const auto usage = fixture.attachments->SnapshotUsage();
    RequireEqual(usage.active_applications, 0, "the shared application was detached by its last user");
    RequireEqual(usage.detaching_applications, 0, "no application is left mid-detach");
    RequireEqual(usage.retained_applications, 0, "no application is retained");
    RequireEqual(fixture.receiver.use_count(), 1, "the attachments never took ownership of the receiver");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2,
            "two sockets, two close responsibilities claimed");
    fixture.RequireReceiveBudgetZero("no payload survives the two releases");
    // The local is free again, which is only true if the registration really
    // ended: a registration merely handed to a binding that will never settle it
    // would still hold the local.
    auto again = fixture.attachments->ClaimApplication(std::vector<Locator>{fixture.local},
            fixture.snapshot);
    Require(again.result == ApplicationClaimResult::Claimed,
            "the local is claimable again once every sharing binding released");
}

// R-D: a joiner must never adopt a shared registration whose last member has
// already decided to settle it. The registration is finished at that point: a
// binding that joined it would report itself bound and receiver-open against a
// receiver that is closing, and the local would be free for a third party while
// it still believed it held it. The interleaving is driven through the release
// path's own decision point.
void JoinDuringSettle()
{
    Fixture fixture;
    std::mutex gate;
    std::condition_variable signal;
    bool at_decision = false, resume = false;
    ServiceRecordBindingTestAccess::Hooks hooks;
    hooks.before_group_settle = [&] {
        std::unique_lock<std::mutex> lock(gate);
        at_decision = true;
        signal.notify_all();
        signal.wait(lock, [&] { return resume; });
    };
    auto creator = fixture.DeployWithHooks(
            fixture.CreateRequest("pkg.srtc.join_settle", RecordRole::Client, false), 191, std::move(hooks));
    auto joiner = fixture.Deploy(fixture.JoinRequest(*creator, RecordRole::Client, true), 192);
    // The first joiner leaves first, so the creator's release is the last member
    // and latches the registration as settled before it applies that.
    RequireStatus(joiner->BeginClose(), NativeStatus::None, "close the first joiner");
    RequireStatus(joiner->Abandon(), NativeStatus::None, "release the first joiner");
    const auto usage_before = fixture.attachments->SnapshotUsage();
    RequireEqual(usage_before.active_applications, 1, "the shared application is still held");
    std::atomic<bool> released{false};
    std::atomic<int> release_status{static_cast<int>(NativeStatus::InvalidHandle)};
    std::thread releaser([&] {
        release_status.store(static_cast<int>(creator->Abandon()));
        released.store(true);
    });
    bool reached = false;
    {
        std::unique_lock<std::mutex> lock(gate);
        reached = signal.wait_for(lock, std::chrono::seconds(10), [&] { return at_decision; });
    }
    // Everything from here to the join is judged on this thread and recorded, not
    // thrown: the releasing thread is still joinable, and an exception escaping
    // here would run std::thread's destructor and terminate the process instead of
    // reporting which check failed.
    std::unique_ptr<ServiceRecordBinding> late;
    std::string late_failure;
    try {
        Require(reached, "the creator's release reached its shared-registration decision");
        // Its own native owner plan, but the record's attachments: a joiner that
        // cannot adopt the registration must not be able to claim a second one
        // for a local this record already serves.
        late = fixture.BuildJoinerOn(fixture.JoinRequest(*creator, RecordRole::Client, false),
                *fixture.attachments);
        fixture.Arm(193);
        RequireStatus(late->Prepare(), NativeStatus::None, "the late joiner prepares against the live record");
        RequireStatus(late->Connect(), NativeStatus::None, "the late joiner connects");
        const auto late_publish = late->Publish();
        const auto late_view = late->Observe();
        Require(late_publish != NativeStatus::None,
                "a registration that is settling is never adopted, so the late joiner cannot publish on it");
        Require(!late_view.application_bound,
                "the late joiner is not bound to a registration that is closing");
        Require(!late_view.receiver_open, "and holds no receiver registration of its own");
    } catch (const std::exception& error) {
        late_failure = error.what();
    }
    {
        std::lock_guard<std::mutex> lock(gate);
        resume = true;
    }
    signal.notify_all();
    releaser.join();
    Require(late_failure.empty(), late_failure.empty() ? "" : late_failure.c_str());
    RequireStatus(static_cast<NativeStatus>(release_status.load()), NativeStatus::None,
            "the creator's release succeeded");
    Require(released.load(), "and completed");
    // The registration really ended, exactly once, and the local is free again.
    const auto usage_after = fixture.attachments->SnapshotUsage();
    RequireEqual(usage_after.active_applications, 0, "the shared application was detached once");
    RequireEqual(usage_after.retained_applications, 0, "and is not retained");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "the shared attachments are empty");
    auto again = fixture.attachments->ClaimApplication(std::vector<Locator>{fixture.local},
            fixture.snapshot);
    Require(again.result == ApplicationClaimResult::Claimed,
            "the local is claimable again once the settling release finished");
    RequireStatus(late->Abandon(), NativeStatus::None, "roll the late joiner back");
}

// R-E: only the binding that exposed a role may report that the registration
// attempt through its entrance produced no owner. A joiner that fails before it
// owns anything must not drop the exposing binding's pending anchor: doing so can
// seal the record, withdraw its sink and refuse the exposing binding's own
// publish while it is still alive.
void JoinerFailureKeepsCreator()
{
    Fixture fixture;
    auto creator = fixture.Build(fixture.CreateRequest("pkg.srtc.join_fail", RecordRole::Client, false));
    RequireStatus(creator->Prepare(), NativeStatus::None, "the creator exposes both roles");
    const auto client_role = creator->RoleEntrance(RecordRole::Client);
    const auto server_role = creator->RoleEntrance(RecordRole::Server);
    const auto exposed = Table().Observe(client_role);
    Require(exposed.phase == RecordPhase::Exposed, "the record is exposed");
    Require(exposed.sink_attached, "and holds its sink");
    // Two joiners whose preparation fails after the record-side work: the local
    // they ask for is not in the native table's namespace, so the namespace
    // acquisition is refused.
    const auto missing_local = MakeLocator(9);
    for (const auto role : {RecordRole::Client, RecordRole::Server}) {
        auto request = fixture.JoinRequest(*creator, role, false);
        request.local = missing_local;
        auto failing = fixture.Build(request);
        const auto status = failing->Prepare();
        Require(status != NativeStatus::None, "a join for an unconfigured local is refused");
        RequireStatus(failing->Abandon(), NativeStatus::None, "and its rollback releases what it took");
    }
    // The creator's own exposure is untouched by both failures.
    const auto after = Table().Observe(client_role);
    Require(after.phase == RecordPhase::Exposed, "the record is still exposed");
    Require(after.sink_attached, "and still holds its sink");
    RequireEqual(after.role_pending[0] ? 1u : 0u, 1u, "the client role's exposure is still pending");
    RequireEqual(after.role_pending[1] ? 1u : 0u, 1u, "and so is the server role's");
    Require(!Table().SnapshotUsage().slots_unanchored, "no record was sealed by a joiner's failure");
    // And the creator can still publish, which is what the defect took away.
    fixture.Arm(201);
    RequireStatus(creator->Connect(), NativeStatus::None, "the creator connects");
    RequireStatus(creator->Publish(), NativeStatus::None, "and publishes");
    fixture.Emit(*creator, 201, PayloadBytes());
    RequireEqual(fixture.Drain(*creator), 1, "and delivers");
    RequireStatus(creator->BeginClose(), NativeStatus::None, "close");
    RequireStatus(creator->Abandon(), NativeStatus::None, "release");
    fixture.RequireReceiveBudgetZero("nothing is left after the release");
}

// R-F: an allocation failure while the shared registration object is being built
// must not strand the claim that is already installed. The local has to be
// claimable again afterwards, on the same management object.
void RegistrationAllocationFailure()
{
    Fixture fixture;
    ServiceRecordBindingTestAccess::Hooks hooks;
    hooks.before_registration = [] { throw std::bad_alloc(); };
    // Hooks are fixed when a binding is built, so this is the binding under test.
    auto failing = fixture.BuildWithHooks(fixture.CreateRequest("pkg.srtc.reg_alloc", RecordRole::Client, false),
            std::move(hooks));
    fixture.Arm(211);
    RequireStatus(failing->Prepare(), NativeStatus::None, "prepare the fault injection");
    RequireStatus(failing->Connect(), NativeStatus::None, "connect the fault injection");
    RequireStatus(failing->Publish(), NativeStatus::AllocationFailure,
            "an allocation failure while building the shared registration is reported");
    Require(!failing->Observe().application_bound, "the failed publication is not bound");
    RequireEqual(failing->Observe().admitted, 0, "and admitted nothing");
    // The claim was installed before the failure, so the rollback must detach it.
    const auto usage = fixture.attachments->SnapshotUsage();
    RequireEqual(usage.active_applications, 0, "the failed publication left no active application");
    RequireEqual(usage.retained_applications, 0, "and retained none");
    auto again = fixture.attachments->ClaimApplication(std::vector<Locator>{fixture.local}, fixture.snapshot);
    Require(again.result == ApplicationClaimResult::Claimed,
            "the local is claimable again after the failed registration");
    // Settle the retry as well and drop its handle, so the case leaves no dynamic
    // object behind: a retained handle would keep the application alive.
    Require(static_cast<bool>(again.claim), "the retry returned a live claim");
    (void)fixture.attachments->RequestDetach(again.claim);
    (void)fixture.attachments->AwaitDetached(again.claim);
    again.claim = ApplicationClaim{};
    RequireStatus(failing->Abandon(), NativeStatus::None, "roll the failure back");
    RequireStatus(failing->BeginClose(), NativeStatus::ServiceClosing, "the failed binding is already settled");
    fixture.RequireReceiveBudgetZero("no payload outlives the release");
}
// C15: the third topology. A Server-role socket accepted through a listener is
// adopted whole through ObserveInbound: the composition performs no Socket or
// Listen call of its own, the owner plan comes from the native table, and the
// release path claims the accepted socket's close responsibility.
void PreparedAcceptedBinding()
{
    Fixture f;
    auto listener=f.Deploy(f.CreateRequest("pkg.prepared.accepted",RecordRole::Server,false),331);
    const auto initial=Table().SnapshotUsage();
    auto request=f.JoinRequest(*listener,RecordRole::Server,false);
    request.is_accepted=true;request.defer_accepted_ownership=true;request.accepted_socket=332;
    request.accepted={f.config.directory.locals[0].callback_namespace,"net.c","peer.c",RecordDataType::Bytes};
    auto pending=f.Build(request);
    RequireStatus(pending->Prepare(),NativeStatus::None,"reserve negotiation budget");
    Require(!pending->Observe().socket_known&&!pending->Observe().membership,"negotiation is NOT owned socket or published receiver");
    RequireStatus(pending->Connect(),NativeStatus::NotReady,"OnBind is required before connect");
    RequireStatus(pending->Abandon(),NativeStatus::None,"cancel negotiation");
    RequireEqual(f.provider.shutdown,0,"unowned candidate has no Shutdown right");
    RequireEqual(f.native->SnapshotUsage().owners,1,"pending budget returned");
    pending.reset();
    auto accepted=f.Build(request);RequireStatus(accepted->Prepare(),NativeStatus::None,"prepare retry");
    RequireStatus(accepted->CommitAccepted(999),NativeStatus::InvalidResult,"wrong ID does not adopt");
    RequireStatus(accepted->CommitAccepted(332),NativeStatus::None,"exact candidate acquires ownership");
    RequireStatus(accepted->Connect(),NativeStatus::None,"accepted no public create/bind");
    RequireStatus(accepted->Publish(),NativeStatus::None,"publish original listener record");
    RequireEqual(Table().SnapshotUsage().records_created,initial.records_created,"accepted consumes no new record");
    Require(accepted->Observe().record_incarnation==listener->Observe().record_incarnation,"original live record");
    Require(accepted->Observe().owner_incarnation!=listener->Observe().owner_incarnation,"distinct native owner");
    Require(!accepted->Observe().owns_application&&listener->Observe().receiver_open,"shared existing receiver claim");
    f.Emit(*accepted,332,PayloadBytes());RequireEqual(f.Drain(*accepted),1,"accepted delivery");
    RequireStatus(accepted->ObserveProviderShutdown(332),NativeStatus::None,"exact owner shutdown notification");
    RequireStatus(accepted->Abandon(),NativeStatus::None,"control settles observed shutdown");
    RequireEqual(f.provider.shutdown,0,"notification does not license another Shutdown");
    Require(listener->Observe().receiver_open,"listener claim unaffected");
    RequireStatus(listener->Abandon(),NativeStatus::None,"final listener close");
    RequireEqual(f.provider.shutdown,1,"owned listener still closes exactly once");
    Fixture::RequireAttachmentsEmpty(*f.attachments,"last claim settles");
}

void AcceptedTopology()
{
    Fixture fixture;
    auto request = fixture.CreateRequest("pkg.srtc.accepted", RecordRole::Server, false);
    const auto& route = fixture.config.directory.routes[2];
    Require(fixture.config.directions[2] == RouteDirection::Accept,
            "the fixture's third route is an accepted-socket route");
    request.is_accepted = true;
    request.accepted = ObservedPeerKey{fixture.config.directory.locals[0].callback_namespace,
            route.fields.remote_network_id, route.fields.expected_remote_initiator_names[0],
            RecordDataType::Bytes};
    request.accepted_socket = 221;
    auto binding = fixture.Build(request);
    RequireStatus(binding->Prepare(), NativeStatus::None, "adopt the accepted socket");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 0,
            "an accepted socket performs no Socket call");
    RequireStatus(binding->Connect(), NativeStatus::None, "connect through the adopted owner");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 0, "and still no Socket call");
    RequireEqual(static_cast<std::size_t>(fixture.provider.listened), 0, "and no Listen call");
    RequireStatus(binding->Publish(), NativeStatus::None, "publish the accepted socket");
    const auto view = binding->Observe();
    RequireEqual(view.socket, 221, "the binding serves the accepted socket");
    Require(view.setup_complete, "the adopted owner is ready");
    Require(view.owner_incarnation != 0, "the owner identity comes from the native table");
    Require(view.socket_known, "and knows the accepted id");
    RequireEqual(Table().Observe(binding->RoleEntrance(RecordRole::Server)).role_owners[1], 1,
            "the accepted socket is registered under the server role");
    fixture.Emit(*binding, 221, PayloadBytes());
    RequireEqual(binding->Observe().admitted, 1, "the accepted socket admits");
    RequireEqual(fixture.Drain(*binding), 1, "and delivers");
    RequireEqual(fixture.receiver->Count(), 1, "to the receiver");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "close");
    RequireStatus(binding->Abandon(), NativeStatus::None, "release");
    RequireEqual(static_cast<std::size_t>(fixture.provider.created), 0,
            "the release invents no Socket call either");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1,
            "the accepted socket's close responsibility is claimed once");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing is left after the release");
    // The adopted owner really ended: the record has no anchor and releases its sink.
    const auto record = Table().Observe(binding->RoleEntrance(RecordRole::Server));
    Require(record.phase == RecordPhase::Unanchored, "the accepted record has no anchor left");
    Require(!record.sink_attached, "and released its sink with its last binding");

    // The accepted request has exactly three premises, and each refusal is decided
    // before anything is reserved: a client-role accepted request, a negative
    // accepted socket, and a peer the native table does not know.
    Fixture refusals;
    auto wrong_role = refusals.CreateRequest("pkg.srtc.accepted.role", RecordRole::Server, false);
    wrong_role.is_accepted = true;
    wrong_role.role = RecordRole::Client;
    wrong_role.accepted_socket = 231;
    auto client_accepted = refusals.Build(wrong_role);
    RequireStatus(client_accepted->Prepare(), NativeStatus::InvalidConfiguration,
            "an accepted request for the client role is refused");
    RequireEqual(refusals.native->SnapshotUsage().owners, 0, "and reserved no native owner");
    Require(!client_accepted->Observe().membership, "and registered no record membership");

    auto no_socket = refusals.CreateRequest("pkg.srtc.accepted.socket", RecordRole::Server, false);
    no_socket.is_accepted = true;
    no_socket.accepted_socket = -1;
    no_socket.accepted = ObservedPeerKey{refusals.config.directory.locals[0].callback_namespace,
            refusals.config.directory.routes[2].fields.remote_network_id,
            refusals.config.directory.routes[2].fields.expected_remote_initiator_names[0],
            RecordDataType::Bytes};
    auto missing_socket = refusals.Build(no_socket);
    RequireStatus(missing_socket->Prepare(), NativeStatus::InvalidConfiguration,
            "an accepted request without a socket id is refused");
    RequireEqual(refusals.native->SnapshotUsage().owners, 0, "and reserved no native owner");
    Require(!missing_socket->Observe().membership, "and registered no record membership");

    auto bad_peer = refusals.CreateRequest("pkg.srtc.accepted.peer", RecordRole::Server, false);
    bad_peer.is_accepted = true;
    bad_peer.accepted_socket = 232;
    bad_peer.accepted = ObservedPeerKey{refusals.config.directory.locals[0].callback_namespace,
            "net.unknown", "peer.unknown", RecordDataType::Bytes};
    auto unknown_peer = refusals.Build(bad_peer);
    RequireStatus(unknown_peer->Prepare(), NativeStatus::UnknownPeer,
            "an accepted request whose peer the native table does not know is refused");
    Require(!unknown_peer->Observe().membership, "and registered no record membership");
    // The native table records the refused peer as a rejected owner cell; what must
    // not happen is the composition adopting it, which the membership and the
    // binding's own state above rule out.
    RequireEqual(refusals.native->SnapshotUsage().owners, 1,
            "the refused peer is recorded as a rejected owner, not adopted");
}


// ---- CALLBACK_CONTEXT_DESTROY_CONTRACT -----------------------------------------
// A callback thread may ask the binding to close, but it is never made to wait for
// its own in-flight permit: the request is recorded and returns, and the owner
// completes it outside the callback. Every case here is bounded by the suite's own
// per-case timeout (CMake sets 60 s), and none of them proves completion by sleeping.

// CLOSE-C1: a callback requests the close; the request is recorded, closes logical
// admission, and returns without waiting.
void CloseRequestFromCallback()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c1", RecordRole::Client, false), 251);
    NativeStatus request = NativeStatus::InvalidHandle, again = NativeStatus::InvalidHandle;
    std::size_t delivering_during_callback = 0, queued_during_callback = 0;
    fixture.receiver->on_receive = [&] {
        // Observed from inside the callback, where these readings can still be
        // wrong: the delivery that is running must be accounted as delivering, and
        // the close request must not disturb it.
        const auto inside = binding->Observe().receive;
        delivering_during_callback = inside.delivering;
        queued_during_callback = inside.queued;
        request = binding->CloseRequest();
        again = binding->CloseRequest();
    };
    fixture.Emit(*binding, 251, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran and returned");
    RequireStatus(request, NativeStatus::None, "a callback can request a close");
    RequireStatus(again, NativeStatus::ServiceClosing, "and a second request is the same close");
    const auto closing = binding->Observe();
    RequireEqual(static_cast<std::size_t>(closing.phase), static_cast<std::size_t>(BindingPhase::Closing),
            "the request put the binding in its closing phase");
    RequireEqual(closing.admitted, 1, "and did not retract the delivery that was already admitted");
    RequireEqual(delivering_during_callback, 1, "the running delivery was accounted as delivering");
    RequireEqual(queued_during_callback, 0, "and was no longer counted as queued");
    const auto refused = closing.refused_phase;
    const auto queued = closing.receive.queued;
    fixture.Emit(*binding, 251, PayloadBytes());
    RequireEqual(binding->Observe().refused_phase - refused, 1, "a requested close stops new admission");
    RequireEqual(binding->Observe().receive.queued, queued, "and the refused ingress queued nothing");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the owner completes the requested close");
}

// CLOSE-C2: the synchronous controls are refused inside a callback, with a status
// rather than a wait, and the refusals change nothing.
void CloseSyncRefusedInCallback()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c2", RecordRole::Client, false), 252);
    NativeStatus begin = NativeStatus::InvalidHandle, abandon = NativeStatus::InvalidHandle,
            deferred = NativeStatus::InvalidHandle;
    fixture.receiver->on_receive = [&] {
        begin = binding->BeginClose();
        abandon = binding->Abandon();
        deferred = binding->DeferredClose();
    };
    fixture.Emit(*binding, 252, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran and returned");
    RequireStatus(begin, NativeStatus::WrongState, "the logical close is refused inside a callback");
    RequireStatus(abandon, NativeStatus::WrongState, "the release is refused inside a callback");
    RequireStatus(deferred, NativeStatus::WrongState, "completing a close is refused inside a callback");
    const auto view = binding->Observe();
    RequireEqual(static_cast<std::size_t>(view.phase), static_cast<std::size_t>(BindingPhase::Published),
            "the refusals left the binding published");
    fixture.Emit(*binding, 252, PayloadBytes());
    RequireEqual(binding->Observe().admitted, 2, "and still admitting");
    RequireEqual(fixture.Drain(*binding), 1, "and still delivering");
    RequireStatus(binding->BeginClose(), NativeStatus::None, "the control context may close");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "and complete it");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "one close responsibility");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic survives");
    // A deployment that never reached its published phase has nothing to request.
    auto unsettled = fixture.Build(fixture.CreateRequest("pkg.srtc.close_c2.unsettled", RecordRole::Client, true));
    RequireStatus(unsettled->CloseRequest(), NativeStatus::WrongState,
            "a request on a binding that was never published is refused");
    RequireStatus(unsettled->DeferredClose(), NativeStatus::WrongState,
            "and so is completing one");
}

// CLOSE-C2b: the completion guard is enforced *during* a real close, not only when
// the completion would be refused for having nothing to complete. With the close
// already requested, a callback-context completion would release inside the frame
// that holds the permit.
void CloseCompletionGuardInCallback()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c2b", RecordRole::Client, false), 260);
    NativeStatus request = NativeStatus::InvalidHandle, completion = NativeStatus::InvalidHandle;
    fixture.receiver->on_receive = [&] {
        request = binding->CloseRequest();
        completion = binding->DeferredClose();
    };
    fixture.Emit(*binding, 260, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery returned");
    RequireStatus(request, NativeStatus::None, "the callback requested the close");
    RequireStatus(completion, NativeStatus::WrongState,
            "completing it inside the callback is refused, not performed and not waited on");
    const auto closing = binding->Observe();
    RequireEqual(static_cast<std::size_t>(closing.phase), static_cast<std::size_t>(BindingPhase::Closing),
            "the refused completion left the close in progress");
    Require(closing.membership, "with its registrations still held");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the control context completes it");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "exactly one close");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic survives");
}

// CLOSE-C3: the owner completes a callback-requested close from a control context;
// it drains and releases exactly like the synchronous path.
void CloseCompletesOutsideCallback()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c3", RecordRole::Client, false), 253);
    RequireStatus(binding->DeferredClose(), NativeStatus::WrongState,
            "completing is refused while no close is in progress");
    std::atomic<int> requested{static_cast<int>(NativeStatus::InvalidHandle)};
    fixture.receiver->on_receive = [&] { requested.store(static_cast<int>(binding->CloseRequest())); };
    fixture.Emit(*binding, 253, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran");
    RequireStatus(static_cast<NativeStatus>(requested.load()), NativeStatus::None,
            "the callback requested the close");
    RequireStatus(binding->BeginClose(), NativeStatus::ServiceClosing,
            "the logical close is already recorded, so the synchronous close reports it instead of repeating it");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the owner completes the close");
    const auto view = binding->Observe();
    RequireEqual(static_cast<std::size_t>(view.phase), static_cast<std::size_t>(BindingPhase::Closed),
            "the binding is closed");
    Require(!view.membership, "the record membership is released");
    Require(!view.application_bound, "the application claim is released");
    Require(!view.receiver_open, "the receiver registration is released");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "exactly one close, claimed once");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic outlives the close");
    RequireStatus(binding->DeferredClose(), NativeStatus::AlreadySettled, "a second completion is settled once");
    RequireStatus(binding->CloseRequest(), NativeStatus::ServiceClosing,
            "and a request after the close reports that there is nothing left to request");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "and claims no second close");
}

// CLOSE-C4: a callback that requests the close and then throws still leaves the close
// completable, because the intent was recorded before the exception.
void CloseAfterCallbackException()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c4", RecordRole::Client, false), 254);
    std::atomic<int> requested{static_cast<int>(NativeStatus::InvalidHandle)};
    fixture.receiver->on_receive = [&] {
        requested.store(static_cast<int>(binding->CloseRequest()));
        throw std::runtime_error("receiver callback failure");
    };
    fixture.Emit(*binding, 254, PayloadBytes());
    auto taken = binding->NextAdmitted();
    Require(static_cast<bool>(taken), "the admitted work was taken");
    RequireDelivery(binding->RunAdmitted(*taken.value), InputDeliveryResult::CallbackFailure,
            "the throwing callback is reported, not propagated");
    RequireStatus(static_cast<NativeStatus>(requested.load()), NativeStatus::None,
            "the request made before the throw survives it");
    fixture.receiver->on_receive = nullptr;
    RequireStatus(binding->DeferredClose(), NativeStatus::None,
            "the close still completes after the callback threw");
    const auto view = binding->Observe();
    RequireEqual(static_cast<std::size_t>(view.phase), static_cast<std::size_t>(BindingPhase::Closed), "closed");
    RequireEqual(view.receive.queued, 0, "nothing is queued");
    RequireEqual(view.receive.delivering, 0, "nothing is delivering");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "one close responsibility");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic survives the exception path");
}

// CLOSE-C5: concurrent requests, one from a callback and several from control
// threads, start exactly one close and produce exactly one close.
void ManyCloseRequestsOneClose()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c5", RecordRole::Client, false), 255);
    std::atomic<int> started{0}, already{0};
    const auto record = [&](NativeStatus status) {
        if (status == NativeStatus::None) started.fetch_add(1);
        else if (status == NativeStatus::ServiceClosing) already.fetch_add(1);
    };
    fixture.receiver->on_receive = [&] { record(binding->CloseRequest()); };
    fixture.Emit(*binding, 255, PayloadBytes());
    auto taken = binding->NextAdmitted();
    Require(static_cast<bool>(taken), "the admitted work was taken");
    std::vector<std::thread> requesters;
    for (int i = 0; i < 4; ++i) {
        requesters.emplace_back([&] { record(binding->CloseRequest()); });
    }
    RequireDelivery(binding->RunAdmitted(*taken.value), InputDeliveryResult::Delivered,
            "the delivery ran while the other threads were requesting");
    for (auto& requester : requesters) requester.join();
    RequireEqual(static_cast<std::size_t>(started.load()), 1, "exactly one request starts the close");
    RequireEqual(static_cast<std::size_t>(already.load()), 4, "every other request sees the same close");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 0, "a request alone closes nothing");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the owner completes the close once");
    RequireStatus(binding->DeferredClose(), NativeStatus::AlreadySettled, "and only once");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "exactly one final close");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic survives");
}

// CLOSE-C6: a requested close stops new admission while work already admitted still
// runs to completion.
void NoAdmissionAfterCloseRequest()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c6", RecordRole::Client, false), 256);
    fixture.Emit(*binding, 256, PayloadBytes(16));
    fixture.Emit(*binding, 256, PayloadBytes(16));
    RequireEqual(binding->Observe().receive.queued, 2, "two deliveries are admitted and queued");
    NativeStatus request = NativeStatus::InvalidHandle;
    fixture.receiver->on_receive = [&] {
        if (request == NativeStatus::InvalidHandle) request = binding->CloseRequest();
    };
    RequireEqual(fixture.Drain(*binding), 2, "both admitted deliveries still ran");
    RequireStatus(request, NativeStatus::None, "the first delivery requested the close");
    RequireEqual(fixture.receiver->Count(), 2, "and both reached the receiver");
    const auto refused = binding->Observe().refused_phase;
    fixture.Emit(*binding, 256, PayloadBytes(16));
    RequireEqual(binding->Observe().refused_phase - refused, 1, "new ingress is refused after the request");
    RequireEqual(binding->Observe().receive.queued, 0, "and nothing new was queued");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the owner completes the close");
    RequireEqual(fixture.receiver->Count(), 2, "and the close admits nothing further");
}

// CLOSE-C7: the four contract resources - sink, receiver, owner, claim - are released
// by a callback-requested close exactly as by the synchronous one.
void CloseRequestReleasesResources()
{
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c7", RecordRole::Client, false), 257);
    const auto entrance = binding->RoleEntrance(RecordRole::Client);
    // Without this the "released" readings below could not be told apart from a
    // binding that never held an owner at all.
    const auto held = binding->Observe();
    Require(held.socket_known, "the binding holds a native owner before the close");
    Require(held.setup_complete, "with a ready owner plan");
    std::atomic<int> requested{static_cast<int>(NativeStatus::InvalidHandle)};
    fixture.receiver->on_receive = [&] { requested.store(static_cast<int>(binding->CloseRequest())); };
    fixture.Emit(*binding, 257, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran");
    RequireStatus(static_cast<NativeStatus>(requested.load()), NativeStatus::None, "the close was requested");
    RequireStatus(binding->DeferredClose(), NativeStatus::None, "the close completed outside the callback");
    const auto record = Table().Observe(entrance);
    Require(record.phase == RecordPhase::Unanchored, "the record has no anchor left");
    Require(!record.sink_attached, "and released its sink registration");
    const auto usage = fixture.attachments->SnapshotUsage();
    RequireEqual(usage.active_applications, 0, "the shared application is detached");
    RequireEqual(usage.retained_applications, 0, "and not retained");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "the attachments are empty");
    auto again = fixture.attachments->ClaimApplication(std::vector<Locator>{fixture.local}, fixture.snapshot);
    Require(again.result == ApplicationClaimResult::Claimed, "the local is claimable again");
    Require(static_cast<bool>(again.claim), "the retry returned a live claim");
    (void)fixture.attachments->RequestDetach(again.claim);
    (void)fixture.attachments->AwaitDetached(again.claim);
    again.claim = ApplicationClaim{};
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1, "one owner close, claimed once");
    // owner: the lease that was provably held above is released with the close, so
    // the binding no longer reports an owner at all.
    const auto closed = binding->Observe();
    Require(!closed.socket_known, "the native owner lease that was held is released");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "nothing dynamic outlives the close");
}

// CLOSE-C8: the forbidden shape. Destroying the binding from inside one of its own
// callbacks cannot be made safe - the release would wait for the permit that frame
// holds - so the contract forbids it and the destructor fails fast instead of
// deadlocking. That is only observable across a process boundary, so the case runs
// itself as a child and requires the child to stop; the suite's per-case timeout
// bounds the wait.
void CallbackDestroyChild()
{
    // The forbidden shape itself, run only as a child process by the case below (it
    // is deliberately not registered with CTest): the binding is destroyed from
    // inside its own delivery callback, and the destructor must stop the process
    // with a diagnostic instead of waiting for the permit this frame holds.
    // A Debug build would otherwise put up the platform's abort dialog and block
    // there; the child suppresses that so its exit is a fact the parent can read.
    (void)_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c8", RecordRole::Client, false), 258);
    fixture.receiver->on_receive = [&] { binding.reset(); };
    fixture.Emit(*binding, 258, PayloadBytes());
    auto taken = binding->NextAdmitted();
    Require(static_cast<bool>(taken), "the admitted work was taken");
    (void)binding->RunAdmitted(*taken.value);
    // Reaching this line means the destructive callback returned, which is the
    // failure this case exists to detect. The marker is how the parent tells that
    // outcome apart from the intended abort.
    std::printf("FASTDDS_DSOFTBUS_SRT_CLOSE_CHILD_SURVIVED\n");
    std::fflush(stdout);
    Require(false, "the child must stop inside the destructive callback, never here");
}

void CallbackDestroyFailsFast()
{
    RequireChildStops("callback-destroy-child", "the destructive callback returned instead of stopping");
    Fixture fixture;
    auto binding = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c8.legal", RecordRole::Client, false), 259);
    std::atomic<int> requested{static_cast<int>(NativeStatus::InvalidHandle)};
    fixture.receiver->on_receive = [&] { requested.store(static_cast<int>(binding->CloseRequest())); };
    fixture.Emit(*binding, 259, PayloadBytes());
    RequireEqual(fixture.Drain(*binding), 1, "the delivery ran");
    RequireStatus(static_cast<NativeStatus>(requested.load()), NativeStatus::None,
            "the callback requested the close");
    Require(static_cast<bool>(binding), "the binding is still alive in its owner after the callback");
    binding.reset();
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1,
            "destroying outside the callback completes the close exactly once");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "and releases everything");
}

// CLOSE-C9: the guard is scoped to the registration a frame is inside, so destroying
// an *unrelated* binding from a callback is legal - and was legal before this round.
// It is pinned here because a thread-global guard would abort it.
void DestroyUnrelatedBindingInCallback()
{
    Fixture fixture;
    auto host = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c9", RecordRole::Client, false), 261);
    // A second record, with its own native table and attachments: nothing about its
    // release waits on the frame that is running the first binding's delivery.
    auto other = fixture.DeployFresh("pkg.srtc.close_c9.other", 262);
    std::atomic<bool> destroyed{false};
    fixture.receiver->on_receive = [&] {
        other.reset();
        destroyed.store(true);
    };
    fixture.Emit(*host, 261, PayloadBytes());
    auto taken = host->NextAdmitted();
    Require(static_cast<bool>(taken), "the admitted work was taken");
    RequireDelivery(host->RunAdmitted(*taken.value), InputDeliveryResult::Delivered,
            "the delivery completed with an unrelated binding destroyed inside it");
    Require(destroyed.load(), "the unrelated binding really was destroyed from the callback");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 1,
            "its own release completed exactly once");
    RequireStatus(host->BeginClose(), NativeStatus::None, "the host binding is untouched");
    RequireStatus(host->Abandon(), NativeStatus::None, "and releases normally");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2, "one close per binding");
}

// Runs one forbidden shape in a separate process and requires it to stop for the
// contract's reason. The capture is removed first and required to exist afterwards,
// so a leftover file cannot stand in for a child that never ran; the child prints a
// marker only if its destructive callback returns, so a child that merely fails, or
// never starts, cannot satisfy this. The wait is bounded by the suite's per-case
// timeout and by the runner's.
void RequireChildStops(const char* child_case, const char* reason,const char* detail)
{
    Require(!executable_path.empty(), "the case knows the executable it is running as");
    const std::string output_path = "srtc_close_child_output.txt";
    (void)std::remove(output_path.c_str());
    // cmd.exe strips a leading quote when it is given /c, so the whole command is
    // wrapped in one more pair; without it the redirect form does not resolve.
    const std::string command = "\"\"" + executable_path + "\" --case " + child_case + " > \"" +
            output_path + "\" 2>&1\"";
    const int child = std::system(command.c_str());
    const bool captured_existed = static_cast<bool>(std::ifstream(output_path));
    std::string captured;
    {
        std::ifstream captured_stream(output_path);
        captured.assign(std::istreambuf_iterator<char>(captured_stream), std::istreambuf_iterator<char>());
    }
    Require(child != -1, "the child process was actually started");
    Require(captured_existed, "the child's output was captured, so it ran with a working shell");
    (void)std::remove(output_path.c_str());
    Require(child != 0, reason);
    if(detail)Require(captured.find(detail)!=std::string::npos,"specific child contract diagnostic");
    Require(captured.find("destroyed from inside a callback") != std::string::npos,
            "and it stopped with the contract diagnostic");
    Require(captured.find("FASTDDS_DSOFTBUS_SRT_CLOSE_CHILD_SURVIVED") == std::string::npos,
            "and not by returning from the destructive callback");
}

void ShutdownDestroySameTableChild()
{
    (void)_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    Fixture f;auto a=f.Deploy(f.CreateRequest("pkg.credit.destroy",RecordRole::Client,false),81);auto b=f.Deploy(f.JoinRequest(*a,RecordRole::Client,true),82);
    f.provider.shutdown_hook=[&](NativeSocketId id){if(id==81)b.reset();};
    RequireStatus(a->BeginClose(),NativeStatus::None,"outer begin");(void)a->Abandon();
    std::printf("FASTDDS_DSOFTBUS_SRT_CLOSE_CHILD_SURVIVED\n");std::fflush(stdout);
}
void ShutdownDestroySameTable()
{
    RequireChildStops("shutdown-destroy-child","destructor must diagnose its own shared close credit","native close credit held by this stack");
}
void ShutdownDestroyOtherTable()
{
    Fixture a_fixture,b_fixture;auto a=a_fixture.Deploy(a_fixture.CreateRequest("pkg.credit.outer",RecordRole::Client,false),81);auto b=b_fixture.Deploy(b_fixture.CreateRequest("pkg.credit.unrelated",RecordRole::Client,false),82);
    a_fixture.provider.shutdown_hook=[&](NativeSocketId id){if(id==81)b.reset();};
    RequireStatus(a->BeginClose(),NativeStatus::None,"outer begin");RequireStatus(a->Abandon(),NativeStatus::None,"outer completes");a_fixture.provider.shutdown_hook={};
    Require(!b,"unrelated native table binding really destroyed");RequireEqual(a_fixture.provider.shutdown,1,"outer exactly once");RequireEqual(b_fixture.provider.shutdown,1,"unrelated exactly once");
}

// CLOSE-C10: a release that is not the last one settles nothing and waits for
// nothing, so destroying such a sibling from inside a callback is legal even though
// it shares the registration the frame is running under.
void DestroyNonLastSiblingInCallback()
{
    Fixture fixture;
    auto creator = fixture.Deploy(fixture.CreateRequest("pkg.srtc.close_c10", RecordRole::Client, false), 263);
    auto sibling = fixture.Deploy(fixture.JoinRequest(*creator, RecordRole::Client, true), 264);
    {
        const auto shared = fixture.attachments->SnapshotUsage();
        RequireEqual(shared.active_applications, 1, "the two bindings share one registration");
    }
    std::atomic<bool> destroyed{false};
    fixture.receiver->on_receive = [&] {
        sibling.reset();
        destroyed.store(true);
    };
    fixture.Emit(*creator, 263, PayloadBytes());
    auto taken = creator->NextAdmitted();
    Require(static_cast<bool>(taken), "the admitted work was taken");
    RequireDelivery(creator->RunAdmitted(*taken.value), InputDeliveryResult::Delivered,
            "the delivery completed with the non-last sibling destroyed inside it");
    Require(destroyed.load(), "the sibling really was destroyed from the callback");
    // The registration survived, held by the creator, and the local is still served.
    const auto shared = fixture.attachments->SnapshotUsage();
    RequireEqual(shared.active_applications, 1, "the shared registration belongs to the surviving binding");
    RequireEqual(shared.retained_applications, 1, "and is still held exactly once");
    fixture.Emit(*creator, 263, PayloadBytes());
    RequireEqual(creator->Observe().admitted, 2, "the survivor still admits");
    RequireEqual(fixture.Drain(*creator), 1, "and still delivers");
    RequireStatus(creator->BeginClose(), NativeStatus::None, "close the survivor");
    RequireStatus(creator->Abandon(), NativeStatus::None, "release the survivor");
    RequireEqual(static_cast<std::size_t>(fixture.provider.shutdown), 2, "one close per binding");
    Fixture::RequireAttachmentsEmpty(*fixture.attachments, "the registration ends with its last user");
}

struct DecisionObserver final : ProcessReceiveObserver
{
    std::array<ReceiveDecision, 64> decisions{};
    std::size_t count=0,overflow=0;
    ServiceRecordBinding* binding=nullptr;
    bool reentrant_safe=true;
    void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override {}
    void OnDecision(const ReceiveDecision& d) noexcept override {
        if(count<decisions.size())decisions[count]=d;else ++overflow;
        ++count;
        if(binding){(void)binding->Observe();reentrant_safe &= binding->BeginClose()==NativeStatus::WrongState;}
    }
};
void DecisionAccounting()
{
    Fixture f;auto observer=std::make_shared<DecisionObserver>();
    auto request=f.CreateRequest("pkg.decision.accounting",RecordRole::Client,false);
    request.receive_observer=observer;
    auto binding=f.Deploy(request,271);observer->binding=binding.get();
    const auto entry=binding->Entrance();
    f.Emit(*binding,271,PayloadBytes());
    RequireEqual(observer->count,1,"actual admission emits one decision");
    auto d=observer->decisions[0];
    Require(d.branch==ReceiveDecisionBranch::BridgeAccepted && d.bridge_called && !d.refused_receive,
            "accepted bridge is not rejection");
    Require(d.bridge.result_known && d.bridge.reason==RxAdmissionReason::AcceptedQueue,
            "actual bridge result retained");
    Require(d.bridge.application_known && d.bridge.receiver_known,"captured receiver identity known");
    const auto oversized=PayloadBytes(60001);
    entry(271,oversized.data(),static_cast<std::uint32_t>(oversized.size()));
    RequireEqual(observer->count,2,"oversized buffer decision once");
    d=observer->decisions[1];
    Require(d.branch==ReceiveDecisionBranch::BridgeRefused && d.bridge_called && d.refused_receive,
            "bridge refusal separate");
    Require(d.bridge.reason==RxAdmissionReason::TooLarge,"actual short circuit reason");
    RequireStatus(binding->CloseInput(),NativeStatus::None,"queued admission cancels without pump");
    RequireEqual(binding->Observe().admitted,1,"cancellation does not become admission refusal");
    f.Emit(*binding,271,PayloadBytes());
    RequireEqual(observer->count,3,"gap decision once");d=observer->decisions[2];
    Require(d.branch==ReceiveDecisionBranch::NoRegistration && !d.bridge_called && !d.bridge.result_known,
            "no registration must not fabricate bridge call or result");
    Require(d.refused_receive,"same original counter unit");
    RequireEqual(binding->Observe().refused_receive,2,"original counter unchanged");
    Require(observer->decisions[0].sequence<observer->decisions[1].sequence &&
            observer->decisions[1].sequence<d.sequence,"decision sequences distinct");
    Require(observer->reentrant_safe,"observer outside locks within callback context");
    observer->binding=nullptr;
    RequireStatus(binding->CloseRequest(),NativeStatus::None,"request close");
    f.Emit(*binding,271,PayloadBytes());
    Require(observer->decisions[3].branch==ReceiveDecisionBranch::BindingPhase &&
            !observer->decisions[3].refused_receive && !observer->decisions[3].bridge_called,
            "closing phase is a distinct counter and no bridge");
}
void DecisionBudget()
{
    Fixture f;auto observer=std::make_shared<DecisionObserver>();
    auto request=f.CreateRequest("pkg.decision.budget",RecordRole::Client,false);request.receive_observer=observer;
    auto binding=f.Deploy(request,272);
    for(unsigned i=0;i<33;++i)f.Emit(*binding,272,PayloadBytes());
    RequireEqual(observer->count,33,"each original admission observed once");
    const auto& d=observer->decisions[32];
    Require(d.branch==ReceiveDecisionBranch::BridgeRefused && d.bridge.reason==RxAdmissionReason::LocalRecords &&
            d.bridge.capacity_known && d.bridge.local_records_used==32 && d.bridge.local_records_limit==32,
            "actual local record budget captured at decision");
    RequireEqual(binding->Observe().admitted,32,"budget unchanged");
    RequireEqual(binding->Observe().refused_receive,1,"one rejection not multiple predicates");
    RequireStatus(binding->CloseInput(),NativeStatus::None,"cancel all queued without pump");
    f.RequireReceiveBudgetZero("no queued bytes or leases retained");
}
void DecisionStoreBound()
{
    dds_experiment::DecisionStore<2> store;
    ReceiveDecision d;d.branch=ReceiveDecisionBranch::NoRegistration;d.refused_receive=true;
    store.Record(d,1);store.Record(d,2);store.Record(d,3);
    const auto a=store.Capture(),b=store.Capture();
    RequireEqual(a.count,2,"fixed detail bound");RequireEqual(a.total,3,"aggregate retains overflow");
    RequireEqual(a.overflow,1,"overflow visible");
    RequireEqual(a.branch_totals[static_cast<std::size_t>(d.branch)],3,"reason retained after full");
    RequireEqual(a.total,b.total,"snapshot read does not recount");
    d.branch=static_cast<ReceiveDecisionBranch>(999);store.Record(d,4);
    RequireEqual(store.Capture().unknown,1,"unknown enumeration visible");
    RequireEqual(store.At(0).ingress,1,"published prefix immutable");
}
void DecisionCloseRequest()
{
    struct Observer final:ProcessReceiveObserver {
        ServiceRecordBinding* binding=nullptr;NativeStatus requested=NativeStatus::NotImplemented,synchronous=NativeStatus::NotImplemented;
        void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override {}
        void OnDecision(const ReceiveDecision&) noexcept override {
            (void)binding->Observe();requested=binding->CloseRequest();synchronous=binding->DeferredClose();
        }
    };
    Fixture f;auto observer=std::make_shared<Observer>();auto request=f.CreateRequest("pkg.decision.close",RecordRole::Client,false);request.receive_observer=observer;
    auto binding=f.Deploy(request,273);observer->binding=binding.get();f.Emit(*binding,273,PayloadBytes());
    RequireStatus(observer->requested,NativeStatus::None,"decision callback may request close");
    RequireStatus(observer->synchronous,NativeStatus::WrongState,"decision callback cannot synchronously finish close");
    RequireStatus(binding->DeferredClose(),NativeStatus::None,"external close settles original admission");
    f.RequireReceiveBudgetZero("requested close cancels queued admission");
    RequireEqual(f.provider.shutdown,1,"one close responsibility");
}
void DecisionPausedDrain()
{
    struct Observer final:ProcessReceiveObserver {
        std::promise<void> entered;std::mutex gate;std::condition_variable changed;bool release=false;
        void OnReceive(ReceiveStage,const ReceiveDiagnostic&,const std::uint8_t*,std::size_t) noexcept override {}
        void OnDecision(const ReceiveDecision&) noexcept override {
            entered.set_value();std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[&]{return release;});
        }
    };
    Fixture f;auto observer=std::make_shared<Observer>();auto ready=observer->entered.get_future();
    auto request=f.CreateRequest("pkg.decision.pause",RecordRole::Client,false);request.receive_observer=observer;
    auto binding=f.Deploy(request,274);
    auto ingress=std::async(std::launch::async,[&]{f.Emit(*binding,274,PayloadBytes());});ready.wait();
    auto close=std::async(std::launch::async,[&]{return binding->CloseInput();});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool sealed=false;
    while(std::chrono::steady_clock::now()<deadline){
        if(binding->Observe().input_closing){sealed=true;break;}
        if(close.wait_for(std::chrono::milliseconds(0))==std::future_status::ready)break;
        std::this_thread::yield();
    }
    const bool pending=close.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready;
    {std::lock_guard<std::mutex> lock(observer->gate);observer->release=true;}observer->changed.notify_all();
    ingress.get();const auto status=close.get();
    Require(sealed&&pending,"actual close gate sealed while original decision activity remains held");
    RequireStatus(status,NativeStatus::None,"release permits close completion without pump");
    f.RequireReceiveBudgetZero("queued record settles once");
}
} // namespace

int main(int argc, char** argv)
{
    static_assert(std::is_same_v<RecordBytesCallback, decltype(ISocketListener::OnBytes)>,
            "the composition entrance must be the public Socket byte-callback type");
    const std::map<std::string, void (*)()> cases{
        {"decision-accounting",DecisionAccounting},
        {"decision-store-bound",DecisionStoreBound},
        {"decision-budget",DecisionBudget},
        {"decision-close-request",DecisionCloseRequest},{"decision-paused-drain",DecisionPausedDrain},
        {"two-owners", TwoOwnersSameRecord}, {"cross-role", CrossRole}, {"partial-failure", PartialFailure},
        {"close-vs-capture", CloseVersusCapture}, {"admitted-not-run", AdmittedButNotRun},
        {"running-receiver", CloseWaitsForRunningReceiver}, {"unique-close", UniqueCloseResponsibility}, {"close-credit-same",CloseCreditSameRecord}, {"close-credit-different",CloseCreditDifferentRecord}, {"shutdown-reentrant-sibling",ShutdownReentrantSibling}, {"shutdown-destroy-child",ShutdownDestroySameTableChild}, {"shutdown-destroy-same-table",ShutdownDestroySameTable}, {"shutdown-destroy-other-table",ShutdownDestroyOtherTable},
        {"reentrant-boundary", ReentrantBoundary}, {"exceptional-admission", ExceptionalAdmission},
        {"old-entry-new-incarnation", OldEntryNewIncarnation}, {"repeated-lifecycles", RepeatedLifecycles},
        {"slot-exhaustion", SlotExhaustion}, {"resource-release", ResourceRelease},
        {"fail-closed", FailClosedDefault},
        {"creator-exits-joiner-lives", CreatorExitsJoinerLives},
        {"delivery-context-bypass", DeliveryContextBypass},
        {"shared-group-release-race", SharedGroupReleaseRace},
        {"join-during-settle", JoinDuringSettle}, {"joiner-failure-keeps-creator", JoinerFailureKeepsCreator},
        {"registration-allocation-failure", RegistrationAllocationFailure},
        {"accepted-topology", AcceptedTopology},
        {"prepared-accepted", PreparedAcceptedBinding},
        {"close-request-from-callback", CloseRequestFromCallback},
        {"close-sync-refused-in-callback", CloseSyncRefusedInCallback},
        {"close-completion-guard-in-callback", CloseCompletionGuardInCallback},
        {"close-completes-outside-callback", CloseCompletesOutsideCallback},
        {"close-after-callback-exception", CloseAfterCallbackException},
        {"many-close-requests-one-close", ManyCloseRequestsOneClose},
        {"no-admission-after-close-request", NoAdmissionAfterCloseRequest},
        {"close-request-releases-resources", CloseRequestReleasesResources},
        {"callback-destroy-fails-fast", CallbackDestroyFailsFast},
        {"destroy-unrelated-in-callback", DestroyUnrelatedBindingInCallback},
        {"destroy-shared-sibling-in-callback", DestroyNonLastSiblingInCallback},
        // Not registered with CTest: it is the forbidden shape, run by the case above.
        {"callback-destroy-child", CallbackDestroyChild}};
    if (argc > 0) executable_path = argv[0];
    if (argc != 3 || std::string(argv[1]) != "--case" || cases.count(argv[2]) == 0) return 64;
    try {
        cases.at(argv[2])();
        std::cout << "HOST_COMPOSITION_PASS " << argv[2]
                  << " sdk_abi_equivalence=NOT_CLAIMED device=NOT_RUN\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << argv[2] << ": " << e.what() << '\n';
        return 1;
    }
}
