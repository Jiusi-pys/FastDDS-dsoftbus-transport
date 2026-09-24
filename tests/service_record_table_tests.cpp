// Bounded shared service-record table contracts. One case per process: the
// process-lifetime entrance pool cannot be reset, so a case never inherits
// another case's slots. Nothing here claims target SDK record lifecycle,
// entrance-pointer equivalence on a device, or callback drainage.
#include <mdds/dsoftbus/ServiceRecordTable.hpp>
#include "socket.h" // Current source header. Not a firmware or target equivalence claim.

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// The allocation-failure contract is exercised by denying allocation in this
// test process, the same technique the repository already uses for the native
// owner model. The denial window is single threaded and one call wide.
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

using namespace mdds::dsoftbus;
namespace mdds::dsoftbus {
// Test-only access to the bounded counter conditions. It grants no production
// authority and is the only writer of those counters outside real use.
class ServiceRecordTableTestAccess
{
public:
    static void NextIdentities(ServiceRecordTable& table, std::uint64_t record_incarnation,
            std::uint64_t reservation)
    { table.TestSetNextIdentities(record_incarnation, reservation); }
    static void NextOwnerIncarnation(ServiceRecordTable& table, std::size_t slot, std::uint64_t value)
    { table.TestSetNextOwnerIncarnation(slot, value); }
};
} // namespace mdds::dsoftbus

