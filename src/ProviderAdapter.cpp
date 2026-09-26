#include <mdds/dsoftbus/ProviderAdapter.hpp>
#include <mdds/dsoftbus/NativeSendExecution.hpp>
#include <stdexcept>
#include <array>
#include <condition_variable>
#include <limits>
#include <mutex>

namespace mdds::dsoftbus::phase0 {
namespace detail {
struct ProviderDomainState {
    std::mutex gate;
    std::array<std::weak_ptr<ProviderAdapterState>, record_slot_capacity> entries;
};
struct ProviderAdapterState final : ServiceRecordProvider, NativeSendProvider, std::enable_shared_from_this<ProviderAdapterState> {
    ProviderAdapterState(ServiceRecordTable& r, NativeOwnerTable& n, ApplicationAttachments& a,
            std::shared_ptr<NativeBackend> p, BindingRequest q, BackendIdentity i)
        : records(r), native(n), attachments(a), backend(std::move(p)), request(std::move(q)), identity(std::move(i)) {}
    ServiceRecordTable& records;
    NativeOwnerTable& native;
    ApplicationAttachments& attachments;
    const std::shared_ptr<NativeBackend> backend;
    BindingRequest request; // receiver is cleared/replaced only by RX control.
    const BackendIdentity identity;
    mutable std::mutex gate;
    std::condition_variable idle;
    AdapterPhase phase = AdapterPhase::Constructed;
    bool stopped = false, creating = false, pending_stop_conflict = false;
    bool input_open = false;
    std::optional<std::int32_t> pending_stop;
    std::int32_t socket = -1;
    std::size_t active = 0, rejected = 0, exceptions = 0;
    std::size_t dispatching = 0;
    struct DispatchActivity {
        ProviderAdapterState& state; bool armed=false;
        ~DispatchActivity(){if(armed){std::lock_guard<std::mutex> lock(state.gate);--state.dispatching;state.idle.notify_all();}}
    };
    std::size_t send_calls = 0;
    std::int32_t last_priority = 0;
    BackendStatus provider_status = BackendStatus::None;
    // Destroyed first, while its ServiceRecordProvider bridge and backend live.
    std::unique_ptr<ServiceRecordBinding> binding;

