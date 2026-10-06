#include "softbus_ingress_contract.hpp"
#include "socket.h" // Current source header, not a claim of firmware equivalence.

#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>

using namespace fastdds_dsoftbus_probe;
namespace {
void Require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
struct Receiver final : DiagnosticReceiver {
    std::vector<std::vector<std::uint8_t>> messages;
    std::function<void()> while_receiving;
    void Receive(std::int32_t, const std::vector<std::uint8_t>& bytes) override {
        messages.push_back(bytes);
        if (while_receiving) while_receiving();
    }
};
struct Channel {
    NativeTableToken table;
    PublicBytesCallback callback = nullptr;
    std::shared_ptr<Receiver> receiver = std::make_shared<Receiver>();
    DiagnosticAttachment attachment;
    explicit Channel(bool attached = true, std::int32_t socket = 23) {
        auto& bank = IngressDiagnosticBank::Process();
        table = bank.Reserve(); Require(static_cast<bool>(table), "reserve native callback table");
        callback = bank.Expose(table, socket); Require(callback != nullptr, "expose real-shaped function pointer");
        if (attached) {
            attachment = bank.Attach(table, receiver);
            Require(static_cast<bool>(attachment), "attach application");
        }
    }
};
std::vector<std::uint8_t> Payload() { return {1, 2, 3, 4}; }
void Emit(PublicBytesCallback callback, const std::vector<std::uint8_t>& bytes) {
    callback(23, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

// A scheduler oracle identifies the original native event for assertions ONLY.
// Dispatch accepts precisely the three public OnBytes arguments. The bank never
// receives the oracle, a generation, a context pointer, or a native event token.
enum class Oracle { Old, New };
struct Scheduled {
    Oracle oracle;
    PublicBytesCallback table;
    std::int32_t socket;
    const std::vector<std::uint8_t>* bytes;
    void Dispatch() const { table(socket, bytes->data(), static_cast<std::uint32_t>(bytes->size())); }
};
void Ambiguous(const Scheduled& old_event, const Scheduled& new_event, const char* case_name) {
    Require(old_event.oracle != new_event.oracle, "oracle says different native origin");
    Require(old_event.table == new_event.table && old_event.socket == new_event.socket &&
            old_event.bytes->data() == new_event.bytes->data() && *old_event.bytes == *new_event.bytes,
            "complete callback-visible tuple identical, including borrowed data address");
    old_event.Dispatch(); new_event.Dispatch();
    std::cout << "identity=" << case_name << " verdict=AMBIGUOUS platform=UNQUALIFIED "
              << "premise=controlled_host_schedule board_reproduction=NOT_CLAIMED\n";
}
void ReusableUnexposed() {
    auto& b = IngressDiagnosticBank::Process();
    auto old = b.Reserve(); Require(static_cast<bool>(old), "reserve initial unexposed");
    Require(b.AbandonUnexposed(old), "unexposed reservation may return");
    auto fresh = b.Reserve(); Require(static_cast<bool>(fresh), "reserve again");
    Require(!b.AbandonUnexposed(old) && b.Expose(old, 23) == nullptr, "stale reservation cannot expose/release new one");
    Require(b.Expose(fresh, 23) != nullptr, "fresh reservation exposes");
    Require(!b.AbandonUnexposed(fresh), "exposed native table cannot return to free");
    Require(b.Retire(fresh) && b.Expose(fresh, 23) == nullptr, "failed exposed creation permanently tombstones");
    Require(b.Stats().exposed == 1 && b.Stats().tombstoned == 1, "exposed failure counted permanently");
}
void SlotExhaustion() {
    auto& b = IngressDiagnosticBank::Process();
    std::vector<PublicBytesCallback> functions;
    for (std::size_t i = 0; i < ingress_diagnostic_slots; ++i) {
        Channel c;
        for (auto f : functions) Require(f != c.callback, "distinct immutable trampoline address per slot");
        functions.push_back(c.callback); Require(b.Retire(c.table), "retire native incarnation");
    }
    Require(!b.Reserve(), "process-lifetime exhausted slots cannot reset or recycle");
    Require(b.Stats().tombstoned == ingress_diagnostic_slots, "all exposed slots remain tombstones");
}
void ReattachStableBudget() {
    auto& b = IngressDiagnosticBank::Process(); Channel c;
    for (int i = 0; i < 40; ++i) {
        Require(b.BeginDetach(c.table, c.attachment) && b.DetachComplete(c.attachment), "empty application detaches");
        c.attachment = b.Attach(c.table, c.receiver); Require(static_cast<bool>(c.attachment), "same live InputChannel reattaches");
        Require(b.Stats().exposed == 1 && b.Stats().tombstoned == 0, "application rebuilding consumes no native slot");
    }
}
void RetiredTable() {
    auto& b = IngressDiagnosticBank::Process(); Channel old;
    auto bytes = Payload(); Scheduled pending{Oracle::Old, old.callback, 23, &bytes};
    Require(b.Retire(old.table), "retire while old copied callback pending");
    Channel fresh; pending.Dispatch(); Emit(fresh.callback, bytes);
    Require(!b.Take(old.table), "retired callback must not queue");
    auto record = b.Take(fresh.table); Require(record.Deliver(), "new incarnation delivers");
    Require(old.receiver->messages.empty() && fresh.receiver->messages.size() == 1, "old table cannot reach new receiver");
    Require(b.Stats().dropped_retired == 1, "late old table is terminal drop");
    // The diagnostic has no native Shutdown capability: it cannot close socket
    // 23 based on this possibly reused number. No fake shutdown workaround.
}
void SameTableIdReuse() {
    auto& b = IngressDiagnosticBank::Process(); Channel c; auto bytes = Payload();
    // Conditional allocator reuse experiment, not a claim that the deployed
    // SDK wraps/reuses public socket IDs in an ordinary application lifetime.
    std::map<int, Oracle> native_owners{{23, Oracle::Old}};
    Scheduled old{native_owners.at(23), c.callback, 23, &bytes};
    native_owners.erase(23); native_owners.emplace(23, Oracle::New);
    Scheduled fresh{native_owners.at(23), c.callback, 23, &bytes};
    Ambiguous(old, fresh, "same_table_numeric_id_reuse");
    auto a = b.Take(c.table), n = b.Take(c.table); Require(a.Deliver() && n.Deliver(), "both indistinguishable records follow same routing");
    Require(c.receiver->messages.size() == 2, "no invented generation filters old event");
}
void PrelookupRemap() {
    auto& b = IngressDiagnosticBank::Process(); Channel old; auto bytes = Payload();
    // Both SDK-internal events carry only channel ID and data until lookup.
    struct Internal { Oracle oracle; int channel; } delayed{Oracle::Old, 8}, genuine{Oracle::New, 8};
    Require(b.Retire(old.table), "retire original mapping before old event lookup");
    Channel fresh(true, 24); // Reusing channel 8 does NOT require public ID reuse.
    std::map<int, std::pair<PublicBytesCallback, std::int32_t>> current{{8, {fresh.callback, 24}}};
    auto lookup = [&](Internal e) { auto m = current.at(e.channel); return Scheduled{e.oracle, m.first, m.second, &bytes}; };
    Ambiguous(lookup(delayed), lookup(genuine), "sdk_current_mapping_before_ingress");
    auto a = b.Take(fresh.table), n = b.Take(fresh.table); Require(a.Deliver() && n.Deliver(), "current table cannot identify historical origin");
    Require(fresh.receiver->messages.size() == 2 && b.Stats().dropped_retired == 0, "old trampoline was never called");
}
void OldReceiverLease() {
    auto& b = IngressDiagnosticBank::Process(); Channel c; auto bytes = Payload();
    Emit(c.callback, bytes); auto captured = b.Take(c.table); Require(static_cast<bool>(captured), "capture old receiver lease");
    auto old = c.attachment; Require(b.BeginDetach(c.table, old), "begin old detach");
    Require(!b.DetachComplete(old), "old execution lease keeps detach pending");
    auto fresh = std::make_shared<Receiver>(); auto attached = b.Attach(c.table, fresh);
    Require(static_cast<bool>(attached), "new application attaches to live native table");
    Require(captured.Deliver(), "old leased record finishes");
    Require(c.receiver->messages.size() == 1 && fresh->messages.empty(), "captured old record never re-resolves new receiver");
    Require(b.DetachComplete(old), "old receiver drain complete only after lease release");
    Emit(c.callback, bytes); Require(b.Take(c.table).Deliver() && fresh->messages.size() == 1, "post-capture new ingress sees new app");
}
void NoApplicationDrop() {
    auto& b = IngressDiagnosticBank::Process(); Channel c(false); auto bytes = Payload();
    Emit(c.callback, bytes); Require(!b.Take(c.table), "no-app record is dropped at ingress");
    c.attachment = b.Attach(c.table, c.receiver); Require(static_cast<bool>(c.attachment), "later application attaches");
    Require(!b.Take(c.table) && c.receiver->messages.empty(), "no-app record never cached for future application");
    Require(b.Stats().dropped_no_application == 1, "terminal no-app disposition counted");
}
void BeforeIngressRebuild() {
    auto& b = IngressDiagnosticBank::Process(); Channel c; auto bytes = Payload();
    Scheduled sdk_pending{Oracle::Old, c.callback, 23, &bytes};
    Require(b.BeginDetach(c.table, c.attachment) && b.DetachComplete(c.attachment), "no adapter record existed at detach");
    auto fresh = std::make_shared<Receiver>(); Require(static_cast<bool>(b.Attach(c.table, fresh)), "rebuild same native channel");
    Scheduled genuine{Oracle::New, c.callback, 23, &bytes};
    Ambiguous(sdk_pending, genuine, "application_rebuild_before_adapter_capture");
    Require(b.Take(c.table).Deliver() && b.Take(c.table).Deliver(), "both enter after new attachment linearization");
    Require(fresh->messages.size() == 2 && c.receiver->messages.empty(), "current attachment not claimed to be original event generation");
}
void OwnedBoundedRecords() {
    auto& b = IngressDiagnosticBank::Process(); Channel c; auto bytes = Payload(); const auto original = bytes;
    Emit(c.callback, bytes); auto held = b.Take(c.table); bytes.assign(bytes.size(), 99);
    Require(held.Deliver() && c.receiver->messages.front() == original, "callback buffer may be overwritten immediately");
    std::vector<CapturedIngressRecord> leases;
    for (std::size_t i = 0; i < ingress_diagnostic_records; ++i) { Emit(c.callback, bytes); leases.push_back(b.Take(c.table)); }
    Emit(c.callback, bytes); Require(!b.Take(c.table), "dequeued execution leases still consume bounded budget");
    Require(b.Stats().outstanding == ingress_diagnostic_records && b.Stats().dropped_budget == 1, "owned records fully accounted");
    leases.clear(); Require(b.Stats().outstanding == 0, "abandon record releases budget and lease");
    c.callback(23, nullptr, 4); c.callback(99, bytes.data(), 4);
    std::vector<std::uint8_t> oversize(ingress_diagnostic_message_bytes + 1); Emit(c.callback, oversize);
    Require(b.Stats().dropped_invalid == 2 && b.Stats().dropped_unknown_socket == 1, "invalid/bounded input rejected");
}
void ReentrantDelivery() {
    auto& b = IngressDiagnosticBank::Process(); Channel c; auto bytes = Payload();
    c.receiver->while_receiving = [&] {
        Require(b.BeginDetach(c.table, c.attachment), "receiver can reenter adapter without held adapter lock");
        Require(!b.DetachComplete(c.attachment), "invoking old lease still pending");
        throw std::runtime_error("intentional receiver exception");
    };
    Emit(c.callback, bytes); auto record = b.Take(c.table); bool threw = false;
    try { record.Deliver(); } catch (const std::runtime_error&) { threw = true; }
    Require(threw && !record && b.DetachComplete(c.attachment) && b.Stats().outstanding == 0,
            "receiver exception releases lease and cannot redeliver");

    struct ReentrantDestructor final : DiagnosticReceiver {
        std::function<void()> on_destroy;
        void Receive(std::int32_t, const std::vector<std::uint8_t>&) override {}
        ~ReentrantDestructor() override { on_destroy(); }
    };
    bool destroyed = false;
    auto retiring = b.Reserve(); Require(b.Expose(retiring, 24) != nullptr, "reserve second native table");
    auto receiver = std::make_shared<ReentrantDestructor>();
    receiver->on_destroy = [&] { (void)b.Stats(); destroyed = true; };
    auto attachment = b.Attach(retiring, receiver); Require(static_cast<bool>(attachment), "attach retiring receiver");
    receiver.reset(); attachment = {};
    Require(b.Retire(retiring) && destroyed, "last receiver destruction occurs after releasing adapter lock");
}
} // namespace

int main(int argc, char** argv) {
    static_assert(std::is_same_v<PublicBytesCallback, decltype(ISocketListener::OnBytes)>);
    static_assert(!std::is_copy_constructible_v<CapturedIngressRecord>);
    const std::map<std::string, void (*)()> cases{
        {"unexposed", ReusableUnexposed}, {"exhaustion", SlotExhaustion}, {"reattach", ReattachStableBudget},
        {"retired", RetiredTable}, {"same-table", SameTableIdReuse}, {"prelookup", PrelookupRemap},
        {"old-lease", OldReceiverLease}, {"no-app", NoApplicationDrop}, {"pre-ingress", BeforeIngressRebuild},
        {"bounded", OwnedBoundedRecords}, {"reentrant", ReentrantDelivery}};
    // Exactly one case/process: never reset a process-lifetime callback bank.
    if (argc != 3 || std::string(argv[1]) != "--case" || cases.count(argv[2]) == 0) return 64;
    try { cases.at(argv[2])(); std::cout << "HOST_DIAGNOSTIC_PASS " << argv[2] << " native_qualification=NOT_CLAIMED\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL " << argv[2] << ": " << e.what() << '\n'; return 1; }
}