namespace {
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void RequireEqual(std::size_t actual, std::size_t expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "count=" << actual << " expected=" << expected << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
void RequirePhase(RecordPhase actual, RecordPhase expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "phase=" << static_cast<int>(actual) << " expected=" << static_cast<int>(expected)
                  << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
void RequireStatus(RecordStatus actual, RecordStatus expected, const char* reason)
{
    if (actual != expected) {
        std::cerr << "status=" << static_cast<int>(actual) << " expected=" << static_cast<int>(expected)
                  << " (" << reason << ")\n";
        throw std::runtime_error(reason);
    }
}
template<class T>
T RequireOk(RecordResult<T>&& result, const char* reason)
{
    Require(result.status == RecordStatus::None, reason);
    Require(result.value.has_value(), reason);
    return std::move(*result.value);
}
RecordStatus StatusOf(const RecordResult<RecordReservation>& r) { return r.status; }
RecordStatus StatusOf(const RecordResult<RecordEntrance>& r) { return r.status; }
RecordStatus StatusOf(const RecordResult<RecordMembership>& r) { return r.status; }
RecordStatus StatusOf(const RecordResult<RecordAttribution>& r) { return r.status; }
std::vector<std::uint8_t> Payload() { return {7, 7, 7, 7, 7, 7}; }

std::size_t RoleIndex(RecordRole role) { return static_cast<std::size_t>(role); }

// Records exactly what the adapter could observe at its single attribution
// point. It never looks up current table state to reinterpret an old record.
struct Attributed
{
    RecordRole role;
    std::int32_t socket;
    std::size_t slot;
    std::uint64_t record_incarnation;
    std::vector<std::uint8_t> bytes;
    bool current_at_call;
};
struct Sink final : RecordIngressSink
{
    std::mutex mutex;
    std::vector<Attributed> calls;
    std::function<void()> reenter;
    std::function<void()> reenter_throw;
    std::atomic<int> entered{0};
    explicit Sink(std::size_t reserve = 8192) { calls.reserve(reserve); }
    void OnAttributedBytes(const RecordAttribution& attribution, std::int32_t socket,
            const void* data, std::size_t size) override
    {
        entered.fetch_add(1, std::memory_order_relaxed);
        const auto observation = attribution.Observe();
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        Attributed record{attribution.role(), socket, observation.slot, observation.record_incarnation,
                std::vector<std::uint8_t>(bytes, bytes + size), attribution.Current()};
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(std::move(record));
        }
        // Deliberately after the local lock is released: a reentrant hook may
        // touch the table and this sink again without self-deadlock.
        if (reenter) reenter();
        if (reenter_throw) reenter_throw();
    }
    std::size_t Size() { std::lock_guard<std::mutex> lock(mutex); return calls.size(); }
    Attributed At(std::size_t index) { std::lock_guard<std::mutex> lock(mutex); return calls.at(index); }
};
void Emit(const RecordEntrance& entrance, std::int32_t socket, const std::vector<std::uint8_t>& bytes)
{
    entrance.function()(socket, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

// One exposed record plus its retained reservation and fixed entrance.
struct Channel
{
    RecordReservation reservation;
    RecordEntrance entrance;
};
ServiceRecordTable& Table() { return ServiceRecordTable::Process(); }
Channel Open(const std::string& name, RecordRole role, Sink& sink)
{
    Channel channel;
    channel.reservation = RequireOk(Table().ReserveRecord(name), "reserve record");
    channel.entrance = RequireOk(Table().Expose(channel.reservation, role, sink), "expose role");
    return channel;
}

// A never-exposed reservation returns to FREE; exposure is permanent and a
// stale reservation cannot expose or release a later record.
void Unexposed()
{
    auto& table = Table();
    Sink sink;
    auto first = RequireOk(table.ReserveRecord("pkg.srt.unexposed"), "reserve first");
    RequireEqual(table.SnapshotUsage().slots_reserved, 1, "reservation is bounded and counted");
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.unexposed")), RecordStatus::NameInUse,
            "one name holds one slot at a time");
    RequireStatus(table.AbandonUnexposed(first), RecordStatus::None, "unexposed reservation returns");
    RequireEqual(table.SnapshotUsage().slots_free, record_slot_capacity, "slot returns to FREE");
    RequireStatus(StatusOf(table.Expose(first, RecordRole::Client, sink)), RecordStatus::StaleHandle,
            "abandoned reservation cannot expose the reused slot");
    auto second = RequireOk(table.ReserveRecord("pkg.srt.unexposed"), "reserve second");
    auto entrance = RequireOk(table.Expose(second, RecordRole::Client, sink), "expose second");
    RequireStatus(table.AbandonUnexposed(second), RecordStatus::NotUnexposed,
            "an exposed slot is never reclaimed");
    RequireStatus(StatusOf(table.Expose(second, RecordRole::Client, sink)), RecordStatus::AlreadyExposed,
            "the entrance is retained, never derived twice");
    RequireEqual(table.SnapshotUsage().slots_exposed, 1, "exposed slot counted once");
    RequireStatus(table.AbandonExposure(entrance), RecordStatus::None, "failed registration drops the anchor");
    RequirePhase(table.Observe(entrance).phase, RecordPhase::Unanchored,
            "an exposed record with no anchor has unproven continuity");
    RequireEqual(table.SnapshotUsage().slots_unanchored, 1, "unanchored slot is not FREE");
    const auto before_drop = table.SnapshotUsage();
    {
        // Dropping a never-exposed reservation returns its slot instead of
        // stranding it in RESERVED_UNEXPOSED and sealing the name forever.
        const auto dropped = RequireOk(table.ReserveRecord("pkg.srt.unexposed.dropped"), "reserve a dropped record");
        Require(static_cast<bool>(dropped), "the dropped reservation is live");
        RequireEqual(table.SnapshotUsage().slots_reserved, 1, "the dropped reservation holds a slot");
    }
    RequireEqual(table.SnapshotUsage().slots_free, before_drop.slots_free, "the dropped slot returned to FREE");
    auto reused = RequireOk(table.ReserveRecord("pkg.srt.unexposed.dropped"),
            "the dropped name is reservable again");
    RequireStatus(table.AbandonUnexposed(reused), RecordStatus::None, "cleanup");
    {
        // The other direction: a slot that was exposed is never reclaimed, even
        // when its reservation handle is dropped.
        RecordEntrance kept;
        {
            auto exposed = Open("pkg.srt.unexposed.exposed", RecordRole::Client, sink);
            kept = exposed.entrance;
            auto anchored = RequireOk(table.RegisterOwner(kept, 93), "anchor the exposed record");
            RequireStatus(table.ReleaseOwner(anchored), RecordStatus::None, "release the anchor");
        }
        RequirePhase(table.Observe(kept).phase, RecordPhase::Unanchored,
                "dropping the reservation left the exposed slot alone");
        RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.unexposed.exposed")),
                RecordStatus::ContinuityUnproven, "the exposed name is still sealed");
    }
    {
        // Overwriting a live reservation releases the slot it held first.
        auto overwritten = RequireOk(table.ReserveRecord("pkg.srt.unexposed.move.a"), "reserve a");
        auto adopted = RequireOk(table.ReserveRecord("pkg.srt.unexposed.move.b"), "reserve b");
        RequireEqual(table.SnapshotUsage().slots_reserved, 2, "two pending reservations");
        overwritten = std::move(adopted);
        RequireEqual(table.SnapshotUsage().slots_reserved, 1,
                "the overwritten reservation released its slot");
        Require(static_cast<bool>(overwritten), "the adopted reservation is live");
        auto reacquired = RequireOk(table.ReserveRecord("pkg.srt.unexposed.move.a"),
                "the released name is reservable again");
        RequireStatus(table.AbandonUnexposed(overwritten), RecordStatus::None, "cleanup b");
        RequireStatus(table.AbandonUnexposed(reacquired), RecordStatus::None, "cleanup a");
    }
    RequireEqual(table.SnapshotUsage().slots_reserved, 0, "every reservation was released");
}

// Repeated registration of one fixed entrance on a confirmed live table
// consumes no new slot, keeps the incarnation, and never rebinds the entrance.
void SharedRole()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.shared", RecordRole::Client, sink);
    const auto before = table.Observe(channel.entrance);
    Require(before.role_exposed[RoleIndex(RecordRole::Client)], "role reports exposed");
    auto first = RequireOk(table.RegisterOwner(channel.entrance, 11), "first owner");
    auto second = RequireOk(table.RegisterOwner(channel.entrance, 12), "second owner on the same table");
    const auto after = table.Observe(channel.entrance);
    Require(after.record_incarnation == before.record_incarnation, "incarnation unchanged by registration");
    Require(after.slot == before.slot, "no second slot for a second owner");
    RequireEqual(after.role_owners[RoleIndex(RecordRole::Client)], 2, "two owners, one table");
    const auto usage = table.SnapshotUsage();
    RequireEqual(usage.records_created, 1, "one record created");
    RequireEqual(usage.slots_exposed, 1, "one slot consumed");
    RequireEqual(usage.owners, 2, "both owners charged to the record");
    RequireEqual(usage.owner_high_water, 2, "owner high water recorded");
    RequireStatus(StatusOf(table.Expose(channel.reservation, RecordRole::Client, sink)),
            RecordStatus::AlreadyExposed, "re-registration cannot rebind the slot");
    auto payload = Payload();
    Emit(channel.entrance, 11, payload);
    Emit(channel.entrance, 12, payload);
    RequireEqual(sink.Size(), 2, "both owners are attributable under one entrance");
    Require(sink.At(0).current_at_call && sink.At(1).current_at_call, "captured owners were current");
    Require(sink.At(0).bytes == payload && sink.At(1).bytes == payload, "borrowed bytes reach the sink");
    RequireStatus(table.ReleaseOwner(first), RecordStatus::None, "release first");
    RequireStatus(table.ReleaseOwner(second), RecordStatus::None, "release second");
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Unanchored, "no anchor remains");
}