    void Fault(BackendStatus status) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        provider_status = status;
        if (status == BackendStatus::ProviderException) ++exceptions;
    }
    NativeSocketResult CreateSocket() noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            creating = true;
        }
        try {
            auto result = backend->Create(ProviderCallbacks{weak_from_this()});
            if (result.status == BackendStatus::None && result.socket && *result.socket >= 0) {
                std::lock_guard<std::mutex> lock(gate);
                creating = false;
                socket = *result.socket;
                if (pending_stop_conflict) {
                    stopped = true;
                    provider_status = BackendStatus::InvalidIdentity;
                } else if (pending_stop) {
                    if (*pending_stop == socket) stopped = true;
                    else ++rejected; // foreign candidate never closes an owner
                }
                pending_stop.reset();
                return NativeSocketResult::Id(socket);
            }
            Fault(result.status == BackendStatus::None ? BackendStatus::InvalidIdentity : result.status);
        } catch (...) { Fault(BackendStatus::ProviderException); }
        {
            std::lock_guard<std::mutex> lock(gate);
            creating = false;
            pending_stop.reset();
        }
        // Legacy model bridge requires a negative failure sentinel. It is never
        // exported as an SDK rc: provider_status carries the actual adapter fault.
        return NativeSocketResult::Error(-1);
    }
    SyncListenResult Listen(NativeSocketId id) noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            if (stopped) return SyncListenResult::Error(-1); // provider NotCalled
        }
        try {
            auto result = backend->Listen(id);
            if (result == BackendStatus::None) return SyncListenResult::Success();
            Fault(result);
        } catch (...) { Fault(BackendStatus::ProviderException); }
        return SyncListenResult::Error(-1);
    }
    SyncBindResult Bind(NativeSocketId id) noexcept override {
        {
            std::lock_guard<std::mutex> lock(gate);
            if (stopped) return SyncBindResult::Error(-1); // provider NotCalled
        }
        try {
            auto result = backend->Bind(id);
            if (result == BackendStatus::None) return SyncBindResult::Success();
            Fault(result);
        } catch (...) { Fault(BackendStatus::ProviderException); }
        return SyncBindResult::Error(-1);
    }
    void Shutdown(NativeSocketId id) noexcept override {
        try {
            auto result = backend->Shutdown(id);
            if (result != BackendStatus::None) Fault(result);
        } catch (...) { Fault(BackendStatus::ProviderException); }
    }
    std::int32_t Send(NativeSocketId id, const Route& route, const std::uint8_t* data,
            std::size_t size, SendQueue::Clock::time_point deadline, std::int32_t priority) override {
        // Reachable only from the controlled Host loop's admitted invocation.
        // Closing permits an invocation that won native entry before revocation;
        // the controller waits for that entire invocation before binding close.
        {
            std::lock_guard<std::mutex> lock(gate);
            if ((phase != AdapterPhase::Published && phase != AdapterPhase::Closing) || stopped ||
                    id != socket || route.key.local_locator != request.route.local_locator ||
                    route.key.remote_locator != request.route.remote_locator)
                throw std::runtime_error("Host provider identity/state rejected");
            ++send_calls; last_priority = priority;
        }
        try {
            const auto result = backend->Send(id, data, size, deadline);
            if (result.status != BackendStatus::None || !result.provider_return)
                throw std::runtime_error("Host provider returned no send result");
            return *result.provider_return;
        } catch (...) { Fault(BackendStatus::ProviderException); throw; }
    }
    BackendStatus Bytes(std::int32_t id, const void* data, std::uint32_t size) noexcept {
        ServiceRecordBinding* original = nullptr;
        {
            std::lock_guard<std::mutex> lock(gate);
            auto reject = [&](BackendStatus s) { ++rejected; return s; };
            if (stopped) return reject(BackendStatus::ProviderStopped);
            if (phase != AdapterPhase::Published) return reject(BackendStatus::InvalidState);
            if (!input_open) return reject(BackendStatus::ChannelClosed);
            if (id != socket) return reject(BackendStatus::ForeignSocket);
            if (!data || !size) return reject(BackendStatus::InvalidBuffer);
            if (active == std::numeric_limits<std::size_t>::max()) return reject(BackendStatus::Capacity);
            original = binding.get();
            ++active;
        }
        // The callback's shared State pin and Close's ingress barrier retain
        // exactly this binding. No lookup of a newer application is performed.
        original->Entrance()(id, data, size);
        {
            std::lock_guard<std::mutex> lock(gate);
            --active;
        }
        idle.notify_all();
        // Forwarded into binding, not a claim of receiver delivery/acceptance.
        return BackendStatus::None;
    }
    BackendStatus Stop(std::int32_t id) noexcept {
        std::lock_guard<std::mutex> lock(gate);
        if (phase == AdapterPhase::Closed) return BackendStatus::InvalidState;
        if (creating && socket < 0 && id >= 0) {
            // One bounded candidate for this Create call, not a native generation
            // claim. Reconcile with its return before any binding publication.
            if (pending_stop && *pending_stop != id) pending_stop_conflict = true;
            else if (!pending_stop) pending_stop = id;
            return BackendStatus::PendingIdentity;
        }
        if (socket < 0 || id != socket) return BackendStatus::ForeignSocket;
        stopped = true;
        // No binding operation here: setup may still hold Binding::State::gate.
        return BackendStatus::None;
    }
    BackendCloseResult Close() noexcept {
        {
            std::unique_lock<std::mutex> lock(gate);
            if (phase == AdapterPhase::Closed) return {provider_status, true};
            phase = AdapterPhase::Closing;
            idle.wait(lock, [&] { return active == 0; });
        }
        if (binding) {
            const auto before = binding->Observe();
            if (before.phase != BindingPhase::Empty) {
                const auto status = binding->Abandon();
                if (status != NativeStatus::None && status != NativeStatus::AlreadySettled)
                    return {BackendStatus::BindingRejected, false};
            }
            if (!binding->DrainComplete()) return {BackendStatus::BindingRejected, false};
        }
        std::lock_guard<std::mutex> lock(gate);
        phase = AdapterPhase::Closed;
        input_open = false; request.receiver = nullptr;
        // Adapter resources may settle even when provider shutdown fails. Never
        // translate that into native drain, successful cancellation or retirement.
        return {provider_status, true};
    }
};
} // namespace detail

