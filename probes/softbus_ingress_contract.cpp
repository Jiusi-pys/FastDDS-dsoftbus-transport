#include "softbus_ingress_contract.hpp"

#include <array>
#include <atomic>
#include <deque>
#include <limits>
#include <mutex>
#include <utility>

namespace mdds_probe {
namespace ingress_detail {
struct Application {
    std::shared_ptr<DiagnosticReceiver> receiver;
    std::atomic<std::size_t> outstanding{0};
    std::atomic<bool> detached{false};
};
struct Budget { std::atomic<std::size_t> outstanding{0}; };
struct Captured {
    std::shared_ptr<Application> application;
    std::shared_ptr<Budget> budget;
    std::int32_t socket;
    std::vector<std::uint8_t> bytes;

    Captured(std::shared_ptr<Application> owner, std::shared_ptr<Budget> resource,
            std::int32_t id, const std::uint8_t* data, std::size_t length)
        : application(std::move(owner)), budget(std::move(resource)), socket(id), bytes(data, data + length) {
        // No fallible operation follows accounting. The single capture lock
        // held by Ingress makes owner selection and lease acquisition atomic.
        application->outstanding.fetch_add(1, std::memory_order_relaxed);
        budget->outstanding.fetch_add(1, std::memory_order_relaxed);
    }
    ~Captured() {
        budget->outstanding.fetch_sub(1, std::memory_order_release);
        application->outstanding.fetch_sub(1, std::memory_order_release);
    }
};
} // namespace ingress_detail

namespace {
struct Slot {
    IncarnationState state = IncarnationState::Free;
    std::uint64_t reservation = 0;
    std::int32_t socket = -1;
    std::shared_ptr<ingress_detail::Application> application;
    std::shared_ptr<ingress_detail::Budget> budget;
    std::deque<CapturedIngressRecord> records;
};
struct BankState {
    std::mutex mutex;
    std::array<Slot, ingress_diagnostic_slots> slots;
    std::uint64_t next_reservation = 1;
    IngressDiagnosticStats stats;
};
BankState& State() { static BankState state; return state; }
} // namespace

CapturedIngressRecord::CapturedIngressRecord() noexcept = default;
CapturedIngressRecord::~CapturedIngressRecord() = default;
CapturedIngressRecord::CapturedIngressRecord(CapturedIngressRecord&&) noexcept = default;
CapturedIngressRecord& CapturedIngressRecord::operator=(CapturedIngressRecord&&) noexcept = default;
CapturedIngressRecord::operator bool() const noexcept { return static_cast<bool>(captured_); }
bool CapturedIngressRecord::Deliver() {
    auto owned = std::move(captured_);
    if (!owned) return false;
    // No adapter mutex or current-application lookup here. Local ownership
    // releases the lease on normal return AND when the receiver throws.
    owned->application->receiver->Receive(owned->socket, owned->bytes);
    return true;
}
IngressDiagnosticBank& IngressDiagnosticBank::Process() { static IngressDiagnosticBank bank; return bank; }

NativeTableToken IngressDiagnosticBank::Reserve() {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    NativeTableToken token;
    if (state.next_reservation == std::numeric_limits<std::uint64_t>::max()) return token;
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        auto& slot = state.slots[i];
        if (slot.state != IncarnationState::Free) continue;
        slot.state = IncarnationState::ReservedUnexposed;
        slot.reservation = state.next_reservation++;
        token.slot_ = i; token.reservation_ = slot.reservation;
        break;
    }
    return token;
}
bool IngressDiagnosticBank::AbandonUnexposed(const NativeTableToken& token) {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    if (!token || token.slot_ >= state.slots.size()) return false;
    auto& slot = state.slots[token.slot_];
    if (slot.reservation != token.reservation_ || slot.state != IncarnationState::ReservedUnexposed) return false;
    slot.state = IncarnationState::Free;
    return true;
}
template<std::size_t Index>
void IngressDiagnosticBank::OnBytes(std::int32_t socket, const void* data, std::uint32_t length) noexcept {
    Ingress(Index, socket, data, length);
}
PublicBytesCallback IngressDiagnosticBank::Expose(const NativeTableToken& token, std::int32_t socket) {
    static constexpr std::array<PublicBytesCallback, ingress_diagnostic_slots> callbacks{{
        &OnBytes<0>, &OnBytes<1>, &OnBytes<2>, &OnBytes<3>, &OnBytes<4>, &OnBytes<5>, &OnBytes<6>, &OnBytes<7>}};
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    if (!token || token.slot_ >= state.slots.size() || socket < 0) return nullptr;
    auto& slot = state.slots[token.slot_];
    if (slot.reservation != token.reservation_ || slot.state != IncarnationState::ReservedUnexposed) return nullptr;
    slot.budget = std::make_shared<ingress_detail::Budget>();
    slot.socket = socket;
    slot.state = IncarnationState::Exposed;
    ++state.stats.exposed;
    // Returning this pointer counts as exposure even if subsequent native
    // endpoint creation fails. A caller must retire it, never abandon/rebind.
    return callbacks[token.slot_];
}
bool IngressDiagnosticBank::Retire(const NativeTableToken& token) {
    std::shared_ptr<ingress_detail::Application> retired_application;
    std::deque<CapturedIngressRecord> retired_records;
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (!token || token.slot_ >= state.slots.size()) return false;
        auto& slot = state.slots[token.slot_];
        if (slot.reservation != token.reservation_ || slot.state != IncarnationState::Exposed) return false;
        slot.state = IncarnationState::Tombstoned;
        if (slot.application) slot.application->detached.store(true, std::memory_order_release);
        retired_application = std::move(slot.application);
        retired_records.swap(slot.records);
        ++state.stats.tombstoned;
    }
    // Releasing the last lease may run an application receiver destructor.
    // That too must occur outside the adapter mutex. Already-taken records
    // retain their original lease and are unaffected by current attachment.
    return true;
}
DiagnosticAttachment IngressDiagnosticBank::Attach(const NativeTableToken& token,
        std::shared_ptr<DiagnosticReceiver> receiver) {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    DiagnosticAttachment attachment;
    if (!token || token.slot_ >= state.slots.size() || !receiver) return attachment;
    auto& slot = state.slots[token.slot_];
    if (slot.reservation != token.reservation_ || slot.state != IncarnationState::Exposed || slot.application) return attachment;
    auto application = std::make_shared<ingress_detail::Application>();
    application->receiver = std::move(receiver);
    slot.application = application;
    attachment.application_ = std::move(application);
    return attachment;
}
bool IngressDiagnosticBank::BeginDetach(const NativeTableToken& token, const DiagnosticAttachment& attachment) {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    if (!token || token.slot_ >= state.slots.size() || !attachment) return false;
    auto& slot = state.slots[token.slot_];
    if (slot.reservation != token.reservation_ || slot.state != IncarnationState::Exposed ||
            slot.application != attachment.application_) return false;
    attachment.application_->detached.store(true, std::memory_order_release);
    slot.application.reset();
    return true;
}
bool IngressDiagnosticBank::DetachComplete(const DiagnosticAttachment& attachment) const {
    return attachment && attachment.application_->detached.load(std::memory_order_acquire) &&
           attachment.application_->outstanding.load(std::memory_order_acquire) == 0;
}
CapturedIngressRecord IngressDiagnosticBank::Take(const NativeTableToken& token) {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    CapturedIngressRecord record;
    if (!token || token.slot_ >= state.slots.size()) return record;
    auto& slot = state.slots[token.slot_];
    if (slot.reservation != token.reservation_ || slot.state != IncarnationState::Exposed || slot.records.empty()) return record;
    record = std::move(slot.records.front()); slot.records.pop_front();
    return record;
}
IngressDiagnosticStats IngressDiagnosticBank::Stats() const {
    auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
    auto stats = state.stats;
    for (const auto& slot : state.slots) {
        if (slot.budget) stats.outstanding += slot.budget->outstanding.load(std::memory_order_acquire);
    }
    return stats;
}
void IngressDiagnosticBank::Ingress(std::size_t index, std::int32_t socket,
        const void* data, std::uint32_t length) noexcept {
    try {
        auto& state = State(); std::lock_guard<std::mutex> lock(state.mutex);
        auto& slot = state.slots[index]; // only called by eight fixed trampolines
        if (slot.state != IncarnationState::Exposed) { ++state.stats.dropped_retired; return; }
        if (socket != slot.socket) { ++state.stats.dropped_unknown_socket; return; }
        if (!data || length == 0 || length > ingress_diagnostic_message_bytes) { ++state.stats.dropped_invalid; return; }
        if (!slot.application) { ++state.stats.dropped_no_application; return; }
        if (slot.budget->outstanding.load(std::memory_order_acquire) >= ingress_diagnostic_records) {
            ++state.stats.dropped_budget; return;
        }
        try {
            CapturedIngressRecord record;
            // Attribution linearization: while this SAME mutex excludes
            // detach/reattach, capture the current application and acquire its
            // immutable receiver lease with the payload. This describes only
            // adapter ingress; it says nothing about an SDK-internal event's
            // earlier native/application identity.
            record.captured_ = std::make_unique<ingress_detail::Captured>(slot.application,
                    slot.budget, socket, static_cast<const std::uint8_t*>(data), length);
            slot.records.push_back(std::move(record));
            ++state.stats.captured;
        } catch (...) { ++state.stats.dropped_budget; }
    } catch (...) {
        // Public callback exception barrier. Diagnostic callbacks perform no
        // SDK calls, native closing, adapter retries or late re-attribution.
    }
}
} // namespace mdds_probe