// Releasing one owner never retires a table that still serves a sibling, and
// the released socket id is a terminal drop rather than a re-attribution.
void SiblingRelease()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.sibling", RecordRole::Client, sink);
    auto kept = RequireOk(table.RegisterOwner(channel.entrance, 21), "kept owner");
    auto gone = RequireOk(table.RegisterOwner(channel.entrance, 22), "released owner");
    auto payload = Payload();
    auto pin = RequireOk(table.Attribute(channel.entrance, 22), "capture the released-owner attribution");
    Require(pin.Current(), "the captured membership is current before the release");
    Emit(channel.entrance, 22, payload);
    RequireEqual(sink.Size(), 1, "the sibling's owner is attributable before the release");
    RequireStatus(table.ReleaseOwner(gone), RecordStatus::None, "release the sibling");
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Exposed, "the table is still live");
    RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 1,
            "the sibling is still registered");
    RequireStatus(table.ReleaseOwner(gone), RecordStatus::StaleHandle, "a released membership is stale");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, 22)), RecordStatus::UnknownSocket,
            "a released id is a terminal drop until it is registered again");
    auto rejoined = RequireOk(table.RegisterOwner(channel.entrance, 22),
            "a reused id may join later as a new membership");
    Require(!pin.Current(), "the old capture never becomes current again");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, 23)), RecordStatus::UnknownSocket,
            "an unregistered id is a terminal drop");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, 21)), RecordStatus::None,
            "the sibling is still attributable");
    const auto before = sink.Size();
    Emit(channel.entrance, 23, payload);
    RequireEqual(sink.Size(), before, "a dropped callback never reaches the sink");
    RequireEqual(table.SnapshotUsage().dropped_unknown_socket, 1, "the terminal drop is counted");
    {
        // A handle owns its association: overwriting one releases the owner it
        // held, and dropping a handle releases its owner, so no handle can
        // strand an owner that could never be released.
        auto transient = RequireOk(table.RegisterOwner(channel.entrance, 24), "transient owner");
        RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 3,
                "three owners before the overwrite");
        auto replacement = RequireOk(table.RegisterOwner(channel.entrance, 25), "replacement owner");
        RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 4,
                "four owners before the overwrite");
        transient = std::move(replacement);
        RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 3,
                "the overwritten owner was released");
        RequireStatus(StatusOf(table.Attribute(channel.entrance, 24)), RecordStatus::UnknownSocket,
                "the overwritten socket is no longer an owner");
        RequireStatus(StatusOf(table.Attribute(channel.entrance, 25)), RecordStatus::None,
                "the retained socket is still attributable");
    }
    RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 2,
            "the dropped handle released its owner");
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Exposed, "the table is still live");
    RequireStatus(table.ReleaseOwner(rejoined), RecordStatus::None, "release the rejoined membership");
    RequireStatus(table.ReleaseOwner(kept), RecordStatus::None, "release the kept owner");
}

// While one role anchors the record, the other role keeps both its incarnation
// and its fixed entrance, including across a temporary zero-owner period.
void CrossRole()
{
    auto& table = Table();
    Sink sink;
    auto client = Open("pkg.srt.cross", RecordRole::Client, sink);
    auto server = RequireOk(table.Expose(client.reservation, RecordRole::Server, sink), "expose server role");
    Require(server.function() != client.entrance.function(), "each role has its own fixed entrance");
    auto dial = RequireOk(table.RegisterOwner(client.entrance, 31), "client owner");
    auto listen = RequireOk(table.RegisterOwner(server, 41), "server owner");
    RequireStatus(table.ReleaseOwner(dial), RecordStatus::None, "the client role reaches zero owners");
    RequirePhase(table.Observe(client.entrance).phase, RecordPhase::Exposed, "the other role anchors the record");
    RequireEqual(table.Observe(client.entrance).role_owners[RoleIndex(RecordRole::Client)], 0,
            "the client role is empty");
    Require(table.Observe(client.entrance).role_exposed[RoleIndex(RecordRole::Client)],
            "an empty role keeps its incarnation");
    auto payload = Payload();
    Emit(server, 41, payload);
    RequireEqual(sink.Size(), 1, "the server role still serves");
    auto redial = RequireOk(table.RegisterOwner(client.entrance, 32), "re-register into the emptied role");
    RequireStatus(StatusOf(table.Expose(client.reservation, RecordRole::Client, sink)),
            RecordStatus::AlreadyExposed,
            "the emptied role keeps its issued entrance instead of deriving another");
    RequireStatus(table.ReleaseOwner(redial), RecordStatus::None, "release the re-dial");
    RequireStatus(table.ReleaseOwner(listen), RecordStatus::None, "release the last anchor");
    RequirePhase(table.Observe(client.entrance).phase, RecordPhase::Unanchored, "no anchor remains");
    // The already-issued server entrance is reported as such, never re-derived.
    RequireStatus(StatusOf(table.Expose(client.reservation, RecordRole::Server, sink)),
            RecordStatus::AlreadyExposed, "an issued role entrance is retained, not re-derived");
    // A role that was never exposed is not exposed on an unproven record.
    auto late = Open("pkg.srt.cross.late", RecordRole::Client, sink);
    auto late_owner = RequireOk(table.RegisterOwner(late.entrance, 33), "late owner");
    RequireStatus(table.ReleaseOwner(late_owner), RecordStatus::None, "release the late owner");
    RequireStatus(StatusOf(table.Expose(late.reservation, RecordRole::Server, sink)),
            RecordStatus::ContinuityUnproven, "a never-exposed role is not exposed on an unproven record");
}