ProviderAdapterDomain::ProviderAdapterDomain() : state_(std::make_shared<detail::ProviderDomainState>()) {}
ProviderAdapter::ProviderAdapter(std::shared_ptr<detail::ProviderAdapterState> s) noexcept : state_(std::move(s)) {}
ProviderAdapter::~ProviderAdapter() { if (state_ && !state_->Close().adapter_drained) std::terminate(); }
AdapterBuildResult ProviderAdapter::Build(ProviderAdapterDomain& domain, ServiceRecordTable& records,
        NativeOwnerTable& native, ApplicationAttachments& attachments, std::shared_ptr<NativeBackend> backend,
        const BindingRequest& request) {
    if (!backend || backend->Kind() != BackendKind::HostMock)
        return {BackendStatus::NativeBackendUnavailable, nullptr};
    BackendIdentity identity;
    try { identity = backend->Identity(); }
    catch (...) { return {BackendStatus::ProviderException, nullptr}; }
    if (!identity.record.value || !identity.owner.value || identity.service_name.empty() ||
            identity.service_name != request.service_name)
        return {BackendStatus::InvalidIdentity, nullptr};
    if (request.existing || request.record_owner || request.is_accepted)
        return {BackendStatus::InvalidState, nullptr}; // not this phase's topology
    auto state = std::make_shared<detail::ProviderAdapterState>(records, native, attachments,
            std::move(backend), request, std::move(identity));
    {
        std::lock_guard<std::mutex> lock(domain.state_->gate);
        std::weak_ptr<detail::ProviderAdapterState>* free = nullptr;
        for (auto& entry : domain.state_->entries) {
            if (auto other = entry.lock()) {
                if (other->identity.record.value == state->identity.record.value ||
                        other->identity.service_name == state->identity.service_name)
                    return {BackendStatus::DuplicateRecord, nullptr};
            } else if (!free) free = &entry;
        }
        if (!free) return {BackendStatus::Capacity, nullptr};
        *free = state;
    }
    return {BackendStatus::None, std::unique_ptr<ProviderAdapter>(new ProviderAdapter(std::move(state)))};
}
BackendStatus ProviderAdapter::Initialize() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->phase != AdapterPhase::Constructed) return BackendStatus::InvalidState;
        state_->phase = AdapterPhase::Deploying;
    }
    auto built = ServiceRecordBinding::Build(state_->records, state_->native, state_->attachments,
            *state_, state_->request);
    NativeStatus status = built.issue.code;
    if (built) {
        state_->binding = std::move(*built.value);
        status = state_->binding->Prepare();
        if (status == NativeStatus::None) status = state_->binding->Connect();
    }
    std::lock_guard<std::mutex> lock(state_->gate);
    state_->phase = status == NativeStatus::None ? AdapterPhase::Connected : AdapterPhase::Failed;
    if (state_->stopped) return BackendStatus::ProviderStopped;
    if (state_->provider_status != BackendStatus::None) return state_->provider_status;
    return status == NativeStatus::None ? BackendStatus::None : BackendStatus::BindingRejected;
}
BackendStatus ProviderAdapter::Publish() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->stopped) return BackendStatus::ProviderStopped;
        if (state_->phase != AdapterPhase::Connected) return BackendStatus::InvalidState;
    }
    const auto status = state_->binding->Publish();
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->stopped) return BackendStatus::ProviderStopped;
    if (status != NativeStatus::None) { state_->phase = AdapterPhase::Failed; return BackendStatus::BindingRejected; }
    state_->phase = AdapterPhase::Published;
    state_->input_open = true;
    return BackendStatus::None;
}
BackendStatus ProviderAdapter::DispatchOne() noexcept {
    detail::ProviderAdapterState::DispatchActivity activity{*state_};
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if (state_->stopped) return BackendStatus::ProviderStopped;
        if (state_->phase != AdapterPhase::Published) return BackendStatus::InvalidState;
        if (!state_->input_open) return BackendStatus::ChannelClosed;
        ++state_->dispatching; activity.armed=true;
    }
    auto work = state_->binding->NextAdmitted();
    if (!work) return BackendStatus::NoWork;
    const auto result = state_->binding->RunAdmitted(*work.value);
    if (result == InputDeliveryResult::CallbackFailure) return BackendStatus::CallbackFailure;
    return result == InputDeliveryResult::Delivered ? BackendStatus::None : BackendStatus::BindingRejected;
}
BackendStatus ProviderAdapter::CloseInput() noexcept {
    bool was_open=false;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        was_open=state_->input_open;state_->input_open=false;
    }
    const auto result=state_->binding?state_->binding->CloseInput():NativeStatus::None;
    if(result!=NativeStatus::None) {
        std::lock_guard<std::mutex> lock(state_->gate);state_->input_open=was_open;
        return BackendStatus::BindingRejected;
    }
    std::unique_lock<std::mutex> lock(state_->gate);
    state_->idle.wait(lock,[&]{return state_->active==0&&state_->dispatching==0;});
    state_->request.receiver=nullptr;
    return BackendStatus::None;
}
BackendStatus ProviderAdapter::ReopenInput(InputChannels::Receiver* receiver) noexcept {
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(state_->stopped)return BackendStatus::ProviderStopped;
        if(state_->phase!=AdapterPhase::Published||state_->input_open||!receiver)return BackendStatus::InvalidState;
    }
    const auto result=state_->binding->ReopenInput(receiver);
    if(result!=NativeStatus::None)return result==NativeStatus::AllocationFailure?BackendStatus::AllocationFailure:BackendStatus::BindingRejected;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        if(!state_->stopped&&state_->phase==AdapterPhase::Published) {
            state_->request.receiver=receiver;state_->input_open=true;return BackendStatus::None;
        }
    }
    (void)state_->binding->CloseInput(); // stop during reopen: no half-published receiver
    return BackendStatus::ProviderStopped;
}
BackendCloseResult ProviderAdapter::Close() noexcept { return state_->Close(); }
AdapterObservation ProviderAdapter::Observe() const noexcept {
    AdapterObservation result;
    {
        std::lock_guard<std::mutex> lock(state_->gate);
        result.phase = state_->phase; result.provider_stopped = state_->stopped;
        result.input_open = state_->input_open;
        result.ingress_active = state_->active; result.ingress_rejected = state_->rejected;
        result.provider_exceptions = state_->exceptions; result.provider_status = state_->provider_status;
        result.send_calls = state_->send_calls; result.last_priority = state_->last_priority;
    }
    if (state_->binding) result.binding = state_->binding->Observe();
    return result;
}
BackendStatus ProviderCallbacks::OnBytes(std::int32_t id, const void* data, std::uint32_t size) const noexcept {
    auto state = state_.lock();
    return state ? state->Bytes(id, data, size) : BackendStatus::InvalidState;
}
BackendStatus ProviderCallbacks::OnShutdown(std::int32_t id) const noexcept {
    auto state = state_.lock();
    return state ? state->Stop(id) : BackendStatus::InvalidState;
}
std::shared_ptr<NativeSendProvider> ProviderAdapter::BindSendProvider(NativeOwnerTable& table) const noexcept {
    return &table == &state_->native ? std::static_pointer_cast<NativeSendProvider>(state_) : nullptr;
}
void ProviderAdapter::SealIngress() noexcept {
    std::lock_guard<std::mutex> lock(state_->gate);
    if (state_->phase == AdapterPhase::Published) state_->phase = AdapterPhase::Closing;
}
}