// Unknown continuity stops new owners, new reservations of that name and any
// re-attribution. Other service names are unaffected.
void Unanchored()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.unanchored", RecordRole::Client, sink);
    auto owner = RequireOk(table.RegisterOwner(channel.entrance, 51), "owner");
    RequireStatus(table.ReleaseOwner(owner), RecordStatus::None, "release the last anchor");
    auto payload = Payload();
    const auto before = sink.Size();
    Emit(channel.entrance, 51, payload);
    RequireEqual(sink.Size(), before, "an unanchored callback is not delivered");
    RequireEqual(table.SnapshotUsage().dropped_unanchored, 1, "the unanchored drop is counted");
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.unanchored")), RecordStatus::ContinuityUnproven,
            "the same name is sealed while continuity is unproven");
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, 52)), RecordStatus::ContinuityUnproven,
            "no new owner under an uncertain incarnation");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, 51)), RecordStatus::ContinuityUnproven,
            "no re-attribution under an uncertain incarnation");
    Require(table.Observe(channel.entrance).name_sealed, "the sealed state is observable");
    auto other = RequireOk(table.ReserveRecord("pkg.srt.unanchored.other"), "another name is unaffected");
    RequireStatus(table.AbandonUnexposed(other), RecordStatus::None, "the other name cleans up");
}

// A proved destruction ends the record and never recycles the old slot: a
// same-name replacement receives a new slot, incarnation and entrance, and no
// old capture or old entrance reaches it.
void DestroyReplace()
{
    auto& table = Table();
    Sink sink;
    auto payload = Payload();
    auto old_channel = Open("pkg.srt.destroy", RecordRole::Client, sink);
    auto old_owner = RequireOk(table.RegisterOwner(old_channel.entrance, 61), "old owner");
    auto captured = RequireOk(table.Attribute(old_channel.entrance, 61), "capture the old attribution");
    Require(captured.Current(), "the capture is current while its membership lives");
    const auto old_observation = table.Observe(old_channel.entrance);
    Emit(old_channel.entrance, 61, payload);
    RequireEqual(sink.At(sink.Size() - 1).slot, old_observation.slot, "the old entrance attributed the old record");
    RequireStatus(table.ConfirmDestroyed(old_channel.entrance), RecordStatus::StillAnchored,
            "a live table is never retired by assumption");
    RequireStatus(table.ReleaseOwner(old_owner), RecordStatus::None, "release the old owner");
    RequireStatus(table.ConfirmDestroyed(old_channel.entrance), RecordStatus::None, "proved destruction");
    RequirePhase(table.Observe(old_channel.entrance).phase, RecordPhase::Tombstoned, "the slot is tombstoned");
    RequireStatus(table.Observe(old_channel.entrance).status, RecordStatus::Retired, "the old entrance is retired");
    const auto dropped = table.SnapshotUsage().dropped_retired;
    Emit(old_channel.entrance, 61, payload);
    RequireEqual(table.SnapshotUsage().dropped_retired - dropped, 1, "a late old callback is terminal");
    RequireStatus(StatusOf(table.Attribute(old_channel.entrance, 61)), RecordStatus::Retired,
            "the old entrance cannot be attributed");
    RequireStatus(table.ConfirmDestroyed(old_channel.entrance), RecordStatus::Retired,
            "destruction is settled once");
    Require(!captured.Current(), "the old capture is no longer current");
    RequireStatus(captured.Observe().status, RecordStatus::Retired, "the old capture observes the old record");
    RequireEqual(table.Observe(old_channel.entrance).callbacks_in_flight, 0,
            "a destroyed record reports its drain condition instead of hiding it");
    auto replacement = Open("pkg.srt.destroy", RecordRole::Client, sink);
    Require(replacement.entrance.function() != old_channel.entrance.function(),
            "a same-name record never inherits the retired entrance");
    const auto new_observation = table.Observe(replacement.entrance);
    Require(new_observation.slot != old_observation.slot, "the same-name replacement uses a new slot");
    Require(new_observation.record_incarnation != old_observation.record_incarnation, "a new incarnation");
    auto new_owner = RequireOk(table.RegisterOwner(replacement.entrance, 61),
            "the same numeric id joins as a new membership");
    const auto before = sink.Size();
    Emit(replacement.entrance, 61, payload);
    RequireEqual(sink.Size() - before, 1, "the new entrance serves the new owner");
    RequireEqual(sink.At(sink.Size() - 1).slot, new_observation.slot, "delivery carries the new attribution");
    RequireEqual(captured.Observe().slot, old_observation.slot, "the old capture never follows the replacement");
    RequireStatus(table.ReleaseOwner(new_owner), RecordStatus::None, "release the new owner");
}

// Destruction is refused while any anchor remains, and an exposure that no
// owner consumed is itself an anchor.
void StillAnchored()
{
    auto& table = Table();
    Sink sink;
    auto pending = Open("pkg.srt.anchored", RecordRole::Client, sink);
    RequireStatus(table.ConfirmDestroyed(pending.entrance), RecordStatus::StillAnchored,
            "a pending exposure is an anchor");
    RequireStatus(table.AbandonExposure(pending.entrance), RecordStatus::None, "a failed registration drops it");
    RequireStatus(table.AbandonExposure(pending.entrance), RecordStatus::ExposureSettled,
            "an exposure is abandoned once");
    RequireStatus(table.ConfirmDestroyed(pending.entrance), RecordStatus::None, "no anchor remains");
    auto registered = Open("pkg.srt.anchored.two", RecordRole::Server, sink);
    auto kept = RequireOk(table.RegisterOwner(registered.entrance, 72), "kept owner");
    RequireStatus(table.ConfirmDestroyed(registered.entrance), RecordStatus::StillAnchored,
            "a registered owner blocks destruction");
    RequireStatus(table.ReleaseOwner(kept), RecordStatus::None, "release the kept owner");
    RequireStatus(table.ConfirmDestroyed(registered.entrance), RecordStatus::None, "destruction after release");
}

// Slot exhaustion is decided before any entrance can reach the SDK, blocks only
// creations that need a new incarnation, and never recycles tombstones.
void Exhaustion()
{
    auto& table = Table();
    Sink sink;
    std::vector<Channel> channels;
    for (std::size_t i = 0; i < record_slot_capacity; ++i) {
        channels.push_back(Open("pkg.srt.exhaust." + std::to_string(i), RecordRole::Client, sink));
    }
    RequireEqual(table.SnapshotUsage().slots_exposed, record_slot_capacity, "all slots exposed");
    RequireEqual(table.SnapshotUsage().records_created, record_slot_capacity, "one record per slot");
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.exhaust.overflow")), RecordStatus::SlotExhausted,
            "a new incarnation is refused before exposure");
    auto live = RequireOk(table.RegisterOwner(channels[0].entrance, 81), "an existing live table still registers");
    RequireEqual(table.SnapshotUsage().owners, 1, "the owner is charged without a new slot");
    RequireStatus(table.ReleaseOwner(live), RecordStatus::None, "release");
    RequireStatus(table.AbandonExposure(channels[1].entrance), RecordStatus::None, "abandon one exposure");
    RequireStatus(table.ConfirmDestroyed(channels[1].entrance), RecordStatus::None, "tombstone one slot");
    RequireEqual(table.SnapshotUsage().slots_tombstoned, 1, "the tombstone is recorded");
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.exhaust.1")), RecordStatus::SlotExhausted,
            "a tombstoned slot is never recycled");
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.exhaust.overflow")), RecordStatus::SlotExhausted,
            "exhaustion persists");
    RequireEqual(table.SnapshotUsage().records_created, record_slot_capacity, "records_created is monotonic");
}

// A failed first registration cannot reclaim the slot, and the retained
// entrance stays usable for a later owner while an anchor still exists.
void FailedRegistration()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.failed", RecordRole::Client, sink);
    RequireStatus(table.AbandonExposure(channel.entrance), RecordStatus::None, "the registration failed");
    RequireStatus(table.AbandonUnexposed(channel.reservation), RecordStatus::NotUnexposed,
            "the slot is not reclaimed");
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Unanchored,
            "an entrance handed out without an owner has unproven continuity");
    RequireStatus(StatusOf(table.Expose(channel.reservation, RecordRole::Client, sink)),
            RecordStatus::AlreadyExposed, "the exposure is not re-derived");
    auto anchored = Open("pkg.srt.failed.anchored", RecordRole::Client, sink);
    auto server = RequireOk(table.Expose(anchored.reservation, RecordRole::Server, sink), "expose server role");
    auto owner = RequireOk(table.RegisterOwner(anchored.entrance, 91), "client anchor");
    RequireStatus(table.AbandonExposure(server), RecordStatus::None, "the server registration failed later");
    RequirePhase(table.Observe(anchored.entrance).phase, RecordPhase::Exposed,
            "a sibling anchor keeps the record live");
    Require(table.Observe(anchored.entrance).role_exposed[RoleIndex(RecordRole::Server)],
            "the failed role keeps its exposed entrance");
    auto later = RequireOk(table.RegisterOwner(server, 92), "the retained entrance still registers owners");
    RequireStatus(table.ReleaseOwner(owner), RecordStatus::None, "release the client anchor");
    RequireStatus(table.ReleaseOwner(later), RecordStatus::None, "release the server owner");
}

// Owner capacity, duplicate ids and invalid input are refused without damaging
// the live table.
void Capacity()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.capacity", RecordRole::Client, sink);
    auto server = RequireOk(table.Expose(channel.reservation, RecordRole::Server, sink), "expose server role");
    std::vector<RecordMembership> members;
    for (std::int32_t i = 0; i < 12; ++i) {
        members.push_back(RequireOk(table.RegisterOwner(channel.entrance, 100 + i), "an owner within capacity"));
    }
    RequireEqual(table.SnapshotUsage().owners, 12, "capacity reached");
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, 200)), RecordStatus::Capacity,
            "capacity is refused");
    RequireStatus(StatusOf(table.RegisterOwner(server, 100)), RecordStatus::ForeignIdCollision,
            "one numeric id is never two roles of one record");
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, 100)), RecordStatus::AlreadyMember,
            "the same socket is registered once per role");
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, -1)), RecordStatus::InvalidSocket,
            "a negative socket id is invalid");
    auto payload = Payload();
    Emit(channel.entrance, 100, payload);
    RequireEqual(sink.Size(), 1, "refusals leave the live table serving");
    for (auto& member : members) RequireStatus(table.ReleaseOwner(member), RecordStatus::None, "release a member");
    // The unclaimed server exposure is itself an anchor, exactly as in the
    // still-anchored case, so the record stays live until it is abandoned.
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Exposed,
            "an unclaimed exposure still anchors the record");
    RequireStatus(table.AbandonExposure(server), RecordStatus::None, "abandon the unclaimed server exposure");
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Unanchored, "all anchors released");
}

// Invalid names, invalid roles and empty handles are refused without touching
// live state.
void Handles()
{
    auto& table = Table();
    Sink sink;
    RequireStatus(StatusOf(table.ReserveRecord("")), RecordStatus::InvalidString, "empty name");
    RequireStatus(StatusOf(table.ReserveRecord(std::string(256, 'a'))), RecordStatus::InvalidString,
            "oversized name");
    RequireStatus(StatusOf(table.ReserveRecord(std::string("pkg.srt\0nul", 11))), RecordStatus::InvalidString,
            "embedded NUL name");
    RequireStatus(table.AbandonUnexposed({}), RecordStatus::NotFound, "empty reservation");
    RequireStatus(StatusOf(table.Expose({}, RecordRole::Client, sink)), RecordStatus::NotFound,
            "empty reservation");
    RequireStatus(table.AbandonExposure({}), RecordStatus::NotFound, "empty entrance");
    RequireStatus(StatusOf(table.RegisterOwner({}, 1)), RecordStatus::NotFound, "empty entrance");
    RequireStatus(StatusOf(table.Attribute({}, 1)), RecordStatus::NotFound, "empty entrance");
    RequireStatus(table.ReleaseOwner({}), RecordStatus::NotFound, "empty membership");
    RequireStatus(table.ConfirmDestroyed({}), RecordStatus::NotFound, "empty entrance");
    auto channel = Open("pkg.srt.handles", RecordRole::Client, sink);
    RequireStatus(StatusOf(table.Expose(channel.reservation, RecordRole::Count, sink)),
            RecordStatus::InvalidConfiguration, "role out of range");
    Sink other_sink;
    RequireStatus(StatusOf(table.Expose(channel.reservation, RecordRole::Server, other_sink)),
            RecordStatus::SinkMismatch, "one record serves one sink");
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, -5)), RecordStatus::InvalidSocket, "negative id");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, -5)), RecordStatus::InvalidSocket, "negative id");
    RequireStatus(StatusOf(table.Attribute(channel.entrance, INT32_MAX)), RecordStatus::UnknownSocket,
            "a valid unused id is a terminal drop");
    auto owner = RequireOk(table.RegisterOwner(channel.entrance, 111), "owner");
    RequireStatus(table.ReleaseOwner(owner), RecordStatus::None, "release");
    RequireEqual(table.SnapshotUsage().slots_free, record_slot_capacity - 1,
            "no invalid operation consumed a slot");
    // Invalid callback input through the real entrance: rejected, counted, and
    // never delivered.
    auto payload = Payload();
    const auto before_invalid = table.SnapshotUsage().dropped_invalid;
    channel.entrance.function()(111, nullptr, 4);
    channel.entrance.function()(111, payload.data(), 0);
    channel.entrance.function()(-1, payload.data(), static_cast<std::uint32_t>(payload.size()));
    RequireEqual(table.SnapshotUsage().dropped_invalid - before_invalid, 3,
            "invalid callback input is rejected and accounted");
    RequireEqual(static_cast<std::size_t>(sink.entered.load()), 0, "invalid input never reaches the sink");
}

// The sink runs outside the table gate: it may reenter the table, and a throwing
// sink is absorbed and accounted instead of escaping into the SDK callback.
void Reentrant()
{
    auto& table = Table();
    Sink sink;
    auto channel = Open("pkg.srt.reentrant", RecordRole::Client, sink);
    auto owner = RequireOk(table.RegisterOwner(channel.entrance, 121), "owner");
    auto payload = Payload();
    // A membership handle owns its association, so the reentrant registration
    // is retained here instead of dropped.
    // Hook outcomes are recorded, never asserted: Dispatch absorbs a hook's
    // exception into callback_exceptions, so an assertion raised inside a hook
    // could not fail this case. Every check runs after the entrance returns.
    RecordMembership reentrant;
    RecordPhase observed_phase = RecordPhase::Free;
    std::size_t observed_in_flight = 0, observed_usage_in_flight = 0;
    RecordStatus reentrant_status = RecordStatus::NotImplemented;
    sink.reenter = [&] {
        observed_phase = table.Observe(channel.entrance).phase;
        observed_in_flight = table.Observe(channel.entrance).callbacks_in_flight;
        observed_usage_in_flight = table.SnapshotUsage().callbacks_in_flight;
        auto added = table.RegisterOwner(channel.entrance, 122);
        reentrant_status = added.status;
        if (added) reentrant = std::move(*added.value);
    };
    Emit(channel.entrance, 121, payload);
    RequireEqual(sink.Size(), 1, "attributed once");
    RequirePhase(observed_phase, RecordPhase::Exposed, "a reentrant observation is not deadlocked");
    RequireEqual(observed_in_flight, 1, "the running sink is visible as one in-flight callback");
    RequireEqual(observed_usage_in_flight, 1, "in-flight callbacks are counted");
    RequireStatus(reentrant_status, RecordStatus::None, "a reentrant registration succeeds");
    RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 2,
            "the reentrant registration took effect");
    RequireEqual(table.Observe(channel.entrance).callbacks_in_flight, 0,
            "a returned callback leaves nothing in flight");
    sink.reenter = nullptr;
    bool escaped = false;
    RecordStatus reentrant_release = RecordStatus::NotImplemented;
    sink.reenter_throw = [&] {
        reentrant_release = table.ReleaseOwner(owner);
        throw std::runtime_error("intentional sink exception");
    };
    try { Emit(channel.entrance, 122, payload); }
    catch (...) { escaped = true; }
    Require(!escaped, "a callback exception never escapes the entrance barrier");
    RequireStatus(reentrant_release, RecordStatus::None, "a reentrant release succeeds");
    RequireEqual(table.SnapshotUsage().callback_exceptions, 1, "the exception is absorbed and counted");
    // The exception path must still release the in-flight permit; without this
    // assertion a leaked permit would be invisible until a caller waited on it.
    RequireEqual(table.SnapshotUsage().callbacks_in_flight, 0,
            "a throwing callback releases its in-flight permit");
    RequireEqual(table.Observe(channel.entrance).callbacks_in_flight, 0,
            "the record reports no in-flight callback after an absorbed exception");
    sink.reenter_throw = nullptr;
    Emit(channel.entrance, 122, payload);
    RequireEqual(sink.Size(), 3, "the entrance keeps serving after an absorbed exception");
}

// Concurrent attribution against a concurrent release. Only invariants are
// asserted: every attributed record belongs to the original record, every
// callback is accounted exactly once, and no callback is misattributed.
void Concurrent()
{
    auto& table = Table();
    Sink sink(20000);
    auto channel = Open("pkg.srt.concurrent", RecordRole::Client, sink);
    const auto expected = table.Observe(channel.entrance);
    std::vector<RecordMembership> members;
    for (std::int32_t id = 131; id <= 134; ++id) {
        members.push_back(RequireOk(table.RegisterOwner(channel.entrance, id), "a concurrent owner"));
    }
    auto payload = Payload();
    std::atomic<bool> start{false};
    std::atomic<int> dispatched{0};
    std::atomic<int> release_failures{0};
    constexpr int per_thread = 1500;
    std::vector<std::thread> threads;
    for (int t = 0; t < 3; ++t) {
        threads.emplace_back([&, t] {
            while (!start.load(std::memory_order_acquire)) {}
            for (int i = 0; i < per_thread; ++i) {
                const std::int32_t id = (i % 5 == 4) ? 199 : 131 + ((i + t) % 4);
                Emit(channel.entrance, id, payload);
                dispatched.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    std::thread releaser([&] {
        while (!start.load(std::memory_order_acquire)) {}
        // Start releasing only after one real attribution, so this case always
        // exercises a live table instead of one whose owners are already gone.
        // That is a precondition, not an interleaving proof, and it is the only
        // scheduling constraint here: the assertions below are invariants that
        // hold under any schedule, as PLAN 18.4 requires. A case that instead
        // asserted "the releases really overlapped the callbacks" would fail a
        // correct implementation on a host that simply never schedules the
        // releaser until the emitters have finished.
        while (sink.entered.load(std::memory_order_acquire) == 0) {}
        for (auto& member : members) {
            if (table.ReleaseOwner(member) != RecordStatus::None) release_failures.fetch_add(1);
        }
    });
    start.store(true, std::memory_order_release);
    releaser.join();
    for (auto& thread : threads) thread.join();
    RequireEqual(static_cast<std::size_t>(release_failures.load()), 0, "every concurrent release succeeded");
    const auto usage = table.SnapshotUsage();
    RequireEqual(static_cast<std::size_t>(dispatched.load()), 3 * static_cast<std::size_t>(per_thread),
            "all callbacks were dispatched");
    RequireEqual(usage.attributed + usage.dropped_unknown_socket + usage.dropped_unanchored,
            3 * static_cast<std::size_t>(per_thread), "every callback is accounted exactly once");
    RequireEqual(usage.attributed, sink.Size(), "the attributed count equals the delivered count");
    RequireEqual(static_cast<std::size_t>(sink.entered.load()), usage.attributed, "the sink entered once per attribution");
    RequireEqual(usage.dropped_retired, 0, "a live record never reports a retired entrance");
    RequireEqual(usage.dropped_invalid, 0, "no valid callback was rejected as invalid");
    RequireEqual(usage.callback_exceptions, 0, "no concurrent callback threw");
    RequireEqual(usage.callbacks_in_flight, 0, "every attributed callback returned before the join");
    for (std::size_t i = 0; i < sink.Size(); ++i) {
        const auto record = sink.At(i);
        Require(record.socket >= 131 && record.socket <= 134, "only registered ids are attributed");
        Require(record.slot == expected.slot, "no attribution leaves the original slot");
        Require(record.record_incarnation == expected.record_incarnation, "no attribution changes incarnation");
        // Current() is deliberately not asserted here: a concurrent release may
        // land between the attribution and the sink call. What must never move
        // is the captured (slot, incarnation, socket), which is asserted above.
    }
    RequirePhase(table.Observe(channel.entrance).phase, RecordPhase::Unanchored, "the releases took effect");
    const auto drained = table.SnapshotUsage();
    Emit(channel.entrance, 131, payload);
    RequireEqual(table.SnapshotUsage().dropped_unanchored - drained.dropped_unanchored, 1,
            "after the release every callback is a terminal unanchored drop");
    RequireStatus(table.ConfirmDestroyed(channel.entrance), RecordStatus::None, "destruction after the drain");
}

// A refused bounded-identity condition leaves no partial state: no slot is
// consumed and no owner is charged.
void IdentityBoundary()
{
    auto& table = Table();
    Sink sink;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    ServiceRecordTableTestAccess::NextIdentities(table, maximum, maximum);
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.identity")), RecordStatus::IdentityExhausted,
            "an exhausted record identity is refused");
    RequireEqual(table.SnapshotUsage().slots_reserved, 0, "the refusal consumes no slot");
    ServiceRecordTableTestAccess::NextIdentities(table, 100, maximum);
    RequireStatus(StatusOf(table.ReserveRecord("pkg.srt.identity")), RecordStatus::IdentityExhausted,
            "an exhausted reservation identity is refused");
    RequireEqual(table.SnapshotUsage().slots_reserved, 0, "the refusal consumes no slot either");
    ServiceRecordTableTestAccess::NextIdentities(table, 100, 100);
    auto channel = Open("pkg.srt.identity", RecordRole::Client, sink);
    const auto slot = table.Observe(channel.entrance).slot;
    ServiceRecordTableTestAccess::NextOwnerIncarnation(table, slot, maximum);
    RequireStatus(StatusOf(table.RegisterOwner(channel.entrance, 141)), RecordStatus::IdentityExhausted,
            "an exhausted owner identity is refused");
    RequireEqual(table.SnapshotUsage().owners, 0, "the refusal charges no owner");
    RequireEqual(table.Observe(channel.entrance).role_owners[RoleIndex(RecordRole::Client)], 0,
            "the refused registration left no owner behind");
    ServiceRecordTableTestAccess::NextOwnerIncarnation(table, slot, 500);
    auto owner = RequireOk(table.RegisterOwner(channel.entrance, 141), "registration recovers afterwards");
    RequireStatus(table.ReleaseOwner(owner), RecordStatus::None, "release");
    RequireStatus(table.ConfirmDestroyed(channel.entrance), RecordStatus::None, "release the last anchor");
}

// An allocation failure is reported as such and leaves the table exactly as it
// was, so the same name can be reserved once allocation succeeds again.
void AllocationFailurePath()
{
    auto& table = Table();
    Sink sink;
    const std::string name = "pkg.srt.allocation";
    deny_allocations = true;
    const auto refused = table.ReserveRecord(name);
    deny_allocations = false;
    RequireStatus(StatusOf(refused), RecordStatus::AllocationFailure, "an allocation failure is reported");
    RequireEqual(table.SnapshotUsage().slots_reserved, 0, "no slot is consumed by the failure");
    RequireEqual(table.SnapshotUsage().slots_free, record_slot_capacity, "every slot is still free");
    auto channel = Open(name, RecordRole::Client, sink);
    RequireEqual(table.SnapshotUsage().slots_exposed, 1, "the same name is reservable after recovery");
    auto owner = RequireOk(table.RegisterOwner(channel.entrance, 151), "registration after recovery");
    RequireStatus(table.ReleaseOwner(owner), RecordStatus::None, "release");
}
} // namespace

int main(int argc, char** argv)
{
    static_assert(std::is_same_v<RecordBytesCallback, decltype(ISocketListener::OnBytes)>,
            "the entrance shape must be the public Socket byte-callback type");
    static_assert(!std::is_copy_constructible_v<RecordReservation>, "a reservation is unique");
    static_assert(!std::is_copy_constructible_v<RecordMembership>, "a membership is released once");
    static_assert(!std::is_copy_constructible_v<RecordAttribution>, "an attribution is captured once");
    const std::map<std::string, void (*)()> cases{
        {"unexposed", Unexposed}, {"shared-role", SharedRole}, {"sibling-release", SiblingRelease},
        {"cross-role", CrossRole}, {"unanchored", Unanchored}, {"destroy-replace", DestroyReplace},
        {"still-anchored", StillAnchored}, {"exhaustion", Exhaustion},
        {"failed-registration", FailedRegistration}, {"capacity", Capacity}, {"handles", Handles},
        {"reentrant", Reentrant}, {"concurrent", Concurrent},
        {"identity-boundary", IdentityBoundary}, {"allocation-failure", AllocationFailurePath}};
    // Exactly one case per process: the entrance pool is process-lifetime.
    if (argc != 3 || std::string(argv[1]) != "--case" || cases.count(argv[2]) == 0) return 64;
    try {
        cases.at(argv[2])();
        std::cout << "HOST_MODEL_PASS " << argv[2]
                  << " native_qualification=NOT_CLAIMED device=NOT_RUN\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << argv[2] << ": " << e.what() << '\n';
        return 1;
    }
}
